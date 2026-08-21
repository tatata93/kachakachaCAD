#include "kachakacha/model/WorkPlane.h"

#include <cmath>
#include <stdexcept>

namespace kachakacha::model {

using kachakacha::geometry::Cross;
using kachakacha::geometry::Dot;
using kachakacha::geometry::Vector3;

namespace {

Vector3 PickFallbackUAxis(const Vector3& normal)
{
    if (std::abs(normal.x) < 0.9) {
        return {1.0, 0.0, 0.0};
    }

    return {0.0, 1.0, 0.0};
}

Vector3 MakePerpendicularUnit(const Vector3& axisHint, const Vector3& normal)
{
    Vector3 projected = axisHint - normal * Dot(axisHint, normal);
    if (projected.LengthSquared() <= 1.0e-18) {
        const Vector3 fallback = PickFallbackUAxis(normal);
        projected = fallback - normal * Dot(fallback, normal);
    }

    return projected.Normalized();
}

Vector3 RotateVector(const Vector3& value, const Vector3& axisUnit, double angleRadians)
{
    const double c = std::cos(angleRadians);
    const double s = std::sin(angleRadians);

    return value * c
        + Cross(axisUnit, value) * s
        + axisUnit * (Dot(axisUnit, value) * (1.0 - c));
}

} // namespace

WorkPlane::WorkPlane(Vector3 origin, Vector3 uAxis, Vector3 vAxis, Vector3 normal)
    : origin_(origin), uAxis_(uAxis), vAxis_(vAxis), normal_(normal)
{
}

WorkPlane WorkPlane::FromOriginAxes(Vector3 origin, Vector3 uAxisHint, Vector3 normal)
{
    if (!origin.IsFinite() || !uAxisHint.IsFinite() || !normal.IsFinite()) {
        throw std::invalid_argument("WorkPlane input contains a non-finite value.");
    }

    const Vector3 normalUnit = normal.Normalized();
    const Vector3 uAxisUnit = MakePerpendicularUnit(uAxisHint, normalUnit);
    const Vector3 vAxisUnit = Cross(normalUnit, uAxisUnit).Normalized();

    return {origin, uAxisUnit, vAxisUnit, normalUnit};
}

WorkPlane WorkPlane::FromThreePoints(Vector3 pointA, Vector3 pointB, Vector3 pointC)
{
    const Vector3 ab = pointB - pointA;
    const Vector3 ac = pointC - pointA;
    const Vector3 normal = Cross(ab, ac);

    if (normal.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Cannot create a WorkPlane from collinear points.");
    }

    return FromOriginAxes(pointA, ab, normal);
}

WorkPlane WorkPlane::FromPointNormal(Vector3 origin, Vector3 normal, Vector3 uAxisHint)
{
    return FromOriginAxes(origin, uAxisHint, normal);
}

WorkPlane WorkPlane::Offset(double distance) const
{
    return FromOriginAxes(origin_ + normal_ * distance, uAxis_, normal_);
}

WorkPlane WorkPlane::Translated(Vector3 delta) const
{
    if (!delta.IsFinite()) {
        throw std::invalid_argument("WorkPlane translation contains a non-finite value.");
    }

    return FromOriginAxes(origin_ + delta, uAxis_, normal_);
}

WorkPlane WorkPlane::RotateAroundAxis(Vector3 axisPoint, Vector3 axisDirection, double angleRadians) const
{
    if (!axisPoint.IsFinite() || !axisDirection.IsFinite() || !std::isfinite(angleRadians)) {
        throw std::invalid_argument("WorkPlane rotation input contains a non-finite value.");
    }

    const Vector3 axisUnit = axisDirection.Normalized();
    const Vector3 rotatedOrigin = axisPoint + RotateVector(origin_ - axisPoint, axisUnit, angleRadians);
    const Vector3 rotatedUAxis = RotateVector(uAxis_, axisUnit, angleRadians);
    const Vector3 rotatedNormal = RotateVector(normal_, axisUnit, angleRadians);

    return FromOriginAxes(rotatedOrigin, rotatedUAxis, rotatedNormal);
}

Vector3 WorkPlane::ToWorld(double u, double v, double w) const noexcept
{
    return origin_ + uAxis_ * u + vAxis_ * v + normal_ * w;
}

PlaneCoordinates WorkPlane::Project(Vector3 point) const noexcept
{
    const Vector3 delta = point - origin_;
    return {
        Dot(delta, uAxis_),
        Dot(delta, vAxis_),
        Dot(delta, normal_),
    };
}

} // namespace kachakacha::model
