#pragma once

//! 境界面(境界埋め)で選んだ線を、「外周の輪」と「面が必ず通る線」へ分ける。
//!
//! オーナー方針(2026-09-22):**人が引いたワイヤーは、面が必ずそこを通る線である。**
//! 外周の線と、その内側に引いた線(膨らみの目安の円弧、稜線の直線など)をまとめて
//! 選んだら、外周を輪として境界にし、残りは面が通る線として拘束する。
//! 以前は選んだ線を全部 1 本の輪につなごうとして、上下の頂点で 3 本が集まると
//! 「枝分かれしていて順番を決められません(GEO-W002)」で止まっていた。
//!
//! 決め方:
//!   1. 端点を許容差でまとめて、線を辺とするグラフにする。
//!   2. 片側が行き止まりの線(内側で T 字に当たるだけの線など)を繰り返し外す。
//!      これは輪に入らないので「通る線」。
//!   3. 残りから閉じた輪を全部数え、囲む面積(Newell のベクトル面積 = いちばん
//!      よく見える向きへ投影した面積)が最大の輪を外周にする。
//!   4. 外周に入らなかった線は全部「通る線」。
//!
//! ここは OCCT も Qt も呼ばない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::app {

struct OuterLoopSplit {
    //! 外周の輪に入る選択の番号。**輪をたどる順**。
    std::vector<std::size_t> loop;
    //! 面が通るだけの選択の番号。小さい順。
    std::vector<std::size_t> passThrough;
};

//! 輪を数えるのをあきらめる線の本数。これより多いと組み合わせが爆発する。
inline constexpr std::size_t kOuterLoopMaximumEdges = 24;

[[nodiscard]] base::Result<OuterLoopSplit> SplitOuterLoop(
    const std::vector<modeling::GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance);

//! 境界面の表へ、選んだ線を入れる。外周の輪を 1 本の「境界辺」の行にまとめ、
//! 残りは 1 本ずつ「通る線」(GuideU)の行にする。
[[nodiscard]] base::Result<modeling::GuideTable> AddBoundaryFillRows(
    const modeling::GuideTable& table,
    const std::vector<modeling::GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::app
