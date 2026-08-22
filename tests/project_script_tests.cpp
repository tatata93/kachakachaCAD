#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/geometry/Vector3.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::WriteProjectScript;
using kachakacha::model::PlateSplitAxis;
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

void ConstructionWiresRoundTripAndStayOutOfSurfaces()
{
    std::istringstream input(R"(
        plane_point_normal drawing 0 0 0  0 0 1  1 0 0
        line3d centerline -10 0 0  10 0 0
        wire_meta centerline drawing reference
        wire_role centerline construction
    )");
    auto project = LoadProjectScript(input, "construction-test");
    Require(project.Wires()[0].metadata.construction, "construction role loaded");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("wire_role centerline construction") != std::string::npos,
        "construction role written");
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "construction-roundtrip");
    Require(roundTripped.Wires()[0].metadata.construction, "construction role roundtrip");

    bool rejected = false;
    try {
        project.AddPlanarSurface("invalid", "centerline");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "construction wire rejected as surface source");
}

void CoincidentEndpointsRoundTripAndDriveGeometry()
{
    std::istringstream input(R"(
        line3d anchor 0 0 0  10 0 0
        line3d follower 20 5 0  20 10 0
        line3d tail 30 20 0  30 30 0
        wire_coincident anchor end follower start
        wire_coincident follower end tail start
    )");
    auto project = LoadProjectScript(input, "coincident-test");
    RequireNear(project.Wires()[1].wire.Start(), {10.0, 0.0, 0.0}, "follower initially coincident");
    RequireNear(project.Wires()[2].wire.Start(), {20.0, 10.0, 0.0}, "coincidence chain initially solved");

    project.UpdateWire("anchor", Wire::Line({2.0, 3.0, 0.0}, {12.0, 3.0, 0.0}));
    RequireNear(project.Wires()[1].wire.Start(), {12.0, 3.0, 0.0}, "follower tracks anchor edit");
    RequireNear(project.Wires()[2].wire.Start(), {20.0, 10.0, 0.0}, "tail remains attached to follower end");

    project.UpdateWire("follower", Wire::Line({99.0, 99.0, 0.0}, {25.0, 12.0, 0.0}));
    RequireNear(project.Wires()[1].wire.Start(), {12.0, 3.0, 0.0}, "follower endpoint cannot leave anchor");
    RequireNear(project.Wires()[2].wire.Start(), {25.0, 12.0, 0.0}, "chain follows edited follower end");

    auto conflictingMetadata = project.Wires()[1].metadata;
    conflictingMetadata.lineConstraints.lengthMillimeters = 8.0;
    bool rejectedConflictingDimension = false;
    try {
        project.SetWireMetadata("follower", conflictingMetadata);
    } catch (const std::invalid_argument&) {
        rejectedConflictingDimension = true;
    }
    Require(rejectedConflictingDimension, "follower dimension conflicts are rejected");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("wire_coincident anchor end follower start") != std::string::npos,
        "coincidence written");
    std::istringstream roundTripInput(output.str());
    auto roundTripped = LoadProjectScript(roundTripInput, "coincident-roundtrip");
    Require(roundTripped.CoincidentConstraints().size() == 2, "coincidence roundtrip count");
    RequireNear(roundTripped.Wires()[1].wire.Start(), {12.0, 3.0, 0.0}, "coincidence roundtrip point");

    bool protectedFromRemoval = false;
    try {
        (void)roundTripped.RemoveWire("anchor");
    } catch (const std::invalid_argument&) {
        protectedFromRemoval = true;
    }
    Require(protectedFromRemoval, "coincidence protects referenced wire");
    Require(roundTripped.RemoveWireCoincidentConstraints("anchor") == 1, "remove anchor coincidence");
    Require(roundTripped.RemoveWireCoincidentConstraints("follower") == 1, "remove follower chain coincidence");
    Require(roundTripped.RemoveWire("anchor"), "wire removable after coincidence removal");
}

void TangentEndpointsRoundTripAndDriveBezierHandle()
{
    std::istringstream input(R"(
        line3d anchor 0 0 0  10 0 0
        bezier3d follower 20 2 0  20 5 0  26 4 0  30 0 0
        wire_coincident anchor end follower start
        wire_tangent anchor end follower start
    )");
    auto project = LoadProjectScript(input, "tangent-test");
    Require(project.TangentConstraints().size() == 1, "tangent loaded");
    RequireNear(project.Wires()[1].wire.Start(), {10.0, 0.0, 0.0}, "tangent follower coincident");
    RequireNear(project.Wires()[1].wire.ControlPoints()[1], {13.0, 0.0, 0.0}, "bezier handle tangent to line");

    project.UpdateWire("anchor", Wire::Line({0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}));
    RequireNear(project.Wires()[1].wire.Start(), {0.0, 10.0, 0.0}, "tangent follower tracks anchor point");
    RequireNear(project.Wires()[1].wire.ControlPoints()[1], {0.0, 13.0, 0.0}, "bezier handle tracks anchor direction");

    project.UpdateWire("follower", Wire::CubicBezier(
        {50.0, 50.0, 0.0}, {55.0, 55.0, 0.0}, {26.0, 4.0, 0.0}, {30.0, 0.0, 0.0}));
    RequireNear(project.Wires()[1].wire.Start(), {0.0, 10.0, 0.0}, "edited tangent endpoint remains coincident");
    const Vector3 handle = project.Wires()[1].wire.ControlPoints()[1] - project.Wires()[1].wire.Start();
    Require(std::abs(handle.x) <= 1.0e-9 && handle.y > 0.0, "edited bezier handle remains tangent");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("wire_tangent anchor end follower start") != std::string::npos,
        "tangent written");
    std::istringstream roundTripInput(output.str());
    auto roundTripped = LoadProjectScript(roundTripInput, "tangent-roundtrip");
    Require(roundTripped.TangentConstraints().size() == 1, "tangent roundtrip count");
    Require(roundTripped.RemoveWireTangentConstraints("follower") == 1, "remove tangent only");
    Require(roundTripped.CoincidentConstraints().size() == 1, "coincidence remains after tangent removal");
    Require(roundTripped.RemoveWireCoincidentConstraints("follower") == 1, "remove remaining coincidence");
}

void TangentEndpointsDriveCircularArc()
{
    std::istringstream input(R"(
        line3d anchor 0 0 0  10 0 0
        arc3d follower 20 5 0  1 0 0  0 1 0  3 0 90
        wire_radius_constraint follower 3
        wire_coincident anchor end follower start
        wire_tangent anchor end follower start
    )");
    auto project = LoadProjectScript(input, "arc-tangent-test");
    Require(project.TangentConstraints().size() == 1, "arc tangent loaded");
    RequireNear(project.Wires()[1].wire.Start(), {10.0, 0.0, 0.0},
        "arc tangent follower coincident");
    RequireNear(project.Wires()[1].wire.ArcData().center, {10.0, 3.0, 0.0},
        "arc tangent center aligned");
    RequireNear(project.Wires()[1].wire.End(), {13.0, 3.0, 0.0},
        "arc tangent keeps radius and sweep");

    project.UpdateWire("anchor", Wire::Line({0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}));
    RequireNear(project.Wires()[1].wire.Start(), {0.0, 10.0, 0.0},
        "arc tangent endpoint tracks anchor");
    RequireNear(project.Wires()[1].wire.ArcData().center, {-3.0, 10.0, 0.0},
        "arc tangent rotates around endpoint");
    RequireNear(project.Wires()[1].wire.End(), {-3.0, 13.0, 0.0},
        "rotated arc keeps sweep");

    project.UpdateWire("follower", Wire::CircularArc(
        {40.0, 50.0, 6.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 9.0, 0.5, 1.0));
    RequireNear(project.Wires()[1].wire.Start(), {0.0, 10.0, 0.0},
        "edited arc remains coincident");
    Require(std::abs(project.Wires()[1].wire.ArcData().radius - 3.0) <= 1.0e-12,
        "edited arc keeps fixed radius");
    Require(std::abs(project.Wires()[1].wire.ArcData().sweepAngleRadians - 1.0) <= 1.0e-12,
        "edited arc keeps editable sweep");

    std::ostringstream output;
    WriteProjectScript(output, project);
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "arc-tangent-roundtrip");
    Require(roundTripped.TangentConstraints().size() == 1, "arc tangent roundtrip count");
    RequireNear(roundTripped.Wires()[1].wire.Start(), roundTripped.Wires()[0].wire.End(),
        "arc tangent roundtrip coincidence");

    std::istringstream lockedInput(R"(
        plane_point_normal top 0 0 0  0 0 1  1 0 0
        line3d locked_anchor -10 0 0  0 0 0
        arc3d locked_arc 0 3 0  1 0 0  0 1 0  3 180 90
        wire_meta locked_arc top locked
        wire_coincident locked_anchor end locked_arc start
        wire_tangent locked_anchor end locked_arc start
    )");
    auto lockedProject = LoadProjectScript(lockedInput, "locked-arc-tangent");
    const Wire lockedArcBefore = lockedProject.Wires()[1].wire;
    bool outOfPlaneRejected = false;
    try {
        lockedProject.UpdateWire(
            "locked_anchor", Wire::Line({0.0, 0.0, -10.0}, {0.0, 0.0, 0.0}));
    } catch (const std::invalid_argument&) {
        outOfPlaneRejected = true;
    }
    Require(outOfPlaneRejected, "locked arc rejects out-of-plane tangent");
    RequireNear(lockedProject.Wires()[1].wire.ArcData().center, lockedArcBefore.ArcData().center,
        "rejected locked arc edit is atomic");
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

void LineConstraintsRoundTripAndDriveGeometry()
{
    std::istringstream input(R"(
        plane_point_normal drawing 0 0 0  0 0 1  1 0 0
        line3d bracket 2 3 0  5 7 0
        wire_meta bracket drawing locked
        wire_constraint bracket 10 30
    )");
    auto project = LoadProjectScript(input, "test");

    const auto& constrained = project.Wires()[0];
    Require(constrained.metadata.lineConstraints.lengthMillimeters == 10.0, "fixed length loaded");
    Require(constrained.metadata.lineConstraints.angleDegrees == 30.0, "fixed angle loaded");
    RequireNear(
        constrained.wire.End(),
        {2.0 + 5.0 * std::sqrt(3.0), 8.0, 0.0},
        "constraints drive loaded geometry");

    project.UpdateWire("bracket", Wire::Line({4.0, 6.0, 0.0}, {5.0, 6.0, 0.0}));
    RequireNear(
        project.Wires()[0].wire.End(),
        {4.0 + 5.0 * std::sqrt(3.0), 11.0, 0.0},
        "constraints survive direct geometry edit");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("wire_constraint bracket 10 30") != std::string::npos,
        "constraints written");
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "roundtrip");
    Require(roundTripped.Wires()[0].metadata.lineConstraints.lengthMillimeters == 10.0,
        "fixed length roundtrip");
    Require(roundTripped.Wires()[0].metadata.lineConstraints.angleDegrees == 30.0,
        "fixed angle roundtrip");
}

void RadiusConstraintsRoundTripAndDriveGeometry()
{
    std::istringstream input(R"(
        circle3d lamp 1 2 3  1 0 0  0 1 0  2
        wire_radius_constraint lamp 4.5
        arc3d corner 0 0 0  1 0 0  0 1 0  3 15 120
        wire_radius_constraint corner 6.25
    )");
    auto project = LoadProjectScript(input, "radius-test");
    Require(project.Wires()[0].metadata.curveConstraints.radiusMillimeters == 4.5,
        "circle fixed radius loaded");
    Require(std::abs(project.Wires()[0].wire.ArcData().radius - 4.5) <= 1.0e-12,
        "circle radius constraint drives loaded geometry");
    Require(std::abs(project.Wires()[1].wire.ArcData().radius - 6.25) <= 1.0e-12,
        "arc radius constraint drives loaded geometry");

    project.UpdateWire("lamp", Wire::Circle(
        {8.0, 9.0, 10.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 12.0));
    RequireNear(project.Wires()[0].wire.ArcData().center, {8.0, 9.0, 10.0},
        "radius-constrained circle center remains editable");
    Require(std::abs(project.Wires()[0].wire.ArcData().radius - 4.5) <= 1.0e-12,
        "fixed radius survives direct geometry edit");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("wire_radius_constraint lamp 4.5") != std::string::npos,
        "circle radius constraint written");
    Require(output.str().find("wire_radius_constraint corner 6.25") != std::string::npos,
        "arc radius constraint written");
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "radius-roundtrip");
    Require(roundTripped.Wires()[0].metadata.curveConstraints.radiusMillimeters == 4.5,
        "circle fixed radius roundtrip");
    Require(roundTripped.Wires()[1].metadata.curveConstraints.radiusMillimeters == 6.25,
        "arc fixed radius roundtrip");
}

void ReferenceDimensionsRoundTripAndFollowGeometry()
{
    std::istringstream input(R"(
        plane_point_normal base 0 0 0  0 0 1  1 0 0
        plane_point_normal roof 0 0 5  0 0 1  1 0 0
        line3d rail 0 0 0  10 0 0
        line3d diagonal 0 4 0  10 14 0
        circle3d lamp 4 3 0  1 0 0  0 1 0  2
        reference_dimension point_gap point_distance point 0 3 0 wire rail 0.5
        reference_dimension rail_length wire_length wire rail 0.25 none
        reference_dimension lamp_radius wire_radius wire lamp 0.25 none
        reference_dimension wire_gap wire_distance wire rail 0.5 wire diagonal 0.5
        reference_dimension wire_angle wire_angle wire rail 0.5 wire diagonal 0.5
        reference_dimension point_wire point_wire_distance point 5 3 0 wire rail 0.5
        reference_dimension point_plane point_plane_distance point 2 3 7 plane base
        reference_dimension wire_plane wire_plane_angle wire diagonal 0.5 plane base
        reference_dimension plane_angle plane_angle plane base plane roof
        reference_dimension plane_gap plane_distance plane base plane roof
        visibility dimension lamp_radius hidden
    )");
    auto project = LoadProjectScript(input, "reference-dimension-test");
    Require(project.ReferenceDimensions().size() == 10, "reference dimension count");
    Require(std::abs(project.EvaluateReferenceDimension("rail_length").value - 10.0) <= 1.0e-12,
        "wire length dimension");
    Require(std::abs(project.EvaluateReferenceDimension("lamp_radius").value - 2.0) <= 1.0e-12,
        "wire radius dimension");
    Require(!project.ReferenceDimensions()[2].visible, "dimension visibility loaded");
    Require(std::abs(project.EvaluateReferenceDimension("plane_gap").value - 5.0) <= 1.0e-12,
        "parallel plane distance dimension");
    Require(std::abs(project.EvaluateReferenceDimension("wire_angle").value - 45.0) <= 1.0e-9,
        "wire angle dimension");

    project.UpdateWire("rail", Wire::Line({0.0, 0.0, 0.0}, {20.0, 0.0, 0.0}));
    Require(std::abs(project.EvaluateReferenceDimension("rail_length").value - 20.0) <= 1.0e-12,
        "wire length dimension follows edit");
    Require(std::abs(project.EvaluateReferenceDimension("point_gap").value - std::sqrt(109.0)) <= 1.0e-9,
        "wire point dimension follows edit");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("reference_dimension point_gap point_distance") != std::string::npos,
        "reference dimension written");
    Require(output.str().find("visibility dimension lamp_radius hidden") != std::string::npos,
        "dimension visibility written");
    std::istringstream roundTripInput(output.str());
    auto roundTripped = LoadProjectScript(roundTripInput, "reference-dimension-roundtrip");
    Require(roundTripped.ReferenceDimensions().size() == 10,
        "reference dimension roundtrip count");
    Require(std::abs(roundTripped.EvaluateReferenceDimension("rail_length").value - 20.0) <= 1.0e-12,
        "reference dimension roundtrip value");
    Require(roundTripped.RemoveReferenceDimension("point_gap"), "remove reference dimension");
    Require(roundTripped.RemoveWire("rail"), "remove dimensioned wire");
    Require(std::none_of(
        roundTripped.ReferenceDimensions().begin(), roundTripped.ReferenceDimensions().end(),
        [](const auto& dimension) {
            return (dimension.first.kind == kachakacha::model::DimensionReferenceKind::Wire
                       && dimension.first.objectName == "rail")
                || (dimension.second.kind == kachakacha::model::DimensionReferenceKind::Wire
                    && dimension.second.objectName == "rail");
        }),
        "removing wire removes its reference dimensions");
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
        plate nose_plate nose_skin 0.5 centered styrene
        plate_opening nose_plate light_on_skin
    )");
    auto project = LoadProjectScript(input, "surface-test");
    Require(project.Surfaces().size() == 1, "surface count");
    Require(project.Wires().size() == 4, "projected wire count");
    Require(project.Wires()[3].projection.has_value(), "projection relation exists");
    Require(project.Wires()[3].wire.IsClosed(), "projected light remains closed");
    Require(project.Plates().size() == 1, "plate with opening count");
    Require(project.Plates()[0].openingWireNames == std::vector<std::string>{"light_on_skin"}, "plate opening relation exists");

    bool unprojectedOpeningRejected = false;
    try {
        project.AddPlateOpening("nose_plate", "light_plan");
    } catch (const std::invalid_argument&) {
        unprojectedOpeningRejected = true;
    }
    Require(unprojectedOpeningRejected, "unprojected drawing cannot be used directly as a plate opening");

    bool usedSectionRejected = false;
    try {
        project.RemoveWire("section_a");
    } catch (const std::invalid_argument&) {
        usedSectionRejected = true;
    }
    Require(usedSectionRejected, "surface section wire cannot be deleted while in use");

    bool projectionSourceRejected = false;
    try {
        project.RemoveWire("light_plan");
    } catch (const std::invalid_argument&) {
        projectionSourceRejected = true;
    }
    Require(projectionSourceRejected, "projection source wire cannot be deleted while in use");

    const Vector3 beforeUpdate = project.Wires()[3].wire.Evaluate(0.25);
    project.UpdateWire("section_b", Wire::CubicBezier(
        {12.0, -6.0, 0.0}, {12.0, -2.0, 7.0}, {12.0, 2.0, 7.0}, {12.0, 6.0, 0.0}));
    Require(!AlmostEqual(beforeUpdate, project.Wires()[3].wire.Evaluate(0.25), 1.0e-6), "projection rebuilds after section edit");
    Require(project.Plates()[0].openingWireNames[0] == "light_on_skin", "opening relation survives section edit");

    bool usedOpeningRejected = false;
    try {
        project.RemoveWire("light_on_skin");
    } catch (const std::invalid_argument&) {
        usedOpeningRejected = true;
    }
    Require(usedOpeningRejected, "opening wire cannot be deleted while in use");
    project.RemovePlateOpening("nose_plate", "light_on_skin");
    Require(project.Plates()[0].openingWireNames.empty(), "remove plate opening relation");
    project.AddPlateOpening("nose_plate", "light_on_skin");

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
    Require(roundTripped.Plates().size() == 1, "roundtrip plate with opening");
    Require(roundTripped.Plates()[0].openingWireNames == std::vector<std::string>{"light_on_skin"}, "roundtrip plate opening");
    Require(roundTripped.Wires()[3].projection->sourceWireName == "light_plan", "roundtrip projection source");
    RequireNear(roundTripped.Wires()[3].wire.Evaluate(0.5), project.Wires()[3].wire.Evaluate(0.5), "roundtrip projected geometry");
}

void LoftSurfacesRoundTrip()
{
    std::istringstream input(R"(
        bezier3d section_a 0 -6 0  0 -2 3  0 2 3  0 6 0
        bezier3d section_b 6 -6 0  6 -2 6  6 2 6  6 6 0
        bezier3d section_c 12 -6 0  12 -2 4  12 2 4  12 6 0
        surface_loft carbody section_a section_b section_c
        plate shell carbody 0.5 centered styrene
    )");
    auto project = LoadProjectScript(input, "loft-test");
    Require(project.Surfaces().size() == 1, "loft surface count");
    Require(project.Surfaces()[0].surface.Kind() == kachakacha::model::SurfaceKind::Loft, "loft surface kind");
    Require(project.Surfaces()[0].sourceWireNames.size() == 3, "loft section dependency count");
    Require(project.Plates().size() == 1, "loft plate count");

    const auto platePointBeforeUpdate = project.Plates()[0].plate.SourceSurface().Evaluate(0.5, 0.5);
    project.UpdateWire("section_b", Wire::CubicBezier(
        {6.0, -6.0, 0.0}, {6.0, -2.0, 8.0}, {6.0, 2.0, 8.0}, {6.0, 6.0, 0.0}));
    Require(!kachakacha::geometry::AlmostEqual(
        platePointBeforeUpdate, project.Plates()[0].plate.SourceSurface().Evaluate(0.5, 0.5), 1.0e-8),
        "plate rebuilds after loft section edit");

    project.UpdatePlate("shell", "carbody", 0.7, kachakacha::model::PlateThicknessDirection::Positive, "paper");
    Require(std::abs(project.Plates()[0].plate.Thickness() - 0.7) <= 1.0e-12, "update plate thickness");
    Require(project.Plates()[0].material == "paper", "update plate material");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("surface_loft carbody section_a section_b section_c") != std::string::npos, "write loft command");
    Require(output.str().find("plate shell carbody ") != std::string::npos
            && output.str().find(" positive paper") != std::string::npos,
        "write updated plate command");
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "loft-roundtrip");
    Require(roundTripped.Surfaces()[0].surface.Kind() == kachakacha::model::SurfaceKind::Loft, "roundtrip loft kind");
    Require(roundTripped.Plates().size() == 1, "roundtrip loft plate");
    Require(std::abs(roundTripped.Plates()[0].plate.Thickness() - 0.7) <= 1.0e-12, "roundtrip updated plate thickness");
    Require(roundTripped.Plates()[0].material == "paper", "roundtrip updated plate material");
    RequireNear(roundTripped.Surfaces()[0].surface.Evaluate(0.5, 0.5),
        project.Surfaces()[0].surface.Evaluate(0.5, 0.5), "roundtrip loft geometry");
}

void PlateSplitsRoundTrip()
{
    std::istringstream input(R"(
        plane_point_normal plan 0 0 12  0 0 1  1 0 0
        bezier3d section_a 0 -6 0  0 -2 3  0 2 3  0 6 0
        bezier3d section_b 12 -6 0  12 -2 5  12 2 5  12 6 0
        sketch_circle light_plan plan 6 0 1.25 reference
        surface_ruled nose_skin section_a section_b
        wire_project light_on_skin light_plan nose_skin 0 0 -1
        plate nose_plate nose_skin 0.5 centered styrene
        plate_opening nose_plate light_on_skin
    )");
    auto project = LoadProjectScript(input, "plate-split-test");

    bool crossingOpeningRejected = false;
    try {
        project.SplitPlate("nose_plate", PlateSplitAxis::V, 0.5, "cross_a", "cross_b");
    } catch (const std::invalid_argument&) {
        crossingOpeningRejected = true;
    }
    Require(crossingOpeningRejected, "split line crossing opening is rejected");
    Require(project.Plates().size() == 1, "rejected split keeps original plate");

    project.SplitPlate("nose_plate", PlateSplitAxis::V, 0.25, "nose_front", "nose_rear");
    Require(project.Plates().size() == 2, "plate split creates two pieces");
    Require(std::abs(project.Plates()[0].plate.Range().maximumV - 0.25) <= 1.0e-12, "first split range");
    Require(std::abs(project.Plates()[1].plate.Range().minimumV - 0.25) <= 1.0e-12, "second split range");
    Require(project.Plates()[0].openingWireNames.empty(), "opening is absent from first piece");
    Require(project.Plates()[1].openingWireNames == std::vector<std::string>{"light_on_skin"},
        "opening follows containing piece");
    bool outsidePieceOpeningRejected = false;
    try {
        project.AddPlateOpening("nose_front", "light_on_skin");
    } catch (const std::invalid_argument&) {
        outsidePieceOpeningRejected = true;
    }
    Require(outsidePieceOpeningRejected, "opening outside split piece is rejected");

    const Vector3 lightBeforeRejectedMove = project.Wires()[3].wire.Evaluate(0.25);
    bool openingAcrossSeamEditRejected = false;
    try {
        project.UpdateWire("light_plan", Wire::Circle(
            {2.0, 0.0, 12.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.25));
    } catch (const std::invalid_argument&) {
        openingAcrossSeamEditRejected = true;
    }
    Require(openingAcrossSeamEditRejected, "source drawing cannot move opening across plate seam");
    RequireNear(project.Wires()[3].wire.Evaluate(0.25), lightBeforeRejectedMove,
        "rejected opening move keeps projected geometry");

    const auto rearRange = project.Plates()[1].plate.Range();
    project.UpdateWire("section_b", Wire::CubicBezier(
        {12.0, -6.0, 0.0}, {12.0, -2.0, 7.0}, {12.0, 2.0, 7.0}, {12.0, 6.0, 0.0}));
    Require(std::abs(project.Plates()[1].plate.Range().minimumV - rearRange.minimumV) <= 1.0e-12,
        "split range survives source edit");

    std::ostringstream output;
    WriteProjectScript(output, project);
    Require(output.str().find("plate_range nose_front 0 1 0 0.25") != std::string::npos,
        "write first plate range");
    Require(output.str().find("plate_range nose_rear 0 1 0.25 1") != std::string::npos,
        "write second plate range");
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "plate-split-roundtrip");
    Require(roundTripped.Plates().size() == 2, "roundtrip split plate count");
    Require(std::abs(roundTripped.Plates()[0].plate.Range().maximumV - 0.25) <= 1.0e-12,
        "roundtrip first range");
    Require(roundTripped.Plates()[1].openingWireNames == std::vector<std::string>{"light_on_skin"},
        "roundtrip opening assignment");
}

void VisibilityRoundTrips()
{
    std::istringstream input(R"(
        plane_point_normal drawing 0 0 0  0 0 1  1 0 0
        sketch_circle boundary drawing 0 0 5 reference
        surface_planar panel boundary
        plate side_sheet panel 0.5 centered styrene
        visibility workplane drawing hidden
        visibility wire boundary hidden
        visibility surface panel hidden
        visibility plate side_sheet hidden
    )");
    const auto project = LoadProjectScript(input, "visibility-test");
    Require(!project.WorkPlanes()[0].visible, "load hidden workplane");
    Require(!project.Wires()[0].visible, "load hidden wire");
    Require(!project.Surfaces()[0].visible, "load hidden surface");
    Require(project.Plates().size() == 1, "load plate");
    Require(project.Plates()[0].material == "styrene", "load plate material");
    Require(std::abs(project.Plates()[0].plate.Thickness() - 0.5) <= 1.0e-12, "load plate thickness");
    Require(!project.Plates()[0].visible, "load hidden plate");

    std::ostringstream output;
    WriteProjectScript(output, project);
    std::istringstream roundTripInput(output.str());
    const auto roundTripped = LoadProjectScript(roundTripInput, "visibility-roundtrip");
    Require(!roundTripped.WorkPlanes()[0].visible, "roundtrip hidden workplane");
    Require(!roundTripped.Wires()[0].visible, "roundtrip hidden wire");
    Require(!roundTripped.Surfaces()[0].visible, "roundtrip hidden surface");
    Require(roundTripped.Plates().size() == 1, "roundtrip plate");
    Require(!roundTripped.Plates()[0].visible, "roundtrip hidden plate");
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

void AngleConstraintTracksAndProtectsItsWorkPlane()
{
    std::istringstream input(R"(
        plane_point_normal drawing 0 0 0  0 0 1  1 0 0
        sketch_line dimensioned drawing 2 3  6 3 reference
        wire_constraint dimensioned 4 0
    )");
    auto project = LoadProjectScript(input, "test");
    project.UpdateWorkPlane("drawing", project.FindWorkPlane("drawing")->Translated({5.0, 7.0, 2.0}));
    RequireNear(project.Wires()[0].wire.Start(), {7.0, 10.0, 2.0}, "angle-constrained line follows plane");
    RequireNear(project.Wires()[0].wire.End(), {11.0, 10.0, 2.0}, "angle and length survive plane move");

    bool removalRejected = false;
    try {
        (void)project.RemoveWorkPlane("drawing");
    } catch (const std::invalid_argument&) {
        removalRejected = true;
    }
    Require(removalRejected, "angle constraint protects source plane");
}

} // namespace

int main()
{
    try {
        LoadsPlanesAndWires();
        LoadsWireMetadataForDirectWires();
        ConstructionWiresRoundTripAndStayOutOfSurfaces();
        CoincidentEndpointsRoundTripAndDriveGeometry();
        TangentEndpointsRoundTripAndDriveBezierHandle();
        TangentEndpointsDriveCircularArc();
        WrittenProjectRoundTrips();
        LineConstraintsRoundTripAndDriveGeometry();
        RadiusConstraintsRoundTripAndDriveGeometry();
        ReferenceDimensionsRoundTripAndFollowGeometry();
        SurfacesAndProjectedWiresRoundTrip();
        LoftSurfacesRoundTrip();
        PlateSplitsRoundTrip();
        VisibilityRoundTrips();
        UnknownPlaneIsRejected();
        RemovingPlaneKeepsWireIn3D();
        UpdatingPlaneMovesOnlyLockedWires();
        AngleConstraintTracksAndProtectsItsWorkPlane();
    } catch (const std::exception& error) {
        std::cerr << "project_script_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "project_script_tests passed\n";
    return 0;
}
