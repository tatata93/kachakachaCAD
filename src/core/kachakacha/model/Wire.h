#pragma once

#include "kachakacha/geometry/Vector3.h"

#include <vector>

namespace kachakacha::model {

enum class WireKind {
    Line,
    Polyline,
    CubicBezier,
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

    [[nodiscard]] WireKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const std::vector<geometry::Vector3>& ControlPoints() const noexcept { return controlPoints_; }
    [[nodiscard]] const geometry::Vector3& Start() const noexcept { return controlPoints_.front(); }
    [[nodiscard]] const geometry::Vector3& End() const noexcept { return controlPoints_.back(); }

    [[nodiscard]] geometry::Vector3 Evaluate(double t) const;
    [[nodiscard]] bool IsClosed(double epsilon = 1.0e-9) const noexcept;

private:
    Wire(WireKind kind, std::vector<geometry::Vector3> controlPoints);

    WireKind kind_;
    std::vector<geometry::Vector3> controlPoints_;
};

} // namespace kachakacha::model

