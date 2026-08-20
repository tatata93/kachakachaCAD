#include "kachakacha/io/ProjectScript.h"

#include "kachakacha/model/Sketch.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

namespace kachakacha::io {

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;
using kachakacha::model::Project;
using kachakacha::model::Sketch;
using kachakacha::model::Wire;
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
            } else {
                ThrowLineError(sourceName, lineNumber, "Unknown command: " + command);
            }
        } catch (const std::exception& error) {
            ThrowLineError(sourceName, lineNumber, error.what());
        }
    }

    return project;
}

} // namespace kachakacha::io
