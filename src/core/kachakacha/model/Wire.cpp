#include "kachakacha/model/Wire.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kachakacha::model {

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Cross;
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

} // namespace kachakacha::model
