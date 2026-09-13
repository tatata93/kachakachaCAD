#pragma once

//! 画面のこの位置に、掴める制御点があるか(v1-input-parity.md §1-2 の 12)。
//!
//! V1は9pxで拾っていた。線そのものの当たり判定(8px)より少し広い。
//! 制御点の方が狙いにくいので、広くしておかないと掴めない。
//! 制御点は **選んでいる線分にだけ** 出す。全部に出すと画面が埋まる。
//! 物体ごと選んでいれば、その物体の全線分が選んでいる線分である。

#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/ControlPointEdit.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <optional>
#include <vector>

namespace kachakacha::v2::app {

//! 画面に出す制御点1つ。
struct ShownControlPoint {
    base::EntityId entityId;
    base::SegmentId segmentId;
    std::size_t index = 0;
    geometry::Vector3 position{};
    std::string_view labelJa;
};

//! 選んでいる線分の制御点をすべて集める。画面に四角を出すために使う。
//! どの線分を選んでいるかは IsCurveSelected と同じ規則で決める。
[[nodiscard]] std::vector<ShownControlPoint> ControlPointsForSelection(
    const modeling::SnapScene& scene, const SelectionSet& selection);

//! 画面のこの位置にいちばん近い制御点。範囲の外なら値を持たない。
[[nodiscard]] std::optional<ShownControlPoint> PickControlPoint(
    const modeling::SnapScene& scene, const SelectionSet& selection,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer);

//! 制御点を拾う範囲(px)。V1と同じ。
[[nodiscard]] double ControlPointPickPx() noexcept;

} // namespace kachakacha::v2::app
