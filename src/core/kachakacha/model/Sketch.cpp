#include "kachakacha/model/Sketch.h"

#include <stdexcept>

namespace kachakacha::model {

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;

namespace {

void RequireFinite(Vector2 point)
{
    if (!point.IsFinite()) {
        throw std::invalid_argument("Sketch point contains a non-finite value.");
    }
}

} // namespace

Sketch::Sketch(WorkPlane plane)
    : plane_(plane)
{
}

Wire Sketch::MakeLine(Vector2 start, Vector2 end) const
{
    RequireFinite(start);
    RequireFinite(end);

    return Wire::Line(ToWorld(start), ToWorld(end));
}

Wire Sketch::MakeCubicBezier(Vector2 start, Vector2 control1, Vector2 control2, Vector2 end) const
{
    RequireFinite(start);
    RequireFinite(control1);
    RequireFinite(control2);
    RequireFinite(end);

    return Wire::CubicBezier(
        ToWorld(start),
        ToWorld(control1),
        ToWorld(control2),
        ToWorld(end));
}

Wire Sketch::MakeCircle(Vector2 center, double radius) const
{
    RequireFinite(center);
    return Wire::Circle(ToWorld(center), plane_.UAxis(), plane_.VAxis(), radius);
}

Wire Sketch::MakeCircularArc(Vector2 center, double radius, double startAngleRadians, double sweepAngleRadians) const
{
    RequireFinite(center);
    return Wire::CircularArc(ToWorld(center), plane_.UAxis(), plane_.VAxis(), radius, startAngleRadians, sweepAngleRadians);
}

Vector3 Sketch::ToWorld(Vector2 point) const noexcept
{
    return plane_.ToWorld(point.x, point.y);
}

} // namespace kachakacha::model
