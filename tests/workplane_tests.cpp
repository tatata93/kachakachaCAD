#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/model/WorkPlane.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::model::WorkPlane;

namespace {

constexpr double Pi = 3.14159265358979323846;

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireNear(double actual, double expected, const char* message)
{
    if (!AlmostEqual(actual, expected, 1.0e-9)) {
        std::cerr << message << ": actual=" << actual << " expected=" << expected << '\n';
        throw std::runtime_error(message);
    }
}

void RequireNear(const Vector3& actual, const Vector3& expected, const char* message)
{
    if (!AlmostEqual(actual, expected, 1.0e-9)) {
        std::cerr << message
                  << ": actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ")"
                  << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ")\n";
        throw std::runtime_error(message);
    }
}

void ThreePointPlaneUsesSelectedPoints()
{
    const WorkPlane plane = WorkPlane::FromThreePoints(
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    RequireNear(plane.Origin(), {0.0, 0.0, 0.0}, "three point origin");
    RequireNear(plane.UAxis(), {1.0, 0.0, 0.0}, "three point u axis");
    RequireNear(plane.VAxis(), {0.0, 1.0, 0.0}, "three point v axis");
    RequireNear(plane.Normal(), {0.0, 0.0, 1.0}, "three point normal");

    const auto projected = plane.Project({0.25, 0.5, 0.0});
    RequireNear(projected.u, 0.25, "projected u");
    RequireNear(projected.v, 0.5, "projected v");
    RequireNear(projected.w, 0.0, "projected w");
}

void OffsetMovesAlongNormal()
{
    const WorkPlane base = WorkPlane::FromThreePoints(
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    const WorkPlane shifted = base.Offset(2.5);
    RequireNear(shifted.Origin(), {0.0, 0.0, 2.5}, "offset origin");
    RequireNear(shifted.Normal(), {0.0, 0.0, 1.0}, "offset normal");
}

void RotationAroundEdgeKeepsAxisAndTiltsPlane()
{
    const WorkPlane base = WorkPlane::FromThreePoints(
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    const WorkPlane rotated = base.RotateAroundAxis(
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        Pi / 2.0);

    RequireNear(rotated.UAxis(), {1.0, 0.0, 0.0}, "rotated u axis");
    RequireNear(rotated.VAxis(), {0.0, 0.0, 1.0}, "rotated v axis");
    RequireNear(rotated.Normal(), {0.0, -1.0, 0.0}, "rotated normal");
}

void PointNormalPlaneProjectsAndRestoresCoordinates()
{
    const WorkPlane plane = WorkPlane::FromPointNormal(
        {10.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    const Vector3 world = plane.ToWorld(2.0, 3.0);
    RequireNear(world, {10.0, 2.0, 3.0}, "to world");

    const auto local = plane.Project(world);
    RequireNear(local.u, 2.0, "roundtrip u");
    RequireNear(local.v, 3.0, "roundtrip v");
    RequireNear(local.w, 0.0, "roundtrip w");
}

void CollinearThreePointPlaneIsRejected()
{
    bool rejected = false;
    try {
        (void)WorkPlane::FromThreePoints(
            {0.0, 0.0, 0.0},
            {1.0, 1.0, 1.0},
            {2.0, 2.0, 2.0});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    Require(rejected, "collinear points should be rejected");
}

} // namespace

int main()
{
    try {
        ThreePointPlaneUsesSelectedPoints();
        OffsetMovesAlongNormal();
        RotationAroundEdgeKeepsAxisAndTiltsPlane();
        PointNormalPlaneProjectsAndRestoresCoordinates();
        CollinearThreePointPlaneIsRejected();
    } catch (const std::exception& error) {
        std::cerr << "workplane_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "workplane_tests passed\n";
    return 0;
}

