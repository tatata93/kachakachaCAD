#pragma once

//! 点を通るスプライン(D-13「通過点」)。
//!
//! V2 の 3次B-spline は一様(区分ベジェとして評価する、両端を締めない形)である。
//! その形のまま、押した点 Q0..Qm を **節(区切り)の上で必ず通る** ように制御点を解く。
//!   - 区切り k(t = k/m)での値は (P_k + 4 P_{k+1} + P_{k+2}) / 6
//!   - 両端は自然な終わり方(端で曲がりの変化が 0: P_0 - 2P_1 + P_2 = 0)
//! すると P_1 = Q0、P_{m+1} = Qm になり、残りは対角が 4 の三重対角の連立(安定)で決まる。
//! 割り付けは一様(押した点の間隔によらず、区切りは等しい)。間隔が大きく違うと
//! ふくらむことがあるが、どの点も必ず通る。点列への当てはめ(近似・Fit)ではない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <vector>

namespace kachakacha::v2::geometry {

//! 点を通る 3次B-spline。3 点以上。続けて同じ場所の点・数値でない点は断る(GEO-C022)。
[[nodiscard]] base::Result<CurveSegment> CubicBSplineThroughPoints(
    const std::vector<Vector3>& points);

} // namespace kachakacha::v2::geometry
