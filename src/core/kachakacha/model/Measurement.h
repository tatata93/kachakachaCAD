#pragma once

#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <optional>

namespace kachakacha::model {

struct DistanceMeasurement {
    geometry::Vector3 firstPoint;
    geometry::Vector3 secondPoint;
    double distanceMillimeters = 0.0;
};

struct AngleMeasurement {
    double directedDegrees = 0.0;
    double acuteDegrees = 0.0;
};

[[nodiscard]] double MeasureWireLength(const Wire& wire, double tolerance = 1.0e-5);
[[nodiscard]] std::optional<double> MeasureWireRadius(const Wire& wire);
[[nodiscard]] geometry::Vector3 MeasureWireTangent(const Wire& wire, double parameter);
[[nodiscard]] AngleMeasurement MeasureDirectionsAngle(
    geometry::Vector3 firstDirection,
    geometry::Vector3 secondDirection);
[[nodiscard]] DistanceMeasurement MeasurePointToWireDistance(
    geometry::Vector3 point,
    const Wire& wire,
    double tolerance = 1.0e-3);
[[nodiscard]] DistanceMeasurement MeasureWireToWireDistance(
    const Wire& first,
    const Wire& second,
    double tolerance = 1.0e-3);
[[nodiscard]] double MeasureDirectionToPlaneAngleDegrees(
    geometry::Vector3 direction,
    const WorkPlane& plane);
[[nodiscard]] double MeasurePlaneToPlaneAngleDegrees(
    const WorkPlane& first,
    const WorkPlane& second);
[[nodiscard]] double MeasureSignedPointToPlaneDistance(
    geometry::Vector3 point,
    const WorkPlane& plane);

} // namespace kachakacha::model
