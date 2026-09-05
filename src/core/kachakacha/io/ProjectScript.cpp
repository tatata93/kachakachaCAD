#include "kachakacha/io/ProjectScript.h"

#include "kachakacha/model/Sketch.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
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
using kachakacha::model::DimensionReference;
using kachakacha::model::DimensionReferenceKind;
using kachakacha::model::JigSide;
using kachakacha::model::PlateSurfaceRange;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::ReferenceDimension;
using kachakacha::model::ReferenceDimensionKind;
using kachakacha::model::Sketch;
using kachakacha::model::Wire;
using kachakacha::model::WireArcData;
using kachakacha::model::WireContinuity;
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

std::size_t ReadCount(std::istringstream& stream, std::string_view sourceName, int lineNumber, const char* label)
{
    long long value = 0;
    if (!(stream >> value) || value < 0) {
        ThrowLineError(sourceName, lineNumber, std::string("Expected count for ") + label + ".");
    }

    return static_cast<std::size_t>(value);
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

void RequireScriptNameSafe(std::string_view name, const char* label);
void WriteVector3(std::ostream& output, const Vector3& value);

ReferenceDimensionKind ParseReferenceDimensionKind(
    const std::string& token,
    std::string_view sourceName,
    int lineNumber)
{
    if (token == "point_distance") return ReferenceDimensionKind::PointDistance;
    if (token == "wire_length") return ReferenceDimensionKind::WireLength;
    if (token == "wire_radius") return ReferenceDimensionKind::WireRadius;
    if (token == "wire_distance") return ReferenceDimensionKind::WireDistance;
    if (token == "wire_angle") return ReferenceDimensionKind::WireAngle;
    if (token == "point_wire_distance") return ReferenceDimensionKind::PointWireDistance;
    if (token == "point_plane_distance") return ReferenceDimensionKind::PointPlaneDistance;
    if (token == "wire_plane_angle") return ReferenceDimensionKind::WirePlaneAngle;
    if (token == "plane_angle") return ReferenceDimensionKind::PlaneAngle;
    if (token == "plane_distance") return ReferenceDimensionKind::PlaneDistance;
    ThrowLineError(sourceName, lineNumber, "Unknown reference dimension kind: " + token);
}

const char* ReferenceDimensionKindToken(ReferenceDimensionKind kind)
{
    switch (kind) {
    case ReferenceDimensionKind::PointDistance: return "point_distance";
    case ReferenceDimensionKind::WireLength: return "wire_length";
    case ReferenceDimensionKind::WireRadius: return "wire_radius";
    case ReferenceDimensionKind::WireDistance: return "wire_distance";
    case ReferenceDimensionKind::WireAngle: return "wire_angle";
    case ReferenceDimensionKind::PointWireDistance: return "point_wire_distance";
    case ReferenceDimensionKind::PointPlaneDistance: return "point_plane_distance";
    case ReferenceDimensionKind::WirePlaneAngle: return "wire_plane_angle";
    case ReferenceDimensionKind::PlaneAngle: return "plane_angle";
    case ReferenceDimensionKind::PlaneDistance: return "plane_distance";
    }
    return "point_distance";
}

DimensionReference ReadDimensionReference(
    std::istringstream& stream,
    std::string_view sourceName,
    int lineNumber)
{
    const std::string kind = ReadName(stream, sourceName, lineNumber, "dimension reference kind");
    if (kind == "none" || kind == "-") {
        return {};
    }
    if (kind == "point") {
        return {
            DimensionReferenceKind::FixedPoint,
            {},
            {
                ReadDouble(stream, sourceName, lineNumber, "point x"),
                ReadDouble(stream, sourceName, lineNumber, "point y"),
                ReadDouble(stream, sourceName, lineNumber, "point z"),
            },
            0.0,
        };
    }
    if (kind == "wire") {
        DimensionReference result;
        result.kind = DimensionReferenceKind::Wire;
        result.objectName = ReadName(stream, sourceName, lineNumber, "dimension wire");
        result.wireParameter = ReadDouble(stream, sourceName, lineNumber, "wire parameter");
        return result;
    }
    if (kind == "plane") {
        DimensionReference result;
        result.kind = DimensionReferenceKind::WorkPlane;
        result.objectName = ReadName(stream, sourceName, lineNumber, "dimension plane");
        return result;
    }
    ThrowLineError(sourceName, lineNumber, "Unknown dimension reference kind: " + kind);
}

void WriteDimensionReference(std::ostream& output, const DimensionReference& reference)
{
    switch (reference.kind) {
    case DimensionReferenceKind::None:
        output << "none";
        return;
    case DimensionReferenceKind::FixedPoint:
        output << "point ";
        WriteVector3(output, reference.point);
        return;
    case DimensionReferenceKind::Wire:
        RequireScriptNameSafe(reference.objectName, "Dimension wire reference");
        output << "wire " << reference.objectName << ' ' << reference.wireParameter;
        return;
    case DimensionReferenceKind::WorkPlane:
        RequireScriptNameSafe(reference.objectName, "Dimension plane reference");
        output << "plane " << reference.objectName;
        return;
    }
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

JigSide ParseJigSide(const std::string& token, std::string_view sourceName, int lineNumber)
{
    if (token == "positive" || token == "outside") {
        return JigSide::Positive;
    }
    if (token == "negative" || token == "inside") {
        return JigSide::Negative;
    }
    ThrowLineError(sourceName, lineNumber, "Unknown jig side: " + token);
}

const char* JigSideToken(JigSide side)
{
    return side == JigSide::Negative ? "negative" : "positive";
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
    bool formatVersionSeen = false;

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
            if (command == "format_version") {
                int version = 0;
                if (!(stream >> version)) {
                    ThrowLineError(sourceName, lineNumber, "Expected project format version.");
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                if (formatVersionSeen) {
                    ThrowLineError(sourceName, lineNumber, "Project format version is specified more than once.");
                }
                if (version != 1) {
                    ThrowLineError(sourceName, lineNumber, "Unsupported project format version: " + std::to_string(version));
                }
                formatVersionSeen = true;
            } else if (command == "plane_three") {
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
            } else if (command == "point3d") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "point");
                const Vector3 point = ReadVector3(stream, sourceName, lineNumber, "position");
                const std::string sourcePlaneName = ReadName(
                    stream, sourceName, lineNumber, "source plane");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPoint(
                    name,
                    point,
                    sourcePlaneName == "-" || sourcePlaneName == "none"
                        ? std::nullopt
                        : std::optional<std::string>(sourcePlaneName));
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
            } else if (command == "bspline3d") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                std::vector<Vector3> controlPoints;
                while (true) {
                    stream >> std::ws;
                    if (stream.eof()) {
                        break;
                    }
                    controlPoints.push_back(ReadVector3(stream, sourceName, lineNumber, "B-spline control point"));
                }
                project.AddWire(name, Wire::CubicBSpline(std::move(controlPoints)));
            } else if (command == "bspline3d_knots") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const double rawCount = ReadDouble(
                    stream, sourceName, lineNumber, "B-spline control point count");
                if (!std::isfinite(rawCount) || rawCount < 4.0 || std::floor(rawCount) != rawCount) {
                    ThrowLineError(sourceName, lineNumber,
                        "B-spline control point count must be an integer of at least four.");
                }
                const auto controlCount = static_cast<std::size_t>(rawCount);
                std::vector<Vector3> controlPoints;
                controlPoints.reserve(controlCount);
                for (std::size_t index = 0; index < controlCount; ++index) {
                    controlPoints.push_back(ReadVector3(
                        stream, sourceName, lineNumber, "B-spline control point"));
                }
                std::vector<double> knots;
                knots.reserve(controlCount + 4);
                for (std::size_t index = 0; index < controlCount + 4; ++index) {
                    knots.push_back(ReadDouble(stream, sourceName, lineNumber, "B-spline knot"));
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWire(name, Wire::CubicBSplineWithKnots(
                    std::move(controlPoints), std::move(knots)));
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
            } else if (command == "wire_radius_constraint") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const double radius = ReadDouble(stream, sourceName, lineNumber, "fixed radius");
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
                metadata.curveConstraints.radiusMillimeters = radius;
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
            } else if (command == "wire_tangent") {
                const std::string anchorWire = ReadName(stream, sourceName, lineNumber, "anchor wire");
                const std::string anchorEndpoint = ReadName(stream, sourceName, lineNumber, "anchor endpoint");
                const std::string followerWire = ReadName(stream, sourceName, lineNumber, "follower wire");
                const std::string followerEndpoint = ReadName(stream, sourceName, lineNumber, "follower endpoint");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWireTangentConstraint(
                    {anchorWire, ParseWireEndpoint(anchorEndpoint, sourceName, lineNumber)},
                    {followerWire, ParseWireEndpoint(followerEndpoint, sourceName, lineNumber)});
            } else if (command == "wire_curvature") {
                const std::string anchorWire = ReadName(stream, sourceName, lineNumber, "anchor wire");
                const std::string anchorEndpoint = ReadName(stream, sourceName, lineNumber, "anchor endpoint");
                const std::string followerWire = ReadName(stream, sourceName, lineNumber, "follower wire");
                const std::string followerEndpoint = ReadName(stream, sourceName, lineNumber, "follower endpoint");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddWireTangentConstraint(
                    {anchorWire, ParseWireEndpoint(anchorEndpoint, sourceName, lineNumber)},
                    {followerWire, ParseWireEndpoint(followerEndpoint, sourceName, lineNumber)},
                    WireContinuity::G2Curvature);
            } else if (command == "reference_dimension") {
                ReferenceDimension dimension;
                dimension.name = ReadName(stream, sourceName, lineNumber, "reference dimension");
                dimension.kind = ParseReferenceDimensionKind(
                    ReadName(stream, sourceName, lineNumber, "reference dimension kind"),
                    sourceName,
                    lineNumber);
                dimension.first = ReadDimensionReference(stream, sourceName, lineNumber);
                dimension.second = ReadDimensionReference(stream, sourceName, lineNumber);
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddReferenceDimension(std::move(dimension));
            } else if (command == "sketch_line") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string planeName = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector2 start = ReadVector2(stream, sourceName, lineNumber, "start");
                const Vector2 end = ReadVector2(stream, sourceName, lineNumber, "end");
                const WirePlanePolicy policy = ReadOptionalSketchPolicy(stream, sourceName, lineNumber);
                project.AddWire(
                    name,
                    Sketch(RequirePlane(project, planeName, sourceName, lineNumber)).MakeLine(start, end),
                    WireMetadata{planeName, policy, {}, {}});
            } else if (command == "sketch_circle") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "wire");
                const std::string planeName = ReadName(stream, sourceName, lineNumber, "plane");
                const Vector2 center = ReadVector2(stream, sourceName, lineNumber, "center");
                const double radius = ReadDouble(stream, sourceName, lineNumber, "radius");
                const WirePlanePolicy policy = ReadOptionalSketchPolicy(stream, sourceName, lineNumber);
                project.AddWire(
                    name,
                    Sketch(RequirePlane(project, planeName, sourceName, lineNumber)).MakeCircle(center, radius),
                    WireMetadata{planeName, policy, {}, {}});
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
                    WireMetadata{planeName, policy, {}, {}});
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
                    WireMetadata{planeName, policy, {}, {}});
            } else if (command == "surface_planar") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "surface");
                std::vector<std::string> boundaries;
                std::string boundaryName;
                while (stream >> boundaryName) {
                    boundaries.push_back(boundaryName);
                }
                if (boundaries.empty()) {
                    ThrowLineError(sourceName, lineNumber, "surface_planar requires a boundary wire");
                }
                project.AddPlanarSurface(name, std::move(boundaries));
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
            } else if (command == "surface_auto") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "surface");
                const std::size_t wireCount =
                    ReadCount(stream, sourceName, lineNumber, "auto surface wire count");
                std::vector<std::string> wireNames;
                wireNames.reserve(wireCount);
                for (std::size_t index = 0; index < wireCount; ++index) {
                    wireNames.push_back(
                        ReadName(stream, sourceName, lineNumber, "auto surface wire"));
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddAutoSurface(name, std::move(wireNames));
            } else if (command == "surface_gordon") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "surface");
                const std::size_t sectionCount = ReadCount(stream, sourceName, lineNumber, "section count");
                std::vector<std::string> sections;
                sections.reserve(sectionCount);
                for (std::size_t index = 0; index < sectionCount; ++index) {
                    sections.push_back(ReadName(stream, sourceName, lineNumber, "section wire"));
                }
                const std::size_t guideCount = ReadCount(stream, sourceName, lineNumber, "guide count");
                std::vector<std::string> guides;
                guides.reserve(guideCount);
                for (std::size_t index = 0; index < guideCount; ++index) {
                    guides.push_back(ReadName(stream, sourceName, lineNumber, "guide wire"));
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddGordonSurface(name, std::move(sections), std::move(guides));
            } else if (command == "surface_grouped") {
                const std::string kind = ReadName(
                    stream, sourceName, lineNumber, "grouped surface kind");
                const std::string name = ReadName(
                    stream, sourceName, lineNumber, "surface");
                int groupCount = 0;
                if (!(stream >> groupCount) || groupCount <= 0) {
                    ThrowLineError(
                        sourceName, lineNumber,
                        "surface_grouped requires a positive group count");
                }
                std::vector<std::vector<std::string>> groups;
                groups.reserve(static_cast<std::size_t>(groupCount));
                for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
                    int wireCount = 0;
                    if (!(stream >> wireCount) || wireCount <= 0) {
                        ThrowLineError(
                            sourceName, lineNumber,
                            "each grouped surface input requires a positive wire count");
                    }
                    std::vector<std::string> group;
                    group.reserve(static_cast<std::size_t>(wireCount));
                    for (int wireIndex = 0; wireIndex < wireCount; ++wireIndex) {
                        group.push_back(ReadName(
                            stream, sourceName, lineNumber,
                            "grouped surface source wire"));
                    }
                    groups.push_back(std::move(group));
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                if (kind == "planar") {
                    if (groups.size() != 1) {
                        ThrowLineError(
                            sourceName, lineNumber,
                            "grouped planar surface requires one boundary group");
                    }
                    project.AddPlanarSurface(name, std::move(groups.front()));
                } else if (kind == "ruled") {
                    if (groups.size() != 2) {
                        ThrowLineError(
                            sourceName, lineNumber,
                            "grouped ruled surface requires two section groups");
                    }
                    project.AddRuledSurface(
                        name, std::move(groups[0]), std::move(groups[1]));
                } else if (kind == "loft") {
                    project.AddLoftSurface(name, std::move(groups));
                } else if (kind == "guided_loft") {
                    if (groups.size() < 3) {
                        ThrowLineError(
                            sourceName, lineNumber,
                            "grouped guided loft requires two guides and at least one section");
                    }
                    std::vector<std::vector<std::string>> sections(
                        std::make_move_iterator(groups.begin() + 2),
                        std::make_move_iterator(groups.end()));
                    project.AddGuidedLoftSurface(
                        name, std::move(groups[0]), std::move(groups[1]),
                        std::move(sections));
                } else {
                    ThrowLineError(
                        sourceName, lineNumber,
                        "unknown grouped surface kind: " + kind);
                }
            } else if (command == "surface_guided_loft") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "surface");
                const std::string firstGuide = ReadName(stream, sourceName, lineNumber, "first guide wire");
                const std::string secondGuide = ReadName(stream, sourceName, lineNumber, "second guide wire");
                std::vector<std::string> sections;
                std::string sectionName;
                while (stream >> sectionName) {
                    sections.push_back(sectionName);
                }
                project.AddGuidedLoftSurface(
                    name, firstGuide, secondGuide, std::move(sections));
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
            } else if (command == "plate_variable") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "plate");
                const std::string sourceSurface = ReadName(stream, sourceName, lineNumber, "source surface");
                const double startThickness = ReadDouble(stream, sourceName, lineNumber, "start thickness");
                const double endThickness = ReadDouble(stream, sourceName, lineNumber, "end thickness");
                const std::string directionToken = ReadName(stream, sourceName, lineNumber, "thickness direction");
                const std::string material = ReadName(stream, sourceName, lineNumber, "material");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPlate(name, sourceSurface, startThickness, endThickness,
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
            } else if (command == "plate_relief_cut") {
                const std::string plateName = ReadName(stream, sourceName, lineNumber, "plate");
                const std::string wireName = ReadName(stream, sourceName, lineNumber, "relief-cut wire");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPlateReliefCut(plateName, wireName);
            } else if (command == "plate_split_line") {
                const std::string plateName = ReadName(stream, sourceName, lineNumber, "plate");
                const std::string wireName = ReadName(stream, sourceName, lineNumber, "split-line wire");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPlateSplitLine(plateName, wireName);
            } else if (command == "wire_plate_offset") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "offset wire");
                const std::string sourceWire = ReadName(stream, sourceName, lineNumber, "source wire");
                const std::string plateName = ReadName(stream, sourceName, lineNumber, "plate");
                const double throughThickness = ReadDouble(
                    stream, sourceName, lineNumber, "position through plate thickness");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPlateOffsetWire(name, sourceWire, plateName, throughThickness);
            } else if (command == "body_surface_jig") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "body");
                const std::string sourceSurface = ReadName(stream, sourceName, lineNumber, "source surface");
                const JigSide side = ParseJigSide(
                    ReadName(stream, sourceName, lineNumber, "jig side"),
                    sourceName,
                    lineNumber);
                const double clearance = ReadDouble(stream, sourceName, lineNumber, "jig clearance");
                const double thickness = ReadDouble(stream, sourceName, lineNumber, "jig thickness");
                const PlateSurfaceRange range{
                    ReadDouble(stream, sourceName, lineNumber, "minimum U"),
                    ReadDouble(stream, sourceName, lineNumber, "maximum U"),
                    ReadDouble(stream, sourceName, lineNumber, "minimum V"),
                    ReadDouble(stream, sourceName, lineNumber, "maximum V"),
                };
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddSurfaceJig(
                    name, sourceSurface, range, side, clearance, thickness);
            } else if (command == "part_model" || command == "part_model_surface") {
                const bool fromSurface = command == "part_model_surface";
                const std::string name = ReadName(stream, sourceName, lineNumber, "part model");
                const std::string sourceObjectName = ReadName(
                    stream, sourceName, lineNumber,
                    fromSurface ? "source surface" : "source plate");
                const std::string axisToken = ReadName(stream, sourceName, lineNumber, "split axis");
                if (axisToken != "u" && axisToken != "v") {
                    throw std::invalid_argument("Part-model split axis must be u or v.");
                }
                const int automatic = static_cast<int>(ReadDouble(stream, sourceName, lineNumber, "automatic flag"));
                model::PartApproximationOptions options;
                options.splitAxis = axisToken == "u"
                    ? model::PartSplitAxis::U
                    : model::PartSplitAxis::V;
                options.automaticBoundaries = automatic != 0;
                options.maximumDeviationMillimeters =
                    ReadDouble(stream, sourceName, lineNumber, "maximum deviation");
                options.maximumPartCount = static_cast<int>(ReadDouble(stream, sourceName, lineNumber, "maximum part count"));
                options.minimumPartWidthMillimeters =
                    ReadDouble(stream, sourceName, lineNumber, "minimum part width");
                const int manualCount = static_cast<int>(ReadCount(stream, sourceName, lineNumber, "manual boundary count"));
                for (int index = 0; index < manualCount; ++index) {
                    options.manualBoundaryParameters.push_back(
                        ReadDouble(stream, sourceName, lineNumber, "manual boundary parameter"));
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                if (fromSurface) {
                    project.AddPartModelFromSurface(name, sourceObjectName, options);
                } else {
                    project.AddPartModel(name, sourceObjectName, options);
                }
            } else if (command == "surface_opening") {
                const std::string surfaceName = ReadName(stream, sourceName, lineNumber, "surface");
                const std::string wireName = ReadName(stream, sourceName, lineNumber, "opening wire");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddSurfaceOpening(surfaceName, wireName);
            } else if (command == "plate_laminate") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "plate");
                const std::string baseName = ReadName(stream, sourceName, lineNumber, "laminate base plate");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetPlateLaminate(name, baseName);
            } else if (command == "part_model_scope") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "part model");
                const int wireCount = static_cast<int>(
                    ReadCount(stream, sourceName, lineNumber, "scope wire count"));
                std::vector<std::string> wireNames;
                wireNames.reserve(wireCount);
                for (int index = 0; index < wireCount; ++index) {
                    wireNames.push_back(
                        ReadName(stream, sourceName, lineNumber, "scope wire"));
                }
                const int surfaceCount = static_cast<int>(
                    ReadCount(stream, sourceName, lineNumber, "scope surface count"));
                std::vector<std::string> surfaceNames;
                surfaceNames.reserve(surfaceCount);
                for (int index = 0; index < surfaceCount; ++index) {
                    surfaceNames.push_back(
                        ReadName(stream, sourceName, lineNumber, "scope surface"));
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetPartModelConnectionScope(
                    name, std::move(wireNames), std::move(surfaceNames));
            } else if (command == "part_model_opening") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "part model");
                const int partNumber = static_cast<int>(
                    ReadCount(stream, sourceName, lineNumber, "part number"));
                const std::string wireName =
                    ReadName(stream, sourceName, lineNumber, "opening source wire");
                const double dx = ReadDouble(stream, sourceName, lineNumber, "direction x");
                const double dy = ReadDouble(stream, sourceName, lineNumber, "direction y");
                const double dz = ReadDouble(stream, sourceName, lineNumber, "direction z");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.AddPartModelOpening(name, partNumber, wireName, {dx, dy, dz});
            } else if (command == "part_model_part_offset") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "part model");
                const int partNumber = static_cast<int>(
                    ReadCount(stream, sourceName, lineNumber, "part number"));
                const double dx = ReadDouble(stream, sourceName, lineNumber, "offset x");
                const double dy = ReadDouble(stream, sourceName, lineNumber, "offset y");
                const double dz = ReadDouble(stream, sourceName, lineNumber, "offset z");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetPartModelPartOffset(name, partNumber, {dx, dy, dz});
            } else if (command == "part_model_fold") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "part model");
                const int count = static_cast<int>(ReadCount(stream, sourceName, lineNumber, "fold value count"));
                std::vector<double> progress;
                progress.reserve(count);
                for (int index = 0; index < count; ++index) {
                    progress.push_back(
                        ReadDouble(stream, sourceName, lineNumber, "fold progress"));
                }
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetPartModelRailFoldProgress(name, std::move(progress));
            } else if (command == "part_model_assembly") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "part model");
                const double progress =
                    ReadDouble(stream, sourceName, lineNumber, "assembly progress");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetPartModelAssemblyProgress(name, progress);
            } else if (command == "object_set_parent") {
                const std::string child = ReadName(stream, sourceName, lineNumber, "set");
                const std::string parent = ReadName(stream, sourceName, lineNumber, "parent set");
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetObjectSetParent(child, parent);
            } else if (command == "object_set_export") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "set");
                const int enabled = static_cast<int>(ReadDouble(stream, sourceName, lineNumber, "export flag"));
                EnsureLineEnded(stream, sourceName, lineNumber);
                project.SetObjectSetExport(name, enabled != 0);
            } else if (command == "object_set" || command == "object_set_state") {
                const std::string name = ReadName(stream, sourceName, lineNumber, "set");
                const std::string stateToken = ReadName(stream, sourceName, lineNumber, "set state");
                model::ObjectSetState state = model::ObjectSetState::Visible;
                if (stateToken == "reference") {
                    state = model::ObjectSetState::ReferenceOnly;
                } else if (stateToken == "hidden") {
                    state = model::ObjectSetState::Hidden;
                } else if (stateToken != "visible") {
                    throw std::invalid_argument("Set state must be visible, reference, or hidden.");
                }
                if (command == "object_set_state") {
                    EnsureLineEnded(stream, sourceName, lineNumber);
                    project.SetObjectSetState(name, state);
                } else {
                    const int memberCount = static_cast<int>(ReadCount(stream, sourceName, lineNumber, "set member count"));
                    project.CreateObjectSet(name, state);
                    for (int index = 0; index < memberCount; ++index) {
                        const std::string kindToken =
                            ReadName(stream, sourceName, lineNumber, "set member kind");
                        const std::string memberName =
                            ReadName(stream, sourceName, lineNumber, "set member");
                        model::ProjectObjectKind kind = model::ProjectObjectKind::Wire;
                        if (kindToken == "workplane") {
                            kind = model::ProjectObjectKind::WorkPlane;
                        } else if (kindToken == "point") {
                            kind = model::ProjectObjectKind::Point;
                        } else if (kindToken == "wire") {
                            kind = model::ProjectObjectKind::Wire;
                        } else if (kindToken == "surface") {
                            kind = model::ProjectObjectKind::Surface;
                        } else if (kindToken == "plate") {
                            kind = model::ProjectObjectKind::Plate;
                        } else if (kindToken == "body") {
                            kind = model::ProjectObjectKind::Body;
                        } else if (kindToken == "part_model") {
                            kind = model::ProjectObjectKind::PartModel;
                        } else {
                            throw std::invalid_argument(
                                "Set member kind is unknown: " + kindToken);
                        }
                        project.AssignObjectToSet(kind, memberName, name);
                    }
                    EnsureLineEnded(stream, sourceName, lineNumber);
                }
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
                } else if (objectKind == "point") {
                    project.SetPointVisible(name, visible);
                } else if (objectKind == "wire") {
                    project.SetWireVisible(name, visible);
                } else if (objectKind == "surface") {
                    project.SetSurfaceVisible(name, visible);
                } else if (objectKind == "plate") {
                    project.SetPlateVisible(name, visible);
                } else if (objectKind == "body") {
                    project.SetBodyVisible(name, visible);
                } else if (objectKind == "dimension") {
                    project.SetReferenceDimensionVisible(name, visible);
                } else {
                    throw std::invalid_argument(
                        "Visibility object kind must be workplane, point, wire, surface, plate, body, or dimension.");
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
    output << "# Units are millimeters.\n";
    output << "format_version 1\n\n";

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

    if (!project.WorkPlanes().empty() && (!project.Points().empty() || !project.Wires().empty())) {
        output << '\n';
    }

    for (const auto& namedPoint : project.Points()) {
        RequireScriptNameSafe(namedPoint.name, "Point");
        output << "point3d " << namedPoint.name << ' ';
        WriteVector3(output, namedPoint.point);
        output << ' ';
        if (namedPoint.sourcePlaneName.has_value()) {
            RequireScriptNameSafe(*namedPoint.sourcePlaneName, "Point source plane");
            output << *namedPoint.sourcePlaneName;
        } else {
            output << '-';
        }
        output << '\n';
    }

    if (!project.Points().empty() && !project.Wires().empty()) {
        output << '\n';
    }

    for (const auto& namedWire : project.Wires()) {
        if (namedWire.projection.has_value() || namedWire.plateOffset.has_value()
            || namedWire.partModelSourceName.has_value()) {
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

        case WireKind::CubicBSpline:
            output << "bspline3d_knots " << namedWire.name << ' ' << points.size();
            for (const Vector3& point : points) {
                output << ' ';
                WriteVector3(output, point);
            }
            for (double knot : namedWire.wire.BSplineKnots()) {
                output << ' ' << knot;
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
        if (namedWire.metadata.curveConstraints.radiusMillimeters.has_value()) {
            output << "wire_radius_constraint " << namedWire.name << ' '
                   << *namedWire.metadata.curveConstraints.radiusMillimeters << '\n';
        }
        if (namedWire.metadata.construction) {
            output << "wire_role " << namedWire.name << " construction\n";
        }
    }

    const bool hasProjectedWires = std::any_of(project.Wires().begin(), project.Wires().end(), [](const auto& wire) {
        return wire.projection.has_value();
    });
    if (!project.Surfaces().empty() || hasProjectedWires) {
        output << '\n';
    }

    const auto writeSurface = [&](const auto& namedSurface) {
        RequireScriptNameSafe(namedSurface.name, "Surface");
        for (const std::string& sourceWireName : namedSurface.sourceWireNames) {
            RequireScriptNameSafe(sourceWireName, "Surface source wire");
        }
        for (const std::string& guideWireName : namedSurface.guideWireNames) {
            RequireScriptNameSafe(guideWireName, "Surface guide wire");
        }
        if (namedSurface.autoAssembled) {
            // おまかせ面: 読込時に同じ手順(自動連結・整列)で作り直す。
            output << "surface_auto " << namedSurface.name << ' '
                   << namedSurface.sourceWireNames.size();
            for (const std::string& sourceName : namedSurface.sourceWireNames) {
                output << ' ' << sourceName;
            }
            output << '\n';
            return;
        }
        const bool compoundGroups = !namedSurface.sourceWireGroups.empty()
            && std::any_of(
                namedSurface.sourceWireGroups.begin(),
                namedSurface.sourceWireGroups.end(),
                [](const auto& group) { return group.size() > 1; });
        if (compoundGroups
            && namedSurface.surface.Kind() != model::SurfaceKind::Planar) {
            const char* kind = namedSurface.surface.Kind() == model::SurfaceKind::Ruled
                ? "ruled"
                : namedSurface.surface.Kind() == model::SurfaceKind::Loft
                ? "loft"
                : "guided_loft";
            output << "surface_grouped " << kind << ' '
                   << namedSurface.name << ' '
                   << namedSurface.sourceWireGroups.size();
            for (const auto& group : namedSurface.sourceWireGroups) {
                output << ' ' << group.size();
                for (const std::string& sourceName : group) {
                    output << ' ' << sourceName;
                }
            }
            output << '\n';
        } else if (namedSurface.surface.Kind() == model::SurfaceKind::Planar) {
            output << "surface_planar " << namedSurface.name;
            for (const std::string& boundaryName : namedSurface.sourceWireNames) {
                output << ' ' << boundaryName;
            }
            output << '\n';
        } else if (namedSurface.surface.Kind() == model::SurfaceKind::Ruled) {
            output << "surface_ruled " << namedSurface.name << ' '
                   << namedSurface.sourceWireNames.at(0) << ' ' << namedSurface.sourceWireNames.at(1) << '\n';
        } else if (namedSurface.surface.Kind() == model::SurfaceKind::Loft) {
            output << "surface_loft " << namedSurface.name;
            for (const std::string& sectionName : namedSurface.sourceWireNames) {
                output << ' ' << sectionName;
            }
            output << '\n';
        } else if (namedSurface.surface.Kind() == model::SurfaceKind::Gordon) {
            output << "surface_gordon " << namedSurface.name << ' ' << namedSurface.sourceWireNames.size();
            for (const std::string& sectionName : namedSurface.sourceWireNames) {
                output << ' ' << sectionName;
            }
            output << ' ' << namedSurface.guideWireNames.size();
            for (const std::string& guideName : namedSurface.guideWireNames) {
                output << ' ' << guideName;
            }
            output << '\n';
        } else {
            output << "surface_guided_loft " << namedSurface.name;
            for (const std::string& sourceName : namedSurface.sourceWireNames) {
                output << ' ' << sourceName;
            }
            output << '\n';
        }
    };

    const auto writeProjectedWire = [&](const auto& namedWire) {
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
    };

    std::vector<bool> surfaceWritten(project.Surfaces().size(), false);
    std::vector<bool> projectionWritten(project.Wires().size(), false);
    std::size_t pendingSurfaces = project.Surfaces().size();
    std::size_t pendingProjections = static_cast<std::size_t>(std::count_if(
        project.Wires().begin(), project.Wires().end(), [](const auto& wire) {
            return wire.projection.has_value();
        }));
    while (pendingSurfaces > 0 || pendingProjections > 0) {
        bool madeProgress = false;
        for (std::size_t surfaceIndex = 0; surfaceIndex < project.Surfaces().size(); ++surfaceIndex) {
            if (surfaceWritten[surfaceIndex]) {
                continue;
            }
            const auto& surface = project.Surfaces()[surfaceIndex];
            if (surface.partModelSourceName.has_value()) {
                // 部材近似モデルの派生面は保存しない(読み込み時に再生成)。
                surfaceWritten[surfaceIndex] = true;
                --pendingSurfaces;
                madeProgress = true;
                continue;
            }
            const bool sourcesWritten = std::all_of(
                surface.sourceWireNames.begin(), surface.sourceWireNames.end(),
                [&](const std::string& sourceName) {
                    const auto source = std::find_if(
                        project.Wires().begin(), project.Wires().end(), [&](const auto& wire) {
                            return wire.name == sourceName;
                        });
                    if (source == project.Wires().end()) {
                        throw std::logic_error("Surface source wire is missing: " + sourceName);
                    }
                    if (!source->projection.has_value()) {
                        return true;
                    }
                    return static_cast<bool>(projectionWritten[static_cast<std::size_t>(
                        std::distance(project.Wires().begin(), source))]);
                });
            if (!sourcesWritten) {
                continue;
            }
            writeSurface(surface);
            surfaceWritten[surfaceIndex] = true;
            --pendingSurfaces;
            madeProgress = true;
        }
        for (std::size_t wireIndex = 0; wireIndex < project.Wires().size(); ++wireIndex) {
            const auto& wire = project.Wires()[wireIndex];
            if (!wire.projection.has_value() || projectionWritten[wireIndex]) {
                continue;
            }
            if (wire.partModelSourceName.has_value()) {
                // 部材近似モデルの派生開口ワイヤは保存しない(読み込み時に再生成)。
                projectionWritten[wireIndex] = true;
                --pendingProjections;
                madeProgress = true;
                continue;
            }
            const auto target = std::find_if(
                project.Surfaces().begin(), project.Surfaces().end(), [&](const auto& surface) {
                    return surface.name == wire.projection->targetSurfaceName;
                });
            if (target == project.Surfaces().end()) {
                throw std::logic_error("Projection target surface is missing: "
                    + wire.projection->targetSurfaceName);
            }
            const std::size_t targetIndex = static_cast<std::size_t>(
                std::distance(project.Surfaces().begin(), target));
            if (!surfaceWritten[targetIndex]) {
                continue;
            }
            writeProjectedWire(wire);
            projectionWritten[wireIndex] = true;
            --pendingProjections;
            madeProgress = true;
        }
        if (!madeProgress) {
            throw std::logic_error("Surface and projected-wire dependencies contain a cycle.");
        }
    }
    if ((!project.Surfaces().empty() || hasProjectedWires)
        && (!project.CoincidentConstraints().empty() || !project.TangentConstraints().empty()
            || !project.ReferenceDimensions().empty())) {
        output << '\n';
    }
    for (const auto& constraint : project.CoincidentConstraints()) {
        RequireScriptNameSafe(constraint.anchor.wireName, "Coincidence anchor wire");
        RequireScriptNameSafe(constraint.follower.wireName, "Coincidence follower wire");
        output << "wire_coincident "
               << constraint.anchor.wireName << ' ' << WireEndpointToken(constraint.anchor.endpoint) << ' '
               << constraint.follower.wireName << ' ' << WireEndpointToken(constraint.follower.endpoint) << '\n';
    }
    for (const auto& constraint : project.TangentConstraints()) {
        RequireScriptNameSafe(constraint.anchor.wireName, "Tangent anchor wire");
        RequireScriptNameSafe(constraint.follower.wireName, "Tangent follower wire");
        output << (constraint.continuity == WireContinuity::G2Curvature
                    ? "wire_curvature " : "wire_tangent ")
               << constraint.anchor.wireName << ' ' << WireEndpointToken(constraint.anchor.endpoint) << ' '
               << constraint.follower.wireName << ' ' << WireEndpointToken(constraint.follower.endpoint) << '\n';
    }
    for (const auto& dimension : project.ReferenceDimensions()) {
        RequireScriptNameSafe(dimension.name, "Reference dimension");
        output << "reference_dimension " << dimension.name << ' '
               << ReferenceDimensionKindToken(dimension.kind) << ' ';
        WriteDimensionReference(output, dimension.first);
        output << ' ';
        WriteDimensionReference(output, dimension.second);
        output << '\n';
    }

    if (!project.Plates().empty()) {
        output << '\n';
    }
    const auto isPartSurfacePlate = [&project](const model::NamedPlate& namedPlate) {
        for (const auto& surface : project.Surfaces()) {
            if (surface.name == namedPlate.sourceSurfaceName) {
                return surface.partModelSourceName.has_value();
            }
        }
        return false;
    };
    const auto writePlateLine = [&output](const model::NamedPlate& namedPlate) {
        RequireScriptNameSafe(namedPlate.name, "Plate");
        RequireScriptNameSafe(namedPlate.sourceSurfaceName, "Plate source surface");
        RequireScriptNameSafe(namedPlate.material, "Plate material");
        output << (namedPlate.plate.HasVariableThickness() ? "plate_variable " : "plate ")
               << namedPlate.name << ' ' << namedPlate.sourceSurfaceName << ' '
               << namedPlate.plate.Thickness() << ' ';
        if (namedPlate.plate.HasVariableThickness()) {
            output << namedPlate.plate.EndThickness() << ' ';
        }
        output << PlateDirectionToken(namedPlate.plate.Direction()) << ' '
               << namedPlate.material << '\n';
    };
    for (const auto& namedPlate : project.Plates()) {
        if (isPartSurfacePlate(namedPlate)) {
            continue; // 部材面由来の板材は part_model の後で書く。
        }
        writePlateLine(namedPlate);
    }
    for (const auto& namedPlate : project.Plates()) {
        if (namedPlate.plate.Range().IsFull() || isPartSurfacePlate(namedPlate)) {
            continue;
        }
        const auto& range = namedPlate.plate.Range();
        output << "plate_range " << namedPlate.name << ' '
               << range.minimumU << ' ' << range.maximumU << ' '
               << range.minimumV << ' ' << range.maximumV << '\n';
    }
    for (const auto& namedSurface : project.Surfaces()) {
        if (namedSurface.partModelSourceName.has_value()) {
            continue; // 派生面の開口は part_model が再生成する。
        }
        for (const std::string& openingWireName : namedSurface.openingWireNames) {
            RequireScriptNameSafe(openingWireName, "Surface opening wire");
            output << "surface_opening " << namedSurface.name << ' ' << openingWireName << '\n';
        }
    }
    for (const auto& namedPlate : project.Plates()) {
        if (isPartSurfacePlate(namedPlate)) {
            continue;
        }
        for (const std::string& openingWireName : namedPlate.openingWireNames) {
            RequireScriptNameSafe(openingWireName, "Plate opening wire");
            output << "plate_opening " << namedPlate.name << ' ' << openingWireName << '\n';
        }
    }
    for (const auto& namedPlate : project.Plates()) {
        if (isPartSurfacePlate(namedPlate)) {
            continue;
        }
        for (const std::string& cutWireName : namedPlate.reliefCutWireNames) {
            RequireScriptNameSafe(cutWireName, "Plate relief-cut wire");
            output << "plate_relief_cut " << namedPlate.name << ' ' << cutWireName << '\n';
        }
    }
    for (const auto& namedPlate : project.Plates()) {
        if (isPartSurfacePlate(namedPlate)) {
            continue;
        }
        for (const std::string& splitWireName : namedPlate.splitWireNames) {
            RequireScriptNameSafe(splitWireName, "Plate split-line wire");
            output << "plate_split_line " << namedPlate.name << ' ' << splitWireName << '\n';
        }
    }
    for (const auto& namedWire : project.Wires()) {
        if (!namedWire.plateOffset.has_value()) {
            continue;
        }
        RequireScriptNameSafe(namedWire.name, "Plate-offset wire");
        RequireScriptNameSafe(namedWire.plateOffset->sourceWireName, "Plate-offset source wire");
        RequireScriptNameSafe(namedWire.plateOffset->plateName, "Plate-offset plate");
        output << "wire_plate_offset " << namedWire.name << ' '
               << namedWire.plateOffset->sourceWireName << ' '
               << namedWire.plateOffset->plateName << ' '
               << namedWire.plateOffset->throughThickness << '\n';
    }

    if (!project.Bodies().empty()) {
        output << '\n';
    }
    for (const auto& namedBody : project.Bodies()) {
        RequireScriptNameSafe(namedBody.name, "Body");
        RequireScriptNameSafe(namedBody.sourceSurfaceName, "Body source surface");
        const auto& range = namedBody.body.Range();
        output << "body_surface_jig " << namedBody.name << ' '
               << namedBody.sourceSurfaceName << ' '
               << JigSideToken(namedBody.body.Side()) << ' '
               << namedBody.body.ClearanceMillimeters() << ' '
               << namedBody.body.ThicknessMillimeters() << ' '
               << range.minimumU << ' ' << range.maximumU << ' '
               << range.minimumV << ' ' << range.maximumV << '\n';
    }

    for (const auto& model : project.PartModels()) {
        RequireScriptNameSafe(model.name, "Part model");
        const bool fromSurface = !model.sourceSurfaceName.empty();
        const std::string& sourceObjectName
            = fromSurface ? model.sourceSurfaceName : model.sourcePlateName;
        RequireScriptNameSafe(sourceObjectName,
            fromSurface ? "Part-model source surface" : "Part-model source plate");
        output << (fromSurface ? "part_model_surface " : "part_model ")
               << model.name << ' ' << sourceObjectName << ' '
               << (model.options.splitAxis == model::PartSplitAxis::U ? "u" : "v") << ' '
               << (model.options.automaticBoundaries ? 1 : 0) << ' '
               << model.options.maximumDeviationMillimeters << ' '
               << model.options.maximumPartCount << ' '
               << model.options.minimumPartWidthMillimeters << ' '
               << model.options.manualBoundaryParameters.size();
        for (const double parameter : model.options.manualBoundaryParameters) {
            output << ' ' << parameter;
        }
        output << '\n';
        // 可動折り線(合意10): 完成形(全て1)以外のときだけ保存する。
        const bool customFold = std::any_of(
            model.railFoldProgress.begin(), model.railFoldProgress.end(),
            [](double value) { return std::abs(value - 1.0) > 1.0e-12; });
        if (customFold) {
            output << "part_model_fold " << model.name << ' '
                   << model.railFoldProgress.size();
            for (const double value : model.railFoldProgress) {
                output << ' ' << value;
            }
            output << '\n';
        }
        // 組立の進行度(オーナー指示: スライダーで実面が動く)。100%以外のとき保存。
        if (std::abs(model.assemblyProgress - 1.0) > 1.0e-12) {
            output << "part_model_assembly " << model.name << ' '
                   << model.assemblyProgress << '\n';
        }
        // 接続スコープ(合意13)。派生「_接続」は読込時に再生成される。
        if (!model.scopeWireNames.empty() || !model.scopeSurfaceNames.empty()) {
            for (const std::string& scopeName : model.scopeWireNames) {
                RequireScriptNameSafe(scopeName, "Part-model scope wire");
            }
            for (const std::string& scopeName : model.scopeSurfaceNames) {
                RequireScriptNameSafe(scopeName, "Part-model scope surface");
            }
            output << "part_model_scope " << model.name << ' '
                   << model.scopeWireNames.size();
            for (const std::string& scopeName : model.scopeWireNames) {
                output << ' ' << scopeName;
            }
            output << ' ' << model.scopeSurfaceNames.size();
            for (const std::string& scopeName : model.scopeSurfaceNames) {
                output << ' ' << scopeName;
            }
            output << '\n';
        }
        // 部材面への後付け開口(#17b)。派生穴は読込時に再投影される。
        for (const auto& record : model.partOpenings) {
            RequireScriptNameSafe(record.sourceWireName, "Part-model opening source wire");
            output << "part_model_opening " << model.name << ' '
                   << record.partNumber << ' ' << record.sourceWireName << ' '
                   << record.direction.x << ' ' << record.direction.y << ' '
                   << record.direction.z << '\n';
        }
        // 部材ごとの平行移動オフセット。読込時に同じ姿勢へ再構築される。
        for (const auto& offset : model.partOffsets) {
            output << "part_model_part_offset " << model.name << ' '
                   << offset.partNumber << ' ' << offset.delta.x << ' '
                   << offset.delta.y << ' ' << offset.delta.z << '\n';
        }
    }
    for (const auto& namedPlate : project.Plates()) {
        if (!isPartSurfacePlate(namedPlate)) {
            continue;
        }
        writePlateLine(namedPlate);
        if (!namedPlate.plate.Range().IsFull()) {
            const auto& range = namedPlate.plate.Range();
            output << "plate_range " << namedPlate.name << ' '
                   << range.minimumU << ' ' << range.maximumU << ' '
                   << range.minimumV << ' ' << range.maximumV << '\n';
        }
        for (const std::string& openingWireName : namedPlate.openingWireNames) {
            RequireScriptNameSafe(openingWireName, "Plate opening wire");
            output << "plate_opening " << namedPlate.name << ' ' << openingWireName << '\n';
        }
        for (const std::string& cutWireName : namedPlate.reliefCutWireNames) {
            RequireScriptNameSafe(cutWireName, "Plate relief-cut wire");
            output << "plate_relief_cut " << namedPlate.name << ' ' << cutWireName << '\n';
        }
        for (const std::string& splitWireName : namedPlate.splitWireNames) {
            RequireScriptNameSafe(splitWireName, "Plate split-line wire");
            output << "plate_split_line " << namedPlate.name << ' ' << splitWireName << '\n';
        }
    }
    for (const auto& namedPlate : project.Plates()) {
        if (namedPlate.laminateBaseName.empty()) {
            continue;
        }
        RequireScriptNameSafe(namedPlate.laminateBaseName, "Laminate base plate");
        output << "plate_laminate " << namedPlate.name << ' '
               << namedPlate.laminateBaseName << '\n';
    }
    const auto setStateToken = [](model::ObjectSetState state) {
        switch (state) {
        case model::ObjectSetState::ReferenceOnly:
            return "reference";
        case model::ObjectSetState::Hidden:
            return "hidden";
        case model::ObjectSetState::Visible:
        default:
            return "visible";
        }
    };
    const auto setMemberKindToken = [](model::ProjectObjectKind kind) {
        switch (kind) {
        case model::ProjectObjectKind::WorkPlane: return "workplane";
        case model::ProjectObjectKind::Point: return "point";
        case model::ProjectObjectKind::Wire: return "wire";
        case model::ProjectObjectKind::Surface: return "surface";
        case model::ProjectObjectKind::Plate: return "plate";
        case model::ProjectObjectKind::Body: return "body";
        case model::ProjectObjectKind::PartModel:
        default: return "part_model";
        }
    };
    for (const auto& set : project.ObjectSets()) {
        RequireScriptNameSafe(set.name, "Set");
        if (set.automatic) {
            // 自動セットは part_model が再生成するため、状態だけ保存する。
            if (set.state != model::ObjectSetState::Visible) {
                output << "object_set_state " << set.name << ' '
                       << setStateToken(set.state) << '\n';
            }
            continue;
        }
        output << "object_set " << set.name << ' ' << setStateToken(set.state)
               << ' ' << set.members.size();
        for (const auto& member : set.members) {
            RequireScriptNameSafe(member.name, "Set member");
            output << ' ' << setMemberKindToken(member.kind) << ' ' << member.name;
        }
        output << '\n';
    }

    const bool hasHiddenObjects = std::any_of(project.WorkPlanes().begin(), project.WorkPlanes().end(), [](const auto& plane) {
        return !plane.visible;
    }) || std::any_of(project.Points().begin(), project.Points().end(), [](const auto& point) {
        return !point.visible;
    }) || std::any_of(project.Wires().begin(), project.Wires().end(), [](const auto& wire) {
        return !wire.visible;
    }) || std::any_of(project.Surfaces().begin(), project.Surfaces().end(), [](const auto& surface) {
        return !surface.visible;
    }) || std::any_of(project.Plates().begin(), project.Plates().end(), [](const auto& plate) {
        return !plate.visible;
    }) || std::any_of(project.Bodies().begin(), project.Bodies().end(), [](const auto& body) {
        return !body.visible;
    }) || std::any_of(project.ReferenceDimensions().begin(), project.ReferenceDimensions().end(), [](const auto& dimension) {
        return !dimension.visible;
    });
    if (hasHiddenObjects) {
        output << '\n';
    }
    for (const auto& plane : project.WorkPlanes()) {
        if (!plane.visible) {
            output << "visibility workplane " << plane.name << " hidden\n";
        }
    }
    for (const auto& point : project.Points()) {
        if (!point.visible) {
            output << "visibility point " << point.name << " hidden\n";
        }
    }
    for (const auto& wire : project.Wires()) {
        if (!wire.visible && !wire.partModelSourceName.has_value()) {
            output << "visibility wire " << wire.name << " hidden\n";
        }
    }
    for (const auto& surface : project.Surfaces()) {
        if (!surface.visible && !surface.partModelSourceName.has_value()) {
            output << "visibility surface " << surface.name << " hidden\n";
        }
    }
    for (const auto& plate : project.Plates()) {
        if (!plate.visible) {
            output << "visibility plate " << plate.name << " hidden\n";
        }
    }
    for (const auto& body : project.Bodies()) {
        if (!body.visible) {
            output << "visibility body " << body.name << " hidden\n";
        }
    }
    for (const auto& dimension : project.ReferenceDimensions()) {
        if (!dimension.visible) {
            output << "visibility dimension " << dimension.name << " hidden\n";
        }
    }
    // 出力除外フラグと親子関係は全セット(自動セット含む)が生成された後に
    // 読めるよう末尾に書く。
    for (const auto& set : project.ObjectSets()) {
        if (!set.exportEnabled) {
            output << "object_set_export " << set.name << " 0\n";
        }
    }
    for (const auto& set : project.ObjectSets()) {
        if (!set.parentName.empty()) {
            output << "object_set_parent " << set.name << ' ' << set.parentName << '\n';
        }
    }
}

} // namespace kachakacha::io
