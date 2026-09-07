#include "kachakacha/geometry/WireChain.h"

#include <algorithm>
#include <map>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::MakeWarning;
using base::Result;

namespace {

constexpr const char* kDisconnected = "GEO-W001";
constexpr const char* kBranched = "GEO-W002";
constexpr const char* kEndpointGap = "GEO-W003";
constexpr const char* kDegenerate = "GEO-W005";
constexpr const char* kDuplicate = "GEO-W006";
constexpr const char* kEmpty = "GEO-W007";

//! 端点1つ。どのSegmentのどちら端かを覚えておく。
struct Endpoint {
    std::size_t inputIndex = 0;
    int endpointIndex = 0; //!< 0=始点 1=終点
    Vector3 position{};
};

//! 入力の並びを、IDで安定に並べ替える(手順1)。
//! 入力の順番が違っても同じ結果になるようにする。
[[nodiscard]] std::vector<std::size_t> StableOrder(const std::vector<ChainInput>& inputs)
{
    std::vector<std::size_t> order(inputs.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    std::sort(order.begin(), order.end(), [&inputs](std::size_t l, std::size_t r) {
        if (inputs[l].entityId != inputs[r].entityId) {
            return inputs[l].entityId < inputs[r].entityId;
        }
        return inputs[l].segmentId < inputs[r].segmentId;
    });
    return order;
}

} // namespace

// ---------------- WireEntity ----------------

Result<WireEntity> WireEntity::Make(base::EntityId id, std::vector<Item> items,
    double modelLinearMm)
{
    if (items.empty()) {
        return Result<WireEntity>::Failure(MakeError(kEmpty,
            "ワイヤーに線が1本も入っていません。", "少なくとも1本必要です。"));
    }
    for (std::size_t index = 1; index < items.size(); ++index) {
        const Vector3 previousEnd = items[index - 1].segment.EndPoint();
        const Vector3 currentStart = items[index].segment.StartPoint();
        const double gap = Distance(previousEnd, currentStart);
        if (gap > modelLinearMm) {
            return Result<WireEntity>::Failure(MakeError(kDisconnected,
                "となりあう線がつながっていません。",
                std::to_string(index) + " 本目との隙間が " + std::to_string(gap)
                    + " mm あります。"));
        }
    }
    WireEntity wire;
    wire.id_ = id;
    wire.items_ = std::move(items);
    const bool singleClosedSegment = wire.items_.size() == 1
        && wire.items_.front().segment.IsClosed(modelLinearMm);
    wire.closed_ = singleClosedSegment
        || Distance(wire.items_.back().segment.EndPoint(),
               wire.items_.front().segment.StartPoint())
            <= modelLinearMm;
    return Result<WireEntity>::Success(std::move(wire));
}

double WireEntity::TotalLength(double tolerance) const
{
    double length = 0.0;
    for (const Item& item : items_) {
        length += item.segment.TotalLength(tolerance);
    }
    return length;
}

// ---------------- 連結解析 ----------------

Result<ChainAnalysis> AnalyzeChain(std::vector<ChainInput> inputs,
    const GeometryTolerance& tolerance)
{
    if (inputs.empty()) {
        return Result<ChainAnalysis>::Failure(MakeError(kEmpty,
            "線が1本も選ばれていません。", "つなぐ線を選んでください。"));
    }

    // 手順1: IDで安定に並べる。
    const std::vector<std::size_t> order = StableOrder(inputs);

    // 同じSegmentを同じ役割へ二重に入れていないか(GEO-W006)。
    for (std::size_t index = 1; index < order.size(); ++index) {
        const ChainInput& previous = inputs[order[index - 1]];
        const ChainInput& current = inputs[order[index]];
        if (previous.entityId == current.entityId
            && previous.segmentId == current.segmentId) {
            return Result<ChainAnalysis>::Failure(MakeError(kDuplicate,
                "同じ線が二重に入っています。",
                "同じ役割へ同じ線を2回入れることはできません。"));
        }
    }

    // 退化した線(GEO-W005)。
    for (const std::size_t index : order) {
        const CurveSegment& segment = inputs[index].segment;
        if (segment.TotalLength(tolerance.modelLinearMm) <= tolerance.modelLinearMm) {
            return Result<ChainAnalysis>::Failure(MakeError(kDegenerate,
                "長さが許容差以下の線が混ざっています。",
                "つぶれた線を取り除いてください。"));
        }
    }

    // 1本だけで閉じている場合(円など)は、そのまま閉じた鎖。
    if (order.size() == 1 && inputs[order[0]].segment.IsClosed(tolerance.modelLinearMm)) {
        ChainAnalysis analysis;
        analysis.order.closed = true;
        analysis.order.segments.push_back(
            {inputs[order[0]].entityId, inputs[order[0]].segmentId, false});
        return Result<ChainAnalysis>::Success(std::move(analysis));
    }

    // 手順2: modelLinearMm 内の端点を同じノードへまとめる。
    std::vector<Endpoint> endpoints;
    endpoints.reserve(order.size() * 2);
    for (const std::size_t index : order) {
        endpoints.push_back({index, 0, inputs[index].segment.StartPoint()});
        endpoints.push_back({index, 1, inputs[index].segment.EndPoint()});
    }
    std::vector<int> nodeOf(endpoints.size(), -1);
    std::vector<Vector3> nodePositions;
    std::vector<std::vector<std::size_t>> nodeEndpoints;
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        for (std::size_t node = 0; node < nodePositions.size(); ++node) {
            if (Distance(endpoints[index].position, nodePositions[node])
                <= tolerance.modelLinearMm) {
                nodeOf[index] = static_cast<int>(node);
                nodeEndpoints[node].push_back(index);
                break;
            }
        }
        if (nodeOf[index] < 0) {
            nodeOf[index] = static_cast<int>(nodePositions.size());
            nodePositions.push_back(endpoints[index].position);
            nodeEndpoints.push_back({index});
        }
    }

    // interactiveJoinMm 内だが modelLinearMm 外の端点は「ほぼ接続」として知らせる。
    // 勝手に動かさない。形状生成もしない(GEO-W003)。
    std::vector<Vector3> nearMiss;
    for (std::size_t left = 0; left < nodePositions.size(); ++left) {
        for (std::size_t right = left + 1; right < nodePositions.size(); ++right) {
            const double gap = Distance(nodePositions[left], nodePositions[right]);
            if (gap > tolerance.modelLinearMm && gap <= tolerance.interactiveJoinMm) {
                nearMiss.push_back(nodePositions[left]);
            }
        }
    }
    if (!nearMiss.empty()) {
        ChainAnalysis analysis;
        analysis.nearMissPoints = nearMiss;
        return Result<ChainAnalysis>::Failure(MakeError(kEndpointGap,
            "端点がぴったり合っていません。",
            "近いだけの端点を勝手に動かすことはしません。"
            "「端点を結合」で合わせるか、許容差を変えるか、別の線を選んでください。"));
    }

    // 手順4-6: 次数を見る。
    std::vector<int> degree(nodePositions.size(), 0);
    for (const std::vector<std::size_t>& members : nodeEndpoints) {
        degree[static_cast<std::size_t>(&members - nodeEndpoints.data())] =
            static_cast<int>(members.size());
    }
    std::vector<std::size_t> branchNodes;
    std::vector<std::size_t> openNodes;
    for (std::size_t node = 0; node < degree.size(); ++node) {
        if (degree[node] >= 3) {
            branchNodes.push_back(node);
        } else if (degree[node] == 1) {
            openNodes.push_back(node);
        }
    }
    if (!branchNodes.empty()) {
        ChainAnalysis analysis;
        for (const std::size_t node : branchNodes) {
            BranchCandidate candidate;
            candidate.position = nodePositions[node];
            candidate.degree = degree[node];
            for (const std::size_t endpointIndex : nodeEndpoints[node]) {
                const ChainInput& input = inputs[endpoints[endpointIndex].inputIndex];
                candidate.incident.push_back({input.entityId, input.segmentId, false});
            }
            analysis.branches.push_back(std::move(candidate));
        }
        Result<ChainAnalysis> failure = Result<ChainAnalysis>::Failure(MakeError(kBranched,
            "線が枝分かれしていて、つなぐ順番を決められません。",
            "分かれ目でどちらへ進むかを選んでください。"));
        return failure;
    }
    if (openNodes.size() != 0 && openNodes.size() != 2) {
        return Result<ChainAnalysis>::Failure(MakeError(kDisconnected,
            "選んだ線が1本につながっていません。",
            "端が " + std::to_string(openNodes.size())
                + " か所あります。1本の鎖なら0か2か所です。"));
    }

    // 手順7: 開始端を決める。開いた鎖は端のうちIDが小さい方、閉じた鎖は最小IDから。
    const bool closed = openNodes.empty();
    std::size_t startEndpoint = 0;
    if (closed) {
        startEndpoint = 0; // endpoints は既にID順。その先頭が最小ID。
    } else {
        std::size_t best = endpoints.size();
        for (const std::size_t node : openNodes) {
            for (const std::size_t endpointIndex : nodeEndpoints[node]) {
                best = std::min(best, endpointIndex);
            }
        }
        startEndpoint = best;
    }

    // 鎖をたどる。
    ChainAnalysis analysis;
    analysis.order.closed = closed;
    std::vector<bool> used(inputs.size(), false);
    std::size_t currentEndpoint = startEndpoint;
    for (std::size_t step = 0; step < order.size(); ++step) {
        const Endpoint& from = endpoints[currentEndpoint];
        const std::size_t inputIndex = from.inputIndex;
        if (used[inputIndex]) {
            return Result<ChainAnalysis>::Failure(MakeError(kDisconnected,
                "同じ線を2回通ってしまいました。",
                "選んだ線が1本の鎖になっていません。"));
        }
        used[inputIndex] = true;
        // 始点から入ったなら向きはそのまま、終点から入ったなら反転。
        analysis.order.segments.push_back({inputs[inputIndex].entityId,
            inputs[inputIndex].segmentId, from.endpointIndex == 1});
        // 同じSegmentの反対側の端点へ移る。
        const std::size_t otherEndpoint =
            from.endpointIndex == 0 ? currentEndpoint + 1 : currentEndpoint - 1;
        // その端点が属するノードから、まだ使っていない次のSegmentを選ぶ。
        const int node = nodeOf[otherEndpoint];
        std::size_t nextEndpoint = endpoints.size();
        for (const std::size_t candidate : nodeEndpoints[static_cast<std::size_t>(node)]) {
            if (candidate == otherEndpoint) {
                continue;
            }
            if (!used[endpoints[candidate].inputIndex]) {
                nextEndpoint = candidate;
                break;
            }
        }
        if (nextEndpoint == endpoints.size()) {
            break;
        }
        currentEndpoint = nextEndpoint;
    }

    if (analysis.order.segments.size() != order.size()) {
        return Result<ChainAnalysis>::Failure(MakeError(kDisconnected,
            "選んだ線が1本につながっていません。",
            "つながっているのは " + std::to_string(analysis.order.segments.size())
                + " 本で、選んだのは " + std::to_string(order.size()) + " 本です。"));
    }
    return Result<ChainAnalysis>::Success(std::move(analysis));
}

} // namespace kachakacha::v2::geometry
