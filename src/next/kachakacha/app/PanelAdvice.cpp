#include "kachakacha/app/PanelAdvice.h"

#include <sstream>

namespace kachakacha::v2::app {
namespace {

using fabrication::FabricationStrategy;
using fabrication::PanelPartition;

//! 部材の中で一番大きい展開偏差。
[[nodiscard]] double WorstDeviationMm(const PanelPartition& partition)
{
    double worst = 0.0;
    for (const auto& piece : partition.pieces) {
        if (piece.maximumDeviationMm > worst) {
            worst = piece.maximumDeviationMm;
        }
    }
    return worst;
}

} // namespace

PanelAdvice AdvisePanelStrategy(const std::vector<fabrication::PanelCandidate>& panels,
    const std::vector<fabrication::PanelAdjacency>& adjacencies,
    const fabrication::FabricationSettings& settings, double targetMaxDeviationMm)
{
    PanelAdvice advice;
    if (panels.empty()) {
        advice.messageJa = "分ける相手の面がありません。";
        return advice;
    }
    const auto comparison = fabrication::CompareAllStrategies(panels, adjacencies, settings,
        targetMaxDeviationMm);
    // 目標に収まるもののうち、枚数の少ないほうを採る。
    // 枚数が同じなら、先に並んでいるほう(1枚 → 少数 → 全部ばらす → 混合)。
    for (const auto& entry : comparison.entries) {
        if (!entry.available) {
            continue;
        }
        const double worst = WorstDeviationMm(entry.partition);
        if (worst > targetMaxDeviationMm) {
            continue;
        }
        const std::size_t count = entry.partition.PieceCount();
        if (advice.found && count >= advice.pieceCount) {
            continue;
        }
        advice.found = true;
        advice.strategy = entry.strategy;
        advice.pieceCount = count;
        advice.maximumDeviationMm = worst;
    }
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    if (!advice.found) {
        // 収まる分け方が1つも無い。どれくらい足りないのかまで言う。
        double best = 0.0;
        bool any = false;
        for (const auto& entry : comparison.entries) {
            if (!entry.available) {
                continue;
            }
            const double worst = WorstDeviationMm(entry.partition);
            if (!any || worst < best) {
                best = worst;
                any = true;
            }
        }
        if (!any) {
            advice.messageJa = "どの分け方でも部材になりませんでした。"
                               "面が離れ島に分かれていないか見てください。";
            return advice;
        }
        text << "どの分け方でも目標の " << targetMaxDeviationMm
             << " mm には収まりません。一番近い分け方でも " << best
             << " mm ずれます。再現度を下げるか、切れ目を許すか、形を直してください。";
        advice.messageJa = text.str();
        return advice;
    }
    if (advice.pieceCount == 1) {
        text << "1枚のまま作れます(ずれは最大 " << advice.maximumDeviationMm << " mm)。";
    } else {
        text << "1枚では作れませんが、" << advice.pieceCount
             << " 枚に分ければ作れます(ずれは最大 " << advice.maximumDeviationMm
             << " mm)。分け方は「"
             << fabrication::FabricationStrategyNameJa(advice.strategy) << "」です。";
    }
    advice.messageJa = text.str();
    return advice;
}

} // namespace kachakacha::v2::app
