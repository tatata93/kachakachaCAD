#pragma once

#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/geometry/CurveProjection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <tuple>
#include <vector>

namespace kachakacha::v2::app::detail {

struct PlanarBoundaryAlignment {
    std::vector<geometry::CurveSegment> curves;
    std::vector<bool> anchoredArcs;
    double maximumMovementMm = 0;
    bool closesAllEnds = false;
};

inline base::Result<geometry::CurveSegment> BoundaryCurveOnPlane(const geometry::CurveSegment& curve,
    const geometry::ProjectionPlane& plane, const geometry::GeometryTolerance& tolerance)
{
    using K = geometry::CurveKind;
    if (curve.Kind() == K::Line) return geometry::ProjectCurveOntoPlane(curve, plane, tolerance);
    const auto center = geometry::ProjectPointOntoPlane(curve.Center(), plane);
    const auto normal = geometry::Dot(curve.Normal(), plane.normal) < 0 ? plane.normal * -1 : plane.normal;
    if (curve.Kind() == K::Circle)
        return geometry::CurveSegment::MakeCircle(center, normal, curve.ReferenceDirection(), curve.Radius());
    return geometry::CurveSegment::MakeCircularArc(center, normal, curve.ReferenceDirection(),
        curve.Radius(), curve.StartAngleRad(), curve.SweepAngleRad());
}

inline double CurveMovementBound(const geometry::CurveSegment& source, const geometry::CurveSegment& prepared)
{
    using geometry::Distance;
    if (source.Kind() == geometry::CurveKind::Line)
        return std::max(Distance(source.StartPoint(), prepared.StartPoint()), Distance(source.EndPoint(), prepared.EndPoint()));
    const auto oldX = source.ReferenceDirection();
    const auto newX = prepared.ReferenceDirection();
    const auto oldY = geometry::Cross(source.Normal(), oldX);
    const auto newY = geometry::Cross(prepared.Normal(), newX);
    // The radius and angles are unchanged. Cauchy's inequality bounds movement over the entire arc.
    return Distance(source.Center(), prepared.Center()) + source.Radius() * std::hypot(Distance(oldX, newX), Distance(oldY, newY));
}

inline std::optional<PlanarBoundaryAlignment> ProjectPlanarBoundaryEnds(
    const std::vector<const geometry::CurveSegment*>& curves, const geometry::GeometryTolerance& tolerance)
{
    using K = geometry::CurveKind;
    const geometry::CurveSegment* reference = nullptr;
    const auto rank = [](const geometry::CurveSegment& curve) {
        const auto center = curve.Center();
        const auto normal = curve.Normal();
        return std::tuple{curve.Radius(), center.x, center.y, center.z, normal.x, normal.y, normal.z};
    };
    for (const auto* curve : curves) {
        if (curve->Kind() == K::Line) continue;
        if (curve->Kind() != K::CircularArc && curve->Kind() != K::Circle) return std::nullopt;
        if (!reference || rank(*curve) > rank(*reference)) reference = curve;
    }
    if (!reference) return std::nullopt;

    const geometry::ProjectionPlane plane{reference->Center(), reference->Normal()};
    struct Junction { geometry::Vector3 source; unsigned uses = 1; };
    LoopGraph endpoints(tolerance.modelLinearMm);
    std::vector<Junction> junctions;
    bool closesGap = false;
    const auto link = [&](const auto& original, const auto& projected) {
        const auto node = endpoints.NodeOf(projected);
        if (node == junctions.size()) {
            junctions.push_back({original});
            return true;
        }
        auto& join = junctions[node];
        const auto gap = geometry::Distance(original, join.source);
        closesGap = closesGap || gap > tolerance.modelLinearMm;
        return ++join.uses == 2 && gap <= tolerance.interactiveJoinMm;
    };

    PlanarBoundaryAlignment alignment;
    bool validLinks = true;
    alignment.curves.reserve(curves.size());
    alignment.anchoredArcs.reserve(curves.size());
    for (const auto* curve : curves) {
        const auto result = BoundaryCurveOnPlane(*curve, plane, tolerance);
        if (!result.HasValue()) return std::nullopt;
        const auto& projected = result.Value();
        const double movement = CurveMovementBound(*curve, projected);
        if (movement > tolerance.interactiveJoinMm) return std::nullopt;
        if (!link(curve->StartPoint(), projected.StartPoint())) validLinks = false;
        if (!link(curve->EndPoint(), projected.EndPoint())) validLinks = false;
        alignment.curves.push_back(projected);
        alignment.anchoredArcs.push_back(curve->Kind() != K::Line && movement <= tolerance.modelLinearMm);
        alignment.maximumMovementMm = std::max(alignment.maximumMovementMm, movement);
    }
    alignment.closesAllEnds = validLinks && closesGap
        && std::all_of(junctions.begin(), junctions.end(), [](const auto& join) { return join.uses == 2; });
    return alignment;
}

inline std::optional<PlanarBoundaryAlignment> AlignPlanarBoundaryEnds(
    const std::vector<const geometry::CurveSegment*>& curves, const geometry::GeometryTolerance& tolerance)
{
    auto alignment = ProjectPlanarBoundaryEnds(curves, tolerance);
    if (!alignment || !alignment->closesAllEnds) return std::nullopt;
    return alignment;
}

} // namespace kachakacha::v2::app::detail
