#include "kachakacha/model/Wire.h"

#include <algorithm>
#include <stdexcept>

namespace kachakacha::model {

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;

namespace {

void RequireFinite(const std::vector<Vector3>& points)
{
    for (const Vector3& point : points) {
        if (!point.IsFinite()) {
            throw std::invalid_argument("Wire point contains a non-finite value.");
        }
    }
}

Vector3 Lerp(const Vector3& start, const Vector3& end, double t)
{
    return start * (1.0 - t) + end * t;
}

} // namespace

Wire::Wire(WireKind kind, std::vector<Vector3> controlPoints)
    : kind_(kind), controlPoints_(std::move(controlPoints))
{
}

Wire Wire::Line(Vector3 start, Vector3 end)
{
    std::vector<Vector3> points = {start, end};
    RequireFinite(points);

    if (AlmostEqual(start, end)) {
        throw std::invalid_argument("Line wire requires two different points.");
    }

    return {WireKind::Line, std::move(points)};
}

Wire Wire::Polyline(std::vector<Vector3> points)
{
    RequireFinite(points);

    if (points.size() < 2) {
        throw std::invalid_argument("Polyline wire requires at least two points.");
    }

    return {WireKind::Polyline, std::move(points)};
}

Wire Wire::CubicBezier(Vector3 start, Vector3 control1, Vector3 control2, Vector3 end)
{
    std::vector<Vector3> points = {start, control1, control2, end};
    RequireFinite(points);

    if (AlmostEqual(start, end)) {
        throw std::invalid_argument("Cubic Bezier wire requires different start and end points.");
    }

    return {WireKind::CubicBezier, std::move(points)};
}

Vector3 Wire::Evaluate(double t) const
{
    if (!std::isfinite(t)) {
        throw std::invalid_argument("Wire parameter must be finite.");
    }

    const double clamped = std::clamp(t, 0.0, 1.0);

    switch (kind_) {
    case WireKind::Line:
        return Lerp(controlPoints_[0], controlPoints_[1], clamped);

    case WireKind::Polyline: {
        const double scaled = clamped * static_cast<double>(controlPoints_.size() - 1);
        const auto segment = static_cast<std::size_t>(std::min(
            scaled,
            static_cast<double>(controlPoints_.size() - 2)));
        const double localT = scaled - static_cast<double>(segment);
        return Lerp(controlPoints_[segment], controlPoints_[segment + 1], localT);
    }

    case WireKind::CubicBezier: {
        const double oneMinusT = 1.0 - clamped;
        return controlPoints_[0] * (oneMinusT * oneMinusT * oneMinusT)
            + controlPoints_[1] * (3.0 * oneMinusT * oneMinusT * clamped)
            + controlPoints_[2] * (3.0 * oneMinusT * clamped * clamped)
            + controlPoints_[3] * (clamped * clamped * clamped);
    }
    }

    throw std::logic_error("Unknown wire kind.");
}

bool Wire::IsClosed(double epsilon) const noexcept
{
    return AlmostEqual(Start(), End(), epsilon);
}

} // namespace kachakacha::model

