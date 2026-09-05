#include "kachakacha/model/Wire.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kachakacha::model {

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Cross;
using kachakacha::geometry::Dot;
using kachakacha::geometry::Vector3;

namespace {

constexpr double TwoPi = 6.28318530717958647692;
constexpr std::size_t CubicDegree = 3;

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

std::vector<double> MakeClampedUniformKnots(std::size_t controlPointCount)
{
    const std::size_t lastControlPoint = controlPointCount - 1;
    const std::size_t lastKnot = lastControlPoint + CubicDegree + 1;
    std::vector<double> knots(lastKnot + 1, 0.0);
    for (std::size_t index = lastKnot - CubicDegree; index <= lastKnot; ++index) {
        knots[index] = 1.0;
    }

    const std::size_t interiorCount = lastControlPoint - CubicDegree;
    for (std::size_t index = 1; index <= interiorCount; ++index) {
        knots[CubicDegree + index] = static_cast<double>(index)
            / static_cast<double>(interiorCount + 1);
    }
    return knots;
}

std::size_t FindBSplineSpan(double parameter, std::size_t controlPointCount, const std::vector<double>& knots)
{
    const std::size_t lastControlPoint = controlPointCount - 1;
    if (parameter >= 1.0) {
        return lastControlPoint;
    }

    for (std::size_t span = CubicDegree; span <= lastControlPoint; ++span) {
        if (parameter >= knots[span] && parameter < knots[span + 1]) {
            return span;
        }
    }
    return CubicDegree;
}

Vector3 EvaluateCubicBSpline(
    const std::vector<Vector3>& controlPoints,
    const std::vector<double>& knots,
    double parameter)
{
    const std::size_t span = FindBSplineSpan(parameter, controlPoints.size(), knots);
    std::array<Vector3, CubicDegree + 1> working{};
    for (std::size_t index = 0; index <= CubicDegree; ++index) {
        working[index] = controlPoints[span - CubicDegree + index];
    }

    for (int level = 1; level <= static_cast<int>(CubicDegree); ++level) {
        for (int index = static_cast<int>(CubicDegree); index >= level; --index) {
            const std::size_t knotIndex = span - CubicDegree + static_cast<std::size_t>(index);
            const double denominator = knots[knotIndex + CubicDegree - static_cast<std::size_t>(level) + 1]
                - knots[knotIndex];
            const double alpha = denominator <= 1.0e-15
                ? 0.0
                : (parameter - knots[knotIndex]) / denominator;
            working[static_cast<std::size_t>(index)] =
                working[static_cast<std::size_t>(index - 1)] * (1.0 - alpha)
                + working[static_cast<std::size_t>(index)] * alpha;
        }
    }
    return working[CubicDegree];
}

std::vector<double> NormalizeKnots(std::vector<double> knots)
{
    if (knots.empty()) {
        return knots;
    }
    const double first = knots.front();
    const double range = knots.back() - first;
    if (!std::isfinite(first) || !std::isfinite(range) || range <= 1.0e-15) {
        throw std::invalid_argument("B-spline knot range must be positive.");
    }
    for (double& knot : knots) {
        if (!std::isfinite(knot)) {
            throw std::invalid_argument("B-spline knot contains a non-finite value.");
        }
        knot = (knot - first) / range;
    }
    return knots;
}

void ValidateCubicBSplineKnots(
    const std::vector<Vector3>& controlPoints,
    const std::vector<double>& knots)
{
    if (knots.size() != controlPoints.size() + CubicDegree + 1) {
        throw std::invalid_argument("Cubic B-spline knot count does not match its control points.");
    }
    for (std::size_t index = 1; index < knots.size(); ++index) {
        if (!std::isfinite(knots[index]) || knots[index] + 1.0e-12 < knots[index - 1]) {
            throw std::invalid_argument("Cubic B-spline knots must be finite and nondecreasing.");
        }
    }
    for (std::size_t index = 1; index <= CubicDegree; ++index) {
        if (std::abs(knots[index] - knots.front()) > 1.0e-12
            || std::abs(knots[knots.size() - 1 - index] - knots.back()) > 1.0e-12) {
            throw std::invalid_argument("Cubic B-spline must be clamped at both ends.");
        }
    }
    for (std::size_t first = 0; first < knots.size();) {
        std::size_t last = first + 1;
        while (last < knots.size()
            && std::abs(knots[last] - knots[first]) <= 1.0e-12) {
            ++last;
        }
        const bool internal = knots[first] > knots.front() + 1.0e-12
            && knots[first] < knots.back() - 1.0e-12;
        if (internal && last - first > CubicDegree) {
            throw std::invalid_argument("Cubic B-spline internal knot multiplicity is too high.");
        }
        first = last;
    }
}

struct KnotInsertionResult {
    std::vector<Vector3> controlPoints;
    std::vector<double> knots;
};

std::size_t KnotMultiplicity(const std::vector<double>& knots, double parameter)
{
    return static_cast<std::size_t>(std::count_if(
        knots.begin(), knots.end(), [&](double knot) {
            return std::abs(knot - parameter) <= 1.0e-12;
        }));
}

KnotInsertionResult InsertCubicKnotOnce(
    const std::vector<Vector3>& controlPoints,
    const std::vector<double>& knots,
    double parameter)
{
    const std::size_t lastControl = controlPoints.size() - 1;
    const std::size_t lastKnot = knots.size() - 1;
    const std::size_t span = FindBSplineSpan(parameter, controlPoints.size(), knots);
    const std::size_t multiplicity = KnotMultiplicity(knots, parameter);
    if (multiplicity >= CubicDegree + 1) {
        return {controlPoints, knots};
    }

    std::vector<Vector3> insertedControls(controlPoints.size() + 1);
    std::vector<double> insertedKnots(knots.size() + 1);
    for (std::size_t index = 0; index <= span; ++index) {
        insertedKnots[index] = knots[index];
    }
    insertedKnots[span + 1] = parameter;
    for (std::size_t index = span + 1; index <= lastKnot; ++index) {
        insertedKnots[index + 1] = knots[index];
    }

    for (std::size_t index = 0; index <= span - CubicDegree; ++index) {
        insertedControls[index] = controlPoints[index];
    }
    for (std::size_t index = span - multiplicity; index <= lastControl; ++index) {
        insertedControls[index + 1] = controlPoints[index];
    }
    for (std::size_t index = span - CubicDegree + 1;
         index <= span - multiplicity; ++index) {
        const double denominator = knots[index + CubicDegree] - knots[index];
        const double alpha = denominator <= 1.0e-15
            ? 0.0
            : (parameter - knots[index]) / denominator;
        insertedControls[index] = controlPoints[index - 1] * (1.0 - alpha)
            + controlPoints[index] * alpha;
    }
    return {std::move(insertedControls), std::move(insertedKnots)};
}

std::vector<double> CubicBSplineBasisValues(
    std::size_t controlPointCount,
    double parameter)
{
    std::vector<double> values(controlPointCount, 0.0);
    const std::vector<double> knots = MakeClampedUniformKnots(controlPointCount);
    const std::size_t span = FindBSplineSpan(parameter, controlPointCount, knots);
    std::array<double, CubicDegree + 1> basis{};
    std::array<double, CubicDegree + 1> left{};
    std::array<double, CubicDegree + 1> right{};
    basis[0] = 1.0;

    for (std::size_t degree = 1; degree <= CubicDegree; ++degree) {
        left[degree] = parameter - knots[span + 1 - degree];
        right[degree] = knots[span + degree] - parameter;
        double saved = 0.0;
        for (std::size_t index = 0; index < degree; ++index) {
            const double denominator = right[index + 1] + left[degree - index];
            const double portion = denominator <= 1.0e-15 ? 0.0 : basis[index] / denominator;
            basis[index] = saved + right[index + 1] * portion;
            saved = left[degree - index] * portion;
        }
        basis[degree] = saved;
    }
    for (std::size_t index = 0; index <= CubicDegree; ++index) {
        values[span - CubicDegree + index] = basis[index];
    }
    return values;
}

std::vector<Vector3> SolveInterpolationControls(const std::vector<Vector3>& throughPoints)
{
    const std::size_t count = throughPoints.size();
    std::vector<std::vector<double>> matrix(count, std::vector<double>(count + 3, 0.0));
    for (std::size_t row = 0; row < count; ++row) {
        const double parameter = static_cast<double>(row) / static_cast<double>(count - 1);
        const std::vector<double> basis = CubicBSplineBasisValues(count, parameter);
        std::copy(basis.begin(), basis.end(), matrix[row].begin());
        matrix[row][count] = throughPoints[row].x;
        matrix[row][count + 1] = throughPoints[row].y;
        matrix[row][count + 2] = throughPoints[row].z;
    }

    for (std::size_t column = 0; column < count; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < count; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[pivot][column]) <= 1.0e-12) {
            throw std::invalid_argument("Spline through-points could not be interpolated.");
        }
        std::swap(matrix[column], matrix[pivot]);

        const double divisor = matrix[column][column];
        for (std::size_t value = column; value < count + 3; ++value) {
            matrix[column][value] /= divisor;
        }
        for (std::size_t row = 0; row < count; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = matrix[row][column];
            for (std::size_t value = column; value < count + 3; ++value) {
                matrix[row][value] -= factor * matrix[column][value];
            }
        }
    }

    std::vector<Vector3> controls;
    controls.reserve(count);
    for (const auto& row : matrix) {
        controls.push_back({row[count], row[count + 1], row[count + 2]});
    }
    return controls;
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
    std::vector<double> bsplineKnots)
    : kind_(kind),
      controlPoints_(std::move(controlPoints)),
      bsplineKnots_(std::move(bsplineKnots))
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

Wire Wire::CubicBSpline(std::vector<Vector3> controlPoints)
{
    if (controlPoints.size() < CubicDegree + 1) {
        throw std::invalid_argument("Cubic B-spline wire requires at least four control points.");
    }
    const std::vector<double> knots = MakeClampedUniformKnots(controlPoints.size());
    return CubicBSplineWithKnots(
        std::move(controlPoints), knots);
}

Wire Wire::CubicBSplineWithKnots(
    std::vector<Vector3> controlPoints,
    std::vector<double> knots)
{
    RequireFinite(controlPoints);
    if (controlPoints.size() < CubicDegree + 1) {
        throw std::invalid_argument("Cubic B-spline wire requires at least four control points.");
    }
    if (AlmostEqual(controlPoints.front(), controlPoints.back())) {
        throw std::invalid_argument("Cubic B-spline wire requires different start and end points.");
    }
    knots = NormalizeKnots(std::move(knots));
    ValidateCubicBSplineKnots(controlPoints, knots);
    return {WireKind::CubicBSpline, std::move(controlPoints), std::move(knots)};
}

Wire Wire::InterpolatingCubicBSpline(const std::vector<Vector3>& throughPoints)
{
    RequireFinite(throughPoints);
    if (throughPoints.size() < CubicDegree + 1) {
        throw std::invalid_argument("Interpolating cubic B-spline requires at least four through-points.");
    }
    if (AlmostEqual(throughPoints.front(), throughPoints.back())) {
        throw std::invalid_argument("Interpolating cubic B-spline requires different start and end points.");
    }
    const std::vector<Vector3> controls = SolveInterpolationControls(throughPoints);
    return CubicBSpline(controls);
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

Wire Wire::CircularArcFromEndpointsRadius(
    Vector3 start,
    Vector3 end,
    Vector3 planeNormal,
    double radius,
    bool bulgeLeft)
{
    if (!start.IsFinite() || !end.IsFinite() || !planeNormal.IsFinite()
        || !std::isfinite(radius) || radius <= 1.0e-9) {
        throw std::invalid_argument("Endpoint-radius arc definition is invalid.");
    }
    const Vector3 chord = end - start;
    const double chordLength = chord.Length();
    if (chordLength <= 1.0e-9) {
        throw std::invalid_argument("Endpoint-radius arc requires two different endpoints.");
    }
    if (chordLength > radius * 2.0 + 1.0e-9) {
        throw std::invalid_argument("Arc radius must be at least half the endpoint distance.");
    }
    const Vector3 normal = planeNormal.Normalized();
    const Vector3 chordDirection = chord / chordLength;
    const Vector3 left = Cross(normal, chordDirection).Normalized();
    const double centerOffset = std::sqrt(std::max(
        0.0, radius * radius - chordLength * chordLength * 0.25));
    const Vector3 midpoint = (start + end) * 0.5;
    const Vector3 center = midpoint + left * (bulgeLeft ? -centerOffset : centerOffset);
    const Vector3 uAxis = (start - center).Normalized();
    const Vector3 vAxis = Cross(normal, uAxis).Normalized();
    const Vector3 endDirection = (end - center).Normalized();
    double positiveSweep = std::atan2(Dot(endDirection, vAxis), Dot(endDirection, uAxis));
    if (positiveSweep <= 1.0e-12) {
        positiveSweep += TwoPi;
    }
    const double negativeSweep = positiveSweep - TwoPi;
    const auto midpointSide = [&](double sweep) {
        const Vector3 arcMidpoint = EvaluateArcPoint(
            center, uAxis, vAxis, radius, sweep * 0.5);
        return Dot(arcMidpoint - midpoint, left);
    };
    const double sweep = (midpointSide(positiveSweep) >= 0.0) == bulgeLeft
        ? positiveSweep
        : negativeSweep;
    return CircularArc(center, uAxis, vAxis, radius, 0.0, sweep);
}

Wire Wire::CircularArcFromStartTangent(
    Vector3 start,
    Vector3 tangentDirection,
    Vector3 planeNormal,
    double radius,
    double sweepAngleRadians)
{
    if (!start.IsFinite() || !tangentDirection.IsFinite() || !planeNormal.IsFinite()
        || !std::isfinite(radius) || !std::isfinite(sweepAngleRadians)
        || radius <= 1.0e-9 || std::abs(sweepAngleRadians) <= 1.0e-9
        || std::abs(sweepAngleRadians) >= TwoPi - 1.0e-9) {
        throw std::invalid_argument("Start-tangent arc definition is invalid.");
    }
    const Vector3 normal = planeNormal.Normalized();
    const Vector3 tangentInPlane = tangentDirection
        - normal * Dot(tangentDirection, normal);
    if (tangentInPlane.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Arc tangent direction must lie in its drawing plane.");
    }
    const Vector3 tangent = tangentInPlane.Normalized();
    const double orientation = sweepAngleRadians > 0.0 ? 1.0 : -1.0;
    const Vector3 center = start + Cross(normal, tangent) * (radius * orientation);
    const Vector3 uAxis = (start - center).Normalized();
    const Vector3 vAxis = Cross(normal, uAxis).Normalized();
    return CircularArc(
        center, uAxis, vAxis, radius, 0.0, sweepAngleRadians);
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

const std::vector<double>& Wire::BSplineKnots() const
{
    if (kind_ != WireKind::CubicBSpline) {
        throw std::logic_error("Wire is not a cubic B-spline.");
    }
    return bsplineKnots_;
}

Wire Wire::WithMovedControlPoint(std::size_t controlPointIndex, Vector3 point) const
{
    if (!point.IsFinite() || controlPointIndex >= controlPoints_.size()) {
        throw std::invalid_argument("Wire control point move is invalid.");
    }

    std::vector<Vector3> controls = controlPoints_;
    controls[controlPointIndex] = point;
    switch (kind_) {
    case WireKind::Line:
        return Line(controls[0], controls[1]);
    case WireKind::Polyline:
        return Polyline(std::move(controls));
    case WireKind::CubicBezier:
        return CubicBezier(controls[0], controls[1], controls[2], controls[3]);
    case WireKind::CubicBSpline:
        return CubicBSplineWithKnots(std::move(controls), bsplineKnots_);
    case WireKind::Circle:
    case WireKind::CircularArc:
        break;
    }

    if (controlPointIndex == 0) {
        return Translated(point - arcCenter_);
    }

    const Vector3 normal = Cross(arcUAxis_, arcVAxis_).Normalized();
    const Vector3 radial = point - arcCenter_ - normal * Dot(point - arcCenter_, normal);
    if (radial.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Arc handle must remain away from its center.");
    }
    if (kind_ == WireKind::Circle) {
        const double radius = radial.Length();
        const Vector3 uAxis = radial / radius;
        return Circle(arcCenter_, uAxis, Cross(normal, uAxis), radius);
    }

    const double handleAngle = std::atan2(Dot(radial, arcVAxis_), Dot(radial, arcUAxis_));
    if (controlPointIndex == 1) {
        return CircularArc(
            arcCenter_, arcUAxis_, arcVAxis_, arcRadius_,
            handleAngle, arcSweepAngleRadians_);
    }
    if (controlPointIndex == 2) {
        constexpr double twoPi = std::numbers::pi * 2.0;
        double sweep = handleAngle - arcStartAngleRadians_;
        if (arcSweepAngleRadians_ > 0.0) {
            while (sweep <= 1.0e-9) {
                sweep += twoPi;
            }
            sweep = std::min(sweep, twoPi - 1.0e-6);
        } else {
            while (sweep >= -1.0e-9) {
                sweep -= twoPi;
            }
            sweep = std::max(sweep, -twoPi + 1.0e-6);
        }
        return CircularArc(
            arcCenter_, arcUAxis_, arcVAxis_, arcRadius_,
            arcStartAngleRadians_, sweep);
    }
    throw std::invalid_argument("Arc control point move is invalid.");
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

    case WireKind::CubicBSpline:
        return EvaluateCubicBSpline(controlPoints_, bsplineKnots_, clamped);

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

    if (kind_ == WireKind::CubicBSpline) {
        return {kind_, std::move(translatedPoints), bsplineKnots_};
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

    if (kind_ == WireKind::CubicBSpline) {
        return {kind_, std::move(mirroredPoints), bsplineKnots_};
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
    if (kind_ == WireKind::CubicBSpline) {
        return {kind_, std::move(rotatedPoints), bsplineKnots_};
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
    case WireKind::CubicBSpline: {
        std::vector<Vector3> points = controlPoints_;
        std::reverse(points.begin(), points.end());
        std::vector<double> knots;
        knots.reserve(bsplineKnots_.size());
        for (auto iterator = bsplineKnots_.rbegin(); iterator != bsplineKnots_.rend(); ++iterator) {
            knots.push_back(1.0 - *iterator);
        }
        return CubicBSplineWithKnots(std::move(points), std::move(knots));
    }
    case WireKind::Circle:
        // 逆回り: 始点(シーム)は同じまま巻き方向だけ反転する。
        // (以前は同じ円を返しており、円どうしのルールド・ロフトで
        //  巻きの食い違いを直せず面がねじれていた。)
        return Circle(arcCenter_, arcUAxis_, arcVAxis_ * -1.0, arcRadius_);
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
    case WireKind::CubicBSpline: {
        KnotInsertionResult inserted{controlPoints_, bsplineKnots_};
        while (KnotMultiplicity(inserted.knots, parameter) < CubicDegree) {
            inserted = InsertCubicKnotOnce(
                inserted.controlPoints, inserted.knots, parameter);
        }
        const auto firstKnot = std::find_if(
            inserted.knots.begin(), inserted.knots.end(), [&](double knot) {
                return std::abs(knot - parameter) <= 1.0e-12;
            });
        if (firstKnot == inserted.knots.end()) {
            throw std::logic_error("Inserted B-spline knot was not found.");
        }
        const std::size_t firstKnotIndex = static_cast<std::size_t>(
            std::distance(inserted.knots.begin(), firstKnot));
        const std::size_t splitControlIndex = firstKnotIndex - 1;
        const auto lastKnot = std::find_if(
            firstKnot, inserted.knots.end(), [&](double knot) {
                return std::abs(knot - parameter) > 1.0e-12;
            });

        std::vector<Vector3> firstControls(
            inserted.controlPoints.begin(),
            inserted.controlPoints.begin() + splitControlIndex + 1);
        std::vector<Vector3> secondControls(
            inserted.controlPoints.begin() + splitControlIndex,
            inserted.controlPoints.end());
        std::vector<double> firstKnots(
            inserted.knots.begin(), inserted.knots.begin() + firstKnotIndex);
        firstKnots.insert(firstKnots.end(), CubicDegree + 1, parameter);
        std::vector<double> secondKnots(CubicDegree + 1, parameter);
        secondKnots.insert(secondKnots.end(), lastKnot, inserted.knots.end());
        return {
            CubicBSplineWithKnots(std::move(firstControls), std::move(firstKnots)),
            CubicBSplineWithKnots(std::move(secondControls), std::move(secondKnots)),
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
