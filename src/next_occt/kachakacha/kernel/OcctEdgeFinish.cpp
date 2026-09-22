#include "kachakacha/kernel/OcctEdgeFinish.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>

#endif

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using geometry::Vector3;
using modeling::KernelShapeHandle;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

[[nodiscard]] double VolumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return std::abs(properties.Mass());
}

[[nodiscard]] std::vector<TopoDS_Edge> EdgesOf(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_EDGE, map);
    std::vector<TopoDS_Edge> edges;
    for (int index = 1; index <= map.Extent(); ++index) {
        const TopoDS_Edge edge = TopoDS::Edge(map(index));
        if (!BRep_Tool::Degenerated(edge)) {
            edges.push_back(edge);
        }
    }
    return edges;
}

[[nodiscard]] Vector3 MidpointOf(const TopoDS_Edge& edge)
{
    BRepAdaptor_Curve curve(edge);
    return FromPoint(curve.Value((curve.FirstParameter() + curve.LastParameter()) * 0.5));
}

[[nodiscard]] std::vector<Vector3> PolylineOf(const TopoDS_Edge& edge)
{
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    std::vector<Vector3> points;
    constexpr int kCount = 24;
    for (int k = 0; k <= kCount; ++k) {
        points.push_back(FromPoint(curve.Value(first + (last - first) * k / kCount)));
    }
    return points;
}

//! 真ん中の点が一番近い辺。許す離れ(tolerance)より遠ければ見つからない。
[[nodiscard]] int EdgeIndexNear(const std::vector<TopoDS_Edge>& edges, const Vector3& midpoint,
    double allowedMm)
{
    int best = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const double distance = geometry::Distance(MidpointOf(edges[index]), midpoint);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(index);
        }
    }
    return bestDistance <= allowedMm ? best : -1;
}

template <class T, class Body>
[[nodiscard]] Result<T> Guarded(Body&& body, const char* whatJa)
{
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return Result<T>::Failure(MakeError(kEdgeFinishFailed, std::string(whatJa) + "を作れませんでした。",
            std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return Result<T>::Failure(MakeError(kEdgeFinishFailed, std::string(whatJa) + "を作れませんでした。",
            error.what()));
    } catch (...) {
        return Result<T>::Failure(MakeError(kEdgeFinishFailed, std::string(whatJa) + "を作れませんでした。",
            "理由が分かりません。"));
    }
}

[[nodiscard]] double AllowedMm(const geometry::GeometryTolerance& tolerance)
{
    return std::max({tolerance.modelLinearMm * 10.0, tolerance.interactiveJoinMm, 1.0e-4});
}

} // namespace

Result<SolidEdgeInfo> NearestSolidEdge(const KernelShapeHandle& solid, const Vector3& point)
{
    return Guarded<SolidEdgeInfo>([&]() -> Result<SolidEdgeInfo> {
        TopoDS_Shape shape;
        if (!solid.Valid() || !LookupShape(solid, shape)) {
            return Result<SolidEdgeInfo>::Failure(MakeError(kEdgeFinishEdgeMissing,
                "部品の立体が見つかりません。", {}));
        }
        const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(ToPoint(point));
        const auto edges = EdgesOf(shape);
        int best = -1;
        double bestDistance = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < edges.size(); ++index) {
            BRepExtrema_DistShapeShape distance(vertex, edges[index]);
            if (distance.IsDone() && distance.Value() < bestDistance) {
                bestDistance = distance.Value();
                best = static_cast<int>(index);
            }
        }
        if (best < 0) {
            return Result<SolidEdgeInfo>::Failure(MakeError(kEdgeFinishEdgeMissing,
                "部品に辺が見つかりません。", {}));
        }
        SolidEdgeInfo info;
        info.midpoint = MidpointOf(edges[static_cast<std::size_t>(best)]);
        info.polyline = PolylineOf(edges[static_cast<std::size_t>(best)]);
        info.distanceMm = bestDistance;
        return Result<SolidEdgeInfo>::Success(std::move(info));
    }, "辺の拾い出し");
}

Result<SolidEdgeInfo> SolidEdgeAt(const KernelShapeHandle& solid, const Vector3& midpoint,
    const geometry::GeometryTolerance& tolerance)
{
    return Guarded<SolidEdgeInfo>([&]() -> Result<SolidEdgeInfo> {
        TopoDS_Shape shape;
        if (!solid.Valid() || !LookupShape(solid, shape)) {
            return Result<SolidEdgeInfo>::Failure(MakeError(kEdgeFinishEdgeMissing,
                "部品の立体が見つかりません。", {}));
        }
        const auto edges = EdgesOf(shape);
        const int index = EdgeIndexNear(edges, midpoint, AllowedMm(tolerance));
        if (index < 0) {
            return Result<SolidEdgeInfo>::Failure(MakeError(kEdgeFinishEdgeMissing,
                "指した辺が部品にありません。", "部品の形が変わったかもしれません。辺を選び直してください。"));
        }
        SolidEdgeInfo info;
        info.midpoint = MidpointOf(edges[static_cast<std::size_t>(index)]);
        info.polyline = PolylineOf(edges[static_cast<std::size_t>(index)]);
        return Result<SolidEdgeInfo>::Success(std::move(info));
    }, "辺の拾い出し");
}

Result<EdgeFinishResult> FinishSolidEdges(const KernelShapeHandle& solid, EdgeFinishKind kind,
    double sizeMm, const std::vector<Vector3>& edgeMidpoints,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<EdgeFinishResult>;
    const char* what = kind == EdgeFinishKind::Fillet ? "丸め(フィレット)" : "面取り";
    return Guarded<EdgeFinishResult>([&]() -> Out {
        if (!(sizeMm > tolerance.modelLinearMm)) {
            return Out::Failure(MakeError(kEdgeFinishFailed,
                std::string(what) + "の大きさが 0 です。", "半径・距離を 0 より大きくしてください。"));
        }
        if (edgeMidpoints.empty()) {
            return Out::Failure(MakeError(kEdgeFinishEdgeMissing, "辺を選んでいません。", {}));
        }
        TopoDS_Shape shape;
        if (!solid.Valid() || !LookupShape(solid, shape)) {
            return Out::Failure(MakeError(kEdgeFinishEdgeMissing, "部品の立体が見つかりません。", {}));
        }
        const auto edges = EdgesOf(shape);
        std::vector<int> chosen;
        for (std::size_t index = 0; index < edgeMidpoints.size(); ++index) {
            const int found = EdgeIndexNear(edges, edgeMidpoints[index], AllowedMm(tolerance));
            if (found < 0) {
                return Out::Failure(MakeError(kEdgeFinishEdgeMissing,
                    std::to_string(index + 1) + " 本目の辺が部品にありません。",
                    "部品の形が変わったかもしれません。辺を選び直してください。"));
            }
            if (std::find(chosen.begin(), chosen.end(), found) == chosen.end()) {
                chosen.push_back(found);
            }
        }
        const double before = VolumeOf(shape);
        TopoDS_Shape made;
        if (kind == EdgeFinishKind::Fillet) {
            BRepFilletAPI_MakeFillet fillet(shape);
            for (const int index : chosen) {
                fillet.Add(sizeMm, edges[static_cast<std::size_t>(index)]);
            }
            fillet.Build();
            if (!fillet.IsDone()) {
                return Out::Failure(MakeError(kEdgeFinishFailed, "丸め(フィレット)を作れませんでした。",
                    "半径が大きすぎるか、隣の面とぶつかります。半径を小さくしてください。"));
            }
            made = fillet.Shape();
        } else {
            BRepFilletAPI_MakeChamfer chamfer(shape);
            for (const int index : chosen) {
                chamfer.Add(sizeMm, edges[static_cast<std::size_t>(index)]);
            }
            chamfer.Build();
            if (!chamfer.IsDone()) {
                return Out::Failure(MakeError(kEdgeFinishFailed, "面取りを作れませんでした。",
                    "距離が大きすぎるか、隣の面とぶつかります。距離を小さくしてください。"));
            }
            made = chamfer.Shape();
        }
        if (made.IsNull() || !BRepCheck_Analyzer(made).IsValid()) {
            return Out::Failure(MakeError(kEdgeFinishInvalid,
                std::string(what) + "の形が立体として壊れているので、作れたことにしません。",
                "大きさを小さくするか、辺を選び直してください。"));
        }
        const double after = VolumeOf(made);
        if (!(after > 0.0) || std::abs(after - before) <= before * 1.0e-9) {
            return Out::Failure(MakeError(kEdgeFinishInvalid,
                std::string(what) + "をしても体積が変わらないので、作れたことにしません。", {}));
        }
        EdgeFinishResult result;
        result.handle = StoreShape(made);
        result.volumeMm3 = after;
        result.previousVolumeMm3 = before;
        return Out::Success(result);
    }, what);
}

#else

Result<SolidEdgeInfo> NearestSolidEdge(const KernelShapeHandle&, const Vector3&)
{
    return Result<SolidEdgeInfo>::Failure(MakeError(kEdgeFinishUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<SolidEdgeInfo> SolidEdgeAt(const KernelShapeHandle&, const Vector3&,
    const geometry::GeometryTolerance&)
{
    return Result<SolidEdgeInfo>::Failure(MakeError(kEdgeFinishUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<EdgeFinishResult> FinishSolidEdges(const KernelShapeHandle&, EdgeFinishKind, double,
    const std::vector<Vector3>&, const geometry::GeometryTolerance&)
{
    return Result<EdgeFinishResult>::Failure(MakeError(kEdgeFinishUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
