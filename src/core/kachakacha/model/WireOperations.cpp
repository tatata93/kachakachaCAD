#include "kachakacha/model/WireOperations.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <numbers>
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

struct ParameterPoint {
    Vector3 point;
    double parameter = 0.0;
};

struct WireIntersection {
    Vector3 point;
    double firstParameter = 0.0;
    double secondParameter = 0.0;
};

double PointToSegmentDistance(Vector3 point, Vector3 start, Vector3 end)
{
    const Vector3 segment = end - start;
    const double lengthSquared = segment.LengthSquared();
    if (lengthSquared <= 1.0e-18) {
        return (point - start).Length();
    }
    const double parameter = std::clamp(
        Dot(point - start, segment) / lengthSquared, 0.0, 1.0);
    return (point - (start + segment * parameter)).Length();
}

void FlattenWireRange(
    const Wire& wire,
    double startParameter,
    double endParameter,
    Vector3 start,
    Vector3 end,
    double tolerance,
    int depth,
    std::vector<ParameterPoint>& points)
{
    const double span = endParameter - startParameter;
    const std::array<double, 3> fractions = {0.25, 0.5, 0.75};
    double flatness = 0.0;
    for (double fraction : fractions) {
        flatness = std::max(flatness, PointToSegmentDistance(
            wire.Evaluate(startParameter + span * fraction), start, end));
    }
    if (flatness <= tolerance || depth >= 18) {
        points.push_back({end, endParameter});
        return;
    }
    const double middleParameter = (startParameter + endParameter) * 0.5;
    const Vector3 middle = wire.Evaluate(middleParameter);
    FlattenWireRange(
        wire, startParameter, middleParameter, start, middle,
        tolerance, depth + 1, points);
    FlattenWireRange(
        wire, middleParameter, endParameter, middle, end,
        tolerance, depth + 1, points);
}

std::vector<ParameterPoint> FlattenWireWithParameters(const Wire& wire, double tolerance)
{
    if (wire.Kind() == WireKind::Line) {
        return {{wire.Start(), 0.0}, {wire.End(), 1.0}};
    }
    if (wire.Kind() == WireKind::Polyline) {
        const auto& controls = wire.ControlPoints();
        std::vector<ParameterPoint> points;
        points.reserve(controls.size());
        for (std::size_t index = 0; index < controls.size(); ++index) {
            points.push_back({
                controls[index],
                static_cast<double>(index) / static_cast<double>(controls.size() - 1),
            });
        }
        return points;
    }

    std::vector<ParameterPoint> points{{wire.Start(), 0.0}};
    FlattenWireRange(
        wire, 0.0, 1.0, wire.Start(), wire.End(),
        tolerance, 0, points);
    return points;
}

struct SegmentPair {
    double first = 0.0;
    double second = 0.0;
    double distance = std::numeric_limits<double>::infinity();
};

std::optional<SegmentPair> ClosestSegmentPair(
    Vector3 firstStart,
    Vector3 firstEnd,
    Vector3 secondStart,
    Vector3 secondEnd)
{
    const Vector3 firstDirection = firstEnd - firstStart;
    const Vector3 secondDirection = secondEnd - secondStart;
    const Vector3 offset = firstStart - secondStart;
    const double a = Dot(firstDirection, firstDirection);
    const double b = Dot(firstDirection, secondDirection);
    const double c = Dot(secondDirection, secondDirection);
    const double d = Dot(firstDirection, offset);
    const double e = Dot(secondDirection, offset);
    const double denominator = a * c - b * b;
    if (a <= 1.0e-18 || c <= 1.0e-18
        || std::abs(denominator) <= 1.0e-14 * std::max(a * c, 1.0)) {
        return std::nullopt;
    }

    double firstParameter = (b * e - c * d) / denominator;
    double secondParameter = (a * e - b * d) / denominator;
    firstParameter = std::clamp(firstParameter, 0.0, 1.0);
    secondParameter = std::clamp((b * firstParameter + e) / c, 0.0, 1.0);
    firstParameter = std::clamp((b * secondParameter - d) / a, 0.0, 1.0);
    const Vector3 firstPoint = firstStart + firstDirection * firstParameter;
    const Vector3 secondPoint = secondStart + secondDirection * secondParameter;
    return SegmentPair{firstParameter, secondParameter, (firstPoint - secondPoint).Length()};
}

Vector3 NumericWireDerivative(const Wire& wire, double parameter)
{
    constexpr double step = 1.0e-6;
    const double before = std::max(0.0, parameter - step);
    const double after = std::min(1.0, parameter + step);
    if (after - before <= 1.0e-15) {
        return {};
    }
    return (wire.Evaluate(after) - wire.Evaluate(before)) / (after - before);
}

WireIntersection RefineWireIntersection(
    const Wire& first,
    const Wire& second,
    double firstParameter,
    double secondParameter)
{
    for (int iteration = 0; iteration < 16; ++iteration) {
        const Vector3 firstPoint = first.Evaluate(firstParameter);
        const Vector3 secondPoint = second.Evaluate(secondParameter);
        const Vector3 residual = firstPoint - secondPoint;
        const Vector3 firstDerivative = NumericWireDerivative(first, firstParameter);
        const Vector3 secondDerivative = NumericWireDerivative(second, secondParameter);
        const double a = Dot(firstDerivative, firstDerivative);
        const double b = Dot(firstDerivative, secondDerivative);
        const double c = Dot(secondDerivative, secondDerivative);
        const double denominator = a * c - b * b;
        if (a <= 1.0e-18 || c <= 1.0e-18
            || std::abs(denominator) <= 1.0e-18 * std::max(a * c, 1.0)) {
            break;
        }
        const double firstRight = -Dot(firstDerivative, residual);
        const double secondRight = Dot(secondDerivative, residual);
        const double firstDelta = (firstRight * c + b * secondRight) / denominator;
        const double secondDelta = (a * secondRight + b * firstRight) / denominator;
        const double nextFirst = std::clamp(firstParameter + firstDelta, 0.0, 1.0);
        const double nextSecond = std::clamp(secondParameter + secondDelta, 0.0, 1.0);
        if (std::abs(nextFirst - firstParameter) + std::abs(nextSecond - secondParameter) <= 1.0e-13) {
            firstParameter = nextFirst;
            secondParameter = nextSecond;
            break;
        }
        firstParameter = nextFirst;
        secondParameter = nextSecond;
    }
    const Vector3 firstPoint = first.Evaluate(firstParameter);
    const Vector3 secondPoint = second.Evaluate(secondParameter);
    return {(firstPoint + secondPoint) * 0.5, firstParameter, secondParameter};
}

std::vector<WireIntersection> IntersectFiniteWires(
    const Wire& first,
    const Wire& second,
    double tolerance)
{
    const double flattenTolerance = std::max(tolerance * 0.25, 1.0e-6);
    const auto firstPoints = FlattenWireWithParameters(first, flattenTolerance);
    const auto secondPoints = FlattenWireWithParameters(second, flattenTolerance);
    std::vector<WireIntersection> intersections;
    for (std::size_t firstIndex = 1; firstIndex < firstPoints.size(); ++firstIndex) {
        for (std::size_t secondIndex = 1; secondIndex < secondPoints.size(); ++secondIndex) {
            const auto segment = ClosestSegmentPair(
                firstPoints[firstIndex - 1].point,
                firstPoints[firstIndex].point,
                secondPoints[secondIndex - 1].point,
                secondPoints[secondIndex].point);
            if (!segment.has_value() || segment->distance > flattenTolerance * 3.0) {
                continue;
            }
            const double firstParameter = firstPoints[firstIndex - 1].parameter
                + (firstPoints[firstIndex].parameter - firstPoints[firstIndex - 1].parameter)
                    * segment->first;
            const double secondParameter = secondPoints[secondIndex - 1].parameter
                + (secondPoints[secondIndex].parameter - secondPoints[secondIndex - 1].parameter)
                    * segment->second;
            const WireIntersection refined = RefineWireIntersection(
                first, second, firstParameter, secondParameter);
            if ((first.Evaluate(refined.firstParameter)
                    - second.Evaluate(refined.secondParameter)).Length() > tolerance * 2.0) {
                continue;
            }
            const auto duplicate = std::find_if(
                intersections.begin(), intersections.end(), [&](const WireIntersection& existing) {
                    return std::abs(existing.firstParameter - refined.firstParameter) <= 1.0e-6
                        && std::abs(existing.secondParameter - refined.secondParameter) <= 1.0e-6;
                });
            if (duplicate == intersections.end()) {
                intersections.push_back(refined);
            }
        }
    }
    return intersections;
}

Wire ExtractWireRange(const Wire& wire, double startParameter, double endParameter)
{
    constexpr double parameterTolerance = 1.0e-10;
    if (!std::isfinite(startParameter) || !std::isfinite(endParameter)
        || startParameter < -parameterTolerance || endParameter > 1.0 + parameterTolerance
        || endParameter - startParameter <= parameterTolerance) {
        throw std::invalid_argument("Wire extraction range is invalid.");
    }
    const double start = std::clamp(startParameter, 0.0, 1.0);
    const double end = std::clamp(endParameter, 0.0, 1.0);
    if (start <= parameterTolerance && end >= 1.0 - parameterTolerance) {
        return wire;
    }
    if (wire.Kind() == WireKind::Circle) {
        const WireArcData arc = wire.ArcData();
        return Wire::CircularArc(
            arc.center, arc.uAxis, arc.vAxis, arc.radius,
            arc.startAngleRadians + arc.sweepAngleRadians * start,
            arc.sweepAngleRadians * (end - start));
    }
    if (start <= parameterTolerance) {
        return wire.SplitAt(end).first;
    }
    if (end >= 1.0 - parameterTolerance) {
        return wire.SplitAt(start).second;
    }
    const auto beforeEnd = wire.SplitAt(end).first;
    return beforeEnd.SplitAt(start / end).second;
}

Vector3 EvaluateBezierPolynomial(const std::array<Vector3, 4>& controls, double parameter)
{
    const double oneMinus = 1.0 - parameter;
    return controls[0] * (oneMinus * oneMinus * oneMinus)
        + controls[1] * (3.0 * oneMinus * oneMinus * parameter)
        + controls[2] * (3.0 * oneMinus * parameter * parameter)
        + controls[3] * (parameter * parameter * parameter);
}

Vector3 EvaluateBezierDerivative(const std::array<Vector3, 4>& controls, double parameter)
{
    const double oneMinus = 1.0 - parameter;
    return (controls[1] - controls[0]) * (3.0 * oneMinus * oneMinus)
        + (controls[2] - controls[1]) * (6.0 * oneMinus * parameter)
        + (controls[3] - controls[2]) * (3.0 * parameter * parameter);
}

Wire BezierPolynomialRange(
    const std::array<Vector3, 4>& controls,
    double startParameter,
    double endParameter)
{
    const double span = endParameter - startParameter;
    const Vector3 start = EvaluateBezierPolynomial(controls, startParameter);
    const Vector3 end = EvaluateBezierPolynomial(controls, endParameter);
    return Wire::CubicBezier(
        start,
        start + EvaluateBezierDerivative(controls, startParameter) * (span / 3.0),
        end - EvaluateBezierDerivative(controls, endParameter) * (span / 3.0),
        end);
}

std::array<Vector3, 4> BezierControls(const Wire& wire)
{
    if (wire.Kind() != WireKind::CubicBezier || wire.ControlPoints().size() != 4) {
        throw std::invalid_argument("Wire is not a cubic Bezier segment.");
    }
    const auto& points = wire.ControlPoints();
    return {points[0], points[1], points[2], points[3]};
}

std::vector<std::array<Vector3, 4>> DecomposeBSplineToBezier(const Wire& wire)
{
    if (wire.Kind() != WireKind::CubicBSpline) {
        throw std::invalid_argument("Wire is not a cubic B-spline.");
    }
    std::vector<double> internalKnots;
    const auto& knots = wire.BSplineKnots();
    for (std::size_t index = 4; index + 4 < knots.size(); ++index) {
        if (knots[index] <= 1.0e-12 || knots[index] >= 1.0 - 1.0e-12) {
            continue;
        }
        if (internalKnots.empty() || std::abs(internalKnots.back() - knots[index]) > 1.0e-12) {
            internalKnots.push_back(knots[index]);
        }
    }

    Wire remaining = wire;
    double previous = 0.0;
    std::vector<std::array<Vector3, 4>> segments;
    for (double knot : internalKnots) {
        const double local = (knot - previous) / (1.0 - previous);
        auto parts = remaining.SplitAt(local);
        const auto& controls = parts.first.ControlPoints();
        if (controls.size() != 4) {
            throw std::logic_error("B-spline span did not reduce to a cubic Bezier segment.");
        }
        segments.push_back({controls[0], controls[1], controls[2], controls[3]});
        remaining = std::move(parts.second);
        previous = knot;
    }
    const auto& controls = remaining.ControlPoints();
    if (controls.size() != 4) {
        throw std::logic_error("Final B-spline span did not reduce to a cubic Bezier segment.");
    }
    segments.push_back({controls[0], controls[1], controls[2], controls[3]});
    return segments;
}

Wire BezierChainToBSpline(const std::vector<std::array<Vector3, 4>>& segments)
{
    if (segments.empty()) {
        throw std::invalid_argument("Bezier chain must contain at least one segment.");
    }
    std::vector<Vector3> controls(segments.front().begin(), segments.front().end());
    for (std::size_t segment = 1; segment < segments.size(); ++segment) {
        controls.insert(controls.end(), segments[segment].begin() + 1, segments[segment].end());
    }
    std::vector<double> knots(4, 0.0);
    for (std::size_t segment = 1; segment < segments.size(); ++segment) {
        const double knot = static_cast<double>(segment) / static_cast<double>(segments.size());
        knots.insert(knots.end(), 3, knot);
    }
    knots.insert(knots.end(), 4, 1.0);
    return Wire::CubicBSplineWithKnots(std::move(controls), std::move(knots));
}

double ExtensionReach(const Wire& target, Vector3 endpoint, const std::vector<Wire>& boundaries)
{
    double reach = std::max((target.End() - target.Start()).Length() * 4.0, 10.0);
    for (const Wire& boundary : boundaries) {
        for (int sample = 0; sample <= 24; ++sample) {
            reach = std::max(reach,
                (boundary.Evaluate(static_cast<double>(sample) / 24.0) - endpoint).Length() * 1.5);
        }
    }
    return reach;
}

struct ExtensionCandidate {
    Wire added;
    std::function<Wire(double)> buildExtended;
};

ExtensionCandidate MakeExtensionCandidate(
    const Wire& target,
    RetainedLineEnd end,
    const std::vector<Wire>& boundaries)
{
    const bool start = end == RetainedLineEnd::Start;
    const Vector3 endpoint = start ? target.Start() : target.End();
    const double reach = ExtensionReach(target, endpoint, boundaries);
    if (target.Kind() == WireKind::Line || target.Kind() == WireKind::Polyline) {
        const auto& points = target.ControlPoints();
        const Vector3 adjacent = start ? points[1] : points[points.size() - 2];
        const Vector3 direction = (endpoint - adjacent).Normalized();
        const Wire added = Wire::Line(endpoint, endpoint + direction * reach);
        return {
            added,
            [target, start, endpoint, direction, reach](double parameter) {
                const Vector3 intersection = endpoint + direction * (reach * parameter);
                if (target.Kind() == WireKind::Line) {
                    return start
                        ? Wire::Line(intersection, target.End())
                        : Wire::Line(target.Start(), intersection);
                }
                std::vector<Vector3> controls = target.ControlPoints();
                (start ? controls.front() : controls.back()) = intersection;
                return Wire::Polyline(std::move(controls));
            },
        };
    }
    if (target.Kind() == WireKind::CircularArc) {
        const WireArcData arc = target.ArcData();
        const double orientation = arc.sweepAngleRadians >= 0.0 ? 1.0 : -1.0;
        const double available = 2.0 * std::numbers::pi
            - std::abs(arc.sweepAngleRadians) - 1.0e-6;
        if (available <= 1.0e-6) {
            throw std::invalid_argument("Circular arc has no remaining sweep to extend.");
        }
        const double endpointAngle = start
            ? arc.startAngleRadians
            : arc.startAngleRadians + arc.sweepAngleRadians;
        const double addedSweep = (start ? -orientation : orientation) * available;
        const Wire added = Wire::CircularArc(
            arc.center, arc.uAxis, arc.vAxis, arc.radius,
            endpointAngle, addedSweep);
        return {
            added,
            [arc, start, orientation, available](double parameter) {
                const double delta = available * parameter;
                return start
                    ? Wire::CircularArc(
                        arc.center, arc.uAxis, arc.vAxis, arc.radius,
                        arc.startAngleRadians - orientation * delta,
                        arc.sweepAngleRadians + orientation * delta)
                    : Wire::CircularArc(
                        arc.center, arc.uAxis, arc.vAxis, arc.radius,
                        arc.startAngleRadians,
                        arc.sweepAngleRadians + orientation * delta);
            },
        };
    }
    if (target.Kind() == WireKind::Circle) {
        throw std::invalid_argument("A closed circle cannot be extended.");
    }

    std::vector<std::array<Vector3, 4>> sourceSegments;
    if (target.Kind() == WireKind::CubicBezier) {
        sourceSegments.push_back(BezierControls(target));
    } else if (target.Kind() == WireKind::CubicBSpline) {
        sourceSegments = DecomposeBSplineToBezier(target);
    } else {
        throw std::invalid_argument("This wire kind cannot be extended.");
    }
    const auto endpointSegment = start ? sourceSegments.front() : sourceSegments.back();
    const Vector3 derivative = EvaluateBezierDerivative(endpointSegment, start ? 0.0 : 1.0);
    if (derivative.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Curve endpoint has no stable direction to extend.");
    }
    const double span = std::clamp(reach / derivative.Length(), 1.0, 64.0);
    const Wire added = start
        ? BezierPolynomialRange(endpointSegment, 0.0, -span)
        : BezierPolynomialRange(endpointSegment, 1.0, 1.0 + span);
    if (target.Kind() == WireKind::CubicBezier) {
        return {
            added,
            [endpointSegment, start, span](double parameter) {
                return start
                    ? BezierPolynomialRange(endpointSegment, -span * parameter, 1.0)
                    : BezierPolynomialRange(endpointSegment, 0.0, 1.0 + span * parameter);
            },
        };
    }
    return {
        added,
        [sourceSegments, added, start](double parameter) {
            std::vector<std::array<Vector3, 4>> segments = sourceSegments;
            const auto partial = BezierControls(ExtractWireRange(added, 0.0, parameter));
            if (start) {
                const Wire reversed = Wire::CubicBezier(
                    partial[3], partial[2], partial[1], partial[0]);
                segments.insert(segments.begin(), BezierControls(reversed));
            } else {
                segments.push_back(partial);
            }
            return BezierChainToBSpline(segments);
        },
    };
}

} // namespace

DirectWireTrimResult TrimWireAtBoundaries(
    const Wire& target,
    double pickedParameter,
    const std::vector<Wire>& boundaries,
    double tolerance)
{
    if (!std::isfinite(pickedParameter) || pickedParameter < 0.0 || pickedParameter > 1.0) {
        throw std::invalid_argument("Trim pick parameter must lie on the target wire.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Trim tolerance must be positive.");
    }

    constexpr double parameterTolerance = 1.0e-7;
    std::vector<double> intersections;
    for (const Wire& boundary : boundaries) {
        for (const WireIntersection& intersection : IntersectFiniteWires(target, boundary, tolerance)) {
            if (!target.IsClosed()
                && (intersection.firstParameter <= parameterTolerance
                    || intersection.firstParameter >= 1.0 - parameterTolerance)) {
                continue;
            }
            const auto duplicate = std::find_if(
                intersections.begin(), intersections.end(), [&](double parameter) {
                    return std::abs(parameter - intersection.firstParameter) <= parameterTolerance;
                });
            if (duplicate == intersections.end()) {
                intersections.push_back(intersection.firstParameter);
            }
        }
    }
    if (intersections.empty()) {
        throw std::invalid_argument("No visible wire boundary intersects the target.");
    }
    std::sort(intersections.begin(), intersections.end());

    if (target.IsClosed()) {
        if (target.Kind() != WireKind::Circle) {
            throw std::invalid_argument("Closed polyline trimming is not available yet.");
        }
        if (intersections.size() < 2) {
            throw std::invalid_argument("A closed curve needs two boundary intersections to trim.");
        }
        auto upperIterator = std::upper_bound(intersections.begin(), intersections.end(), pickedParameter);
        const double upper = upperIterator == intersections.end()
            ? intersections.front() + 1.0
            : *upperIterator;
        const double lower = upperIterator == intersections.begin()
            ? intersections.back() - 1.0
            : *(upperIterator - 1);
        const double removedFraction = upper - lower;
        const double retainedFraction = 1.0 - removedFraction;
        if (removedFraction <= parameterTolerance || retainedFraction <= parameterTolerance) {
            throw std::invalid_argument("Trimmed curve portion is too short.");
        }
        const WireArcData arc = target.ArcData();
        const auto makeArc = [&](double start, double fraction) {
            return Wire::CircularArc(
                arc.center, arc.uAxis, arc.vAxis, arc.radius,
                arc.startAngleRadians + arc.sweepAngleRadians * start,
                arc.sweepAngleRadians * fraction);
        };
        return {
            makeArc(lower, removedFraction),
            {makeArc(upper, retainedFraction)},
        };
    }

    double lower = 0.0;
    double upper = 1.0;
    for (double parameter : intersections) {
        if (parameter < pickedParameter - parameterTolerance) {
            lower = parameter;
        } else if (parameter > pickedParameter + parameterTolerance) {
            upper = parameter;
            break;
        } else if (pickedParameter <= parameter) {
            upper = parameter;
            break;
        } else {
            lower = parameter;
        }
    }

    if (upper - lower <= parameterTolerance) {
        throw std::invalid_argument("Trimmed curve portion is too short.");
    }

    std::vector<Wire> retained;
    if (lower > parameterTolerance) {
        retained.push_back(ExtractWireRange(target, 0.0, lower));
    }
    if (upper < 1.0 - parameterTolerance) {
        retained.push_back(ExtractWireRange(target, upper, 1.0));
    }
    if (retained.empty()) {
        throw std::invalid_argument("Trim would remove the complete target wire.");
    }
    return {ExtractWireRange(target, lower, upper), std::move(retained)};
}

DirectWireExtendResult ExtendWireToBoundary(
    const Wire& target,
    double pickedParameter,
    const std::vector<Wire>& boundaries,
    double tolerance)
{
    if (!std::isfinite(pickedParameter) || pickedParameter < 0.0 || pickedParameter > 1.0) {
        throw std::invalid_argument("Extend pick parameter must lie on the target wire.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Extend tolerance must be positive.");
    }

    const RetainedLineEnd extendedEnd = pickedParameter < 0.5
        ? RetainedLineEnd::Start
        : RetainedLineEnd::End;
    const ExtensionCandidate candidate = MakeExtensionCandidate(
        target, extendedEnd, boundaries);
    constexpr double parameterTolerance = 1.0e-7;
    double bestParameter = std::numeric_limits<double>::infinity();
    std::optional<WireIntersection> best;
    for (const Wire& boundary : boundaries) {
        for (const WireIntersection& intersection : IntersectFiniteWires(
                 candidate.added, boundary, tolerance)) {
            if (intersection.firstParameter <= parameterTolerance) {
                continue;
            }
            if (intersection.firstParameter < bestParameter) {
                bestParameter = intersection.firstParameter;
                best = intersection;
            }
        }
    }
    if (!best.has_value()) {
        throw std::invalid_argument("No visible wire boundary lies in the selected extension direction.");
    }
    Wire added = ExtractWireRange(candidate.added, 0.0, bestParameter);
    if (extendedEnd == RetainedLineEnd::Start) {
        added = added.Reversed();
    }
    return {
        candidate.buildExtended(bestParameter),
        std::move(added),
        best->point,
        extendedEnd,
    };
}

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
    if (wire.Kind() == WireKind::CubicBezier || wire.Kind() == WireKind::CubicBSpline) {
        throw std::invalid_argument("Exact spline offset is not supported yet.");
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
