#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/geometry/Vector3.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::WriteProjectScript;
using kachakacha::model::Wire;
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
        polyline3d handrail 0 0 0  1 0 0  1 1 0
        circle3d coupler 0 0 0  1 0 0  0 1 0  2
        arc3d roof_arc 0 0 2.5  1 0 0  0 1 0  1 0 90
        sketch_line window front 2 3  5 7
        sketch_circle light front 4 4  1.25 reference
        sketch_arc rounded_corner front 6 6  0.75 180 90 locked
        sketch_bezier nose roof 0 0  1 3  4 3  5 0 locked
    )");

    const auto project = LoadProjectScript(input, "test");

    Require(project.WorkPlanes().size() == 3, "plane count");
    Require(project.Wires().size() == 8, "wire count");
    RequireNear(project.Wires()[0].wire.End(), {2.0, 7.0, 4.0}, "3d line end");
    Require(project.Wires()[0].metadata.planePolicy == WirePlanePolicy::Free3D, "3d line is free");
    Require(!project.Wires()[0].metadata.sourcePlaneName.has_value(), "3d line has no source plane");
    RequireNear(project.Wires()[1].wire.Evaluate(1.0), {1.0, 1.0, 0.0}, "3d polyline end");
    RequireNear(project.Wires()[2].wire.Evaluate(0.25), {0.0, 2.0, 0.0}, "3d circle quarter");
    RequireNear(project.Wires()[3].wire.End(), {0.0, 1.0, 2.5}, "3d arc end");
    RequireNear(project.Wires()[4].wire.Start(), {10.0, 2.0, 3.0}, "sketch line start");
    RequireNear(project.Wires()[4].wire.End(), {10.0, 5.0, 7.0}, "sketch line end");
    Require(project.Wires()[4].metadata.sourcePlaneName == "front", "sketch line source plane");
    Require(project.Wires()[4].metadata.planePolicy == WirePlanePolicy::ReferenceOnly, "sketch line defaults to reference");
    RequireNear(project.Wires()[5].wire.Start(), {10.0, 5.25, 4.0}, "sketch circle start");
    Require(project.Wires()[5].metadata.planePolicy == WirePlanePolicy::ReferenceOnly, "sketch circle reference");
    RequireNear(project.Wires()[6].wire.End(), {10.0, 6.0, 5.25}, "sketch arc end");
    Require(project.Wires()[6].metadata.planePolicy == WirePlanePolicy::LockedToPlane, "sketch arc can be locked");
    RequireNear(project.Wires()[7].wire.Start(), {0.0, 0.0, 2.5}, "offset sketch bezier start");
    Require(project.Wires()[7].metadata.sourcePlaneName == "roof", "sketch bezier source plane");
    Require(project.Wires()[7].metadata.planePolicy == WirePlanePolicy::LockedToPlane, "sketch bezier can be locked");
}

void LoadsWireMetadataForDirectWires()
{
    std::istringstream input(R"(
        plane_point_normal front 10 0 0  1 0 0  0 1 0
        line3d window 10 2 3  10 5 3
        wire_meta window front locked
    )");

    const auto project = LoadProjectScript(input, "test");

    Require(project.Wires().size() == 1, "metadata wire count");
    Require(project.Wires()[0].metadata.sourcePlaneName == "front", "direct wire source plane");
    Require(project.Wires()[0].metadata.planePolicy == WirePlanePolicy::LockedToPlane, "direct wire policy");
}

void WrittenProjectRoundTrips()
{
    std::istringstream input(R"(
        plane_point_normal front 10 0 0  1 0 0  0 1 0
        line3d bottom 10 2 3  10 5 3
        wire_meta bottom front reference
        polyline3d handrail 0 0 0  1 0 0  1 1 0
        bezier3d nose 0 0 0  1 2 0  3 2 0  4 0 0
        circle3d light 10 3 3  0 1 0  0 0 1  0.5
        arc3d roof 0 0 2  1 0 0  0 1 0  1.25 180 90
        wire_meta roof - locked
    )");

    const auto project = LoadProjectScript(input, "test");

    std::ostringstream output;
    WriteProjectScript(output, project);

    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "roundtrip");

    Require(roundTripped.WorkPlanes().size() == project.WorkPlanes().size(), "roundtrip plane count");
    Require(roundTripped.Wires().size() == project.Wires().size(), "roundtrip wire count");
    RequireNear(roundTripped.Wires()[0].wire.Start(), project.Wires()[0].wire.Start(), "roundtrip line start");
    Require(roundTripped.Wires()[0].metadata.sourcePlaneName == "front", "roundtrip source plane");
    Require(roundTripped.Wires()[0].metadata.planePolicy == WirePlanePolicy::ReferenceOnly, "roundtrip line policy");
    RequireNear(roundTripped.Wires()[1].wire.End(), project.Wires()[1].wire.End(), "roundtrip polyline end");
    RequireNear(roundTripped.Wires()[2].wire.Evaluate(0.5), project.Wires()[2].wire.Evaluate(0.5), "roundtrip bezier");
    RequireNear(roundTripped.Wires()[3].wire.Evaluate(0.25), project.Wires()[3].wire.Evaluate(0.25), "roundtrip circle");
    RequireNear(roundTripped.Wires()[4].wire.End(), project.Wires()[4].wire.End(), "roundtrip arc");
    Require(roundTripped.Wires()[4].metadata.planePolicy == WirePlanePolicy::LockedToPlane, "roundtrip arc policy");
}

void SurfacesAndProjectedWiresRoundTrip()
{
    std::istringstream input(R"(
        plane_point_normal plan 0 0 12  0 0 1  1 0 0
        bezier3d section_a 0 -6 0  0 -2 3  0 2 3  0 6 0
        bezier3d section_b 12 -6 0  12 -2 5  12 2 5  12 6 0
        sketch_circle light_plan plan 6 0 1.25 reference
        surface_ruled nose_skin section_a section_b
        wire_project light_on_skin light_plan nose_skin 0 0 -1
    )");
    auto project = LoadProjectScript(input, "surface-test");
    Require(project.Surfaces().size() == 1, "surface count");
    Require(project.Wires().size() == 4, "projected wire count");
    Require(project.Wires()[3].projection.has_value(), "projection relation exists");
    Require(project.Wires()[3].wire.IsClosed(), "projected light remains closed");

    const Vector3 beforeUpdate = project.Wires()[3].wire.Evaluate(0.25);
    project.UpdateWire("section_b", Wire::CubicBezier(
        {12.0, -6.0, 0.0}, {12.0, -2.0, 7.0}, {12.0, 2.0, 7.0}, {12.0, 6.0, 0.0}));
    Require(!AlmostEqual(beforeUpdate, project.Wires()[3].wire.Evaluate(0.25), 1.0e-6), "projection rebuilds after section edit");

    const Vector3 sectionBeforeRejectedEdit = project.Wires()[1].wire.Start();
    const Vector3 projectionBeforeRejectedEdit = project.Wires()[3].wire.Evaluate(0.25);
    bool invalidEditRejected = false;
    try {
        project.UpdateWire("section_b", Wire::CubicBezier(
            {0.0, -6.0, 0.0}, {0.0, -2.0, 3.0}, {0.0, 2.0, 3.0}, {0.0, 6.0, 0.0}));
    } catch (const std::invalid_argument&) {
        invalidEditRejected = true;
    }
    Require(invalidEditRejected, "invalid dependent edit is rejected");
    RequireNear(project.Wires()[1].wire.Start(), sectionBeforeRejectedEdit, "rejected edit keeps source section");
    RequireNear(project.Wires()[3].wire.Evaluate(0.25), projectionBeforeRejectedEdit, "rejected edit keeps projection");

    std::ostringstream output;
    WriteProjectScript(output, project);
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "surface-roundtrip");
    Require(roundTripped.Surfaces().size() == 1, "roundtrip surface count");
    Require(roundTripped.Wires().size() == 4, "roundtrip projected wire count");
    Require(roundTripped.Wires()[3].projection->sourceWireName == "light_plan", "roundtrip projection source");
    RequireNear(roundTripped.Wires()[3].wire.Evaluate(0.5), project.Wires()[3].wire.Evaluate(0.5), "roundtrip projected geometry");
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

void RemovingPlaneKeepsWireIn3D()
{
    std::istringstream input(R"(
        plane_point_normal front 10 0 0  1 0 0  0 1 0
        sketch_line window front 2 3  5 7 locked
    )");
    auto project = LoadProjectScript(input, "test");

    Require(project.RemoveWorkPlane("front"), "plane removal");
    Require(project.WorkPlanes().empty(), "plane was removed");
    Require(project.Wires().size() == 1, "wire remains after plane removal");
    Require(!project.Wires()[0].metadata.sourcePlaneName.has_value(), "removed source reference is cleared");
    RequireNear(project.Wires()[0].wire.Start(), {10.0, 2.0, 3.0}, "remaining wire keeps 3d position");
    Require(project.RemoveWire("window"), "wire removal");
    Require(project.Wires().empty(), "wire was removed");
}

void UpdatingPlaneMovesOnlyLockedWires()
{
    std::istringstream input(R"(
        plane_point_normal front 10 0 0  1 0 0  0 1 0
        sketch_line locked_line front 2 3  5 7 locked
        sketch_line reference_line front 2 3  5 7 reference
        sketch_circle locked_circle front 4 4  1.25 locked
    )");
    auto project = LoadProjectScript(input, "test");
    const auto movedPlane = project.FindWorkPlane("front")->Translated({3.0, 4.0, 5.0});

    project.UpdateWorkPlane("front", movedPlane);

    RequireNear(project.Wires()[0].wire.Start(), {13.0, 6.0, 8.0}, "locked line follows plane");
    RequireNear(project.Wires()[1].wire.Start(), {10.0, 2.0, 3.0}, "reference line stays in 3d");
    RequireNear(project.Wires()[2].wire.ArcData().center, {13.0, 8.0, 9.0}, "locked circle follows plane");

    project.UpdateWire("reference_line", Wire::Line({0.0, 0.0, 0.0}, {2.0, 7.0, 4.0}));
    RequireNear(project.Wires()[1].wire.End(), {2.0, 7.0, 4.0}, "wire update replaces geometry");
}

} // namespace

int main()
{
    try {
        LoadsPlanesAndWires();
        LoadsWireMetadataForDirectWires();
        WrittenProjectRoundTrips();
        SurfacesAndProjectedWiresRoundTrip();
        UnknownPlaneIsRejected();
        RemovingPlaneKeepsWireIn3D();
        UpdatingPlaneMovesOnlyLockedWires();
    } catch (const std::exception& error) {
        std::cerr << "project_script_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "project_script_tests passed\n";
    return 0;
}
