#include "kachakacha/app/LoopFaces.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/CurveTrim.h"
#include "kachakacha/geometry/WireConnect.h"
#include "kachakacha/modeling/GuideSurfaceSampling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace kachakacha::v2::app {

using base::EntityId;
using base::MakeError;
using base::Result;
using geometry::CurveKind;
using geometry::CurveSegment;
using geometry::Vector3;
using modeling::ChainRole;
using modeling::GuideSurfaceMethod;
using modeling::GuideTable;
using modeling::GuideTableSelection;

namespace {

constexpr const char* kNoLoop = "UI-R011";
constexpr const char* kTooMany = "UI-R012";
constexpr const char* kNoInput = "UI-R004";
constexpr const char* kSplitPending = "UI-R013";
constexpr std::size_t kMaximumEdges = 24;

//! ずれとして挙げる距離の上限。いちばん長い線の 1/4 か、許容差の 10 倍の大きいほう。
//! 相手は行き止まりの端どうしに限るので、これくらい広くても別の端と取り違えにくい。
[[nodiscard]] double NearMissMm(const std::vector<LoopEdge>& edges, double joinMm)
{
    double longest = 0.0;
    for (const LoopEdge& edge : edges) {
        double length = 0.0;
        for (std::size_t at = 0; at + 1 < edge.points.size(); ++at) {
            length += (edge.points[at + 1] - edge.points[at]).Length();
        }
        longest = std::max(longest, length);
    }
    return std::max(joinMm * 10.0, longest * 0.25);
}

//! 節の次数(生きている辺だけ)。
[[nodiscard]] std::vector<int> Degrees(const std::vector<LoopEdge>& edges, std::size_t nodeCount)
{
    std::vector<int> degree(nodeCount, 0);
    for (const LoopEdge& edge : edges) {
        if (edge.alive) {
            ++degree[edge.from];
            ++degree[edge.to];
        }
    }
    return degree;
}

//! 行き止まりの端(次数 1 の節)どうしで近いものを、近い順に組にする。
[[nodiscard]] std::vector<LoopGap> FindGaps(const std::vector<LoopPiece>& pieces,
    const std::vector<LoopEdge>& edges, const LoopGraph& graph, double nearMissMm)
{
    const std::vector<int> degree = Degrees(edges, graph.NodeCount());
    struct End {
        std::size_t node;
        std::size_t selection;
        bool atEnd;
    };
    std::vector<End> ends;
    for (const LoopEdge& edge : edges) {
        if (degree[edge.from] == 1) {
            ends.push_back({edge.from, edge.selection, false});
        }
        if (degree[edge.to] == 1) {
            ends.push_back({edge.to, edge.selection, true});
        }
    }
    struct Pair {
        std::size_t a;
        std::size_t b;
        double distance;
    };
    std::vector<Pair> pairs;
    for (std::size_t a = 0; a < ends.size(); ++a) {
        for (std::size_t b = a + 1; b < ends.size(); ++b) {
            if (ends[a].node == ends[b].node) {
                continue;
            }
            const double distance =
                (graph.NodeAt(ends[a].node) - graph.NodeAt(ends[b].node)).Length();
            if (distance <= nearMissMm) {
                pairs.push_back({a, b, distance});
            }
        }
    }
    std::stable_sort(pairs.begin(), pairs.end(),
        [](const Pair& l, const Pair& r) { return l.distance < r.distance; });
    std::vector<bool> taken(ends.size(), false);
    std::vector<LoopGap> gaps;
    for (const Pair& pair : pairs) {
        if (taken[pair.a] || taken[pair.b]) {
            continue;
        }
        taken[pair.a] = taken[pair.b] = true;
        LoopGap gap;
        gap.firstSelection = ends[pair.a].selection;
        gap.firstAtEnd = ends[pair.a].atEnd;
        gap.secondSelection = ends[pair.b].selection;
        gap.secondAtEnd = ends[pair.b].atEnd;
        gap.distanceMm = pair.distance;
        const auto isLine = [&](std::size_t piece) {
            const auto& segments = pieces[piece].segments;
            return segments.size() == 1 && segments.front().Kind() == CurveKind::Line;
        };
        gap.movable = isLine(gap.firstSelection) || isLine(gap.secondSelection);
        gaps.push_back(gap);
    }
    return gaps;
}

//! 輪の節の並び(辺の数と同じ長さ。i 番目の辺の入口の節)。
[[nodiscard]] std::vector<std::size_t> CycleNodes(const std::vector<LoopEdge>& edges,
    const LoopCycle& cycle)
{
    std::vector<std::size_t> nodes;
    for (std::size_t at = 0; at < cycle.edges.size(); ++at) {
        const LoopEdge& edge = edges[cycle.edges[at]];
        nodes.push_back(cycle.forward[at] ? edge.from : edge.to);
    }
    return nodes;
}

//! 弦を持つか: 輪に入っていない選んだ線で、輪の隣り合わない 2 節を結ぶもの。
//! 同じ 2 節を結ぶ別の線(直線と円弧の半月)は弦ではない。
[[nodiscard]] bool HasChord(const std::vector<LoopEdge>& edges, const LoopCycle& cycle)
{
    const std::vector<std::size_t> nodes = CycleNodes(edges, cycle);
    const std::size_t count = nodes.size();
    if (count < 4) {
        return false;
    }
    const auto positionOf = [&](std::size_t node) -> std::optional<std::size_t> {
        for (std::size_t at = 0; at < count; ++at) {
            if (nodes[at] == node) {
                return at;
            }
        }
        return std::nullopt;
    };
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const LoopEdge& edge = edges[index];
        if (!edge.alive || edge.from == edge.to
            || std::find(cycle.edges.begin(), cycle.edges.end(), index) != cycle.edges.end()) {
            continue;
        }
        const auto a = positionOf(edge.from);
        const auto b = positionOf(edge.to);
        if (!a.has_value() || !b.has_value()) {
            continue;
        }
        const std::size_t apart = (*a > *b ? *a - *b : *b - *a);
        const bool adjacent = apart == 1 || apart == count - 1;
        if (!adjacent) {
            return true;
        }
    }
    return false;
}

//! 輪の向きをそろえる: 最初の辺を始点→終点の向きにたどる形にする(表の 1 行目の向きが
//! 元の線のままになり、開き直しても同じ向きに並ぶ)。
void NormalizeCycle(LoopCycle& cycle)
{
    const auto forwardAt = std::find(cycle.forward.begin(), cycle.forward.end(), true);
    if (forwardAt == cycle.forward.end()) {
        // 全部逆向き: 輪を逆回りにする。
        std::reverse(cycle.edges.begin(), cycle.edges.end());
        std::reverse(cycle.forward.begin(), cycle.forward.end());
        for (std::size_t at = 0; at < cycle.forward.size(); ++at) {
            cycle.forward[at] = true;
        }
        return;
    }
    const std::size_t shift = static_cast<std::size_t>(forwardAt - cycle.forward.begin());
    std::rotate(cycle.edges.begin(), cycle.edges.begin() + static_cast<std::ptrdiff_t>(shift),
        cycle.edges.end());
    std::rotate(cycle.forward.begin(), cycle.forward.begin() + static_cast<std::ptrdiff_t>(shift),
        cycle.forward.end());
}

//! 平面の輪の中に、すでに採った同じ平面の輪の重心があるか(2D の点の内外、平面の局所座標で)。
[[nodiscard]] bool ContainsAcceptedFace(const LoopFace& face, const std::vector<LoopFace>& accepted,
    const geometry::GeometryTolerance& tolerance)
{
    const geometry::PlaneFit fit = geometry::FitPlane(face.ring);
    if (!fit.valid) {
        return false;
    }
    const Vector3 normal = geometry::Normalized(fit.normal);
    // 平面の局所座標。
    Vector3 u = geometry::Cross(normal, Vector3{0.0, 0.0, 1.0});
    if (u.Length() < 1.0e-6) {
        u = geometry::Cross(normal, Vector3{0.0, 1.0, 0.0});
    }
    u = geometry::Normalized(u);
    const Vector3 v = geometry::Cross(normal, u);
    const auto local = [&](const Vector3& p) {
        const Vector3 d = p - fit.origin;
        return std::pair<double, double>{geometry::Dot(d, u), geometry::Dot(d, v)};
    };
    const auto inside = [&](const Vector3& p) {
        const auto [px, py] = local(p);
        bool in = false;
        const std::size_t n = face.ring.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const auto [xi, yi] = local(face.ring[i]);
            const auto [xj, yj] = local(face.ring[j]);
            if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) {
                in = !in;
            }
        }
        return in;
    };
    const double planeTolerance = std::max(tolerance.modelLinearMm * 100.0, 1.0e-6);
    for (const LoopFace& other : accepted) {
        if (other.method != LoopFaceMethod::Planar || other.ring.empty()) {
            continue;
        }
        const Vector3 centroid = geometry::Centroid(other.ring);
        if (std::abs(geometry::Dot(centroid - fit.origin, normal)) > planeTolerance) {
            continue;   // 別の平面
        }
        if (inside(centroid)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] LoopFace MakeFace(const std::vector<LoopEdge>& edges, LoopCycle cycle,
    const geometry::GeometryTolerance& tolerance)
{
    NormalizeCycle(cycle);
    LoopFace face;
    face.edgeIndices = cycle.edges;
    for (std::size_t at = 0; at < cycle.edges.size(); ++at) {
        face.selections.push_back(edges[cycle.edges[at]].selection);
        face.forward.push_back(cycle.forward[at]);
    }
    face.ring = CycleRing(edges, cycle);
    if (!face.ring.empty()) {
        face.previewLines.push_back(face.ring);
        face.previewLines.back().push_back(face.ring.front());
    }
    face.areaMm2 = EnclosedArea(edges, cycle);
    const geometry::PlaneFit fit = geometry::FitPlane(face.ring);
    face.planeDeviationMm = fit.valid ? fit.maximumDeviationMm
                                      : std::numeric_limits<double>::infinity();
    if (fit.valid && fit.maximumDeviationMm <= tolerance.modelLinearMm) {
        face.method = LoopFaceMethod::Planar;
    } else if (cycle.edges.size() == 4) {
        face.method = LoopFaceMethod::FourEdge;
    } else {
        face.method = LoopFaceMethod::BoundaryFill;
    }
    return face;
}

//! 同じ辺の集まりか(向き・始まりが違うだけの輪)。
[[nodiscard]] bool SameEdges(const LoopCycle& l, const LoopCycle& r)
{
    if (l.edges.size() != r.edges.size()) {
        return false;
    }
    std::vector<std::size_t> a = l.edges;
    std::vector<std::size_t> b = r.edges;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

//! 面になる輪を選ぶ(ヘッダの 3)。
[[nodiscard]] std::vector<LoopFace> ChooseFaces(const std::vector<LoopEdge>& edges,
    std::vector<LoopCycle> cycles, const geometry::GeometryTolerance& tolerance)
{
    std::vector<LoopCycle> unique;
    for (LoopCycle& cycle : cycles) {
        const bool known = std::any_of(unique.begin(), unique.end(),
            [&](const LoopCycle& other) { return SameEdges(other, cycle); });
        if (!known && !HasChord(edges, cycle)) {
            unique.push_back(std::move(cycle));
        }
    }
    struct Keyed {
        LoopCycle cycle;
        double area;
    };
    std::vector<Keyed> keyed;
    for (LoopCycle& cycle : unique) {
        const double area = EnclosedArea(edges, cycle);
        keyed.push_back({std::move(cycle), area});
    }
    // 小さい輪から採る(大きい輪は小さい輪の合わさりであることが多い)。同じ大きさなら辺の少ない順。
    std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed& l, const Keyed& r) {
        if (std::abs(l.area - r.area) > 1.0e-9 * std::max(1.0, std::max(l.area, r.area))) {
            return l.area < r.area;
        }
        return l.cycle.edges.size() < r.cycle.edges.size();
    });
    std::vector<int> used(edges.size(), 0);
    std::vector<LoopFace> faces;
    for (Keyed& item : keyed) {
        const bool crowded = std::any_of(item.cycle.edges.begin(), item.cycle.edges.end(),
            [&](std::size_t edge) { return used[edge] >= 2; });
        if (crowded) {
            continue;
        }
        LoopFace face = MakeFace(edges, std::move(item.cycle), tolerance);
        // 同じ平面に載る輪で、すでに採った小さい輪を中に含むものは、その合わさり(円 + 2 本の
        // 半径で言えば、扇 2 つに対する円板)なので面にしない。
        if (face.method == LoopFaceMethod::Planar && ContainsAcceptedFace(face, faces, tolerance)) {
            continue;
        }
        for (const std::size_t edge : face.edgeIndices) {
            ++used[edge];
        }
        face.edgeIndices.clear();
        faces.push_back(std::move(face));
    }
    return faces;
}

[[nodiscard]] std::string Mm(double value)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << value;
    return out.str();
}


//! 線の端が別の線の途中に乗っている(T 字)ところ。
struct TJunction {
    std::size_t target = 0;        //!< 乗られている線(選択番号)
    std::size_t segmentIndex = 0;  //!< その線の何本目の区間か
    double parameter = 0.0;        //!< 区間の中の t
    Vector3 point{};
    std::size_t by = 0;            //!< 乗っている端を持つ線
};

[[nodiscard]] Vector3 ChainStart(const std::vector<CurveSegment>& chain)
{
    return chain.front().StartPoint();
}
[[nodiscard]] Vector3 ChainEnd(const std::vector<CurveSegment>& chain)
{
    return chain.back().EndPoint();
}

//! 線 by の端 p が、線 target の途中に乗っているか。乗っていれば区間と t。
[[nodiscard]] std::optional<TJunction> JunctionOf(const std::vector<GuideTableSelection>& selections,
    std::size_t target, std::size_t by, const Vector3& p, double joinMm)
{
    const auto& chain = selections[target].segments;
    const bool closed = (ChainStart(chain) - ChainEnd(chain)).Length() <= joinMm;
    if (!closed && ((p - ChainStart(chain)).Length() <= joinMm
                       || (p - ChainEnd(chain)).Length() <= joinMm)) {
        return std::nullopt;   // 端と端。T 字ではなく普通のつながり。
    }
    for (std::size_t k = 0; k < chain.size(); ++k) {
        const auto closest = chain[k].ClosestPoint(p);
        if (closest.distance > joinMm) {
            continue;
        }
        // 区間の端にぴったりなら、隣の区間の継ぎ目 = 分けなくてよい(節になる)。
        const bool atSegmentEnd = closest.parameter <= 1.0e-6 || closest.parameter >= 1.0 - 1.0e-6;
        if (atSegmentEnd && !(closed && chain.size() == 1)) {
            if (chain.size() > 1) {
                return std::nullopt;   // 折れ線の角に乗っている: 角は端点として節になる
            }
            return std::nullopt;
        }
        TJunction junction;
        junction.target = target;
        junction.segmentIndex = k;
        junction.parameter = closest.parameter;
        junction.point = closest.point;
        junction.by = by;
        return junction;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<TJunction> FindTJunctions(
    const std::vector<GuideTableSelection>& selections, double joinMm)
{
    std::vector<TJunction> found;
    for (std::size_t by = 0; by < selections.size(); ++by) {
        const auto& chain = selections[by].segments;
        if (chain.empty()) {
            continue;
        }
        for (const Vector3& p : {ChainStart(chain), ChainEnd(chain)}) {
            for (std::size_t target = 0; target < selections.size(); ++target) {
                if (target == by || selections[target].segments.empty()) {
                    continue;
                }
                const auto junction = JunctionOf(selections, target, by, p, joinMm);
                if (!junction.has_value()) {
                    continue;
                }
                const bool known = std::any_of(found.begin(), found.end(), [&](const TJunction& other) {
                    return other.target == target && (other.point - junction->point).Length() <= joinMm;
                });
                if (!known) {
                    found.push_back(*junction);
                }
            }
        }
    }
    return found;
}

//! 線(区間の並び)を、区間ごとの t で分けて片にする。円は最初の区切りを継ぎ目にした一周の円弧に
//! 読み替えてから分ける。
[[nodiscard]] Result<std::vector<std::vector<CurveSegment>>> SplitChainAt(
    const std::vector<CurveSegment>& chain, std::vector<std::pair<std::size_t, double>> cuts)
{
    using Out = Result<std::vector<std::vector<CurveSegment>>>;
    std::sort(cuts.begin(), cuts.end());
    std::vector<std::vector<CurveSegment>> parts;
    std::vector<CurveSegment> current;
    for (std::size_t k = 0; k < chain.size(); ++k) {
        CurveSegment rest = chain[k];
        double base = 0.0;
        bool circle = rest.Kind() == CurveKind::Circle;
        for (const auto& [index, t] : cuts) {
            if (index != k) {
                continue;
            }
            if (circle) {
                // 一周の円弧にする(継ぎ目 = この区切り)。以後の t は継ぎ目からの位置。
                geometry::CutPoint cut;
                cut.parameter = t;
                cut.point = rest.Evaluate(t);
                const auto seam = geometry::SplitAtCut(rest, cut);
                if (!seam.HasValue()) {
                    return Out::Failure(seam.Diagnostics());
                }
                rest = seam.Value().first;
                base = t;
                circle = false;
                continue;
            }
            const double local = base >= 1.0 - 1.0e-9 ? 1.0 : (t - base) / (1.0 - base);
            if (local <= 1.0e-6 || local >= 1.0 - 1.0e-6) {
                continue;
            }
            const auto split = rest.Split(local);
            if (!split.HasValue()) {
                return Out::Failure(split.Diagnostics());
            }
            current.push_back(*split.Value().first);
            parts.push_back(current);
            current.clear();
            rest = *split.Value().second;
            base = t;
        }
        current.push_back(rest);
    }
    parts.push_back(current);
    return Out::Success(std::move(parts));
}

//! 分けた線の区切り(元の t は円の継ぎ目からの位置に直す)。
[[nodiscard]] std::vector<std::pair<std::size_t, double>> CutsOf(const LoopSplit& split,
    const std::vector<CurveSegment>& chain)
{
    std::vector<std::pair<std::size_t, double>> cuts;
    const std::size_t count = chain.size();
    for (const double global : split.parameters) {
        const double scaled = global * static_cast<double>(count);
        std::size_t index = static_cast<std::size_t>(std::floor(scaled));
        if (index >= count) {
            index = count - 1;
        }
        cuts.emplace_back(index, scaled - static_cast<double>(index));
    }
    if (count == 1 && chain.front().Kind() == CurveKind::Circle && cuts.size() > 1) {
        // 継ぎ目(最初の区切り)からの位置に直す。
        std::sort(cuts.begin(), cuts.end());
        const double seam = cuts.front().second;
        for (std::size_t at = 1; at < cuts.size(); ++at) {
            double t = cuts[at].second - seam;
            if (t < 0.0) {
                t += 1.0;
            }
            cuts[at].second = t;
        }
        // 先頭は継ぎ目そのもの(SplitChainAt が円の読み替えに使う)。
    }
    return cuts;
}

//! T 字から、分ける線の一覧と片を作る。
[[nodiscard]] Result<std::vector<LoopSplit>> CollectSplits(
    const std::vector<GuideTableSelection>& selections, const std::vector<TJunction>& junctions)
{
    using Out = Result<std::vector<LoopSplit>>;
    std::vector<LoopSplit> splits;
    for (const TJunction& junction : junctions) {
        auto found = std::find_if(splits.begin(), splits.end(),
            [&](const LoopSplit& split) { return split.source == junction.target; });
        if (found == splits.end()) {
            LoopSplit split;
            split.source = junction.target;
            splits.push_back(split);
            found = splits.end() - 1;
        }
        const double count = static_cast<double>(selections[junction.target].segments.size());
        found->parameters.push_back((static_cast<double>(junction.segmentIndex) + junction.parameter) / count);
        found->points.push_back(junction.point);
        found->bySelections.push_back(junction.by);
    }
    for (LoopSplit& split : splits) {
        // t の昇順にそろえる(points / bySelections も一緒に)。
        std::vector<std::size_t> order(split.parameters.size());
        for (std::size_t at = 0; at < order.size(); ++at) {
            order[at] = at;
        }
        std::sort(order.begin(), order.end(),
            [&](std::size_t l, std::size_t r) { return split.parameters[l] < split.parameters[r]; });
        LoopSplit sorted = split;
        for (std::size_t at = 0; at < order.size(); ++at) {
            sorted.parameters[at] = split.parameters[order[at]];
            sorted.points[at] = split.points[order[at]];
            sorted.bySelections[at] = split.bySelections[order[at]];
        }
        split = sorted;
    }
    std::sort(splits.begin(), splits.end(),
        [](const LoopSplit& l, const LoopSplit& r) { return l.source < r.source; });
    return Out::Success(std::move(splits));
}

//! 片の一覧: 分けていない線はそのまま(同じ番号)、分けた線は印だけ残して片を後ろに足す。
[[nodiscard]] Result<std::vector<LoopPiece>> MakePieces(
    const std::vector<GuideTableSelection>& selections, const std::vector<LoopSplit>& splits)
{
    using Out = Result<std::vector<LoopPiece>>;
    std::vector<LoopPiece> pieces;
    for (std::size_t index = 0; index < selections.size(); ++index) {
        LoopPiece piece;
        piece.source = index;
        piece.segments = selections[index].segments;
        pieces.push_back(std::move(piece));
    }
    for (const LoopSplit& split : splits) {
        const auto parts = SplitChainAt(selections[split.source].segments,
            CutsOf(split, selections[split.source].segments));
        if (!parts.HasValue()) {
            return Out::Failure(parts.Diagnostics());
        }
        pieces[split.source].split = true;
        for (const auto& part : parts.Value()) {
            LoopPiece piece;
            piece.source = split.source;
            piece.segments = part;
            piece.part = true;
            pieces.push_back(std::move(piece));
        }
    }
    return Out::Success(std::move(pieces));
}

//! 輪が無いとき: 開いた線が互いに触れずに並んでいればロフト(断面の順は重心の主軸)。
[[nodiscard]] std::optional<LoopFace> LoftFace(const std::vector<LoopEdge>& edges,
    const LoopGraph& graph)
{
    std::vector<int> degree(graph.NodeCount(), 0);
    for (const LoopEdge& edge : edges) {
        ++degree[edge.from];
        ++degree[edge.to];
    }
    std::vector<modeling::detail::SampledChain> sampled;
    std::vector<std::size_t> indices;
    for (std::size_t at = 0; at < edges.size(); ++at) {
        const LoopEdge& edge = edges[at];
        if (edge.from == edge.to || degree[edge.from] != 1 || degree[edge.to] != 1) {
            return std::nullopt;   // 触れている線がある: 断面の並びではない
        }
        modeling::detail::SampledChain chain;
        chain.chainIndex = at;
        chain.points = edge.points;
        chain.centroid = geometry::Centroid(edge.points);
        sampled.push_back(std::move(chain));
        indices.push_back(at);
    }
    if (sampled.size() < 2) {
        return std::nullopt;
    }
    const auto ordering = modeling::detail::OrderSections(sampled, indices);
    LoopFace face;
    face.method = LoopFaceMethod::Loft;
    for (const std::size_t at : ordering.chainIndices) {
        face.selections.push_back(edges[at].selection);
        face.forward.push_back(true);
        face.previewLines.push_back(edges[at].points);
    }
    // 断面の端どうしを結ぶ線(下見で、どこからどこへ渡るかが見える)。
    for (std::size_t at = 0; at + 1 < ordering.chainIndices.size(); ++at) {
        const auto& a = edges[ordering.chainIndices[at]].points;
        const auto& b = edges[ordering.chainIndices[at + 1]].points;
        face.previewLines.push_back(std::vector<Vector3>{a.front(), b.front()});
        face.previewLines.push_back(std::vector<Vector3>{a.back(), b.back()});
    }
    return face;
}

} // namespace

std::string LoopFaceMethodLabelJa(LoopFaceMethod method)
{
    switch (method) {
    case LoopFaceMethod::Planar:       return "平面";
    case LoopFaceMethod::FourEdge:     return "四辺面";
    case LoopFaceMethod::BoundaryFill: return "境界面(近似)";
    case LoopFaceMethod::Loft:         return "ロフト";
    }
    return "";
}

std::vector<LoopFaceMethod> LoopFaceMethodChoices(std::size_t edgeCount, bool planar, bool loft)
{
    if (loft) {
        return {LoopFaceMethod::Loft};
    }
    std::vector<LoopFaceMethod> choices;
    if (planar) {
        choices.push_back(LoopFaceMethod::Planar);
    }
    if (edgeCount == 4) {
        choices.push_back(LoopFaceMethod::FourEdge);
    }
    if (!planar || edgeCount != 4) {
        choices.push_back(LoopFaceMethod::BoundaryFill);
    }
    return choices;
}

std::string LoopPieceLabelJa(const std::vector<GuideTableSelection>& selections,
    const LoopFacePlan& plan, std::size_t piece)
{
    if (piece >= plan.pieces.size()) {
        return piece < selections.size() ? selections[piece].label : "?";
    }
    const LoopPiece& item = plan.pieces[piece];
    const std::string base = item.source < selections.size() ? selections[item.source].label : "?";
    if (!item.part) {
        return base;
    }
    int number = 0;
    for (std::size_t at = 0; at <= piece; ++at) {
        if (plan.pieces[at].part && plan.pieces[at].source == item.source) {
            ++number;
        }
    }
    return base + "(片 " + std::to_string(number) + ")";
}

std::string LoopSplitTextJa(const std::vector<GuideTableSelection>& selections,
    const LoopSplit& split)
{
    std::string who;
    for (const std::size_t by : split.bySelections) {
        if (!who.empty()) {
            who += "・";
        }
        who += by < selections.size() ? selections[by].label : "?";
    }
    const std::string target = split.source < selections.size() ? selections[split.source].label : "?";
    return who + " の端が " + target + " の途中に乗っています(" + target + " を "
        + std::to_string(split.parameters.size()) + " か所で分けます)";
}


//! 点から折れ線までの距離。
[[nodiscard]] double DistanceToPolyline(const Vector3& p, const std::vector<Vector3>& polyline)
{
    if (polyline.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    double best = (p - polyline.front()).Length();
    for (std::size_t k = 1; k < polyline.size(); ++k) {
        const Vector3 a = polyline[k - 1];
        const Vector3 b = polyline[k];
        const Vector3 ab = b - a;
        const double len2 = geometry::Dot(ab, ab);
        double t = len2 > 0.0 ? geometry::Dot(p - a, ab) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const Vector3 q{a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t};
        best = std::min(best, (p - q).Length());
    }
    return best;
}

//! 片の線が、すでにある面の縁の折れ線に重なるか(標本点が全部 tol 以内)。
[[nodiscard]] bool PieceLiesOn(const std::vector<CurveSegment>& chain,
    const std::vector<Vector3>& polyline, double tolMm)
{
    const std::vector<Vector3> samples = geometry::SampleChain(chain, tolMm * 0.5);
    if (samples.size() < 2) {
        return false;
    }
    return std::all_of(samples.begin(), samples.end(),
        [&](const Vector3& p) { return DistanceToPolyline(p, polyline) <= tolMm; });
}

//! 輪の辺ごとに隣(すでにある面の縁 / 同じ計画の別の輪)を書く。
void AnnotateNeighbors(LoopFacePlan& plan, const std::vector<LoopNeighborCurve>& neighbors,
    double joinMm)
{
    const double tolMm = std::max(joinMm * 10.0, 0.05);
    for (std::size_t f = 0; f < plan.faces.size(); ++f) {
        LoopFace& face = plan.faces[f];
        if (face.method == LoopFaceMethod::Loft) {
            continue;
        }
        face.edges.assign(face.selections.size(), LoopFaceEdge{});
        for (std::size_t e = 0; e < face.selections.size(); ++e) {
            const std::size_t piece = face.selections[e];
            for (std::size_t g = 0; g < plan.faces.size(); ++g) {
                const LoopFace& other = plan.faces[g];
                if (g != f && other.method != LoopFaceMethod::Loft
                    && std::find(other.selections.begin(), other.selections.end(), piece)
                        != other.selections.end()) {
                    face.edges[e].neighborFace = g;
                }
            }
            if (piece >= plan.pieces.size()) {
                continue;
            }
            for (const LoopNeighborCurve& curve : neighbors) {
                if (PieceLiesOn(plan.pieces[piece].segments, curve.polyline, tolMm)) {
                    face.edges[e].neighborSurface = curve.surface;
                    break;
                }
            }
        }
    }
}

Result<LoopFacePlan> PlanLoopFaces(const std::vector<GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance, const std::vector<LoopNeighborCurve>& neighbors)
{
    using Out = Result<LoopFacePlan>;
    const double joinMm = std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm * 100.0);
    std::size_t given = 0;
    for (const auto& selection : selections) {
        given += selection.segments.empty() ? 0 : 1;
    }
    if (given == 0) {
        return Out::Failure(MakeError(kNoInput, "選んだ線がありません。", "線を選んでから押してください。"));
    }
    if (given > kMaximumEdges) {
        return Out::Failure(MakeError(kTooMany, "選んだ線が多すぎて、外周を決められません。",
            std::to_string(given) + " 本(上限 " + std::to_string(kMaximumEdges)
                + " 本)。面ごとに分けて選んでください。"));
    }
    LoopFacePlan plan;
    // T 字: 端が別の線の途中に乗っていれば、その線を分けた片で輪を探す。
    const auto splits = CollectSplits(selections, FindTJunctions(selections, joinMm));
    if (!splits.HasValue()) {
        return Out::Failure(splits.Diagnostics());
    }
    plan.splits = splits.Value();
    const auto pieces = MakePieces(selections, plan.splits);
    if (!pieces.HasValue()) {
        return Out::Failure(pieces.Diagnostics());
    }
    plan.pieces = pieces.Value();
    LoopGraph graph(joinMm);
    std::vector<LoopEdge> edges;
    for (std::size_t index = 0; index < plan.pieces.size(); ++index) {
        const LoopPiece& piece = plan.pieces[index];
        if (!piece.split && !piece.segments.empty()) {
            edges.push_back(MakeLoopEdge(index, piece.segments, graph));
        }
    }
    // ずれは、行き止まりを外す前の次数で見る(外すと次数 1 が消えて分からなくなる)。
    plan.gaps = FindGaps(plan.pieces, edges, graph, NearMissMm(edges, joinMm));
    const std::vector<LoopEdge> before = edges;
    PruneDeadEnds(edges, graph.NodeCount());
    plan.faces = ChooseFaces(edges, FindLoopCycles(edges), tolerance);
    if (plan.faces.empty() && plan.gaps.empty()) {
        if (auto loft = LoftFace(before, graph)) {
            plan.faces.push_back(std::move(*loft));
        }
    }
    std::vector<bool> inFace(selections.size(), false);
    int counts[4] = {0, 0, 0, 0};
    for (const LoopFace& face : plan.faces) {
        ++counts[static_cast<int>(face.method)];
        for (const std::size_t piece : face.selections) {
            inFace[plan.pieces[piece].source] = true;
        }
    }
    for (std::size_t index = 0; index < selections.size(); ++index) {
        if (!inFace[index] && !selections[index].segments.empty()) {
            plan.unused.push_back(index);
        }
    }
    if (plan.faces.empty() && plan.gaps.empty()) {
        return Out::Failure(MakeError(kNoLoop, "選んだ線の中に、閉じた輪がありません。",
            "端点どうしがつながっているか確かめてください。"
            "離れているところは端点スナップで引き直すとつながります。"));
    }
    plan.summaryJa = "平面 " + std::to_string(counts[0]) + "・四辺面 " + std::to_string(counts[1])
        + "・境界面 " + std::to_string(counts[2]);
    if (counts[3] > 0) {
        plan.summaryJa += "・ロフト " + std::to_string(counts[3]);
    }
    if (!plan.splits.empty()) {
        plan.summaryJa += "・T 字 " + std::to_string(plan.splits.size());
    }
    if (!plan.unused.empty()) {
        plan.summaryJa += "・使わない線 " + std::to_string(plan.unused.size());
    }
    if (!plan.gaps.empty()) {
        plan.summaryJa += "・ずれ " + std::to_string(plan.gaps.size());
    }
    AnnotateNeighbors(plan, neighbors, joinMm);
    return Out::Success(std::move(plan));
}

Result<GuideTable> LoopFaceTable(const std::vector<GuideTableSelection>& selections,
    const LoopFace& face, const geometry::GeometryTolerance& tolerance,
    const std::vector<modeling::SurfaceContinuity>& continuity,
    const std::vector<EntityId>& supports)
{
    using Out = Result<GuideTable>;
    for (const std::size_t index : face.selections) {
        if (index >= selections.size()) {
            return Out::Failure(MakeError(kSplitPending, "T 字で分けた線がまだ分けられていません。",
                "先に線を分けてから(Enter)、もう一度計画します。"));
        }
    }
    GuideTable table;
    if (face.method == LoopFaceMethod::Loft) {
        table.method = face.selections.size() == 2 ? GuideSurfaceMethod::RuledSections
                                                    : GuideSurfaceMethod::LoftSections;
        for (const std::size_t selection : face.selections) {
            const auto added = modeling::AddSelectionAsNewRow(table, ChainRole::Section,
                selections[selection]);
            if (!added.HasValue()) {
                return Out::Failure(added.Diagnostics());
            }
            table = added.Value();
        }
        table.lockSectionOrder = true;   // 並びは計画が決めた(重心の主軸)
        return Out::Success(table);
    }
    if (face.method == LoopFaceMethod::Planar) {
        table.method = GuideSurfaceMethod::PlanarBoundary;
        // 外形 1 行に、輪をたどる順に足す(向きは足すときにそろう)。
        auto made = modeling::AddSelectionAsNewRow(table, ChainRole::OuterBoundary,
            selections[face.selections.front()]);
        for (std::size_t at = 1; made.HasValue() && at < face.selections.size(); ++at) {
            made = AppendSelectionToRow(made.Value(), 0, selections[face.selections[at]], tolerance);
        }
        if (!made.HasValue()) {
            return Out::Failure(made.Diagnostics());
        }
        return Out::Success(made.Value());
    }
    table.method = face.method == LoopFaceMethod::FourEdge ? GuideSurfaceMethod::FourEdgePatch
                                                            : GuideSurfaceMethod::BoundaryFill;
    for (std::size_t e = 0; e < face.selections.size(); ++e) {
        const auto added = modeling::AddSelectionAsNewRow(table, ChainRole::BoundarySide,
            selections[face.selections[e]]);
        if (!added.HasValue()) {
            return Out::Failure(added.Diagnostics());
        }
        table = added.Value();
        // 辺の連続: 支持面があるときだけ。無ければ G0 のまま(黙って G1 にしない)。
        const bool hasSupport = e < supports.size() && !supports[e].IsNil();
        if (e < continuity.size() && hasSupport) {
            table.rows.back().continuity = continuity[e];
            table.rows.back().supportSurfaceId = supports[e];
        }
    }
    return Out::Success(table);
}

std::string LoopFaceEdgeTextJa(const LoopFaceEdge& edge, const std::string& neighborSurfaceLabel)
{
    if (edge.neighborSurface.has_value()) {
        return "すでにある面 " + neighborSurfaceLabel + " の縁";
    }
    if (edge.neighborFace.has_value()) {
        return "輪 " + std::to_string(*edge.neighborFace + 1) + " と共有";
    }
    return "隣なし";
}

Result<LoopGapFix> CloseLoopGap(const std::vector<GuideTableSelection>& selections,
    const LoopGap& gap)
{
    using Out = Result<LoopGapFix>;
    const auto& first = selections[gap.firstSelection].segments;
    const auto& second = selections[gap.secondSelection].segments;
    if (first.empty() || second.empty()) {
        return Out::Failure(MakeError(kNoInput, "選んだ線がありません。", {}));
    }
    const CurveSegment& a = gap.firstAtEnd ? first.back() : first.front();
    const CurveSegment& b = gap.secondAtEnd ? second.back() : second.front();
    const Vector3 pa = gap.firstAtEnd ? a.EndPoint() : a.StartPoint();
    const Vector3 pb = gap.secondAtEnd ? b.EndPoint() : b.StartPoint();
    const bool aLine = first.size() == 1 && a.Kind() == CurveKind::Line;
    const bool bLine = second.size() == 1 && b.Kind() == CurveKind::Line;
    if (!aLine && !bLine) {
        return Out::Failure(MakeError("GEO-E011", "この線は端点だけを動かせません。",
            "どちらも直線でないので、端を寄せると途中の形まで変わります。"
            "先にトリムか延長で長さを合わせてください。"));
    }
    LoopGapFix fix;
    const auto move = [](const CurveSegment& line, bool atEnd, const Vector3& target) {
        return atEnd ? CurveSegment::MakeLine(line.StartPoint(), target)
                     : CurveSegment::MakeLine(target, line.EndPoint());
    };
    if (aLine && bLine) {
        // 直線どうしは中点へ(端点一致と同じ。どちらかだけがずれない)。
        const Vector3 joint = (pa + pb) * 0.5;
        const auto ma = move(a, gap.firstAtEnd, joint);
        const auto mb = move(b, gap.secondAtEnd, joint);
        if (!ma.HasValue() || !mb.HasValue()) {
            return Out::Failure(MakeError("GEO-E004", "寄せると線の長さが 0 になります。", {}));
        }
        fix.first = ma.Value();
        fix.second = mb.Value();
        fix.movedMm = gap.distanceMm * 0.5;
        return Out::Success(std::move(fix));
    }
    // 片方だけ直線: 直線の端を相手の端へ。相手(円弧など)は動かさない。
    const auto moved = aLine ? move(a, gap.firstAtEnd, pb) : move(b, gap.secondAtEnd, pa);
    if (!moved.HasValue()) {
        return Out::Failure(MakeError("GEO-E004", "寄せると線の長さが 0 になります。", {}));
    }
    if (aLine) {
        fix.first = moved.Value();
    } else {
        fix.second = moved.Value();
    }
    fix.movedMm = gap.distanceMm;
    return Out::Success(std::move(fix));
}

Result<std::vector<std::vector<CurveSegment>>> SplitLoopSource(
    const std::vector<GuideTableSelection>& selections, const LoopSplit& split)
{
    using Out = Result<std::vector<std::vector<CurveSegment>>>;
    if (split.source >= selections.size() || selections[split.source].segments.empty()) {
        return Out::Failure(MakeError(kNoInput, "選んだ線がありません。", {}));
    }
    const auto& chain = selections[split.source].segments;
    return SplitChainAt(chain, CutsOf(split, chain));
}

std::string LoopGapTextJa(const std::vector<GuideTableSelection>& selections, const LoopGap& gap)
{
    return selections[gap.firstSelection].label + " と " + selections[gap.secondSelection].label
        + " の端が " + Mm(gap.distanceMm) + " mm 離れています"
        + (gap.movable ? "" : "(どちらも直線でないので寄せられません)");
}

} // namespace kachakacha::v2::app
