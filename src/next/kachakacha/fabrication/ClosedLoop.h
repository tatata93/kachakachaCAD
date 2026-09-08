#pragma once

//! 閉じた輪の組立を解く(fabrication-contract.md §10、AT-FAB-010)。
//!
//! 貼り合わせで輪になっている部材は、折り角を勝手に決めると閉じない。
//! ここでは折り角を未知数にして、貼り合わせの隙間が最小になる角度を探す。
//!
//! 大事な決まり: 閉じないときに、板を伸ばしたり歪めたりして「閉じた」ことにしない。
//! 解けなかったら、いちばん開いている貼り合わせを返して断る。
//! V1 は頂点を動かして閉じさせていたので、型紙と実物の寸法が合わなくなった。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/Assembly.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

//! 貼り合わせる1組。型紙の輪郭の頂点の番号で指す。
struct MatePair {
    std::string matePairId;
    std::size_t firstPanelIndex = 0;
    std::size_t firstFromVertex = 0;
    std::size_t firstToVertex = 0;
    std::size_t secondPanelIndex = 0;
    std::size_t secondFromVertex = 0;
    std::size_t secondToVertex = 0;
};

struct ClosureResidual {
    std::string matePairId;
    double gapMm = 0.0;
};

struct ClosedLoopSolution {
    //! 解いたあとの折り。角度だけが変わる。板の形は変えない。
    std::vector<AssemblyFold> folds;
    double maximumGapMm = 0.0;
    double rootMeanSquareGapMm = 0.0;
    //! 貼り合わせごとの残り隙間。並びは入力と同じ。
    std::vector<ClosureResidual> residuals;
    //! 何回で収束したか。0 なら最初から閉じていた。
    int iterations = 0;
    bool converged = false;
};

//! 貼り合わせの辺が、型紙の上で同じ長さかどうか。
//! 違っていたら、どんな角度にしても閉じない。先に断る。
[[nodiscard]] base::Result<bool> CheckMateEdgeLengths(
    const std::vector<AssemblyPanel>& panels, const std::vector<MatePair>& mates,
    double toleranceMm);

//! 折り角を解いて輪を閉じる。
//!
//! 解けたら、閉じた折り角を返す。解けなければ値を返さず、
//! いちばん開いている貼り合わせを診断へ書いて断る(FAB-A001)。
[[nodiscard]] base::Result<ClosedLoopSolution> SolveClosedLoop(
    const std::vector<AssemblyPanel>& panels, const std::vector<AssemblyFold>& folds,
    const std::vector<MatePair>& mates, double toleranceMm, int maximumIterations = 60);

} // namespace kachakacha::v2::fabrication
