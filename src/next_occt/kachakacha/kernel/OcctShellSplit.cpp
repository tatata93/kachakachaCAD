#include "kachakacha/kernel/OcctShellSplit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <exception>

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

[[nodiscard]] int CountOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum kind)
{
    int count = 0;
    for (TopExp_Explorer it(shape, kind); it.More(); it.Next()) {
        ++count;
    }
    return count;
}

[[nodiscard]] std::vector<TopoDS_Face> FacesOf(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_FACE, map);
    std::vector<TopoDS_Face> faces;
    for (int index = 1; index <= map.Extent(); ++index) {
        faces.push_back(TopoDS::Face(map(index)));
    }
    return faces;
}

//! 面の縁(辺ごとの折れ線)。下見に出す。
[[nodiscard]] std::vector<std::vector<Vector3>> OutlineOf(const TopoDS_Face& face)
{
    std::vector<std::vector<Vector3>> outline;
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
        std::vector<Vector3> points;
        constexpr int kCount = 24;
        for (int k = 0; k <= kCount; ++k) {
            points.push_back(FromPoint(curve.Value(first + (last - first) * k / kCount)));
        }
        outline.push_back(std::move(points));
    }
    return outline;
}

//! 点から面(縁で切られた範囲)までの距離。測れなければ無限大。
[[nodiscard]] double DistanceToFace(const TopoDS_Vertex& vertex, const TopoDS_Face& face,
    gp_Pnt* onFace = nullptr)
{
    BRepExtrema_DistShapeShape distance(vertex, face);
    if (!distance.IsDone() || distance.NbSolution() == 0) {
        return std::numeric_limits<double>::infinity();
    }
    if (onFace != nullptr) {
        *onFace = distance.PointOnShape2(1);
    }
    return distance.Value();
}

//! 点から、self 以外の面までの一番近い距離。
[[nodiscard]] double ClearanceFromOthers(const gp_Pnt& point, const std::vector<TopoDS_Face>& faces,
    std::size_t self)
{
    const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(point);
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < faces.size(); ++index) {
        if (index != self) {
            nearest = std::min(nearest, DistanceToFace(vertex, faces[index]));
        }
    }
    return nearest;
}

//! 面の上で、ほかの面から clearMm 以上離れた点。開き直して選び直すとき、隣の面と
//! 取り違えないため(押した点が辺のすぐそばでも、面の内側へ寄せる)。
//! 押した点から面の真ん中(UV)へ向かって順に試し、だめなら面の上を格子で探す。
[[nodiscard]] gp_Pnt InteriorPointOf(const std::vector<TopoDS_Face>& faces, std::size_t self,
    const gp_Pnt& start, double clearMm)
{
    const TopoDS_Face& face = faces[self];
    if (ClearanceFromOthers(start, faces, self) > clearMm) {
        return start;
    }
    const occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face);
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    double su = (u0 + u1) * 0.5;
    double sv = (v0 + v1) * 0.5;
    GeomAPI_ProjectPointOnSurf projection(start, surface);
    if (projection.IsDone() && projection.NbPoints() > 0) {
        projection.LowerDistanceParameters(su, sv);
    }
    std::vector<gp_Pnt2d> candidates;
    constexpr int kSteps = 10;
    for (int k = 1; k <= kSteps; ++k) {
        const double t = static_cast<double>(k) / kSteps;
        candidates.emplace_back(su + ((u0 + u1) * 0.5 - su) * t, sv + ((v0 + v1) * 0.5 - sv) * t);
    }
    constexpr int kGrid = 6;
    for (int i = 1; i < kGrid; ++i) {
        for (int j = 1; j < kGrid; ++j) {
            candidates.emplace_back(u0 + (u1 - u0) * i / kGrid, v0 + (v1 - v0) * j / kGrid);
        }
    }
    for (const gp_Pnt2d& uv : candidates) {
        BRepClass_FaceClassifier inside(face, uv, 1.0e-7);
        if (inside.State() != TopAbs_IN) {
            continue;
        }
        const gp_Pnt point = surface->Value(uv.X(), uv.Y());
        if (ClearanceFromOthers(point, faces, self) > clearMm) {
            return point;
        }
    }
    return start;
}

[[nodiscard]] double AllowedMm(const geometry::GeometryTolerance& tolerance)
{
    return std::max({tolerance.modelLinearMm * 10.0, tolerance.interactiveJoinMm, 1.0e-4});
}

//! 面の上の点で指した面の番号。一番近い面が許す離れより遠い・2 枚目も同じくらい近い
//! (辺の上を指している)なら見つからない(-1)。
[[nodiscard]] int FaceIndexAt(const std::vector<TopoDS_Face>& faces, const Vector3& point,
    double allowedMm)
{
    const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(ToPoint(point));
    int best = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    double secondDistance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const double distance = DistanceToFace(vertex, faces[index]);
        if (distance < bestDistance) {
            secondDistance = bestDistance;
            bestDistance = distance;
            best = static_cast<int>(index);
        } else if (distance < secondDistance) {
            secondDistance = distance;
        }
    }
    if (bestDistance > allowedMm || secondDistance <= allowedMm) {
        return -1;
    }
    return best;
}

template <class T, class Body>
[[nodiscard]] Result<T> Guarded(Body&& body, const char* whatJa)
{
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return Result<T>::Failure(MakeError(kShellSplitFailed, std::string(whatJa) + "を作れませんでした。",
            std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return Result<T>::Failure(MakeError(kShellSplitFailed, std::string(whatJa) + "を作れませんでした。",
            error.what()));
    } catch (...) {
        return Result<T>::Failure(MakeError(kShellSplitFailed, std::string(whatJa) + "を作れませんでした。",
            "理由が分かりません。"));
    }
}

//! 部品の形。立体がちょうど 1 つなら、その立体だけを取り出す(複合形のままだと
//! シェルが断ることがある)。
[[nodiscard]] Result<TopoDS_Shape> SolidShapeOf(const KernelShapeHandle& solid)
{
    TopoDS_Shape shape;
    if (!solid.Valid() || !LookupShape(solid, shape) || shape.IsNull()) {
        return Result<TopoDS_Shape>::Failure(MakeError(kShellSplitFaceMissing,
            "部品の立体が見つかりません。", {}));
    }
    const int solids = CountOf(shape, TopAbs_SOLID);
    if (solids == 0) {
        return Result<TopoDS_Shape>::Failure(MakeError(kShellSplitFaceMissing,
            "部品が立体ではありません。", "面だけの形はシェル・分割できません。"));
    }
    if (solids == 1) {
        TopExp_Explorer it(shape, TopAbs_SOLID);
        return Result<TopoDS_Shape>::Success(it.Current());
    }
    return Result<TopoDS_Shape>::Success(shape);
}

} // namespace

Result<SolidFaceInfo> NearestSolidFace(const KernelShapeHandle& solid, const Vector3& point)
{
    return Guarded<SolidFaceInfo>([&]() -> Result<SolidFaceInfo> {
        const auto shape = SolidShapeOf(solid);
        if (!shape.HasValue()) {
            return Result<SolidFaceInfo>::Failure(shape.Diagnostics());
        }
        const auto faces = FacesOf(shape.Value());
        const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(ToPoint(point));
        int best = -1;
        double bestDistance = std::numeric_limits<double>::infinity();
        gp_Pnt bestPoint;
        for (std::size_t index = 0; index < faces.size(); ++index) {
            gp_Pnt onFace;
            const double distance = DistanceToFace(vertex, faces[index], &onFace);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<int>(index);
                bestPoint = onFace;
            }
        }
        if (best < 0) {
            return Result<SolidFaceInfo>::Failure(MakeError(kShellSplitFaceMissing,
                "部品に面が見つかりません。", {}));
        }
        const auto self = static_cast<std::size_t>(best);
        const double clear = AllowedMm(geometry::GeometryTolerance{}) * 4.0;
        SolidFaceInfo info;
        info.point = FromPoint(InteriorPointOf(faces, self, bestPoint, clear));
        info.outline = OutlineOf(faces[self]);
        info.distanceMm = bestDistance;
        info.faceIndex = best;
        return Result<SolidFaceInfo>::Success(std::move(info));
    }, "面の拾い出し");
}

Result<SolidFaceInfo> SolidFaceAt(const KernelShapeHandle& solid, const Vector3& point,
    const geometry::GeometryTolerance& tolerance)
{
    return Guarded<SolidFaceInfo>([&]() -> Result<SolidFaceInfo> {
        const auto shape = SolidShapeOf(solid);
        if (!shape.HasValue()) {
            return Result<SolidFaceInfo>::Failure(shape.Diagnostics());
        }
        const auto faces = FacesOf(shape.Value());
        const int index = FaceIndexAt(faces, point, AllowedMm(tolerance));
        if (index < 0) {
            return Result<SolidFaceInfo>::Failure(MakeError(kShellSplitFaceMissing,
                "指した面が部品にありません。", "部品の形が変わったかもしれません。面を選び直してください。"));
        }
        SolidFaceInfo info;
        info.point = point;
        info.outline = OutlineOf(faces[static_cast<std::size_t>(index)]);
        info.faceIndex = index;
        return Result<SolidFaceInfo>::Success(std::move(info));
    }, "面の拾い出し");
}

Result<ShellBuildResult> ShellSolid(const KernelShapeHandle& solid,
    const std::vector<Vector3>& facePoints, double thicknessMm,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<ShellBuildResult>;
    return Guarded<ShellBuildResult>([&]() -> Out {
        if (!(thicknessMm > tolerance.modelLinearMm) || !std::isfinite(thicknessMm)) {
            return Out::Failure(MakeError(kShellSplitFailed, "シェルの厚みが 0 です。",
                "残す肉厚を 0 より大きくしてください。"));
        }
        if (facePoints.empty()) {
            return Out::Failure(MakeError(kShellSplitFaceMissing, "抜く面を選んでいません。",
                "開けたい面を 1 枚以上押してください。"));
        }
        const auto shape = SolidShapeOf(solid);
        if (!shape.HasValue()) {
            return Out::Failure(shape.Diagnostics());
        }
        if (CountOf(shape.Value(), TopAbs_SOLID) != 1) {
            return Out::Failure(MakeError(kShellSplitFailed,
                "部品が 2 つ以上の立体に分かれているので、シェルにできません。",
                "先に 1 つの立体にしてください。"));
        }
        const auto faces = FacesOf(shape.Value());
        TopTools_ListOfShape closing;
        std::vector<int> chosen;
        for (std::size_t index = 0; index < facePoints.size(); ++index) {
            const int found = FaceIndexAt(faces, facePoints[index], AllowedMm(tolerance));
            if (found < 0) {
                return Out::Failure(MakeError(kShellSplitFaceMissing,
                    std::to_string(index + 1) + " 枚目の面が部品にありません。",
                    "部品の形が変わったかもしれません。面を選び直してください。"));
            }
            if (std::find(chosen.begin(), chosen.end(), found) == chosen.end()) {
                chosen.push_back(found);
                closing.Append(faces[static_cast<std::size_t>(found)]);
            }
        }
        const double before = VolumeOf(shape.Value());
        BRepOffsetAPI_MakeThickSolid maker;
        maker.MakeThickSolidByJoin(shape.Value(), closing, -thicknessMm, 1.0e-3);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kShellSplitFailed, "シェルを作れませんでした。",
                "肉厚が部品に対して大きすぎるか、曲がりがきつすぎます。肉厚を小さくしてください。"));
        }
        const TopoDS_Shape made = maker.Shape();
        if (made.IsNull() || !BRepCheck_Analyzer(made).IsValid()
            || CountOf(made, TopAbs_SOLID) == 0) {
            return Out::Failure(MakeError(kShellSplitInvalid,
                "シェルの形が立体として壊れているので、作れたことにしません。",
                "肉厚を小さくするか、抜く面を選び直してください。"));
        }
        const double after = VolumeOf(made);
        if (!(after > 0.0) || !(after < before * (1.0 - 1.0e-9))) {
            return Out::Failure(MakeError(kShellSplitInvalid,
                "シェルにしても体積が減らないので、作れたことにしません。", {}));
        }
        ShellBuildResult result;
        result.handle = StoreShape(made);
        result.volumeMm3 = after;
        result.previousVolumeMm3 = before;
        return Out::Success(result);
    }, "シェル");
}

namespace {

//! 分ける平面の面の半分の大きさ。平面の点から部品の外接箱のどこまでも届く長さに余裕を足す。
[[nodiscard]] double MarginFor(const TopoDS_Shape& shape, const Vector3& planeOrigin)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        return 1000.0;
    }
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z1 = 0.0;
    box.Get(x0, y0, z0, x1, y1, z1);
    const Vector3 low{x0, y0, z0};
    const Vector3 high{x1, y1, z1};
    const Vector3 center = (low + high) * 0.5;
    return std::max(geometry::Distance(center, planeOrigin) + geometry::Distance(low, high) * 2.0 + 10.0,
        1000.0);
}

//! 平面の片側(sidePoint の側)だけを残す。
[[nodiscard]] TopoDS_Shape SideOf(const TopoDS_Shape& shape, const TopoDS_Face& plane,
    const gp_Pnt& sidePoint)
{
    BRepPrimAPI_MakeHalfSpace half(plane, sidePoint);
    if (!half.IsDone()) {
        return {};
    }
    BRepAlgoAPI_Common common(shape, half.Solid());
    common.Build();
    if (!common.IsDone()) {
        return {};
    }
    return common.Shape();
}

} // namespace

Result<SplitBuildResult> SplitSolidByPlane(const KernelShapeHandle& solid,
    const Vector3& planeOrigin, const Vector3& planeNormal,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<SplitBuildResult>;
    return Guarded<SplitBuildResult>([&]() -> Out {
        const double length = planeNormal.Length();
        if (!planeNormal.IsFinite() || !planeOrigin.IsFinite() || !(length > 1.0e-12)) {
            return Out::Failure(MakeError(kShellSplitFailed, "分ける平面の向きが決まりません。", {}));
        }
        const Vector3 normal = planeNormal * (1.0 / length);
        const auto shape = SolidShapeOf(solid);
        if (!shape.HasValue()) {
            return Out::Failure(shape.Diagnostics());
        }
        // 無限の面ではなく、部品を十分に覆う有限の面で切る(厚みを平面で切る道と同じ形。
        // 無限の形をブール演算へ渡さない)。
        const double margin = MarginFor(shape.Value(), planeOrigin);
        BRepBuilderAPI_MakeFace maker(gp_Pln(ToPoint(planeOrigin), gp_Dir(normal.x, normal.y, normal.z)),
            -margin, margin, -margin, margin);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kShellSplitFailed, "分ける平面の面を作れませんでした。", {}));
        }
        const TopoDS_Face plane = maker.Face();
        const TopoDS_Shape positive = SideOf(shape.Value(), plane, ToPoint(planeOrigin + normal));
        const TopoDS_Shape negative = SideOf(shape.Value(), plane, ToPoint(planeOrigin - normal));
        const double before = VolumeOf(shape.Value());
        const double minimum = std::max(before * 1.0e-9, tolerance.modelLinearMm);
        const bool positiveEmpty = positive.IsNull() || CountOf(positive, TopAbs_SOLID) == 0
            || VolumeOf(positive) <= minimum;
        const bool negativeEmpty = negative.IsNull() || CountOf(negative, TopAbs_SOLID) == 0
            || VolumeOf(negative) <= minimum;
        if (positiveEmpty || negativeEmpty) {
            return Out::Failure(MakeError(kShellSplitNoCut,
                "分ける平面が部品を通っていないので、2 つに分かれません。",
                "平面の位置か向きを変えてください。"));
        }
        if (!BRepCheck_Analyzer(positive).IsValid() || !BRepCheck_Analyzer(negative).IsValid()) {
            return Out::Failure(MakeError(kShellSplitInvalid,
                "分けた形が立体として壊れているので、作れたことにしません。", {}));
        }
        SplitBuildResult result;
        result.positiveVolumeMm3 = VolumeOf(positive);
        result.negativeVolumeMm3 = VolumeOf(negative);
        const double sum = result.positiveVolumeMm3 + result.negativeVolumeMm3;
        if (std::abs(sum - before) > std::max(before * 1.0e-6, tolerance.modelLinearMm)) {
            return Out::Failure(MakeError(kShellSplitInvalid,
                "分けた 2 つの体積の和が元と合わないので、作れたことにしません。", {}));
        }
        result.positiveSolidCount = CountOf(positive, TopAbs_SOLID);
        result.negativeSolidCount = CountOf(negative, TopAbs_SOLID);
        result.positive = StoreShape(positive);
        result.negative = StoreShape(negative);
        return Out::Success(result);
    }, "分割");
}

#else

namespace {
[[nodiscard]] base::Diagnostic NoKernel()
{
    return MakeError(kShellSplitUnsupported, "この実行ファイルには幾何カーネルが入っていません。", {});
}
} // namespace

Result<SolidFaceInfo> NearestSolidFace(const KernelShapeHandle&, const Vector3&)
{
    return Result<SolidFaceInfo>::Failure(NoKernel());
}

Result<SolidFaceInfo> SolidFaceAt(const KernelShapeHandle&, const Vector3&,
    const geometry::GeometryTolerance&)
{
    return Result<SolidFaceInfo>::Failure(NoKernel());
}

Result<ShellBuildResult> ShellSolid(const KernelShapeHandle&, const std::vector<Vector3>&, double,
    const geometry::GeometryTolerance&)
{
    return Result<ShellBuildResult>::Failure(NoKernel());
}

Result<SplitBuildResult> SplitSolidByPlane(const KernelShapeHandle&, const Vector3&,
    const Vector3&, const geometry::GeometryTolerance&)
{
    return Result<SplitBuildResult>::Failure(NoKernel());
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
