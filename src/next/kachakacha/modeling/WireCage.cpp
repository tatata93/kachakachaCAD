#include "kachakacha/modeling/WireCage.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace kachakacha::v2::modeling {

using base::Diagnostic;
using base::MakeError;
using base::MakeInformation;
using base::MakeWarning;
using base::Result;
using geometry::PlanarFrame;
using geometry::PlaneFit;
using geometry::Point2;

namespace {

constexpr const char* kNoCandidate = "GEO-S001";
constexpr const char* kAmbiguous = "GEO-S002";
constexpr const char* kOpenShell = "GEO-S003";
constexpr const char* kNonManifold = "GEO-S004";
constexpr const char* kZeroVolume = "GEO-S006";
constexpr const char* kMultipleSolids = "GEO-S007";
//! 入力そのものが使えない(重複した線、点になっている線など)。
constexpr const char* kBadInput = "GEO-S009";

//! 端点をまとめた頂点。
struct Vertex {
    Vector3 position{};
    std::vector<std::size_t> incidentEdges;
};

struct Edge {
    std::size_t inputIndex = 0;
    std::size_t startVertex = 0;
    std::size_t endVertex = 0;
    std::vector<Vector3> points;   //!< 検査用。曲線のままでは平面判定ができない
    double lengthMm = 0.0;
};

//! 面の候補。辺の並びと、その向き。
struct LoopCandidate {
    std::vector<std::size_t> edges;
    std::vector<bool> reversed;
    Vector3 normal{};
    Vector3 origin{};
    double areaMm2 = 0.0;
    //! 同じ辺集合の重複を潰すための鍵。
    std::set<std::size_t> edgeSet;
    bool declared = false;
};

//! 境界の線を1周ぶんの順番へ並べ直す。順不同で渡された辺から輪を作る。
//! 輪にならなければ空を返す。
[[nodiscard]] bool BuildRing(const std::vector<Edge>& edges,
    const std::vector<std::size_t>& members, std::vector<std::size_t>& ordered,
    std::vector<bool>& reversed)
{
    if (members.size() < 2) {
        return false;
    }
    std::vector<std::size_t> remaining(members.begin() + 1, members.end());
    ordered.assign(1, members.front());
    reversed.assign(1, false);
    std::size_t tail = edges[members.front()].endVertex;
    const std::size_t head = edges[members.front()].startVertex;
    while (!remaining.empty()) {
        bool joined = false;
        for (std::size_t at = 0; at < remaining.size(); ++at) {
            const Edge& candidate = edges[remaining[at]];
            if (candidate.startVertex == tail) {
                ordered.push_back(remaining[at]);
                reversed.push_back(false);
                tail = candidate.endVertex;
            } else if (candidate.endVertex == tail) {
                ordered.push_back(remaining[at]);
                reversed.push_back(true);
                tail = candidate.startVertex;
            } else {
                continue;
            }
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(at));
            joined = true;
            break;
        }
        if (!joined) {
            return false;
        }
    }
    return tail == head;
}

//! 平面でない輪郭の法線と面積。Newellの方法で、ねじれていても向きが決まる。
void NewellNormalAndArea(const std::vector<Vector3>& outline, Vector3& normal,
    double& areaMm2)
{
    normal = Vector3{};
    for (std::size_t at = 0; at < outline.size(); ++at) {
        const Vector3& a = outline[at];
        const Vector3& b = outline[(at + 1) % outline.size()];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    const double length = normal.Length();
    areaMm2 = 0.5 * length;
    if (length > 0.0) {
        normal = normal * (1.0 / length);
    }
}

[[nodiscard]] double SamplingToleranceMm(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
}

//! 端点を許容差でまとめて頂点にする。
[[nodiscard]] std::size_t AddVertex(std::vector<Vertex>& vertices, const Vector3& position,
    double toleranceMm)
{
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if ((vertices[index].position - position).Length() <= toleranceMm) {
            return index;
        }
    }
    vertices.push_back(Vertex{position, {}});
    return vertices.size() - 1;
}

//! 平面の候補を、頂点で交わる辺の組から作る。
struct CandidatePlane {
    Vector3 origin{};
    Vector3 normal{};
};

[[nodiscard]] bool SamePlane(const CandidatePlane& a, const CandidatePlane& b,
    double toleranceMm)
{
    const double parallel = std::abs(Dot(a.normal, b.normal));
    if (parallel < 1.0 - 1.0e-9) {
        return false;
    }
    return std::abs(Dot(b.origin - a.origin, a.normal)) <= toleranceMm;
}

//! 辺の全点がその平面へ載っているか。
[[nodiscard]] bool EdgeOnPlane(const Edge& edge, const CandidatePlane& plane,
    double toleranceMm)
{
    for (const Vector3& point : edge.points) {
        if (std::abs(Dot(point - plane.origin, plane.normal)) > toleranceMm) {
            return false;
        }
    }
    return true;
}

//! 平面上の部分グラフから、面(閉路)を取り出す。
//!
//! 各有向辺について「共有する頂点で、いちばん右へ曲がる次の辺」を選んで進む。
//! 平面グラフの面を1つずつ辿る定石で、頂点の次数が3以上でも正しく分かれる。
//! 単純に「次数2の連結成分」で済ませると、辺が3本以上集まる形で必ず破綻する。
[[nodiscard]] std::vector<LoopCandidate> ExtractPlanarLoops(const std::vector<Edge>& edges,
    const std::vector<Vertex>& vertices, const std::vector<std::size_t>& members,
    const CandidatePlane& plane, double toleranceMm)
{
    std::vector<LoopCandidate> loops;
    // 面は2辺でも成り立つ(直線と円弧で囲まれた半円など)。3辺以上に縛らない。
    if (members.size() < 2) {
        return loops;
    }
    PlaneFit fit;
    fit.origin = plane.origin;
    fit.normal = plane.normal;
    fit.valid = true;
    const PlanarFrame frame = geometry::MakeFrame(fit);

    const auto flatten = [&](const Vector3& point) {
        const Vector3 d = point - frame.origin;
        return Point2{Dot(d, frame.uDirection), Dot(d, frame.vDirection)};
    };

    // 有向辺: (辺の添字, 反転かどうか)。
    struct Directed {
        std::size_t edge = 0;
        bool reversed = false;
        std::size_t from = 0;
        std::size_t to = 0;
        double outgoingAngle = 0.0;   //!< from から出ていく向き
        double incomingAngle = 0.0;   //!< to へ入ってくる向きの逆
    };
    std::vector<Directed> directed;
    for (const std::size_t index : members) {
        const Edge& edge = edges[index];
        for (int flip = 0; flip < 2; ++flip) {
            Directed item;
            item.edge = index;
            item.reversed = flip == 1;
            item.from = flip == 0 ? edge.startVertex : edge.endVertex;
            item.to = flip == 0 ? edge.endVertex : edge.startVertex;
            const std::vector<Vector3>& points = edge.points;
            const Vector3 tail = flip == 0 ? points.front() : points.back();
            const Vector3 nextPoint = flip == 0 ? points[1] : points[points.size() - 2];
            const Vector3 head = flip == 0 ? points.back() : points.front();
            const Vector3 previousPoint = flip == 0 ? points[points.size() - 2] : points[1];
            const Point2 outward{flatten(nextPoint).u - flatten(tail).u,
                flatten(nextPoint).v - flatten(tail).v};
            const Point2 inward{flatten(head).u - flatten(previousPoint).u,
                flatten(head).v - flatten(previousPoint).v};
            item.outgoingAngle = std::atan2(outward.v, outward.u);
            item.incomingAngle = std::atan2(-inward.v, -inward.u);
            directed.push_back(item);
        }
    }

    // 頂点ごとに、出ていく有向辺を角度順に並べる。
    std::map<std::size_t, std::vector<std::size_t>> outgoing;
    for (std::size_t index = 0; index < directed.size(); ++index) {
        outgoing[directed[index].from].push_back(index);
    }
    for (auto& entry : outgoing) {
        std::sort(entry.second.begin(), entry.second.end(),
            [&](std::size_t l, std::size_t r) {
                if (directed[l].outgoingAngle != directed[r].outgoingAngle) {
                    return directed[l].outgoingAngle < directed[r].outgoingAngle;
                }
                return l < r;
            });
    }

    std::vector<bool> visited(directed.size(), false);
    for (std::size_t start = 0; start < directed.size(); ++start) {
        if (visited[start]) {
            continue;
        }
        LoopCandidate loop;
        std::size_t current = start;
        bool broken = false;
        for (std::size_t guard = 0; guard <= directed.size() + 1; ++guard) {
            if (visited[current]) {
                broken = current != start || loop.edges.empty();
                break;
            }
            visited[current] = true;
            loop.edges.push_back(directed[current].edge);
            loop.reversed.push_back(directed[current].reversed);

            // 次の有向辺: to から出ていくもののうち、入ってきた向きのすぐ右隣。
            const auto found = outgoing.find(directed[current].to);
            if (found == outgoing.end() || found->second.empty()) {
                broken = true;
                break;
            }
            const std::vector<std::size_t>& choices = found->second;
            const double reference = directed[current].incomingAngle;
            std::size_t next = choices.front();
            double bestGap = 1.0e18;
            for (const std::size_t candidate : choices) {
                double gap = reference - directed[candidate].outgoingAngle;
                while (gap <= 0.0) {
                    gap += 2.0 * 3.14159265358979323846;
                }
                while (gap > 2.0 * 3.14159265358979323846) {
                    gap -= 2.0 * 3.14159265358979323846;
                }
                if (gap < bestGap) {
                    bestGap = gap;
                    next = candidate;
                }
            }
            current = next;
            if (current == start) {
                break;
            }
        }
        if (broken || loop.edges.size() < 2) {
            continue;
        }
        // 同じ辺を2回使う面は、この段階では捨てる(袋小路を含む面)。
        loop.edgeSet = std::set<std::size_t>(loop.edges.begin(), loop.edges.end());
        if (loop.edgeSet.size() != loop.edges.size()) {
            continue;
        }
        // 面積と向き。
        std::vector<Point2> outline;
        for (std::size_t at = 0; at < loop.edges.size(); ++at) {
            const Edge& edge = edges[loop.edges[at]];
            if (!loop.reversed[at]) {
                for (std::size_t p = 0; p + 1 < edge.points.size(); ++p) {
                    outline.push_back(flatten(edge.points[p]));
                }
            } else {
                for (std::size_t p = edge.points.size() - 1; p > 0; --p) {
                    outline.push_back(flatten(edge.points[p]));
                }
            }
        }
        const double signedArea = geometry::SignedArea(outline);
        if (std::abs(signedArea) <= toleranceMm * toleranceMm) {
            continue;
        }
        loop.areaMm2 = std::abs(signedArea);
        loop.origin = frame.origin;
        loop.normal = signedArea > 0.0 ? plane.normal : -plane.normal;
        loops.push_back(std::move(loop));
    }
    (void)vertices;
    return loops;
}

//! 閉路の境界を1本の点列にする。
[[nodiscard]] std::vector<Vector3> LoopOutline(const std::vector<Edge>& edges,
    const LoopCandidate& loop)
{
    std::vector<Vector3> outline;
    for (std::size_t index = 0; index < loop.edges.size(); ++index) {
        const Edge& edge = edges[loop.edges[index]];
        if (!loop.reversed[index]) {
            for (std::size_t p = 0; p + 1 < edge.points.size(); ++p) {
                outline.push_back(edge.points[p]);
            }
        } else {
            for (std::size_t p = edge.points.size() - 1; p > 0; --p) {
                outline.push_back(edge.points[p]);
            }
        }
    }
    return outline;
}

//! 閉路の重心。体積の計算に使う。
[[nodiscard]] Vector3 LoopCentroid(const std::vector<Edge>& edges, const LoopCandidate& loop)
{
    Vector3 sum{};
    std::size_t count = 0;
    for (const std::size_t index : loop.edges) {
        for (const Vector3& point : edges[index].points) {
            sum = sum + point;
            ++count;
        }
    }
    return count > 0 ? sum * (1.0 / static_cast<double>(count)) : Vector3{};
}

//! シェルの体積。各面を重心から扇状に三角形へ割り、発散定理で足す。
[[nodiscard]] double ShellVolume(const std::vector<Edge>& edges,
    const std::vector<LoopCandidate>& loops, const std::vector<bool>& flipped)
{
    double volume = 0.0;
    for (std::size_t at = 0; at < loops.size(); ++at) {
        const LoopCandidate& loop = loops[at];
        const Vector3 center = LoopCentroid(edges, loop);
        const std::vector<Vector3> outline = LoopOutline(edges, loop);
        for (std::size_t p = 0; p < outline.size(); ++p) {
            const Vector3& a = center;
            Vector3 b = outline[p];
            Vector3 c = outline[(p + 1) % outline.size()];
            if (flipped[at]) {
                std::swap(b, c);
            }
            volume += Dot(a, Cross(b, c)) / 6.0;
        }
    }
    return volume;
}

//! 面の向きを揃える。隣り合う面が共有する辺を、互いに逆向きに通ること。
//! 揃えられなければ閉シェルではない(メビウスの帯のような形)。
//! 面の集まりを、辺を共有しているかどうかで塊に分ける。
//!
//! 離れた2つの立体を同時に選ぶと、辺をちょうど2回ずつ使う組合せが
//! 「2つぶんまとめて」1つ出てくる。これをそのまま1つの殻として扱うと、
//! 向きが揃わずに候補ごと落ちてしまう(実際に落ちていた)。
//! 塊に分けてから、それぞれを1つの立体として扱う(AT-GEO-013)。
[[nodiscard]] std::vector<std::vector<std::size_t>> SplitIntoComponents(
    const std::vector<LoopCandidate>& loops)
{
    std::vector<std::size_t> parent(loops.size());
    for (std::size_t index = 0; index < parent.size(); ++index) {
        parent[index] = index;
    }
    const std::function<std::size_t(std::size_t)> find =
        [&parent, &find](std::size_t index) -> std::size_t {
        while (parent[index] != index) {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    };
    // 同じ辺を持つ面どうしをつなぐ。
    std::map<std::size_t, std::vector<std::size_t>> facesOfEdge;
    for (std::size_t at = 0; at < loops.size(); ++at) {
        for (const std::size_t edge : loops[at].edgeSet) {
            facesOfEdge[edge].push_back(at);
        }
    }
    for (const auto& entry : facesOfEdge) {
        for (std::size_t at = 1; at < entry.second.size(); ++at) {
            const std::size_t first = find(entry.second.front());
            const std::size_t other = find(entry.second[at]);
            if (first != other) {
                parent[other] = first;
            }
        }
    }
    // 塊ごとにまとめる。並びは面の番号順で決まるので、毎回同じ順になる。
    std::map<std::size_t, std::vector<std::size_t>> grouped;
    for (std::size_t at = 0; at < loops.size(); ++at) {
        grouped[find(at)].push_back(at);
    }
    std::vector<std::vector<std::size_t>> components;
    components.reserve(grouped.size());
    for (auto& entry : grouped) {
        components.push_back(std::move(entry.second));
    }
    return components;
}

[[nodiscard]] bool OrientShell(const std::vector<LoopCandidate>& loops,
    std::vector<bool>& flipped)
{
    flipped.assign(loops.size(), false);
    if (loops.empty()) {
        return false;
    }
    // 各面の「有向辺の集合」を作り、隣を辿って向きを決める。
    const auto directedEdges = [&](std::size_t face, bool flip) {
        std::vector<std::pair<std::size_t, bool>> list;
        for (std::size_t at = 0; at < loops[face].edges.size(); ++at) {
            const bool reversed = loops[face].reversed[at] != flip;
            list.emplace_back(loops[face].edges[at], reversed);
        }
        return list;
    };
    std::vector<bool> decided(loops.size(), false);
    decided[0] = true;
    std::vector<std::size_t> queue{0};
    while (!queue.empty()) {
        const std::size_t face = queue.back();
        queue.pop_back();
        const auto mine = directedEdges(face, flipped[face]);
        for (std::size_t other = 0; other < loops.size(); ++other) {
            if (other == face) {
                continue;
            }
            // 共有している辺を1本探す。
            std::size_t sharedEdge = 0;
            bool found = false;
            bool myReversed = false;
            for (const auto& item : mine) {
                if (loops[other].edgeSet.count(item.first) > 0) {
                    sharedEdge = item.first;
                    myReversed = item.second;
                    found = true;
                    break;
                }
            }
            if (!found) {
                continue;
            }
            bool theirReversed = false;
            for (std::size_t at = 0; at < loops[other].edges.size(); ++at) {
                if (loops[other].edges[at] == sharedEdge) {
                    theirReversed = loops[other].reversed[at];
                    break;
                }
            }
            // 共有辺は互いに逆向きでなければならない。
            const bool needFlip = (theirReversed == myReversed);
            if (!decided[other]) {
                decided[other] = true;
                flipped[other] = needFlip;
                queue.push_back(other);
            } else if (flipped[other] != needFlip) {
                return false;   // 向きを揃えられない
            }
        }
    }
    for (const bool value : decided) {
        if (!value) {
            return false;   // つながっていない面がある
        }
    }
    return true;
}

//! 各辺がちょうど2回使われる面の組合せを探す。
//!
//! まだ2回に足りていない辺のうち、いちばん小さい添字を「今決める辺」に決め、
//! その辺を含む面だけを試す。足りない回数が2ならば面を2枚まとめて選ぶ。
//! こうすると同じ組合せを順番違いで2度作らずに済む。
//! (面の添字を一律に昇順へ縛ると、後から必要になる小さい添字の面を
//!  選べなくなって正しい組合せを取り逃がす。)
void SearchCombinations(const std::vector<LoopCandidate>& loops,
    const std::vector<std::size_t>& allEdges, std::map<std::size_t, int>& usage,
    std::vector<bool>& taken, std::vector<std::size_t>& chosen,
    std::vector<std::vector<std::size_t>>& results, std::size_t& steps)
{
    constexpr std::size_t kStepLimit = 200000;
    if (steps++ > kStepLimit || results.size() >= 8) {
        return;
    }
    std::size_t target = allEdges.size();
    for (const std::size_t edge : allEdges) {
        const int used = usage.count(edge) > 0 ? usage[edge] : 0;
        if (used > 2) {
            return;   // 使いすぎ。この枝は無い
        }
        if (used < 2 && target == allEdges.size()) {
            target = edge;
        }
    }
    if (target == allEdges.size()) {
        std::vector<std::size_t> sorted = chosen;
        std::sort(sorted.begin(), sorted.end());
        results.push_back(std::move(sorted));
        return;
    }

    std::vector<std::size_t> candidates;
    for (std::size_t index = 0; index < loops.size(); ++index) {
        if (!taken[index] && loops[index].edgeSet.count(target) > 0) {
            candidates.push_back(index);
        }
    }
    const int used = usage.count(target) > 0 ? usage[target] : 0;
    const int need = 2 - used;
    if (static_cast<int>(candidates.size()) < need) {
        return;   // この辺を2回使える面が足りない
    }

    const auto take = [&](std::size_t index) {
        taken[index] = true;
        chosen.push_back(index);
        for (const std::size_t edge : loops[index].edgeSet) {
            ++usage[edge];
        }
    };
    const auto give = [&](std::size_t index) {
        for (const std::size_t edge : loops[index].edgeSet) {
            --usage[edge];
        }
        chosen.pop_back();
        taken[index] = false;
    };

    if (need == 2) {
        for (std::size_t a = 0; a < candidates.size(); ++a) {
            for (std::size_t b = a + 1; b < candidates.size(); ++b) {
                take(candidates[a]);
                take(candidates[b]);
                SearchCombinations(loops, allEdges, usage, taken, chosen, results, steps);
                give(candidates[b]);
                give(candidates[a]);
            }
        }
        return;
    }
    for (const std::size_t index : candidates) {
        take(index);
        SearchCombinations(loops, allEdges, usage, taken, chosen, results, steps);
        give(index);
    }
}

} // namespace

Result<WireCageAnalysis> AnalyzeWireCage(const std::vector<CageEdgeInput>& inputs,
    const GeometryTolerance& tolerance)
{
    return AnalyzeWireCage(inputs, {}, tolerance);
}

Result<WireCageAnalysis> AnalyzeWireCage(const std::vector<CageEdgeInput>& inputs,
    const std::vector<CageDeclaredPatch>& declaredPatches, const GeometryTolerance& tolerance)
{
    if (inputs.size() < 4) {
        return Result<WireCageAnalysis>::Failure(MakeError(kNoCandidate,
            "立体を囲むには線が足りません。",
            "選ばれている線は " + std::to_string(inputs.size()) + " 本です。"));
    }
    const double joinTolerance = std::max(tolerance.interactiveJoinMm,
        tolerance.modelLinearMm * 100.0);
    const double samplingTolerance = SamplingToleranceMm(tolerance);

    std::vector<Vertex> vertices;
    std::vector<Edge> edges;
    std::vector<Diagnostic> errors;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        Edge edge;
        edge.inputIndex = index;
        const auto sampled = geometry::SampleCurve(inputs[index].segment, samplingTolerance);
        for (const auto& point : sampled) {
            edge.points.push_back(point.position);
        }
        if (edge.points.size() < 2) {
            errors.push_back(MakeError(kBadInput, "点になっている線があります。",
                inputs[index].segmentId.ToString()));
            continue;
        }
        for (std::size_t at = 1; at < edge.points.size(); ++at) {
            edge.lengthMm += (edge.points[at] - edge.points[at - 1]).Length();
        }
        if (edge.lengthMm <= tolerance.modelLinearMm) {
            errors.push_back(MakeError(kBadInput, "長さが0の線があります。",
                inputs[index].segmentId.ToString()));
            continue;
        }
        edge.startVertex = AddVertex(vertices, edge.points.front(), joinTolerance);
        edge.endVertex = AddVertex(vertices, edge.points.back(), joinTolerance);
        if (edge.startVertex == edge.endVertex) {
            errors.push_back(MakeError(kBadInput,
                "始点と終点が同じ線は、この方法では使えません。",
                inputs[index].segmentId.ToString() + "。閉じた輪郭は分割してください。"));
            continue;
        }
        edges.push_back(std::move(edge));
    }
    if (!errors.empty()) {
        return Result<WireCageAnalysis>::Failure(std::move(errors));
    }

    // 同じ2頂点を結ぶ線が2本あると、どちらを使うか決められない。
    for (std::size_t a = 0; a < edges.size(); ++a) {
        for (std::size_t b = a + 1; b < edges.size(); ++b) {
            const bool sameEnds = (edges[a].startVertex == edges[b].startVertex
                                      && edges[a].endVertex == edges[b].endVertex)
                || (edges[a].startVertex == edges[b].endVertex
                    && edges[a].endVertex == edges[b].startVertex);
            if (!sameEnds) {
                continue;
            }
            // 端点が同じでも、途中が違えば別の線として使える(円弧と直線など)。
            const double deviation = geometry::MaximumDeviationTo(edges[a].points,
                edges[b].points);
            if (deviation <= joinTolerance) {
                errors.push_back(MakeError(kBadInput, "同じ線が2本選ばれています。",
                    inputs[edges[a].inputIndex].segmentId.ToString() + " と "
                        + inputs[edges[b].inputIndex].segmentId.ToString()));
            }
        }
    }
    if (!errors.empty()) {
        return Result<WireCageAnalysis>::Failure(std::move(errors));
    }

    for (std::size_t index = 0; index < edges.size(); ++index) {
        vertices[edges[index].startVertex].incidentEdges.push_back(index);
        vertices[edges[index].endVertex].incidentEdges.push_back(index);
    }
    // 端がどこにも繋がっていない線があれば、その時点で閉じていない。
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (vertices[index].incidentEdges.size() < 2) {
            return Result<WireCageAnalysis>::Failure(MakeError(kOpenShell,
                "端がつながっていない線があります。",
                "位置 (" + std::to_string(vertices[index].position.x) + ", "
                    + std::to_string(vertices[index].position.y) + ", "
                    + std::to_string(vertices[index].position.z) + ")。"));
        }
    }

    // 平面の候補を、頂点で交わる辺の組から作る。
    std::vector<CandidatePlane> planes;
    for (const Vertex& vertex : vertices) {
        for (std::size_t a = 0; a < vertex.incidentEdges.size(); ++a) {
            for (std::size_t b = a + 1; b < vertex.incidentEdges.size(); ++b) {
                const Edge& first = edges[vertex.incidentEdges[a]];
                const Edge& second = edges[vertex.incidentEdges[b]];
                std::vector<Vector3> both = first.points;
                both.insert(both.end(), second.points.begin(), second.points.end());
                const PlaneFit fit = geometry::FitPlane(both);
                if (!fit.valid || fit.maximumDeviationMm > tolerance.modelLinearMm) {
                    continue;
                }
                CandidatePlane plane{fit.origin, fit.normal};
                const bool known = std::any_of(planes.begin(), planes.end(),
                    [&](const CandidatePlane& other) {
                        return SamePlane(other, plane, tolerance.modelLinearMm);
                    });
                if (!known) {
                    planes.push_back(plane);
                }
            }
        }
    }

    // 平面ごとに面の候補を取り出し、辺集合が同じものはまとめる。
    std::vector<LoopCandidate> loops;
    for (const CandidatePlane& plane : planes) {
        std::vector<std::size_t> members;
        for (std::size_t index = 0; index < edges.size(); ++index) {
            if (EdgeOnPlane(edges[index], plane, tolerance.modelLinearMm)) {
                members.push_back(index);
            }
        }
        for (LoopCandidate& loop : ExtractPlanarLoops(edges, vertices, members, plane,
                 tolerance.modelLinearMm)) {
            const bool known = std::any_of(loops.begin(), loops.end(),
                [&](const LoopCandidate& other) { return other.edgeSet == loop.edgeSet; });
            if (!known) {
                loops.push_back(std::move(loop));
            }
        }
    }
    // 利用者が根拠を示した面を足す。平面でない面はここからしか来ない(§7.2)。
    for (std::size_t at = 0; at < declaredPatches.size(); ++at) {
        std::vector<std::size_t> members;
        for (const std::size_t inputIndex : declaredPatches[at].edgeIndices) {
            const auto found = std::find_if(edges.begin(), edges.end(),
                [&](const Edge& edge) { return edge.inputIndex == inputIndex; });
            if (found == edges.end()) {
                return Result<WireCageAnalysis>::Failure(MakeError(kBadInput,
                    "指定された面の境界に、選ばれていない線があります。",
                    std::to_string(at + 1) + " 枚目。"));
            }
            members.push_back(static_cast<std::size_t>(found - edges.begin()));
        }
        LoopCandidate loop;
        if (!BuildRing(edges, members, loop.edges, loop.reversed)) {
            return Result<WireCageAnalysis>::Failure(MakeError(kBadInput,
                "指定された面の境界が輪になっていません。",
                std::to_string(at + 1) + " 枚目。"));
        }
        loop.edgeSet = std::set<std::size_t>(loop.edges.begin(), loop.edges.end());
        if (loop.edgeSet.size() != loop.edges.size()) {
            return Result<WireCageAnalysis>::Failure(MakeError(kBadInput,
                "指定された面の境界で、同じ線が2回使われています。",
                std::to_string(at + 1) + " 枚目。"));
        }
        loop.declared = true;
        NewellNormalAndArea(LoopOutline(edges, loop), loop.normal, loop.areaMm2);
        loop.origin = LoopCentroid(edges, loop);
        const bool known = std::any_of(loops.begin(), loops.end(),
            [&](const LoopCandidate& other) { return other.edgeSet == loop.edgeSet; });
        if (!known) {
            loops.push_back(std::move(loop));
        }
    }

    if (loops.empty()) {
        return Result<WireCageAnalysis>::Failure(MakeError(kNoCandidate,
            "面になる閉じた輪郭が見つかりません。",
            "同じ平面の上で輪になっている線が必要です。"));
    }

    std::vector<std::size_t> allEdges;
    for (std::size_t index = 0; index < edges.size(); ++index) {
        allEdges.push_back(index);
    }
    std::map<std::size_t, int> usage;
    std::vector<bool> taken(loops.size(), false);
    std::vector<std::size_t> chosen;
    std::vector<std::vector<std::size_t>> combinations;
    std::size_t steps = 0;
    SearchCombinations(loops, allEdges, usage, taken, chosen, combinations, steps);

    if (combinations.empty()) {
        // どの辺が余っているかを言う。「閉じていない」より役に立つ。
        std::map<std::size_t, int> reachable;
        for (const LoopCandidate& loop : loops) {
            for (const std::size_t edge : loop.edgeSet) {
                ++reachable[edge];
            }
        }
        std::vector<std::string> lonely;
        for (const std::size_t edge : allEdges) {
            if (reachable[edge] < 2) {
                lonely.push_back(inputs[edges[edge].inputIndex].segmentId.ToString());
            }
        }
        std::string detail = "どの面にも2回使われない線があります。";
        if (!lonely.empty()) {
            detail += " 例: " + lonely.front();
            if (lonely.size() > 1) {
                detail += " ほか " + std::to_string(lonely.size() - 1) + " 本";
            }
        }
        return Result<WireCageAnalysis>::Failure(MakeError(kOpenShell,
            "選ばれた線は立体を囲んでいません。", detail));
    }

    WireCageAnalysis analysis;
    std::vector<std::vector<LoopCandidate>> shellFaces;
    for (const std::vector<std::size_t>& combination : combinations) {
        std::vector<LoopCandidate> whole;
        for (const std::size_t index : combination) {
            whole.push_back(loops[index]);
        }
        // 離れた立体は別々に扱う。まとめて1つの殻にしない。
        for (const std::vector<std::size_t>& component : SplitIntoComponents(whole)) {
            std::vector<LoopCandidate> part;
            part.reserve(component.size());
            for (const std::size_t at : component) {
                part.push_back(whole[at]);
            }
            shellFaces.push_back(std::move(part));
        }
    }
    for (const std::vector<LoopCandidate>& faces : shellFaces) {
        std::vector<bool> flipped;
        if (!OrientShell(faces, flipped)) {
            analysis.notes.push_back(MakeWarning(kNonManifold,
                "面の向きを揃えられない組合せがあったので、候補から外しました。",
                std::to_string(faces.size()) + " 面。"));
            continue;
        }
        double volume = ShellVolume(edges, faces, flipped);
        bool outward = volume >= 0.0;
        if (!outward) {
            volume = -volume;
            for (std::size_t at = 0; at < flipped.size(); ++at) {
                flipped[at] = !flipped[at];
            }
        }
        const double minimumVolume = tolerance.modelLinearMm * tolerance.modelLinearMm
            * tolerance.modelLinearMm;
        if (!(volume > minimumVolume)) {
            analysis.notes.push_back(MakeWarning(kZeroVolume,
                "厚みの無い組合せがあったので、候補から外しました。", {}));
            continue;
        }
        CageShell shell;
        shell.volumeMm3 = volume;
        shell.outwardOriented = true;
        shell.volumeIsApproximate = std::any_of(faces.begin(), faces.end(),
            [](const LoopCandidate& face) { return face.declared; });
        for (std::size_t at = 0; at < faces.size(); ++at) {
            CagePatch patch;
            patch.normal = flipped[at] ? -faces[at].normal : faces[at].normal;
            patch.areaMm2 = faces[at].areaMm2;
            patch.declared = faces[at].declared;
            // 面の向きを裏返すときは、辺を1本ずつ逆にするだけでは足りない。
            // 1周する順そのものも逆にしないと、隣どうしの端点が合わなくなる
            // (e1 の終点は e2 の始点だが、e1 を逆にすると終点が始点へ移る)。
            const std::size_t edgeCount = faces[at].edges.size();
            for (std::size_t step = 0; step < edgeCount; ++step) {
                const std::size_t index =
                    flipped[at] ? (edgeCount - 1 - step) : step;
                patch.edgeIndices.push_back(edges[faces[at].edges[index]].inputIndex);
                patch.reversed.push_back(faces[at].reversed[index] != flipped[at]);
            }
            // 代表する線は、辺のうちIDが最小のもの。並べ替えても同じキーになる。
            std::size_t representative = patch.edgeIndices.front();
            for (const std::size_t candidate : patch.edgeIndices) {
                if (inputs[candidate].segmentId.ToString()
                    < inputs[representative].segmentId.ToString()) {
                    representative = candidate;
                }
            }
            patch.key = MakeCagePatch(inputs[representative].segmentId);
            shell.patches.push_back(std::move(patch));
        }
        analysis.shells.push_back(std::move(shell));
    }

    if (analysis.shells.empty()) {
        return Result<WireCageAnalysis>::Failure(MakeError(kNoCandidate,
            "立体になる組合せが見つかりません。",
            "面の向きが揃わないか、厚みがありません。"));
    }
    if (analysis.shells.size() > 1) {
        // 候補が複数。黙って1つ選ばない(§7.3の6)。
        std::string detail;
        for (std::size_t at = 0; at < analysis.shells.size(); ++at) {
            detail += std::to_string(at + 1) + ": "
                + std::to_string(analysis.shells[at].patches.size()) + " 面 / 体積 "
                + std::to_string(analysis.shells[at].volumeMm3) + " mm3。";
        }
        analysis.notes.push_back(MakeWarning(kAmbiguous,
            "立体の作り方が何通りかあります。どれにするか選んでください。", detail));
    }

    std::set<std::size_t> used;
    for (const CageShell& shell : analysis.shells) {
        for (const CagePatch& patch : shell.patches) {
            for (const std::size_t index : patch.edgeIndices) {
                used.insert(index);
            }
        }
    }
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (used.count(index) == 0) {
            analysis.unusedEdges.push_back(index);
        }
    }
    // 閉じた立体が複数見つかったら、そのまま作ると複数の部品になる。
    // 勝手に1つへまとめず、いくつになるかを先に知らせる(geometry-contract §7.2)。
    if (analysis.shells.size() > 1) {
        analysis.notes.push_back(MakeInformation(kMultipleSolids,
            "閉じた立体が複数あります。",
            std::to_string(analysis.shells.size())
                + " 個の部品になります。続けるなら、この数で作ります。"));
    }
    return Result<WireCageAnalysis>::Success(std::move(analysis));
}

} // namespace kachakacha::v2::modeling

namespace kachakacha::v2::modeling {

base::Result<std::vector<WireCagePart>> PlanWireCageParts(const WireCageAnalysis& analysis,
    const std::vector<std::size_t>& chosenShells)
{
    using Out = base::Result<std::vector<WireCagePart>>;
    if (chosenShells.empty()) {
        return Out::Failure(base::MakeError("GEO-S011",
            "確定する立体が選ばれていません。", "候補から1つ以上選んでください。"));
    }
    std::vector<std::size_t> seen;
    std::vector<WireCagePart> parts;
    parts.reserve(chosenShells.size());
    for (const std::size_t index : chosenShells) {
        if (index >= analysis.shells.size()) {
            return Out::Failure(base::MakeError("GEO-S011",
                "確定する立体が選ばれていません。",
                "番号 " + std::to_string(index + 1) + " の立体は候補にありません。"));
        }
        if (std::find(seen.begin(), seen.end(), index) != seen.end()) {
            return Out::Failure(base::MakeError("GEO-S010",
                "同じ立体が2度選ばれています。",
                "1つの立体からは1つの部品しか作れません。"));
        }
        seen.push_back(index);
        const CageShell& shell = analysis.shells[index];
        WireCagePart part;
        part.shellIndex = index;
        part.volumeMm3 = shell.volumeMm3;
        part.volumeIsApproximate = shell.volumeIsApproximate;
        for (const CagePatch& patch : shell.patches) {
            part.faceKeys.push_back(patch.key);
        }
        parts.push_back(std::move(part));
    }
    return Out::Success(std::move(parts));
}

} // namespace kachakacha::v2::modeling
