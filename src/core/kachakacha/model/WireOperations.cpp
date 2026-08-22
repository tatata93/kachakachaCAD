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
