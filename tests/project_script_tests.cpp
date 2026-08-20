#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/geometry/Vector3.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::io::LoadProjectScript;
using kachakacha::model::WirePlanePolicy;

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

void LoadsPlanesAndWires()
{
    std::istringstream input(R"(
        plane_three base 0 0 0  1 0 0  0 1 0
        plane_offset roof base 2.5
        plane_point_normal front 10 0 0  1 0 0  0 1 0
        line3d diagonal 0 0 0  2 7 4
        sketch_line window front 2 3  5 7
        sketch_bezier nose roof 0 0  1 3  4 3  5 0 locked
    )");

    const auto project = LoadProjectScript(input, "test");

    Require(project.WorkPlanes().size() == 3, "plane count");
    Require(project.Wires().size() == 3, "wire count");
    RequireNear(project.Wires()[0].wire.End(), {2.0, 7.0, 4.0}, "3d line end");
    Require(project.Wires()[0].metadata.planePolicy == WirePlanePolicy::Free3D, "3d line is free");
    Require(!project.Wires()[0].metadata.sourcePlaneName.has_value(), "3d line has no source plane");
    RequireNear(project.Wires()[1].wire.Start(), {10.0, 2.0, 3.0}, "sketch line start");
    RequireNear(project.Wires()[1].wire.End(), {10.0, 5.0, 7.0}, "sketch line end");
    Require(project.Wires()[1].metadata.sourcePlaneName == "front", "sketch line source plane");
    Require(project.Wires()[1].metadata.planePolicy == WirePlanePolicy::ReferenceOnly, "sketch line defaults to reference");
    RequireNear(project.Wires()[2].wire.Start(), {0.0, 0.0, 2.5}, "offset sketch bezier start");
    Require(project.Wires()[2].metadata.sourcePlaneName == "roof", "sketch bezier source plane");
    Require(project.Wires()[2].metadata.planePolicy == WirePlanePolicy::LockedToPlane, "sketch bezier can be locked");
}

void UnknownPlaneIsRejected()
{
    std::istringstream input("sketch_line bad missing 0 0 1 1");

    bool rejected = false;
    try {
        (void)LoadProjectScript(input, "test");
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    Require(rejected, "unknown plane should be rejected");
}

} // namespace

int main()
{
    try {
        LoadsPlanesAndWires();
        UnknownPlaneIsRejected();
    } catch (const std::exception& error) {
        std::cerr << "project_script_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "project_script_tests passed\n";
    return 0;
}
