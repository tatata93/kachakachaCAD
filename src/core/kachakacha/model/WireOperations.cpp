#include "kachakacha/model/WireOperations.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kachakacha::model {

using geometry::Dot;
using geometry::Vector3;

namespace {

struct SegmentIntersection {
    Vector3 point;
    double firstParameter = 0.0;
    double secondParameter = 0.0;
};

SegmentIntersection IntersectSegments(const Wire& first, const Wire& second, double tolerance)
{
    if (first.Kind() != WireKind::Line || second.Kind() != WireKind::Line) {
        throw std::invalid_argument("Chamfer requires two line wires.");
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Chamfer tolerance must be positive.");
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
        throw std::invalid_argument("Chamfer lines are parallel or nearly parallel.");
    }

    const double firstParameter = (b * e - c * d) / denominator;
    const double secondParameter = (a * e - b * d) / denominator;
    const Vector3 firstPoint = firstStart + firstDirection * firstParameter;
    const Vector3 secondPoint = secondStart + secondDirection * secondParameter;
    if ((firstPoint - secondPoint).Length() > tolerance) {
        throw std::invalid_argument("Chamfer lines do not intersect in 3D.");
    }
    if (firstParameter < -tolerance || firstParameter > 1.0 + tolerance
        || secondParameter < -tolerance || secondParameter > 1.0 + tolerance) {
        throw std::invalid_argument("Chamfer intersection is outside a line segment.");
    }

    return {(firstPoint + secondPoint) * 0.5, firstParameter, secondParameter};
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

} // namespace

LineChamferResult ChamferIntersectingLines(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    double firstSetback,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double secondSetback,
    double tolerance)
{
    const SegmentIntersection intersection = IntersectSegments(first, second, tolerance);
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

} // namespace kachakacha::model
