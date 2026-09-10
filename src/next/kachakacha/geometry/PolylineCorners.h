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
//!
//! vertexIndex を 0 以上にすると、その頂点(点の番号、0 始まり)の角だけを加工する
//! (V1 の「ポリラインの角」の「頂点番号」)。頂点 k の角は k-1 番目の辺と k 番目の辺の間。
//! 閉じた並びでは頂点 0 が最後の辺と最初の辺の角。開いた並びの両端(0 と最後)は角でないので
//! GEO-E021 で断る。無い番号も断る。
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> ProcessPolylineCorners(
    const std::vector<geometry::CurveSegment>& segments, CornerStyle style, double sizeMm,
    double toleranceMm, int vertexIndex = -1);

} // namespace kachakacha::v2::geometry
