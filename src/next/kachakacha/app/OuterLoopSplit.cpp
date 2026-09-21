#include "kachakacha/app/OuterLoopSplit.h"

#include "kachakacha/app/GuideTableBuild.h"

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Vector3.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using geometry::Vector3;
using modeling::GuideTableSelection;

namespace {

//! 1 本の選択を、グラフの辺として見たもの。
struct LoopEdge {
    std::size_t selection = 0;
    std::size_t from = 0;   //!< 端点(節)の番号
    std::size_t to = 0;
    std::vector<Vector3> points;   //!< from から to へ向かう点列
    bool alive = true;
};

//! 輪を 1 つ: 辺の番号と、それぞれを from→to の向きでたどるか。
struct LoopCycle {
    std::vector<std::size_t> edges;
    std::vector<bool> forward;
};

class Graph {
public:
    explicit Graph(double joinMm) : joinMm_(joinMm) {}

    std::size_t NodeOf(const Vector3& point)
    {
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            if ((nodes_[index] - point).Length() <= joinMm_) {
                return index;
            }
        }
        nodes_.push_back(point);
        return nodes_.size() - 1;
    }

    [[nodiscard]] std::size_t NodeCount() const noexcept { return nodes_.size(); }

private:
    double joinMm_;
    std::vector<Vector3> nodes_;
};

//! 行き止まりの線を、なくなるまで外す。
void PruneDeadEnds(std::vector<LoopEdge>& edges, std::size_t nodeCount)
{
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<int> degree(nodeCount, 0);
        for (const LoopEdge& edge : edges) {
            if (edge.alive) {
                ++degree[edge.from];
                ++degree[edge.to];
            }
        }
        for (LoopEdge& edge : edges) {
            if (edge.alive && edge.from != edge.to
                && (degree[edge.from] < 2 || degree[edge.to] < 2)) {
                edge.alive = false;
                changed = true;
            }
        }
    }
}

//! 生きている辺から、閉じた輪を全部数える。同じ輪は、いちばん小さい辺から始めた
//! ものだけを数える(向きが逆の 2 通りは両方出るが、面積が同じなので害はない)。
class CycleFinder {
public:
    explicit CycleFinder(const std::vector<LoopEdge>& edges) : edges_(edges) {}

    std::vector<LoopCycle> Find()
    {
        for (std::size_t start = 0; start < edges_.size(); ++start) {
            if (!edges_[start].alive) {
                continue;
            }
            const LoopEdge& first = edges_[start];
            if (first.from == first.to) {
                cycles_.push_back({{start}, {true}});
                continue;
            }
            start_ = start;
            target_ = first.from;
            path_ = {start};
            forward_ = {true};
            visited_ = {first.from, first.to};
            Walk(first.to);
        }
        return std::move(cycles_);
    }

private:
    static constexpr std::size_t kMaximumCycles = 20000;

    void Walk(std::size_t node)
    {
        if (cycles_.size() >= kMaximumCycles) {
            return;
        }
        for (std::size_t index = start_ + 1; index < edges_.size(); ++index) {
            const LoopEdge& edge = edges_[index];
            if (!edge.alive || edge.from == edge.to
                || std::find(path_.begin(), path_.end(), index) != path_.end()) {
                continue;
            }
            bool forward = true;
            std::size_t next = 0;
            if (edge.from == node) {
                next = edge.to;
            } else if (edge.to == node) {
                next = edge.from;
                forward = false;
            } else {
                continue;
            }
            path_.push_back(index);
            forward_.push_back(forward);
            if (next == target_) {
                cycles_.push_back({path_, forward_});
            } else if (std::find(visited_.begin(), visited_.end(), next) == visited_.end()) {
                visited_.push_back(next);
                Walk(next);
                visited_.pop_back();
            }
            path_.pop_back();
            forward_.pop_back();
        }
    }

    const std::vector<LoopEdge>& edges_;
    std::vector<LoopCycle> cycles_;
    std::size_t start_ = 0;
    std::size_t target_ = 0;
    std::vector<std::size_t> path_;
    std::vector<bool> forward_;
    std::vector<std::size_t> visited_;
};

//! 輪が囲む面積。Newell のベクトル面積の大きさ(平らでない輪でも、いちばんよく
//! 見える向きへ投影した面積になる)。
[[nodiscard]] double EnclosedArea(const std::vector<LoopEdge>& edges, const LoopCycle& cycle)
{
    std::vector<Vector3> ring;
    for (std::size_t at = 0; at < cycle.edges.size(); ++at) {
        const auto& points = edges[cycle.edges[at]].points;
        if (cycle.forward[at]) {
            ring.insert(ring.end(), points.begin(), points.end());
        } else {
            ring.insert(ring.end(), points.rbegin(), points.rend());
        }
    }
    Vector3 sum{};
    for (std::size_t at = 0; at < ring.size(); ++at) {
        sum = sum + Cross(ring[at], ring[(at + 1) % ring.size()]);
    }
    return sum.Length() * 0.5;
}

} // namespace

Result<OuterLoopSplit> SplitOuterLoop(const std::vector<GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance)
{
    const double joinMm = std::max(tolerance.interactiveJoinMm,
        tolerance.modelLinearMm * 100.0);
    Graph graph(joinMm);
    std::vector<LoopEdge> edges;
    for (std::size_t index = 0; index < selections.size(); ++index) {
        const auto& segments = selections[index].segments;
        if (segments.empty()) {
            continue;
        }
        LoopEdge edge;
        edge.selection = index;
        edge.from = graph.NodeOf(segments.front().StartPoint());
        edge.to = graph.NodeOf(segments.back().EndPoint());
        edge.points = geometry::SampleChain(segments, std::max(joinMm, 0.05));
        edges.push_back(std::move(edge));
    }
    if (edges.size() > kOuterLoopMaximumEdges) {
        return Result<OuterLoopSplit>::Failure(MakeError("UI-R012",
            "選んだ線が多すぎて、外周を決められません。",
            std::to_string(edges.size()) + " 本(上限 "
                + std::to_string(kOuterLoopMaximumEdges)
                + " 本)。外周の線を「境界」、内側の線を「通る線」の欄へ分けて入れてください。"));
    }
    PruneDeadEnds(edges, graph.NodeCount());
    const std::vector<LoopCycle> cycles = CycleFinder(edges).Find();
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
