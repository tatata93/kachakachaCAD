#pragma once

#include "kachakacha/geometry/Vector3.h"

#include <utility>
#include <vector>

namespace kachakacha::model {

enum class WireKind {
    Line,
    Polyline,
    CubicBezier,
    CubicBSpline,
    Circle,
    CircularArc,
};

struct WireArcData {
    geometry::Vector3 center;
    geometry::Vector3 uAxis;
    geometry::Vector3 vAxis;
    double radius = 0.0;
    double startAngleRadians = 0.0;
    double sweepAngleRadians = 0.0;
};

class Wire {
public:
    [[nodiscard]] static Wire Line(
        geometry::Vector3 start,
        geometry::Vector3 end);

    [[nodiscard]] static Wire Polyline(
        std::vector<geometry::Vector3> points);

    [[nodiscard]] static Wire CubicBezier(
        geometry::Vector3 start,
        geometry::Vector3 control1,
        geometry::Vector3 control2,
        geometry::Vector3 end);

    [[nodiscard]] static Wire CubicBSpline(
        std::vector<geometry::Vector3> controlPoints);

    [[nodiscard]] static Wire CubicBSplineWithKnots(
        std::vector<geometry::Vector3> controlPoints,
        std::vector<double> knots);

    [[nodiscard]] static Wire InterpolatingCubicBSpline(
        const std::vector<geometry::Vector3>& throughPoints);

    [[nodiscard]] static Wire Circle(
        geometry::Vector3 center,
        geometry::Vector3 uAxisHint,
        geometry::Vector3 vAxisHint,
        double radius);

    [[nodiscard]] static Wire CircularArc(
        geometry::Vector3 center,
        geometry::Vector3 uAxisHint,
        geometry::Vector3 vAxisHint,
        double radius,
        double startAngleRadians,
        double sweepAngleRadians);

    [[nodiscard]] static Wire CircularArcThroughThreePoints(
        geometry::Vector3 start,
        geometry::Vector3 through,
        geometry::Vector3 end);

    [[nodiscard]] static Wire CircularArcFromEndpointsRadius(
        geometry::Vector3 start,
        geometry::Vector3 end,
        geometry::Vector3 planeNormal,
        double radius,
        bool bulgeLeft);

    [[nodiscard]] static Wire CircularArcFromStartTangent(
        geometry::Vector3 start,
        geometry::Vector3 tangentDirection,
        geometry::Vector3 planeNormal,
        double radius,
        double sweepAngleRadians);

    [[nodiscard]] WireKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const std::vector<geometry::Vector3>& ControlPoints() const noexcept { return controlPoints_; }
    [[nodiscard]] const std::vector<double>& BSplineKnots() const;
    [[nodiscard]] geometry::Vector3 Start() const { return Evaluate(0.0); }
    [[nodiscard]] geometry::Vector3 End() const { return Evaluate(1.0); }

    [[nodiscard]] WireArcData ArcData() const;
    [[nodiscard]] geometry::Vector3 Evaluate(double t) const;
    [[nodiscard]] Wire WithMovedControlPoint(
        std::size_t controlPointIndex,
        geometry::Vector3 point) const;
    [[nodiscard]] Wire Translated(geometry::Vector3 delta) const;
    [[nodiscard]] Wire Mirrored(
        geometry::Vector3 linePoint,
        geometry::Vector3 lineDirection,
        geometry::Vector3 planeNormal) const;
    [[nodiscard]] Wire RotatedAroundAxis(
        geometry::Vector3 axisPoint,
        geometry::Vector3 axisDirection,
        double angleRadians) const;
    [[nodiscard]] Wire Reversed() const;
    [[nodiscard]] std::pair<Wire, Wire> SplitAt(double parameter) const;
    [[nodiscard]] bool IsClosed(double epsilon = 1.0e-9) const noexcept;

private:
    Wire(WireKind kind, std::vector<geometry::Vector3> controlPoints);
    Wire(
        WireKind kind,
        std::vector<geometry::Vector3> controlPoints,
        std::vector<double> bsplineKnots);
    Wire(
        WireKind kind,
        std::vector<geometry::Vector3> controlPoints,
        geometry::Vector3 arcCenter,
        geometry::Vector3 arcUAxis,
        geometry::Vector3 arcVAxis,
        double arcRadius,
        double arcStartAngleRadians,
        double arcSweepAngleRadians);

    WireKind kind_;
    std::vector<geometry::Vector3> controlPoints_;
    std::vector<double> bsplineKnots_;
    geometry::Vector3 arcCenter_;
    geometry::Vector3 arcUAxis_ = {1.0, 0.0, 0.0};
    geometry::Vector3 arcVAxis_ = {0.0, 1.0, 0.0};
    double arcRadius_ = 0.0;
    double arcStartAngleRadians_ = 0.0;
    double arcSweepAngleRadians_ = 0.0;
};

} // namespace kachakacha::model
