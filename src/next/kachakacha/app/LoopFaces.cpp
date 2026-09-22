#include "kachakacha/app/LoopFaces.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireConnect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace kachakacha::v2::app {

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
[[nodiscard]] std::vector<LoopGap> FindGaps(const std::vector<GuideTableSelection>& selections,
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
        const auto isLine = [&](std::size_t selection) {
            const auto& segments = selections[selection].segments;
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

[[nodiscard]] LoopFace MakeFace(const std::vector<LoopEdge>& edges, LoopCycle cycle,
    const geometry::GeometryTolerance& tolerance)
{
    NormalizeCycle(cycle);
    LoopFace face;
    for (std::size_t at = 0; at < cycle.edges.size(); ++at) {
        face.selections.push_back(edges[cycle.edges[at]].selection);
        face.forward.push_back(cycle.forward[at]);
    }
    face.ring = CycleRing(edges, cycle);
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
    std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed& l, const Keyed& r) {
        if (l.cycle.edges.size() != r.cycle.edges.size()) {
            return l.cycle.edges.size() < r.cycle.edges.size();
        }
        return l.area < r.area;
    });
    std::vector<int> used(edges.size(), 0);
    std::vector<LoopFace> faces;
    for (Keyed& item : keyed) {
        const bool crowded = std::any_of(item.cycle.edges.begin(), item.cycle.edges.end(),
            [&](std::size_t edge) { return used[edge] >= 2; });
        if (crowded) {
            continue;
        }
        for (const std::size_t edge : item.cycle.edges) {
            ++used[edge];
        }
        faces.push_back(MakeFace(edges, std::move(item.cycle), tolerance));
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

} // namespace

std::string LoopFaceMethodLabelJa(LoopFaceMethod method)
{
    switch (method) {
    case LoopFaceMethod::Planar:       return "平面";
    case LoopFaceMethod::FourEdge:     return "四辺面";
    case LoopFaceMethod::BoundaryFill: return "境界面(近似)";
    }
    return "";
}

Result<LoopFacePlan> PlanLoopFaces(const std::vector<GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<LoopFacePlan>;
    const double joinMm = std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm * 100.0);
    LoopGraph graph(joinMm);
    std::vector<LoopEdge> edges;
    for (std::size_t index = 0; index < selections.size(); ++index) {
        if (!selections[index].segments.empty()) {
            edges.push_back(MakeLoopEdge(index, selections[index].segments, graph));
        }
    }
    if (edges.empty()) {
        return Out::Failure(MakeError(kNoInput, "選んだ線がありません。", "線を選んでから押してください。"));
    }
    if (edges.size() > kMaximumEdges) {
        return Out::Failure(MakeError(kTooMany, "選んだ線が多すぎて、外周を決められません。",
            std::to_string(edges.size()) + " 本(上限 " + std::to_string(kMaximumEdges)
                + " 本)。面ごとに分けて選んでください。"));
    }
    LoopFacePlan plan;
    // ずれは、行き止まりを外す前の次数で見る(外すと次数 1 が消えて分からなくなる)。
    plan.gaps = FindGaps(selections, edges, graph, NearMissMm(edges, joinMm));
    PruneDeadEnds(edges, graph.NodeCount());
    plan.faces = ChooseFaces(edges, FindLoopCycles(edges), tolerance);
    std::vector<bool> inFace(selections.size(), false);
    int counts[3] = {0, 0, 0};
    for (const LoopFace& face : plan.faces) {
        ++counts[static_cast<int>(face.method)];
        for (const std::size_t selection : face.selections) {
            inFace[selection] = true;
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
    if (!plan.unused.empty()) {
        plan.summaryJa += "・使わない線 " + std::to_string(plan.unused.size());
    }
    if (!plan.gaps.empty()) {
        plan.summaryJa += "・ずれ " + std::to_string(plan.gaps.size());
    }
    return Out::Success(std::move(plan));
}

Result<GuideTable> LoopFaceTable(const std::vector<GuideTableSelection>& selections,
    const LoopFace& face, const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<GuideTable>;
    GuideTable table;
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
    for (const std::size_t selection : face.selections) {
        const auto added = modeling::AddSelectionAsNewRow(table, ChainRole::BoundarySide,
            selections[selection]);
        if (!added.HasValue()) {
            return Out::Failure(added.Diagnostics());
        }
        table = added.Value();
    }
    return Out::Success(table);
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

std::string LoopGapTextJa(const std::vector<GuideTableSelection>& selections, const LoopGap& gap)
{
    return selections[gap.firstSelection].label + " と " + selections[gap.secondSelection].label
        + " の端が " + Mm(gap.distanceMm) + " mm 離れています"
        + (gap.movable ? "" : "(どちらも直線でないので寄せられません)");
}

} // namespace kachakacha::v2::app
