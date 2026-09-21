#pragma once

//! 鎖(つながった曲線の並び)の上の位置と、2 点の間で切り出す道具。
//!
//! ロフトのガイドは、最初の断面から最後の断面までの間にだけ面を張る。
//! ガイドのその部分を **曲線の種類を保ったまま** 切り出すのに使う
//! (点列へ落として近似し直すと、ガイドそのものから外れた線で面を作ってしまう)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::geometry {

//! 鎖の上の 1 点。どの曲線の、どの t か。
struct ChainPosition {
    std::size_t segment = 0;
    double parameter = 0.0;
    Vector3 point{};
    double distance = 0.0;
};

//! 点にいちばん近い鎖の上の位置。鎖が空なら distance が無限大。
[[nodiscard]] ChainPosition ClosestPositionOnChain(const std::vector<CurveSegment>& chain,
    const Vector3& point);

//! 位置の前後(鎖の向きで a が b より前なら真)。
[[nodiscard]] bool ChainPositionBefore(const ChainPosition& a, const ChainPosition& b) noexcept;

//! from と to の間を切り出す。向きは鎖の向きのまま(from が後ろなら入れ替えて切る)。
//! 切り出した長さが toleranceMm 以下なら断る。
[[nodiscard]] base::Result<std::vector<CurveSegment>> TrimChainBetween(
    const std::vector<CurveSegment>& chain, const Vector3& from, const Vector3& to,
    double toleranceMm);

} // namespace kachakacha::v2::geometry
