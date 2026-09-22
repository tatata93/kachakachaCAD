#pragma once

//! 2 本の線(直線・円弧・円・ベジェ)の角を落とす(C面取り)・丸める(R丸め)。
//! 直線どうしは WireEdit の解析解のまま。ここは **曲線が混ざる組**(直線×曲線・曲線どうし)。
//!
//! 角: 2 本の実交点(無ければ直線は延ばし、円弧は円にして探す。ベジェは延ばさない)のうち、
//!     押した点(CornerOptions の hint)に近いもの。押した点が無ければ、両端に近いもの。
//! 残す側: 「残す側」の欄で明示した側 > 押した点のある側 > 角から遠い端の側。
//! 丸め: 残す側で両方に接する半径 r の円弧。接点は数値で求める(接点の候補 A(u) から r だけ
//!       内側へ置いた中心が、B からちょうど r 離れる u を二分で探す)。接する条件は作ったあと測る。
//! 面取り: 角から **線に沿った長さ** で切戻し、2 点を直線で結ぶ。
//! 出来上がりは WireEdit の CornerResult(1 本目 → 角の線 → 2 本目、つながった向き)。
//! B スプラインは途中で切れない(GEO-C014)ので断る。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/WireEdit.h"

namespace kachakacha::v2::geometry {

enum class CornerKind {
    Chamfer,
    Fillet,
};

//! 曲線が混ざる 2 本の角。sizeMm は面取りなら 1 本目の切戻し(2 本目は options)、丸めなら半径。
[[nodiscard]] base::Result<CornerResult> CornerBetweenCurves(const CurveSegment& first,
    const CurveSegment& second, CornerKind kind, double sizeMm, const CornerOptions& options,
    double toleranceMm);

} // namespace kachakacha::v2::geometry
