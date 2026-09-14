#pragma once

//! 部材の分割と統合(オーナー指示 2026-09-14 §32)。
//!
//! 自動で出した分け方は出発点であって、答えではない。
//! 「この2枚はつないだまま切りたい」「ここは分けたい」は人が決める。
//!
//! 大事な決まりが2つある。
//!
//!   1. **誤差が増えるからという理由だけで禁止しない。**
//!      2枚を1枚にすれば、たいてい誤差は増える。それでも、
//!      接着線が1本減るほうがきれいに作れることがある。決めるのは作る人である。
//!      こちらは「前はこう、後はこう」を見せるところまでにする。
//!   2. **部材の境目と切れ目は別のもの。**
//!      境目は部材が分かれるところ、切れ目は1枚の中に入れる切り込みである。
//!      統合すると境目は消えるが、切れ目は消えない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/PanelStrategy.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

//! 分けたり統合したりする前と後。人が決める前に見せる。
struct PanelEditPreview {
    std::size_t pieceCountBefore = 0;
    std::size_t pieceCountAfter = 0;
    double maximumDeviationBeforeMm = 0.0;
    double maximumDeviationAfterMm = 0.0;
    //! できるか。できないなら理由を messageJa に入れる。
    bool possible = false;
    std::string messageJa;
};

//! 2つの部材を1つにする。隣り合っていなければ断る。
//!
//! 隣り合っていない2枚を1枚にすると、離れた紙が1枚として型紙に出る。
//! 切り出しても組み立てられない。
[[nodiscard]] base::Result<PanelPartition> MergePieces(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, std::size_t firstPiece,
    std::size_t secondPiece);

//! 統合したらどうなるかを、やる前に見せる。
[[nodiscard]] PanelEditPreview PreviewMerge(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, std::size_t firstPiece,
    std::size_t secondPiece);

//! 1つの部材を2つに分ける。挙げた面が新しい部材になる。
//!
//! 全部を挙げる(または1つも挙げない)のは断る。分けたことにならない。
[[nodiscard]] base::Result<PanelPartition> SplitPiece(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels, std::size_t pieceIndex,
    const std::vector<std::size_t>& movedPanelIndices);

//! 分けたらどうなるかを、やる前に見せる。
[[nodiscard]] PanelEditPreview PreviewSplit(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels, std::size_t pieceIndex,
    const std::vector<std::size_t>& movedPanelIndices);

//! 「2部材 最大 0.35mm → 1部材 最大 0.62mm」のような一文。
[[nodiscard]] std::string DescribePanelEditJa(const PanelEditPreview& preview);

} // namespace kachakacha::v2::fabrication
