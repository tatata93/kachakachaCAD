#include "kachakacha/model/Measurement.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::model::MeasureDirectionToPlaneAngleDegrees;
using kachakacha::model::MeasureDirectionsAngle;
using kachakacha::model::MeasurePlaneToPlaneAngleDegrees;
using kachakacha::model::MeasurePointToWireDistance;
using kachakacha::model::MeasureSignedPointToPlaneDistance;
using kachakacha::model::MeasureWireLength;
using kachakacha::model::MeasureWireRadius;
using kachakacha::model::MeasureWireTangent;
using kachakacha::model::MeasureWireToWireDistance;
using kachakacha::model::Wire;
using kachakacha::model::WorkPlane;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireNear(double actual, double expected, double tolerance, const char* message)
{
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void MeasuresWireDimensions()
{
    RequireNear(MeasureWireLength(Wire::Line({}, {3.0, 4.0, 12.0})), 13.0, 1.0e-9, "line length");
    RequireNear(
        MeasureWireLength(Wire::Polyline({{0.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {3.0, 4.0, 0.0}})),
        7.0,
        1.0e-9,
        "polyline length");
    const Wire circle = Wire::Circle({}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 5.0);
    RequireNear(MeasureWireLength(circle), 10.0 * 3.14159265358979323846, 1.0e-9, "circle length");
    Require(MeasureWireRadius(circle) == 5.0, "circle radius");
    Require(!MeasureWireRadius(Wire::Line({}, {1.0, 0.0, 0.0})).has_value(), "line has no radius");

    const Wire straightBezier = Wire::CubicBezier(
        {0.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {7.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    RequireNear(MeasureWireLength(straightBezier), 10.0, 1.0e-6, "Bezier length");

    const Wire duplicatePointPolyline = Wire::Polyline(
        {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 5.0, 0.0}});
    Require(
        AlmostEqual(MeasureWireTangent(duplicatePointPolyline, 0.0), {0.0, 1.0, 0.0}, 1.0e-9),
        "polyline tangent skips duplicate points");
}

void MeasuresDistancesAndAngles()
{
    const Wire first = Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const Wire second = Wire::Line({3.0, 4.0, 5.0}, {3.0, 9.0, 5.0});
    const auto distance = MeasureWireToWireDistance(first, second);
    Require(AlmostEqual(distance.firstPoint, {3.0, 0.0, 0.0}, 1.0e-8), "wire distance first point");
    Require(AlmostEqual(distance.secondPoint, {3.0, 4.0, 5.0}, 1.0e-8), "wire distance second point");
    RequireNear(distance.distanceMillimeters, std::sqrt(41.0), 1.0e-8, "wire distance");

    const auto rightAngle = MeasureDirectionsAngle(
        MeasureWireTangent(first, 0.5),
        MeasureWireTangent(second, 0.5));
    RequireNear(rightAngle.directedDegrees, 90.0, 1.0e-9, "directed line angle");
    RequireNear(rightAngle.acuteDegrees, 90.0, 1.0e-9, "acute line angle");

    const Wire circle = Wire::Circle({}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 5.0);
    const auto pointDistance = MeasurePointToWireDistance({8.0, 0.0, 0.0}, circle, 1.0e-4);
    RequireNear(pointDistance.distanceMillimeters, 3.0, 2.0e-4, "point to circle distance");
    Require(AlmostEqual(MeasureWireTangent(circle, 0.0), {0.0, 1.0, 0.0}, 1.0e-9), "circle tangent");
}

void MeasuresPlaneRelations()
{
    const WorkPlane xy = WorkPlane::FromPointNormal({}, {0.0, 0.0, 1.0});
    const WorkPlane yz = WorkPlane::FromPointNormal({}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    RequireNear(MeasureDirectionToPlaneAngleDegrees({1.0, 0.0, 0.0}, xy), 0.0, 1.0e-9, "line parallel to plane");
    RequireNear(MeasureDirectionToPlaneAngleDegrees({0.0, 0.0, 1.0}, xy), 90.0, 1.0e-9, "line normal to plane");
    RequireNear(MeasurePlaneToPlaneAngleDegrees(xy, yz), 90.0, 1.0e-9, "plane angle");
    RequireNear(MeasureSignedPointToPlaneDistance({0.0, 0.0, 7.5}, xy), 7.5, 1.0e-9, "point plane distance");
}

} // namespace

int main()
{
    try {
        MeasuresWireDimensions();
        MeasuresDistancesAndAngles();
        MeasuresPlaneRelations();
    } catch (const std::exception& error) {
        std::cerr << "measurement_tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "measurement_tests passed\n";
    return 0;
}
