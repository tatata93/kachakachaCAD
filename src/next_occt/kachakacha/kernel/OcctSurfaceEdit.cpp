#include "kachakacha/kernel/OcctSurfaceEdit.h"

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctLoftSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepProj_Projection.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAbs_Shape.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomConvert.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <Precision.hxx>
#include <ShapeFix_Face.hxx>
#include <Standard_Failure.hxx>
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
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#endif // KACHACAD_V2_WITH_OCCT

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;
using modeling::GuideSurfaceResult;
using modeling::KernelShapeHandle;
using modeling::SurfaceContinuity;

using WireList = std::vector<std::vector<CurveSegment>>;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

template<class Function>
[[nodiscard]] auto Guarded(Function&& body, const char* code, const char* what)
    -> decltype(body())
{
    using ResultType = decltype(body());
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return ResultType::Failure(MakeError(code, std::string(what) + "に失敗しました。",
            std::string("幾何カーネル: ") + failure.what()));
    } catch (const std::exception& error) {
        return ResultType::Failure(MakeError(code, std::string(what) + "に失敗しました。",
            error.what()));
    } catch (...) {
        return ResultType::Failure(MakeError(code, std::string(what) + "に失敗しました。", {}));
    }
}

[[nodiscard]] std::string Mm(double value, int digits = 3)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

[[nodiscard]] Result<TopoDS_Face> FaceOf(const KernelShapeHandle& handle)
{
    TopoDS_Shape shape;
    if (!handle.Valid() || !LookupShape(handle, shape)) {
        return Result<TopoDS_Face>::Failure(MakeError(kEditSourceMissing,
            "面の形が見つかりません。", "面を作り直すか、選び直してください。"));
    }
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        return Result<TopoDS_Face>::Success(TopoDS::Face(explorer.Current()));
    }
    return Result<TopoDS_Face>::Failure(MakeError(kEditSourceMissing,
        "選んだものに面がありません。", {}));
}

[[nodiscard]] std::vector<TopoDS_Edge> EdgesOf(const TopoDS_Face& face)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(face, TopAbs_EDGE, map);
    std::vector<TopoDS_Edge> edges;
    for (int index = 1; index <= map.Extent(); ++index) {
        edges.push_back(TopoDS::Edge(map(index)));
    }
    return edges;
}

[[nodiscard]] Result<TopoDS_Edge> EdgeOf(const TopoDS_Face& face, int index)
{
    const auto edges = EdgesOf(face);
    if (index < 0 || index >= static_cast<int>(edges.size())
        || BRep_Tool::Degenerated(edges[static_cast<std::size_t>(index)])) {
        return Result<TopoDS_Edge>::Failure(MakeError(kEditEdgeMissing,
            "指した縁がその面にありません。",
            "面の形が変わったかもしれません。縁を選び直してください(番号 "
                + std::to_string(index) + "、縁の数 " + std::to_string(edges.size()) + ")。"));
    }
    return Result<TopoDS_Edge>::Success(edges[static_cast<std::size_t>(index)]);
}

[[nodiscard]] std::vector<Vector3> EdgePolyline(const TopoDS_Edge& edge)
{
    std::vector<Vector3> points;
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    constexpr int kCount = 32;
    for (int k = 0; k <= kCount; ++k) {
        points.push_back(FromPoint(curve.Value(first + (last - first) * k / kCount)));
    }
    return points;
}

[[nodiscard]] TopoDS_Face FirstFaceOf(const TopoDS_Shape& shape)
{
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        return TopoDS::Face(explorer.Current());
    }
    return TopoDS_Face();
}

//! from の面の上の点(内側だけ)が、to の面からどれだけ離れているかの最大(mm)。
[[nodiscard]] double MaximumDistance(const TopoDS_Face& from, const TopoDS_Face& to)
{
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;
    BRepTools::UVBounds(from, u0, u1, v0, v1);
    const occ::handle<Geom_Surface> source = BRep_Tool::Surface(from);
    const occ::handle<Geom_Surface> target = BRep_Tool::Surface(to);
    double worst = 0.0;
    constexpr int kCount = 16;
    for (int i = 0; i <= kCount; ++i) {
        for (int j = 0; j <= kCount; ++j) {
            const double u = u0 + (u1 - u0) * i / kCount;
            const double v = v0 + (v1 - v0) * j / kCount;
            BRepClass_FaceClassifier inside(from, gp_Pnt2d(u, v), 1.0e-7);
            if (inside.State() == TopAbs_OUT) {
                continue;
            }
            GeomAPI_ProjectPointOnSurf projection(source->Value(u, v), target);
            if (projection.IsDone() && projection.NbPoints() > 0) {
                worst = std::max(worst, projection.LowerDistance());
            }
        }
    }
    return worst;
}

//! 縁の両端。
[[nodiscard]] std::pair<gp_Pnt, gp_Pnt> EndsOf(const TopoDS_Edge& edge)
{
    BRepAdaptor_Curve curve(edge);
    return {curve.Value(curve.FirstParameter()), curve.Value(curve.LastParameter())};
}

[[nodiscard]] GeomAbs_Shape OrderOf(SurfaceContinuity order)
{
    return detail::FillingOrder(order);   // G2 は GeomAbs_C1(OcctLoftSurface.h の注記)
}

//! 指定の滑らかさに届いているか。届いていなければ理由を言う。
[[nodiscard]] Result<detail::EdgeContinuity> CheckContinuity(const TopoDS_Face& result,
    const TopoDS_Face& support, const TopoDS_Edge& edge, SurfaceContinuity order,
    const std::string& whereJa)
{
    const auto measured = detail::MeasureEdgeContinuity(result, support, edge);
    if (!measured.measured) {
        return Result<detail::EdgeContinuity>::Failure(MakeError(kEditContinuityMissed,
            whereJa + "の滑らかさを測れませんでした。",
            "縁の上の点を面へ落とせませんでした(" + std::to_string(measured.measuredSamples)
                + " / " + std::to_string(measured.samples) + " 点)。"));
    }
    if (measured.g1Deg > detail::kContinuityG1LimitDeg) {
        return Result<detail::EdgeContinuity>::Failure(MakeError(kEditContinuityMissed,
            std::string(modeling::SurfaceContinuityName(order)) + "を指定しましたが、" + whereJa
                + "で面が最大 " + Mm(measured.g1Deg) + " 度折れています。",
            "許容は " + Mm(detail::kContinuityG1LimitDeg)
                + " 度です。縁の形か、ほかの縁とのつながりを見直してください。"));
    }
    if (order == SurfaceContinuity::G2 && measured.g2 > detail::kContinuityG2Limit) {
        return Result<detail::EdgeContinuity>::Failure(MakeError(kEditContinuityMissed,
            "G2を指定しましたが、" + whereJa + "で曲率が最大 " + Mm(measured.g2)
                + " (1/mm) 食い違っています。",
            "許容は " + Mm(detail::kContinuityG2Limit) + " (1/mm) です。G1 にしてください。"));
    }
    return Result<detail::EdgeContinuity>::Success(measured);
}

[[nodiscard]] std::string ContinuityWords(SurfaceContinuity order,
    const detail::EdgeContinuity& measured)
{
    if (order == SurfaceContinuity::G0) {
        return "G0";
    }
    std::string words = std::string(modeling::SurfaceContinuityName(order)) + "(折れ目 最大 "
        + Mm(measured.g1Deg) + " 度";
    if (order == SurfaceContinuity::G2) {
        words += "、曲率の差 最大 " + Mm(measured.g2, 4) + " /mm";
    }
    return words + ")";
}

//! 面の上の点 p での法線(面の向きのまま)。
[[nodiscard]] bool NormalNear(const occ::handle<Geom_Surface>& surface, const gp_Pnt& point,
    gp_Vec& normal)
{
    GeomAPI_ProjectPointOnSurf projection(point, surface);
    if (!projection.IsDone() || projection.NbPoints() == 0) {
        return false;
    }
    double u = 0.0;
    double v = 0.0;
    projection.LowerDistanceParameters(u, v);
    GeomLProp_SLProps props(surface, u, v, 1, Precision::Confusion());
    if (!props.IsNormalDefined()) {
        return false;
    }
    normal = gp_Vec(props.Normal());
    return true;
}

} // namespace

Result<SurfaceEdgeInfo> NearestSurfaceEdge(const KernelShapeHandle& surface,
    const Vector3& point)
{
    return Guarded([&]() -> Result<SurfaceEdgeInfo> {
        const auto face = FaceOf(surface);
        if (!face.HasValue()) {
            return Result<SurfaceEdgeInfo>::Failure(face.Diagnostics());
        }
        const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(ToPoint(point));
        const auto edges = EdgesOf(face.Value());
        SurfaceEdgeInfo best;
        best.distanceMm = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < edges.size(); ++index) {
            if (BRep_Tool::Degenerated(edges[index])) {
                continue;
            }
            BRepExtrema_DistShapeShape distance(vertex, edges[index]);
            if (!distance.IsDone() || !(distance.Value() < best.distanceMm)) {
                continue;
            }
            best.index = static_cast<int>(index);
            best.distanceMm = distance.Value();
        }
        if (best.index < 0) {
            return Result<SurfaceEdgeInfo>::Failure(MakeError(kEditEdgeMissing,
                "面に縁が見つかりません。", {}));
        }
        best.polyline = EdgePolyline(edges[static_cast<std::size_t>(best.index)]);
        return Result<SurfaceEdgeInfo>::Success(std::move(best));
    }, kEditEdgeMissing, "縁の拾い出し");
}

Result<SurfaceEdgeInfo> SurfaceEdgeAt(const KernelShapeHandle& surface, int edgeIndex)
{
    return Guarded([&]() -> Result<SurfaceEdgeInfo> {
        const auto face = FaceOf(surface);
        if (!face.HasValue()) {
            return Result<SurfaceEdgeInfo>::Failure(face.Diagnostics());
        }
        const auto edge = EdgeOf(face.Value(), edgeIndex);
        if (!edge.HasValue()) {
            return Result<SurfaceEdgeInfo>::Failure(edge.Diagnostics());
        }
        SurfaceEdgeInfo info;
        info.index = edgeIndex;
        info.polyline = EdgePolyline(edge.Value());
        return Result<SurfaceEdgeInfo>::Success(std::move(info));
    }, kEditEdgeMissing, "縁の取り出し");
}

Result<SurfaceEditResult> MatchSurfaceEdge(const KernelShapeHandle& target, int targetEdge,
    const KernelShapeHandle& reference, int referenceEdge, SurfaceContinuity order,
    const GeometryTolerance& tolerance)
{
    using Out = Result<SurfaceEditResult>;
    return Guarded([&]() -> Out {
        const auto targetFace = FaceOf(target);
        const auto referenceFace = FaceOf(reference);
        if (!targetFace.HasValue() || !referenceFace.HasValue()) {
            return Out::Failure(targetFace.HasValue() ? referenceFace.Diagnostics()
                                                      : targetFace.Diagnostics());
        }
        const auto moving = EdgeOf(targetFace.Value(), targetEdge);
        const auto goal = EdgeOf(referenceFace.Value(), referenceEdge);
        if (!moving.HasValue() || !goal.HasValue()) {
            return Out::Failure(moving.HasValue() ? goal.Diagnostics() : moving.Diagnostics());
        }
        // 端がそろっていること。端を動かして合わせる(隣の縁まで直す)のは、まだできない。
        const auto [m0, m1] = EndsOf(moving.Value());
        const auto [g0, g1] = EndsOf(goal.Value());
        const double gap = std::min(std::max(m0.Distance(g0), m1.Distance(g1)),
            std::max(m0.Distance(g1), m1.Distance(g0)));
        const double allowed = std::max(tolerance.interactiveJoinMm, 1.0e-4);
        if (gap > allowed) {
            return Out::Failure(MakeError(kEditEndsApart,
                "直す縁の端と、合わせ先の縁の端が離れています。",
                "最大 " + Mm(gap) + " mm 離れています(許容 " + Mm(allowed)
                    + " mm)。端がそろっている縁どうしで合わせてください(端を動かして"
                      "合わせるのは、まだできません)。"));
        }
        const TopoDS_Wire outer = BRepTools::OuterWire(targetFace.Value());
        BRepOffsetAPI_MakeFilling filler(3, 15, 2, false, 1.0e-5, std::max(1.0e-4, gap * 2.0),
            0.01, 0.1, 8, 12);
        bool replaced = false;
        for (BRepTools_WireExplorer explorer(outer); explorer.More(); explorer.Next()) {
            const TopoDS_Edge& edge = explorer.Current();
            if (edge.IsSame(moving.Value())) {
                replaced = true;
                if (order == SurfaceContinuity::G0) {
                    filler.Add(goal.Value(), GeomAbs_C0, true);
                } else {
                    filler.Add(goal.Value(), referenceFace.Value(), OrderOf(order), true);
                }
            } else {
                filler.Add(edge, GeomAbs_C0, true);
            }
        }
        if (!replaced) {
            return Out::Failure(MakeError(kEditMatchFailed,
                "穴の縁は、まだ合わせられません。", "面の外周の縁を選んでください。"));
        }
        // 元の面を初期形にする。核は初期形への足し分を張るので、合わせた縁から遠い
        // ところほど元の形のまま残る。
        filler.LoadInitSurface(targetFace.Value());
        filler.Build();
        if (!filler.IsDone()) {
            return Out::Failure(MakeError(kEditMatchFailed, "面を合わせられませんでした。",
                "縁の形が大きく違うか、面の縁の並びが崩れています。"));
        }
        const TopoDS_Shape built = filler.Shape();
        const TopoDS_Face resultFace = FirstFaceOf(built);
        detail::EdgeContinuity measured;
        if (order != SurfaceContinuity::G0) {
            const auto checked = CheckContinuity(resultFace, referenceFace.Value(),
                goal.Value(), order, "合わせた縁");
            if (!checked.HasValue()) {
                return Out::Failure(checked.Diagnostics());
            }
            measured = checked.Value();
        }
        auto finished = detail::FinishSurfaceResult(built, tolerance);
        if (!finished.HasValue()) {
            return Out::Failure(finished.Diagnostics());
        }
        SurfaceEditResult result;
        result.surface = finished.Value();
        result.deviationMm = MaximumDistance(targetFace.Value(), resultFace);
        if (order != SurfaceContinuity::G0) {
            result.continuityG1Deg = measured.g1Deg;
            result.continuityG2 = order == SurfaceContinuity::G2 ? measured.g2 : -1.0;
        }
        result.noteJa = "縁を合わせ先の縁へ " + ContinuityWords(order, measured)
            + " で合わせました。元の面から最大 " + Mm(result.deviationMm) + " mm 動いています。";
        return Out::Success(std::move(result), finished.Diagnostics());
    }, kEditMatchFailed, "面を合わせるの");
}

namespace {

//! 縁の上の N+1 点と、縁から出る向き(相手の側へ)。
struct EdgeFrame {
    std::vector<gp_Pnt> points;
    std::vector<gp_Vec> outward;
};

[[nodiscard]] Result<EdgeFrame> FrameOf(const TopoDS_Face& face, const TopoDS_Edge& edge,
    bool reversed, int count)
{
    EdgeFrame frame;
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    const occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face);
    for (int k = 0; k <= count; ++k) {
        const double t = static_cast<double>(k) / count;
        const double u = reversed ? last - (last - first) * t : first + (last - first) * t;
        gp_Pnt point;
        gp_Vec tangent;
        curve.D1(u, point, tangent);
        gp_Vec normal;
        if (!NormalNear(surface, point, normal) || !(tangent.Magnitude() > 1.0e-12)) {
            return Result<EdgeFrame>::Failure(MakeError(kEditBridgeFailed,
                "縁から出る向きを決められませんでした。", "縁の上で面の向きが決まりません。"));
        }
        frame.points.push_back(point);
        frame.outward.push_back(normal.Crossed(tangent).Normalized());
    }
    return Result<EdgeFrame>::Success(std::move(frame));
}

[[nodiscard]] gp_Pnt Hermite(const gp_Pnt& a, const gp_Vec& da, const gp_Pnt& b, const gp_Vec& db,
    double s)
{
    const double h00 = 2.0 * s * s * s - 3.0 * s * s + 1.0;
    const double h10 = s * s * s - 2.0 * s * s + s;
    const double h01 = -2.0 * s * s * s + 3.0 * s * s;
    const double h11 = s * s * s - s * s;
    return gp_Pnt(a.XYZ() * h00 + da.XYZ() * h10 + b.XYZ() * h01 + db.XYZ() * h11);
}

[[nodiscard]] Result<TopoDS_Edge> EdgeThroughPoints(const std::vector<gp_Pnt>& points)
{
    NCollection_Array1<gp_Pnt> array(1, static_cast<int>(points.size()));
    for (std::size_t index = 0; index < points.size(); ++index) {
        array.SetValue(static_cast<int>(index) + 1, points[index]);
    }
    GeomAPI_PointsToBSpline fit(array, 3, 8, GeomAbs_C2, 1.0e-6);
    if (!fit.IsDone() || fit.Curve().IsNull()) {
        return Result<TopoDS_Edge>::Failure(MakeError(kEditBridgeFailed,
            "つなぐ面の脇の縁を作れませんでした。", {}));
    }
    const occ::handle<Geom_Curve> curve = fit.Curve();
    BRepBuilderAPI_MakeEdge maker{curve};
    if (!maker.IsDone()) {
        return Result<TopoDS_Edge>::Failure(MakeError(kEditBridgeFailed,
            "つなぐ面の脇の縁を作れませんでした。", {}));
    }
    return Result<TopoDS_Edge>::Success(maker.Edge());
}

} // namespace

Result<SurfaceEditResult> BridgeSurfaceEdges(const KernelShapeHandle& first, int firstEdge,
    SurfaceContinuity firstOrder, const KernelShapeHandle& second, int secondEdge,
    SurfaceContinuity secondOrder, double tension, const GeometryTolerance& tolerance)
{
    using Out = Result<SurfaceEditResult>;
    return Guarded([&]() -> Out {
        const auto faceA = FaceOf(first);
        const auto faceB = FaceOf(second);
        if (!faceA.HasValue() || !faceB.HasValue()) {
            return Out::Failure(faceA.HasValue() ? faceB.Diagnostics() : faceA.Diagnostics());
        }
        const auto edgeA = EdgeOf(faceA.Value(), firstEdge);
        const auto edgeB = EdgeOf(faceB.Value(), secondEdge);
        if (!edgeA.HasValue() || !edgeB.HasValue()) {
            return Out::Failure(edgeA.HasValue() ? edgeB.Diagnostics() : edgeA.Diagnostics());
        }
        if (!(tension > 0.0)) {
            return Out::Failure(MakeError(kEditBridgeFailed, "張りの強さは 0 より大きくしてください。",
                "いまの値 " + Mm(tension, 2) + "。"));
        }
        // 縁 B の向きを、縁 A と同じ端から始まるようにそろえる。
        const auto [a0, a1] = EndsOf(edgeA.Value());
        const auto [b0, b1] = EndsOf(edgeB.Value());
        const bool reversed = a0.Distance(b1) + a1.Distance(b0) < a0.Distance(b0) + a1.Distance(b1);
        constexpr int kAlong = 24;
        constexpr int kAcross = 16;
        auto frameA = FrameOf(faceA.Value(), edgeA.Value(), false, kAlong);
        auto frameB = FrameOf(faceB.Value(), edgeB.Value(), reversed, kAlong);
        if (!frameA.HasValue() || !frameB.HasValue()) {
            return Out::Failure(frameA.HasValue() ? frameB.Diagnostics() : frameA.Diagnostics());
        }
        NCollection_Array2<gp_Pnt> grid(1, kAlong + 1, 1, kAcross + 1);
        std::vector<gp_Pnt> sideStart;
        std::vector<gp_Pnt> sideEnd;
        for (int k = 0; k <= kAlong; ++k) {
            const gp_Pnt& pa = frameA.Value().points[static_cast<std::size_t>(k)];
            const gp_Pnt& pb = frameB.Value().points[static_cast<std::size_t>(k)];
            const gp_Vec chord(pa, pb);
            const double length = chord.Magnitude();
            if (!(length > 1.0e-9)) {
                return Out::Failure(MakeError(kEditBridgeFailed,
                    "2 本の縁が重なっているところがあります。", "離れた縁どうしをつないでください。"));
            }
            // 縁から出る向き。G0 は向きを決めない(相手へまっすぐ)。相手の側を向かせる。
            gp_Vec da = firstOrder == SurfaceContinuity::G0 ? chord.Normalized()
                                                            : frameA.Value().outward[static_cast<std::size_t>(k)];
            if (da.Dot(chord) < 0.0) {
                da.Reverse();
            }
            gp_Vec db = secondOrder == SurfaceContinuity::G0 ? chord.Reversed().Normalized()
                                                             : frameB.Value().outward[static_cast<std::size_t>(k)];
            if (db.Dot(chord) > 0.0) {
                db.Reverse();
            }
            const double magnitude = tension * length;
            for (int j = 0; j <= kAcross; ++j) {
                const double s = static_cast<double>(j) / kAcross;
                const gp_Pnt point = Hermite(pa, da * magnitude, pb, db.Reversed() * magnitude, s);
                grid.SetValue(k + 1, j + 1, point);
                if (k == 0) {
                    sideStart.push_back(point);
                }
                if (k == kAlong) {
                    sideEnd.push_back(point);
                }
            }
        }
        GeomAPI_PointsToBSplineSurface initial(grid, 3, 8, GeomAbs_C2, 1.0e-3);
        if (!initial.IsDone() || initial.Surface().IsNull()) {
            return Out::Failure(MakeError(kEditBridgeFailed, "つなぐ面の初期形を作れませんでした。", {}));
        }
        const occ::handle<Geom_Surface> initialSurface = initial.Surface();
        BRepBuilderAPI_MakeFace initialFace{initialSurface, Precision::Confusion()};
        auto sideA = EdgeThroughPoints(sideStart);
        auto sideB = EdgeThroughPoints(sideEnd);
        if (!sideA.HasValue() || !sideB.HasValue() || !initialFace.IsDone()) {
            return Out::Failure(sideA.HasValue() ? sideB.Diagnostics() : sideA.Diagnostics());
        }
        BRepOffsetAPI_MakeFilling filler(3, 15, 2, false, 1.0e-5,
            std::max(1.0e-4, tolerance.modelLinearMm), 0.01, 0.1, 8, 12);
        if (firstOrder == SurfaceContinuity::G0) {
            filler.Add(edgeA.Value(), GeomAbs_C0, true);
        } else {
            filler.Add(edgeA.Value(), faceA.Value(), OrderOf(firstOrder), true);
        }
        filler.Add(sideB.Value(), GeomAbs_C0, true);
        if (secondOrder == SurfaceContinuity::G0) {
            filler.Add(edgeB.Value(), GeomAbs_C0, true);
        } else {
            filler.Add(edgeB.Value(), faceB.Value(), OrderOf(secondOrder), true);
        }
        filler.Add(sideA.Value(), GeomAbs_C0, true);
        filler.LoadInitSurface(initialFace.Face());
        filler.Build();
        if (!filler.IsDone()) {
            return Out::Failure(MakeError(kEditBridgeFailed, "2 本の縁をつなぐ面を張れませんでした。",
                "縁どうしの向きか、張りの強さを見直してください。"));
        }
        const TopoDS_Shape built = filler.Shape();
        const TopoDS_Face resultFace = FirstFaceOf(built);
        detail::EdgeContinuity measuredA;
        detail::EdgeContinuity measuredB;
        if (firstOrder != SurfaceContinuity::G0) {
            const auto checked = CheckContinuity(resultFace, faceA.Value(), edgeA.Value(),
                firstOrder, "縁 A");
            if (!checked.HasValue()) {
                return Out::Failure(checked.Diagnostics());
            }
            measuredA = checked.Value();
        }
        if (secondOrder != SurfaceContinuity::G0) {
            const auto checked = CheckContinuity(resultFace, faceB.Value(), edgeB.Value(),
                secondOrder, "縁 B");
            if (!checked.HasValue()) {
                return Out::Failure(checked.Diagnostics());
            }
            measuredB = checked.Value();
        }
        auto finished = detail::FinishSurfaceResult(built, tolerance);
        if (!finished.HasValue()) {
            return Out::Failure(finished.Diagnostics());
        }
        SurfaceEditResult result;
        result.surface = finished.Value();
        result.continuityG1Deg = std::max(firstOrder == SurfaceContinuity::G0 ? -1.0 : measuredA.g1Deg,
            secondOrder == SurfaceContinuity::G0 ? -1.0 : measuredB.g1Deg);
        const bool anyG2 = firstOrder == SurfaceContinuity::G2 || secondOrder == SurfaceContinuity::G2;
        result.continuityG2 = anyG2
            ? std::max(firstOrder == SurfaceContinuity::G2 ? measuredA.g2 : 0.0,
                  secondOrder == SurfaceContinuity::G2 ? measuredB.g2 : 0.0)
            : -1.0;
        result.noteJa = "縁 A を " + ContinuityWords(firstOrder, measuredA) + "、縁 B を "
            + ContinuityWords(secondOrder, measuredB) + " でつなぐ面を作りました(張り "
            + Mm(tension, 2) + ")。";
        return Out::Success(std::move(result), finished.Diagnostics());
    }, kEditBridgeFailed, "面をつなぐの");
}

namespace {

[[nodiscard]] bool IsAnalytic(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    switch (surface.GetType()) {
    case GeomAbs_Plane:
    case GeomAbs_Cylinder:
    case GeomAbs_Cone:
    case GeomAbs_Sphere:
    case GeomAbs_Torus:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] int PoleCount(const occ::handle<Geom_Surface>& surface)
{
    const occ::handle<Geom_BSplineSurface> spline =
        GeomConvert::SurfaceToBSplineSurface(surface);
    return spline.IsNull() ? 0 : spline->NbUPoles() * spline->NbVPoles();
}

//! 新しい面を、元の面と同じ縁で切る(切っていない面なら全体を使う)。
[[nodiscard]] TopoDS_Face TrimLike(const TopoDS_Face& original,
    const occ::handle<Geom_Surface>& surface)
{
    if (BRep_Tool::NaturalRestriction(original)) {
        BRepBuilderAPI_MakeFace maker{surface, Precision::Confusion()};
        return maker.IsDone() ? maker.Face() : TopoDS_Face();
    }
    const TopoDS_Wire outer = BRepTools::OuterWire(original);
    BRepBuilderAPI_MakeFace maker(surface, outer, true);
    if (!maker.IsDone()) {
        return TopoDS_Face();
    }
    for (TopExp_Explorer explorer(original, TopAbs_WIRE); explorer.More(); explorer.Next()) {
        const TopoDS_Wire wire = TopoDS::Wire(explorer.Current());
        if (!wire.IsSame(outer)) {
            maker.Add(wire);
        }
    }
    ShapeFix_Face fix(maker.Face());
    fix.Perform();
    return fix.Face();
}

} // namespace

Result<SurfaceEditResult> RefitSurface(const KernelShapeHandle& source, double toleranceMm,
    const GeometryTolerance& tolerance)
{
    using Out = Result<SurfaceEditResult>;
    return Guarded([&]() -> Out {
        const auto face = FaceOf(source);
        if (!face.HasValue()) {
            return Out::Failure(face.Diagnostics());
        }
        if (!(toleranceMm > 0.0)) {
            return Out::Failure(MakeError(kEditRefitFailed, "許容は 0 より大きくしてください。",
                "いまの値 " + Mm(toleranceMm, 4) + " mm。"));
        }
        if (IsAnalytic(face.Value())) {
            return Out::Failure(MakeError(kEditRefitFailed,
                "平面・円筒・円錐・球などの面は、すでにいちばん簡単な形です。",
                "整え直す必要はありません(形を変えずに制御点を減らす余地がありません)。"));
        }
        const occ::handle<Geom_Surface> original = BRep_Tool::Surface(face.Value());
        const int before = PoleCount(original);
        double u0 = 0.0;
        double u1 = 0.0;
        double v0 = 0.0;
        double v1 = 0.0;
        BRepTools::UVBounds(face.Value(), u0, u1, v0, v1);
        constexpr int kSamples = 30;
        NCollection_Array2<gp_Pnt> grid(1, kSamples + 1, 1, kSamples + 1);
        for (int i = 0; i <= kSamples; ++i) {
            for (int j = 0; j <= kSamples; ++j) {
                grid.SetValue(i + 1, j + 1,
                    original->Value(u0 + (u1 - u0) * i / kSamples, v0 + (v1 - v0) * j / kSamples));
            }
        }
        // 3 次、C2。形は許容の半分で合わせ、残りを縁の切り直しの余裕に取る。
        GeomAPI_PointsToBSplineSurface fit(grid, 3, 3, GeomAbs_C2, toleranceMm * 0.5);
        if (!fit.IsDone() || fit.Surface().IsNull()) {
            return Out::Failure(MakeError(kEditRefitFailed, "面を整え直せませんでした。",
                "許容を大きくしてください。"));
        }
        const occ::handle<Geom_Surface> refit = fit.Surface();
        const int after = fit.Surface()->NbUPoles() * fit.Surface()->NbVPoles();
        if (before > 0 && after >= before) {
            return Out::Failure(MakeError(kEditRefitFailed,
                "整えても制御点が減りません(前 " + std::to_string(before) + "、後 "
                    + std::to_string(after) + ")。",
                "許容を大きくするか、この面はそのまま使ってください。形は変えていません。"));
        }
        const TopoDS_Face made = TrimLike(face.Value(), refit);
        if (made.IsNull()) {
            return Out::Failure(MakeError(kEditRefitFailed, "整えた面を元の縁で切れませんでした。", {}));
        }
        const double moved = std::max(MaximumDistance(face.Value(), made),
            MaximumDistance(made, face.Value()));
        if (moved > toleranceMm) {
            return Out::Failure(MakeError(kEditRefitFailed,
                "整えると、元の面から許容を超えて動きます。",
                "最大 " + Mm(moved, 4) + " mm(許容 " + Mm(toleranceMm, 4)
                    + " mm)。許容を大きくしてください。"));
        }
        auto finished = detail::FinishSurfaceResult(made, tolerance);
        if (!finished.HasValue()) {
            return Out::Failure(finished.Diagnostics());
        }
        SurfaceEditResult result;
        result.surface = finished.Value();
        result.deviationMm = moved;
        result.polesBefore = before;
        result.polesAfter = after;
        result.noteJa = "制御点 " + std::to_string(before) + " → " + std::to_string(after)
            + "、元の面から最大 " + Mm(moved, 4) + " mm(許容 " + Mm(toleranceMm, 4) + " mm)。";
        return Out::Success(std::move(result), finished.Diagnostics());
    }, kEditRefitFailed, "面を整えるの");
}

Result<SurfaceEditResult> MirrorSurface(const KernelShapeHandle& source,
    const Vector3& planePoint, const Vector3& planeNormal, const GeometryTolerance& tolerance)
{
    using Out = Result<SurfaceEditResult>;
    return Guarded([&]() -> Out {
        const auto face = FaceOf(source);
        if (!face.HasValue()) {
            return Out::Failure(face.Diagnostics());
        }
        const double length = planeNormal.Length();
        if (!(length > 1.0e-12)) {
            return Out::Failure(MakeError(kEditMirrorFailed, "対称面の向きがありません。", {}));
        }
        const gp_Dir normal(planeNormal.x / length, planeNormal.y / length, planeNormal.z / length);
        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2(ToPoint(planePoint), normal));
        BRepBuilderAPI_Transform transform(face.Value(), mirror, true);
        if (!transform.IsDone()) {
            return Out::Failure(MakeError(kEditMirrorFailed, "面を対称に写せませんでした。", {}));
        }
        const TopoDS_Shape mirrored = transform.Shape();
        // 元の面の縁のうち、対称面に乗っているもの(境目)で、折れ目を測る。
        const double onPlane = std::max(tolerance.interactiveJoinMm, 1.0e-3);
        const gp_Vec axis(normal);
        const gp_Pnt origin = ToPoint(planePoint);
        const occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face.Value());
        double worst = -1.0;
        for (const TopoDS_Edge& edge : EdgesOf(face.Value())) {
            if (BRep_Tool::Degenerated(edge)) {
                continue;
            }
            const auto points = EdgePolyline(edge);
            bool seam = true;
            for (const Vector3& point : points) {
                seam = seam && std::abs(gp_Vec(origin, ToPoint(point)).Dot(axis)) <= onPlane;
            }
            if (!seam) {
                continue;
            }
            for (std::size_t k = 1; k + 1 < points.size(); ++k) {
                gp_Vec n;
                if (!NormalNear(surface, ToPoint(points[k]), n)) {
                    continue;
                }
                // 写した面の法線は n − 2(n・N)N。2 本の法線の角度が境目の折れ目。
                const double along = n.Dot(axis);
                const double cosine = std::abs(1.0 - 2.0 * along * along);
                worst = std::max(worst, std::acos(std::min(1.0, cosine)) * 180.0 / 3.14159265358979323846);
            }
        }
        auto finished = detail::FinishSurfaceResult(mirrored, tolerance);
        if (!finished.HasValue()) {
            return Out::Failure(finished.Diagnostics());
        }
        SurfaceEditResult result;
        result.surface = finished.Value();
        std::vector<base::Diagnostic> warnings = finished.Diagnostics();
        if (worst < 0.0) {
            result.noteJa = "対称に写しました。元の面は対称面に接していないので、境目はありません。";
            warnings.push_back(base::MakeWarning("KER-D101",
                "元の面が対称面に接していないので、境目はありません。",
                "左右をつなぐときは、面の縁を対称面の上に置いてください。"));
        } else {
            result.continuityG1Deg = worst;
            if (worst <= detail::kContinuityG1LimitDeg) {
                // 対称な形は、境目で向きがそろえば曲がり方もそろう(G2)。
                result.continuityG2 = 0.0;
                result.noteJa = "対称に写しました。境目は G1(折れ目 最大 " + Mm(worst)
                    + " 度)で、対称なので曲がり方もそろいます(G2)。";
            } else {
                result.noteJa = "対称に写しました。境目で最大 " + Mm(worst)
                    + " 度折れています(面が対称面に直角に当たっていません)。";
                warnings.push_back(base::MakeWarning("KER-D102",
                    "対称面の境目が折れています。",
                    "最大 " + Mm(worst) + " 度。対称面の上の縁で、面が対称面に直角に当たるように"
                        "すると滑らかにつながります。"));
            }
        }
        return Out::Success(std::move(result), std::move(warnings));
    }, kEditMirrorFailed, "対称に写すの");
}

Result<WireList> ExtractIsoCurves(const KernelShapeHandle& source, int direction, int count,
    const GeometryTolerance& tolerance)
{
    using Out = Result<WireList>;
    return Guarded([&]() -> Out {
        const auto face = FaceOf(source);
        if (!face.HasValue()) {
            return Out::Failure(face.Diagnostics());
        }
        if (count < 1 || count > 50) {
            return Out::Failure(MakeError(kEditIsoFailed, "本数は 1〜50 本にしてください。",
                "いまの値 " + std::to_string(count) + "。"));
        }
        const occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face.Value());
        double u0 = 0.0;
        double u1 = 0.0;
        double v0 = 0.0;
        double v1 = 0.0;
        BRepTools::UVBounds(face.Value(), u0, u1, v0, v1);
        WireList wires;
        for (int pass = 0; pass < 2; ++pass) {
            // pass 0 = U 方向の線(V 一定)、pass 1 = V 方向の線(U 一定)。
            if ((pass == 0 && direction == 1) || (pass == 1 && direction == 0)) {
                continue;
            }
            for (int i = 1; i <= count; ++i) {
                const double fixed = pass == 0 ? v0 + (v1 - v0) * i / (count + 1)
                                               : u0 + (u1 - u0) * i / (count + 1);
                const occ::handle<Geom_Curve> iso = pass == 0 ? surface->VIso(fixed)
                                                              : surface->UIso(fixed);
                const double from = pass == 0 ? u0 : v0;
                const double to = pass == 0 ? u1 : v1;
                // 面の外(穴・切り欠き)を通るところは切る。
                constexpr int kSteps = 200;
                int runStart = -1;
                for (int k = 0; k <= kSteps + 1; ++k) {
                    bool inside = false;
                    if (k <= kSteps) {
                        const double at = from + (to - from) * k / kSteps;
                        const gp_Pnt2d uv = pass == 0 ? gp_Pnt2d(at, fixed) : gp_Pnt2d(fixed, at);
                        inside = BRepClass_FaceClassifier(face.Value(), uv, 1.0e-7).State()
                            != TopAbs_OUT;
                    }
                    if (inside && runStart < 0) {
                        runStart = k;
                    }
                    if (!inside && runStart >= 0) {
                        const int runEnd = k - 1;
                        if (runEnd - runStart >= 2) {
                            const double a = from + (to - from) * runStart / kSteps;
                            const double b = from + (to - from) * runEnd / kSteps;
                            const occ::handle<Geom_Curve> piece = new Geom_TrimmedCurve(iso, a, b);
                            BRepBuilderAPI_MakeEdge maker{piece};
                            if (maker.IsDone()) {
                                auto segment = FromEdge(maker.Edge(), tolerance.modelLinearMm);
                                if (!segment.HasValue()) {
                                    return Out::Failure(segment.Diagnostics());
                                }
                                wires.push_back({segment.Value()});
                            }
                        }
                        runStart = -1;
                    }
                }
            }
        }
        if (wires.empty()) {
            return Out::Failure(MakeError(kEditIsoFailed, "面の U/V 線を取り出せませんでした。",
                "面の内側を通る線がありません。"));
        }
        return Out::Success(std::move(wires));
    }, kEditIsoFailed, "U/V 線の取り出し");
}

Result<WireList> ProjectWiresOntoSurface(const KernelShapeHandle& surface,
    const WireList& wires, const Vector3& direction, const GeometryTolerance& tolerance)
{
    using Out = Result<WireList>;
    return Guarded([&]() -> Out {
        const auto face = FaceOf(surface);
        if (!face.HasValue()) {
            return Out::Failure(face.Diagnostics());
        }
        const double length = direction.Length();
        if (!(length > 1.0e-12)) {
            return Out::Failure(MakeError(kEditProjectFailed, "落とす向きがありません。",
                "作業平面を決めてください。"));
        }
        const gp_Dir along(direction.x / length, direction.y / length, direction.z / length);
        WireList out;
        for (const auto& segments : wires) {
            const auto wire = ToWire(segments, std::max(tolerance.modelLinearMm, 1.0e-6));
            if (!wire.HasValue()) {
                return Out::Failure(wire.Diagnostics());
            }
            BRepProj_Projection projection(wire.Value(), face.Value(), along);
            if (!projection.IsDone()) {
                continue;
            }
            for (; projection.More(); projection.Next()) {
                const auto curves = FromWire(projection.Current(), tolerance.modelLinearMm);
                if (!curves.HasValue()) {
                    return Out::Failure(curves.Diagnostics());
                }
                if (!curves.Value().empty()) {
                    out.push_back(curves.Value());
                }
            }
        }
        if (out.empty()) {
            return Out::Failure(MakeError(kEditProjectFailed, "線が面に落ちませんでした。",
                "線を向きに沿って伸ばしても、面に当たりません。面の上に来るように置いてください。"));
        }
        return Out::Success(std::move(out));
    }, kEditProjectFailed, "面への投影");
}

#else // KACHACAD_V2_WITH_OCCT

namespace {

template<class T>
[[nodiscard]] Result<T> NoKernel()
{
    return Result<T>::Failure(MakeError(kEditSourceMissing,
        "この実行ファイルには幾何カーネルが入っていません。", "OCCT を有効にしてビルドしてください。"));
}

} // namespace

Result<SurfaceEdgeInfo> NearestSurfaceEdge(const KernelShapeHandle&, const Vector3&)
{
    return NoKernel<SurfaceEdgeInfo>();
}

Result<SurfaceEdgeInfo> SurfaceEdgeAt(const KernelShapeHandle&, int)
{
    return NoKernel<SurfaceEdgeInfo>();
}

Result<SurfaceEditResult> MatchSurfaceEdge(const KernelShapeHandle&, int,
    const KernelShapeHandle&, int, SurfaceContinuity, const GeometryTolerance&)
{
    return NoKernel<SurfaceEditResult>();
}

Result<SurfaceEditResult> BridgeSurfaceEdges(const KernelShapeHandle&, int, SurfaceContinuity,
    const KernelShapeHandle&, int, SurfaceContinuity, double, const GeometryTolerance&)
{
    return NoKernel<SurfaceEditResult>();
}

Result<SurfaceEditResult> RefitSurface(const KernelShapeHandle&, double, const GeometryTolerance&)
{
    return NoKernel<SurfaceEditResult>();
}

Result<SurfaceEditResult> MirrorSurface(const KernelShapeHandle&, const Vector3&, const Vector3&,
    const GeometryTolerance&)
{
    return NoKernel<SurfaceEditResult>();
}

Result<WireList> ExtractIsoCurves(const KernelShapeHandle&, int, int, const GeometryTolerance&)
{
    return NoKernel<WireList>();
}

Result<WireList> ProjectWiresOntoSurface(const KernelShapeHandle&, const WireList&,
    const Vector3&, const GeometryTolerance&)
{
    return NoKernel<WireList>();
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
