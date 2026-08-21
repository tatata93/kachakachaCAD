#pragma once

#include "kachakacha/geometry/Vector3.h"

#include <vector>

namespace kachakacha::model {

enum class WireKind {
    Line,
    Polyline,
    CubicBezier,
    Circle,
    CircularArc,
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

    [[nodiscard]] WireKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const std::vector<geometry::Vector3>& ControlPoints() const noexcept { return controlPoints_; }
    [[nodiscard]] geometry::Vector3 Start() const { return Evaluate(0.0); }
    [[nodiscard]] geometry::Vector3 End() const { return Evaluate(1.0); }

    [[nodiscard]] geometry::Vector3 Evaluate(double t) const;
    [[nodiscard]] Wire Translated(geometry::Vector3 delta) const;
    [[nodiscard]] bool IsClosed(double epsilon = 1.0e-9) const noexcept;

private:
    Wire(WireKind kind, std::vector<geometry::Vector3> controlPoints);
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
    geometry::Vector3 arcCenter_;
    geometry::Vector3 arcUAxis_ = {1.0, 0.0, 0.0};
    geometry::Vector3 arcVAxis_ = {0.0, 1.0, 0.0};
    double arcRadius_ = 0.0;
    double arcStartAngleRadians_ = 0.0;
    double arcSweepAngleRadians_ = 0.0;
};

} // namespace kachakacha::model
