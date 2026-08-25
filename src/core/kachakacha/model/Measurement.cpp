#include "kachakacha/model/Measurement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace kachakacha::model {

using geometry::Cross;
using geometry::Dot;
using geometry::Vector3;

namespace {

constexpr double kPi = 3.14159265358979323846;

double PointToLineDistance(Vector3 point, Vector3 start, Vector3 end)
{
    const Vector3 line = end - start;
    const double lengthSquared = line.LengthSquared();
    if (lengthSquared <= 1.0e-18) {
        return (point - start).Length();
    }
    return Cross(point - start, line).Length() / std::sqrt(lengthSquared);
}

void FlattenBezier(
    Vector3 p0,
    Vector3 p1,
    Vector3 p2,
    Vector3 p3,
    double tolerance,
    int depth,
    std::vector<Vector3>& points)
{
    const double flatness = std::max(
        PointToLineDistance(p1, p0, p3),
        PointToLineDistance(p2, p0, p3));
    if (flatness <= tolerance || depth >= 18) {
        points.push_back(p3);
        return;
    }

    const Vector3 p01 = (p0 + p1) * 0.5;
    const Vector3 p12 = (p1 + p2) * 0.5;
    const Vector3 p23 = (p2 + p3) * 0.5;
    const Vector3 p012 = (p01 + p12) * 0.5;
    const Vector3 p123 = (p12 + p23) * 0.5;
    const Vector3 midpoint = (p012 + p123) * 0.5;
    FlattenBezier(p0, p01, p012, midpoint, tolerance, depth + 1, points);
    FlattenBezier(midpoint, p123, p23, p3, tolerance, depth + 1, points);
}

void FlattenEvaluatedWire(
    const Wire& wire,
    double startParameter,
    double endParameter,
    Vector3 start,
    Vector3 end,
    double tolerance,
    int depth,
    std::vector<Vector3>& points)
{
    const double span = endParameter - startParameter;
    const Vector3 quarter = wire.Evaluate(startParameter + span * 0.25);
    const Vector3 midpoint = wire.Evaluate(startParameter + span * 0.5);
    const Vector3 threeQuarter = wire.Evaluate(startParameter + span * 0.75);
    const double flatness = std::max({
        PointToLineDistance(quarter, start, end),
        PointToLineDistance(midpoint, start, end),
        PointToLineDistance(threeQuarter, start, end),
    });
    if (flatness <= tolerance || depth >= 18) {
        points.push_back(end);
        return;
    }
    FlattenEvaluatedWire(
        wire, startParameter, startParameter + span * 0.5,
        start, midpoint, tolerance, depth + 1, points);
    FlattenEvaluatedWire(
        wire, startParameter + span * 0.5, endParameter,
        midpoint, end, tolerance, depth + 1, points);
}

std::vector<Vector3> FlattenWire(const Wire& wire, double tolerance)
{
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Measurement tolerance must be positive.");
    }

    if (wire.Kind() == WireKind::Line || wire.Kind() == WireKind::Polyline) {
        return wire.ControlPoints();
    }
    if (wire.Kind() == WireKind::CubicBezier) {
        const auto& controls = wire.ControlPoints();
        std::vector<Vector3> points{controls[0]};
        FlattenBezier(
            controls[0], controls[1], controls[2], controls[3],
            tolerance, 0, points);
        return points;
    }
    if (wire.Kind() == WireKind::CubicBSpline) {
        std::vector<Vector3> points{wire.Start()};
        const std::size_t spans = wire.ControlPoints().size() - 3;
        for (std::size_t span = 0; span < spans; ++span) {
            const double startParameter = static_cast<double>(span) / static_cast<double>(spans);
            const double endParameter = static_cast<double>(span + 1) / static_cast<double>(spans);
            FlattenEvaluatedWire(
                wire, startParameter, endParameter,
                wire.Evaluate(startParameter), wire.Evaluate(endParameter),
                tolerance, 0, points);
        }
        return points;
    }

    const WireArcData arc = wire.ArcData();
    const double ratio = std::clamp(1.0 - tolerance / arc.radius, -1.0, 1.0);
    const double maximumStep = std::max(2.0 * std::acos(ratio), 1.0e-4);
    const int minimumSegments = wire.Kind() == WireKind::Circle ? 32 : 2;
    const int segments = std::clamp(
        static_cast<int>(std::ceil(std::abs(arc.sweepAngleRadians) / maximumStep)),
        minimumSegments,
        2048);
    std::vector<Vector3> points;
    points.reserve(static_cast<std::size_t>(segments + 1));
    for (int index = 0; index <= segments; ++index) {
        points.push_back(wire.Evaluate(static_cast<double>(index) / segments));
    }
    return points;
}

DistanceMeasurement ClosestSegmentPoints(
    Vector3 firstStart,
    Vector3 firstEnd,
    Vector3 secondStart,
    Vector3 secondEnd)
{
    constexpr double epsilon = 1.0e-18;
    const Vector3 firstDirection = firstEnd - firstStart;
    const Vector3 secondDirection = secondEnd - secondStart;
    const Vector3 offset = firstStart - secondStart;
    const double a = Dot(firstDirection, firstDirection);
    const double e = Dot(secondDirection, secondDirection);
    const double f = Dot(secondDirection, offset);
    double firstParameter = 0.0;
    double secondParameter = 0.0;

    if (a <= epsilon && e <= epsilon) {
        return {firstStart, secondStart, (firstStart - secondStart).Length()};
    }
    if (a <= epsilon) {
        secondParameter = std::clamp(f / e, 0.0, 1.0);
    } else {
        const double c = Dot(firstDirection, offset);
        if (e <= epsilon) {
            firstParameter = std::clamp(-c / a, 0.0, 1.0);
        } else {
            const double b = Dot(firstDirection, secondDirection);
            const double denominator = a * e - b * b;
            if (std::abs(denominator) > epsilon) {
                firstParameter = std::clamp((b * f - c * e) / denominator, 0.0, 1.0);
            }
            secondParameter = (b * firstParameter + f) / e;
            if (secondParameter < 0.0) {
                secondParameter = 0.0;
                firstParameter = std::clamp(-c / a, 0.0, 1.0);
            } else if (secondParameter > 1.0) {
                secondParameter = 1.0;
                firstParameter = std::clamp((b - c) / a, 0.0, 1.0);
            }
        }
    }

    const Vector3 firstPoint = firstStart + firstDirection * firstParameter;
    const Vector3 secondPoint = secondStart + secondDirection * secondParameter;
    return {firstPoint, secondPoint, (firstPoint - secondPoint).Length()};
}

DistanceMeasurement ClosestPolylinePoints(
    const std::vector<Vector3>& first,
    const std::vector<Vector3>& second)
{
    DistanceMeasurement best;
    best.distanceMillimeters = std::numeric_limits<double>::infinity();
    for (std::size_t firstIndex = 1; firstIndex < first.size(); ++firstIndex) {
        for (std::size_t secondIndex = 1; secondIndex < second.size(); ++secondIndex) {
            const DistanceMeasurement candidate = ClosestSegmentPoints(
                first[firstIndex - 1], first[firstIndex],
                second[secondIndex - 1], second[secondIndex]);
            if (candidate.distanceMillimeters < best.distanceMillimeters) {
                best = candidate;
            }
        }
    }
    return best;
}

} // namespace

double MeasureWireLength(const Wire& wire, double tolerance)
{
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Measurement tolerance must be positive.");
    }
    if (wire.Kind() == WireKind::Line) {
        return (wire.End() - wire.Start()).Length();
    }
    if (wire.Kind() == WireKind::Circle || wire.Kind() == WireKind::CircularArc) {
        const WireArcData arc = wire.ArcData();
        return arc.radius * std::abs(arc.sweepAngleRadians);
    }

    const std::vector<Vector3> points = FlattenWire(wire, tolerance);
    double length = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        length += (points[index] - points[index - 1]).Length();
    }
    return length;
}

std::optional<double> MeasureWireRadius(const Wire& wire)
{
    if (wire.Kind() != WireKind::Circle && wire.Kind() != WireKind::CircularArc) {
        return std::nullopt;
    }
    return wire.ArcData().radius;
}

Vector3 MeasureWireTangent(const Wire& wire, double parameter)
{
    if (!std::isfinite(parameter)) {
        throw std::invalid_argument("Measurement parameter must be finite.");
    }
    const double t = std::clamp(parameter, 0.0, 1.0);
    if (wire.Kind() == WireKind::Line) {
        return (wire.End() - wire.Start()).Normalized();
    }
    if (wire.Kind() == WireKind::Polyline) {
        const auto& points = wire.ControlPoints();
        const double scaled = t * static_cast<double>(points.size() - 1);
        const std::size_t segment = static_cast<std::size_t>(std::min(
            scaled,
            static_cast<double>(points.size() - 2)));
        for (std::size_t offset = 0; offset < points.size() - 1; ++offset) {
            if (segment >= offset) {
                const Vector3 before = points[segment - offset + 1] - points[segment - offset];
                if (before.LengthSquared() > 1.0e-18) {
                    return before.Normalized();
                }
            }
            const std::size_t afterSegment = segment + offset + 1;
            if (afterSegment < points.size() - 1) {
                const Vector3 after = points[afterSegment + 1] - points[afterSegment];
                if (after.LengthSquared() > 1.0e-18) {
                    return after.Normalized();
                }
            }
        }
        throw std::invalid_argument("Polyline wire has no measurable direction.");
    }
    if (wire.Kind() == WireKind::CubicBezier) {
        const auto& points = wire.ControlPoints();
        const double oneMinusT = 1.0 - t;
        Vector3 tangent = (points[1] - points[0]) * (3.0 * oneMinusT * oneMinusT)
            + (points[2] - points[1]) * (6.0 * oneMinusT * t)
            + (points[3] - points[2]) * (3.0 * t * t);
        if (tangent.LengthSquared() <= 1.0e-18) {
            const double before = std::max(0.0, t - 1.0e-5);
            const double after = std::min(1.0, t + 1.0e-5);
            tangent = wire.Evaluate(after) - wire.Evaluate(before);
        }
        return tangent.Normalized();
    }
    if (wire.Kind() == WireKind::CubicBSpline) {
        const double before = std::max(0.0, t - 1.0e-5);
        const double after = std::min(1.0, t + 1.0e-5);
        return (wire.Evaluate(after) - wire.Evaluate(before)).Normalized();
    }

    const WireArcData arc = wire.ArcData();
    const double angle = arc.startAngleRadians + arc.sweepAngleRadians * t;
    const double orientation = arc.sweepAngleRadians >= 0.0 ? 1.0 : -1.0;
    return (arc.uAxis * (-std::sin(angle) * orientation)
        + arc.vAxis * (std::cos(angle) * orientation)).Normalized();
}

std::optional<Vector3> MeasureWireCurvatureNormal(const Wire& wire, double parameter)
{
    if (!std::isfinite(parameter)) {
        throw std::invalid_argument("Measurement parameter must be finite.");
    }
    const double t = std::clamp(parameter, 0.0, 1.0);
    if (wire.Kind() == WireKind::Line || wire.Kind() == WireKind::Polyline) {
        return std::nullopt;
    }
    if (wire.Kind() == WireKind::Circle || wire.Kind() == WireKind::CircularArc) {
        const Vector3 inward = wire.ArcData().center - wire.Evaluate(t);
        return inward.LengthSquared() > 1.0e-18
            ? std::optional<Vector3>(inward.Normalized())
            : std::nullopt;
    }

    Vector3 secondDerivative;
    if (wire.Kind() == WireKind::CubicBezier) {
        const auto& points = wire.ControlPoints();
        secondDerivative = (points[2] - points[1] * 2.0 + points[0]) * (6.0 * (1.0 - t))
            + (points[3] - points[2] * 2.0 + points[1]) * (6.0 * t);
    } else {
        constexpr double step = 1.0e-4;
        if (t <= step) {
            secondDerivative = (wire.Evaluate(step * 2.0)
                - wire.Evaluate(step) * 2.0 + wire.Evaluate(0.0)) / (step * step);
        } else if (t >= 1.0 - step) {
            secondDerivative = (wire.Evaluate(1.0)
                - wire.Evaluate(1.0 - step) * 2.0
                + wire.Evaluate(1.0 - step * 2.0)) / (step * step);
        } else {
            secondDerivative = (wire.Evaluate(t + step)
                - wire.Evaluate(t) * 2.0 + wire.Evaluate(t - step)) / (step * step);
        }
    }
    const Vector3 tangent = MeasureWireTangent(wire, t);
    const Vector3 normal = secondDerivative - tangent * Dot(secondDerivative, tangent);
    if (normal.LengthSquared() <= 1.0e-16) {
        return std::nullopt;
    }
    return normal.Normalized();
}

AngleMeasurement MeasureDirectionsAngle(Vector3 firstDirection, Vector3 secondDirection)
{
    const double cosine = std::clamp(
        Dot(firstDirection.Normalized(), secondDirection.Normalized()),
        -1.0,
        1.0);
    const double directed = std::acos(cosine) * 180.0 / kPi;
    return {directed, std::min(directed, 180.0 - directed)};
}

AngleMeasurement MeasureThreePointAngle(
    Vector3 vertex,
    Vector3 firstPoint,
    Vector3 secondPoint)
{
    const Vector3 firstDirection = firstPoint - vertex;
    const Vector3 secondDirection = secondPoint - vertex;
    if (firstDirection.LengthSquared() <= 1.0e-18
        || secondDirection.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Three-point angle requires points away from its vertex.");
    }
    return MeasureDirectionsAngle(firstDirection, secondDirection);
}

DistanceMeasurement MeasurePointToWireDistance(Vector3 point, const Wire& wire, double tolerance)
{
    if (!point.IsFinite()) {
        throw std::invalid_argument("Measurement point must be finite.");
    }
    const std::vector<Vector3> flattened = FlattenWire(wire, tolerance);
    return ClosestPolylinePoints({point, point}, flattened);
}

DistanceMeasurement MeasureWireToWireDistance(const Wire& first, const Wire& second, double tolerance)
{
    return ClosestPolylinePoints(FlattenWire(first, tolerance), FlattenWire(second, tolerance));
}

double MeasureDirectionToPlaneAngleDegrees(Vector3 direction, const WorkPlane& plane)
{
    const double normalComponent = std::clamp(
        std::abs(Dot(direction.Normalized(), plane.Normal())),
        0.0,
        1.0);
    return std::asin(normalComponent) * 180.0 / kPi;
}

double MeasurePlaneToPlaneAngleDegrees(const WorkPlane& first, const WorkPlane& second)
{
    const double cosine = std::clamp(std::abs(Dot(first.Normal(), second.Normal())), 0.0, 1.0);
    return std::acos(cosine) * 180.0 / kPi;
}

double MeasureSignedPointToPlaneDistance(Vector3 point, const WorkPlane& plane)
{
    return Dot(point - plane.Origin(), plane.Normal());
}

} // namespace kachakacha::model
