#pragma once

//! 線の上に置いて押す編集(トリム・延長・分割、Inventor のスケッチと同じ手順)の計画。
//!
//! 画面は「押した線(番号と線分)・線の上の位置 t・押した点」だけを渡す。境目は場面(SnapScene)の
//! 全部の線。ここが **消える区間 / 伸びる先 / 分かれる点** と、その線(ワイヤー)がどう残るかを決め、
//! 画面は下見の点列を描き、押されたら chains を新しいワイヤーとして文書へ入れる。
//! 折れ線は残った鎖ごとに分ける(真ん中を消すと 2 本、閉じた折れ線の 1 か所なら 1 本の開いた線)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/CurveTrim.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 画面で押した(置いた)線の上の場所。
struct HoverEditPick {
    base::EntityId entityId;
    base::SegmentId segmentId;
    double parameter = 0.0;
    geometry::Vector3 point{};
};

//! 場面の中の 1 本のワイヤー(番号順の線分)。
struct WireCurves {
    base::EntityId entityId;
    std::vector<base::SegmentId> segmentIds;
    std::vector<geometry::CurveSegment> segments;
    bool construction = false;
    bool datum = false;
    //! 最後の終点が最初の始点へ戻っている(折れ線の輪)。円 1 本も閉じている。
    bool closedLoop = false;
};

//! 場面からそのワイヤーの線分を番号順に集める。無ければ値なし。
[[nodiscard]] std::optional<WireCurves> WireCurvesOf(const modeling::SnapScene& scene,
    const base::EntityId& entityId, const geometry::GeometryTolerance& tolerance);

//! 編集の結果。chains が新しいワイヤー(1 本ずつ)。空なら線ごと消える。
struct HoverEditOutcome {
    WireCurves source;
    std::size_t segmentIndex = 0;
    std::vector<std::vector<geometry::CurveSegment>> chains;
    //! 下見: 消える / 伸びる区間の点列(トリム・延長)。
    std::vector<geometry::Vector3> previewLine;
    //! 下見: 分かれる点(分割)。
    std::optional<geometry::Vector3> previewPoint;
    //! 「円の 90.0° が消えて円弧になります」のような一言。
    std::string summaryJa;
    //! 一番下の一行(「トリム: 直線 / CUT=12.345 mm / 2 本に」)。
    std::string footerJa;
};

[[nodiscard]] base::Result<HoverEditOutcome> PlanTrim(const modeling::SnapScene& scene,
    const HoverEditPick& pick, const geometry::GeometryTolerance& tolerance);

[[nodiscard]] base::Result<HoverEditOutcome> PlanExtend(const modeling::SnapScene& scene,
    const HoverEditPick& pick, const geometry::GeometryTolerance& tolerance);

[[nodiscard]] base::Result<HoverEditOutcome> PlanSplit(const modeling::SnapScene& scene,
    const HoverEditPick& pick, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::app
