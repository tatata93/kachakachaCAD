#pragma once

//! ロフト(断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1)と四辺面を作る(OcctGuideSurface の中身)。
//!
//! 外へ見せる API ではない。OcctGuideSurface.cpp だけが使う。
//! 作り方は検査(modeling/LoftInput・FourEdgeInput)が決めて analysis に書いてある。
//! ここはそのとおりに作るだけで、**どの入力も捨てない**。出来たものは呼び出し元が
//! 全部の線からの外れを測り、外れすぎていれば捨てる。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <GeomAbs_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <vector>

namespace kachakacha::v2::kernel::detail {

//! MakeFilling へ渡す拘束の次数。OCCT の BRepFill_Filling は GeomAbs_Shape を整数のまま
//! 拘束の次数(0 = G0、1 = G1、2 = G2)として BRepFill_CurveConstraint へ渡す。
//! GeomAbs_G2 は値が 3 なので "The continuity is not G0 G1 or G2" で断られる
//! (2026-09-22 PC で確認)。次数 2 には、値が 2 の GeomAbs_C1 を渡す。
static_assert(static_cast<int>(GeomAbs_C0) == 0 && static_cast<int>(GeomAbs_G1) == 1
        && static_cast<int>(GeomAbs_C1) == 2,
    "BRepFill_Filling は GeomAbs_Shape の値をそのまま拘束の次数にする");
[[nodiscard]] inline GeomAbs_Shape FillingOrder(modeling::SurfaceContinuity order) noexcept
{
    switch (order) {
    case modeling::SurfaceContinuity::G0: return GeomAbs_C0;
    case modeling::SurfaceContinuity::G1: return GeomAbs_G1;
    case modeling::SurfaceContinuity::G2: return GeomAbs_C1;   // 値 2 = 次数 2(上の注記)
    }
    return GeomAbs_C0;
}

//! G1 の許容(度)と G2 の許容(辺を横切る法曲率の差、1/mm)。核の MakeFilling に渡す
//! 目標よりゆるく、目で見て折れ目が分からない程度。超えたら「滑らかにできなかった」と断る。
inline constexpr double kContinuityG1LimitDeg = 1.5;
inline constexpr double kContinuityG2Limit = 0.1;

//! 境界の辺の連続条件(G1/G2)を、作ったあとで測るための控え。
struct ContinuityCheck {
    std::size_t chainIndex = 0;
    modeling::SurfaceContinuity order = modeling::SurfaceContinuity::G0;
    //! その辺が乗っている支持面の面。
    TopoDS_Face support;
};

//! 測った滑らかさ。負の値は「測る対象が無い」。
struct ContinuityMeasure {
    double g1ErrorDeg = -1.0;
    double g2Error = -1.0;
};

//! 境界面の初期面(2026-09-24 オーナー報告「膜がへこむ」)。
//! BRepOffsetAPI_MakeFilling は初期面を与えないと外周の最小二乗平面から始め、曲げエネルギー最小の
//! 「膜」を張る。円弧が上に膨らんでいてもその膨らみを内側へ運ぶ情報が無く、平面へ向かって垂れる。
//! 外周(ring: 輪をたどる順の鎖の番号)を、折れの小さいつなぎ目から束ねて 3〜4 側にし、四辺面と同じ
//! 道(SideCurve → SnapCorners → GeomFill Coons)で面にする。作れなければ空の面(呼び手はこれまでどおり)。
[[nodiscard]] TopoDS_Face CoonsFromRing(const modeling::GuideSurfaceRequest& request,
    const std::vector<std::size_t>& ring, const geometry::GeometryTolerance& tolerance);

//! 1 本の境界辺を足す。G1/G2 なら支持面(その辺が乗っている面)に対して足し、控えを残す。
//! edge を渡せばその辺を、空なら鎖の曲線を 1 本ずつ辺にして足す。
[[nodiscard]] base::Result<bool> AddBoundaryEdges(BRepOffsetAPI_MakeFilling& filler,
    const modeling::GuideSurfaceRequest& request, std::size_t chainIndex,
    const std::vector<TopoDS_Edge>& edges, const geometry::GeometryTolerance& tolerance,
    std::vector<ContinuityCheck>& checks);

//! 作ったあとで、控えた G1/G2 を測る。許容を超えていれば、どの辺で何度折れたかを言って断る。
//!
//! 核の G1Error / G2Error は使わない。OCCT の GeomPlate_BuildPlateSurface::G1Error(Index)
//! は作業用の配列を「曲線上の点の数(作るときの引数)」で確保し、拘束の点がそれより多いと
//! 配列の外へ書く(2026-09-22 PC: ヒープ破損 0xc0000374)。だから辺の上の点で、
//! 出来た面と支持面の法線の角度(G1)と、辺を横切る向きの法曲率の差(G2)を自分で測る。
[[nodiscard]] base::Result<ContinuityMeasure> MeasureContinuity(const TopoDS_Shape& built,
    const modeling::GuideSurfaceRequest& request, const std::vector<ContinuityCheck>& checks);

//! 2 つの面が、辺の上でどれだけ滑らかにつながっているか。
struct EdgeContinuity {
    //! 辺の上の点の半分以上で測れたか。
    bool measured = false;
    std::size_t samples = 0;
    std::size_t measuredSamples = 0;
    //! 法線の角度の最大(度)。
    double g1Deg = 0.0;
    //! 辺を横切る向きの法曲率の差の最大(1/mm)。
    double g2 = 0.0;
};

//! 出来た面と支持面(隣の面)の、辺の上の滑らかさを測る(MeasureContinuity と同じ測り方)。
[[nodiscard]] EdgeContinuity MeasureEdgeContinuity(const TopoDS_Face& result,
    const TopoDS_Face& support, const TopoDS_Edge& edge);

//! 出来た形を面の結果にする(標本・正体・外周・面積。核の表へ入れて番号を付ける)。
//! 面を作る道(BuildGuideSurface)と面の編集(OcctSurfaceEdit)が同じものを使う。
[[nodiscard]] base::Result<modeling::GuideSurfaceResult> FinishSurfaceResult(
    const TopoDS_Shape& shape, const geometry::GeometryTolerance& tolerance);

//! ガイドや中心線を使うロフト(解析の LoftSolver が Sections 以外)。
[[nodiscard]] base::Result<TopoDS_Shape> BuildLoftShape(
    const modeling::GuideSurfaceRequest& request,
    const modeling::GuideSurfaceAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance);

//! 曲線網(Gordon)。core が組み立てた格子点(U 線・V 線を全部通る)を B-spline 面へ写す。
[[nodiscard]] base::Result<TopoDS_Shape> BuildNetworkShape(
    const modeling::GuideSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance);

//! 四辺面。内側の通る線があれば、4 辺の面を初期面にして通る線へ寄せて張り直す。
[[nodiscard]] base::Result<TopoDS_Shape> BuildFourEdgeShape(
    const modeling::GuideSurfaceRequest& request,
    const modeling::GuideSurfaceAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance, ContinuityMeasure& measure);

} // namespace kachakacha::v2::kernel::detail
#endif
