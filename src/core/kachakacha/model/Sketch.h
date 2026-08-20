#pragma once

#include "kachakacha/geometry/Vector2.h"
#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

namespace kachakacha::model {

class Sketch {
public:
    explicit Sketch(WorkPlane plane);

    [[nodiscard]] const WorkPlane& Plane() const noexcept { return plane_; }

    [[nodiscard]] Wire MakeLine(
        geometry::Vector2 start,
        geometry::Vector2 end) const;

    [[nodiscard]] Wire MakeCubicBezier(
        geometry::Vector2 start,
        geometry::Vector2 control1,
        geometry::Vector2 control2,
        geometry::Vector2 end) const;

    [[nodiscard]] Wire MakeCircle(
        geometry::Vector2 center,
        double radius) const;

    [[nodiscard]] Wire MakeCircularArc(
        geometry::Vector2 center,
        double radius,
        double startAngleRadians,
        double sweepAngleRadians) const;

private:
    [[nodiscard]] geometry::Vector3 ToWorld(geometry::Vector2 point) const noexcept;

    WorkPlane plane_;
};

} // namespace kachakacha::model
