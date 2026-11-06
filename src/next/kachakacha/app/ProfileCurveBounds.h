#pragma once

#include "kachakacha/geometry/CurveSegment.h"

namespace kachakacha::v2::app::detail {

// Broad-phase rejection must enclose the entire curve, including extrema between
// display samples. Polynomial Bezier/B-spline curves stay in their control hull;
// a complete circle's sphere also conservatively encloses any circular arc.
[[nodiscard]] inline geometry::Bounds3 ProfileCurveBounds(const geometry::CurveSegment& curve)
{
    geometry::Bounds3 result;
    if (curve.Kind() == geometry::CurveKind::CircularArc
        || curve.Kind() == geometry::CurveKind::Circle) {
        const double r = curve.Radius();
        result.Add(curve.Center() - geometry::Vector3{r, r, r});
        result.Add(curve.Center() + geometry::Vector3{r, r, r});
    } else {
        for (const auto& point : curve.ControlPoints()) result.Add(point);
    }
    return result;
}

[[nodiscard]] inline bool ProfileBoundsTouch(const geometry::Bounds3& a,
    const geometry::Bounds3& b, double tolerance)
{
    if (a.empty || b.empty) return true;
    return a.minimum.x <= b.maximum.x + tolerance && b.minimum.x <= a.maximum.x + tolerance
        && a.minimum.y <= b.maximum.y + tolerance && b.minimum.y <= a.maximum.y + tolerance
        && a.minimum.z <= b.maximum.z + tolerance && b.minimum.z <= a.maximum.z + tolerance;
}

} // namespace kachakacha::v2::app::detail
