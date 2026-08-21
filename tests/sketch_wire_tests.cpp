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

constexpr double Pi = 3.14159265358979323846;

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

void CircleWireEvaluatesOnPlane()
{
    const Wire wire = Wire::Circle(
        {1.0, 2.0, 3.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        2.0);

    Require(wire.Kind() == WireKind::Circle, "circle kind");
    Require(wire.IsClosed(), "circle is closed");
    RequireNear(wire.Evaluate(0.0), {1.0, 4.0, 3.0}, "circle start");
    RequireNear(wire.Evaluate(0.25), {1.0, 2.0, 5.0}, "circle quarter");
    RequireNear(wire.Evaluate(0.5), {1.0, 0.0, 3.0}, "circle half");
}

void SketchArcOnWorkPlaneBecomesWorldWire()
{
    const WorkPlane plane = WorkPlane::FromPointNormal(
        {10.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    const Sketch sketch(plane);
    const Wire wire = sketch.MakeCircularArc({2.0, 3.0}, 1.5, 0.0, Pi / 2.0);

    Require(wire.Kind() == WireKind::CircularArc, "arc kind");
    Require(!wire.IsClosed(), "arc is open");
    RequireNear(wire.Start(), {10.0, 3.5, 3.0}, "arc start");
    RequireNear(wire.End(), {10.0, 2.0, 4.5}, "arc end");
}

void TranslatedLineKeepsShape()
{
    const Wire wire = Wire::Line({1.0, 2.0, 3.0}, {4.0, 6.0, 8.0});
    const Wire moved = wire.Translated({10.0, -2.0, 0.5});

    Require(moved.Kind() == WireKind::Line, "translated line kind");
    RequireNear(moved.Start(), {11.0, 0.0, 3.5}, "translated line start");
    RequireNear(moved.End(), {14.0, 4.0, 8.5}, "translated line end");
    RequireNear(moved.Evaluate(0.5), {12.5, 2.0, 6.0}, "translated line midpoint");
}

void TranslatedArcKeepsFrameAndRadius()
{
    const Wire wire = Wire::CircularArc(
        {1.0, 2.0, 3.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        2.0,
        0.0,
        Pi / 2.0);
    const Wire moved = wire.Translated({5.0, 0.5, -1.0});

    Require(moved.Kind() == WireKind::CircularArc, "translated arc kind");
    RequireNear(moved.Start(), {6.0, 4.5, 2.0}, "translated arc start");
    RequireNear(moved.End(), {6.0, 2.5, 4.0}, "translated arc end");
}

} // namespace

int main()
{
    try {
        DirectLineWireUsesExactEndpoints();
        CubicBezierWireUsesControlPoints();
        SketchLineOnWorkPlaneBecomesWorldWire();
        SketchBezierOnWorkPlaneBecomesWorldWire();
        CircleWireEvaluatesOnPlane();
        SketchArcOnWorkPlaneBecomesWorldWire();
        TranslatedLineKeepsShape();
        TranslatedArcKeepsFrameAndRadius();
    } catch (const std::exception& error) {
        std::cerr << "sketch_wire_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "sketch_wire_tests passed\n";
    return 0;
}
