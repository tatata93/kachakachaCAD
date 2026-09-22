#include "kachakacha/app/LoopGraph.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>

namespace kachakacha::v2::app {

using geometry::Vector3;

std::size_t LoopGraph::NodeOf(const Vector3& point)
{
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        if ((nodes_[index] - point).Length() <= joinMm_) {
            return index;
        }
    }
    nodes_.push_back(point);
    return nodes_.size() - 1;
}

LoopEdge MakeLoopEdge(std::size_t selection, const std::vector<geometry::CurveSegment>& segments,
    LoopGraph& graph)
{
    LoopEdge edge;
    edge.selection = selection;
    edge.from = graph.NodeOf(segments.front().StartPoint());
    edge.to = graph.NodeOf(segments.back().EndPoint());
    edge.points = geometry::SampleChain(segments, std::max(graph.JoinMm(), 0.05));
    return edge;
}

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

namespace {

class CycleFinder {
public:
    CycleFinder(const std::vector<LoopEdge>& edges, std::size_t maximumCycles)
        : edges_(edges), maximumCycles_(maximumCycles)
    {
    }

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
    void Walk(std::size_t node)
    {
        if (cycles_.size() >= maximumCycles_) {
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
    std::size_t maximumCycles_;
    std::vector<LoopCycle> cycles_;
    std::size_t start_ = 0;
    std::size_t target_ = 0;
    std::vector<std::size_t> path_;
    std::vector<bool> forward_;
    std::vector<std::size_t> visited_;
};

} // namespace

std::vector<LoopCycle> FindLoopCycles(const std::vector<LoopEdge>& edges,
    std::size_t maximumCycles)
{
    return CycleFinder(edges, maximumCycles).Find();
}

std::vector<Vector3> CycleRing(const std::vector<LoopEdge>& edges, const LoopCycle& cycle)
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
    return ring;
}

double EnclosedArea(const std::vector<LoopEdge>& edges, const LoopCycle& cycle)
{
    const std::vector<Vector3> ring = CycleRing(edges, cycle);
    Vector3 sum{};
    for (std::size_t at = 0; at < ring.size(); ++at) {
        sum = sum + Cross(ring[at], ring[(at + 1) % ring.size()]);
    }
    return sum.Length() * 0.5;
}

} // namespace kachakacha::v2::app
