#include "kachakacha/io/FacetedPapercraft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace kachakacha::io {

using geometry::Vector2;
using geometry::Vector3;
using model::NamedPlate;
using model::NamedWire;
using model::Plate;
using model::PlateDevelopability;
using model::Project;
using model::WireKind;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPointTolerance = 1.0e-7;

struct UvPath {
    std::string name;
    std::vector<Vector2> points;
};

struct Facet {
    std::array<int, 3> vertices;
    int strip = 0;
};

using Edge = std::pair<int, int>;

struct BoundarySegment {
    Vector2 first;
    Vector2 second;
    int face = -1;
    Edge edge;
};

struct RigidTransform {
    std::array<std::array<double, 3>, 3> rotation{{
        {{1.0, 0.0, 0.0}},
        {{0.0, 1.0, 0.0}},
        {{0.0, 0.0, 1.0}},
    }};
    Vector3 translation;

    [[nodiscard]] Vector3 Apply(Vector3 point) const noexcept
    {
        return {
            rotation[0][0] * point.x + rotation[0][1] * point.y
                + rotation[0][2] * point.z + translation.x,
            rotation[1][0] * point.x + rotation[1][1] * point.y
                + rotation[1][2] * point.z + translation.y,
            rotation[2][0] * point.x + rotation[2][1] * point.y
                + rotation[2][2] * point.z + translation.z,
        };
    }
};

double Length(Vector2 value)
{
    return std::hypot(value.x, value.y);
}

double Cross2(Vector2 first, Vector2 second)
{
    return first.x * second.y - first.y * second.x;
}

bool AlmostSame(Vector2 first, Vector2 second, double tolerance = kPointTolerance)
{
    return Length(first - second) <= tolerance;
}

Edge OrderedEdge(int first, int second)
{
    return std::minmax(first, second);
}

Vector3 RotateAroundAxis(Vector3 value, Vector3 axis, double angleRadians)
{
    if (axis.LengthSquared() <= 1.0e-18 || std::abs(angleRadians) <= 1.0e-15) {
        return value;
    }
    axis = axis.Normalized();
    const double cosine = std::cos(angleRadians);
    const double sine = std::sin(angleRadians);
    return value * cosine
        + geometry::Cross(axis, value) * sine
        + axis * geometry::Dot(axis, value) * (1.0 - cosine);
}

RigidTransform RotationAroundLine(
    Vector3 axisPoint,
    Vector3 axisDirection,
    double angleRadians)
{
    RigidTransform transform;
    if (axisDirection.LengthSquared() <= 1.0e-18
        || std::abs(angleRadians) <= 1.0e-15) {
        return transform;
    }
    const Vector3 axis = axisDirection.Normalized();
    const double cosine = std::cos(angleRadians);
    const double sine = std::sin(angleRadians);
    const double oneMinusCosine = 1.0 - cosine;
    transform.rotation = {{
        {{cosine + axis.x * axis.x * oneMinusCosine,
          axis.x * axis.y * oneMinusCosine - axis.z * sine,
          axis.x * axis.z * oneMinusCosine + axis.y * sine}},
        {{axis.y * axis.x * oneMinusCosine + axis.z * sine,
          cosine + axis.y * axis.y * oneMinusCosine,
          axis.y * axis.z * oneMinusCosine - axis.x * sine}},
        {{axis.z * axis.x * oneMinusCosine - axis.y * sine,
          axis.z * axis.y * oneMinusCosine + axis.x * sine,
          cosine + axis.z * axis.z * oneMinusCosine}},
    }};
    const Vector3 rotatedAxisPoint = transform.Apply(axisPoint);
    transform.translation = axisPoint - rotatedAxisPoint;
    return transform;
}

RigidTransform Compose(RigidTransform after, const RigidTransform& before)
{
    RigidTransform result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.rotation[row][column] = 0.0;
            for (int inner = 0; inner < 3; ++inner) {
                result.rotation[row][column]
                    += after.rotation[row][inner] * before.rotation[inner][column];
            }
        }
    }
    result.translation = after.Apply(before.translation);
    return result;
}

Vector3 TriangleNormal(const std::array<Vector3, 3>& triangle)
{
    return geometry::Cross(
        triangle[1] - triangle[0], triangle[2] - triangle[0]).Normalized();
}

double NormalAngleDegrees(Vector3 first, Vector3 second)
{
    return std::acos(std::clamp(geometry::Dot(first, second), -1.0, 1.0))
        * 180.0 / kPi;
}

void ClosePath(std::vector<Vector2>& points)
{
    if (points.size() < 3) {
        throw std::invalid_argument("A faceted papercraft boundary collapsed.");
    }
    if (!AlmostSame(points.front(), points.back())) {
        points.push_back(points.front());
    }
}

const NamedWire& RequireNamedWire(const Project& project, std::string_view name)
{
    const auto position = std::find_if(
        project.Wires().begin(), project.Wires().end(),
        [&](const NamedWire& wire) { return wire.name == name; });
    if (position == project.Wires().end()) {
        throw std::invalid_argument(
            "Faceted papercraft source wire was not found: " + std::string(name));
    }
    return *position;
}

std::vector<Vector2> BuildPlateWireUvPath(
    const Project& project,
    const NamedPlate& namedPlate,
    std::string_view wireName,
    int requestedSamples,
    bool close)
{
    const NamedWire& projected = RequireNamedWire(project, wireName);
    if (!projected.projection.has_value()
        || projected.projection->targetSurfaceName != namedPlate.sourceSurfaceName) {
        throw std::invalid_argument(
            "A curved-plate papercraft wire must be projected onto the selected plate: "
            + projected.name);
    }
    const NamedWire& source = RequireNamedWire(
        project, projected.projection->sourceWireName);
    const auto& plate = namedPlate.plate;
    const auto& range = plate.Range();
    const int samples = source.wire.Kind() == WireKind::Polyline
        ? std::max(requestedSamples,
            static_cast<int>(source.wire.ControlPoints().size()) * 4)
        : requestedSamples;
    std::vector<Vector2> result;
    result.reserve(static_cast<std::size_t>(samples) + 1);
    for (int sample = 0; sample <= samples; ++sample) {
        const double parameter = static_cast<double>(sample) / samples;
        const auto hit = plate.SourceSurface().ProjectPointAlongDirection(
            source.wire.Evaluate(parameter), projected.projection->direction);
        const double u = (hit.u - range.minimumU)
            / (range.maximumU - range.minimumU);
        const double v = (hit.v - range.minimumV)
            / (range.maximumV - range.minimumV);
        constexpr double rangeTolerance = 1.0e-5;
        if (u < -rangeTolerance || u > 1.0 + rangeTolerance
            || v < -rangeTolerance || v > 1.0 + rangeTolerance) {
            throw std::invalid_argument(
                "Papercraft wire lies outside the selected plate: " + projected.name);
        }
        const Vector2 point{std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0)};
        if (result.empty() || !AlmostSame(result.back(), point)) {
            result.push_back(point);
        }
    }
    if (close) {
        ClosePath(result);
    }
    return result;
}

bool PointInsidePath(Vector2 point, const UvPath& path)
{
    if (path.points.size() < 4) {
        return false;
    }
    bool inside = false;
    for (std::size_t first = 0, second = path.points.size() - 1;
         first < path.points.size(); second = first++) {
        const Vector2 a = path.points[first];
        const Vector2 b = path.points[second];
        if ((a.y > point.y) != (b.y > point.y)
            && point.x < (b.x - a.x) * (point.y - a.y)
                    / (b.y - a.y + 1.0e-30) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

bool SegmentsIntersect(Vector2 a, Vector2 b, Vector2 c, Vector2 d)
{
    constexpr double tolerance = 1.0e-10;
    const double first = Cross2(b - a, c - a);
    const double second = Cross2(b - a, d - a);
    const double third = Cross2(d - c, a - c);
    const double fourth = Cross2(d - c, b - c);
    return ((first > tolerance && second < -tolerance)
            || (first < -tolerance && second > tolerance))
        && ((third > tolerance && fourth < -tolerance)
            || (third < -tolerance && fourth > tolerance));
}

Vector2 PlaceTriangleVertex(
    Vector2 first,
    Vector2 second,
    double firstRadius,
    double secondRadius,
    Vector2 reference)
{
    const Vector2 edge = second - first;
    const double edgeLength = Length(edge);
    if (edgeLength <= 1.0e-12) {
        throw std::invalid_argument("Faceted papercraft contains a collapsed edge.");
    }
    const Vector2 direction = edge * (1.0 / edgeLength);
    const Vector2 perpendicular{-direction.y, direction.x};
    const double along = (firstRadius * firstRadius - secondRadius * secondRadius
        + edgeLength * edgeLength) / (2.0 * edgeLength);
    const double height = std::sqrt(std::max(
        0.0, firstRadius * firstRadius - along * along));
    const Vector2 base = first + direction * along;
    const Vector2 positive = base + perpendicular * height;
    const Vector2 negative = base - perpendicular * height;
    const double referenceSide = Cross2(edge, reference - first);
    if (std::abs(referenceSide) <= 1.0e-14) {
        return positive;
    }
    return Cross2(edge, positive - first) * referenceSide <= 0.0
        ? positive
        : negative;
}

bool TrianglesOverlap(
    const std::array<Vector2, 3>& first,
    const std::array<Vector2, 3>& second)
{
    constexpr double tolerance = 1.0e-8;
    const auto separated = [&](const std::array<Vector2, 3>& source) {
        for (int edge = 0; edge < 3; ++edge) {
            const Vector2 direction
                = source[static_cast<std::size_t>((edge + 1) % 3)]
                - source[static_cast<std::size_t>(edge)];
            const Vector2 axis{-direction.y, direction.x};
            double firstMinimum = std::numeric_limits<double>::infinity();
            double firstMaximum = -std::numeric_limits<double>::infinity();
            double secondMinimum = std::numeric_limits<double>::infinity();
            double secondMaximum = -std::numeric_limits<double>::infinity();
            for (int point = 0; point < 3; ++point) {
                const double firstProjection = first[static_cast<std::size_t>(point)].x * axis.x
                    + first[static_cast<std::size_t>(point)].y * axis.y;
                const double secondProjection = second[static_cast<std::size_t>(point)].x * axis.x
                    + second[static_cast<std::size_t>(point)].y * axis.y;
                firstMinimum = std::min(firstMinimum, firstProjection);
                firstMaximum = std::max(firstMaximum, firstProjection);
                secondMinimum = std::min(secondMinimum, secondProjection);
                secondMaximum = std::max(secondMaximum, secondProjection);
            }
            if (std::min(firstMaximum, secondMaximum)
                    - std::max(firstMinimum, secondMinimum) <= tolerance) {
                return true;
            }
        }
        return false;
    };
    return !separated(first) && !separated(second);
}

double TargetSpacing(int fidelity)
{
    const double parameter = static_cast<double>(std::clamp(fidelity, 1, 10) - 1) / 9.0;
    return 22.0 * std::pow(2.5 / 22.0, parameter);
}

double SampledLength(const Plate& plate, bool alongU)
{
    constexpr int samples = 96;
    double length = 0.0;
    Vector3 previous = alongU
        ? plate.Evaluate(0.0, 0.5, 0.5)
        : plate.Evaluate(0.5, 0.0, 0.5);
    for (int sample = 1; sample <= samples; ++sample) {
        const double parameter = static_cast<double>(sample) / samples;
        const Vector3 current = alongU
            ? plate.Evaluate(parameter, 0.5, 0.5)
            : plate.Evaluate(0.5, parameter, 0.5);
        length += (current - previous).Length();
        previous = current;
    }
    return length;
}

std::vector<double> BuildParameters(
    int intervals,
    const std::vector<UvPath>& openings,
    bool useU,
    int fidelity)
{
    std::vector<double> parameters;
    parameters.reserve(static_cast<std::size_t>(intervals) + 40);
    for (int interval = 0; interval <= intervals; ++interval) {
        parameters.push_back(static_cast<double>(interval) / intervals);
    }
    const int desiredFeatures = std::clamp(fidelity * 2, 4, 20);
    std::vector<double> featureParameters;
    for (const UvPath& opening : openings) {
        if (opening.points.empty()) {
            continue;
        }
        const int stride = std::max(
            1, static_cast<int>(opening.points.size()) / desiredFeatures);
        for (std::size_t index = 0; index < opening.points.size();
             index += static_cast<std::size_t>(stride)) {
            featureParameters.push_back(
                useU ? opening.points[index].x : opening.points[index].y);
        }
    }
    std::sort(featureParameters.begin(), featureParameters.end());
    featureParameters.erase(std::unique(
        featureParameters.begin(), featureParameters.end(),
        [](double first, double second) {
            return std::abs(first - second) <= 1.0e-5;
        }), featureParameters.end());
    const std::size_t maximumAdded = static_cast<std::size_t>(
        std::clamp(4 + fidelity * 2, 6, 24));
    if (featureParameters.size() <= maximumAdded) {
        parameters.insert(
            parameters.end(), featureParameters.begin(), featureParameters.end());
    } else {
        for (std::size_t index = 0; index < maximumAdded; ++index) {
            const std::size_t source = static_cast<std::size_t>(std::llround(
                static_cast<double>(index) * (featureParameters.size() - 1)
                / static_cast<double>(maximumAdded - 1)));
            parameters.push_back(featureParameters[source]);
        }
    }
    std::sort(parameters.begin(), parameters.end());
    parameters.erase(std::unique(parameters.begin(), parameters.end(),
        [](double first, double second) {
            return std::abs(first - second) <= 1.0e-5;
        }), parameters.end());
    if (parameters.size() > 97) {
        std::vector<double> reduced;
        reduced.reserve(97);
        for (int index = 0; index <= 96; ++index) {
            const std::size_t source = static_cast<std::size_t>(std::llround(
                static_cast<double>(index) * (parameters.size() - 1) / 96.0));
            reduced.push_back(parameters[source]);
        }
        parameters = std::move(reduced);
    }
    parameters.front() = 0.0;
    parameters.back() = 1.0;
    return parameters;
}

class FacetedGenerator {
public:
    FacetedGenerator(
        const Project& project,
        const NamedPlate& namedPlate,
        PlateFlatPatternOptions options)
        : project_(project), namedPlate_(namedPlate), plate_(namedPlate.plate),
          options_(std::move(options))
    {
        ValidateOptions();
        ReadFeaturePaths();
        BuildMesh();
        BuildAdjacency();
        MarkManualCuts();
        DevelopWithoutOverlaps();
        BuildPieces();
    }

    [[nodiscard]] PlateFlatPattern Pattern() const
    {
        PlateFlatPattern result;
        result.plateName = namedPlate_.name + "_faceted_papercraft";
        result.pieces = pieces_;
        result.analysis.classification = plate_.AnalyzeDevelopability().classification;
        result.analysis.pieceCount = static_cast<int>(pieces_.size());
        result.analysis.maximumEdgeDistortionMillimeters = maximumEdgeError_;
        result.analysis.rootMeanSquareEdgeDistortionMillimeters = rmsEdgeError_;
        result.analysis.maximumBoundaryApproximationMillimeters = maximumDeviation_;
        result.analysis.automaticNotchCount = notchCount_;
        for (const auto& piece : pieces_) {
            if (result.outerBoundary.points.empty()) {
                result.outerBoundary = piece.outerBoundary;
            }
            result.openings.insert(
                result.openings.end(), piece.openings.begin(), piece.openings.end());
            result.foldLines.insert(
                result.foldLines.end(), piece.foldLines.begin(), piece.foldLines.end());
            result.reliefCuts.insert(
                result.reliefCuts.end(), piece.reliefCuts.begin(), piece.reliefCuts.end());
        }
        return result;
    }

    [[nodiscard]] PlateAssemblyGuide Guide() const
    {
        PlateAssemblyGuide result;
        result.plateName = namedPlate_.name + "_faceted_papercraft";
        std::set<Edge> addedFolds;
        std::set<Edge> addedCuts;
        for (const auto& [edge, adjacent] : edgeFaces_) {
            if (adjacent.size() != 2
                || !activeFaces_.contains(adjacent[0])
                || !activeFaces_.contains(adjacent[1])) {
                continue;
            }
            const std::vector<Vector3> points{
                spatial_[static_cast<std::size_t>(edge.first)],
                spatial_[static_cast<std::size_t>(edge.second)],
            };
            if (cutEdges_.contains(edge)) {
                if (addedCuts.insert(edge).second) {
                    result.splitLines.push_back({
                        "faceted_cut_" + std::to_string(result.splitLines.size() + 1),
                        points,
                    });
                }
            } else if (addedFolds.insert(edge).second
                && EdgeFoldAngle(edge) + 1.0e-9 >= options_.minimumFoldAngleDegrees) {
                result.foldLines.push_back({
                    "faceted_fold_" + std::to_string(result.foldLines.size() + 1),
                    points,
                });
            }
        }
        return result;
    }

    [[nodiscard]] PlateAssemblyMotion Motion(double progress) const
    {
        if (!std::isfinite(progress)) {
            throw std::invalid_argument("Papercraft assembly progress must be finite.");
        }
        PlateAssemblyMotion motion;
        motion.plateName = namedPlate_.name + "_faceted_papercraft";
        motion.progress = std::clamp(progress, 0.0, 1.0);
        motion.pieceCount = static_cast<int>(pieces_.size());

        const Vector3 layoutOrigin = plate_.Evaluate(0.0, 0.0, 0.5);
        Vector3 layoutNormal = plate_.SourceSurface().Normal(
            plate_.SourceU(0.01), plate_.SourceV(0.01));
        Vector3 layoutX = plate_.Evaluate(0.01, 0.0, 0.5) - layoutOrigin;
        layoutX = layoutX - layoutNormal * geometry::Dot(layoutX, layoutNormal);
        if (layoutX.LengthSquared() <= 1.0e-18) {
            layoutX = plate_.Evaluate(0.0, 0.01, 0.5) - layoutOrigin;
            layoutX = layoutX - layoutNormal * geometry::Dot(layoutX, layoutNormal);
        }
        layoutX = layoutX.Normalized();
        Vector3 layoutY = geometry::Cross(layoutNormal, layoutX).Normalized();
        if (geometry::Dot(
                layoutY, plate_.Evaluate(0.0, 0.01, 0.5) - layoutOrigin) < 0.0) {
            layoutY = -layoutY;
            layoutNormal = -layoutNormal;
        }

        std::vector<std::array<Vector3, 3>> current(faces_.size());
        for (const int face : activeFaces_) {
            const int piece = facePiece_[static_cast<std::size_t>(face)];
            for (int corner = 0; corner < 3; ++corner) {
                const Vector2 point = flat_[static_cast<std::size_t>(face)]
                    [static_cast<std::size_t>(corner)]
                    + pieceOffsets_[static_cast<std::size_t>(piece)];
                current[static_cast<std::size_t>(face)][static_cast<std::size_t>(corner)]
                    = layoutOrigin + layoutX * point.x + layoutY * point.y;
            }
        }

        const auto targetTriangle = [&](int face) {
            std::array<Vector3, 3> result;
            for (int corner = 0; corner < 3; ++corner) {
                result[static_cast<std::size_t>(corner)] = spatial_[static_cast<std::size_t>(
                    faces_[static_cast<std::size_t>(face)].vertices[static_cast<std::size_t>(corner)])];
            }
            return result;
        };
        const auto currentVertex = [&](int face, int vertex) {
            const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
            const auto position = std::find(vertices.begin(), vertices.end(), vertex);
            if (position == vertices.end()) {
                throw std::logic_error("Faceted papercraft hinge vertex was not found.");
            }
            return current[static_cast<std::size_t>(face)]
                [static_cast<std::size_t>(std::distance(vertices.begin(), position))];
        };

        for (std::size_t pieceIndex = 0; pieceIndex < components_.size(); ++pieceIndex) {
            const auto& component = components_[pieceIndex];
            if (component.empty()) {
                continue;
            }
            const int root = component.front();
            const auto flatRoot = current[static_cast<std::size_t>(root)];
            const auto targetRoot = targetTriangle(root);
            const Vector3 flatX = (flatRoot[1] - flatRoot[0]).Normalized();
            const Vector3 flatNormal = TriangleNormal(flatRoot);
            const Vector3 targetX = (targetRoot[1] - targetRoot[0]).Normalized();
            const Vector3 targetNormal = TriangleNormal(targetRoot);
            Vector3 firstAxis = geometry::Cross(flatNormal, targetNormal);
            double firstAngle = 0.0;
            if (firstAxis.LengthSquared() > 1.0e-18) {
                firstAngle = std::atan2(
                    firstAxis.Length(), geometry::Dot(flatNormal, targetNormal));
                firstAxis = firstAxis.Normalized();
            } else if (geometry::Dot(flatNormal, targetNormal) < 0.0) {
                firstAxis = flatX;
                firstAngle = kPi;
            } else {
                firstAxis = flatX;
            }
            const Vector3 alignedX = RotateAroundAxis(flatX, firstAxis, firstAngle);
            const double twistAngle = std::atan2(
                geometry::Dot(targetNormal, geometry::Cross(alignedX, targetX)),
                geometry::Dot(alignedX, targetX));
            const Vector3 partialNormal = RotateAroundAxis(
                flatNormal, firstAxis, firstAngle * motion.progress);
            const Vector3 rootPosition = flatRoot[0] * (1.0 - motion.progress)
                + targetRoot[0] * motion.progress;
            for (const int face : component) {
                for (Vector3& point : current[static_cast<std::size_t>(face)]) {
                    Vector3 relative = point - flatRoot[0];
                    relative = RotateAroundAxis(
                        relative, firstAxis, firstAngle * motion.progress);
                    relative = RotateAroundAxis(
                        relative, partialNormal, twistAngle * motion.progress);
                    point = rootPosition + relative;
                }
            }

            std::vector<RigidTransform> cumulative(faces_.size());
            for (std::size_t order = 1; order < component.size(); ++order) {
                const int child = component[order];
                const int parent = parentFace_[static_cast<std::size_t>(child)];
                const Edge edge = parentEdge_[static_cast<std::size_t>(child)];
                if (parent < 0) {
                    continue;
                }
                const Vector3 axisStart = currentVertex(parent, edge.first);
                const Vector3 axisEnd = currentVertex(parent, edge.second);
                const Vector3 sourceAxis
                    = (spatial_[static_cast<std::size_t>(edge.second)]
                        - spatial_[static_cast<std::size_t>(edge.first)]).Normalized();
                const double foldAngle = std::atan2(
                    geometry::Dot(sourceAxis,
                        geometry::Cross(
                            TriangleNormal(targetTriangle(parent)),
                            TriangleNormal(targetTriangle(child)))),
                    geometry::Dot(
                        TriangleNormal(targetTriangle(parent)),
                        TriangleNormal(targetTriangle(child))));
                const RigidTransform hinge = RotationAroundLine(
                    axisStart, axisEnd - axisStart, foldAngle * motion.progress);
                cumulative[static_cast<std::size_t>(child)] = Compose(
                    hinge, cumulative[static_cast<std::size_t>(parent)]);
                for (Vector3& point : current[static_cast<std::size_t>(child)]) {
                    point = cumulative[static_cast<std::size_t>(child)].Apply(point);
                }
            }
        }

        for (const int face : activeFaces_) {
            const std::array<Vector3, 3>& panel = current[static_cast<std::size_t>(face)];
            const int piece = facePiece_[static_cast<std::size_t>(face)];
            const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
            const Vector2 uvCenter = (
                uv_[static_cast<std::size_t>(vertices[0])]
                + uv_[static_cast<std::size_t>(vertices[1])]
                + uv_[static_cast<std::size_t>(vertices[2])]) * (1.0 / 3.0);
            for (int corner = 0; corner < 3; ++corner) {
                const Vector3 target = spatial_[static_cast<std::size_t>(
                    vertices[static_cast<std::size_t>(corner)])];
                motion.maximumTargetMismatchMillimeters = std::max(
                    motion.maximumTargetMismatchMillimeters,
                    (panel[static_cast<std::size_t>(corner)] - target).Length());
            }
            motion.panels.push_back(panel);
            motion.pieceIndices.push_back(piece);
            motion.panelThicknessMillimeters.push_back(plate_.Thickness(uvCenter.y));
            const double deviation = FaceDeviation(face);
            motion.panelDeviationMillimeters.push_back(deviation);
            motion.maximumPanelDeviationMillimeters = std::max(
                motion.maximumPanelDeviationMillimeters, deviation);
            motion.materialAreaSquareMillimeters += geometry::Cross(
                panel[1] - panel[0], panel[2] - panel[0]).Length() * 0.5;
        }
        return motion;
    }

private:
    void ValidateOptions() const
    {
        if (options_.papercraftFidelity < 1 || options_.papercraftFidelity > 10
            || options_.openingSamples < 8 || options_.openingSamples > 100000
            || !std::isfinite(options_.marginMillimeters)
            || options_.marginMillimeters < 0.0
            || !std::isfinite(options_.minimumFoldAngleDegrees)
            || options_.minimumFoldAngleDegrees < 0.0
            || !std::isfinite(options_.reliefCutDepthRatio)
            || options_.reliefCutDepthRatio <= 0.0
            || options_.reliefCutDepthRatio >= 1.0
            || !std::isfinite(options_.reliefCutSpacingMillimeters)
            || options_.reliefCutSpacingMillimeters <= 0.0
            || !std::isfinite(options_.reliefNotchCurveStrength)
            || options_.reliefNotchCurveStrength < 0.0
            || options_.reliefNotchCurveStrength > 1.0) {
            throw std::invalid_argument("Faceted papercraft options are invalid.");
        }
    }

    void ReadFeaturePaths()
    {
        const int featureSamples = std::max(options_.openingSamples, 192);
        if (options_.includeOpenings) {
            for (const std::string& name : namedPlate_.openingWireNames) {
                openings_.push_back({name, BuildPlateWireUvPath(
                    project_, namedPlate_, name, featureSamples, true)});
            }
        }
        for (const std::string& name : namedPlate_.splitWireNames) {
            splitLines_.push_back({name, BuildPlateWireUvPath(
                project_, namedPlate_, name, featureSamples, false)});
        }
    }

    [[nodiscard]] int VertexIndex(int u, int v) const noexcept
    {
        return v * static_cast<int>(uParameters_.size()) + u;
    }

    void BuildMesh()
    {
        const double target = TargetSpacing(options_.papercraftFidelity);
        const double uLength = SampledLength(plate_, true);
        const double vLength = SampledLength(plate_, false);
        const int uIntervals = std::clamp(
            static_cast<int>(std::ceil(uLength / target)), 1, 64);
        const int vIntervals = std::clamp(
            static_cast<int>(std::ceil(vLength / target)), 1, 64);
        uParameters_ = BuildParameters(
            uIntervals, openings_, true, options_.papercraftFidelity);
        vParameters_ = BuildParameters(
            vIntervals, openings_, false, options_.papercraftFidelity);

        bool splitAlongU = options_.cutDirection != PapercraftCutDirection::Horizontal;
        if (options_.cutDirection == PapercraftCutDirection::Both) {
            const auto normalChange = [&](bool alongU) {
                double total = 0.0;
                Vector3 previous = plate_.SourceSurface().Normal(
                    plate_.SourceU(alongU ? 0.0 : 0.5),
                    plate_.SourceV(alongU ? 0.5 : 0.0));
                for (int sample = 1; sample <= 24; ++sample) {
                    const double parameter = static_cast<double>(sample) / 24.0;
                    const Vector3 current = plate_.SourceSurface().Normal(
                        plate_.SourceU(alongU ? parameter : 0.5),
                        plate_.SourceV(alongU ? 0.5 : parameter));
                    total += NormalAngleDegrees(previous, current);
                    previous = current;
                }
                return total;
            };
            splitAlongU = normalChange(true) >= normalChange(false);
        }
        splitAlongU_ = splitAlongU;

        uv_.reserve(uParameters_.size() * vParameters_.size());
        spatial_.reserve(uParameters_.size() * vParameters_.size());
        for (const double v : vParameters_) {
            for (const double u : uParameters_) {
                uv_.push_back({u, v});
                spatial_.push_back(plate_.Evaluate(u, v, 0.5));
            }
        }
        for (int v = 0; v + 1 < static_cast<int>(vParameters_.size()); ++v) {
            for (int u = 0; u + 1 < static_cast<int>(uParameters_.size()); ++u) {
                const int lowerLeft = VertexIndex(u, v);
                const int lowerRight = VertexIndex(u + 1, v);
                const int upperLeft = VertexIndex(u, v + 1);
                const int upperRight = VertexIndex(u + 1, v + 1);
                const int strip = splitAlongU_ ? u : v;
                const std::array<Facet, 2> cell{{
                    {{{lowerLeft, lowerRight, upperRight}}, strip},
                    {{{lowerLeft, upperRight, upperLeft}}, strip},
                }};
                for (const Facet& face : cell) {
                    const Vector2 center = (
                        uv_[static_cast<std::size_t>(face.vertices[0])]
                        + uv_[static_cast<std::size_t>(face.vertices[1])]
                        + uv_[static_cast<std::size_t>(face.vertices[2])]) * (1.0 / 3.0);
                    const bool inOpening = std::any_of(
                        openings_.begin(), openings_.end(),
                        [&](const UvPath& opening) {
                            return PointInsidePath(center, opening);
                        });
                    const int index = static_cast<int>(faces_.size());
                    faces_.push_back(face);
                    if (!inOpening) {
                        activeFaces_.insert(index);
                    }
                }
            }
        }
        if (activeFaces_.empty()) {
            throw std::invalid_argument(
                "All faceted papercraft panels were removed by openings.");
        }
    }

    void BuildAdjacency()
    {
        for (int face = 0; face < static_cast<int>(faces_.size()); ++face) {
            const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
            for (int edge = 0; edge < 3; ++edge) {
                edgeFaces_[OrderedEdge(
                    vertices[static_cast<std::size_t>(edge)],
                    vertices[static_cast<std::size_t>((edge + 1) % 3)])].push_back(face);
            }
        }
        for (const auto& [edge, adjacent] : edgeFaces_) {
            if (adjacent.size() != 2
                || !activeFaces_.contains(adjacent[0])
                || !activeFaces_.contains(adjacent[1])) {
                continue;
            }
            if (faces_[static_cast<std::size_t>(adjacent[0])].strip
                != faces_[static_cast<std::size_t>(adjacent[1])].strip) {
                cutEdges_.insert(edge);
            }
        }
    }

    void MarkManualCuts()
    {
        for (const UvPath& split : splitLines_) {
            bool touched = false;
            for (const auto& [edge, adjacent] : edgeFaces_) {
                if (adjacent.size() != 2
                    || !activeFaces_.contains(adjacent[0])
                    || !activeFaces_.contains(adjacent[1])) {
                    continue;
                }
                for (std::size_t point = 1; point < split.points.size(); ++point) {
                    if (SegmentsIntersect(
                            uv_[static_cast<std::size_t>(edge.first)],
                            uv_[static_cast<std::size_t>(edge.second)],
                            split.points[point - 1], split.points[point])) {
                        cutEdges_.insert(edge);
                        touched = true;
                        break;
                    }
                }
            }
            if (!touched) {
                throw std::invalid_argument(
                    "A manual split line does not cross the faceted net: " + split.name);
            }
        }
    }

    [[nodiscard]] int CornerForVertex(int face, int vertex) const
    {
        const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
        const auto position = std::find(vertices.begin(), vertices.end(), vertex);
        if (position == vertices.end()) {
            throw std::logic_error("A faceted-net vertex is not part of its face.");
        }
        return static_cast<int>(std::distance(vertices.begin(), position));
    }

    void Develop()
    {
        facePiece_.assign(faces_.size(), -1);
        parentFace_.assign(faces_.size(), -2);
        parentEdge_.assign(faces_.size(), {-1, -1});
        depth_.assign(faces_.size(), 0);
        flat_.assign(faces_.size(), {});
        components_.clear();

        for (const int seed : activeFaces_) {
            if (facePiece_[static_cast<std::size_t>(seed)] >= 0) {
                continue;
            }
            const int piece = static_cast<int>(components_.size());
            components_.emplace_back();
            const auto& initial = faces_[static_cast<std::size_t>(seed)].vertices;
            flat_[static_cast<std::size_t>(seed)][0] = {0.0, 0.0};
            flat_[static_cast<std::size_t>(seed)][1] = {
                (spatial_[static_cast<std::size_t>(initial[1])]
                    - spatial_[static_cast<std::size_t>(initial[0])]).Length(), 0.0};
            flat_[static_cast<std::size_t>(seed)][2] = PlaceTriangleVertex(
                flat_[static_cast<std::size_t>(seed)][0],
                flat_[static_cast<std::size_t>(seed)][1],
                (spatial_[static_cast<std::size_t>(initial[2])]
                    - spatial_[static_cast<std::size_t>(initial[0])]).Length(),
                (spatial_[static_cast<std::size_t>(initial[2])]
                    - spatial_[static_cast<std::size_t>(initial[1])]).Length(),
                {0.0, -1.0});

            std::queue<int> pending;
            pending.push(seed);
            facePiece_[static_cast<std::size_t>(seed)] = piece;
            parentFace_[static_cast<std::size_t>(seed)] = -1;
            while (!pending.empty()) {
                const int current = pending.front();
                pending.pop();
                components_.back().push_back(current);
                const auto& currentVertices
                    = faces_[static_cast<std::size_t>(current)].vertices;
                for (int localEdge = 0; localEdge < 3; ++localEdge) {
                    const int first = currentVertices[static_cast<std::size_t>(localEdge)];
                    const int second = currentVertices[static_cast<std::size_t>((localEdge + 1) % 3)];
                    const int reference = currentVertices[static_cast<std::size_t>((localEdge + 2) % 3)];
                    const Edge edge = OrderedEdge(first, second);
                    if (cutEdges_.contains(edge)) {
                        continue;
                    }
                    const auto& adjacent = edgeFaces_.at(edge);
                    for (const int neighbor : adjacent) {
                        if (neighbor == current || !activeFaces_.contains(neighbor)
                            || facePiece_[static_cast<std::size_t>(neighbor)] >= 0) {
                            continue;
                        }
                        const auto& neighborVertices
                            = faces_[static_cast<std::size_t>(neighbor)].vertices;
                        const int target = *std::find_if(
                            neighborVertices.begin(), neighborVertices.end(),
                            [&](int vertex) {
                                return vertex != first && vertex != second;
                            });
                        std::array<Vector2, 3>& neighborFlat
                            = flat_[static_cast<std::size_t>(neighbor)];
                        neighborFlat[static_cast<std::size_t>(CornerForVertex(neighbor, first))]
                            = flat_[static_cast<std::size_t>(current)]
                                [static_cast<std::size_t>(CornerForVertex(current, first))];
                        neighborFlat[static_cast<std::size_t>(CornerForVertex(neighbor, second))]
                            = flat_[static_cast<std::size_t>(current)]
                                [static_cast<std::size_t>(CornerForVertex(current, second))];
                        neighborFlat[static_cast<std::size_t>(CornerForVertex(neighbor, target))]
                            = PlaceTriangleVertex(
                                neighborFlat[static_cast<std::size_t>(CornerForVertex(neighbor, first))],
                                neighborFlat[static_cast<std::size_t>(CornerForVertex(neighbor, second))],
                                (spatial_[static_cast<std::size_t>(target)]
                                    - spatial_[static_cast<std::size_t>(first)]).Length(),
                                (spatial_[static_cast<std::size_t>(target)]
                                    - spatial_[static_cast<std::size_t>(second)]).Length(),
                                flat_[static_cast<std::size_t>(current)]
                                    [static_cast<std::size_t>(CornerForVertex(current, reference))]);
                        facePiece_[static_cast<std::size_t>(neighbor)] = piece;
                        parentFace_[static_cast<std::size_t>(neighbor)] = current;
                        parentEdge_[static_cast<std::size_t>(neighbor)] = edge;
                        depth_[static_cast<std::size_t>(neighbor)]
                            = depth_[static_cast<std::size_t>(current)] + 1;
                        pending.push(neighbor);
                    }
                }
            }
        }
    }

    [[nodiscard]] bool CutFirstOverlap()
    {
        for (const auto& component : components_) {
            for (std::size_t firstIndex = 0; firstIndex < component.size(); ++firstIndex) {
                const int first = component[firstIndex];
                for (std::size_t secondIndex = firstIndex + 1;
                     secondIndex < component.size(); ++secondIndex) {
                    const int second = component[secondIndex];
                    bool sharesRetainedEdge = false;
                    for (int edge = 0; edge < 3; ++edge) {
                        const auto& vertices = faces_[static_cast<std::size_t>(first)].vertices;
                        const Edge candidate = OrderedEdge(
                            vertices[static_cast<std::size_t>(edge)],
                            vertices[static_cast<std::size_t>((edge + 1) % 3)]);
                        const auto adjacent = edgeFaces_.find(candidate);
                        if (!cutEdges_.contains(candidate)
                            && adjacent != edgeFaces_.end()
                            && std::find(adjacent->second.begin(), adjacent->second.end(), second)
                                != adjacent->second.end()) {
                            sharesRetainedEdge = true;
                            break;
                        }
                    }
                    if (sharesRetainedEdge
                        || !TrianglesOverlap(
                            flat_[static_cast<std::size_t>(first)],
                            flat_[static_cast<std::size_t>(second)])) {
                        continue;
                    }
                    int cutFace = depth_[static_cast<std::size_t>(first)]
                            >= depth_[static_cast<std::size_t>(second)]
                        ? first
                        : second;
                    if (parentFace_[static_cast<std::size_t>(cutFace)] < 0) {
                        cutFace = cutFace == first ? second : first;
                    }
                    if (parentFace_[static_cast<std::size_t>(cutFace)] < 0) {
                        continue;
                    }
                    cutEdges_.insert(parentEdge_[static_cast<std::size_t>(cutFace)]);
                    return true;
                }
            }
        }
        return false;
    }

    void DevelopWithoutOverlaps()
    {
        for (std::size_t attempt = 0; attempt <= activeFaces_.size(); ++attempt) {
            Develop();
            if (!CutFirstOverlap()) {
                AnalyzeGeometry();
                return;
            }
        }
        throw std::runtime_error(
            "Faceted papercraft overlap resolution did not converge.");
    }

    [[nodiscard]] std::vector<BoundarySegment> BoundarySegments(
        const std::vector<int>& component,
        int piece) const
    {
        std::vector<BoundarySegment> result;
        for (const int face : component) {
            const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                const int first = vertices[static_cast<std::size_t>(edgeIndex)];
                const int second = vertices[static_cast<std::size_t>((edgeIndex + 1) % 3)];
                const Edge edge = OrderedEdge(first, second);
                bool retainedNeighbor = false;
                const auto adjacent = edgeFaces_.find(edge);
                if (!cutEdges_.contains(edge) && adjacent != edgeFaces_.end()) {
                    retainedNeighbor = std::any_of(
                        adjacent->second.begin(), adjacent->second.end(),
                        [&](int neighbor) {
                            return neighbor != face && activeFaces_.contains(neighbor)
                                && facePiece_[static_cast<std::size_t>(neighbor)] == piece;
                        });
                }
                if (retainedNeighbor) {
                    continue;
                }
                result.push_back({
                    flat_[static_cast<std::size_t>(face)]
                        [static_cast<std::size_t>(edgeIndex)],
                    flat_[static_cast<std::size_t>(face)]
                        [static_cast<std::size_t>((edgeIndex + 1) % 3)],
                    face,
                    edge,
                });
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<BoundarySegment> OrderBoundary(
        std::vector<BoundarySegment> segments) const
    {
        if (segments.empty()) {
            throw std::logic_error("Faceted papercraft piece has no boundary.");
        }
        std::vector<BoundarySegment> ordered;
        ordered.reserve(segments.size());
        ordered.push_back(segments.front());
        segments.erase(segments.begin());
        while (!segments.empty()) {
            const Vector2 end = ordered.back().second;
            auto next = std::find_if(segments.begin(), segments.end(),
                [&](const BoundarySegment& segment) {
                    return AlmostSame(segment.first, end)
                        || AlmostSame(segment.second, end);
                });
            if (next == segments.end()) {
                throw std::logic_error(
                    "Faceted papercraft boundary could not be ordered.");
            }
            if (AlmostSame(next->second, end)) {
                std::swap(next->first, next->second);
            }
            ordered.push_back(*next);
            segments.erase(next);
            if (AlmostSame(ordered.back().second, ordered.front().first)
                && !segments.empty()) {
                throw std::logic_error(
                    "Faceted papercraft piece contains more than one boundary loop.");
            }
        }
        if (!AlmostSame(ordered.back().second, ordered.front().first)) {
            throw std::logic_error("Faceted papercraft boundary is open.");
        }
        return ordered;
    }

    [[nodiscard]] bool EligibleNotch(const BoundarySegment& segment) const
    {
        if (!options_.allowAutomaticNotches || !cutEdges_.contains(segment.edge)) {
            return false;
        }
        const auto adjacent = edgeFaces_.find(segment.edge);
        if (adjacent == edgeFaces_.end() || adjacent->second.size() != 2
            || !activeFaces_.contains(adjacent->second[0])
            || !activeFaces_.contains(adjacent->second[1])) {
            return false;
        }
        const int other = adjacent->second[0] == segment.face
            ? adjacent->second[1]
            : adjacent->second[0];
        return segment.face < other
            && EdgeFoldAngle(segment.edge) + 1.0e-9
                >= options_.minimumFoldAngleDegrees;
    }

    [[nodiscard]] std::vector<Vector2> NotchPath(
        const BoundarySegment& segment) const
    {
        const Vector2 edge = segment.second - segment.first;
        const double edgeLength = Length(edge);
        const auto& triangle = flat_[static_cast<std::size_t>(segment.face)];
        const Vector2 center = (triangle[0] + triangle[1] + triangle[2]) * (1.0 / 3.0);
        const Vector2 midpoint = (segment.first + segment.second) * 0.5;
        Vector2 inward = center - midpoint;
        const double inwardLength = Length(inward);
        if (inwardLength <= 1.0e-12) {
            return {segment.first, midpoint, segment.second};
        }
        inward = inward * (1.0 / inwardLength);
        const double depth = std::min(
            inwardLength * options_.reliefCutDepthRatio,
            edgeLength * 0.42);
        const double requestedHalfWidth = depth * std::tan(
            options_.reliefNotchAngleDegrees * kPi / 360.0);
        const double halfWidth = std::clamp(
            requestedHalfWidth, edgeLength * 0.12, edgeLength * 0.42);
        const Vector2 along = edge * (1.0 / edgeLength);
        const Vector2 first = midpoint - along * halfWidth;
        const Vector2 second = midpoint + along * halfWidth;
        const Vector2 tip = midpoint + inward * depth;
        if (options_.notchStyle == ReliefNotchStyle::SharpV) {
            return {first, tip, second};
        }
        std::vector<Vector2> result;
        constexpr int armSamples = 5;
        const double curve = options_.reliefNotchCurveStrength;
        const auto appendArm = [&](Vector2 start, Vector2 end, bool skipFirst) {
            const Vector2 control = (start + end) * 0.5
                + inward * (depth * 0.24 * curve);
            for (int sample = skipFirst ? 1 : 0; sample <= armSamples; ++sample) {
                const double t = static_cast<double>(sample) / armSamples;
                result.push_back(start * ((1.0 - t) * (1.0 - t))
                    + control * (2.0 * (1.0 - t) * t)
                    + end * (t * t));
            }
        };
        appendArm(first, tip, false);
        appendArm(tip, second, true);
        return result;
    }

    void BuildPieces()
    {
        pieces_.resize(components_.size());
        pieceOffsets_.resize(components_.size());
        double cursorX = 0.0;
        double cursorY = 0.0;
        double rowHeight = 0.0;
        constexpr double targetRowWidth = 190.0;
        const double notchSpacing = options_.fidelityControlsFeatureSpacing
            ? TargetSpacing(options_.papercraftFidelity)
            : options_.reliefCutSpacingMillimeters;
        for (std::size_t pieceIndex = 0; pieceIndex < components_.size(); ++pieceIndex) {
            const auto boundary = OrderBoundary(BoundarySegments(
                components_[pieceIndex], static_cast<int>(pieceIndex)));
            PlateFlatPatternPiece& piece = pieces_[pieceIndex];
            piece.name = "faceted_piece_" + std::to_string(pieceIndex + 1);
            piece.outerBoundary.name = piece.name + "_cut";
            piece.outerBoundary.points.push_back(boundary.front().first);
            double notchDistance = notchSpacing;
            for (const BoundarySegment& segment : boundary) {
                const double segmentLength = Length(segment.second - segment.first);
                notchDistance += segmentLength;
                if (EligibleNotch(segment)
                    && notchDistance + 1.0e-9 >= notchSpacing) {
                    const std::vector<Vector2> notch = NotchPath(segment);
                    for (const Vector2 point : notch) {
                        if (!AlmostSame(piece.outerBoundary.points.back(), point)) {
                            piece.outerBoundary.points.push_back(point);
                        }
                    }
                    PlateFlatPatternPath relief{
                        "faceted_" + std::string(
                            options_.notchStyle == ReliefNotchStyle::CurvedV
                                ? "curved_v_"
                                : "v_") + std::to_string(++notchCount_),
                        notch,
                        true,
                    };
                    piece.reliefCuts.push_back(std::move(relief));
                    notchDistance = 0.0;
                }
                if (!AlmostSame(piece.outerBoundary.points.back(), segment.second)) {
                    piece.outerBoundary.points.push_back(segment.second);
                }
            }
            ClosePath(piece.outerBoundary.points);

            if (options_.includeFoldLines) {
                std::set<Edge> added;
                for (const int face : components_[pieceIndex]) {
                    const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
                    for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                        const Edge edge = OrderedEdge(
                            vertices[static_cast<std::size_t>(edgeIndex)],
                            vertices[static_cast<std::size_t>((edgeIndex + 1) % 3)]);
                        if (!added.insert(edge).second || cutEdges_.contains(edge)) {
                            continue;
                        }
                        const auto adjacent = edgeFaces_.find(edge);
                        if (adjacent == edgeFaces_.end() || adjacent->second.size() != 2
                            || !activeFaces_.contains(adjacent->second[0])
                            || !activeFaces_.contains(adjacent->second[1])
                            || facePiece_[static_cast<std::size_t>(adjacent->second[0])]
                                != static_cast<int>(pieceIndex)
                            || facePiece_[static_cast<std::size_t>(adjacent->second[1])]
                                != static_cast<int>(pieceIndex)
                            || EdgeFoldAngle(edge) + 1.0e-9
                                < options_.minimumFoldAngleDegrees) {
                            continue;
                        }
                        const int firstCorner = CornerForVertex(face, edge.first);
                        const int secondCorner = CornerForVertex(face, edge.second);
                        piece.foldLines.push_back({
                            "faceted_fold_" + std::to_string(pieceIndex + 1)
                                + "_" + std::to_string(piece.foldLines.size() + 1),
                            {
                                flat_[static_cast<std::size_t>(face)]
                                    [static_cast<std::size_t>(firstCorner)],
                                flat_[static_cast<std::size_t>(face)]
                                    [static_cast<std::size_t>(secondCorner)],
                            },
                        });
                    }
                }
            }

            double minimumX = std::numeric_limits<double>::infinity();
            double minimumY = std::numeric_limits<double>::infinity();
            double maximumX = -std::numeric_limits<double>::infinity();
            double maximumY = -std::numeric_limits<double>::infinity();
            for (const Vector2 point : piece.outerBoundary.points) {
                minimumX = std::min(minimumX, point.x);
                minimumY = std::min(minimumY, point.y);
                maximumX = std::max(maximumX, point.x);
                maximumY = std::max(maximumY, point.y);
            }
            const double width = maximumX - minimumX;
            const double height = maximumY - minimumY;
            if (cursorX > 0.0
                && cursorX + width > targetRowWidth) {
                cursorX = 0.0;
                cursorY += rowHeight + options_.marginMillimeters;
                rowHeight = 0.0;
            }
            const Vector2 offset{cursorX - minimumX, cursorY - minimumY};
            pieceOffsets_[pieceIndex] = offset;
            const auto translate = [&](PlateFlatPatternPath& path) {
                for (Vector2& point : path.points) {
                    point = point + offset;
                }
            };
            translate(piece.outerBoundary);
            for (auto& fold : piece.foldLines) {
                translate(fold);
            }
            for (auto& relief : piece.reliefCuts) {
                translate(relief);
            }
            cursorX += width + options_.marginMillimeters;
            rowHeight = std::max(rowHeight, height);
        }
    }

    [[nodiscard]] double EdgeFoldAngle(Edge edge) const
    {
        const auto adjacent = edgeFaces_.find(edge);
        if (adjacent == edgeFaces_.end() || adjacent->second.size() != 2) {
            return 0.0;
        }
        const auto triangle = [&](int face) {
            std::array<Vector3, 3> result;
            for (int corner = 0; corner < 3; ++corner) {
                result[static_cast<std::size_t>(corner)] = spatial_[static_cast<std::size_t>(
                    faces_[static_cast<std::size_t>(face)].vertices[static_cast<std::size_t>(corner)])];
            }
            return result;
        };
        return NormalAngleDegrees(
            TriangleNormal(triangle(adjacent->second[0])),
            TriangleNormal(triangle(adjacent->second[1])));
    }

    [[nodiscard]] double FaceDeviation(int face) const
    {
        const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
        std::array<Vector3, 3> points;
        std::array<Vector2, 3> parameters;
        for (int corner = 0; corner < 3; ++corner) {
            points[static_cast<std::size_t>(corner)]
                = spatial_[static_cast<std::size_t>(vertices[static_cast<std::size_t>(corner)])];
            parameters[static_cast<std::size_t>(corner)]
                = uv_[static_cast<std::size_t>(vertices[static_cast<std::size_t>(corner)])];
        }
        const Vector3 normal = TriangleNormal(points);
        const std::array<std::array<double, 3>, 4> samples{{
            {{0.5, 0.5, 0.0}},
            {{0.0, 0.5, 0.5}},
            {{0.5, 0.0, 0.5}},
            {{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}},
        }};
        double maximum = 0.0;
        for (const auto& weights : samples) {
            const Vector2 uv = parameters[0] * weights[0]
                + parameters[1] * weights[1]
                + parameters[2] * weights[2];
            maximum = std::max(maximum, std::abs(geometry::Dot(
                plate_.Evaluate(uv.x, uv.y, 0.5) - points[0], normal)));
        }
        return maximum;
    }

    void AnalyzeGeometry()
    {
        double squaredError = 0.0;
        std::size_t measuredEdges = 0;
        for (const int face : activeFaces_) {
            const auto& vertices = faces_[static_cast<std::size_t>(face)].vertices;
            for (int edge = 0; edge < 3; ++edge) {
                const int first = vertices[static_cast<std::size_t>(edge)];
                const int second = vertices[static_cast<std::size_t>((edge + 1) % 3)];
                const double spatialLength = (
                    spatial_[static_cast<std::size_t>(second)]
                    - spatial_[static_cast<std::size_t>(first)]).Length();
                const double flatLength = Length(
                    flat_[static_cast<std::size_t>(face)]
                        [static_cast<std::size_t>((edge + 1) % 3)]
                    - flat_[static_cast<std::size_t>(face)]
                        [static_cast<std::size_t>(edge)]);
                const double error = std::abs(spatialLength - flatLength);
                maximumEdgeError_ = std::max(maximumEdgeError_, error);
                squaredError += error * error;
                ++measuredEdges;
            }
            maximumDeviation_ = std::max(maximumDeviation_, FaceDeviation(face));
        }
        if (measuredEdges > 0) {
            rmsEdgeError_ = std::sqrt(
                squaredError / static_cast<double>(measuredEdges));
        }
    }

    const Project& project_;
    const NamedPlate& namedPlate_;
    const Plate& plate_;
    PlateFlatPatternOptions options_;
    bool splitAlongU_ = true;
    std::vector<UvPath> openings_;
    std::vector<UvPath> splitLines_;
    std::vector<double> uParameters_;
    std::vector<double> vParameters_;
    std::vector<Vector2> uv_;
    std::vector<Vector3> spatial_;
    std::vector<Facet> faces_;
    std::set<int> activeFaces_;
    std::map<Edge, std::vector<int>> edgeFaces_;
    std::set<Edge> cutEdges_;
    std::vector<int> facePiece_;
    std::vector<int> parentFace_;
    std::vector<Edge> parentEdge_;
    std::vector<int> depth_;
    std::vector<std::array<Vector2, 3>> flat_;
    std::vector<std::vector<int>> components_;
    std::vector<PlateFlatPatternPiece> pieces_;
    std::vector<Vector2> pieceOffsets_;
    double maximumEdgeError_ = 0.0;
    double rmsEdgeError_ = 0.0;
    double maximumDeviation_ = 0.0;
    int notchCount_ = 0;
};

} // namespace

PlateFlatPattern BuildFacetedPapercraftPattern(
    const Project& project,
    const NamedPlate& plate,
    PlateFlatPatternOptions options)
{
    if (plate.plate.AnalyzeDevelopability().classification
        == PlateDevelopability::Planar) {
        options.includeAutomaticReliefCuts = false;
        return BuildPlateFlatPattern(project, plate, options);
    }
    return FacetedGenerator(project, plate, std::move(options)).Pattern();
}

PlateAssemblyGuide BuildFacetedPapercraftGuide(
    const Project& project,
    const NamedPlate& plate,
    PlateFlatPatternOptions options)
{
    if (plate.plate.AnalyzeDevelopability().classification
        == PlateDevelopability::Planar) {
        return BuildPlateAssemblyGuide(project, plate, std::move(options));
    }
    return FacetedGenerator(project, plate, std::move(options)).Guide();
}

PlateAssemblyMotion BuildFacetedPapercraftMotion(
    const Project& project,
    const NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options)
{
    if (plate.plate.AnalyzeDevelopability().classification
        == PlateDevelopability::Planar) {
        options.includeAutomaticReliefCuts = false;
        return BuildPlateAssemblyMotion(
            project, plate, progress, std::move(options));
    }
    return FacetedGenerator(project, plate, std::move(options)).Motion(progress);
}

FacetedPapercraftPreview BuildFacetedPapercraftPreview(
    const Project& project,
    const NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options)
{
    if (plate.plate.AnalyzeDevelopability().classification
        == PlateDevelopability::Planar) {
        options.includeAutomaticReliefCuts = false;
        return {
            BuildPlateAssemblyGuide(project, plate, options),
            BuildPlateAssemblyMotion(project, plate, progress, std::move(options)),
        };
    }
    const FacetedGenerator generator(project, plate, std::move(options));
    return {generator.Guide(), generator.Motion(progress)};
}

} // namespace kachakacha::io
