#pragma once

//! 形状ガイド(GuideSurface)の入力と、その検査(geometry-contract §6)。
//!
//! ここには面を作る処理は入らない。入るのは「その入力で面を作ってよいか」の判断だけ。
//! 面そのものは OCCT 側(src/next_occt)で作る。分ける理由は2つ。
//!   1. core は OCCT に依存しない(AT-ARC-001)。
//!   2. 断り方を数値で決められる。V1が変な面を出したのは、
//!      作れない入力をそのまま渡して、OCCTが出した「それらしい何か」を採用したため。
//!
//! この層の約束: 作れないものは作れないと言う。近い形で誤魔化さない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::modeling {

using base::Diagnostic;
using base::EntityId;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;

enum class GuideSurfaceMethod {
    PlanarBoundary,
    RuledSections,
    LoftSections,
    GuidedLoft,
    GordonNetwork,
    BoundaryFill,
    OffsetGuide,
    //! 回転体(V1 の回転面)。断面 1 本を軸のまわりに回す。末尾に足すのは番号を保つため。
    Revolve,
    //! 四辺面(U0/U1/V0/V1 の 4 辺で囲う 1 枚)。末尾に足すのは保存の番号を保つため。
    FourEdgePatch,
    //! 曲線網(Gordon): U 線と V 線を全部通る面を、網の形から直接組み立てる。
    //! GordonNetwork(Filling で近づける)とは別の作り方。網の外側の線が端で交わるときだけ作れる。
    CurveNetworkExact,
};

[[nodiscard]] constexpr std::string_view GuideSurfaceMethodName(
    GuideSurfaceMethod method) noexcept
{
    switch (method) {
    case GuideSurfaceMethod::PlanarBoundary: return "planar_boundary";
    case GuideSurfaceMethod::RuledSections:  return "ruled_sections";
    case GuideSurfaceMethod::LoftSections:   return "loft_sections";
    case GuideSurfaceMethod::GuidedLoft:     return "guided_loft";
    case GuideSurfaceMethod::GordonNetwork:  return "gordon_network";
    case GuideSurfaceMethod::BoundaryFill:   return "boundary_fill";
    case GuideSurfaceMethod::OffsetGuide:    return "offset_guide";
    case GuideSurfaceMethod::Revolve:        return "revolve";
    case GuideSurfaceMethod::FourEdgePatch:  return "four_edge_patch";
    case GuideSurfaceMethod::CurveNetworkExact: return "curve_network_exact";
    }
    return "unknown";
}

//! 鎖の役割。kcd2-format.md §11 の role enum と同じ。
enum class ChainRole {
    OuterBoundary,
    HoleBoundary,
    Section,
    GuideU,
    GuideV,
    BoundarySide,
    SourceSurface,
    //! ロフトの中心線(断面を運ぶ道筋)。面の上には乗らない。末尾に足して番号を保つ。
    Centerline,
};

[[nodiscard]] constexpr std::string_view ChainRoleName(ChainRole role) noexcept
{
    switch (role) {
    case ChainRole::OuterBoundary: return "outer_boundary";
    case ChainRole::HoleBoundary:  return "hole_boundary";
    case ChainRole::Section:       return "section";
    case ChainRole::GuideU:        return "guide_u";
    case ChainRole::GuideV:        return "guide_v";
    case ChainRole::BoundarySide:  return "boundary_side";
    case ChainRole::SourceSurface: return "source_surface";
    case ChainRole::Centerline:    return "centerline";
    }
    return "unknown";
}

//! 隣の面との滑らかさ(境界の辺ごと)。
//!   G0: 位置だけ合う(辺を通る)
//!   G1: 接線も合う(折れ目が無い)。隣の面(支持面)が要る
//!   G2: 曲率も合う(映り込みが途切れない)。隣の面(支持面)が要る
//! G1/G2 は「隣の面に対して」の条件なので、線だけでは成り立たない。
//! 支持面が無いのに G1/G2 を指定したら、成り立つふりをせず断る。
enum class SurfaceContinuity {
    G0,
    G1,
    G2,
};

[[nodiscard]] constexpr std::string_view SurfaceContinuityName(SurfaceContinuity value) noexcept
{
    switch (value) {
    case SurfaceContinuity::G0: return "G0";
    case SurfaceContinuity::G1: return "G1";
    case SurfaceContinuity::G2: return "G2";
    }
    return "G0";
}

//! 面の入力になる1本の鎖。順序と向きは AnalyzeChain が済ませたもの。
struct GuideChain {
    ChainRole role = ChainRole::Section;
    int index = 1;                      //!< 役割ごとに1始まりで一意
    EntityId sourceEntityId;            //!< どのワイヤーから来たか(壊れた参照の判定に使う)
    std::vector<CurveSegment> segments;
    bool closed = false;
    //! 境界の辺の連続条件(境界面・四辺面の境界辺だけ)。
    SurfaceContinuity continuity = SurfaceContinuity::G0;
    //! G1/G2 の相手の面(形状ガイド)。G0 なら要らない。
    EntityId supportSurfaceId;
    //! 支持面の実体(核の表の番号)。画面が作る直前に入れる。0 = 無い。
    std::uint64_t supportShapeHandle = 0;
};

//! 四辺面の張り方(OCCT GeomFill_BSplineCurves の 3 方式)。
enum class FourEdgeStyle {
    Coons,      //!< 4 辺を線形に混ぜる標準の張り方
    Stretch,    //!< 平坦優先(張りを強く、ふくらみを抑える)
    Curved,     //!< 丸み優先(辺の曲がりを内側へ多めに伝える)
};

[[nodiscard]] constexpr std::string_view FourEdgeStyleLabelJa(FourEdgeStyle style) noexcept
{
    switch (style) {
    case FourEdgeStyle::Coons:   return "標準(Coons)";
    case FourEdgeStyle::Stretch: return "平坦優先";
    case FourEdgeStyle::Curved:  return "丸み優先";
    }
    return "";
}

struct GuideSurfaceRequest {
    GuideSurfaceMethod method = GuideSurfaceMethod::PlanarBoundary;
    std::vector<GuideChain> chains;
    //! GuidedLoft: 端に断面が無いとき、仮想断面を作るか(既定は作る)。
    bool createVirtualEndSections = true;
    //! RuledSections / LoftSections: 断面を **渡した順のまま** 使う(手動固定)。
    //! 偽なら幾何の位置から並べ直す(自動)。既定は自動。
    //! 真のときも、隣り合う断面が重なっていないかの検査は同じように通す。
    bool keepSectionOrder = false;
    //! FourEdgePatch: 張り方。
    FourEdgeStyle fourEdgeStyle = FourEdgeStyle::Coons;
    //! OffsetGuide: 距離。0は拒否する。
    double offsetDistanceMm = 0.0;
    //! Revolve: 軸(点と向き)と回す角度。角度は 0 より大きく 2π 以下。
    geometry::Vector3 revolveAxisPoint{};
    geometry::Vector3 revolveAxisDirection{0.0, 0.0, 1.0};
    double revolveAngleRad = 0.0;
};

// ---- 検査の結果 ----

//! PlanarBoundary が見つけた輪郭の入れ子。
struct PlanarLoopClassification {
    std::size_t chainIndex = 0;    //!< request.chains の添字
    bool isHole = false;
    //! 外周のとき、この外周に属する穴の chainIndex。
    std::vector<std::size_t> holes;
};

//! GordonNetwork / GuidedLoft が見つけた交差。
struct ChainCrossing {
    std::size_t firstChainIndex = 0;
    std::size_t secondChainIndex = 0;
    //! それぞれの鎖上での正規化弧長。順序の単調性はこの値で見る。
    double firstParameter = 0.0;
    double secondParameter = 0.0;
    double distanceMm = 0.0;
    Vector3 position{};
};

//! 断面の並び順(LoftSections が主成分軸で決めたもの)。
struct SectionOrdering {
    std::vector<std::size_t> chainIndices;   //!< 並べ替え後
    Vector3 axisDirection{};                 //!< 並べる基準にした向き
};

//! ロフト(断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1)をどう作るか。
//! 検査が入力のつながりから決める。人に方式名を覚えさせないため、画面はこれを言葉で出す。
enum class LoftSolver {
    //! ガイドも中心線も無い。断面をなめらかに通す(ThruSections)。断面の上に乗る。
    Sections,
    //! 中心線に沿って断面を運ぶ(ガイド無し)。
    Centerline,
    //! 外側のガイド 2 本だけ(断面の両端に 1 本ずつ)。2 本のレールで掃く。
    //! 従来の「案内付きロフト」と同じ作り方(2 本のときの近道)。
    TwoRailSweep,
    //! それ以外(ガイド 1 本、3 本以上、内側のガイド、中心線とガイドの併用)。
    //! 最初と最後の断面と両脇を境界に、残りの断面と **全部のガイド** を
    //! 面が通る拘束にして張る。作ったあとで全部の線からの外れを測る。
    RailFilling,
};

[[nodiscard]] constexpr std::string_view LoftSolverLabelJa(LoftSolver solver) noexcept
{
    switch (solver) {
    case LoftSolver::Sections:     return "断面をなめらかに通す";
    case LoftSolver::Centerline:   return "中心線に沿って断面を運ぶ";
    case LoftSolver::TwoRailSweep: return "両端の2本のガイドで掃く";
    case LoftSolver::RailFilling:  return "断面とガイドを全部通るように張る(近似)";
    }
    return "";
}

//! ガイドが断面のどちら側にあるか。
enum class LoftRailSide {
    Start,      //!< どの断面でも始点側の端に接する(外側)
    End,        //!< どの断面でも終点側の端に接する(外側)
    Interior,   //!< 断面の途中を通る(内側)
};

struct LoftRail {
    std::size_t chainIndex = 0;
    LoftRailSide side = LoftRailSide::Interior;
    //! 最初の断面から最後の断面までの部分(RailFilling のときだけ)。曲線の種類は保つ。
    std::vector<CurveSegment> span;
};

struct LoftPlan {
    LoftSolver solver = LoftSolver::Sections;
    std::vector<LoftRail> rails;
    bool hasCenterline = false;
    std::size_t centerlineChainIndex = 0;
    //! 断面を逆向きに使うか。sectionOrdering.chainIndices と同じ並び。
    std::vector<bool> reverseSections;
};

//! 四辺面の 4 辺の並び。検査が端点のつながりから決める(渡した順と向きは問わない)。
struct FourEdgePlan {
    //! 輪をたどる順の鎖番号。U0 → V1 → U1 → V0 の順。
    std::vector<std::size_t> sides;
    //! 輪をたどる向きに対して逆向きか。sides と同じ並び。
    std::vector<bool> reversed;
    //! 内側の通る線がある(方式として近似拘束になる)。
    bool hasInteriorConstraints = false;
    //! 4 辺の面(Coons など)をそのまま使えず、4 辺を境界に張り直す(通る線か G1/G2 がある)。
    bool refill = false;
};

struct GuideSurfaceAnalysis {
    GuideSurfaceMethod method = GuideSurfaceMethod::PlanarBoundary;
    //! PlanarBoundary。非接触の外周が複数あれば、その数だけ入る。
    std::vector<PlanarLoopClassification> planarLoops;
    geometry::PlaneFit planeFit;
    //! RuledSections / LoftSections。
    SectionOrdering sectionOrdering;
    //! GordonNetwork / GuidedLoft。
    std::vector<ChainCrossing> crossings;
    //! GuidedLoft で自動生成する仮想断面の位置(ガイド上の正規化弧長)。
    std::vector<double> virtualSectionParameters;
    //! LoftSections / GuidedLoft の作り方の内訳。
    LoftPlan loft;
    //! FourEdgePatch の 4 辺。
    FourEdgePlan fourEdge;
    //! 情報や警告。エラーはここではなく Result の側に出る。
    std::vector<Diagnostic> notes;
};

//! 入力を調べる。作れないと判断したら値を返さず、GEO-G0xx の診断で断る。
[[nodiscard]] base::Result<GuideSurfaceAnalysis> AnalyzeGuideSurfaceRequest(
    const GuideSurfaceRequest& request, const GeometryTolerance& tolerance);

//! 検査が採用した断面の並びを、元のワイヤーの番号で返す(画面の「断面順」に出す)。
//! 断面を使わない作り方なら空。
[[nodiscard]] std::vector<EntityId> AdoptedSectionSources(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis);

//! 出来上がった面が、入力の鎖を許容差内で通っているかを測る。
//! OCCT側が面を作ったあとに必ず通す(§6.4、§6.6 の偏差検査)。
struct SurfaceFitCheck {
    double maximumDeviationMm = 0.0;
    std::size_t worstChainIndex = 0;
    bool withinTolerance = true;
};

//! surfacePoints は面上の点群(OCCT側が吐いたもの)。曲線側は自前で点列にする。
[[nodiscard]] SurfaceFitCheck CheckSurfaceFit(const GuideSurfaceRequest& request,
    const std::vector<Vector3>& surfacePoints, const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::modeling
