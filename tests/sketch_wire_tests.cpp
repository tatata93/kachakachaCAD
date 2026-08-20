#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/model/Sketch.h"
#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::model::Sketch;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;
using kachakacha::model::WorkPlane;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
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

void DirectLineWireUsesExactEndpoints()
{
    const Wire wire = Wire::Line({0.0, 0.0, 0.0}, {2.0, 7.0, 4.0});

    Require(wire.Kind() == WireKind::Line, "line kind");
    RequireNear(wire.Start(), {0.0, 0.0, 0.0}, "line start");
    RequireNear(wire.End(), {2.0, 7.0, 4.0}, "line end");
    RequireNear(wire.Evaluate(0.5), {1.0, 3.5, 2.0}, "line midpoint");
}

void CubicBezierWireUsesControlPoints()
{
    const Wire wire = Wire::CubicBezier(
        {0.0, 0.0, 0.0},
        {0.0, 3.0, 0.0},
        {3.0, 3.0, 0.0},
        {3.0, 0.0, 0.0});

    Require(wire.Kind() == WireKind::CubicBezier, "bezier kind");
    RequireNear(wire.Start(), {0.0, 0.0, 0.0}, "bezier start");
    RequireNear(wire.End(), {3.0, 0.0, 0.0}, "bezier end");
    RequireNear(wire.Evaluate(0.5), {1.5, 2.25, 0.0}, "bezier midpoint");
}

void SketchLineOnWorkPlaneBecomesWorldWire()
{
    const WorkPlane plane = WorkPlane::FromPointNormal(
        {10.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    const Sketch sketch(plane);
    const Wire wire = sketch.MakeLine({2.0, 3.0}, {5.0, 7.0});

    RequireNear(wire.Start(), {10.0, 2.0, 3.0}, "sketch line start");
    RequireNear(wire.End(), {10.0, 5.0, 7.0}, "sketch line end");
}

void SketchBezierOnWorkPlaneBecomesWorldWire()
{
    const WorkPlane plane = WorkPlane::FromThreePoints(
        {0.0, 0.0, 2.0},
        {1.0, 0.0, 2.0},
        {0.0, 1.0, 2.0});

    const Sketch sketch(plane);
    const Wire wire = sketch.MakeCubicBezier(
        {0.0, 0.0},
        {0.0, 1.0},
        {2.0, 1.0},
        {2.0, 0.0});

    RequireNear(wire.Start(), {0.0, 0.0, 2.0}, "sketch bezier start");
    RequireNear(wire.ControlPoints()[1], {0.0, 1.0, 2.0}, "sketch bezier control 1");
    RequireNear(wire.ControlPoints()[2], {2.0, 1.0, 2.0}, "sketch bezier control 2");
    RequireNear(wire.End(), {2.0, 0.0, 2.0}, "sketch bezier end");
}

} // namespace

int main()
{
    try {
        DirectLineWireUsesExactEndpoints();
        CubicBezierWireUsesControlPoints();
        SketchLineOnWorkPlaneBecomesWorldWire();
        SketchBezierOnWorkPlaneBecomesWorldWire();
    } catch (const std::exception& error) {
        std::cerr << "sketch_wire_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "sketch_wire_tests passed\n";
    return 0;
}

