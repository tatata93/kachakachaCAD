#include "kachakacha/io/PlanarExport.h"

#include "kachakacha/geometry/Vector2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace kachakacha::io {

using geometry::Vector2;
using geometry::Vector3;
using model::NamedWire;
using model::Wire;
using model::WireKind;
using model::WorkPlane;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct PlanarPolyline {
    std::string name;
    std::vector<Vector2> points;
    bool closed = false;
};

double DistanceToChord(Vector3 point, Vector3 start, Vector3 end)
{
    const Vector3 chord = end - start;
    if (chord.LengthSquared() <= 1.0e-24) {
        return (point - start).Length();
    }
    const double parameter = std::clamp(geometry::Dot(point - start, chord) / chord.LengthSquared(), 0.0, 1.0);
    return (point - (start + chord * parameter)).Length();
}

void FlattenBezier(
    const std::array<Vector3, 4>& points,
    double tolerance,
    int depth,
    std::vector<Vector3>& result)
{
    const double flatness = std::max(
        DistanceToChord(points[1], points[0], points[3]),
        DistanceToChord(points[2], points[0], points[3]));
    if (flatness <= tolerance || depth >= 24) {
        result.push_back(points[3]);
        return;
    }

    const Vector3 p01 = (points[0] + points[1]) * 0.5;
    const Vector3 p12 = (points[1] + points[2]) * 0.5;
    const Vector3 p23 = (points[2] + points[3]) * 0.5;
    const Vector3 p012 = (p01 + p12) * 0.5;
    const Vector3 p123 = (p12 + p23) * 0.5;
    const Vector3 midpoint = (p012 + p123) * 0.5;
    FlattenBezier({points[0], p01, p012, midpoint}, tolerance, depth + 1, result);
    FlattenBezier({midpoint, p123, p23, points[3]}, tolerance, depth + 1, result);
}

std::vector<Vector3> FlattenWire(const Wire& wire, double tolerance)
{
    switch (wire.Kind()) {
    case WireKind::Line:
    case WireKind::Polyline:
        return wire.ControlPoints();

    case WireKind::CubicBezier: {
        const auto& controls = wire.ControlPoints();
        std::vector<Vector3> points = {controls[0]};
        FlattenBezier({controls[0], controls[1], controls[2], controls[3]}, tolerance, 0, points);
        return points;
    }

    case WireKind::Circle:
    case WireKind::CircularArc: {
        const auto arc = wire.ArcData();
        const double ratio = std::clamp(1.0 - tolerance / arc.radius, -1.0, 1.0);
        double maximumAngle = 2.0 * std::acos(ratio);
        if (!std::isfinite(maximumAngle) || maximumAngle <= 1.0e-8) {
            maximumAngle = kPi / 180.0;
        }
        const int minimumSegments = wire.Kind() == WireKind::Circle ? 12 : 2;
        const int segmentCount = std::clamp(
            static_cast<int>(std::ceil(std::abs(arc.sweepAngleRadians) / maximumAngle)),
            minimumSegments,
            100000);
        std::vector<Vector3> points;
        points.reserve(static_cast<std::size_t>(segmentCount) + 1);
        for (int segment = 0; segment <= segmentCount; ++segment) {
            points.push_back(wire.Evaluate(static_cast<double>(segment) / segmentCount));
        }
        return points;
    }
    }
    throw std::logic_error("Unknown wire kind.");
}

void ValidateOptions(const PlanarExportOptions& options)
{
    if (!std::isfinite(options.chordToleranceMillimeters) || options.chordToleranceMillimeters <= 0.0
        || !std::isfinite(options.marginMillimeters) || options.marginMillimeters < 0.0
        || !std::isfinite(options.planeToleranceMillimeters) || options.planeToleranceMillimeters <= 0.0) {
        throw std::invalid_argument("Planar export options must contain valid positive tolerances and margin.");
    }
}

std::vector<PlanarPolyline> ProjectWires(
    const WorkPlane& plane,
    const std::vector<NamedWire>& wires,
    const PlanarExportOptions& options)
{
    ValidateOptions(options);
    if (wires.empty()) {
        throw std::invalid_argument("No wires were selected for planar export.");
    }

    std::vector<PlanarPolyline> projected;
    projected.reserve(wires.size());
    for (const NamedWire& named : wires) {
        if (named.metadata.construction) {
            continue;
        }
        PlanarPolyline polyline;
        polyline.name = named.name;
        polyline.closed = named.wire.IsClosed();
        for (const Vector3 point : FlattenWire(named.wire, options.chordToleranceMillimeters)) {
            const auto coordinates = plane.Project(point);
            if (std::abs(coordinates.w) > options.planeToleranceMillimeters) {
                throw std::invalid_argument("A wire is not on the selected export plane: " + named.name);
            }
            polyline.points.push_back({coordinates.u, coordinates.v});
        }
        projected.push_back(std::move(polyline));
    }
    if (projected.empty()) {
        throw std::invalid_argument("No model wires were selected for planar export.");
    }
    return projected;
}

void WriteDxfPair(std::ostream& output, int code, std::string_view value)
{
    output << code << '\n' << value << '\n';
}

} // namespace

bool WireLiesOnWorkPlane(const Wire& wire, const WorkPlane& plane, double toleranceMillimeters)
{
    if (!std::isfinite(toleranceMillimeters) || toleranceMillimeters <= 0.0) {
        return false;
    }
    try {
        const int samples = wire.Kind() == WireKind::Line ? 1 : 96;
        for (int sample = 0; sample <= samples; ++sample) {
            if (std::abs(plane.Project(wire.Evaluate(static_cast<double>(sample) / samples)).w) > toleranceMillimeters) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

void WritePlanarSvg(
    std::ostream& output,
    const WorkPlane& plane,
    const std::vector<NamedWire>& wires,
    PlanarExportOptions options)
{
    const auto polylines = ProjectWires(plane, wires, options);
    double minimumU = std::numeric_limits<double>::infinity();
    double minimumV = std::numeric_limits<double>::infinity();
    double maximumU = -std::numeric_limits<double>::infinity();
    double maximumV = -std::numeric_limits<double>::infinity();
    for (const auto& polyline : polylines) {
        for (const Vector2 point : polyline.points) {
            minimumU = std::min(minimumU, point.x);
            minimumV = std::min(minimumV, point.y);
            maximumU = std::max(maximumU, point.x);
            maximumV = std::max(maximumV, point.y);
        }
    }

    const double width = maximumU - minimumU + options.marginMillimeters * 2.0;
    const double height = maximumV - minimumV + options.marginMillimeters * 2.0;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6);
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
           << "mm\" height=\"" << height << "mm\" viewBox=\"0 0 " << width << ' ' << height << "\">\n"
           << "  <g fill=\"none\" stroke=\"#000000\" stroke-width=\"0.1\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
    for (const auto& polyline : polylines) {
        output << "    <polyline points=\"";
        for (std::size_t index = 0; index < polyline.points.size(); ++index) {
            if (index > 0) {
                output << ' ';
            }
            output << polyline.points[index].x - minimumU + options.marginMillimeters << ','
                   << maximumV - polyline.points[index].y + options.marginMillimeters;
        }
        output << "\"/>\n";
    }
    output << "  </g>\n</svg>\n";
    if (!output) {
        throw std::runtime_error("Failed to write SVG output.");
    }
}

void WritePlanarDxf(
    std::ostream& output,
    const WorkPlane& plane,
    const std::vector<NamedWire>& wires,
    PlanarExportOptions options)
{
    const auto polylines = ProjectWires(plane, wires, options);
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(9);
    WriteDxfPair(output, 0, "SECTION");
    WriteDxfPair(output, 2, "HEADER");
    WriteDxfPair(output, 9, "$INSUNITS");
    WriteDxfPair(output, 70, "4");
    WriteDxfPair(output, 0, "ENDSEC");
    WriteDxfPair(output, 0, "SECTION");
    WriteDxfPair(output, 2, "ENTITIES");
    for (const auto& polyline : polylines) {
        WriteDxfPair(output, 0, "POLYLINE");
        WriteDxfPair(output, 8, "CUT");
        WriteDxfPair(output, 66, "1");
        WriteDxfPair(output, 70, polyline.closed ? "1" : "0");
        for (const Vector2 point : polyline.points) {
            WriteDxfPair(output, 0, "VERTEX");
            WriteDxfPair(output, 8, "CUT");
            output << "10\n" << point.x << "\n20\n" << point.y << "\n30\n0.000000000\n";
        }
        WriteDxfPair(output, 0, "SEQEND");
    }
    WriteDxfPair(output, 0, "ENDSEC");
    WriteDxfPair(output, 0, "EOF");
    if (!output) {
        throw std::runtime_error("Failed to write DXF output.");
    }
}

} // namespace kachakacha::io
