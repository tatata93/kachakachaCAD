#include "kachakacha/model/WireOperations.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <stdexcept>

namespace kachakacha::model {

using geometry::Dot;
using geometry::Cross;
using geometry::Vector3;

namespace {

struct LineIntersection {
    Vector3 point;
    double firstParameter = 0.0;
    double secondParameter = 0.0;
};

struct Point2 {
    double u = 0.0;
    double v = 0.0;
};

Point2 operator+(Point2 lhs, Point2 rhs)
{
    return {lhs.u + rhs.u, lhs.v + rhs.v};
}

Point2 operator-(Point2 lhs, Point2 rhs)
{
    return {lhs.u - rhs.u, lhs.v - rhs.v};
}

Point2 operator*(Point2 point, double scale)
{
    return {point.u * scale, point.v * scale};
}

double Cross2(Point2 lhs, Point2 rhs)
{
    return lhs.u * rhs.v - lhs.v * rhs.u;
}

double Dot2(Point2 lhs, Point2 rhs)
{
    return lhs.u * rhs.u + lhs.v * rhs.v;
}

double Length2(Point2 point)
{
    return std::hypot(point.u, point.v);
}

Point2 Normalized2(Point2 point, double tolerance)
{
    const double length = Length2(point);
    if (length <= tolerance) {
        throw std::invalid_argument("Offset wire contains a zero-length segment.");
    }
    return point * (1.0 / length);
}

Point2 LeftNormal(Point2 direction)
{
    return {-direction.v, direction.u};
}

Point2 OffsetCorner(
    Point2 vertex,
    Point2 previousDirection,
    Point2 nextDirection,
    double distance,
    double tolerance)
{
    const Point2 previousNormal = LeftNormal(previousDirection);
    const Point2 nextNormal = LeftNormal(nextDirection);
    const Point2 previousLine = vertex + previousNormal * distance;
    const Point2 nextLine = vertex + nextNormal * distance;
    const double denominator = Cross2(previousDirection, nextDirection);
    if (std::abs(denominator) <= tolerance) {
        if (Dot2(previousDirection, nextDirection) < 0.0) {
            throw std::invalid_argument("Offset wire reverses direction at a vertex.");
        }
        return vertex + (previousNormal + nextNormal) * (distance * 0.5);
    }

    const double parameter = Cross2(nextLine - previousLine, nextDirection) / denominator;
    const Point2 intersection = previousLine + previousDirection * parameter;
    const double miterLength = Length2(intersection - vertex);
    if (miterLength > std::max(std::abs(distance) * 50.0, tolerance * 100.0)) {
        throw std::invalid_argument("Offset corner is too sharp for a stable miter.");
    }
    return intersection;
}

Point2 ProjectOnPlane(const WorkPlane& plane, Vector3 point, double tolerance)
{
    const auto coordinates = plane.Project(point);
    if (std::abs(coordinates.w) > tolerance) {
        throw std::invalid_argument("Offset wire must lie on the selected work plane.");
    }
    return {coordinates.u, coordinates.v};
}

Wire OffsetLineOrPolyline(
    const Wire& wire,
    const WorkPlane& plane,
    double distance,
    double tolerance)
{
    std::vector<Point2> points;
    points.reserve(wire.ControlPoints().size());
    for (const Vector3& point : wire.ControlPoints()) {
        points.push_back(ProjectOnPlane(plane, point, tolerance));
    }
    const bool closed = wire.Kind() == WireKind::Polyline
        && points.size() >= 2
        && Length2(points.front() - points.back()) <= tolerance;
    if (closed) {
        points.pop_back();
    }
    if (points.size() < 2 || (closed && points.size() < 3)) {
        throw std::invalid_argument("Offset wire does not contain enough distinct points.");
    }

    std::vector<Point2> directions;
    const std::size_t segmentCount = closed ? points.size() : points.size() - 1;
    directions.reserve(segmentCount);
    for (std::size_t index = 0; index < segmentCount; ++index) {
        directions.push_back(Normalized2(
            points[(index + 1) % points.size()] - points[index], tolerance));
    }

    std::vector<Point2> offsetPoints;
    offsetPoints.reserve(points.size() + (closed ? 1 : 0));
    if (!closed) {
        offsetPoints.push_back(points.front() + LeftNormal(directions.front()) * distance);
        for (std::size_t index = 1; index + 1 < points.size(); ++index) {
            offsetPoints.push_back(OffsetCorner(
                points[index], directions[index - 1], directions[index], distance, tolerance));
        }
        offsetPoints.push_back(points.back() + LeftNormal(directions.back()) * distance);
    } else {
        for (std::size_t index = 0; index < points.size(); ++index) {
            const std::size_t previousSegment = (index + directions.size() - 1) % directions.size();
            offsetPoints.push_back(OffsetCorner(
                points[index], directions[previousSegment], directions[index], distance, tolerance));
        }
        offsetPoints.push_back(offsetPoints.front());
    }

    std::vector<Vector3> worldPoints;
    worldPoints.reserve(offsetPoints.size());
    for (const Point2& point : offsetPoints) {
        worldPoints.push_back(plane.ToWorld(point.u, point.v));
    }
    return wire.Kind() == WireKind::Line
        ? Wire::Line(worldPoints[0], worldPoints[1])
        : Wire::Polyline(std::move(worldPoints));
}

LineIntersection IntersectInfiniteLines(const Wire& first, const Wire& second, double tolerance)
{
    if (first.Kind() != WireKind::Line || second.Kind() != WireKind::Line) {
        throw std::invalid_argument("Line intersection editing requires two line wires.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Line intersection tolerance must be positive.");
    }

    const Vector3 firstStart = first.Start();
    const Vector3 secondStart = second.Start();
    const Vector3 firstDirection = first.End() - firstStart;
    const Vector3 secondDirection = second.End() - secondStart;
    const Vector3 offset = firstStart - secondStart;
    const double a = Dot(firstDirection, firstDirection);
    const double b = Dot(firstDirection, secondDirection);
    const double c = Dot(secondDirection, secondDirection);
    const double d = Dot(firstDirection, offset);
    const double e = Dot(secondDirection, offset);
    const double denominator = a * c - b * b;
    const double scale = std::max(a * c, 1.0);
    if (std::abs(denominator) <= tolerance * tolerance * scale) {
        throw std::invalid_argument("Lines are parallel or nearly parallel.");
    }

    const double firstParameter = (b * e - c * d) / denominator;
    const double secondParameter = (a * e - b * d) / denominator;
    const Vector3 firstPoint = firstStart + firstDirection * firstParameter;
    const Vector3 secondPoint = secondStart + secondDirection * secondParameter;
    if ((firstPoint - secondPoint).Length() > tolerance) {
        throw std::invalid_argument("Lines do not intersect in 3D.");
    }

    return {(firstPoint + secondPoint) * 0.5, firstParameter, secondParameter};
}

LineIntersection IntersectSegments(const Wire& first, const Wire& second, double tolerance)
{
    const LineIntersection intersection = IntersectInfiniteLines(first, second, tolerance);
    if (intersection.firstParameter < -tolerance || intersection.firstParameter > 1.0 + tolerance
        || intersection.secondParameter < -tolerance || intersection.secondParameter > 1.0 + tolerance) {
        throw std::invalid_argument("Intersection is outside a line segment.");
    }
    return intersection;
}

RetainedLineEnd ResolveRetainedEnd(
    const Wire& line,
    Vector3 intersection,
    RetainedLineEnd requested)
{
    if (requested != RetainedLineEnd::Automatic) {
        return requested;
    }
    const double startDistance = (line.Start() - intersection).LengthSquared();
    const double endDistance = (line.End() - intersection).LengthSquared();
    return startDistance >= endDistance ? RetainedLineEnd::Start : RetainedLineEnd::End;
}

struct TrimmedLine {
    Wire wire;
    Vector3 trimPoint;
};

TrimmedLine TrimLine(
    const Wire& line,
    Vector3 intersection,
    RetainedLineEnd requestedEnd,
    double setback,
    double tolerance)
{
    if (!std::isfinite(setback) || setback <= tolerance) {
        throw std::invalid_argument("Chamfer setback must be positive.");
    }

    const RetainedLineEnd retainedEnd = ResolveRetainedEnd(line, intersection, requestedEnd);
    const Vector3 retainedPoint = retainedEnd == RetainedLineEnd::Start ? line.Start() : line.End();
    const Vector3 branch = retainedPoint - intersection;
    const double branchLength = branch.Length();
    if (branchLength <= setback + tolerance) {
        throw std::invalid_argument("Chamfer setback exceeds the retained line branch.");
    }
    const Vector3 trimPoint = intersection + branch * (setback / branchLength);
    if (retainedEnd == RetainedLineEnd::Start) {
        return {Wire::Line(line.Start(), trimPoint), trimPoint};
    }
    return {Wire::Line(trimPoint, line.End()), trimPoint};
}

Wire MeetLineAtIntersection(
    const Wire& line,
    Vector3 intersection,
    RetainedLineEnd requestedEnd,
    double tolerance)
{
    const RetainedLineEnd retainedEnd = ResolveRetainedEnd(line, intersection, requestedEnd);
    const Vector3 retainedPoint = retainedEnd == RetainedLineEnd::Start ? line.Start() : line.End();
    if ((retainedPoint - intersection).Length() <= tolerance) {
        throw std::invalid_argument("Line intersection would create a zero-length wire.");
    }
    if (retainedEnd == RetainedLineEnd::Start) {
        return Wire::Line(retainedPoint, intersection);
    }
    return Wire::Line(intersection, retainedPoint);
}

std::vector<Vector3> ChainPoints(const Wire& wire)
{
    if (wire.Kind() == WireKind::Line || wire.Kind() == WireKind::Polyline) {
        return wire.ControlPoints();
    }
    throw std::invalid_argument("Join currently supports line and polyline wires.");
}

} // namespace

Wire JoinLineChain(const std::vector<Wire>& wires, double tolerance)
{
    if (wires.size() < 2) {
        throw std::invalid_argument("Join requires at least two wires.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Join tolerance must be positive.");
    }

    std::vector<std::vector<Vector3>> sourcePoints;
    sourcePoints.reserve(wires.size());
    for (const Wire& wire : wires) {
        if (wire.IsClosed(tolerance)) {
            throw std::invalid_argument("Join requires open wires.");
        }
        sourcePoints.push_back(ChainPoints(wire));
    }

    std::vector<bool> used(wires.size(), false);
    std::vector<Vector3> joined;
    std::function<bool(std::size_t)> appendRemaining = [&](std::size_t usedCount) {
        if (usedCount == wires.size()) {
            return true;
        }
        for (std::size_t index = 0; index < sourcePoints.size(); ++index) {
            if (used[index]) {
                continue;
            }
            for (int reverse = 0; reverse < 2; ++reverse) {
                const auto& points = sourcePoints[index];
                const Vector3 candidateStart = reverse == 0 ? points.front() : points.back();
                if (!geometry::AlmostEqual(joined.back(), candidateStart, tolerance)) {
                    continue;
                }
                const std::size_t oldSize = joined.size();
                if (reverse == 0) {
                    joined.insert(joined.end(), points.begin() + 1, points.end());
                } else {
                    joined.insert(joined.end(), std::next(points.rbegin()), points.rend());
                }
                used[index] = true;
                if (appendRemaining(usedCount + 1)) {
                    return true;
                }
                used[index] = false;
                joined.resize(oldSize);
            }
        }
        return false;
    };

    for (std::size_t startIndex = 0; startIndex < sourcePoints.size(); ++startIndex) {
        for (int reverse = 0; reverse < 2; ++reverse) {
            std::fill(used.begin(), used.end(), false);
            joined = sourcePoints[startIndex];
            if (reverse != 0) {
                std::reverse(joined.begin(), joined.end());
            }
            used[startIndex] = true;
            if (appendRemaining(1)) {
                return Wire::Polyline(std::move(joined));
            }
        }
    }
    throw std::invalid_argument("Selected wires do not form one endpoint-connected chain.");
}

Wire OffsetPlanarWire(
    const Wire& wire,
    const WorkPlane& plane,
    double signedDistance,
    double tolerance)
{
    if (!std::isfinite(signedDistance) || std::abs(signedDistance) <= tolerance) {
        throw std::invalid_argument("Offset distance must be non-zero.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Offset tolerance must be positive.");
    }

    if (wire.Kind() == WireKind::Line || wire.Kind() == WireKind::Polyline) {
        return OffsetLineOrPolyline(wire, plane, signedDistance, tolerance);
    }
    if (wire.Kind() == WireKind::CubicBezier) {
        throw std::invalid_argument("Exact Bezier offset is not supported yet.");
    }

    const WireArcData arc = wire.ArcData();
    (void)ProjectOnPlane(plane, arc.center, tolerance);
    const Vector3 arcNormal = Cross(arc.uAxis, arc.vAxis).Normalized();
    const double normalAlignment = Dot(arcNormal, plane.Normal());
    if (std::abs(normalAlignment) < 1.0 - tolerance) {
        throw std::invalid_argument("Offset arc must lie on the selected work plane.");
    }
    const double traversalOrientation = (normalAlignment >= 0.0 ? 1.0 : -1.0)
        * (arc.sweepAngleRadians >= 0.0 ? 1.0 : -1.0);
    const double radius = arc.radius - signedDistance * traversalOrientation;
    if (radius <= tolerance) {
        throw std::invalid_argument("Offset distance collapses the circle or arc.");
    }
    if (wire.Kind() == WireKind::Circle) {
        return Wire::Circle(arc.center, arc.uAxis, arc.vAxis, radius);
    }
    return Wire::CircularArc(
        arc.center,
        arc.uAxis,
        arc.vAxis,
        radius,
        arc.startAngleRadians,
        arc.sweepAngleRadians);
}

Wire ApplyWireLineConstraints(
    const Wire& wire,
    const std::optional<WorkPlane>& plane,
    const WireLineConstraints& constraints,
    double tolerance)
{
    if (constraints.Empty()) {
        return wire;
    }
    if (wire.Kind() != WireKind::Line) {
        throw std::invalid_argument("Line constraints can only be applied to a line wire.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Line constraint tolerance must be positive.");
    }
    if (constraints.lengthMillimeters.has_value()
        && (!std::isfinite(*constraints.lengthMillimeters)
            || *constraints.lengthMillimeters <= tolerance)) {
        throw std::invalid_argument("Line constraint length must be positive.");
    }
    if (constraints.angleDegrees.has_value() && !std::isfinite(*constraints.angleDegrees)) {
        throw std::invalid_argument("Line constraint angle must be finite.");
    }

    const Vector3 start = wire.Start();
    const Vector3 direction = wire.End() - start;
    const double currentLength = direction.Length();
    if (currentLength <= tolerance) {
        throw std::invalid_argument("Line constraints require a non-zero line.");
    }

    if (!constraints.angleDegrees.has_value()) {
        return Wire::Line(
            start,
            start + direction * (*constraints.lengthMillimeters / currentLength));
    }
    if (!plane.has_value()) {
        throw std::invalid_argument("An angle constraint requires a source work plane.");
    }

    const PlaneCoordinates startCoordinates = plane->Project(start);
    const PlaneCoordinates endCoordinates = plane->Project(wire.End());
    const double planarLength = std::hypot(
        endCoordinates.u - startCoordinates.u,
        endCoordinates.v - startCoordinates.v);
    if (planarLength <= tolerance) {
        throw std::invalid_argument("An angle-constrained line must have planar length.");
    }

    constexpr double pi = 3.14159265358979323846;
    const double angleRadians = *constraints.angleDegrees * pi / 180.0;
    const double constrainedLength = constraints.lengthMillimeters.value_or(planarLength);
    return Wire::Line(
        plane->ToWorld(startCoordinates.u, startCoordinates.v),
        plane->ToWorld(
            startCoordinates.u + constrainedLength * std::cos(angleRadians),
            startCoordinates.v + constrainedLength * std::sin(angleRadians)));
}

Wire ApplyWireCurveConstraints(
    const Wire& wire,
    const WireCurveConstraints& constraints,
    double tolerance)
{
    if (constraints.Empty()) {
        return wire;
    }
    if (wire.Kind() != WireKind::Circle && wire.Kind() != WireKind::CircularArc) {
        throw std::invalid_argument("Radius constraints can only be applied to circles and circular arcs.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0
        || !std::isfinite(*constraints.radiusMillimeters)
        || *constraints.radiusMillimeters <= tolerance) {
        throw std::invalid_argument("Radius constraint must be positive.");
    }
    const WireArcData arc = wire.ArcData();
    if (wire.Kind() == WireKind::Circle) {
        return Wire::Circle(
            arc.center, arc.uAxis, arc.vAxis, *constraints.radiusMillimeters);
    }
    return Wire::CircularArc(
        arc.center,
        arc.uAxis,
        arc.vAxis,
        *constraints.radiusMillimeters,
        arc.startAngleRadians,
        arc.sweepAngleRadians);
}

LineIntersectionEditResult MeetLinesAtIntersection(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double tolerance)
{
    const LineIntersection intersection = IntersectInfiniteLines(first, second, tolerance);
    return {
        MeetLineAtIntersection(first, intersection.point, retainedFirst, tolerance),
        MeetLineAtIntersection(second, intersection.point, retainedSecond, tolerance),
        intersection.point,
    };
}

LineChamferResult ChamferIntersectingLines(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    double firstSetback,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double secondSetback,
    double tolerance)
{
    const LineIntersection intersection = IntersectSegments(first, second, tolerance);
    const TrimmedLine trimmedFirst = TrimLine(first, intersection.point, retainedFirst, firstSetback, tolerance);
    const TrimmedLine trimmedSecond = TrimLine(second, intersection.point, retainedSecond, secondSetback, tolerance);
    if ((trimmedFirst.trimPoint - trimmedSecond.trimPoint).Length() <= tolerance) {
        throw std::invalid_argument("Chamfer points are too close together.");
    }

    return {
        trimmedFirst.wire,
        Wire::Line(trimmedFirst.trimPoint, trimmedSecond.trimPoint),
        trimmedSecond.wire,
        intersection.point,
        trimmedFirst.trimPoint,
        trimmedSecond.trimPoint,
    };
}

LineFilletResult FilletIntersectingLines(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double radius,
    double tolerance)
{
    if (!std::isfinite(radius) || radius <= tolerance) {
        throw std::invalid_argument("Fillet radius must be positive.");
    }

    const LineIntersection intersection = IntersectSegments(first, second, tolerance);
    const RetainedLineEnd resolvedFirst = ResolveRetainedEnd(first, intersection.point, retainedFirst);
    const RetainedLineEnd resolvedSecond = ResolveRetainedEnd(second, intersection.point, retainedSecond);
    const Vector3 firstRetainedPoint = resolvedFirst == RetainedLineEnd::Start ? first.Start() : first.End();
    const Vector3 secondRetainedPoint = resolvedSecond == RetainedLineEnd::Start ? second.Start() : second.End();
    const Vector3 firstDirection = (firstRetainedPoint - intersection.point).Normalized();
    const Vector3 secondDirection = (secondRetainedPoint - intersection.point).Normalized();
    const double cosine = std::clamp(Dot(firstDirection, secondDirection), -1.0, 1.0);
    const double angle = std::acos(cosine);
    if (angle <= tolerance || std::abs(3.14159265358979323846 - angle) <= tolerance) {
        throw std::invalid_argument("Fillet branches must form a usable corner angle.");
    }

    const double tangentDistance = radius / std::tan(angle * 0.5);
    const TrimmedLine trimmedFirst = TrimLine(first, intersection.point, resolvedFirst, tangentDistance, tolerance);
    const TrimmedLine trimmedSecond = TrimLine(second, intersection.point, resolvedSecond, tangentDistance, tolerance);
    const Vector3 bisector = (firstDirection + secondDirection).Normalized();
    const Vector3 center = intersection.point + bisector * (radius / std::sin(angle * 0.5));
    const Vector3 normal = Cross(firstDirection, secondDirection).Normalized();
    const Vector3 startRadius = (trimmedFirst.trimPoint - center).Normalized();
    const Vector3 endRadius = (trimmedSecond.trimPoint - center).Normalized();
    const Vector3 arcVAxis = Cross(normal, startRadius).Normalized();
    const double sweep = std::atan2(Dot(Cross(startRadius, endRadius), normal), Dot(startRadius, endRadius));
    if (std::abs(sweep) <= tolerance) {
        throw std::invalid_argument("Fillet arc sweep is too small.");
    }

    return {
        trimmedFirst.wire,
        Wire::CircularArc(center, startRadius, arcVAxis, radius, 0.0, sweep),
        trimmedSecond.wire,
        intersection.point,
        trimmedFirst.trimPoint,
        trimmedSecond.trimPoint,
        center,
        radius,
    };
}

} // namespace kachakacha::model
