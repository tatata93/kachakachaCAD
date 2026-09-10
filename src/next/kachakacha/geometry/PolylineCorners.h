#pragma once

//! ポリラインの角の加工(V1 の「角の加工」)。
//!
//! 1本のワイヤーの中で、隣り合う直線どうしの角を全部、面取り(平らに落とす)か
//! 丸め(円弧)にする。角ごとの加工は geometry(ChamferLines / FilletLines)が持つ。
//! ここは並びをたどって、角ごとに当て、加工した端を次の角へ渡すだけ。
//!
//! 直線でない角(円弧と直線など)は触らずに残す。落とせる角が1つも無ければ
//! GEO-E021 で断る(黙って何もしない、はしない)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <vector>

namespace kachakacha::v2::geometry {

enum class CornerStyle {
    Chamfer,   //!< 平らに落とす(切戻し量)
    Fillet,    //!< 円弧で丸める(半径)
};

//! 並んだ線(ポリラインの各辺)の角をすべて加工する。
//! 閉じた並び(最後の端が最初の始点と一致)なら、その角も加工する。
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> ProcessPolylineCorners(
    const std::vector<geometry::CurveSegment>& segments, CornerStyle style, double sizeMm,
    double toleranceMm);

} // namespace kachakacha::v2::geometry
