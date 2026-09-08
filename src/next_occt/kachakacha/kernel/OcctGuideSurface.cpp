#include "kachakacha/kernel/OcctGuideSurface.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <Extrema_ExtFlag.hxx>
#include <GeomAbs_Shape.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#endif

namespace kachakacha::v2::kernel {

using base::Diagnostic;
using base::MakeError;
using base::Result;
using geometry::GeometryTolerance;
using geometry::Vector3;
using modeling::ChainRole;
using modeling::GuideChain;
using modeling::GuideSurfaceAnalysis;
using modeling::GuideSurfaceMethod;
using modeling::GuideSurfaceRequest;
using modeling::GuideSurfaceResult;
using modeling::KernelShapeHandle;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! OCCT の例外を外へ出さないための包み。
template<class Function>
[[nodiscard]] auto Guarded(Function&& body, const char* what) -> decltype(body())
{
    using ResultType = decltype(body());
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return ResultType::Failure(MakeError(kSurfaceBuildFailed,
            "幾何カーネルが面を作れませんでした。",
            std::string(what) + ": " + std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return ResultType::Failure(MakeError(kSurfaceBuildFailed,
            "幾何カーネルが面を作れませんでした。",
            std::string(what) + ": " + error.what()));
    } catch (...) {
        return ResultType::Failure(MakeError(kSurfaceBuildFailed,
            "幾何カーネルが面を作れませんでした。", what));
    }
}

[[nodiscard]] std::vector<std::size_t> IndicesWithRole(const GuideSurfaceRequest& request,
    ChainRole role)
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        if (request.chains[index].role == role) {
            indices.push_back(index);
        }
    }
    return indices;
}

[[nodiscard]] double SamplingToleranceMm(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.modelLinearMm * 10.0, 1.0e-5);
}

[[nodiscard]] Result<TopoDS_Wire> WireOf(const GuideSurfaceRequest& request,
    std::size_t chainIndex, const GeometryTolerance& tolerance)
{
    return ToWire(request.chains[chainIndex].segments, tolerance.modelLinearMm);
}

// ---------------------------------------------------------------- 作り方ごと

[[nodiscard]] Result<TopoDS_Shape> BuildPlanar(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    if (!analysis.planeFit.valid) {
        return Out::Failure(MakeError(kSurfaceBuildFailed,
            "平面が決まっていません。", {}));
    }
    const gp_Pln plane(ToPoint(analysis.planeFit.origin),
        gp_Dir(ToVector(analysis.planeFit.normal)));

    std::vector<TopoDS_Face> faces;
    for (const auto& loop : analysis.planarLoops) {
        if (loop.isHole) {
            continue;
        }
        auto outer = WireOf(request, loop.chainIndex, tolerance);
        if (!outer.HasValue()) {
            return Out::Failure(outer.Diagnostics());
        }
        BRepBuilderAPI_MakeFace maker(plane, outer.Value(), Standard_True);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "外周から面を作れませんでした。", {}));
        }
        for (const std::size_t holeIndex : loop.holes) {
            auto hole = WireOf(request, holeIndex, tolerance);
            if (!hole.HasValue()) {
                return Out::Failure(hole.Diagnostics());
            }
            TopoDS_Wire holeWire = hole.Value();
            holeWire.Reverse();
            maker.Add(holeWire);
            if (!maker.IsDone()) {
                return Out::Failure(MakeError(kSurfaceBuildFailed,
                    "穴を面から抜けませんでした。", {}));
            }
        }
        faces.push_back(maker.Face());
    }
    if (faces.empty()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "外周がありません。", {}));
    }
    if (faces.size() == 1) {
        return Out::Success(TopoDS_Shape(faces.front()));
    }
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const TopoDS_Face& face : faces) {
        builder.Add(compound, face);
    }
    return Out::Success(TopoDS_Shape(compound));
}

[[nodiscard]] Result<TopoDS_Shape> BuildThruSections(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance, bool ruled)
{
    using Out = Result<TopoDS_Shape>;
    std::vector<std::size_t> order = analysis.sectionOrdering.chainIndices;
    if (order.empty()) {
        order = IndicesWithRole(request, ChainRole::Section);
    }
    if (order.size() < 2) {
        return Out::Failure(MakeError(kSurfaceBuildFailed,
            "断面が2つ以上必要です。", {}));
    }
    return Guarded([&]() -> Out {
        BRepOffsetAPI_ThruSections generator(Standard_False,
            ruled ? Standard_True : Standard_False,
            std::max(tolerance.modelLinearMm, Precision::Confusion()));
        for (const std::size_t index : order) {
            auto wire = WireOf(request, index, tolerance);
            if (!wire.HasValue()) {
                return Out::Failure(wire.Diagnostics());
            }
            generator.AddWire(wire.Value());
        }
        generator.Build();
        if (!generator.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "断面から面を作れませんでした。",
                "断面の向きか、辺の数の対応を確かめてください。"));
        }
        return Out::Success(generator.Shape());
    }, ruled ? "直線でつなぐ面" : "なめらかにつなぐ面");
}

[[nodiscard]] Result<TopoDS_Shape> BuildGuidedLoft(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    const std::vector<std::size_t> guides = IndicesWithRole(request, ChainRole::GuideU);
    std::vector<std::size_t> sections = analysis.sectionOrdering.chainIndices;
    if (sections.empty()) {
        sections = IndicesWithRole(request, ChainRole::Section);
    }
    if (guides.empty()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "案内線がありません。", {}));
    }
    if (sections.size() < 2) {
        return Out::Failure(MakeError(kSurfaceBuildFailed,
            "断面が2つ以上必要です。", {}));
    }
    return Guarded([&]() -> Out {
        auto spine = WireOf(request, guides.front(), tolerance);
        if (!spine.HasValue()) {
            return Out::Failure(spine.Diagnostics());
        }
        BRepOffsetAPI_MakePipeShell shell(spine.Value());
        if (guides.size() >= 2) {
            auto auxiliary = WireOf(request, guides[1], tolerance);
            if (!auxiliary.HasValue()) {
                return Out::Failure(auxiliary.Diagnostics());
            }
            shell.SetMode(auxiliary.Value(), Standard_True);
        } else {
            shell.SetMode(Standard_True);   // Frenet
        }
        for (const std::size_t index : sections) {
            auto wire = WireOf(request, index, tolerance);
            if (!wire.HasValue()) {
                return Out::Failure(wire.Diagnostics());
            }
            shell.Add(wire.Value(), Standard_False, Standard_True);
        }
        shell.Build();
        if (!shell.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "案内線に沿った面を作れませんでした。",
                "案内線と断面の交わり方を確かめてください。"));
        }
        return Out::Success(shell.Shape());
    }, "案内線に沿った面");
}

//! 曲線を点として拘束に足すときの点数。多すぎると解けなくなる。
constexpr int kNetworkPointsPerChain = 9;

[[nodiscard]] Result<TopoDS_Shape> BuildFilling(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance,
    bool boundaryFill)
{
    using Out = Result<TopoDS_Shape>;
    return Guarded([&]() -> Out {
        const double tol3d = std::max(tolerance.modelLinearMm, Precision::Confusion());
        BRepOffsetAPI_MakeFilling filler(3, 15, 2, Standard_False, 1.0e-5, tol3d,
            0.01, 0.1, 8, 9);

        bool addedBoundary = false;
        if (boundaryFill) {
            std::vector<std::size_t> ring = analysis.sectionOrdering.chainIndices;
            if (ring.empty()) {
                ring = IndicesWithRole(request, ChainRole::BoundarySide);
            }
            for (std::size_t at = 0; at < ring.size(); ++at) {
                const bool tangent = at < request.tangentContinuity.size()
                    && request.tangentContinuity[at];
                for (const auto& segment : request.chains[ring[at]].segments) {
                    auto edge = ToEdge(segment);
                    if (!edge.HasValue()) {
                        return Out::Failure(edge.Diagnostics());
                    }
                    filler.Add(edge.Value(), tangent ? GeomAbs_G1 : GeomAbs_C0);
                    addedBoundary = true;
                }
            }
        } else {
            // 曲線網。外周になる線を境界として、残りは点で拘束する。
            // OCCT に Gordon 面は無い。だから作ったあとで必ず測り、
            // 入力を通っていなければ捨てる(この関数の呼び出し元)。
            const std::vector<std::size_t> uChains =
                IndicesWithRole(request, ChainRole::GuideU);
            const std::vector<std::size_t> vChains =
                IndicesWithRole(request, ChainRole::GuideV);
            if (uChains.size() < 2 || vChains.size() < 2) {
                return Out::Failure(MakeError(kSurfaceBuildFailed,
                    "U方向・V方向の線が足りません。", {}));
            }
            const auto addAsBoundary = [&](std::size_t index) -> Result<bool> {
                for (const auto& segment : request.chains[index].segments) {
                    auto edge = ToEdge(segment);
                    if (!edge.HasValue()) {
                        return Result<bool>::Failure(edge.Diagnostics());
                    }
                    filler.Add(edge.Value(), GeomAbs_C0);
                }
                return Result<bool>::Success(true);
            };
            for (const std::size_t index :
                {uChains.front(), uChains.back(), vChains.front(), vChains.back()}) {
                auto added = addAsBoundary(index);
                if (!added.HasValue()) {
                    return Out::Failure(added.Diagnostics());
                }
                addedBoundary = true;
            }
            const double samplingTolerance = SamplingToleranceMm(tolerance);
            const auto addAsPoints = [&](const std::vector<std::size_t>& list,
                                         std::size_t skipFirst, std::size_t skipLast) {
                for (std::size_t at = 0; at < list.size(); ++at) {
                    if (at == skipFirst || at == skipLast) {
                        continue;
                    }
                    const std::vector<Vector3> points = geometry::SampleChain(
                        request.chains[list[at]].segments, samplingTolerance);
                    if (points.size() < 2) {
                        continue;
                    }
                    for (int step = 0; step < kNetworkPointsPerChain; ++step) {
                        const double ratio = static_cast<double>(step)
                            / static_cast<double>(kNetworkPointsPerChain - 1);
                        const std::size_t at2 = static_cast<std::size_t>(
                            ratio * static_cast<double>(points.size() - 1) + 0.5);
                        filler.Add(ToPoint(points[at2]));
                    }
                }
            };
            addAsPoints(uChains, 0, uChains.size() - 1);
            addAsPoints(vChains, 0, vChains.size() - 1);
        }
        if (!addedBoundary) {
            return Out::Failure(MakeError(kSurfaceBuildFailed, "境界がありません。", {}));
        }
        filler.Build();
        if (!filler.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "境界から面を張れませんでした。", {}));
        }
        return Out::Success(filler.Shape());
    }, boundaryFill ? "境界から張る面" : "曲線網から張る面");
}

//! 解析的な面は、解析的なまま外側へずらす。
//! Geom_OffsetSurface に逃げると円筒が円筒でなくなり、厳密展開ができなくなる。
[[nodiscard]] Result<occ::handle<Geom_Surface>> OffsetSurfaceOf(
    const TopoDS_Face& face, double distance)
{
    using Out = Result<occ::handle<Geom_Surface>>;
    BRepAdaptor_Surface adaptor(face, Standard_True);
    const double sign = face.Orientation() == TopAbs_REVERSED ? -1.0 : 1.0;
    const double signedDistance = distance * sign;
    switch (adaptor.GetType()) {
    case GeomAbs_Plane: {
        gp_Pln plane = adaptor.Plane();
        const gp_Dir normal = plane.Axis().Direction();
        plane.Translate(gp_Vec(normal) * signedDistance);
        return Out::Success(occ::handle<Geom_Surface>(new Geom_Plane(plane)));
    }
    case GeomAbs_Cylinder: {
        const gp_Cylinder cylinder = adaptor.Cylinder();
        const double radius = cylinder.Radius() + signedDistance;
        if (!(radius > Precision::Confusion())) {
            return Out::Failure(MakeError(kSurfaceOffsetImpossible,
                "その距離だと円筒がつぶれます。",
                "半径 " + std::to_string(cylinder.Radius()) + " mm に対して "
                    + std::to_string(distance) + " mm。"));
        }
        return Out::Success(occ::handle<Geom_Surface>(
            new Geom_CylindricalSurface(cylinder.Position(), radius)));
    }
    case GeomAbs_Cone: {
        const gp_Cone cone = adaptor.Cone();
        const double cosine = std::cos(cone.SemiAngle());
        if (std::abs(cosine) <= 1.0e-12) {
            return Out::Failure(MakeError(kSurfaceOffsetImpossible,
                "円錐の角度が極端で、ずらせません。", {}));
        }
        const double refRadius = cone.RefRadius() + signedDistance / cosine;
        if (!(refRadius > -Precision::Confusion())) {
            return Out::Failure(MakeError(kSurfaceOffsetImpossible,
                "その距離だと円錐がつぶれます。", {}));
        }
        return Out::Success(occ::handle<Geom_Surface>(
            new Geom_ConicalSurface(cone.Position(), cone.SemiAngle(), refRadius)));
    }
    case GeomAbs_Sphere: {
        const gp_Sphere sphere = adaptor.Sphere();
        const double radius = sphere.Radius() + signedDistance;
        if (!(radius > Precision::Confusion())) {
            return Out::Failure(MakeError(kSurfaceOffsetImpossible,
                "その距離だと球がつぶれます。", {}));
        }
        return Out::Success(occ::handle<Geom_Surface>(
            new Geom_SphericalSurface(sphere.Position(), radius)));
    }
    case GeomAbs_Torus: {
        const gp_Torus torus = adaptor.Torus();
        const double minor = torus.MinorRadius() + signedDistance;
        if (!(minor > Precision::Confusion())) {
            return Out::Failure(MakeError(kSurfaceOffsetImpossible,
                "その距離だとトーラスがつぶれます。", {}));
        }
        return Out::Success(occ::handle<Geom_Surface>(
            new Geom_ToroidalSurface(torus.Position(), torus.MajorRadius(), minor)));
    }
    default:
        break;
    }
    occ::handle<Geom_Surface> base = BRep_Tool::Surface(face);
    if (base.IsNull()) {
        return Out::Failure(MakeError(kSurfaceOffsetImpossible,
            "元の面が取れませんでした。", {}));
    }
    return Out::Success(occ::handle<Geom_Surface>(
        new Geom_OffsetSurface(base, signedDistance)));
}

[[nodiscard]] Result<TopoDS_Shape> BuildOffset(const GuideSurfaceRequest& request,
    KernelShapeHandle sourceShape, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    TopoDS_Shape source;
    if (!sourceShape.Valid() || !LookupShape(sourceShape, source)) {
        return Out::Failure(MakeError(kSurfaceSourceMissing,
            "元にする形状ガイドが見つかりません。",
            "先に元の面を作り直してください。"));
    }
    return Guarded([&]() -> Out {
        std::vector<TopoDS_Face> faces;
        for (TopExp_Explorer explorer(source, TopAbs_FACE); explorer.More();
            explorer.Next()) {
            faces.push_back(TopoDS::Face(explorer.Current()));
        }
        if (faces.empty()) {
            return Out::Failure(MakeError(kSurfaceSourceMissing,
                "元の形状ガイドに面がありません。", {}));
        }
        std::vector<TopoDS_Face> made;
        for (const TopoDS_Face& face : faces) {
            auto surface = OffsetSurfaceOf(face, request.offsetDistanceMm);
            if (!surface.HasValue()) {
                return Out::Failure(surface.Diagnostics());
            }
            double u0 = 0.0;
            double u1 = 0.0;
            double v0 = 0.0;
            double v1 = 0.0;
            BRepTools::UVBounds(face, u0, u1, v0, v1);
            BRepBuilderAPI_MakeFace maker(surface.Value(), u0, u1, v0, v1,
                std::max(tolerance.modelLinearMm, Precision::Confusion()));
            if (!maker.IsDone()) {
                return Out::Failure(MakeError(kSurfaceOffsetImpossible,
                    "ずらした面を作れませんでした。", {}));
            }
            made.push_back(maker.Face());
        }
        if (made.size() == 1) {
            return Out::Success(TopoDS_Shape(made.front()));
        }
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        for (const TopoDS_Face& face : made) {
            builder.Add(compound, face);
        }
        return Out::Success(TopoDS_Shape(compound));
    }, "面をずらす");
}

// ---------------------------------------------------------------- 出来た面を測る

[[nodiscard]] TopoDS_Face FirstFace(const TopoDS_Shape& shape)
{
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        return TopoDS::Face(explorer.Current());
    }
    return TopoDS_Face();
}

[[nodiscard]] std::size_t FaceCount(const TopoDS_Shape& shape)
{
    std::size_t count = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        ++count;
    }
    return count;
}

[[nodiscard]] fabrication::AnalyticSurfaceInfo AnalyticOf(const TopoDS_Face& face)
{
    fabrication::AnalyticSurfaceInfo info;
    if (face.IsNull()) {
        return info;
    }
    BRepAdaptor_Surface adaptor(face, Standard_True);
    const auto fill = [&](const gp_Ax3& axis) {
        info.origin = FromPoint(axis.Location());
        info.axis = Vector3{axis.Direction().X(), axis.Direction().Y(),
            axis.Direction().Z()};
        info.reference = Vector3{axis.XDirection().X(), axis.XDirection().Y(),
            axis.XDirection().Z()};
    };
    switch (adaptor.GetType()) {
    case GeomAbs_Plane: {
        const gp_Pln plane = adaptor.Plane();
        info.kind = fabrication::AnalyticSurfaceKind::Plane;
        fill(plane.Position());
        break;
    }
    case GeomAbs_Cylinder: {
        const gp_Cylinder cylinder = adaptor.Cylinder();
        info.kind = fabrication::AnalyticSurfaceKind::Cylinder;
        fill(cylinder.Position());
        info.radiusMm = cylinder.Radius();
        break;
    }
    case GeomAbs_Cone: {
        const gp_Cone cone = adaptor.Cone();
        info.kind = fabrication::AnalyticSurfaceKind::Cone;
        fill(cone.Position());
        info.radiusMm = cone.RefRadius();
        info.halfAngleRad = cone.SemiAngle();
        break;
    }
    case GeomAbs_Sphere: {
        const gp_Sphere sphere = adaptor.Sphere();
        info.kind = fabrication::AnalyticSurfaceKind::Sphere;
        fill(sphere.Position());
        info.radiusMm = sphere.Radius();
        break;
    }
    case GeomAbs_Torus: {
        const gp_Torus torus = adaptor.Torus();
        info.kind = fabrication::AnalyticSurfaceKind::Torus;
        fill(torus.Position());
        info.radiusMm = torus.MajorRadius();
        info.secondaryRadiusMm = torus.MinorRadius();
        break;
    }
    default:
        info.kind = fabrication::AnalyticSurfaceKind::Unknown;
        break;
    }
    return info;
}

constexpr std::size_t kSampleRows = 17;
constexpr std::size_t kSampleColumns = 17;

[[nodiscard]] fabrication::SurfacePatchSamples SampleFace(const TopoDS_Face& face)
{
    fabrication::SurfacePatchSamples samples;
    if (face.IsNull()) {
        return samples;
    }
    occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face);
    if (surface.IsNull()) {
        return samples;
    }
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    samples.rowCount = kSampleRows;
    samples.columnCount = kSampleColumns;
    samples.points.reserve(kSampleRows * kSampleColumns);
    for (std::size_t row = 0; row < kSampleRows; ++row) {
        const double u = u0 + (u1 - u0) * static_cast<double>(row)
            / static_cast<double>(kSampleRows - 1);
        for (std::size_t column = 0; column < kSampleColumns; ++column) {
            const double v = v0 + (v1 - v0) * static_cast<double>(column)
                / static_cast<double>(kSampleColumns - 1);
            samples.points.push_back(FromPoint(surface->Value(u, v)));
        }
    }
    return samples;
}

//! 1本の鎖から、測るための点を最大 kProbePoints 点だけ取る。
constexpr std::size_t kProbePoints = 33;

[[nodiscard]] std::vector<Vector3> ProbePoints(const GuideChain& chain, double toleranceMm)
{
    const std::vector<Vector3> dense = geometry::SampleChain(chain.segments, toleranceMm);
    if (dense.size() <= kProbePoints) {
        return dense;
    }
    std::vector<Vector3> thin;
    thin.reserve(kProbePoints);
    for (std::size_t at = 0; at < kProbePoints; ++at) {
        const double ratio = static_cast<double>(at)
            / static_cast<double>(kProbePoints - 1);
        const std::size_t index = static_cast<std::size_t>(
            ratio * static_cast<double>(dense.size() - 1) + 0.5);
        thin.push_back(dense[index]);
    }
    return thin;
}

struct Deviation {
    double maximumMm = 0.0;
    double rmsMm = 0.0;
    std::size_t worstChainIndex = 0;
    bool measured = false;
};

//! 面が入力の線を通っているかを、実際の形状との最短距離で測る。
//! 面の下敷きになっている無限の曲面へ落とすと、穴の中でも「近い」ことになってしまう。
//! だからここは必ず TopoDS_Shape との距離で測る。
[[nodiscard]] Deviation MeasureDeviation(const TopoDS_Shape& shape,
    const GuideSurfaceRequest& request, const GeometryTolerance& tolerance)
{
    Deviation deviation;
    const double samplingTolerance = SamplingToleranceMm(tolerance);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        const GuideChain& chain = request.chains[index];
        if (chain.segments.empty()) {
            continue;
        }
        double chainWorst = 0.0;
        for (const Vector3& point : ProbePoints(chain, samplingTolerance)) {
            const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(ToPoint(point));
            BRepExtrema_DistShapeShape measure(vertex, shape,
                std::max(tolerance.modelLinearMm * 0.1, 1.0e-9), Extrema_ExtFlag_MIN);
            if (!measure.IsDone() || measure.NbSolution() < 1) {
                continue;
            }
            const double distance = measure.Value();
            sum += distance * distance;
            ++count;
            chainWorst = std::max(chainWorst, distance);
            deviation.measured = true;
        }
        if (chainWorst > deviation.maximumMm) {
            deviation.maximumMm = chainWorst;
            deviation.worstChainIndex = index;
        }
    }
    if (count > 0) {
        deviation.rmsMm = std::sqrt(sum / static_cast<double>(count));
    }
    return deviation;
}

[[nodiscard]] double AreaOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape, properties);
    return properties.Mass();
}

} // namespace

Result<GuideSurfaceResult> BuildGuideSurface(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance,
    KernelShapeHandle sourceShape)
{
    if (analysis.method != request.method) {
        return Result<GuideSurfaceResult>::Failure(MakeError(kSurfaceUnsupportedMethod,
            "調べたときと作り方が食い違っています。", {}));
    }

    Result<TopoDS_Shape> built = Result<TopoDS_Shape>::Failure(
        MakeError(kSurfaceUnsupportedMethod, "知らない作り方です。", {}));
    switch (request.method) {
    case GuideSurfaceMethod::PlanarBoundary:
        built = BuildPlanar(request, analysis, tolerance);
        break;
    case GuideSurfaceMethod::RuledSections:
        built = BuildThruSections(request, analysis, tolerance, true);
        break;
    case GuideSurfaceMethod::LoftSections:
        built = BuildThruSections(request, analysis, tolerance, false);
        break;
    case GuideSurfaceMethod::GuidedLoft:
        built = BuildGuidedLoft(request, analysis, tolerance);
        break;
    case GuideSurfaceMethod::GordonNetwork:
        built = BuildFilling(request, analysis, tolerance, false);
        break;
    case GuideSurfaceMethod::BoundaryFill:
        built = BuildFilling(request, analysis, tolerance, true);
        break;
    case GuideSurfaceMethod::OffsetGuide:
        built = BuildOffset(request, sourceShape, tolerance);
        break;
    }
    if (!built.HasValue()) {
        return Result<GuideSurfaceResult>::Failure(built.Diagnostics());
    }

    const TopoDS_Shape shape = built.Value();
    if (FaceCount(shape) == 0) {
        return Result<GuideSurfaceResult>::Failure(MakeError(kSurfaceBuildFailed,
            "面が1枚も出来ませんでした。", {}));
    }

    // ここが V1 との決定的な違い。作ったものを測り、通っていなければ捨てる。
    // OffsetGuide の入力は曲線ではないので、この検査の対象にしない。
    Deviation deviation;
    if (request.method != GuideSurfaceMethod::OffsetGuide) {
        deviation = MeasureDeviation(shape, request, tolerance);
        const double limit = std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
        if (deviation.measured && deviation.maximumMm > limit) {
            return Result<GuideSurfaceResult>::Failure(MakeError(kSurfaceMissesInput,
                "出来た面が、指定した線を通っていません。",
                "最大のずれ " + std::to_string(deviation.maximumMm) + " mm(許容 "
                    + std::to_string(limit) + " mm)。"
                    + std::to_string(deviation.worstChainIndex + 1)
                    + " 番目の線が最も外れています。"));
        }
    }

    const TopoDS_Face face = FirstFace(shape);
    GuideSurfaceResult result;
    result.samples = SampleFace(face);
    result.analytic = AnalyticOf(face);
    result.maximumDeviationMm = deviation.maximumMm;
    result.rmsDeviationMm = deviation.rmsMm;
    result.areaMm2 = AreaOf(shape);

    std::vector<Diagnostic> warnings;
    if (!face.IsNull()) {
        const TopoDS_Wire outer = BRepTools::OuterWire(face);
        if (!outer.IsNull()) {
            auto boundary = FromWire(outer, tolerance.modelLinearMm);
            if (boundary.HasValue()) {
                result.boundary = boundary.Value();
            } else {
                warnings.push_back(base::MakeWarning("KER-S100",
                    "面の外周を、曲線の種類を保ったまま取り出せませんでした。",
                    "板取りでは、この面の外周を作り直します。"));
            }
        }
    }
    if (FaceCount(shape) > 1) {
        warnings.push_back(base::MakeWarning("KER-S101",
            "面が複数枚に分かれています。",
            "枚数 " + std::to_string(FaceCount(shape))
                + "。板取りでは1枚ずつ扱います。"));
    }
    result.handle = StoreShape(shape);
    return Result<GuideSurfaceResult>::Success(std::move(result), std::move(warnings));
}

#else // KACHACAD_V2_WITH_OCCT

Result<GuideSurfaceResult> BuildGuideSurface(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance,
    KernelShapeHandle sourceShape)
{
    (void)request;
    (void)analysis;
    (void)tolerance;
    (void)sourceShape;
    // カーネル無しでビルドした版。作れないものを作れたことにしない。
    return Result<GuideSurfaceResult>::Failure(MakeError(kSurfaceUnsupportedMethod,
        "この実行ファイルには幾何カーネルが入っていません。",
        "OCCT を有効にしてビルドしてください。"));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
