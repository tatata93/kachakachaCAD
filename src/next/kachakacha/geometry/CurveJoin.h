#pragma once

//! 曲線どうしの接続(geometry-contract.md §5.3、v1-drawing-parity.md §1 の 18〜20)。
//!
//! V1の「端点一致」「接線接続(G1)」「曲率接続(G2)」に当たる。
//!
//! 大事な約束: 解が無いときは、近い形で誤魔化さずに断り、
//! **どの条件が成り立たないか**を言う(§5.3)。
//! V1は「とりあえず動かして、見た目が合っていればよい」だったので、
//! 接線が合っていないのに合ったことになる場面があった。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"

namespace kachakacha::v2::geometry {

//! どちらの端をつなぐか。
enum class CurveEnd { Start, End };

//! つなぎ方の強さ。
enum class JoinContinuity {
    Position,   //!< G0。端点だけ合わせる
    Tangent,    //!< G1。接線の向きも合わせる
    Curvature,  //!< G2。曲率の向きと大きさも合わせる
};

[[nodiscard]] std::string_view JoinContinuityNameJa(JoinContinuity value) noexcept;

//! どちらを動かすか。
enum class JoinAnchor {
    KeepFirst,   //!< 1本目を固定して2本目を動かす
    KeepSecond,  //!< 2本目を固定して1本目を動かす
    Midpoint,    //!< 両方を中点へ寄せる
};

struct JoinResult {
    CurveSegment first;
    CurveSegment second;
    //! つないだ後に残っている差。
    double positionGapMm = 0.0;
    double tangentAngleRad = 0.0;
    double curvatureDifference = 0.0;
};

//! 2本の曲線を、指定した端でつなぐ。
//!
//! G0 は端点を動かして合わせる。
//! G1 と G2 は、動かすほうの曲線の形を変える必要があるため、
//! 直線とBezierだけを対象にする。円弧やB-splineは、
//! 形を保ったままでは条件を満たせないので断る(黙って形を変えない)。
[[nodiscard]] base::Result<JoinResult> JoinCurves(const CurveSegment& first, CurveEnd firstEnd,
    const CurveSegment& second, CurveEnd secondEnd, JoinContinuity continuity,
    JoinAnchor anchor, const GeometryTolerance& tolerance);

//! ポリラインの角を落とす/丸める(V1の「角の加工」)。
//! 隣り合う2本の直線の角に、面取りまたは丸めを入れる。
//! `radiusOrDistanceMm` は、丸めなら半径、面取りなら角からの距離。
[[nodiscard]] base::Result<std::vector<CurveSegment>> ProcessPolylineCorners(
    const std::vector<CurveSegment>& polyline, double radiusOrDistanceMm, bool round,
    const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::geometry
