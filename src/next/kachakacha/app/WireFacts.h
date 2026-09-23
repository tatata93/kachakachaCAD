#pragma once

//! 線の「事実の行」(UI 設計 2026-09-23 `考察-面作成UIの設計案` 2-5、原則 P2「押す前に事実を見せる」)。
//!
//! 線を 1 本選んだとき、面を作る前から右の棚に出す事実:
//!   - 載る平面(上面 XY / 正面 XZ / 側面 YZ / それに平行 / 傾いた平面 / なし(3D))
//!   - 長さ
//!   - 始点と終点がそれぞれ、どの線のどの端につながっているか(0.000 mm)、
//!     別の線の途中に乗っているか(T 字)、近い端まで何 mm 離れているか(ずれ)、何にも触れていないか。
//! 他社ではスケッチ拘束が担う「つながっている保証」を、本 CAD は拘束を持たないので
//! **見せる**ことで代える。ずれは選んだ時点で分かり、その場で寄せられる(呼び手が LoopGapFix で)。
//! ここは OCCT も Qt も呼ばない。

#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 線の端が、ほかの線とどう触れているか。
enum class WireEndRelation {
    Free,       //!< 何にも触れていない(いちばん近い端が遠い)
    Joined,     //!< 別の線の端と重なっている(許容差以内)
    OnMiddle,   //!< 別の線の途中に乗っている(T 字)
    Near,       //!< 別の線の端が近いが届いていない(ずれ)
};

struct WireEndFact {
    WireEndRelation relation = WireEndRelation::Free;
    std::size_t other = 0;          //!< 相手の線(wires の番号)。Free なら使わない
    bool otherAtEnd = false;        //!< 相手の終点側か(Joined / Near)
    double distanceMm = 0.0;        //!< 相手の端(または途中)までの距離
    geometry::Vector3 target{};     //!< 寄せるならここへ(Near のとき相手の端)
    //! 寄せられるか(この線が直線・折れ線で、端を動かしても形の種類が変わらない)。
    bool movable = false;
};

//! 載る平面。
struct WirePlaneFact {
    std::string nameJa;                        //!< 「上面 XY」「XZ に平行(y = 5.000)」「傾いた平面」「なし(3D)」
    std::optional<geometry::Vector3> normal;   //!< 平面に載るならその法線
    double deviationMm = 0.0;                  //!< その平面からの最大ずれ
};

struct WireFacts {
    WirePlaneFact plane;
    double lengthMm = 0.0;
    bool closed = false;     //!< 円や閉じた輪郭(端の事実は出さない)
    WireEndFact start;
    WireEndFact end;
};

//! wires[target] の事実。相手は wires のほか全部(選んでいない線も含めて渡す)。
//! 「ずれ」と見なす上限は、面にする(app/LoopFaces)と同じ max(joinMm × 10, 長さの 1/4)。
[[nodiscard]] WireFacts DescribeWire(const std::vector<modeling::GuideTableSelection>& wires,
    std::size_t target, const geometry::GeometryTolerance& tolerance);

//! 端の一文。「直線 2 の終点(0.000 mm)」「円弧 1 の途中(T 字, 0.000 mm)」
//! 「円弧 1 の端まで 3.082 mm」「なし」。
[[nodiscard]] std::string WireEndTextJa(const std::vector<modeling::GuideTableSelection>& wires,
    const WireEndFact& fact);

//! 2 行の事実。1 行目「載る面: 正面 XZ   長さ 42.000 mm」、2 行目「始点 → …   終点 → …」。
[[nodiscard]] std::string WireFactsTextJa(const std::vector<modeling::GuideTableSelection>& wires,
    const WireFacts& facts);

} // namespace kachakacha::v2::app
