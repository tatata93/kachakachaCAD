#pragma once

//! 作図中の拘束(V1同等)。
//!
//! V1 は Shift を押している間、線を **作業平面の水平・垂直へ** 固定した。
//! 矩形は **正方形へ** 固定した。これが無いと、水平な線1本を引くのに
//! ピクセル単位で狙うことになる。
//!
//! 「どちらへ寄せるか」は、動かした量の大きいほうに合わせる。
//! 迷ったときに勝手が変わらないよう、真横と真上でちょうど並んだときは横を選ぶ。

#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/modeling/WorkPlane.h"

namespace kachakacha::v2::modeling {

//! その道具が Shift の拘束を使うか。
[[nodiscard]] bool ToolUsesAxisConstraint(DrawingTool tool) noexcept;

//! 基準の点から見て、狙った点を拘束した位置へ寄せる。
//!
//! `anchor` は直線・矩形・円・円弧なら1点目、ポリラインとスプラインなら
//! 直前に置いた点。V1 と同じ決め方である。
[[nodiscard]] geometry::Vector3 ApplyAxisConstraint(DrawingTool tool,
    const WorkPlaneFrame& plane, const geometry::Vector3& anchor,
    const geometry::Vector3& point);

} // namespace kachakacha::v2::modeling
