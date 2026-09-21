#include "kachakacha/kernel/OcctSurfaceAnalysis.h"

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctLoftSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLProp_CLProps.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#endif // KACHACAD_V2_WITH_OCCT

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Vector3;
using modeling::KernelShapeHandle;
using modeling::SurfaceAnalysisData;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

template<class Function>
[[nodiscard]] auto Guarded(Function&& body) -> decltype(body())
{
    using ResultType = decltype(body());
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return ResultType::Failure(MakeError(kAnalysisFailed, "面を解析できませんでした。",
            std::string("幾何カーネル: ") + failure.what()));
    } catch (const std::exception& error) {
        return ResultType::Failure(MakeError(kAnalysisFailed, "面を解析できませんでした。",
            error.what()));
    }
}

[[nodiscard]] Result<TopoDS_Shape> ShapeOf(const KernelShapeHandle& handle)
{
    TopoDS_Shape shape;
    if (!handle.Valid() || !LookupShape(handle, shape)) {
        return Result<TopoDS_Shape>::Failure(MakeError(kAnalysisFailed,
            "解析する面の形が見つかりません。", "面を作り直してください。"));
    }
    return Result<TopoDS_Shape>::Success(shape);
}

[[nodiscard]] TopoDS_Face FirstFaceOf(const TopoDS_Shape& shape)
{
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        return TopoDS::Face(explorer.Current());
    }
    return TopoDS_Face();
}

//! 格子の 1 点。
struct GridPoint {
    bool inside = false;
    Vector3 point{};
    Vector3 normal{};
    double gaussian = 0.0;
    double mean = 0.0;
};

[[nodiscard]] GridPoint SampleAt(const TopoDS_Face& face, const occ::handle<Geom_Surface>& surface,
    bool reversed, double u, double v)
{
    GridPoint out;
    if (BRepClass_FaceClassifier(face, gp_Pnt2d(u, v), 1.0e-7).State() == TopAbs_OUT) {
        return out;
    }
    GeomLProp_SLProps props(surface, u, v, 2, Precision::Confusion());
    if (!props.IsNormalDefined()) {
        return out;
    }
    const gp_Pnt point = props.Value();
    gp_Vec normal(props.Normal());
    double mean = 0.0;
    double gaussian = 0.0;
    if (props.IsCurvatureDefined()) {
        mean = props.MeanCurvature();
        gaussian = props.GaussianCurvature();
    }
    // 面が裏向きなら法線と平均曲率の符号を返す(ガウス曲率は向きによらない)。
    if (reversed) {
        normal.Reverse();
        mean = -mean;
    }
    out.inside = std::isfinite(gaussian) && std::isfinite(mean);
    out.point = FromPoint(point);
    out.normal = Vector3{normal.X(), normal.Y(), normal.Z()};
    out.gaussian = gaussian;
    out.mean = mean;
    return out;
}

void AddTriangle(SurfaceAnalysisData& data, const GridPoint& a, const GridPoint& b,
    const GridPoint& c)
{
    if (!a.inside || !b.inside || !c.inside) {
        return;
    }
    modeling::AnalysisTriangle triangle;
    triangle.triangle.points = {a.point, b.point, c.point};
    Vector3 flat = geometry::Cross(b.point - a.point, c.point - a.point);
    const double length = flat.Length();
    if (!(length > 1.0e-14)) {
        return;
    }
    flat = flat * (1.0 / length);
    const Vector3 average = a.normal + b.normal + c.normal;
    if (flat.x * average.x + flat.y * average.y + flat.z * average.z < 0.0) {
        flat = flat * -1.0;
    }
    triangle.triangle.normal = flat;
    triangle.normals = {a.normal, b.normal, c.normal};
    triangle.gaussian = {a.gaussian, b.gaussian, c.gaussian};
    triangle.mean = {a.mean, b.mean, c.mean};
    data.triangles.push_back(triangle);
}

//! 1 本の U/V 線を、面の内側だけの折れ線にする(外に出たら切る)。
void AddIsoLine(SurfaceAnalysisData& data, const TopoDS_Face& face,
    const occ::handle<Geom_Surface>& surface, bool alongU, double fixed, double from, double to)
{
    constexpr int kSteps = 80;
    std::vector<Vector3> run;
    for (int k = 0; k <= kSteps; ++k) {
        const double at = from + (to - from) * k / kSteps;
        const double u = alongU ? at : fixed;
        const double v = alongU ? fixed : at;
        const bool inside =
            BRepClass_FaceClassifier(face, gp_Pnt2d(u, v), 1.0e-7).State() != TopAbs_OUT;
        if (inside) {
            run.push_back(FromPoint(surface->Value(u, v)));
            continue;
        }
        if (run.size() >= 2) {
            data.isoLines.push_back(run);
        }
        run.clear();
    }
    if (run.size() >= 2) {
        data.isoLines.push_back(run);
    }
}

void AddCombs(SurfaceAnalysisData& data, const TopoDS_Face& face)
{
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(face, TopAbs_EDGE, edges);
    for (int index = 1; index <= edges.Extent(); ++index) {
        const TopoDS_Edge edge = TopoDS::Edge(edges(index));
        if (BRep_Tool::Degenerated(edge)) {
            continue;
        }
        BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        std::vector<modeling::CombSample> comb;
        constexpr int kTeeth = 40;
        for (int k = 0; k <= kTeeth; ++k) {
            const double u = first + (last - first) * k / kTeeth;
            BRepLProp_CLProps props(curve, u, 2, Precision::Confusion());
            modeling::CombSample sample;
            sample.point = FromPoint(curve.Value(u));
            if (props.IsTangentDefined()) {
                const double curvature = props.Curvature();
                if (std::isfinite(curvature) && curvature > 1.0e-12) {
                    gp_Pnt center;
                    props.CentreOfCurvature(center);
                    const Vector3 toward = FromPoint(center) - sample.point;
                    const double length = toward.Length();
                    if (length > 1.0e-14) {
                        sample.towardCenter = toward * (1.0 / length);
                        sample.curvature = curvature;
                    }
                }
            }
            comb.push_back(sample);
        }
        data.combs.push_back(std::move(comb));
    }
}

void AnalyzeFace(SurfaceAnalysisData& data, const TopoDS_Face& face, int cells, int isoCount)
{
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    const occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face);
    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    std::vector<GridPoint> grid(static_cast<std::size_t>((cells + 1) * (cells + 1)));
    const auto at = [&grid, cells](int i, int j) -> GridPoint& {
        return grid[static_cast<std::size_t>(i * (cells + 1) + j)];
    };
    for (int i = 0; i <= cells; ++i) {
        for (int j = 0; j <= cells; ++j) {
            at(i, j) = SampleAt(face, surface, reversed, u0 + (u1 - u0) * i / cells,
                v0 + (v1 - v0) * j / cells);
        }
    }
    for (int i = 0; i < cells; ++i) {
        for (int j = 0; j < cells; ++j) {
            AddTriangle(data, at(i, j), at(i + 1, j), at(i + 1, j + 1));
            AddTriangle(data, at(i, j), at(i + 1, j + 1), at(i, j + 1));
        }
    }
    for (int k = 1; k <= isoCount; ++k) {
        AddIsoLine(data, face, surface, true, v0 + (v1 - v0) * k / (isoCount + 1), u0, u1);
        AddIsoLine(data, face, surface, false, u0 + (u1 - u0) * k / (isoCount + 1), v0, v1);
    }
    AddCombs(data, face);
}

} // namespace

Result<SurfaceAnalysisData> AnalyzeSurfaceShape(const KernelShapeHandle& handle, int gridCells,
    int isoCount)
{
    return Guarded([&]() -> Result<SurfaceAnalysisData> {
        const auto shape = ShapeOf(handle);
        if (!shape.HasValue()) {
            return Result<SurfaceAnalysisData>::Failure(shape.Diagnostics());
        }
        SurfaceAnalysisData data;
        const int cells = std::clamp(gridCells, 4, 120);
        for (TopExp_Explorer explorer(shape.Value(), TopAbs_FACE); explorer.More(); explorer.Next()) {
            AnalyzeFace(data, TopoDS::Face(explorer.Current()), cells, std::clamp(isoCount, 0, 40));
        }
        if (data.triangles.empty()) {
            return Result<SurfaceAnalysisData>::Failure(MakeError(kAnalysisFailed,
                "面を解析できませんでした。", "面の内側に標本が取れませんでした。"));
        }
        Vector3 low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()};
        Vector3 high = low * -1.0;
        for (const auto& triangle : data.triangles) {
            for (const Vector3& point : triangle.triangle.points) {
                low = Vector3{std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
                high = Vector3{std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
            }
        }
        data.sizeMm = (high - low).Length();
        return Result<SurfaceAnalysisData>::Success(std::move(data));
    });
}

Result<std::vector<modeling::EdgeContinuitySample>> SurfaceEdgeContinuity(
    const KernelShapeHandle& surface, const std::vector<KernelShapeHandle>& neighbors,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<std::vector<modeling::EdgeContinuitySample>>;
    return Guarded([&]() -> Out {
        const auto shape = ShapeOf(surface);
        if (!shape.HasValue()) {
            return Out::Failure(shape.Diagnostics());
        }
        const TopoDS_Face face = FirstFaceOf(shape.Value());
        std::vector<TopoDS_Face> others;
        for (const KernelShapeHandle& handle : neighbors) {
            TopoDS_Shape other;
            if (handle.Valid() && handle.value != surface.value && LookupShape(handle, other)) {
                const TopoDS_Face otherFace = FirstFaceOf(other);
                if (!otherFace.IsNull()) {
                    others.push_back(otherFace);
                }
            }
        }
        // 隣とみなす離れ: 縁の上の点が相手の外周からこれ以内(許容の 5 倍、最低 0.05 mm)。
        const double adjacency = std::max(tolerance.interactiveJoinMm * 5.0, 0.05);
        std::vector<modeling::EdgeContinuitySample> out;
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(face, TopAbs_EDGE, edges);
        for (int index = 1; index <= edges.Extent(); ++index) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(index));
            if (BRep_Tool::Degenerated(edge)) {
                continue;
            }
            modeling::EdgeContinuitySample sample;
            BRepAdaptor_Curve curve(edge);
            const double first = curve.FirstParameter();
            const double last = curve.LastParameter();
            for (int k = 0; k <= 32; ++k) {
                sample.polyline.push_back(FromPoint(curve.Value(first + (last - first) * k / 32)));
            }
            for (const TopoDS_Face& other : others) {
                const TopoDS_Wire outer = BRepTools::OuterWire(other);
                double worst = 0.0;
                for (int k = 1; k <= 5; ++k) {
                    const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(
                        curve.Value(first + (last - first) * k / 6.0));
                    BRepExtrema_DistShapeShape distance(vertex, outer);
                    worst = std::max(worst, distance.IsDone() ? distance.Value()
                                                              : std::numeric_limits<double>::infinity());
                }
                if (!(worst <= adjacency)) {
                    continue;
                }
                const auto measured = detail::MeasureEdgeContinuity(face, other, edge);
                sample.hasNeighbor = true;
                sample.gapMm = worst;
                sample.angleDeg = measured.measured ? measured.g1Deg : 180.0;
                sample.curvatureDifference = measured.measured ? measured.g2 : 1.0e9;
                break;
            }
            out.push_back(std::move(sample));
        }
        return Out::Success(std::move(out));
    });
}

Result<std::vector<modeling::DeviationSample>> DeviationFromChains(const KernelShapeHandle& surface,
    const std::vector<std::vector<CurveSegment>>& chains)
{
    using Out = Result<std::vector<modeling::DeviationSample>>;
    return Guarded([&]() -> Out {
        const auto shape = ShapeOf(surface);
        if (!shape.HasValue()) {
            return Out::Failure(shape.Diagnostics());
        }
        std::vector<modeling::DeviationSample> out;
        for (const auto& chain : chains) {
            const std::vector<Vector3> dense = geometry::SampleChain(chain, 0.01);
            modeling::DeviationSample sample;
            const std::size_t step = std::max<std::size_t>(1, dense.size() / 60);
            for (std::size_t k = 0; k < dense.size(); k += step) {
                const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(ToPoint(dense[k]));
                BRepExtrema_DistShapeShape distance(vertex, shape.Value());
                sample.points.push_back(dense[k]);
                sample.distancesMm.push_back(distance.IsDone() ? distance.Value() : 1.0e9);
            }
            out.push_back(std::move(sample));
        }
        return Out::Success(std::move(out));
    });
}

#else // KACHACAD_V2_WITH_OCCT

Result<SurfaceAnalysisData> AnalyzeSurfaceShape(const KernelShapeHandle&, int, int)
{
    return Result<SurfaceAnalysisData>::Failure(MakeError(kAnalysisFailed,
        "この実行ファイルには幾何カーネルが入っていません。", "OCCT を有効にしてビルドしてください。"));
}

Result<std::vector<modeling::EdgeContinuitySample>> SurfaceEdgeContinuity(const KernelShapeHandle&,
    const std::vector<KernelShapeHandle>&, const geometry::GeometryTolerance&)
{
    return Result<std::vector<modeling::EdgeContinuitySample>>::Failure(MakeError(kAnalysisFailed,
        "この実行ファイルには幾何カーネルが入っていません。", "OCCT を有効にしてビルドしてください。"));
}

Result<std::vector<modeling::DeviationSample>> DeviationFromChains(const KernelShapeHandle&,
    const std::vector<std::vector<CurveSegment>>&)
{
    return Result<std::vector<modeling::DeviationSample>>::Failure(MakeError(kAnalysisFailed,
        "この実行ファイルには幾何カーネルが入っていません。", "OCCT を有効にしてビルドしてください。"));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
