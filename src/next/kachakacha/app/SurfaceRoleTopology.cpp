#include "kachakacha/app/SurfaceRoleTopology.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <utility>

namespace kachakacha::v2::app::detail {

using modeling::ChainRole;

namespace {

//! 役割を決めるための点列の点の上限。多すぎると交わりを探すのが遅くなる。
constexpr std::size_t kMaximumProbePoints = 600;

// ---------------------------------------------------------------- 線を調べる

[[nodiscard]] Probe MakeProbe(const RoleWire& wire, std::size_t order, double sampleMm,
    double joinMm)
{
    Probe probe;
    probe.id = wire.id;
    probe.order = order;
    probe.wire = &wire;
    double chord = sampleMm;
    probe.points = geometry::SampleChain(wire.segments, chord);
    while (probe.points.size() > kMaximumProbePoints && chord < 1.0) {
        chord *= 4.0;
        probe.points = geometry::SampleChain(wire.segments, chord);
    }
    probe.chordMm = chord;
    if (probe.points.size() < 2) {
        return probe;
    }
    probe.valid = true;
    probe.start = probe.points.front();
    probe.end = probe.points.back();
    probe.closed = (probe.start - probe.end).Length() <= joinMm;
    probe.low = probe.points.front();
    probe.high = probe.points.front();
    for (std::size_t at = 0; at < probe.points.size(); ++at) {
        const Vector3& p = probe.points[at];
        probe.low = Vector3{std::min(probe.low.x, p.x), std::min(probe.low.y, p.y),
            std::min(probe.low.z, p.z)};
        probe.high = Vector3{std::max(probe.high.x, p.x), std::max(probe.high.y, p.y),
            std::max(probe.high.z, p.z)};
        if (at > 0) {
            probe.lengthMm += (p - probe.points[at - 1]).Length();
        }
    }
    probe.sizeMm = (probe.high - probe.low).Length();
    std::vector<Vector3> distinct = probe.points;
    if (probe.closed) {
        distinct.pop_back();
    }
    probe.centroid = geometry::Centroid(distinct);
    return probe;
}

[[nodiscard]] bool BoxesNear(const Probe& a, const Probe& b, double margin)
{
    return a.low.x - margin <= b.high.x && b.low.x - margin <= a.high.x
        && a.low.y - margin <= b.high.y && b.low.y - margin <= a.high.y
        && a.low.z - margin <= b.high.z && b.low.z - margin <= a.high.z;
}

//! along を辿りながら、across までが許容の内側へ入る回数。
[[nodiscard]] int ApproachesAlong(const std::vector<Vector3>& along,
    const std::vector<Vector3>& across, double limitMm)
{
    int count = 0;
    bool inside = false;
    for (std::size_t at = 0; at + 1 < along.size(); ++at) {
        const std::vector<Vector3> edge{along[at], along[at + 1]};
        const geometry::PolylineApproach approach = geometry::ClosestApproachBetween(edge, across);
        const bool touching = approach.valid && approach.distanceMm <= limitMm;
        if (touching && !inside) {
            ++count;
        }
        inside = touching;
    }
    return count;
}

[[nodiscard]] bool NearEnd(const Probe& probe, const Vector3& point, double limitMm)
{
    return !probe.closed
        && ((point - probe.start).Length() <= limitMm || (point - probe.end).Length() <= limitMm);
}

} // namespace

[[nodiscard]] Topology Examine(const std::vector<RoleWire>& wires,
    const geometry::GeometryTolerance& tolerance)
{
    Topology topology;
    topology.joinMm = std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm * 100.0);
    const double sampleMm = std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
    for (std::size_t index = 0; index < wires.size(); ++index) {
        topology.probes.push_back(MakeProbe(wires[index], index, sampleMm, topology.joinMm));
    }
    for (std::size_t a = 0; a < topology.probes.size(); ++a) {
        for (std::size_t b = a + 1; b < topology.probes.size(); ++b) {
            const Probe& pa = topology.probes[a];
            const Probe& pb = topology.probes[b];
            const double limit = topology.joinMm + pa.chordMm + pb.chordMm;
            if (!pa.valid || !pb.valid || !BoxesNear(pa, pb, limit)) {
                continue;
            }
            const auto approach = geometry::ClosestApproachBetween(pa.points, pb.points);
            if (!approach.valid || approach.distanceMm > limit) {
                continue;
            }
            Contact contact;
            contact.a = a;
            contact.b = b;
            contact.onA = approach.firstParameter;
            contact.onB = approach.secondParameter;
            contact.at = (approach.firstPoint + approach.secondPoint) * 0.5;
            contact.count = std::max(ApproachesAlong(pa.points, pb.points, limit),
                ApproachesAlong(pb.points, pa.points, limit));
            contact.count = std::max(contact.count, 1);
            contact.atEndOfA = NearEnd(pa, contact.at, limit * 2.0);
            contact.atEndOfB = NearEnd(pb, contact.at, limit * 2.0);
            topology.contacts.push_back(contact);
        }
    }
    return topology;
}

// ---------------------------------------------------------------- 閉じた輪

namespace {

[[nodiscard]] Loop MakeLoop(const Topology& topology, std::vector<std::size_t> members)
{
    Loop loop;
    std::sort(members.begin(), members.end());
    loop.members = std::move(members);
    std::vector<Vector3> points;
    Vector3 low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    Vector3 high = low * -1.0;
    for (const std::size_t index : loop.members) {
        const Probe& probe = topology.probes[index];
        points.insert(points.end(), probe.points.begin(), probe.points.end());
        low = Vector3{std::min(low.x, probe.low.x), std::min(low.y, probe.low.y),
            std::min(low.z, probe.low.z)};
        high = Vector3{std::max(high.x, probe.high.x), std::max(high.y, probe.high.y),
            std::max(high.z, probe.high.z)};
    }
    loop.sizeMm = (high - low).Length();
    loop.plane = geometry::FitPlane(points);
    loop.planar = loop.plane.valid && loop.plane.maximumDeviationMm <= topology.joinMm;
    return loop;
}

} // namespace

//! 端どうしでつながって閉じた輪(開いた線 2 本以上)と、1 本で閉じた線。
//! eligible が偽の線(人が断面・ガイド・中心線に決めた線)は輪に入れない。
[[nodiscard]] std::vector<Loop> FindLoops(const Topology& topology,
    const std::vector<bool>& eligible)
{
    std::vector<Loop> loops;
    std::vector<Vector3> vertices;
    std::vector<std::pair<std::size_t, std::size_t>> ends(topology.probes.size(),
        {static_cast<std::size_t>(-1), static_cast<std::size_t>(-1)});
    const auto vertexFor = [&](const Vector3& point) {
        for (std::size_t v = 0; v < vertices.size(); ++v) {
            if ((vertices[v] - point).Length() <= topology.joinMm * 2.0) {
                return v;
            }
        }
        vertices.push_back(point);
        return vertices.size() - 1;
    };
    std::vector<std::size_t> open;
    for (std::size_t index = 0; index < topology.probes.size(); ++index) {
        const Probe& probe = topology.probes[index];
        if (!probe.valid || !eligible[index]) {
            continue;
        }
        if (probe.closed) {
            loops.push_back(MakeLoop(topology, {index}));
            continue;
        }
        ends[index] = {vertexFor(probe.start), vertexFor(probe.end)};
        open.push_back(index);
    }
    // 端を共有する線を 1 組にまとめる。
    std::vector<std::size_t> group(topology.probes.size());
    std::iota(group.begin(), group.end(), 0);
    const auto root = [&group](std::size_t x) {
        while (group[x] != x) {
            group[x] = group[group[x]];
            x = group[x];
        }
        return x;
    };
    for (const std::size_t a : open) {
        for (const std::size_t b : open) {
            if (a < b && (ends[a].first == ends[b].first || ends[a].first == ends[b].second
                             || ends[a].second == ends[b].first || ends[a].second == ends[b].second)) {
                group[root(a)] = root(b);
            }
        }
    }
    std::map<std::size_t, std::vector<std::size_t>> components;
    for (const std::size_t index : open) {
        components[root(index)].push_back(index);
    }
    for (const auto& [key, members] : components) {
        (void)key;
        if (members.size() < 2) {
            continue;
        }
        // どの端も、ちょうど 2 本の線の端になっていれば 1 つの閉じた輪。
        std::map<std::size_t, int> degree;
        for (const std::size_t index : members) {
            ++degree[ends[index].first];
            ++degree[ends[index].second];
        }
        const bool cycle = std::all_of(degree.begin(), degree.end(),
            [](const auto& entry) { return entry.second == 2; });
        if (cycle) {
            loops.push_back(MakeLoop(topology, members));
        }
    }
    return loops;
}

//! 点が、平らな輪の内側にあるか(輪の平面へ落として数える)。
[[nodiscard]] bool InsideLoop(const Topology& topology, const Loop& loop, const Vector3& point)
{
    if (!loop.planar) {
        return false;
    }
    const geometry::PlanarFrame frame = geometry::MakeFrame(loop.plane);
    // 輪を 1 本の点列にする(端の向きをそろえてつなぐ)。
    std::vector<std::size_t> remaining = loop.members;
    std::vector<Vector3> ring = topology.probes[remaining.front()].points;
    remaining.erase(remaining.begin());
    while (!remaining.empty()) {
        bool extended = false;
        for (auto it = remaining.begin(); it != remaining.end(); ++it) {
            const Probe& next = topology.probes[*it];
            if ((next.start - ring.back()).Length() <= topology.joinMm * 2.0) {
                ring.insert(ring.end(), next.points.begin() + 1, next.points.end());
            } else if ((next.end - ring.back()).Length() <= topology.joinMm * 2.0) {
                ring.insert(ring.end(), next.points.rbegin() + 1, next.points.rend());
            } else {
                continue;
            }
            remaining.erase(it);
            extended = true;
            break;
        }
        if (!extended) {
            return false;
        }
    }
    const auto polygon = geometry::ProjectToFrame(ring, frame);
    const auto target = geometry::ProjectToFrame({point}, frame).front();
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& pi = polygon[i];
        const auto& pj = polygon[j];
        if ((pi.v > target.v) != (pj.v > target.v)
            && target.u < (pj.u - pi.u) * (target.v - pi.v) / (pj.v - pi.v) + pi.u) {
            inside = !inside;
        }
    }
    return inside;
}

// ---------------------------------------------------------------- 二部に分ける(はしご形)

namespace {

[[nodiscard]] double AverageLength(const Topology& topology, const std::vector<std::size_t>& part)
{
    double total = 0.0;
    for (const std::size_t index : part) {
        total += topology.probes[index].lengthMm;
    }
    return part.empty() ? 0.0 : total / static_cast<double>(part.size());
}

[[nodiscard]] std::size_t FirstOrder(const Topology& topology, const std::vector<std::size_t>& part)
{
    std::size_t first = std::numeric_limits<std::size_t>::max();
    for (const std::size_t index : part) {
        first = std::min(first, topology.probes[index].order);
    }
    return first;
}

//! どちらの組が断面か。人の決めた役割 → 閉じた線 → 本数 → 長さ → 選んだ順。
[[nodiscard]] bool FirstPartIsSections(const Topology& topology,
    const std::vector<WireRoleChoice>& fixed, const std::vector<std::size_t>& a,
    const std::vector<std::size_t>& b)
{
    const auto has = [&fixed](const std::vector<std::size_t>& part, WireRoleChoice role) {
        return std::any_of(part.begin(), part.end(),
            [&](std::size_t index) { return fixed[index] == role; });
    };
    const bool aSections = has(a, WireRoleChoice::Section) || has(b, WireRoleChoice::Guide);
    const bool bSections = has(b, WireRoleChoice::Section) || has(a, WireRoleChoice::Guide);
    if (aSections != bSections) {
        return aSections;
    }
    const auto anyClosed = [&topology](const std::vector<std::size_t>& part) {
        return std::any_of(part.begin(), part.end(),
            [&](std::size_t index) { return topology.probes[index].closed; });
    };
    if (anyClosed(a) != anyClosed(b)) {
        return anyClosed(a);
    }
    if (a.size() != b.size()) {
        return a.size() > b.size();
    }
    const double la = AverageLength(topology, a);
    const double lb = AverageLength(topology, b);
    if (std::abs(la - lb) > 0.01 * std::max(la, lb)) {
        return la < lb;   // 長い組がガイド(長手)
    }
    return FirstOrder(topology, a) < FirstOrder(topology, b);
}

} // namespace

//! members の交わりを二部(互いに交わらない 2 組)に分ける。分けられなければ found = false。
[[nodiscard]] Ladder SplitIntoLadder(const Topology& topology,
    const std::vector<std::size_t>& members, const std::vector<WireRoleChoice>& fixed)
{
    Ladder ladder;
    std::map<std::size_t, int> color;
    std::vector<std::size_t> ordered = members;
    std::sort(ordered.begin(), ordered.end(), [&topology](std::size_t l, std::size_t r) {
        return topology.probes[l].order < topology.probes[r].order;
    });
    std::size_t components = 0;
    for (const std::size_t seed : ordered) {
        if (color.count(seed) != 0) {
            continue;
        }
        ++components;
        color[seed] = 0;
        std::vector<std::size_t> queue{seed};
        while (!queue.empty()) {
            const std::size_t at = queue.back();
            queue.pop_back();
            for (const std::size_t next : topology.Neighbors(at)) {
                if (std::find(members.begin(), members.end(), next) == members.end()) {
                    continue;
                }
                if (color.count(next) == 0) {
                    color[next] = 1 - color[at];
                    queue.push_back(next);
                } else if (color[next] == color[at]) {
                    ladder.problems.push_back("3 本以上が互いに交わっていて、断面とガイドの"
                                              "2 組に分けられません。");
                    return ladder;
                }
            }
        }
    }
    if (components > 1) {
        ladder.problems.push_back("線のつながりが " + std::to_string(components)
            + " 組に分かれています。1 枚の面にする線だけを選んでください。");
        return ladder;
    }
    std::vector<std::size_t> a;
    std::vector<std::size_t> b;
    for (const std::size_t index : ordered) {
        (color[index] == 0 ? a : b).push_back(index);
    }
    if (a.empty() || b.empty()) {
        return ladder;
    }
    ladder.found = true;
    const bool aSections = FirstPartIsSections(topology, fixed, a, b);
    ladder.sections = aSections ? a : b;
    ladder.rails = aSections ? b : a;
    ladder.complete = true;
    for (const std::size_t s : ladder.sections) {
        for (const std::size_t r : ladder.rails) {
            const Contact* contact = topology.Between(s, r);
            ladder.complete = ladder.complete && contact != nullptr && contact->count == 1;
        }
    }
    return ladder;
}

// ---------------------------------------------------------------- 候補を作り、検査に通す

namespace {

[[nodiscard]] ChainRole RequestRole(GuideSurfaceMethod method, WireRoleChoice role)
{
    switch (role) {
    case WireRoleChoice::Section:
        return method == GuideSurfaceMethod::GordonNetwork
                || method == GuideSurfaceMethod::CurveNetworkExact
            ? ChainRole::GuideU
            : ChainRole::Section;
    case WireRoleChoice::Guide:
        return method == GuideSurfaceMethod::GordonNetwork
                || method == GuideSurfaceMethod::CurveNetworkExact
            ? ChainRole::GuideV
            : ChainRole::GuideU;
    case WireRoleChoice::Boundary:
        return method == GuideSurfaceMethod::PlanarBoundary ? ChainRole::OuterBoundary
                                                            : ChainRole::BoundarySide;
    case WireRoleChoice::PassThrough:
        return ChainRole::GuideU;
    case WireRoleChoice::Centerline:
        return ChainRole::Centerline;
    case WireRoleChoice::Auto:
        break;
    }
    return ChainRole::Section;
}

} // namespace

//! 候補を幾何の検査に通す。平面と境界面は、輪の検査(つながり・同じ平面)で済ませる
//! (表へ入れるときに 1 本の輪にまとめる道が別にあるため)。
[[nodiscard]] SurfaceMethodCandidate Check(const Topology& topology, const Draft& draft,
    const geometry::GeometryTolerance& tolerance)
{
    SurfaceMethodCandidate candidate;
    candidate.method = draft.method;
    for (std::size_t index = 0; index < topology.probes.size(); ++index) {
        ClassifiedWire wire;
        wire.id = topology.probes[index].id;
        wire.role = draft.roles[index];
        wire.reasonJa = draft.reasons[index];
        candidate.wires.push_back(std::move(wire));
    }
    if (!draft.refusalJa.empty()) {
        candidate.reasonJa = draft.refusalJa;
        return candidate;
    }
    if (draft.method == GuideSurfaceMethod::PlanarBoundary
        || draft.method == GuideSurfaceMethod::BoundaryFill) {
        candidate.feasible = true;
        candidate.reasonJa = draft.reasonJa;
        return candidate;
    }
    modeling::GuideSurfaceRequest request;
    request.method = draft.method;
    std::map<ChainRole, int> numbers;
    for (std::size_t index = 0; index < topology.probes.size(); ++index) {
        const Probe& probe = topology.probes[index];
        if (!probe.valid) {
            continue;
        }
        modeling::GuideChain chain;
        chain.role = RequestRole(draft.method, draft.roles[index]);
        chain.index = ++numbers[chain.role];
        chain.sourceEntityId = probe.id;
        chain.segments = probe.wire->segments;
        chain.closed = probe.closed;
        request.chains.push_back(std::move(chain));
    }
    const auto analysis = modeling::AnalyzeGuideSurfaceRequest(request, tolerance);
    candidate.feasible = analysis.HasValue();
    candidate.reasonJa = candidate.feasible ? draft.reasonJa : analysis.FirstSummaryJa();
    return candidate;
}

} // namespace kachakacha::v2::app::detail
