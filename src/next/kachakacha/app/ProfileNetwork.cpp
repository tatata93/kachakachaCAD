#include "kachakacha/app/ProfileNetwork.h"
#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kachakacha::v2::app::detail {
namespace {
using geometry::Vector3;
using geometry::CurveSegment;
using modeling::SnapCurve;

bool TouchBounds(const geometry::Bounds3& a, const geometry::Bounds3& b, double eps)
{
    return a.minimum.x <= b.maximum.x + eps && b.minimum.x <= a.maximum.x + eps
        && a.minimum.y <= b.maximum.y + eps && b.minimum.y <= a.maximum.y + eps
        && a.minimum.z <= b.maximum.z + eps && b.minimum.z <= a.maximum.z + eps;
}

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
    for (const auto& curve : curves) bounds.push_back(curve.segment.Bounds(eps * 0.1));
    for (std::size_t a = 0; a < curves.size(); ++a) {
        for (std::size_t b = a + 1; b < curves.size(); ++b) {
            if (!TouchBounds(bounds[a], bounds[b], eps)) continue;
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

std::vector<HalfEdge> PlaneEdges(const std::vector<SnapCurve>& curves,
    const std::vector<LoopEdge>& edges, const geometry::PlaneFit& plane, double eps)
{
    const auto frame = geometry::MakeFrame(plane);
    std::vector<HalfEdge> result;
    std::vector<std::size_t> included;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const auto& curve = curves[i].segment;
        if (!geometry::CurveLiesInPlane(curve, plane.origin, plane.normal, eps)) continue;
        bool duplicate = false;
        for (auto j : included) {
            const auto reversed = geometry::ReverseCurve(curves[j].segment);
            if (SameGeometry(curve, curves[j].segment, eps)
                || (reversed.HasValue() && SameGeometry(curve, reversed.Value(), eps))) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        included.push_back(i);
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
    const std::vector<HalfEdge>& edges, std::size_t nodes,
    const geometry::PlaneFit& plane, double eps)
{
    std::vector<std::vector<std::size_t>> outgoing(nodes);
    for (std::size_t i = 0; i < edges.size(); ++i) outgoing[edges[i].from].push_back(i);
    for (auto& around : outgoing) std::sort(around.begin(), around.end(), [&](auto a, auto b) {
        return edges[a].angle < edges[b].angle;
    });
    std::vector<bool> visited(edges.size());
    std::vector<ProfileBoundary> result;
    const auto frame = geometry::MakeFrame(plane);
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (visited[start]) continue;
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
        ProfileBoundary boundary;
        // Bridges appear in both directions on the same face. They bound no area.
        for (auto e : cycle) {
            if (std::find(cycle.begin(), cycle.end(), e ^ 1) != cycle.end()) continue;
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
    std::vector<ProfileBoundary> result;
    for (const auto& plane : NetworkPlanes(edges, eps)) {
        auto found = WalkPlane(curves, PlaneEdges(curves, edges, plane, eps),
            graph.NodeCount(), plane, eps);
        result.insert(result.end(), std::make_move_iterator(found.begin()),
            std::make_move_iterator(found.end()));
    }
    return result;
}
} // namespace kachakacha::v2::app::detail
