#include "kachakacha/kernel/OcctThicken.h"

#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

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

#else

Result<ThickenedSolid> ThickenSurface(modeling::KernelShapeHandle,
    double, fabrication::ThicknessPlacement, const geometry::GeometryTolerance&)
{
    return Result<ThickenedSolid>::Failure(MakeError(kThickenFailed,
        "面に厚みを付けられませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

#endif

} // namespace kachakacha::v2::kernel
