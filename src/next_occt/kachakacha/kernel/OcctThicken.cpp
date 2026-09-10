#include "kachakacha/kernel/OcctThicken.h"

#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <exception>

#endif

namespace kachakacha::v2::kernel {
namespace {

using base::MakeError;
using base::Result;

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! 立体の辺を core の曲線へ直す。画面に出すために使う。
//! 直せない辺は飛ばす。飛ばしても立体は正しいので、断る理由にはしない。
[[nodiscard]] std::vector<geometry::CurveSegment> EdgesOf(const TopoDS_Shape& shape,
    const geometry::GeometryTolerance& tolerance)
{
    std::vector<geometry::CurveSegment> edges;
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto made =
            FromEdge(TopoDS::Edge(explorer.Current()), tolerance.modelLinearMm);
        if (made.HasValue()) {
            edges.push_back(made.Value());
        }
    }
    return edges;
}

[[nodiscard]] double VolumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return std::abs(properties.Mass());
}

//! 面を法線方向へずらした面(シェル)を作る。中央付け・内側付けの下ごしらえ。
[[nodiscard]] Result<TopoDS_Shape> OffsetSurface(const TopoDS_Shape& face,
    double distanceMm)
{
    using Out = Result<TopoDS_Shape>;
    try {
        BRepOffsetAPI_MakeOffsetShape maker;
        maker.PerformBySimple(face, distanceMm);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kThickenFailed,
                "面に厚みを付けられませんでした。",
                "面をずらせませんでした。厚みが曲がりに対して大きすぎるか、"
                "ずらした面が自分自身と交わります。厚みを小さくしてください。"));
        }
        return Out::Success(maker.Shape());
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", "幾何カーネルが失敗しました。"));
    }
}

//! 面(またはシェル)を、法線方向へ厚みぶん膨らませて立体にする。
[[nodiscard]] Result<TopoDS_Shape> ThickSolid(const TopoDS_Shape& base,
    double thicknessMm)
{
    using Out = Result<TopoDS_Shape>;
    try {
        BRepOffsetAPI_MakeThickSolid maker;
        maker.MakeThickSolidBySimple(base, thicknessMm);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kThickenFailed,
                "面に厚みを付けられませんでした。",
                "厚みが曲がりに対して大きすぎるか、面が自分自身と交わります。"
                "厚みを小さくするか、面を分けてください。"));
        }
        return Out::Success(maker.Shape());
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", "幾何カーネルが失敗しました。"));
    }
}

} // namespace

Result<ThickenedSolid> ThickenSurface(modeling::KernelShapeHandle sourceShape,
    double thicknessMm, fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<ThickenedSolid>;
    if (!(thicknessMm > 0.0) || !std::isfinite(thicknessMm)) {
        return Out::Failure(MakeError(kThickenBadThickness,
            "厚みが正の数ではありません。",
            "0mm の板は作れません。付けたい厚みを入れてください。"));
    }
    TopoDS_Shape face;
    if (!LookupShape(sourceShape, face) || face.IsNull()) {
        return Out::Failure(MakeError(kThickenSourceMissing,
            "元になる面がありません。",
            "先に形状ガイドで面を作ってから、厚みを付けてください。"));
    }
    // どこから膨らませ始めるかを、厚みの付け方で決める。
    //   外側 : 元の面から外へ    → 元の面が内側の皮になる
    //   中央 : 半分だけ内へ下げてから膨らませる → 元の面が厚みの真ん中に残る
    //   内側 : 厚みぶん内へ下げてから膨らませる → 元の面が外側の皮になる
    // 「膨らませてから半分戻す」ではないのは、曲がった面では戻し方が一意に
    // 決まらず、元の面が動いてしまうからである。
    const double startOffset =
        placement == fabrication::ThicknessPlacement::Centered ? -thicknessMm * 0.5
        : placement == fabrication::ThicknessPlacement::Inside ? -thicknessMm
                                                               : 0.0;
    TopoDS_Shape base = face;
    if (startOffset != 0.0) {
        const auto moved = OffsetSurface(face, startOffset);
        if (!moved.HasValue()) {
            return Out::Failure(moved.Diagnostics());
        }
        base = moved.Value();
    }
    const auto built = ThickSolid(base, thicknessMm);
    if (!built.HasValue()) {
        return Out::Failure(built.Diagnostics());
    }
    const double volume = VolumeOf(built.Value());
    if (!(volume > 0.0)) {
        // 出来たと言われても、中身が無いなら立体ではない。
        // ここを飛ばすと「作れた」と言いながら STL が空になる。
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。",
            "体積が0になりました。面が閉じていないか、厚みが小さすぎます。"));
    }
    ThickenedSolid made;
    made.handle = StoreShape(built.Value());
    made.volumeMm3 = volume;
    made.thicknessMm = thicknessMm;
    made.edges = EdgesOf(built.Value(), tolerance);
    return Out::Success(std::move(made));
}

namespace {

//! 面の点が、平面からどちら側へどれだけ離れているか。
struct PlaneReach {
    double maximumMm = 0.0;   //!< いちばん遠い点までの距離(正)
    int side = 0;             //!< +1 / -1。0 なら平面の上か、またいでいる
    geometry::Vector3 farthest{};
};

[[nodiscard]] Result<PlaneReach> MeasureReach(const TopoDS_Shape& face,
    const geometry::Vector3& origin, const geometry::Vector3& unitNormal,
    double toleranceMm)
{
    using Out = Result<PlaneReach>;
    PlaneReach reach;
    bool positive = false;
    bool negative = false;
    int vertices = 0;
    for (TopExp_Explorer explorer(face, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current()));
        const geometry::Vector3 p{point.X(), point.Y(), point.Z()};
        const double distance = Dot(p - origin, unitNormal);
        ++vertices;
        if (distance > toleranceMm) {
            positive = true;
        } else if (distance < -toleranceMm) {
            negative = true;
        }
        if (std::abs(distance) > reach.maximumMm) {
            reach.maximumMm = std::abs(distance);
            reach.farthest = p;
            reach.side = distance >= 0.0 ? 1 : -1;
        }
    }
    if (vertices == 0) {
        return Out::Failure(MakeError(kThickenBadTarget, "面に頂点がありません。", {}));
    }
    if (positive && negative) {
        return Out::Failure(MakeError(kThickenBadTarget,
            "面が相手の平面をまたいでいます。",
            "面の一部が平面の向こう側にあります。どちら側を埋めるのか決まらないので、"
            "平面を面の片側へ置いてください。"));
    }
    if (!positive && !negative) {
        return Out::Failure(MakeError(kThickenBadTarget,
            "面が相手の平面の上に載っています。",
            "面と平面の間に厚みがありません。平面を面から離してください。"));
    }
    return Out::Success(reach);
}

//! 面の真ん中の法線。厚みをどちらへ膨らませるかを決めるのに使う。
[[nodiscard]] geometry::Vector3 MiddleNormal(const TopoDS_Shape& shape)
{
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        double u0 = 0.0, u1 = 0.0, v0 = 0.0, v1 = 0.0;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        BRepGProp_Face properties(face);
        gp_Pnt point;
        gp_Vec normal;
        properties.Normal((u0 + u1) * 0.5, (v0 + v1) * 0.5, point, normal);
        if (normal.Magnitude() > 1.0e-12) {
            return geometry::Vector3{normal.X(), normal.Y(), normal.Z()}
                * (1.0 / normal.Magnitude());
        }
    }
    return geometry::Vector3{0.0, 0.0, 1.0};
}

} // namespace

Result<ThickenedSolid> ThickenSurfaceToPlane(modeling::KernelShapeHandle sourceShape,
    const geometry::Vector3& planeOrigin, const geometry::Vector3& planeNormal,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<ThickenedSolid>;
    if (!(planeNormal.Length() > 1.0e-12)) {
        return Out::Failure(MakeError(kThickenBadTarget, "相手の平面の向きが決まりません。", {}));
    }
    TopoDS_Shape face;
    if (!LookupShape(sourceShape, face) || face.IsNull()) {
        return Out::Failure(MakeError(kThickenSourceMissing,
            "元になる面がありません。",
            "先に形状ガイドで面を作ってから、平面まで立体にしてください。"));
    }
    const geometry::Vector3 unitNormal = planeNormal * (1.0 / planeNormal.Length());
    const auto reach = MeasureReach(face, planeOrigin, unitNormal,
        std::max(tolerance.modelLinearMm, 1.0e-6));
    if (!reach.HasValue()) {
        return Out::Failure(reach.Diagnostics());
    }
    // 平面へ向かう向き。面の法線がそちらを向いていなければ、負の厚みで膨らませる。
    const geometry::Vector3 toward = unitNormal * (-static_cast<double>(reach.Value().side));
    const double sign = Dot(MiddleNormal(face), toward) >= 0.0 ? 1.0 : -1.0;
    // 斜めに届く分を見込んで余裕を持たせ、平面で切り落とす。
    const double thickness = sign * (reach.Value().maximumMm * 2.5 + 1.0);
    const auto built = ThickSolid(face, thickness);
    if (!built.HasValue()) {
        return Out::Failure(built.Diagnostics());
    }
    try {
        const double margin = std::max(reach.Value().maximumMm * 10.0, 1000.0);
        const gp_Pln plane(ToPoint(planeOrigin), gp_Dir(ToVector(unitNormal)));
        BRepBuilderAPI_MakeFace maker(plane, -margin, margin, -margin, margin);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kThickenTrimFailed,
                "相手の平面で切るための面を作れませんでした。", {}));
        }
        // 面のある側だけを残す。
        BRepPrimAPI_MakeHalfSpace halfSpace(maker.Face(), ToPoint(reach.Value().farthest));
        if (!halfSpace.IsDone()) {
            return Out::Failure(MakeError(kThickenTrimFailed,
                "相手の平面で切るための領域を作れませんでした。", {}));
        }
        BRepAlgoAPI_Common common(built.Value(), halfSpace.Solid());
        common.Build();
        if (!common.IsDone()) {
            return Out::Failure(MakeError(kThickenTrimFailed,
                "相手の平面で切れませんでした。", {}));
        }
        const TopoDS_Shape result = common.Shape();
        const double volume = VolumeOf(result);
        if (!(volume > 0.0)) {
            return Out::Failure(MakeError(kThickenTrimFailed,
                "相手の平面で切ったら、何も残りませんでした。",
                "面の法線が平面と反対を向いているか、面が平面へ届きません。"));
        }
        ThickenedSolid made;
        made.handle = StoreShape(result);
        made.volumeMm3 = volume;
        made.thicknessMm = reach.Value().maximumMm;
        made.edges = EdgesOf(result, tolerance);
        return Out::Success(std::move(made));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kThickenTrimFailed,
            "相手の平面で切れませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kThickenTrimFailed,
            "相手の平面で切れませんでした。", "幾何カーネルが失敗しました。"));
    }
}

#else

Result<ThickenedSolid> ThickenSurface(modeling::KernelShapeHandle,
    double, fabrication::ThicknessPlacement, const geometry::GeometryTolerance&)
{
    return Result<ThickenedSolid>::Failure(MakeError(kThickenFailed,
        "面に厚みを付けられませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

Result<ThickenedSolid> ThickenSurfaceToPlane(modeling::KernelShapeHandle,
    const geometry::Vector3&, const geometry::Vector3&, const geometry::GeometryTolerance&)
{
    return Result<ThickenedSolid>::Failure(MakeError(kThickenFailed,
        "面に厚みを付けられませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

#endif

} // namespace kachakacha::v2::kernel
