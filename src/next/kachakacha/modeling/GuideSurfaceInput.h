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

#include <string>
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
    }
    return "unknown";
}

//! 面の入力になる1本の鎖。順序と向きは AnalyzeChain が済ませたもの。
struct GuideChain {
    ChainRole role = ChainRole::Section;
    int index = 1;                      //!< 役割ごとに1始まりで一意
    EntityId sourceEntityId;            //!< どのワイヤーから来たか(壊れた参照の判定に使う)
    std::vector<CurveSegment> segments;
    bool closed = false;
};

struct GuideSurfaceRequest {
    GuideSurfaceMethod method = GuideSurfaceMethod::PlanarBoundary;
    std::vector<GuideChain> chains;
    //! GuidedLoft: 端に断面が無いとき、仮想断面を作るか(既定は作る)。
    bool createVirtualEndSections = true;
    //! BoundaryFill: 辺ごとの連続条件。true = G1。既定は全部 G0。
    std::vector<bool> tangentContinuity;
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
    //! 情報や警告。エラーはここではなく Result の側に出る。
    std::vector<Diagnostic> notes;
};

//! 入力を調べる。作れないと判断したら値を返さず、GEO-G0xx の診断で断る。
[[nodiscard]] base::Result<GuideSurfaceAnalysis> AnalyzeGuideSurfaceRequest(
    const GuideSurfaceRequest& request, const GeometryTolerance& tolerance);

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
