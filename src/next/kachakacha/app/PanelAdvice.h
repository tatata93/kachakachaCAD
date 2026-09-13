#pragma once

//! 「1枚では無理だが、何枚に分ければできるか」を言う。
//!
//! いまの V2 方式は、展開できない面が1枚でもあるとそこで全部を断っていた。
//! 断るだけでは、作る人は次に何をすればよいのか分からない。
//! 「切るのか、分けるのか、そもそも形を直すのか」が決められない。
//!
//! `fabrication::CompareAllStrategies` は4通りの分け方を作り比べられる。
//! ここはその答えを、作る人の言葉の一文にする。
//!
//! **勝手に分けない。** ここが言うのは「こうすれば作れます」までで、
//! 実際に分けるかどうかは人が決める(fabrication-contract.md §5)。

#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/fabrication/PanelStrategy.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 分け方の見立て。
struct PanelAdvice {
    //! 目標偏差の中に収まる分け方が見つかったか。
    bool found = false;
    fabrication::FabricationStrategy strategy =
        fabrication::FabricationStrategy::FewPieces;
    std::size_t pieceCount = 0;
    double maximumDeviationMm = 0.0;
    //! 作る人へ出す一文。found でなくても必ず入る。
    std::string messageJa;
};

//! 4通りを作り比べて、いちばん少ない枚数で目標に収まるものを選ぶ。
//!
//! 選び方は fabrication-contract.md §5.2 の辞書式に従う。
//! 精度と枚数を重み付き和で交換しない。まず目標偏差に収まること、
//! そのうえで枚数が少ないこと。
[[nodiscard]] PanelAdvice AdvisePanelStrategy(
    const std::vector<fabrication::PanelCandidate>& panels,
    const std::vector<fabrication::PanelAdjacency>& adjacencies,
    const fabrication::FabricationSettings& settings, double targetMaxDeviationMm);

} // namespace kachakacha::v2::app
