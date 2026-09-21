#include "kachakacha/kernel/OcctGuideSurface.h"

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/modeling/GuideSurfaceSampling.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SurfaceDeviationLimit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctLoftSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Surface.hxx>
#include <TopoDS_Edge.hxx>
#include <Geom_Curve.hxx>
#include <NCollection_Array1.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <Extrema_ExtFlag.hxx>
#include <GeomAbs_Shape.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
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
using modeling::ChainCrossing;
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

//! 曲線を点として拘束に足すときの点数。多すぎると解けなくなる。
constexpr int kNetworkPointsPerChain = 9;

[[nodiscard]] Result<TopoDS_Shape> BuildFilling(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance,
    bool boundaryFill, detail::ContinuityMeasure& measure)
{
    using Out = Result<TopoDS_Shape>;
    return Guarded([&]() -> Out {
        // 曲線網(近似)は点で近づける作り方なので、面へ写す許容を 0.1 µm まで緩める
        // (1e-6 mm では写しきれずに張れないことがある)。境界面はこれまでどおり。
        const double tol3d = boundaryFill
            ? std::max(tolerance.modelLinearMm, Precision::Confusion())
            : std::max(tolerance.modelLinearMm, 1.0e-4);
        BRepOffsetAPI_MakeFilling filler(3, 15, 2, false, 1.0e-5, tol3d,
            0.01, 0.1, 8, 9);

        bool addedBoundary = false;
        std::vector<detail::ContinuityCheck> checks;
        if (boundaryFill) {
            std::vector<std::size_t> ring = analysis.sectionOrdering.chainIndices;
            if (ring.empty()) {
                ring = IndicesWithRole(request, ChainRole::BoundarySide);
            }
            // 辺ごとの連続条件(G0/G1/G2)。G1/G2 は支持面に対して足し、作ったあとで測る。
            for (const std::size_t index : ring) {
                const auto added =
                    detail::AddBoundaryEdges(filler, request, index, {}, tolerance, checks);
                if (!added.HasValue()) {
                    return Out::Failure(added.Diagnostics());
                }
                addedBoundary = true;
            }
            // 面が必ず通る線(外周の内側に引いた線)。境界ではない拘束として足す。
            // 通ったかどうかは、作ったあとで MeasureDeviation が測って判定する。
            for (const std::size_t index : IndicesWithRole(request, ChainRole::GuideU)) {
                for (const auto& segment : request.chains[index].segments) {
                    auto edge = ToEdge(segment);
                    if (!edge.HasValue()) {
                        return Out::Failure(edge.Diagnostics());
                    }
                    filler.Add(edge.Value(), GeomAbs_C0, Standard_False);
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
            // 外側の線は「並びの端」。渡された順ではなく、交わる位置の順で決める
            // (2026-09-22: 渡した順の最初と最後を外側にしていた)。
            const auto crossing = [&](std::size_t u, std::size_t v) -> const ChainCrossing* {
                for (const ChainCrossing& one : analysis.crossings) {
                    if (one.firstChainIndex == u && one.secondChainIndex == v) {
                        return &one;
                    }
                }
                return nullptr;
            };
            std::vector<std::size_t> uOrdered = uChains;
            std::vector<std::size_t> vOrdered = vChains;
            const auto along = [&](std::size_t u, std::size_t v, bool onV) {
                const ChainCrossing* one = crossing(u, v);
                return one == nullptr ? 0.0 : (onV ? one->secondParameter : one->firstParameter);
            };
            std::sort(uOrdered.begin(), uOrdered.end(), [&](std::size_t a, std::size_t b) {
                return along(a, vChains.front(), true) < along(b, vChains.front(), true);
            });
            std::sort(vOrdered.begin(), vOrdered.end(), [&](std::size_t a, std::size_t b) {
                return along(uChains.front(), a, false) < along(uChains.front(), b, false);
            });
            const std::size_t u0 = uOrdered.front();
            const std::size_t u1 = uOrdered.back();
            const std::size_t v0 = vOrdered.front();
            const std::size_t v1 = vOrdered.back();
            // 交わる位置(正規化弧長)は検査が測ったもの。同じ細かさで標本を取る。
            const double samplingTolerance = modeling::detail::SamplingToleranceMm(tolerance);
            // 外周の 4 隅は、U と V の交わる点(2 本の中点)。外側の線を隅から隅まで切り出し、
            // 1 本のなめらかな辺にして境界へ入れる。折れ線の区間を 1 本ずつ入れると、
            // 辺が何百本にもなって張れなかった(2026-09-22 PC: U5V4 の網)。
            const auto corner = [&](std::size_t u, std::size_t v) {
                const ChainCrossing* one = crossing(u, v);
                return one == nullptr ? Vector3{} : one->position;
            };
            const auto boundaryEdge = [&](std::size_t chain, double from, double to,
                                          const Vector3& start, const Vector3& end) -> Result<TopoDS_Edge> {
                const std::vector<Vector3> points =
                    geometry::SampleChain(request.chains[chain].segments, samplingTolerance);
                const std::vector<double> parameters = geometry::NormalizedArcLength(points);
                constexpr int kPieces = 32;
                NCollection_Array1<gp_Pnt> array(1, kPieces + 1);
                for (int k = 0; k <= kPieces; ++k) {
                    const double t = from + (to - from) * k / kPieces;
                    const Vector3 point = k == 0 ? start
                        : k == kPieces ? end
                                       : geometry::PointAtNormalizedArcLength(points, parameters, t);
                    array.SetValue(k + 1, ToPoint(point));
                }
                GeomAPI_PointsToBSpline fit(array, 3, 8, GeomAbs_C2, samplingTolerance);
                if (!fit.IsDone() || fit.Curve().IsNull()) {
                    return Result<TopoDS_Edge>::Failure(MakeError(kSurfaceBuildFailed,
                        "曲線網の外側の線を辺にできませんでした。", {}));
                }
                const occ::handle<Geom_Curve> curve = fit.Curve();
                BRepBuilderAPI_MakeEdge maker{curve};
                if (!maker.IsDone()) {
                    return Result<TopoDS_Edge>::Failure(MakeError(kSurfaceBuildFailed,
                        "曲線網の外側の線を辺にできませんでした。", {}));
                }
                return Result<TopoDS_Edge>::Success(maker.Edge());
            };
            const Vector3 c00 = corner(u0, v0);
            const Vector3 c01 = corner(u0, v1);
            const Vector3 c10 = corner(u1, v0);
            const Vector3 c11 = corner(u1, v1);
            const Result<TopoDS_Edge> sides[] = {
                boundaryEdge(u0, along(u0, v0, false), along(u0, v1, false), c00, c01),
                boundaryEdge(v1, along(u0, v1, true), along(u1, v1, true), c01, c11),
                boundaryEdge(u1, along(u1, v1, false), along(u1, v0, false), c11, c10),
                boundaryEdge(v0, along(u1, v0, true), along(u0, v0, true), c10, c00)};
            for (const auto& side : sides) {
                if (!side.HasValue()) {
                    return Out::Failure(side.Diagnostics());
                }
                filler.Add(side.Value(), GeomAbs_C0);
                addedBoundary = true;
            }
            const auto addAsPoints = [&](const std::vector<std::size_t>& list) {
                for (std::size_t at = 1; at + 1 < list.size(); ++at) {
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
            addAsPoints(uOrdered);
            addAsPoints(vOrdered);
        }
        if (!addedBoundary) {
            return Out::Failure(MakeError(kSurfaceBuildFailed, "境界がありません。", {}));
        }
        filler.Build();
        if (!filler.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "境界から面を張れませんでした。", {}));
        }
        const auto measured = detail::MeasureContinuity(filler.Shape(), request, checks);
        if (!measured.HasValue()) {
            return Out::Failure(measured.Diagnostics());
        }
        measure = measured.Value();
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

//! 回転体(V1 の回転面)。断面の鎖を軸のまわりに角度だけ回す。
//! 面は回転面(円筒・円錐・球・トーラス・一般の回転面)としてそのまま持つ。
//! 断面を折れ線へ落としてロフトするのではないので、断面は面の上に厳密に載る。
[[nodiscard]] Result<TopoDS_Shape> BuildRevolve(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    std::vector<std::size_t> sections = analysis.sectionOrdering.chainIndices;
    if (sections.empty()) {
        sections = IndicesWithRole(request, ChainRole::Section);
    }
    if (sections.size() != 1) {
        return Out::Failure(MakeError(kSurfaceBuildFailed,
            "回転体の断面は 1 本にしてください。", {}));
    }
    const geometry::Vector3 direction = geometry::Normalized(request.revolveAxisDirection);
    if (direction == geometry::Vector3{}) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "回転体の軸の向きが決まりません。", {}));
    }
    return Guarded([&]() -> Out {
        auto wire = WireOf(request, sections.front(), tolerance);
        if (!wire.HasValue()) {
            return Out::Failure(wire.Diagnostics());
        }
        const gp_Ax1 axis(ToPoint(request.revolveAxisPoint), gp_Dir(ToVector(direction)));
        BRepPrimAPI_MakeRevol generator(wire.Value(), axis, request.revolveAngleRad,
            Standard_True);
        generator.Build();
        if (!generator.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "断面を回して面を作れませんでした。",
                "断面が軸をまたいでいないか、軸が断面の平面の中にあるかを確かめてください。"));
        }
        return Out::Success(generator.Shape());
    }, "回転体");
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
        if (chain.role == ChainRole::Centerline) {
            continue;   // 中心線は断面を運ぶ道筋で、面の上には乗らない(乗らなくて正しい)
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

//! 「ガイド 5」のような呼び名(いちばん外れた線を人に言うため)。
[[nodiscard]] std::string WorstLabel(const GuideSurfaceRequest& request, std::size_t index)
{
    if (index >= request.chains.size()) {
        return "指定した線";
    }
    const GuideChain& chain = request.chains[index];
    return kachakacha::v2::modeling::ChainRoleLabelJa(request.method, chain.role) + " "
        + std::to_string(chain.index);
}

//! 「0.310」の形。
[[nodiscard]] std::string Millimetres(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
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
    detail::ContinuityMeasure continuity;
    switch (request.method) {
    case GuideSurfaceMethod::PlanarBoundary:
        built = BuildPlanar(request, analysis, tolerance);
        break;
    case GuideSurfaceMethod::RuledSections:
        built = BuildThruSections(request, analysis, tolerance, true);
        break;
    case GuideSurfaceMethod::LoftSections:
    case GuideSurfaceMethod::GuidedLoft:
        // 断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1。作り方は検査が決めた(LoftSolver)。
        built = analysis.loft.solver == modeling::LoftSolver::Sections
            ? BuildThruSections(request, analysis, tolerance, false)
            : detail::BuildLoftShape(request, analysis, tolerance);
        break;
    case GuideSurfaceMethod::GordonNetwork:
        built = BuildFilling(request, analysis, tolerance, false, continuity);
        break;
    case GuideSurfaceMethod::BoundaryFill:
        built = BuildFilling(request, analysis, tolerance, true, continuity);
        break;
    case GuideSurfaceMethod::OffsetGuide:
        built = BuildOffset(request, sourceShape, tolerance);
        break;
    case GuideSurfaceMethod::Revolve:
        built = BuildRevolve(request, analysis, tolerance);
        break;
    case GuideSurfaceMethod::FourEdgePatch:
        built = detail::BuildFourEdgeShape(request, analysis, tolerance, continuity);
        break;
    case GuideSurfaceMethod::CurveNetworkExact:
        built = detail::BuildNetworkShape(request, tolerance);
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
        // 許容は作り方で決まる。決め方は core にある
        // (modeling/SurfaceDeviationLimit.h)。**ここには数を書かない。**
        // 通す作り方(平面・ルールド・ロフト・回転体)はこれまでどおり厳しく、
        // 近づける作り方(案内付きロフト・曲線網・境界埋め)は
        // 後の板材の曲げ近似が許している量までとする。
        const double limit = kachakacha::v2::modeling::SurfaceDeviationLimitMm(
            request, tolerance);
        if (deviation.measured && deviation.maximumMm > limit) {
            return Result<GuideSurfaceResult>::Failure(MakeError(kSurfaceMissesInput,
                "出来た面が、指定した線を通っていません。",
                "面は作れましたが、" + WorstLabel(request, deviation.worstChainIndex)
                    + "から最大 " + Millimetres(deviation.maximumMm)
                    + " mm 外れたため採用しませんでした(許容 " + Millimetres(limit) + " mm)。"));
        }
    }

    auto finished = detail::FinishSurfaceResult(shape, tolerance);
    if (!finished.HasValue()) {
        return finished;
    }
    GuideSurfaceResult result = finished.Value();
    result.maximumDeviationMm = deviation.maximumMm;
    result.rmsDeviationMm = deviation.rmsMm;
    result.continuityG1ErrorDeg = continuity.g1ErrorDeg;
    result.continuityG2Error = continuity.g2Error;

    std::vector<Diagnostic> warnings;
    // 通ったときも、外れた量は言う。**黙って通さない。**
    // 近づけて作る面は、指定した線の上に乗っていない。そのことを知らずに
    // 板取りへ進むと、紙とプラ板を切ってから気づくことになる。
    if (const std::string note = kachakacha::v2::modeling::SurfaceDeviationNoteJa(
            request, deviation.maximumMm, tolerance);
        !note.empty()) {
        warnings.push_back(base::MakeWarning("KER-S102", note,
            "通す作り方(ロフト・ルールド)なら、線の上に乗ります。"));
    }
    for (const Diagnostic& warning : finished.Diagnostics()) {
        warnings.push_back(warning);
    }
    return Result<GuideSurfaceResult>::Success(std::move(result), std::move(warnings));
}

namespace detail {

Result<GuideSurfaceResult> FinishSurfaceResult(const TopoDS_Shape& shape,
    const GeometryTolerance& tolerance)
{
    if (FaceCount(shape) == 0) {
        return Result<GuideSurfaceResult>::Failure(MakeError(kSurfaceBuildFailed,
            "面が1枚も出来ませんでした。", {}));
    }
    const TopoDS_Face face = FirstFace(shape);
    GuideSurfaceResult result;
    result.samples = SampleFace(face);
    result.analytic = AnalyticOf(face);
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

} // namespace detail

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
