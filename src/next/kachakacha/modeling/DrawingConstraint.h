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

#include <optional>

namespace kachakacha::v2::modeling {

//! その道具が Shift の拘束を使うか。
[[nodiscard]] bool ToolUsesAxisConstraint(DrawingTool tool) noexcept;

//! 吸着先が無いときに、向きを直角へ寄せる許し(度)。
//!
//! 1px も狙いを外せない画面で線を引くと、真下へ引いたつもりが -89.95 度になる。
//! Shift を押していれば固定できるが、押していないときに 0.05 度ずれた線が
//! 黙って文書へ入ると、面にしたときに閉じない。**点の吸着先が無いときだけ**
//! この幅で直角へ寄せる(端点やグリッドに吸い付いているときは、そちらが正)。
inline constexpr double kRightAngleSnapToleranceDeg = 1.5;

//! その道具が直角スナップを使うか。矩形は「正方形へ」の拘束が別にあるので使わない。
[[nodiscard]] bool ToolUsesRightAngleSnap(DrawingTool tool) noexcept;

//! 基準の点から見た向きが直角(0/90/180/270 度)に近ければ、長さを変えずに直角へ寄せる。
//! 寄せなかった(近くない・道具が使わない・長さが 0)ときは値を返さない。
[[nodiscard]] std::optional<geometry::Vector3> SnapDirectionToRightAngle(DrawingTool tool,
    const WorkPlaneFrame& plane, const geometry::Vector3& anchor,
    const geometry::Vector3& point, double toleranceDeg = kRightAngleSnapToleranceDeg);

//! 基準の点から見て、狙った点を拘束した位置へ寄せる。
//!
//! `anchor` は直線・矩形・円・円弧なら1点目、ポリラインとスプラインなら
//! 直前に置いた点。V1 と同じ決め方である。
[[nodiscard]] geometry::Vector3 ApplyAxisConstraint(DrawingTool tool,
    const WorkPlaneFrame& plane, const geometry::Vector3& anchor,
    const geometry::Vector3& point);

} // namespace kachakacha::v2::modeling
