#include "kachakacha/model/Wire.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kachakacha::model {

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Cross;
using kachakacha::geometry::Dot;
using kachakacha::geometry::Vector3;

namespace {

constexpr double TwoPi = 6.28318530717958647692;

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

struct ArcFrame {
    Vector3 uAxis;
    Vector3 vAxis;
};

ArcFrame MakeArcFrame(Vector3 uAxisHint, Vector3 vAxisHint)
{
    if (!uAxisHint.IsFinite() || !vAxisHint.IsFinite()) {
        throw std::invalid_argument("Arc axes contain a non-finite value.");
    }

    const Vector3 uAxis = uAxisHint.Normalized();
    const Vector3 normal = Cross(uAxis, vAxisHint);
    if (normal.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Arc axes must not be parallel.");
    }

    const Vector3 normalUnit = normal.Normalized();
    const Vector3 vAxis = Cross(normalUnit, uAxis).Normalized();
    return {uAxis, vAxis};
}

void RequireArcValues(Vector3 center, double radius, double startAngleRadians, double sweepAngleRadians)
{
    if (!center.IsFinite() || !std::isfinite(radius) || !std::isfinite(startAngleRadians) || !std::isfinite(sweepAngleRadians)) {
        throw std::invalid_argument("Arc value contains a non-finite value.");
    }

    if (radius <= 1.0e-9) {
        throw std::invalid_argument("Arc radius must be positive.");
    }

    if (std::abs(sweepAngleRadians) <= 1.0e-9) {
        throw std::invalid_argument("Arc sweep angle must not be zero.");
    }
}

Vector3 EvaluateArcPoint(
    const Vector3& center,
    const Vector3& uAxis,
    const Vector3& vAxis,
    double radius,
    double angleRadians)
{
    return center + uAxis * (std::cos(angleRadians) * radius) + vAxis * (std::sin(angleRadians) * radius);
}

} // namespace

Wire::Wire(WireKind kind, std::vector<Vector3> controlPoints)
    : kind_(kind), controlPoints_(std::move(controlPoints))
{
}

Wire::Wire(
    WireKind kind,
    std::vector<Vector3> controlPoints,
    Vector3 arcCenter,
    Vector3 arcUAxis,
    Vector3 arcVAxis,
    double arcRadius,
    double arcStartAngleRadians,
    double arcSweepAngleRadians)
    : kind_(kind),
      controlPoints_(std::move(controlPoints)),
      arcCenter_(arcCenter),
      arcUAxis_(arcUAxis),
      arcVAxis_(arcVAxis),
      arcRadius_(arcRadius),
      arcStartAngleRadians_(arcStartAngleRadians),
      arcSweepAngleRadians_(arcSweepAngleRadians)
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

Wire Wire::Circle(Vector3 center, Vector3 uAxisHint, Vector3 vAxisHint, double radius)
{
    return CircularArc(center, uAxisHint, vAxisHint, radius, 0.0, TwoPi);
}

Wire Wire::CircularArc(
    Vector3 center,
    Vector3 uAxisHint,
    Vector3 vAxisHint,
    double radius,
    double startAngleRadians,
    double sweepAngleRadians)
{
    RequireArcValues(center, radius, startAngleRadians, sweepAngleRadians);
    const ArcFrame frame = MakeArcFrame(uAxisHint, vAxisHint);
    const Vector3 start = EvaluateArcPoint(center, frame.uAxis, frame.vAxis, radius, startAngleRadians);
    const Vector3 end = EvaluateArcPoint(center, frame.uAxis, frame.vAxis, radius, startAngleRadians + sweepAngleRadians);

    std::vector<Vector3> points = {center, start, end};
    const WireKind kind = AlmostEqual(std::abs(sweepAngleRadians), TwoPi, 1.0e-9)
        ? WireKind::Circle
        : WireKind::CircularArc;

    return {
        kind,
        std::move(points),
        center,
        frame.uAxis,
        frame.vAxis,
        radius,
        startAngleRadians,
        sweepAngleRadians,
    };
}

Wire Wire::CircularArcThroughThreePoints(Vector3 start, Vector3 through, Vector3 end)
{
    const std::vector<Vector3> points = {start, through, end};
    RequireFinite(points);

    const Vector3 startToThrough = through - start;
    const Vector3 startToEnd = end - start;
    const Vector3 planeNormal = Cross(startToThrough, startToEnd);
    const double normalLengthSquared = planeNormal.LengthSquared();
    if (normalLengthSquared <= 1.0e-18) {
        throw std::invalid_argument("Three-point arc requires non-collinear points.");
    }

    const Vector3 centerOffset = (
        Cross(startToEnd, planeNormal) * startToThrough.LengthSquared()
        + Cross(planeNormal, startToThrough) * startToEnd.LengthSquared())
        / (2.0 * normalLengthSquared);
    const Vector3 center = start + centerOffset;
    const Vector3 uAxis = (start - center).Normalized();
    const Vector3 normal = planeNormal.Normalized();
    const Vector3 vAxis = Cross(normal, uAxis).Normalized();
    const double radius = (start - center).Length();

    const auto positiveAngle = [&](Vector3 point) {
        const Vector3 direction = (point - center) / radius;
        double angle = std::atan2(geometry::Dot(direction, vAxis), geometry::Dot(direction, uAxis));
        if (angle < 0.0) {
            angle += TwoPi;
        }
        return angle;
    };
    const double throughAngle = positiveAngle(through);
    double endAngle = positiveAngle(end);
    if (endAngle + 1.0e-12 < throughAngle) {
        endAngle += TwoPi;
    }
    if (endAngle > TwoPi + 1.0e-9) {
        throw std::invalid_argument("Three-point arc could not determine a valid sweep.");
    }
    return CircularArc(center, uAxis, vAxis, radius, 0.0, endAngle);
}

WireArcData Wire::ArcData() const
{
    if (kind_ != WireKind::Circle && kind_ != WireKind::CircularArc) {
        throw std::logic_error("Wire is not an arc or circle.");
    }

    return {
        arcCenter_,
        arcUAxis_,
        arcVAxis_,
        arcRadius_,
        arcStartAngleRadians_,
        arcSweepAngleRadians_,
    };
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

    case WireKind::Circle:
    case WireKind::CircularArc:
        return EvaluateArcPoint(
            arcCenter_,
            arcUAxis_,
            arcVAxis_,
            arcRadius_,
            arcStartAngleRadians_ + arcSweepAngleRadians_ * clamped);
    }

    throw std::logic_error("Unknown wire kind.");
}

bool Wire::IsClosed(double epsilon) const noexcept
{
    if (kind_ == WireKind::Circle) {
        return true;
    }

    return AlmostEqual(Start(), End(), epsilon);
}

Wire Wire::Translated(Vector3 delta) const
{
    if (!delta.IsFinite()) {
        throw std::invalid_argument("Wire translation contains a non-finite value.");
    }

    std::vector<Vector3> translatedPoints;
    translatedPoints.reserve(controlPoints_.size());
    for (const Vector3& point : controlPoints_) {
        translatedPoints.push_back(point + delta);
    }

    if (kind_ == WireKind::Circle || kind_ == WireKind::CircularArc) {
        return {
            kind_,
            std::move(translatedPoints),
            arcCenter_ + delta,
            arcUAxis_,
            arcVAxis_,
            arcRadius_,
            arcStartAngleRadians_,
            arcSweepAngleRadians_,
        };
    }

    return {kind_, std::move(translatedPoints)};
}

Wire Wire::Mirrored(Vector3 linePoint, Vector3 lineDirection, Vector3 planeNormal) const
{
    if (!linePoint.IsFinite() || !lineDirection.IsFinite() || !planeNormal.IsFinite()) {
        throw std::invalid_argument("Wire mirror definition contains a non-finite value.");
    }

    const Vector3 normal = planeNormal.Normalized();
    const Vector3 projectedDirection = lineDirection - normal * Dot(lineDirection, normal);
    if (projectedDirection.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Wire mirror axis must lie in the mirror plane.");
    }
    const Vector3 axis = projectedDirection.Normalized();
    const Vector3 across = Cross(normal, axis).Normalized();
    const auto reflectPoint = [&](Vector3 point) {
        return point - across * (2.0 * Dot(point - linePoint, across));
    };
    const auto reflectDirection = [&](Vector3 direction) {
        return direction - across * (2.0 * Dot(direction, across));
    };

    std::vector<Vector3> mirroredPoints;
    mirroredPoints.reserve(controlPoints_.size());
    for (const Vector3& point : controlPoints_) {
        mirroredPoints.push_back(reflectPoint(point));
    }

    if (kind_ == WireKind::Circle || kind_ == WireKind::CircularArc) {
        return {
            kind_,
            std::move(mirroredPoints),
            reflectPoint(arcCenter_),
            reflectDirection(arcUAxis_),
            reflectDirection(arcVAxis_),
            arcRadius_,
            arcStartAngleRadians_,
            arcSweepAngleRadians_,
        };
    }

    return {kind_, std::move(mirroredPoints)};
}

Wire Wire::RotatedAroundAxis(Vector3 axisPoint, Vector3 axisDirection, double angleRadians) const
{
    if (!axisPoint.IsFinite() || !axisDirection.IsFinite() || !std::isfinite(angleRadians)) {
        throw std::invalid_argument("Wire rotation definition contains a non-finite value.");
    }
    const Vector3 axis = axisDirection.Normalized();
    const double cosine = std::cos(angleRadians);
    const double sine = std::sin(angleRadians);
    const auto rotateDirection = [&](Vector3 direction) {
        return direction * cosine
            + Cross(axis, direction) * sine
            + axis * (Dot(axis, direction) * (1.0 - cosine));
    };
    const auto rotatePoint = [&](Vector3 point) {
        return axisPoint + rotateDirection(point - axisPoint);
    };

    std::vector<Vector3> rotatedPoints;
    rotatedPoints.reserve(controlPoints_.size());
    for (const Vector3& point : controlPoints_) {
        rotatedPoints.push_back(rotatePoint(point));
    }

    if (kind_ == WireKind::Circle || kind_ == WireKind::CircularArc) {
        return {
            kind_,
            std::move(rotatedPoints),
            rotatePoint(arcCenter_),
            rotateDirection(arcUAxis_),
            rotateDirection(arcVAxis_),
            arcRadius_,
            arcStartAngleRadians_,
            arcSweepAngleRadians_,
        };
    }
    return {kind_, std::move(rotatedPoints)};
}

Wire Wire::Reversed() const
{
    switch (kind_) {
    case WireKind::Line:
        return Line(controlPoints_[1], controlPoints_[0]);
    case WireKind::Polyline: {
        std::vector<Vector3> points = controlPoints_;
        std::reverse(points.begin(), points.end());
        return Polyline(std::move(points));
    }
    case WireKind::CubicBezier:
        return CubicBezier(controlPoints_[3], controlPoints_[2], controlPoints_[1], controlPoints_[0]);
    case WireKind::Circle:
        return Circle(arcCenter_, arcUAxis_, arcVAxis_, arcRadius_);
    case WireKind::CircularArc:
        return CircularArc(
            arcCenter_,
            arcUAxis_,
            arcVAxis_,
            arcRadius_,
            arcStartAngleRadians_ + arcSweepAngleRadians_,
            -arcSweepAngleRadians_);
    }
    throw std::logic_error("Unknown wire kind.");
}

std::pair<Wire, Wire> Wire::SplitAt(double parameter) const
{
    if (!std::isfinite(parameter) || parameter <= 1.0e-9 || parameter >= 1.0 - 1.0e-9) {
        throw std::invalid_argument("Wire split parameter must be inside the wire.");
    }

    switch (kind_) {
    case WireKind::Line: {
        const Vector3 split = Evaluate(parameter);
        return {Line(Start(), split), Line(split, End())};
    }
    case WireKind::Polyline: {
        const double scaled = parameter * static_cast<double>(controlPoints_.size() - 1);
        const std::size_t segment = std::min(
            static_cast<std::size_t>(scaled), controlPoints_.size() - 2);
        const double local = scaled - static_cast<double>(segment);
        const Vector3 split = Lerp(controlPoints_[segment], controlPoints_[segment + 1], local);
        std::vector<Vector3> first(controlPoints_.begin(), controlPoints_.begin() + segment + 1);
        std::vector<Vector3> second;
        if (!AlmostEqual(first.back(), split)) {
            first.push_back(split);
        }
        second.push_back(split);
        auto remaining = controlPoints_.begin() + segment + 1;
        if (remaining != controlPoints_.end() && AlmostEqual(*remaining, split)) {
            ++remaining;
        }
        second.insert(second.end(), remaining, controlPoints_.end());
        return {Polyline(std::move(first)), Polyline(std::move(second))};
    }
    case WireKind::CubicBezier: {
        const Vector3 ab = Lerp(controlPoints_[0], controlPoints_[1], parameter);
        const Vector3 bc = Lerp(controlPoints_[1], controlPoints_[2], parameter);
        const Vector3 cd = Lerp(controlPoints_[2], controlPoints_[3], parameter);
        const Vector3 abbc = Lerp(ab, bc, parameter);
        const Vector3 bccd = Lerp(bc, cd, parameter);
        const Vector3 split = Lerp(abbc, bccd, parameter);
        return {
            CubicBezier(controlPoints_[0], ab, abbc, split),
            CubicBezier(split, bccd, cd, controlPoints_[3]),
        };
    }
    case WireKind::CircularArc:
        return {
            CircularArc(
                arcCenter_, arcUAxis_, arcVAxis_, arcRadius_,
                arcStartAngleRadians_, arcSweepAngleRadians_ * parameter),
            CircularArc(
                arcCenter_, arcUAxis_, arcVAxis_, arcRadius_,
                arcStartAngleRadians_ + arcSweepAngleRadians_ * parameter,
                arcSweepAngleRadians_ * (1.0 - parameter)),
        };
    case WireKind::Circle:
        throw std::invalid_argument("A circle requires two split points.");
    }
    throw std::logic_error("Unknown wire kind.");
}

} // namespace kachakacha::model
