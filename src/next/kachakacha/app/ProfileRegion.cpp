#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/app/ProfileNetwork.h"

#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/geometry/CurveIntersection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace kachakacha::v2::app {
namespace {

using geometry::ChainInput;
using geometry::Distance;
using geometry::Dot;
using geometry::Vector3;
using modeling::SnapCurve;

bool SameEntity(const std::vector<base::EntityId>& ids, const base::EntityId& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool EndpointsTouch(const SnapCurve& first, const SnapCurve& second, double toleranceMm)
{
    const Vector3 a0 = first.segment.StartPoint();
    const Vector3 a1 = first.segment.EndPoint();
    const Vector3 b0 = second.segment.StartPoint();
    const Vector3 b1 = second.segment.EndPoint();
    return Distance(a0, b0) <= toleranceMm || Distance(a0, b1) <= toleranceMm
        || Distance(a1, b0) <= toleranceMm || Distance(a1, b1) <= toleranceMm;
}

std::size_t Root(std::vector<std::size_t>& parents, std::size_t at)
{
    while (parents[at] != at) {
        parents[at] = parents[parents[at]];
        at = parents[at];
    }
    return at;
}

void Join(std::vector<std::size_t>& parents, std::size_t first, std::size_t second)
{
    first = Root(parents, first);
    second = Root(parents, second);
    if (first != second) {
        parents[second] = first;
    }
}

std::vector<std::vector<SnapCurve>> ConnectedComponents(std::vector<SnapCurve> curves,
    double toleranceMm)
{
    std::vector<std::size_t> parents(curves.size());
    std::iota(parents.begin(), parents.end(), 0);
    for (std::size_t first = 0; first < curves.size(); ++first) {
        for (std::size_t second = first + 1; second < curves.size(); ++second) {
            if (EndpointsTouch(curves[first], curves[second], toleranceMm)) {
                Join(parents, first, second);
            }
        }
    }
    std::vector<std::vector<SnapCurve>> components;
    std::vector<std::size_t> roots;
    for (std::size_t index = 0; index < curves.size(); ++index) {
        const std::size_t root = Root(parents, index);
        const auto found = std::find(roots.begin(), roots.end(), root);
        if (found == roots.end()) {
            roots.push_back(root);
            components.push_back({});
            components.back().push_back(std::move(curves[index]));
        } else {
            components[static_cast<std::size_t>(found - roots.begin())].push_back(
                std::move(curves[index]));
        }
    }
    return components;
}

std::optional<ProfileBoundary> MakeBoundary(const std::vector<SnapCurve>& component,
    const geometry::GeometryTolerance& tolerance)
{
    std::vector<ChainInput> inputs;
    inputs.reserve(component.size());
    for (const SnapCurve& curve : component) {
        inputs.push_back({curve.entityId, curve.segmentId, curve.segment});
    }
    const auto analyzed = geometry::AnalyzeChain(inputs, tolerance);
    if (!analyzed.HasValue() || !analyzed.Value().order.closed) {
        return std::nullopt;
    }
    ProfileBoundary boundary;
    for (const geometry::OrientedSegment& ordered : analyzed.Value().order.segments) {
        const auto found = std::find_if(component.begin(), component.end(), [&](const auto& item) {
            return item.entityId == ordered.entityId && item.segmentId == ordered.segmentId;
        });
        if (found == component.end()) {
            return std::nullopt;
        }
        geometry::CurveSegment segment = found->segment;
        if (ordered.reversed) {
            const auto reversed = geometry::ReverseCurve(segment);
            if (!reversed.HasValue()) {
                return std::nullopt;
            }
            segment = reversed.Value();
        }
        boundary.entityIds.push_back(found->entityId);
        boundary.segmentIds.push_back(found->segmentId);
        boundary.segments.push_back(std::move(segment));
    }
    const double sampling = std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
    boundary.sampled = geometry::SampleChain(boundary.segments, sampling);
    geometry::RemoveClosingDuplicate(boundary.sampled, tolerance.interactiveJoinMm);
    return boundary.sampled.size() >= 3 ? std::optional<ProfileBoundary>(std::move(boundary))
                                        : std::nullopt;
}

struct FlatLoop {
    ProfileBoundary boundary;
    geometry::PlaneFit plane;
    geometry::PlanarFrame frame;
    double area = 0.0;
    std::optional<std::size_t> parent;
    int depth = 0;
};

std::optional<FlatLoop> FlattenBoundary(ProfileBoundary boundary,
    const geometry::GeometryTolerance& tolerance)
{
    const geometry::PlaneFit plane = geometry::FitPlane(boundary.sampled);
    if (!plane.valid || plane.maximumDeviationMm > tolerance.interactiveJoinMm) {
        return std::nullopt;
    }
    const geometry::PlanarFrame frame = geometry::MakeFrame(plane);
    boundary.planar = geometry::ProjectToFrame(boundary.sampled, frame);
    const double area = std::abs(geometry::SignedArea(boundary.planar));
    if (area <= tolerance.modelLinearMm * tolerance.modelLinearMm) {
        return std::nullopt;
    }
    return FlatLoop{std::move(boundary), plane, frame, area};
}

bool Coplanar(const FlatLoop& first, const FlatLoop& second, double toleranceMm,
    double angularTolerance)
{
    const double parallel = std::abs(Dot(first.plane.normal, second.plane.normal));
    const double offset = std::abs(Dot(second.plane.origin - first.plane.origin,
        first.plane.normal));
    return 1.0 - parallel <= angularTolerance && offset <= toleranceMm;
}

geometry::Point2 ProjectPoint(const Vector3& point, const geometry::PlanarFrame& frame)
{
    const Vector3 local = point - frame.origin;
    return {Dot(local, frame.uDirection), Dot(local, frame.vDirection)};
}

bool InteractsWithOtherWires(const std::vector<SnapCurve>& component,
    const std::vector<SnapCurve>& curves, const geometry::GeometryTolerance& tolerance)
{
    for (const auto& own : component) {
        for (const auto& other : curves) {
            if (own.entityId == other.entityId) continue;
            // Coincident copies remain independently selectable, as before.
            const bool same = std::any_of(component.begin(), component.end(), [&](const auto& c) {
                for (double t : {0.0, 0.25, 0.5, 0.75, 1.0})
                    if (c.segment.ClosestPoint(other.segment.Evaluate(t)).distance
                        > tolerance.modelLinearMm) return false;
                return true;
            });
            if (same) continue;
            if (EndpointsTouch(own, other, tolerance.interactiveJoinMm)) return true;
            const auto hits = geometry::IntersectCurves(own.segment, other.segment, tolerance);
            // A single endpoint contact doesn't divide a closed wire.
            for (const auto& hit : hits) {
                if (hit.firstParameter > 1e-8 && hit.firstParameter < 1.0 - 1e-8)
                    return true;
            }
            for (auto point : {other.segment.StartPoint(), other.segment.EndPoint()}) {
                const auto closest = own.segment.ClosestPoint(point);
                if (closest.distance <= tolerance.interactiveJoinMm
                    && closest.parameter > 1e-8 && closest.parameter < 1.0 - 1e-8) return true;
            }
        }
    }
    return false;
}

std::vector<FlatLoop> ClosedLoops(const modeling::SnapScene& scene,
    const std::vector<base::EntityId>* entityIds,
    const geometry::GeometryTolerance& tolerance)
{
    std::vector<SnapCurve> curves;
    for (const SnapCurve& curve : scene.curves) {
        if (curve.construction || (entityIds != nullptr
                && !SameEntity(*entityIds, curve.entityId))) {
            continue;
        }
        curves.push_back(curve);
    }
    std::vector<FlatLoop> loops;
    std::vector<SnapCurve> remaining;
    std::vector<base::EntityId> entities;
    for (const auto& curve : curves) {
        if (!SameEntity(entities, curve.entityId)) {
            entities.push_back(curve.entityId);
        }
    }
    // 1つのWire内で閉じている輪は先に独立させる。別Wireの同じ端点へ
    // 触れていても、それだけで分岐した1本の鎖へ潰してはいけない。
    for (const auto& entity : entities) {
        std::vector<SnapCurve> owned;
        for (const auto& curve : curves) {
            if (curve.entityId == entity) {
                owned.push_back(curve);
            }
        }
        for (auto& component : ConnectedComponents(std::move(owned),
                 tolerance.interactiveJoinMm)) {
            auto boundary = MakeBoundary(component, tolerance);
            if (boundary.has_value() && !InteractsWithOtherWires(component, curves, tolerance)) {
                auto loop = FlattenBoundary(std::move(*boundary), tolerance);
                if (loop.has_value()) {
                    loops.push_back(std::move(*loop));
                    continue;
                }
            }
            remaining.insert(remaining.end(), component.begin(), component.end());
        }
    }
    // T/X接続を一時的に区切り、各平面の最小区画をたどる。
    for (auto& boundary : detail::ProfileNetworkBoundaries(remaining, tolerance)) {
        auto loop = FlattenBoundary(std::move(boundary), tolerance);
        if (loop.has_value()) {
            loops.push_back(std::move(*loop));
        }
    }
    return loops;
}

void ResolveNesting(std::vector<FlatLoop>& loops,
    const geometry::GeometryTolerance& tolerance)
{
    for (std::size_t child = 0; child < loops.size(); ++child) {
        double parentArea = std::numeric_limits<double>::max();
        for (std::size_t outer = 0; outer < loops.size(); ++outer) {
            if (child == outer || loops[outer].area <= loops[child].area
                || !Coplanar(loops[outer], loops[child], tolerance.interactiveJoinMm,
                    tolerance.modelAngularRad * 10.0)) {
                continue;
            }
            const bool inside = std::all_of(loops[child].boundary.sampled.begin(),
                loops[child].boundary.sampled.end(), [&](const auto& point) {
                    return geometry::ContainsPoint(loops[outer].boundary.planar,
                        ProjectPoint(point, loops[outer].frame));
                });
            if (inside
                && loops[outer].area < parentArea) {
                loops[child].parent = outer;
                parentArea = loops[outer].area;
            }
        }
    }
    for (FlatLoop& loop : loops) {
        std::optional<std::size_t> parent = loop.parent;
        for (std::size_t guard = 0; parent.has_value() && guard < loops.size(); ++guard) {
            ++loop.depth;
            parent = loops[*parent].parent;
        }
    }
}

std::vector<ProfileRegion> RegionsFromLoops(std::vector<FlatLoop> loops,
    const geometry::GeometryTolerance& tolerance)
{
    ResolveNesting(loops, tolerance);
    std::vector<ProfileRegion> regions;
    for (std::size_t outer = 0; outer < loops.size(); ++outer) {
        if ((loops[outer].depth % 2) != 0) {
            continue;
        }
        ProfileRegion region;
        region.outer = loops[outer].boundary;
        region.plane = loops[outer].plane;
        region.frame = loops[outer].frame;
        region.areaMm2 = loops[outer].area;
        for (std::size_t hole = 0; hole < loops.size(); ++hole) {
            if (loops[hole].parent == outer && (loops[hole].depth % 2) == 1) {
                ProfileBoundary boundary = loops[hole].boundary;
                // Contains判定は外周と同じ座標系で行う。各輪のFitPlaneから作った
                // u/v軸は符号も向きも独立なので、そのまま比較してはいけない。
                boundary.planar = geometry::ProjectToFrame(boundary.sampled, region.frame);
                region.holes.push_back(std::move(boundary));
                region.areaMm2 -= loops[hole].area;
            }
        }
        regions.push_back(std::move(region));
    }
    std::sort(regions.begin(), regions.end(), [](const auto& first, const auto& second) {
        return first.areaMm2 < second.areaMm2;
    });
    return regions;
}

} // namespace

std::vector<ProfileRegion> DetectProfileRegions(const modeling::SnapScene& scene,
    const geometry::GeometryTolerance& tolerance)
{
    return RegionsFromLoops(ClosedLoops(scene, nullptr, tolerance), tolerance);
}

std::vector<ProfileRegion> DetectProfileRegions(const modeling::SnapScene& scene,
    const std::vector<base::EntityId>& entityIds,
    const geometry::GeometryTolerance& tolerance)
{
    return RegionsFromLoops(ClosedLoops(scene, &entityIds, tolerance), tolerance);
}

bool ProfileRegionContains(const ProfileRegion& region, const Vector3& point,
    double planeToleranceMm)
{
    if (std::abs(Dot(point - region.plane.origin, region.plane.normal)) > planeToleranceMm) {
        return false;
    }
    const geometry::Point2 local = ProjectPoint(point, region.frame);
    if (!geometry::ContainsPoint(region.outer.planar, local)) {
        return false;
    }
    return std::none_of(region.holes.begin(), region.holes.end(), [&](const auto& hole) {
        return geometry::ContainsPoint(hole.planar, local);
    });
}

std::vector<base::EntityId> ProfileRegionEntityIds(const ProfileRegion& region)
{
    std::vector<base::EntityId> ids;
    const auto append = [&](const ProfileBoundary& boundary) {
        for (const base::EntityId& id : boundary.entityIds) {
            if (!SameEntity(ids, id)) {
                ids.push_back(id);
            }
        }
    };
    append(region.outer);
    for (const ProfileBoundary& hole : region.holes) {
        append(hole);
    }
    return ids;
}

std::vector<std::vector<std::size_t>> GroupProfileRegionsByPlane(
    const std::vector<ProfileRegion>& regions, double limitMm)
{
    std::vector<std::vector<std::size_t>> groups;
    const auto onPlane = [limitMm](const ProfileRegion& owner, const ProfileRegion& other) {
        if (!owner.plane.valid || !other.plane.valid) {
            return false;
        }
        // 向き: 法線どうしがほぼ平行(裏表は問わない)。
        if (1.0 - std::abs(Dot(owner.plane.normal, other.plane.normal)) > 1.0e-9) {
            return false;
        }
        return std::all_of(other.outer.sampled.begin(), other.outer.sampled.end(),
            [&](const Vector3& point) {
                return std::abs(Dot(point - owner.plane.origin, owner.plane.normal)) <= limitMm;
            });
    };
    for (std::size_t index = 0; index < regions.size(); ++index) {
        bool placed = false;
        for (auto& group : groups) {
            if (onPlane(regions[group.front()], regions[index])) {
                group.push_back(index);
                placed = true;
                break;
            }
        }
        if (!placed) {
            groups.push_back({index});
        }
    }
    return groups;
}

Vector3 ProfileRegionInterior(const ProfileRegion& region)
{
    const auto& ring = region.outer.sampled;
    const double sign = geometry::SignedArea(region.outer.planar) >= 0 ? 1.0 : -1.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto a = ring[i], b = ring[(i + 1) % ring.size()];
        const auto inward = geometry::Normalized(geometry::Cross(region.plane.normal, b - a)) * sign;
        for (double fraction : {0.01, 0.001, 0.0001}) {
            const auto point = (a + b) * 0.5 + inward * (geometry::Distance(a, b) * fraction);
            if (ProfileRegionContains(region, point, 1e-6)) return point;
        }
    }
    return geometry::Centroid(ring);
}

void FilterProfileRegions(std::vector<ProfileRegion>& regions,
    const std::vector<Vector3>& seeds, double toleranceMm)
{
    if (seeds.empty()) return;
    regions.erase(std::remove_if(regions.begin(), regions.end(), [&](const auto& region) {
        return std::none_of(seeds.begin(), seeds.end(), [&](const auto& point) {
            return ProfileRegionContains(region, point, toleranceMm);
        });
    }), regions.end());
}

} // namespace kachakacha::v2::app
