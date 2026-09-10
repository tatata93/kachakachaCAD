#pragma once

//! 選んだ線どうしの交点に作図点を作る(V1 の「交点に点」)。
//!
//! 交点は geometry(WireEdit の IntersectCurvesForEditing)が求める。ここは
//! 線の組を総当たりにして、同じ場所の点をまとめ、並びを安定させるだけ。
//! 交点が1つも無ければ UI-X001 で断る(黙って何も作らない、はしない)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <vector>

namespace kachakacha::v2::app {

//! 交点をすべて集める。同じ場所(許容差以内)は1つにまとめる。
//! 線が1本しか無い、または交わらないときは断る。
[[nodiscard]] base::Result<std::vector<geometry::Vector3>> IntersectionPointsOf(
    const std::vector<geometry::CurveSegment>& curves, double toleranceMm);

} // namespace kachakacha::v2::app
