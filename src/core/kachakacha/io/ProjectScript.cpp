#include "kachakacha/io/ProjectScript.h"

#include "kachakacha/model/Sketch.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::io {

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;
using kachakacha::model::Project;
using kachakacha::model::PlateSurfaceRange;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Sketch;
using kachakacha::model::Wire;
using kachakacha::model::WireArcData;
using kachakacha::model::WireKind;
using kachakacha::model::WireLineConstraints;
using kachakacha::model::WireMetadata;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;

namespace {

constexpr double Pi = 3.14159265358979323846;

std::string StripComment(std::string line)
{
    const std::size_t commentStart = line.find('#');
    if (commentStart != std::string::npos) {
        line.resize(commentStart);
    }

    return line;
}

bool IsWhitespaceOnly(const std::string& line)
{
    for (char character : line) {
        if (!std::isspace(static_cast<unsigned char>(character))) {
            return false;
        }
    }

    return true;
}

[[noreturn]] void ThrowLineError(std::string_view sourceName, int lineNumber, const std::string& message)
{
    std::ostringstream output;
    output << sourceName << ":" << lineNumber << ": " << message;
    throw std::runtime_error(output.str());
}

double ReadDouble(std::istringstream& stream, std::string_view sourceName, int lineNumber, const char* label)
{
    double value = 0.0;
    if (!(stream >> value)) {
        ThrowLineError(sourceName, lineNumber, std::string("Expected number for ") + label + ".");
    }

    return value;
}

std::string ReadName(std::istringstream& stream, std::string_view sourceName, int lineNumber, const char* label)
{
    std::string value;
    if (!(stream >> value)) {
        ThrowLineError(sourceName, lineNumber, std::string("Expected name for ") + label + ".");
    }

    return value;
}

std::optional<double> ReadOptionalNumberToken(
    std::istringstream& stream,
    std::string_view sourceName,
    int lineNumber,
    const char* label)
{
    const std::string token = ReadName(stream, sourceName, lineNumber, label);
    if (token == "-" || token == "none") {
        return std::nullopt;
    }
    std::istringstream valueStream(token);
    const double value = ReadDouble(valueStream, sourceName, lineNumber, label);
    std::string extra;
    if (valueStream >> extra) {
        ThrowLineError(sourceName, lineNumber, "Invalid number for " + std::string(label) + ".");
    }
    return value;
}

void EnsureLineEnded(std::istringstream& stream, std::string_view sourceName, int lineNumber)
{
    std::string extra;
    if (stream >> extra) {
        ThrowLineError(sourceName, lineNumber, "Unexpected extra token: " + extra);
    }
}

WirePlanePolicy ParseWirePlanePolicy(const std::string& token, std::string_view sourceName, int lineNumber)
{
    if (token == "free" || token == "free3d" || token == "none") {
        return WirePlanePolicy::Free3D;
    }

    if (token == "reference" || token == "reference_only" || token == "ref") {
        return WirePlanePolicy::ReferenceOnly;
    }

    if (token == "locked" || token == "locked_to_plane" || token == "fixed") {
        return WirePlanePolicy::LockedToPlane;
    }

    ThrowLineError(sourceName, lineNumber, "Unknown wire plane policy: " + token);
}

WirePlanePolicy ReadOptionalSketchPolicy(std::istringstream& stream, std::string_view sourceName, int lineNumber)
{
    std::string token;
    if (!(stream >> token)) {
        return WirePlanePolicy::ReferenceOnly;
    }

    const WirePlanePolicy policy = ParseWirePlanePolicy(token, sourceName, lineNumber);
    EnsureLineEnded(stream, sourceName, lineNumber);
    return policy;
}

std::string WirePlanePolicyToken(WirePlanePolicy policy)
{
    switch (policy) {
    case WirePlanePolicy::Free3D:
        return "free";
    case WirePlanePolicy::ReferenceOnly:
        return "reference";
    case WirePlanePolicy::LockedToPlane:
        return "locked";
    }

    return "free";
}

model::WireEndpoint ParseWireEndpoint(
    const std::string& token,
    std::string_view sourceName,
    int lineNumber)
{
    if (token == "start" || token == "first") {
        return model::WireEndpoint::Start;
    }
    if (token == "end" || token == "last") {
        return model::WireEndpoint::End;
    }
    ThrowLineError(sourceName, lineNumber, "Unknown wire endpoint: " + token);
}

const char* WireEndpointToken(model::WireEndpoint endpoint)
{
    return endpoint == model::WireEndpoint::Start ? "start" : "end";
}

PlateThicknessDirection ParsePlateDirection(const std::string& token, std::string_view sourceName, int lineNumber)
{
    if (token == "positive" || token == "outside") {
        return PlateThicknessDirection::Positive;
    }
    if (token == "centered" || token == "center") {
        return PlateThicknessDirection::Centered;
    }
    if (token == "negative" || token == "inside") {
        return PlateThicknessDirection::Negative;
    }
    ThrowLineError(sourceName, lineNumber, "Unknown plate thickness direction: " + token);
}

const char* PlateDirectionToken(PlateThicknessDirection direction)
{
    switch (direction) {
    case PlateThicknessDirection::Positive:
        return "positive";
    case PlateThicknessDirection::Centered:
        return "centered";
    case PlateThicknessDirection::Negative:
        return "negative";
    }
    return "positive";
}

bool IsScriptNameSafe(std::string_view name)
{
    if (name.empty()) {
        return false;
    }

    for (char character : name) {
        if (std::isspace(static_cast<unsigned char>(character)) || character == '#') {
            return false;
        }
    }

    return true;
}

void RequireScriptNameSafe(std::string_view name, const char* label)
{
    if (!IsScriptNameSafe(name)) {
        throw std::invalid_argument(std::string(label) + " name cannot be empty or contain whitespace/#.");
    }
}

void WriteVector3(std::ostream& output, const Vector3& value)
{
    output << value.x << ' ' << value.y << ' ' << value.z;
}

Vector3 ReadVector3(std::istringstream& stream, std::string_view sourceName, int lineNumber, const char* label)
{
    const double x = ReadDouble(stream, sourceName, lineNumber, label);
    const double y = ReadDouble(stream, sourceName, lineNumber, label);
    const double z = ReadDouble(stream, sourceName, lineNumber, label);
    return {x, y, z};
}

Vector2 ReadVector2(std::istringstream& stream, std::string_view sourceName, int lineNumber, const char* label)
{
    const double u = ReadDouble(stream, sourceName, lineNumber, label);
    const double v = ReadDouble(stream, sourceName, lineNumber, label);
    return {u, v};
}

WorkPlane RequirePlane(
    const Project& project,
    const std::string& name,
    std::string_view sourceName,
    int lineNumber)
{
    std::optional<WorkPlane> plane = project.FindWorkPlane(name);
    if (!plane.has_value()) {
        ThrowLineError(sourceName, lineNumber, "Unknown work plane: " + name);
    }

    return *plane;
}

} // namespace

Project LoadProjectScript(std::istream& input, std::string_view sourceName)
{
    Project project;
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;

        line = StripComment(std::move(line));
        if (IsWhitespaceOnly(line)) {
            continue;
        }

        std::istringstream stream(line);
        std::string command;
        stream >> command;

        try {
            if (command == "plane_three") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector3 a = ReadVector3(stream, sourceName, lineNumber, "point A");
                const Vector3 b = ReadVector3(stream, sourceName, lineNumber, "point B");
                const Vector3 c = ReadVector3(stream, sourceName, lineNumber, "point C");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWorkPlane(name, WorkPlane::FromThreePoints(a, b, c));
            } else if (command == "plane_point_normal") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector3 origin = ReadVector3(stream, sourceName, lineNumber, "origin");
                const Vector3 normal = ReadVector3(stream, sourceName, lineNumber, "normal");
                const Vector3 uAxis = ReadVector3(stream, sourceName, lineNumber, "u axis hint");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWorkPlane(name, WorkPlane::FromPointNormal(origin, normal, uAxis));
            } else if (command == "plane_offset") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "plane");
                const std::string sourcePlaneName = ReadName(stream, sourceName, lineNumber, "source plane");
                const double distance = ReadDouble(stream, sourceName, lineNumber, "offset distance");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWorkPlane(name, RequirePlane(project, sourcePlaneName, sourceName, lineNumber).Offset(distance));
            } else if (command == "plane_rotate") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "plane");
                const std::string sourcePlaneName = ReadName(stream, sourceName, lineNumber, "source plane");
                const Vector3 axisPoint = ReadVector3(stream, sourceName, lineNumber, "axis point");
                const Vector3 axisDirection = ReadVector3(stream, sourceName, lineNumber, "axis direction");
                const double degrees = ReadDouble(stream, sourceName, lineNumber, "angle degrees");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWorkPlane(
                    name,
                    RequirePlane(project, sourcePlaneName, sourceName, lineNumber)
                        .RotateAroundAxis(axisPoint, axisDirection, degrees * Pi / 180.0));
            } else if (command == "line3d") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const Vector3 start = ReadVector3(stream, sourceName, lineNumber, "start");
                const Vector3 end = ReadVector3(stream, sourceName, lineNumber, "end");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWire(name, Wire::Line(start, end));
            } else if (command == "polyline3d") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                std::vector<Vector3> points;
                while (true) {
                    stream >> std::ws;
                    if (stream.eof()) {
                        break;
                    }

                    points.push_back(ReadVector3(stream, sourceName, lineNumber, "polyline point"));
                }
                project.AddWire(name, Wire::Polyline(std::move(points)));
            } else if (command == "bezier3d") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const Vector3 start = ReadVector3(stream, sourceName, lineNumber, "start");
                const Vector3 control1 = ReadVector3(stream, sourceName, lineNumber, "control 1");
                const Vector3 control2 = ReadVector3(stream, sourceName, lineNumber, "control 2");
                const Vector3 end = ReadVector3(stream, sourceName, lineNumber, "end");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWire(name, Wire::CubicBezier(start, control1, control2, end));
            } else if (command == "circle3d") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const Vector3 center = ReadVector3(stream, sourceName, lineNumber, "center");
                const Vector3 uAxis = ReadVector3(stream, sourceName, lineNumber, "u axis");
                const Vector3 vAxis = ReadVector3(stream, sourceName, lineNumber, "v axis");
                const double radius = ReadDouble(stream, sourceName, lineNumber, "radius");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWire(name, Wire::Circle(center, uAxis, vAxis, radius));
            } else if (command == "arc3d") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const Vector3 center = ReadVector3(stream, sourceName, lineNumber, "center");
                const Vector3 uAxis = ReadVector3(stream, sourceName, lineNumber, "u axis");
                const Vector3 vAxis = ReadVector3(stream, sourceName, lineNumber, "v axis");
                const double radius = ReadDouble(stream, sourceName, lineNumber, "radius");
                const double startDegrees = ReadDouble(stream, sourceName, lineNumber, "start angle degrees");
                const double sweepDegrees = ReadDouble(stream, sourceName, lineNumber, "sweep angle degrees");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWire(
                    name,
                    Wire::CircularArc(
                        center,
                        uAxis,
                        vAxis,
                        radius,
                        startDegrees * Pi / 180.0,
                        sweepDegrees * Pi / 180.0));
            } else if (command == "wire_meta") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string sourcePlaneName = ReadName(stream, sourceName, lineNumber, "source plane");
                const std::string policyToken = ReadName(stream, sourceName, lineNumber, "wire plane policy");
                EnsureLineEnded(stream, sourceName, lineNumber);

                std::optional<std::string> sourcePlane;
                if (sourcePlaneName != "-" && sourcePlaneName != "none") {
                    sourcePlane = sourcePlaneName;
                }

                WireMetadata metadata;
                for (const auto& wire : project.Wires()) {
                    if (wire.name == name) {
                        metadata = wire.metadata;
                        break;
                    }
                }
                metadata.sourcePlaneName = std::move(sourcePlane);
                metadata.planePolicy = ParseWirePlanePolicy(policyToken, sourceName, lineNumber);
                project.SetWireMetadata(name, std::move(metadata));
            } else if (command == "wire_constraint") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const WireLineConstraints constraints{
                    ReadOptionalNumberToken(stream, sourceName, lineNumber, "fixed length"),
                    ReadOptionalNumberToken(stream, sourceName, lineNumber, "fixed angle"),
                };
                EnsureLineEnded(stream, sourceName, lineNumber);

                bool found = false;
                WireMetadata metadata;
                for (const auto& wire : project.Wires()) {
                    if (wire.name == name) {
                        metadata = wire.metadata;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ThrowLineError(sourceName, lineNumber, "Wire does not exist: " + name);
                }
                metadata.lineConstraints = constraints;
                project.SetWireMetadata(name, std::move(metadata));
            } else if (command == "wire_role") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string role = ReadName(stream, sourceName, lineNumber, "wire role");
                EnsureLineEnded(stream, sourceName, lineNumber);

                bool found = false;
                WireMetadata metadata;
                for (const auto& wire : project.Wires()) {
                    if (wire.name == name) {
                        metadata = wire.metadata;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ThrowLineError(sourceName, lineNumber, "Wire does not exist: " + name);
                }
                if (role == "construction" || role == "guide") {
                    metadata.construction = true;
                } else if (role == "model" || role == "geometry") {
                    metadata.construction = false;
                } else {
                    ThrowLineError(sourceName, lineNumber, "Unknown wire role: " + role);
                }
                project.SetWireMetadata(name, std::move(metadata));
            } else if (command == "wire_coincident") {
                const std::string anchorWire = ReadName(stream, sourceName, lineNumber, "anchor wire");
                const std::string anchorEndpoint = ReadName(stream, sourceName, lineNumber, "anchor endpoint");
                const std::string followerWire = ReadName(stream, sourceName, lineNumber, "follower wire");
                const std::string followerEndpoint = ReadName(stream, sourceName, lineNumber, "follower endpoint");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWireCoincidentConstraint(
                    {anchorWire, ParseWireEndpoint(anchorEndpoint, sourceName, lineNumber)},
                    {followerWire, ParseWireEndpoint(followerEndpoint, sourceName, lineNumber)});
            } else if (command == "sketch_line") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string planeName = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector2 start = ReadVector2(stream, sourceName, lineNumber, "start");
                const Vector2 end = ReadVector2(stream, sourceName, lineNumber, "end");
                const WirePlanePolicy policy = ReadOptionalSketchPolicy(stream, sourceName, lineNumber);
                project.AddWire(
                    name,
                    Sketch(RequirePlane(project, planeName, sourceName, lineNumber)).MakeLine(start, end),
                    WireMetadata{planeName, policy});
            } else if (command == "sketch_circle") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string planeName = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector2 center = ReadVector2(stream, sourceName, lineNumber, "center");
                const double radius = ReadDouble(stream, sourceName, lineNumber, "radius");
                const WirePlanePolicy policy = ReadOptionalSketchPolicy(stream, sourceName, lineNumber);
                project.AddWire(
                    name,
                    Sketch(RequirePlane(project, planeName, sourceName, lineNumber)).MakeCircle(center, radius),
                    WireMetadata{planeName, policy});
            } else if (command == "sketch_arc") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string planeName = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector2 center = ReadVector2(stream, sourceName, lineNumber, "center");
                const double radius = ReadDouble(stream, sourceName, lineNumber, "radius");
                const double startDegrees = ReadDouble(stream, sourceName, lineNumber, "start angle degrees");
                const double sweepDegrees = ReadDouble(stream, sourceName, lineNumber, "sweep angle degrees");
                const WirePlanePolicy policy = ReadOptionalSketchPolicy(stream, sourceName, lineNumber);
                project.AddWire(
                    name,
                    Sketch(RequirePlane(project, planeName, sourceName, lineNumber))
                        .MakeCircularArc(center, radius, startDegrees * Pi / 180.0, sweepDegrees * Pi / 180.0),
                    WireMetadata{planeName, policy});
            } else if (command == "sketch_bezier") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string planeName = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector2 start = ReadVector2(stream, sourceName, lineNumber, "start");
                const Vector2 control1 = ReadVector2(stream, sourceName, lineNumber, "control 1");
                const Vector2 control2 = ReadVector2(stream, sourceName, lineNumber, "control 2");
                const Vector2 end = ReadVector2(stream, sourceName, lineNumber, "end");
                const WirePlanePolicy policy = ReadOptionalSketchPolicy(stream, sourceName, lineNumber);
                project.AddWire(
                    name,
                    Sketch(RequirePlane(project, planeName, sourceName, lineNumber))
                        .MakeCubicBezier(start, control1, control2, end),
                    WireMetadata{planeName, policy});
            } else if (command == "surface_planar") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "surface");
                const std::string boundary = ReadName(stream, sourceName, lineNumber, "boundary wire");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPlanarSurface(name, boundary);
            } else if (command == "surface_ruled") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "surface");
                const std::string firstSection = ReadName(stream, sourceName, lineNumber, "first section wire");
                const std::string secondSection = ReadName(stream, sourceName, lineNumber, "second section wire");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddRuledSurface(name, firstSection, secondSection);
            } else if (command == "surface_loft") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "surface");
                std::vector<std::string> sections;
                std::string sectionName;
                while (stream >> sectionName) {
                    sections.push_back(sectionName);
                }
                project.AddLoftSurface(name, std::move(sections));
            } else if (command == "wire_project") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "projected wire");
                const std::string sourceWire = ReadName(stream, sourceName, lineNumber, "source drawing wire");
                const std::string targetSurface = ReadName(stream, sourceName, lineNumber, "target surface");
                const Vector3 direction = ReadVector3(stream, sourceName, lineNumber, "projection direction");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddProjectedWire(name, sourceWire, targetSurface, direction);
            } else if (command == "plate") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "plate");
                const std::string sourceSurface = ReadName(stream, sourceName, lineNumber, "source surface");
                const double thickness = ReadDouble(stream, sourceName, lineNumber, "plate thickness");
                const std::string directionToken = ReadName(stream, sourceName, lineNumber, "thickness direction");
                const std::string material = ReadName(stream, sourceName, lineNumber, "material");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPlate(name, sourceSurface, thickness,
                    ParsePlateDirection(directionToken, sourceName, lineNumber), material);
            } else if (command == "plate_range") {
                const std::string plateName = ReadName(stream, sourceName, lineNumber, "plate");
                const PlateSurfaceRange range{
                    ReadDouble(stream, sourceName, lineNumber, "minimum U"),
                    ReadDouble(stream, sourceName, lineNumber, "maximum U"),
                    ReadDouble(stream, sourceName, lineNumber, "minimum V"),
                    ReadDouble(stream, sourceName, lineNumber, "maximum V"),
                };
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetPlateRange(plateName, range);
            } else if (command == "plate_opening") {
                const std::string plateName = ReadName(stream, sourceName, lineNumber, "plate");
                const std::string wireName = ReadName(stream, sourceName, lineNumber, "opening wire");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPlateOpening(plateName, wireName);
            } else if (command == "visibility") {
                const std::string objectKind = ReadName(stream, sourceName, lineNumber, "object kind");
                const std::string name = ReadName(stream, sourceName, lineNumber, "object");
                const std::string state = ReadName(stream, sourceName, lineNumber, "visibility state");
                EnsureLineEnded(stream, sourceName, lineNumber);
                if (state != "shown" && state != "hidden") {
                    throw std::invalid_argument("Visibility state must be shown or hidden.");
                }
                const bool visible = state == "shown";
                if (objectKind == "workplane") {
                    project.SetWorkPlaneVisible(name, visible);
                } else if (objectKind == "wire") {
                    project.SetWireVisible(name, visible);
                } else if (objectKind == "surface") {
                    project.SetSurfaceVisible(name, visible);
                } else if (objectKind == "plate") {
                    project.SetPlateVisible(name, visible);
                } else {
                    throw std::invalid_argument("Visibility object kind must be workplane, wire, surface, or plate.");
                }
            } else {
                ThrowLineError(sourceName, lineNumber, "Unknown command: " + command);
            }
        } catch (const std::exception& error) {
            ThrowLineError(sourceName, lineNumber, error.what());
        }
    }

    return project;
}

void WriteProjectScript(std::ostream& output, const Project& project)
{
    output << std::setprecision(17);
    output << "# kachakachaCAD project script\n";
    output << "# Units are millimeters.\n\n";

    for (const auto& workPlane : project.WorkPlanes()) {
        RequireScriptNameSafe(workPlane.name, "Work plane");
        output << "plane_point_normal " << workPlane.name << ' ';
        WriteVector3(output, workPlane.plane.Origin());
        output << "  ";
        WriteVector3(output, workPlane.plane.Normal());
        output << "  ";
        WriteVector3(output, workPlane.plane.UAxis());
        output << '\n';
    }

    if (!project.WorkPlanes().empty() && !project.Wires().empty()) {
        output << '\n';
    }

    for (const auto& namedWire : project.Wires()) {
        if (namedWire.projection.has_value()) {
            continue;
        }
        RequireScriptNameSafe(namedWire.name, "Wire");
        const Wire& wire = namedWire.wire;
        const std::vector<Vector3>& points = wire.ControlPoints();

        switch (wire.Kind()) {
        case WireKind::Line:
            output << "line3d " << namedWire.name << ' ';
            WriteVector3(output, wire.Start());
            output << "  ";
            WriteVector3(output, wire.End());
            output << '\n';
            break;

        case WireKind::Polyline:
            output << "polyline3d " << namedWire.name;
            for (const Vector3& point : points) {
                output << ' ';
                WriteVector3(output, point);
            }
            output << '\n';
            break;

        case WireKind::CubicBezier:
            output << "bezier3d " << namedWire.name;
            for (const Vector3& point : points) {
                output << ' ';
                WriteVector3(output, point);
            }
            output << '\n';
            break;

        case WireKind::Circle: {
            const WireArcData arc = wire.ArcData();
            output << "circle3d " << namedWire.name << ' ';
            WriteVector3(output, arc.center);
            output << "  ";
            WriteVector3(output, arc.uAxis);
            output << "  ";
            WriteVector3(output, arc.vAxis);
            output << ' ' << arc.radius << '\n';
            break;
        }

        case WireKind::CircularArc: {
            const WireArcData arc = wire.ArcData();
            output << "arc3d " << namedWire.name << ' ';
            WriteVector3(output, arc.center);
            output << "  ";
            WriteVector3(output, arc.uAxis);
            output << "  ";
            WriteVector3(output, arc.vAxis);
            output << ' ' << arc.radius
                   << ' ' << arc.startAngleRadians * 180.0 / Pi
                   << ' ' << arc.sweepAngleRadians * 180.0 / Pi
                   << '\n';
            break;
        }
        }

        const bool hasMetadata = namedWire.metadata.sourcePlaneName.has_value()
            || namedWire.metadata.planePolicy != WirePlanePolicy::Free3D;
        if (hasMetadata) {
            output << "wire_meta " << namedWire.name << ' ';
            if (namedWire.metadata.sourcePlaneName.has_value()) {
                RequireScriptNameSafe(*namedWire.metadata.sourcePlaneName, "Wire source plane");
                output << *namedWire.metadata.sourcePlaneName;
            } else {
                output << '-';
            }
            output << ' ' << WirePlanePolicyToken(namedWire.metadata.planePolicy) << '\n';
        }
        if (!namedWire.metadata.lineConstraints.Empty()) {
            output << "wire_constraint " << namedWire.name << ' ';
            if (namedWire.metadata.lineConstraints.lengthMillimeters.has_value()) {
                output << *namedWire.metadata.lineConstraints.lengthMillimeters;
            } else {
                output << '-';
            }
            output << ' ';
            if (namedWire.metadata.lineConstraints.angleDegrees.has_value()) {
                output << *namedWire.metadata.lineConstraints.angleDegrees;
            } else {
                output << '-';
            }
            output << '\n';
        }
        if (namedWire.metadata.construction) {
            output << "wire_role " << namedWire.name << " construction\n";
        }
    }

    if (!project.Surfaces().empty()) {
        output << '\n';
    }
    for (const auto& namedSurface : project.Surfaces()) {
        RequireScriptNameSafe(namedSurface.name, "Surface");
        for (const std::string& sourceWireName : namedSurface.sourceWireNames) {
            RequireScriptNameSafe(sourceWireName, "Surface source wire");
        }
        if (namedSurface.surface.Kind() == model::SurfaceKind::Planar) {
            output << "surface_planar " << namedSurface.name << ' ' << namedSurface.sourceWireNames.at(0) << '\n';
        } else if (namedSurface.surface.Kind() == model::SurfaceKind::Ruled) {
            output << "surface_ruled " << namedSurface.name << ' '
                   << namedSurface.sourceWireNames.at(0) << ' ' << namedSurface.sourceWireNames.at(1) << '\n';
        } else {
            output << "surface_loft " << namedSurface.name;
            for (const std::string& sectionName : namedSurface.sourceWireNames) {
                output << ' ' << sectionName;
            }
            output << '\n';
        }
    }

    const bool hasProjectedWires = std::any_of(project.Wires().begin(), project.Wires().end(), [](const auto& wire) {
        return wire.projection.has_value();
    });
    if (hasProjectedWires || !project.CoincidentConstraints().empty()) {
        output << '\n';
    }
    for (const auto& namedWire : project.Wires()) {
        if (!namedWire.projection.has_value()) {
            continue;
        }
        RequireScriptNameSafe(namedWire.name, "Projected wire");
        RequireScriptNameSafe(namedWire.projection->sourceWireName, "Projection source wire");
        RequireScriptNameSafe(namedWire.projection->targetSurfaceName, "Projection target surface");
        output << "wire_project " << namedWire.name << ' '
               << namedWire.projection->sourceWireName << ' '
               << namedWire.projection->targetSurfaceName << ' ';
        WriteVector3(output, namedWire.projection->direction);
        output << '\n';
        if (namedWire.metadata.construction) {
            output << "wire_role " << namedWire.name << " construction\n";
        }
    }
    for (const auto& constraint : project.CoincidentConstraints()) {
        RequireScriptNameSafe(constraint.anchor.wireName, "Coincidence anchor wire");
        RequireScriptNameSafe(constraint.follower.wireName, "Coincidence follower wire");
        output << "wire_coincident "
               << constraint.anchor.wireName << ' ' << WireEndpointToken(constraint.anchor.endpoint) << ' '
               << constraint.follower.wireName << ' ' << WireEndpointToken(constraint.follower.endpoint) << '\n';
    }

    if (!project.Plates().empty()) {
        output << '\n';
    }
    for (const auto& namedPlate : project.Plates()) {
        RequireScriptNameSafe(namedPlate.name, "Plate");
        RequireScriptNameSafe(namedPlate.sourceSurfaceName, "Plate source surface");
        RequireScriptNameSafe(namedPlate.material, "Plate material");
        output << "plate " << namedPlate.name << ' ' << namedPlate.sourceSurfaceName << ' '
               << namedPlate.plate.Thickness() << ' ' << PlateDirectionToken(namedPlate.plate.Direction()) << ' '
               << namedPlate.material << '\n';
    }
    for (const auto& namedPlate : project.Plates()) {
        if (namedPlate.plate.Range().IsFull()) {
            continue;
        }
        const auto& range = namedPlate.plate.Range();
        output << "plate_range " << namedPlate.name << ' '
               << range.minimumU << ' ' << range.maximumU << ' '
               << range.minimumV << ' ' << range.maximumV << '\n';
    }
    for (const auto& namedPlate : project.Plates()) {
        for (const std::string& openingWireName : namedPlate.openingWireNames) {
            RequireScriptNameSafe(openingWireName, "Plate opening wire");
            output << "plate_opening " << namedPlate.name << ' ' << openingWireName << '\n';
        }
    }

    const bool hasHiddenObjects = std::any_of(project.WorkPlanes().begin(), project.WorkPlanes().end(), [](const auto& plane) {
        return !plane.visible;
    }) || std::any_of(project.Wires().begin(), project.Wires().end(), [](const auto& wire) {
        return !wire.visible;
    }) || std::any_of(project.Surfaces().begin(), project.Surfaces().end(), [](const auto& surface) {
        return !surface.visible;
    }) || std::any_of(project.Plates().begin(), project.Plates().end(), [](const auto& plate) {
        return !plate.visible;
    });
    if (hasHiddenObjects) {
        output << '\n';
    }
    for (const auto& plane : project.WorkPlanes()) {
        if (!plane.visible) {
            output << "visibility workplane " << plane.name << " hidden\n";
        }
    }
    for (const auto& wire : project.Wires()) {
        if (!wire.visible) {
            output << "visibility wire " << wire.name << " hidden\n";
        }
    }
    for (const auto& surface : project.Surfaces()) {
        if (!surface.visible) {
            output << "visibility surface " << surface.name << " hidden\n";
        }
    }
    for (const auto& plate : project.Plates()) {
        if (!plate.visible) {
            output << "visibility plate " << plate.name << " hidden\n";
        }
    }
}

} // namespace kachakacha::io
