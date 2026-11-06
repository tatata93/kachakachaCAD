#include "kachakacha/app/ProfileNetwork.h"
#include "kachakacha/app/ProfileCurveBounds.h"
#include "kachakacha/app/ProfilePlaneIndex.h"
#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>

namespace kachakacha::v2::app::detail {
namespace {
using geometry::Vector3;
using geometry::CurveSegment;
using modeling::SnapCurve;

void EndpointCuts(const CurveSegment& a, const CurveSegment& b,
    std::vector<double>& cuts, double eps)
{
    for (const auto point : {b.StartPoint(), b.EndPoint()}) {
        const auto closest = a.ClosestPoint(point);
        if (closest.distance <= eps) cuts.push_back(closest.parameter);
    }
}

std::vector<SnapCurve> SplitNetwork(const std::vector<SnapCurve>& curves,
    const geometry::GeometryTolerance& tolerance)
{
    const double eps = tolerance.interactiveJoinMm;
    std::vector<std::vector<double>> cuts(curves.size(), {0.0, 1.0});
    std::vector<geometry::Bounds3> bounds;
    for (const auto& curve : curves) bounds.push_back(ProfileCurveBounds(curve.segment));
    for (std::size_t a = 0; a < curves.size(); ++a) {
        for (std::size_t b = a + 1; b < curves.size(); ++b) {
            if (!ProfileBoundsTouch(bounds[a], bounds[b], eps)) continue;
            EndpointCuts(curves[a].segment, curves[b].segment, cuts[a], eps);
            EndpointCuts(curves[b].segment, curves[a].segment, cuts[b], eps);
            for (const auto& hit : geometry::IntersectCurves(
                     curves[a].segment, curves[b].segment, tolerance)) {
                cuts[a].push_back(hit.firstParameter);
                cuts[b].push_back(hit.secondParameter);
            }
        }
    }
    std::vector<SnapCurve> pieces;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        auto& values = cuts[i];
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end(), [&](double a, double b) {
            return std::abs(a - b) < 1e-9;
        }), values.end());
        auto remaining = curves[i];
        double consumed = 0.0;
        for (double t : values) {
            if (t <= consumed + 1e-9 || t >= 1.0 - 1e-9) continue;
            const auto point = curves[i].segment.Evaluate(t);
            if (geometry::Distance(point, remaining.segment.StartPoint()) <= eps
                || geometry::Distance(point, remaining.segment.EndPoint()) <= eps) continue;
            const auto split = remaining.segment.Split((t - consumed) / (1.0 - consumed));
            if (!split.HasValue()) continue;
            auto piece = remaining;
            piece.segment = *split.Value().first;
            pieces.push_back(std::move(piece));
            remaining.segment = *split.Value().second;
            consumed = t;
        }
        pieces.push_back(std::move(remaining));
    }
    return pieces;
}

void AddPlane(std::vector<geometry::PlaneFit>& planes, const std::vector<Vector3>& points,
    double eps)
{
    const auto plane = geometry::FitPlane(points);
    if (!plane.valid || plane.maximumDeviationMm > eps) return;
    for (const auto& known : planes) {
        if (std::abs(geometry::Dot(known.normal, plane.normal)) > 1.0 - 1e-9
            && std::abs(geometry::Dot(plane.origin - known.origin, known.normal)) <= eps) return;
    }
    planes.push_back(plane);
}

std::vector<geometry::PlaneFit> NetworkPlanes(const std::vector<LoopEdge>& edges, double eps)
{
    std::vector<geometry::PlaneFit> planes;
    for (const auto& edge : edges) AddPlane(planes, edge.points, eps);
    for (std::size_t a = 0; a < edges.size(); ++a) {
        for (std::size_t b = a + 1; b < edges.size(); ++b) {
            if (edges[a].from != edges[b].from && edges[a].from != edges[b].to
                && edges[a].to != edges[b].from && edges[a].to != edges[b].to) continue;
            auto points = edges[a].points;
            points.insert(points.end(), edges[b].points.begin(), edges[b].points.end());
            AddPlane(planes, points, eps);
        }
    }
    return planes;
}

bool SameGeometry(const CurveSegment& a, const CurveSegment& b, double eps)
{
    for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        if (geometry::Distance(a.Evaluate(t), b.Evaluate(t)) > eps) return false;
    }
    return true;
}

struct HalfEdge { std::size_t piece, from, to; bool forward; double angle; };

std::vector<std::vector<std::size_t>> SimpleCycles(const std::vector<std::size_t>& walk,
    const std::vector<HalfEdge>& edges)
{
    std::vector<std::vector<std::size_t>> cycles;
    std::vector<std::size_t> path;
    for (auto edge : walk) {
        path.push_back(edge);
        const auto start = std::find_if(path.begin(), path.end(), [&](auto previous) {
            return edges[previous].from == edges[edge].to;
        });
        if (start != path.end()) {
            cycles.emplace_back(start, path.end());
            path.erase(start, path.end());
        }
    }
    return cycles;
}

void AppendCycles(std::vector<ProfileBoundary>& result, const std::vector<std::size_t>& walk,
    const std::vector<HalfEdge>& edges, const std::vector<SnapCurve>& curves,
    const geometry::PlanarFrame& frame, double eps)
{
    for (const auto& cycle : SimpleCycles(walk, edges)) {
        ProfileBoundary boundary;
        for (auto e : cycle) {
            const auto& source = curves[edges[e].piece];
            auto curve = source.segment;
            if (!edges[e].forward) {
                const auto reversed = geometry::ReverseCurve(curve);
                if (!reversed.HasValue()) { boundary.segments.clear(); break; }
                curve = reversed.Value();
            }
            boundary.entityIds.push_back(source.entityId);
            boundary.segmentIds.push_back(source.segmentId);
            boundary.segments.push_back(std::move(curve));
        }
        if (boundary.segments.empty()) continue;
        boundary.sampled = geometry::SampleChain(boundary.segments, 1e-4);
        geometry::RemoveClosingDuplicate(boundary.sampled, eps);
        const auto flat = geometry::ProjectToFrame(boundary.sampled, frame);
        if (geometry::SignedArea(flat) > eps * eps) result.push_back(std::move(boundary));
    }
}

std::vector<HalfEdge> PlaneEdges(const std::vector<SnapCurve>& curves,
    const std::vector<LoopEdge>& edges, const geometry::PlaneFit& plane, double eps,
    const std::vector<std::size_t>& candidates, const ProfilePlaneIndex& endpoints,
    std::vector<std::size_t>& included, std::size_t generation)
{
    const auto frame = geometry::MakeFrame(plane);
    std::vector<HalfEdge> result;
    for (const auto i : candidates) {
        const auto& curve = curves[i].segment;
        if (!geometry::CurveLiesInPlane(curve, plane.origin, plane.normal, eps)) continue;
        bool duplicate = false;
        const auto start = curve.StartPoint(), end = curve.EndPoint();
        std::size_t previous = curves.size();
        for (const auto point : endpoints.Near(start, eps)) {
            const auto j = point / 2;
            if (j == previous) continue;
            previous = j;
            if (included[j] != generation) continue;
            const auto& other = curves[j].segment;
            const auto otherStart = other.StartPoint(), otherEnd = other.EndPoint();
            if ((start - otherStart).LengthSquared() <= eps * eps
                && (end - otherEnd).LengthSquared() <= eps * eps
                && SameGeometry(curve, other, eps)) {
                duplicate = true;
                break;
            }
            if ((start - otherEnd).LengthSquared() > eps * eps
                || (end - otherStart).LengthSquared() > eps * eps) continue;
            const auto reversed = geometry::ReverseCurve(other);
            if (reversed.HasValue() && SameGeometry(curve, reversed.Value(), eps)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        included[i] = generation;
        for (bool forward : {true, false}) {
            const auto tangent = forward ? curve.Evaluate(1e-5) - curve.StartPoint()
                                         : curve.Evaluate(1.0 - 1e-5) - curve.EndPoint();
            result.push_back({i, forward ? edges[i].from : edges[i].to,
                forward ? edges[i].to : edges[i].from, forward,
                std::atan2(geometry::Dot(tangent, frame.vDirection),
                    geometry::Dot(tangent, frame.uDirection))});
        }
    }
    return result;
}

std::vector<ProfileBoundary> WalkPlane(const std::vector<SnapCurve>& curves,
    const std::vector<HalfEdge>& edges, const geometry::PlaneFit& plane, double eps)
{
    if (edges.empty()) return {};
    // Most candidate planes touch only a few of the global graph's nodes.
    // Keep the same half-edge order without allocating every unrelated node.
    std::map<std::size_t, std::vector<std::size_t>> outgoing;
    for (std::size_t i = 0; i < edges.size(); ++i) outgoing[edges[i].from].push_back(i);
    // An edge ending in a leaf cannot belong to a closed region. Remove these
    // branches before walking, rather than repeatedly constructing and sampling
    // forward/backward walks whose area must be zero.
    std::map<std::size_t, std::size_t> degree;
    std::vector<std::size_t> tips;
    for (const auto& [node, around] : outgoing) {
        degree[node] = around.size();
        if (around.size() == 1) tips.push_back(node);
    }
    std::vector<bool> alive(edges.size(), true);
    std::size_t aliveCount = edges.size();
    for (std::size_t cursor = 0; cursor < tips.size(); ++cursor) {
        const auto node = tips[cursor];
        if (degree[node] != 1) continue;
        const auto& around = outgoing[node];
        const auto found = std::find_if(around.begin(), around.end(), [&](auto edge) { return alive[edge]; });
        if (found == around.end()) continue;
        const auto edge = *found, other = edges[edge].to;
        alive[edge] = alive[edge ^ 1] = false;
        aliveCount -= 2;
        --degree[node];
        if (--degree[other] == 1) tips.push_back(other);
    }
    if (aliveCount == 0) return {};
    for (auto& [node, around] : outgoing) {
        around.erase(std::remove_if(around.begin(), around.end(), [&](auto edge) { return !alive[edge]; }), around.end());
        std::sort(around.begin(), around.end(), [&](auto a, auto b) { return edges[a].angle < edges[b].angle; });
    }
    std::vector<bool> visited(edges.size());
    std::vector<ProfileBoundary> result;
    const auto frame = geometry::MakeFrame(plane);
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (!alive[start] || visited[start]) continue;
        std::vector<std::size_t> cycle;
        std::size_t at = start;
        do {
            if (visited[at]) break;
            visited[at] = true;
            cycle.push_back(at);
            const auto& around = outgoing[edges[at].to];
            const auto back = std::find(around.begin(), around.end(), at ^ 1);
            const auto pos = static_cast<std::size_t>(back - around.begin());
            at = around[(pos + around.size() - 1) % around.size()];
        } while (at != start);
        if (at != start) continue;
        AppendCycles(result, cycle, edges, curves, frame, eps);
    }
    return result;
}
} // namespace

std::vector<SnapCurve> SplitProfileNetwork(const std::vector<SnapCurve>& curves,
    const geometry::GeometryTolerance& tolerance)
{
    return SplitNetwork(curves, tolerance);
}

std::vector<ProfileBoundary> ProfileNetworkBoundaries(const std::vector<SnapCurve>& input,
    const geometry::GeometryTolerance& tolerance)
{
    const double eps = tolerance.interactiveJoinMm;
    const auto curves = SplitNetwork(input, tolerance);
    LoopGraph graph(eps);
    std::vector<LoopEdge> edges;
    for (std::size_t i = 0; i < curves.size(); ++i)
        edges.push_back(MakeLoopEdge(i, {curves[i].segment}, graph));
    std::vector<Vector3> starts;
    std::vector<Vector3> endpoints;
    starts.reserve(curves.size());
    endpoints.reserve(curves.size()*2);
    for (const auto& curve : curves) {
        starts.push_back(curve.segment.StartPoint());
        endpoints.push_back(curve.segment.StartPoint());
        endpoints.push_back(curve.segment.EndPoint());
    }
    const ProfilePlaneIndex index(std::move(starts));
    const ProfilePlaneIndex endpointIndex(std::move(endpoints));
    std::vector<std::size_t> included(curves.size(), 0);
    std::size_t generation = 0;
    std::vector<ProfileBoundary> result;
    const auto planes = NetworkPlanes(edges, eps);
    for (const auto& plane : planes) {
        auto found = WalkPlane(curves, PlaneEdges(curves, edges, plane, eps,
            index.Candidates(plane.origin, plane.normal, eps), endpointIndex, included, ++generation), plane, eps);
        result.insert(result.end(), std::make_move_iterator(found.begin()),
            std::make_move_iterator(found.end()));
    }
    return result;
}
} // namespace kachakacha::v2::app::detail
