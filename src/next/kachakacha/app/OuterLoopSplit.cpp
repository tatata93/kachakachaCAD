#include "kachakacha/app/OuterLoopSplit.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/LoopGraph.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using modeling::GuideTableSelection;

Result<OuterLoopSplit> SplitOuterLoop(const std::vector<GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance)
{
    const double joinMm = std::max(tolerance.interactiveJoinMm,
        tolerance.modelLinearMm * 100.0);
    LoopGraph graph(joinMm);
    std::vector<LoopEdge> edges;
    for (std::size_t index = 0; index < selections.size(); ++index) {
        const auto& segments = selections[index].segments;
        if (segments.empty()) {
            continue;
        }
        edges.push_back(MakeLoopEdge(index, segments, graph));
    }
    if (edges.size() > kOuterLoopMaximumEdges) {
        return Result<OuterLoopSplit>::Failure(MakeError("UI-R012",
            "選んだ線が多すぎて、外周を決められません。",
            std::to_string(edges.size()) + " 本(上限 "
                + std::to_string(kOuterLoopMaximumEdges)
                + " 本)。外周の線を「境界」、内側の線を「通る線」の欄へ分けて入れてください。"));
    }
    PruneDeadEnds(edges, graph.NodeCount());
    const std::vector<LoopCycle> cycles = FindLoopCycles(edges);
    if (cycles.empty()) {
        return Result<OuterLoopSplit>::Failure(MakeError("UI-R011",
            "選んだ線の中に、閉じた輪がありません。",
            "外周の線が端点どうしでつながっているか確かめてください。"
            "離れているところは端点スナップで引き直すとつながります。"));
    }
    std::size_t best = 0;
    double bestArea = -1.0;
    for (std::size_t index = 0; index < cycles.size(); ++index) {
        const double area = EnclosedArea(edges, cycles[index]);
        const bool larger = area > bestArea * (1.0 + 1.0e-9) + 1.0e-12;
        const bool tieWithMore = std::abs(area - bestArea) <= bestArea * 1.0e-9 + 1.0e-12
            && cycles[index].edges.size() > cycles[best].edges.size();
        if (larger || tieWithMore) {
            best = index;
            bestArea = area;
        }
    }
    OuterLoopSplit split;
    for (const std::size_t edge : cycles[best].edges) {
        split.loop.push_back(edges[edge].selection);
    }
    for (std::size_t index = 0; index < selections.size(); ++index) {
        if (std::find(split.loop.begin(), split.loop.end(), index) == split.loop.end()) {
            split.passThrough.push_back(index);
        }
    }
    return Result<OuterLoopSplit>::Success(std::move(split));
}

Result<modeling::GuideTable> AddBoundaryFillRows(const modeling::GuideTable& table,
    const std::vector<GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<modeling::GuideTable>;
    const auto split = SplitOuterLoop(selections, tolerance);
    if (!split.HasValue()) {
        return Out::Failure(split.Diagnostics());
    }
    // 外周の線は 1 本ずつ別の行にする(輪をたどる順)。辺ごとに連続条件(G0/G1/G2)と
    // 支持面を持てるようにするため。輪になっているかは検査(AnalyzeBoundaryFill)が見る。
    (void)tolerance;
    modeling::GuideTable next = table;
    for (const std::size_t index : split.Value().loop) {
        const auto added = modeling::AddSelectionAsNewRow(next,
            modeling::ChainRole::BoundarySide, selections[index]);
        if (!added.HasValue()) {
            return added;
        }
        next = added.Value();
    }
    for (const std::size_t index : split.Value().passThrough) {
        const auto added = modeling::AddSelectionAsNewRow(next, modeling::ChainRole::GuideU,
            selections[index]);
        if (!added.HasValue()) {
            return added;
        }
        next = added.Value();
    }
    return Out::Success(std::move(next));
}

} // namespace kachakacha::v2::app
