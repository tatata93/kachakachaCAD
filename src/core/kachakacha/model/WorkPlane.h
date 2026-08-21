#pragma once

#include "kachakacha/geometry/Vector3.h"

namespace kachakacha::model {

struct PlaneCoordinates {
    double u = 0.0;
    double v = 0.0;
    double w = 0.0;
};

class WorkPlane {
public:
    [[nodiscard]] static WorkPlane FromOriginAxes(
        geometry::Vector3 origin,
        geometry::Vector3 uAxisHint,
        geometry::Vector3 normal);

    [[nodiscard]] static WorkPlane FromThreePoints(
        geometry::Vector3 pointA,
        geometry::Vector3 pointB,
        geometry::Vector3 pointC);

    [[nodiscard]] static WorkPlane FromPointNormal(
        geometry::Vector3 origin,
        geometry::Vector3 normal,
        geometry::Vector3 uAxisHint = {1.0, 0.0, 0.0});

    [[nodiscard]] const geometry::Vector3& Origin() const noexcept { return origin_; }
    [[nodiscard]] const geometry::Vector3& UAxis() const noexcept { return uAxis_; }
    [[nodiscard]] const geometry::Vector3& VAxis() const noexcept { return vAxis_; }
    [[nodiscard]] const geometry::Vector3& Normal() const noexcept { return normal_; }

    [[nodiscard]] WorkPlane Offset(double distance) const;
    [[nodiscard]] WorkPlane Translated(geometry::Vector3 delta) const;
    [[nodiscard]] WorkPlane RotateAroundAxis(
        geometry::Vector3 axisPoint,
        geometry::Vector3 axisDirection,
        double angleRadians) const;

    [[nodiscard]] geometry::Vector3 ToWorld(double u, double v, double w = 0.0) const noexcept;
    [[nodiscard]] PlaneCoordinates Project(geometry::Vector3 point) const noexcept;

private:
    WorkPlane(
        geometry::Vector3 origin,
        geometry::Vector3 uAxis,
        geometry::Vector3 vAxis,
        geometry::Vector3 normal);

    geometry::Vector3 origin_;
    geometry::Vector3 uAxis_;
    geometry::Vector3 vAxis_;
    geometry::Vector3 normal_;
};

} // namespace kachakacha::model
