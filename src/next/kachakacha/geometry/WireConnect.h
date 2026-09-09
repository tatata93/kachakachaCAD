#pragma once

//! 線と線のつなぎ方(geometry-contract §4、V1同等)。
//!
//! V1 にあった「端点一致」「接線接続」「曲率接続」「分割」「結合」を、
//! 画面を出さずに計算できる形でここへ置く。
//!
//! つなぎ方の段階は3つある。
//!   端点一致(G0): 端が同じ点にある。折れていてよい。
//!   接線接続(G1): さらに、つなぎ目で向きがそろっている。
//!   曲率接続(G2): さらに、つなぎ目で曲がり具合もそろっている。
//!
//! 大事な決まり。**動かすのは端点だけで、線の種類は変えない。**
//! 直線を曲線に化けさせてつなぐ、ということはしない。
//! 直線どうしを G1 でつなげと言われたら、向きが違う以上つなげないので断る。
//! できないことを、できたことにしない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <vector>

namespace kachakacha::v2::geometry {

//! つなぎ方の段階。
enum class ConnectContinuity {
    Position,   //!< G0 端点一致
    Tangent,    //!< G1 接線接続
    Curvature,  //!< G2 曲率接続
};

[[nodiscard]] std::string_view ConnectContinuityNameJa(ConnectContinuity value) noexcept;

//! つないだ結果。2本とも返す。片方だけ動かすこともある。
struct ConnectResult {
    CurveSegment first;
    CurveSegment second;
    //! つないだ点。
    Vector3 joint{};
    //! 動かした距離(mm)。どれだけ形が変わったかを利用者に見せる。
    double movedMm = 0.0;
};

//! 2本の線の、近いほうの端どうしをつなぐ。
//!
//! G0 は、近い端どうしを中点で合わせる。両方を同じだけ動かす。
//! G1 と G2 は、つなぎ目で向き(と曲がり)がそろっているかを見るだけで、
//! そろっていなければ断る。種類を変えてまでそろえない。
[[nodiscard]] base::Result<ConnectResult> ConnectCurves(const CurveSegment& first,
    const CurveSegment& second, ConnectContinuity continuity, double toleranceMm);

//! 相手の線との交点で切り分ける。交点が無ければ断る。
//! 返るのは切り分けた断片で、元の1本は呼び手が捨てる。
[[nodiscard]] base::Result<std::vector<CurveSegment>> SplitCurveAtIntersections(
    const CurveSegment& curve, const std::vector<CurveSegment>& others,
    double toleranceMm);

//! つながっている線を1本の並びにする。端点が離れていれば断る。
//! 並べ替えと向きの反転はするが、形は変えない。
[[nodiscard]] base::Result<std::vector<CurveSegment>> JoinCurves(
    const std::vector<CurveSegment>& curves, double toleranceMm);

} // namespace kachakacha::v2::geometry
