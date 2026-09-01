#include "kachakacha/io/PlateFlatPattern.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <ostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <string_view>

namespace kachakacha::io {

using geometry::Vector2;
using geometry::Vector3;
using model::NamedPlate;
using model::NamedWire;
using model::Plate;
using model::PlateDevelopability;
using model::Project;
using model::Surface;
using model::SurfaceKind;
using model::Wire;
using model::WireKind;

namespace {

struct Triangle {
    std::array<int, 3> vertices;
};

double Length(Vector2 value)
{
    return std::hypot(value.x, value.y);
}

double Cross2(Vector2 first, Vector2 second)
{
    return first.x * second.y - first.y * second.x;
}

bool AlmostSame(Vector2 first, Vector2 second, double tolerance = 1.0e-9)
{
    return Length(first - second) <= tolerance;
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

void ValidateOptions(const PlateFlatPatternOptions& options)
{
    if (options.uSegments < 2 || options.uSegments > 2000
        || options.vSegments < 2 || options.vSegments > 2000
        || options.openingSamples < 8 || options.openingSamples > 100000
        || !std::isfinite(options.marginMillimeters) || options.marginMillimeters < 0.0
        || !std::isfinite(options.foldSpacingMillimeters) || options.foldSpacingMillimeters <= 0.0
        || !std::isfinite(options.minimumFoldAngleDegrees)
        || options.minimumFoldAngleDegrees < 0.0 || options.minimumFoldAngleDegrees > 180.0
        || !std::isfinite(options.reliefCutDepthRatio)
        || options.reliefCutDepthRatio <= 0.0 || options.reliefCutDepthRatio >= 1.0
        || !std::isfinite(options.reliefCutSpacingMillimeters)
        || options.reliefCutSpacingMillimeters <= 0.0
        || !std::isfinite(options.reliefNotchAngleDegrees)
        || options.reliefNotchAngleDegrees < 1.0 || options.reliefNotchAngleDegrees > 120.0
        || !std::isfinite(options.reliefNotchCurveStrength)
        || options.reliefNotchCurveStrength < 0.0
        || options.reliefNotchCurveStrength > 1.0
        || options.papercraftFidelity < 1 || options.papercraftFidelity > 10) {
        throw std::invalid_argument("Plate flat-pattern options are invalid.");
    }
}

std::vector<Vector3> SampleWire(const Wire& wire, int requestedSamples)
{
    if (wire.Kind() == WireKind::Line || wire.Kind() == WireKind::Polyline) {
        return wire.ControlPoints();
    }
    const int samples = wire.Kind() == WireKind::Circle
        ? std::max(requestedSamples, 48)
        : requestedSamples;
    std::vector<Vector3> points;
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int sample = 0; sample <= samples; ++sample) {
        points.push_back(wire.Evaluate(static_cast<double>(sample) / samples));
    }
    return points;
}

void ClosePath(std::vector<Vector2>& points)
{
    if (points.size() < 3) {
        throw std::invalid_argument("A flat-pattern path must contain at least three points.");
    }
    if (!AlmostSame(points.front(), points.back())) {
        points.push_back(points.front());
    }
}

void NormalizeClosedPath(PlateFlatPatternPath& path)
{
    std::vector<Vector2> normalized;
    normalized.reserve(path.points.size());
    for (const Vector2 point : path.points) {
        if (normalized.empty() || !AlmostSame(normalized.back(), point, 1.0e-8)) {
            normalized.push_back(point);
        }
    }
    if (normalized.size() > 1 && AlmostSame(normalized.front(), normalized.back(), 1.0e-8)) {
        normalized.pop_back();
    }
    if (normalized.size() >= 3) {
        normalized.push_back(normalized.front());
    }
    path.points = std::move(normalized);
}

void NormalizeOpenPath(PlateFlatPatternPath& path)
{
    std::vector<Vector2> normalized;
    normalized.reserve(path.points.size());
    for (const Vector2 point : path.points) {
        if (normalized.empty() || !AlmostSame(normalized.back(), point, 1.0e-8)) {
            normalized.push_back(point);
        }
    }
    path.points = std::move(normalized);
}

template <typename Inside, typename Intersect>
std::vector<Vector2> ClipPolygon(
    const std::vector<Vector2>& input,
    Inside inside,
    Intersect intersect)
{
    std::vector<Vector2> output;
    if (input.empty()) {
        return output;
    }
    Vector2 previous = input.back();
    bool previousInside = inside(previous);
    for (const Vector2 current : input) {
        const bool currentInside = inside(current);
        if (currentInside != previousInside) {
            output.push_back(intersect(previous, current));
        }
        if (currentInside) {
            output.push_back(current);
        }
        previous = current;
        previousInside = currentInside;
    }
    return output;
}

std::vector<Vector2> ClipToRectangle(
    std::vector<Vector2> polygon,
    double minimumX,
    double maximumX,
    double minimumY,
    double maximumY)
{
    constexpr double epsilon = 1.0e-12;
    const auto verticalIntersection = [](double x, Vector2 first, Vector2 second) {
        const double parameter = (x - first.x) / (second.x - first.x);
        return Vector2{x, first.y + (second.y - first.y) * parameter};
    };
    const auto horizontalIntersection = [](double y, Vector2 first, Vector2 second) {
        const double parameter = (y - first.y) / (second.y - first.y);
        return Vector2{first.x + (second.x - first.x) * parameter, y};
    };
    polygon = ClipPolygon(polygon,
        [&](Vector2 point) { return point.x >= minimumX - epsilon; },
        [&](Vector2 first, Vector2 second) { return verticalIntersection(minimumX, first, second); });
    polygon = ClipPolygon(polygon,
        [&](Vector2 point) { return point.x <= maximumX + epsilon; },
        [&](Vector2 first, Vector2 second) { return verticalIntersection(maximumX, first, second); });
    polygon = ClipPolygon(polygon,
        [&](Vector2 point) { return point.y >= minimumY - epsilon; },
        [&](Vector2 first, Vector2 second) { return horizontalIntersection(minimumY, first, second); });
    return ClipPolygon(polygon,
        [&](Vector2 point) { return point.y <= maximumY + epsilon; },
        [&](Vector2 first, Vector2 second) { return horizontalIntersection(maximumY, first, second); });
}

const NamedWire& RequireNamedWire(const Project& project, std::string_view name)
{
    const auto position = std::find_if(project.Wires().begin(), project.Wires().end(),
        [&](const NamedWire& wire) { return wire.name == name; });
    if (position == project.Wires().end()) {
        throw std::invalid_argument("Flat-pattern source wire was not found: " + std::string(name));
    }
    return *position;
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
        throw std::invalid_argument("Plate development contains a collapsed edge.");
    }
    const Vector2 direction = edge * (1.0 / edgeLength);
    const Vector2 perpendicular{-direction.y, direction.x};
    const double along = (firstRadius * firstRadius - secondRadius * secondRadius
        + edgeLength * edgeLength) / (2.0 * edgeLength);
    double heightSquared = firstRadius * firstRadius - along * along;
    const double numericalTolerance = std::max(1.0, firstRadius * firstRadius) * 1.0e-10;
    if (heightSquared < -numericalTolerance) {
        throw std::invalid_argument("Plate development accumulated too much local distortion.");
    }
    heightSquared = std::max(0.0, heightSquared);
    const Vector2 base = first + direction * along;
    const Vector2 positive = base + perpendicular * std::sqrt(heightSquared);
    const Vector2 negative = base - perpendicular * std::sqrt(heightSquared);
    const double referenceSide = Cross2(edge, reference - first);
    if (std::abs(referenceSide) <= 1.0e-14) {
        return positive;
    }
    return Cross2(edge, positive - first) * referenceSide <= 0.0 ? positive : negative;
}

class DevelopmentGrid {
public:
    DevelopmentGrid(const Plate& plate, int uSegments, int vSegments)
        : plate_(plate), uSegments_(uSegments), vSegments_(vSegments)
    {
        const int uCount = uSegments_ + 1;
        const int vCount = vSegments_ + 1;
        spatial_.reserve(static_cast<std::size_t>(uCount * vCount));
        for (int vIndex = 0; vIndex < vCount; ++vIndex) {
            const double v = static_cast<double>(vIndex) / vSegments_;
            for (int uIndex = 0; uIndex < uCount; ++uIndex) {
                const double u = static_cast<double>(uIndex) / uSegments_;
                spatial_.push_back(plate.Evaluate(u, v, 0.5));
            }
        }

        triangles_.reserve(static_cast<std::size_t>(uSegments_ * vSegments_ * 2));
        for (int vIndex = 0; vIndex < vSegments_; ++vIndex) {
            for (int uIndex = 0; uIndex < uSegments_; ++uIndex) {
                const int lowerLeft = Index(uIndex, vIndex);
                const int lowerRight = Index(uIndex + 1, vIndex);
                const int upperLeft = Index(uIndex, vIndex + 1);
                const int upperRight = Index(uIndex + 1, vIndex + 1);
                triangles_.push_back({{lowerLeft, lowerRight, upperRight}});
                triangles_.push_back({{lowerLeft, upperRight, upperLeft}});
            }
        }
        Develop();
        Analyze();
    }

    [[nodiscard]] Vector2 Evaluate(double u, double v) const
    {
        const double scaledU = std::clamp(u, 0.0, 1.0) * uSegments_;
        const double scaledV = std::clamp(v, 0.0, 1.0) * vSegments_;
        const int uIndex = std::min(static_cast<int>(scaledU), uSegments_ - 1);
        const int vIndex = std::min(static_cast<int>(scaledV), vSegments_ - 1);
        const double localU = scaledU - uIndex;
        const double localV = scaledV - vIndex;
        const Vector2 lowerLeft = flat_[Index(uIndex, vIndex)];
        const Vector2 lowerRight = flat_[Index(uIndex + 1, vIndex)];
        const Vector2 upperLeft = flat_[Index(uIndex, vIndex + 1)];
        const Vector2 upperRight = flat_[Index(uIndex + 1, vIndex + 1)];
        if (localV <= localU) {
            return lowerLeft * (1.0 - localU)
                + lowerRight * (localU - localV)
                + upperRight * localV;
        }
        return lowerLeft * (1.0 - localV)
            + upperLeft * (localV - localU)
            + upperRight * localU;
    }

    [[nodiscard]] std::vector<Vector2> Boundary() const
    {
        std::vector<Vector2> boundary;
        boundary.reserve(static_cast<std::size_t>((uSegments_ + vSegments_) * 2 + 1));
        for (int uIndex = 0; uIndex <= uSegments_; ++uIndex) {
            boundary.push_back(flat_[Index(uIndex, 0)]);
        }
        for (int vIndex = 1; vIndex <= vSegments_; ++vIndex) {
            boundary.push_back(flat_[Index(uSegments_, vIndex)]);
        }
        for (int uIndex = uSegments_ - 1; uIndex >= 0; --uIndex) {
            boundary.push_back(flat_[Index(uIndex, vSegments_)]);
        }
        for (int vIndex = vSegments_ - 1; vIndex >= 0; --vIndex) {
            boundary.push_back(flat_[Index(0, vIndex)]);
        }
        ClosePath(boundary);
        return boundary;
    }

    [[nodiscard]] std::vector<Vector2> ConstantU(
        double u,
        double firstV = 0.0,
        double lastV = 1.0) const
    {
        const int samples = std::max(2, static_cast<int>(
            std::ceil(std::abs(lastV - firstV) * static_cast<double>(vSegments_))));
        std::vector<Vector2> points;
        points.reserve(static_cast<std::size_t>(samples) + 1);
        for (int sample = 0; sample <= samples; ++sample) {
            const double fraction = static_cast<double>(sample) / samples;
            points.push_back(Evaluate(u, firstV + (lastV - firstV) * fraction));
        }
        return points;
    }

    [[nodiscard]] std::vector<Vector2> ConstantV(
        double v,
        double firstU = 0.0,
        double lastU = 1.0) const
    {
        const int samples = std::max(2, static_cast<int>(
            std::ceil(std::abs(lastU - firstU) * static_cast<double>(uSegments_))));
        std::vector<Vector2> points;
        points.reserve(static_cast<std::size_t>(samples) + 1);
        for (int sample = 0; sample <= samples; ++sample) {
            const double fraction = static_cast<double>(sample) / samples;
            points.push_back(Evaluate(firstU + (lastU - firstU) * fraction, v));
        }
        return points;
    }

    [[nodiscard]] double MaximumEdgeDistortion() const noexcept { return maximumEdgeDistortion_; }
    [[nodiscard]] double RootMeanSquareEdgeDistortion() const noexcept { return rmsEdgeDistortion_; }
    [[nodiscard]] double MaximumBoundaryApproximation() const noexcept { return maximumBoundaryApproximation_; }

private:
    [[nodiscard]] int Index(int uIndex, int vIndex) const noexcept
    {
        return vIndex * (uSegments_ + 1) + uIndex;
    }

    void Develop()
    {
        flat_.resize(spatial_.size());
        std::vector<bool> placed(spatial_.size(), false);
        std::vector<bool> processed(triangles_.size(), false);
        std::map<std::pair<int, int>, std::vector<int>> edgeTriangles;
        for (int triangleIndex = 0; triangleIndex < static_cast<int>(triangles_.size()); ++triangleIndex) {
            const auto& vertices = triangles_[triangleIndex].vertices;
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                const int first = vertices[edgeIndex];
                const int second = vertices[(edgeIndex + 1) % 3];
                edgeTriangles[std::minmax(first, second)].push_back(triangleIndex);
            }
        }

        int firstTriangle = -1;
        for (int index = 0; index < static_cast<int>(triangles_.size()); ++index) {
            const auto& vertices = triangles_[index].vertices;
            const Vector3 firstEdge = spatial_[vertices[1]] - spatial_[vertices[0]];
            const Vector3 secondEdge = spatial_[vertices[2]] - spatial_[vertices[0]];
            if (geometry::Cross(firstEdge, secondEdge).LengthSquared() > 1.0e-18) {
                firstTriangle = index;
                break;
            }
        }
        if (firstTriangle < 0) {
            throw std::invalid_argument("Plate surface has no developable area.");
        }

        const auto& initial = triangles_[firstTriangle].vertices;
        const double firstLength = (spatial_[initial[1]] - spatial_[initial[0]]).Length();
        flat_[initial[0]] = {0.0, 0.0};
        flat_[initial[1]] = {firstLength, 0.0};
        flat_[initial[2]] = PlaceTriangleVertex(
            flat_[initial[0]],
            flat_[initial[1]],
            (spatial_[initial[2]] - spatial_[initial[0]]).Length(),
            (spatial_[initial[2]] - spatial_[initial[1]]).Length(),
            {0.0, -1.0});
        placed[initial[0]] = true;
        placed[initial[1]] = true;
        placed[initial[2]] = true;

        std::queue<int> pending;
        pending.push(firstTriangle);
        processed[firstTriangle] = true;
        while (!pending.empty()) {
            const int currentIndex = pending.front();
            pending.pop();
            const auto& current = triangles_[currentIndex].vertices;
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                const int sharedFirst = current[edgeIndex];
                const int sharedSecond = current[(edgeIndex + 1) % 3];
                const int reference = current[(edgeIndex + 2) % 3];
                for (const int neighborIndex : edgeTriangles[std::minmax(sharedFirst, sharedSecond)]) {
                    if (processed[neighborIndex]) {
                        continue;
                    }
                    const auto& neighbor = triangles_[neighborIndex].vertices;
                    const auto targetPosition = std::find_if(neighbor.begin(), neighbor.end(), [&](int vertex) {
                        return vertex != sharedFirst && vertex != sharedSecond;
                    });
                    if (targetPosition == neighbor.end()) {
                        throw std::logic_error("Plate development triangle adjacency is invalid.");
                    }
                    const int target = *targetPosition;
                    if (!placed[target]) {
                        flat_[target] = PlaceTriangleVertex(
                            flat_[sharedFirst],
                            flat_[sharedSecond],
                            (spatial_[target] - spatial_[sharedFirst]).Length(),
                            (spatial_[target] - spatial_[sharedSecond]).Length(),
                            flat_[reference]);
                        placed[target] = true;
                    }
                    processed[neighborIndex] = true;
                    pending.push(neighborIndex);
                }
            }
        }

        if (std::find(placed.begin(), placed.end(), false) != placed.end()) {
            throw std::invalid_argument("Plate surface contains disconnected or collapsed regions.");
        }
    }

    void Analyze()
    {
        std::set<std::pair<int, int>> edges;
        double squaredError = 0.0;
        for (const Triangle& triangle : triangles_) {
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                const auto edge = std::minmax(
                    triangle.vertices[edgeIndex],
                    triangle.vertices[(edgeIndex + 1) % 3]);
                if (!edges.insert(edge).second) {
                    continue;
                }
                const double spatialLength = (spatial_[edge.second] - spatial_[edge.first]).Length();
                const double flatLength = Length(flat_[edge.second] - flat_[edge.first]);
                const double error = std::abs(flatLength - spatialLength);
                maximumEdgeDistortion_ = std::max(maximumEdgeDistortion_, error);
                squaredError += error * error;
            }
        }
        rmsEdgeDistortion_ = std::sqrt(squaredError / static_cast<double>(edges.size()));

        const auto estimateSide = [&](bool alongU, double fixedParameter) {
            const int segments = alongU ? uSegments_ : vSegments_;
            double coarseLength = 0.0;
            double refinedLength = 0.0;
            for (int segment = 0; segment < segments; ++segment) {
                const double firstParameter = static_cast<double>(segment) / segments;
                const double secondParameter = static_cast<double>(segment + 1) / segments;
                const double middleParameter = (firstParameter + secondParameter) * 0.5;
                const auto point = [&](double parameter) {
                    return alongU
                        ? plate_.Evaluate(parameter, fixedParameter, 0.5)
                        : plate_.Evaluate(fixedParameter, parameter, 0.5);
                };
                const Vector3 first = point(firstParameter);
                const Vector3 middle = point(middleParameter);
                const Vector3 second = point(secondParameter);
                coarseLength += (second - first).Length();
                refinedLength += (middle - first).Length() + (second - middle).Length();
            }
            return std::abs(refinedLength - coarseLength);
        };
        maximumBoundaryApproximation_ = std::max({
            estimateSide(true, 0.0),
            estimateSide(true, 1.0),
            estimateSide(false, 0.0),
            estimateSide(false, 1.0),
        });
    }

    const Plate& plate_;
    int uSegments_ = 0;
    int vSegments_ = 0;
    std::vector<Vector3> spatial_;
    std::vector<Vector2> flat_;
    std::vector<Triangle> triangles_;
    double maximumEdgeDistortion_ = 0.0;
    double rmsEdgeDistortion_ = 0.0;
    double maximumBoundaryApproximation_ = 0.0;
};

std::vector<Vector2> BuildPlanarBoundary(const NamedPlate& namedPlate, int samples)
{
    const Plate& plate = namedPlate.plate;
    const Surface& surface = plate.SourceSurface();
    const auto& plane = *surface.PlanarWorkPlane();
    std::vector<Vector2> polygon;
    for (const Vector3 point : SampleWire(surface.FirstBoundary(), samples)) {
        const auto projected = plane.Project(point);
        polygon.push_back({projected.u, projected.v});
    }
    if (polygon.size() > 1 && AlmostSame(polygon.front(), polygon.back())) {
        polygon.pop_back();
    }

    if (!plate.Range().IsFull()) {
        const auto minimum = plane.Project(surface.Evaluate(plate.Range().minimumU, plate.Range().minimumV));
        const auto maximum = plane.Project(surface.Evaluate(plate.Range().maximumU, plate.Range().maximumV));
        polygon = ClipToRectangle(
            std::move(polygon),
            std::min(minimum.u, maximum.u),
            std::max(minimum.u, maximum.u),
            std::min(minimum.v, maximum.v),
            std::max(minimum.v, maximum.v));
    }
    ClosePath(polygon);
    return polygon;
}

std::vector<Vector2> BuildPlanarWirePath(
    const Project& project,
    std::string_view name,
    const Surface& surface,
    int samples,
    bool closePath)
{
    const NamedWire& opening = RequireNamedWire(project, name);
    std::vector<Vector2> points;
    for (const Vector3 point : SampleWire(opening.wire, samples)) {
        const auto projected = surface.PlanarWorkPlane()->Project(point);
        if (std::abs(projected.w) > 1.0e-5) {
            throw std::invalid_argument("Plate wire is not on its planar source surface: " + opening.name);
        }
        points.push_back({projected.u, projected.v});
    }
    if (closePath) {
        ClosePath(points);
    }
    return points;
}

std::vector<Vector2> BuildPlateWireUvPath(
    const Project& project,
    const NamedPlate& namedPlate,
    std::string_view openingName,
    int samples,
    bool closePath)
{
    const NamedWire& opening = RequireNamedWire(project, openingName);
    if (!opening.projection.has_value()) {
        throw std::invalid_argument("A curved-plate wire must come from a projected drawing: " + opening.name);
    }
    if (opening.projection->targetSurfaceName != namedPlate.sourceSurfaceName) {
        throw std::invalid_argument("Plate wire belongs to a different source surface: " + opening.name);
    }
    const NamedWire& source = RequireNamedWire(project, opening.projection->sourceWireName);
    const Surface& surface = namedPlate.plate.SourceSurface();
    const auto& range = namedPlate.plate.Range();
    const int sampleCount = source.wire.Kind() == WireKind::Polyline
        ? std::max(samples, static_cast<int>(source.wire.ControlPoints().size()) * 4)
        : samples;
    std::vector<Vector2> points;
    points.reserve(static_cast<std::size_t>(sampleCount) + 1);
    for (int sample = 0; sample <= sampleCount; ++sample) {
        const double parameter = static_cast<double>(sample) / sampleCount;
        const auto projected = surface.ProjectPointAlongDirection(
            source.wire.Evaluate(parameter),
            opening.projection->direction);
        const double localU = (projected.u - range.minimumU) / (range.maximumU - range.minimumU);
        const double localV = (projected.v - range.minimumV) / (range.maximumV - range.minimumV);
        constexpr double rangeTolerance = 1.0e-5;
        if (localU < -rangeTolerance || localU > 1.0 + rangeTolerance
            || localV < -rangeTolerance || localV > 1.0 + rangeTolerance) {
            throw std::invalid_argument("Plate wire lies outside the selected plate piece: " + opening.name);
        }
        const Vector2 uvPoint{std::clamp(localU, 0.0, 1.0), std::clamp(localV, 0.0, 1.0)};
        if (points.empty() || !AlmostSame(points.back(), uvPoint, 1.0e-7)) {
            points.push_back(uvPoint);
        }
    }
    if (closePath) {
        ClosePath(points);
    }
    return points;
}

std::vector<Vector2> BuildDevelopedWirePath(
    const Project& project,
    const NamedPlate& namedPlate,
    std::string_view openingName,
    const DevelopmentGrid& grid,
    int samples,
    bool closePath)
{
    std::vector<Vector2> points = BuildPlateWireUvPath(
        project, namedPlate, openingName, samples, closePath);
    for (Vector2& point : points) {
        point = grid.Evaluate(point.x, point.y);
    }
    return points;
}

std::vector<Vector3> BuildAssemblyWirePath(
    const Project& project,
    const NamedPlate& namedPlate,
    std::string_view wireName,
    int samples)
{
    const NamedWire& projectedWire = RequireNamedWire(project, wireName);
    if (!projectedWire.projection.has_value()) {
        throw std::invalid_argument("A plate assembly guide must come from a projected drawing: "
            + projectedWire.name);
    }
    if (projectedWire.projection->targetSurfaceName != namedPlate.sourceSurfaceName) {
        throw std::invalid_argument("Plate assembly guide belongs to a different source surface: "
            + projectedWire.name);
    }
    const NamedWire& source = RequireNamedWire(
        project, projectedWire.projection->sourceWireName);
    const Plate& plate = namedPlate.plate;
    const Surface& surface = plate.SourceSurface();
    const auto& range = plate.Range();
    const int sampleCount = source.wire.Kind() == WireKind::Polyline
        ? std::max(samples, static_cast<int>(source.wire.ControlPoints().size()) * 4)
        : samples;
    std::vector<Vector3> points;
    points.reserve(static_cast<std::size_t>(sampleCount) + 1);
    for (int sample = 0; sample <= sampleCount; ++sample) {
        const double parameter = static_cast<double>(sample) / sampleCount;
        const auto projected = surface.ProjectPointAlongDirection(
            source.wire.Evaluate(parameter), projectedWire.projection->direction);
        const double localU = (projected.u - range.minimumU)
            / (range.maximumU - range.minimumU);
        const double localV = (projected.v - range.minimumV)
            / (range.maximumV - range.minimumV);
        constexpr double rangeTolerance = 1.0e-5;
        if (localU < -rangeTolerance || localU > 1.0 + rangeTolerance
            || localV < -rangeTolerance || localV > 1.0 + rangeTolerance) {
            throw std::invalid_argument("Plate assembly guide lies outside the selected plate piece: "
                + projectedWire.name);
        }
        const Vector3 point = plate.Evaluate(localU, localV, 1.0);
        if (points.empty() || (points.back() - point).LengthSquared() > 1.0e-14) {
            points.push_back(point);
        }
    }
    if (points.size() < 2) {
        throw std::invalid_argument("Plate assembly guide collapsed to a point: "
            + projectedWire.name);
    }
    return points;
}

std::vector<Vector3> BuildConstantAssemblyPath(
    const Plate& plate,
    bool constantU,
    double constantParameter,
    double first,
    double last,
    int samples)
{
    samples = std::max(samples, 2);
    std::vector<Vector3> points;
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int sample = 0; sample <= samples; ++sample) {
        const double fraction = static_cast<double>(sample) / samples;
        const double varyingParameter = first + (last - first) * fraction;
        points.push_back(constantU
            ? plate.Evaluate(constantParameter, varyingParameter, 1.0)
            : plate.Evaluate(varyingParameter, constantParameter, 1.0));
    }
    return points;
}

double SpatialPathLength(const Plate& plate, bool alongU, double fixedParameter)
{
    constexpr int samples = 96;
    double length = 0.0;
    Vector3 previous = alongU
        ? plate.Evaluate(0.0, fixedParameter, 0.5)
        : plate.Evaluate(fixedParameter, 0.0, 0.5);
    for (int sample = 1; sample <= samples; ++sample) {
        const double parameter = static_cast<double>(sample) / samples;
        const Vector3 point = alongU
            ? plate.Evaluate(parameter, fixedParameter, 0.5)
            : plate.Evaluate(fixedParameter, parameter, 0.5);
        length += (point - previous).Length();
        previous = point;
    }
    return length;
}

Vector3 PlateNormal(const Plate& plate, double u, double v)
{
    const auto& range = plate.Range();
    return plate.SourceSurface().Normal(
        range.minimumU + u * (range.maximumU - range.minimumU),
        range.minimumV + v * (range.maximumV - range.minimumV));
}

double NormalAngleDegrees(Vector3 first, Vector3 second)
{
    constexpr double radiansToDegrees = 57.2957795130823208768;
    const double cosine = std::clamp(geometry::Dot(first.Normalized(), second.Normalized()), -1.0, 1.0);
    return std::acos(cosine) * radiansToDegrees;
}

double FidelityTargetSpacing(int fidelity)
{
    const double fraction = static_cast<double>(fidelity - 1) / 9.0;
    return 20.0 + (2.0 - 20.0) * fraction;
}

double EffectiveFeatureSpacing(
    const PlateFlatPatternOptions& options,
    double individuallyConfiguredSpacing)
{
    return options.fidelityControlsFeatureSpacing
        ? FidelityTargetSpacing(options.papercraftFidelity)
        : individuallyConfiguredSpacing;
}

std::vector<double> FoldParameters(
    const Plate& plate,
    bool alongU,
    const PlateFlatPatternOptions& options)
{
    const double length = SpatialPathLength(plate, alongU, 0.5);
    const double spacing = EffectiveFeatureSpacing(options, options.foldSpacingMillimeters);
    const int intervalCount = std::clamp(
        static_cast<int>(std::ceil(length / spacing)), 2, 200);
    std::vector<double> parameters;
    for (int interval = 1; interval < intervalCount; ++interval) {
        const double parameter = static_cast<double>(interval) / intervalCount;
        const double delta = std::min(0.02, 0.45 / intervalCount);
        double maximumAngle = 0.0;
        for (const double fixed : {0.08, 0.25, 0.5, 0.75, 0.92}) {
            const Vector3 firstNormal = alongU
                ? PlateNormal(plate, parameter - delta, fixed)
                : PlateNormal(plate, fixed, parameter - delta);
            const Vector3 secondNormal = alongU
                ? PlateNormal(plate, parameter + delta, fixed)
                : PlateNormal(plate, fixed, parameter + delta);
            maximumAngle = std::max(
                maximumAngle, NormalAngleDegrees(firstNormal, secondNormal));
        }
        if (maximumAngle + 1.0e-9
            >= options.minimumFoldAngleDegrees) {
            parameters.push_back(parameter);
        }
    }
    return parameters;
}

double TotalNormalChangeDegrees(const Plate& plate, bool alongU)
{
    constexpr int samples = 32;
    double maximumTotal = 0.0;
    for (const double fixed : {0.08, 0.25, 0.5, 0.75, 0.92}) {
        double total = 0.0;
        Vector3 previous = alongU
            ? PlateNormal(plate, 0.0, fixed)
            : PlateNormal(plate, fixed, 0.0);
        for (int sample = 1; sample <= samples; ++sample) {
            const double parameter = static_cast<double>(sample) / samples;
            const Vector3 current = alongU
                ? PlateNormal(plate, parameter, fixed)
                : PlateNormal(plate, fixed, parameter);
            total += NormalAngleDegrees(previous, current);
            previous = current;
        }
        maximumTotal = std::max(maximumTotal, total);
    }
    return maximumTotal;
}

struct NamedUvPath {
    std::string name;
    std::vector<Vector2> points;
};

bool PointInsideUvPath(Vector2 point, const NamedUvPath& path)
{
    if (path.points.size() < 3) {
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

bool SegmentsIntersect(Vector2 firstA, Vector2 firstB, Vector2 secondA, Vector2 secondB)
{
    constexpr double tolerance = 1.0e-10;
    const auto orientation = [](Vector2 a, Vector2 b, Vector2 c) {
        return Cross2(b - a, c - a);
    };
    const auto within = [&](double value, double first, double second) {
        return value >= std::min(first, second) - tolerance
            && value <= std::max(first, second) + tolerance;
    };
    const auto onSegment = [&](Vector2 a, Vector2 b, Vector2 point) {
        return std::abs(orientation(a, b, point)) <= tolerance
            && within(point.x, a.x, b.x) && within(point.y, a.y, b.y);
    };
    const double first = orientation(firstA, firstB, secondA);
    const double second = orientation(firstA, firstB, secondB);
    const double third = orientation(secondA, secondB, firstA);
    const double fourth = orientation(secondA, secondB, firstB);
    if (((first > tolerance && second < -tolerance) || (first < -tolerance && second > tolerance))
        && ((third > tolerance && fourth < -tolerance) || (third < -tolerance && fourth > tolerance))) {
        return true;
    }
    return (std::abs(first) <= tolerance && onSegment(firstA, firstB, secondA))
        || (std::abs(second) <= tolerance && onSegment(firstA, firstB, secondB))
        || (std::abs(third) <= tolerance && onSegment(secondA, secondB, firstA))
        || (std::abs(fourth) <= tolerance && onSegment(secondA, secondB, firstB));
}

std::vector<double> PapercraftStripParameters(
    const Plate& plate,
    bool splitAlongU,
    const PlateFlatPatternOptions& options,
    const std::vector<NamedUvPath>& protectedPaths)
{
    if (!options.includeAutomaticReliefCuts) {
        return {0.0, 1.0};
    }
    const double length = SpatialPathLength(plate, splitAlongU, 0.5);
    const int stripCount = std::clamp(
        static_cast<int>(std::ceil(length / FidelityTargetSpacing(options.papercraftFidelity))),
        2,
        64);
    std::vector<double> parameters{0.0};
    for (int strip = 1; strip < stripCount; ++strip) {
        const double parameter = static_cast<double>(strip) / stripCount;
        bool crossesProtectedPath = false;
        for (const NamedUvPath& path : protectedPaths) {
            if (path.points.empty()) {
                continue;
            }
            double minimum = 1.0;
            double maximum = 0.0;
            for (const Vector2 point : path.points) {
                const double coordinate = splitAlongU ? point.x : point.y;
                minimum = std::min(minimum, coordinate);
                maximum = std::max(maximum, coordinate);
            }
            if (parameter > minimum + 1.0e-5 && parameter < maximum - 1.0e-5) {
                crossesProtectedPath = true;
                break;
            }
        }
        if (!crossesProtectedPath) {
            parameters.push_back(parameter);
        }
    }
    parameters.push_back(1.0);
    return parameters;
}

struct AutomaticNotchSpec {
    double strongParameter = 0.0;
    double halfMouthParameter = 0.0;
    bool fromMinimumSide = true;
    double depthRatio = 0.45;
};

bool ProtectedPathIntersectsNotch(
    const NamedUvPath& path,
    bool strongIsU,
    double strongMinimum,
    double strongMaximum,
    double depthRatio,
    bool fromMinimumSide)
{
    constexpr double clearance = 0.01;
    for (const Vector2 point : path.points) {
        const double strong = strongIsU ? point.x : point.y;
        const double weak = strongIsU ? point.y : point.x;
        const bool withinStrong = strong >= strongMinimum - clearance
            && strong <= strongMaximum + clearance;
        const bool withinWeak = fromMinimumSide
            ? weak <= depthRatio + clearance
            : weak >= 1.0 - depthRatio - clearance;
        if (withinStrong && withinWeak) {
            return true;
        }
    }
    return false;
}

std::vector<AutomaticNotchSpec> BuildAutomaticNotchSpecs(
    const Plate& plate,
    bool strongIsU,
    const PlateFlatPatternOptions& options,
    const std::vector<NamedUvPath>& protectedPaths)
{
    PlateFlatPatternOptions notchOptions = options;
    notchOptions.foldSpacingMillimeters = EffectiveFeatureSpacing(
        options, options.reliefCutSpacingMillimeters);
    notchOptions.fidelityControlsFeatureSpacing = false;
    const std::vector<double> parameters = FoldParameters(plate, strongIsU, notchOptions);
    const double strongLength = SpatialPathLength(plate, strongIsU, 0.5);
    const double weakLength = SpatialPathLength(plate, !strongIsU, 0.5);
    constexpr double degreesToRadians = 0.0174532925199432957692;
    const double physicalDepth = weakLength * options.reliefCutDepthRatio;
    const double requestedHalfMouth = physicalDepth
        * std::tan(options.reliefNotchAngleDegrees * degreesToRadians * 0.5);

    std::vector<AutomaticNotchSpec> notches;
    notches.reserve(parameters.size());
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const double parameter = parameters[index];
        const double previous = index == 0 ? 0.0 : parameters[index - 1];
        const double next = index + 1 == parameters.size() ? 1.0 : parameters[index + 1];
        const double availableHalfParameter = std::max(0.0,
            std::min(parameter - previous, next - parameter) * 0.4);
        const double halfMouthParameter = std::min({
            requestedHalfMouth / std::max(strongLength, 1.0e-9),
            availableHalfParameter,
            parameter * 0.45,
            (1.0 - parameter) * 0.45,
        });
        if (halfMouthParameter <= 1.0e-5) {
            continue;
        }

        const auto sideIsBlocked = [&](bool fromMinimumSide, double depthRatio) {
            return std::any_of(protectedPaths.begin(), protectedPaths.end(),
                [&](const NamedUvPath& path) {
                    return ProtectedPathIntersectsNotch(
                        path,
                        strongIsU,
                        parameter - halfMouthParameter,
                        parameter + halfMouthParameter,
                        depthRatio,
                        fromMinimumSide);
                });
        };
        const bool preferredMinimumSide = index % 2 == 0;
        bool fromMinimumSide = preferredMinimumSide;
        double selectedDepthRatio = 0.0;
        for (const double depthScale : {1.0, 0.75, 0.5, 0.3}) {
            const double candidateDepth = options.reliefCutDepthRatio * depthScale;
            for (const bool candidateSide : {
                     preferredMinimumSide, !preferredMinimumSide}) {
                if (!sideIsBlocked(candidateSide, candidateDepth)) {
                    fromMinimumSide = candidateSide;
                    selectedDepthRatio = candidateDepth;
                    break;
                }
            }
            if (selectedDepthRatio > 0.0) {
                break;
            }
        }
        if (selectedDepthRatio <= 0.0) {
            continue;
        }
        notches.push_back({
            parameter,
            halfMouthParameter * selectedDepthRatio / options.reliefCutDepthRatio,
            fromMinimumSide,
            selectedDepthRatio,
        });
    }
    return notches;
}

std::vector<Vector2> BuildNotchUvPath(
    const AutomaticNotchSpec& notch,
    bool strongIsU)
{
    const double side = notch.fromMinimumSide ? 0.0 : 1.0;
    const double tipWeak = notch.fromMinimumSide
        ? notch.depthRatio
        : 1.0 - notch.depthRatio;
    if (strongIsU) {
        return {
            {notch.strongParameter - notch.halfMouthParameter, side},
            {notch.strongParameter, tipWeak},
            {notch.strongParameter + notch.halfMouthParameter, side},
        };
    }
    return {
        {side, notch.strongParameter - notch.halfMouthParameter},
        {tipWeak, notch.strongParameter},
        {side, notch.strongParameter + notch.halfMouthParameter},
    };
}

template <typename MapPoint>
std::vector<Vector2> BuildMappedNotchPath(
    const std::vector<Vector2>& uv,
    const PlateFlatPatternOptions& options,
    MapPoint mapPoint)
{
    if (options.notchStyle == ReliefNotchStyle::SharpV) {
        return {mapPoint(uv[0]), mapPoint(uv[1]), mapPoint(uv[2])};
    }
    constexpr int samplesPerArm = 12;
    std::vector<Vector2> path;
    path.reserve(samplesPerArm * 2 + 1);
    for (std::size_t arm = 0; arm + 1 < uv.size(); ++arm) {
        const Vector2 mappedFirst = mapPoint(uv[arm]);
        const Vector2 mappedLast = mapPoint(uv[arm + 1]);
        for (int sample = arm == 0 ? 0 : 1; sample <= samplesPerArm; ++sample) {
            const double fraction = static_cast<double>(sample) / samplesPerArm;
            const Vector2 parameter = uv[arm] * (1.0 - fraction)
                + uv[arm + 1] * fraction;
            const Vector2 shapeFollowing = mapPoint(parameter);
            const Vector2 straight = mappedFirst * (1.0 - fraction)
                + mappedLast * fraction;
            path.push_back(
                straight * (1.0 - options.reliefNotchCurveStrength)
                + shapeFollowing * options.reliefNotchCurveStrength);
        }
    }
    return path;
}

std::vector<Vector2> BuildFlatNotchPath(
    const DevelopmentGrid& grid,
    const AutomaticNotchSpec& notch,
    bool strongIsU,
    const PlateFlatPatternOptions& options)
{
    const std::vector<Vector2> uv = BuildNotchUvPath(notch, strongIsU);
    return BuildMappedNotchPath(uv, options, [&](Vector2 point) {
        return grid.Evaluate(point.x, point.y);
    });
}

std::vector<Vector2> BuildNotchedBoundary(
    const DevelopmentGrid& grid,
    bool strongIsU,
    const std::vector<AutomaticNotchSpec>& notches,
    const PlateFlatPatternOptions& options,
    std::vector<PlateFlatPatternPath>& generatedNotches)
{
    const auto appendUnique = [](std::vector<Vector2>& target, Vector2 point) {
        if (target.empty() || !AlmostSame(target.back(), point, 1.0e-8)) {
            target.push_back(point);
        }
    };
    const auto appendSide = [&](std::vector<Vector2>& boundary,
                                bool varyingU,
                                double fixed,
                                bool increasing,
                                bool notched,
                                bool minimumSide) {
        const auto sidePoint = [&](double parameter) {
            return varyingU ? grid.Evaluate(parameter, fixed) : grid.Evaluate(fixed, parameter);
        };
        appendUnique(boundary, sidePoint(increasing ? 0.0 : 1.0));
        std::vector<const AutomaticNotchSpec*> sideNotches;
        if (notched) {
            for (const AutomaticNotchSpec& notch : notches) {
                if (notch.fromMinimumSide == minimumSide) {
                    sideNotches.push_back(&notch);
                }
            }
            std::sort(sideNotches.begin(), sideNotches.end(),
                [&](const auto* first, const auto* second) {
                    return increasing
                        ? first->strongParameter < second->strongParameter
                        : first->strongParameter > second->strongParameter;
                });
        }
        for (const AutomaticNotchSpec* notch : sideNotches) {
            std::vector<Vector2> path = BuildFlatNotchPath(grid, *notch, strongIsU, options);
            generatedNotches.push_back({
                "auto_" + std::string(
                    options.notchStyle == ReliefNotchStyle::CurvedV
                        ? "curved_v_notch_"
                        : "v_notch_") + std::to_string(generatedNotches.size() + 1),
                path,
                true,
            });
            if (!increasing) {
                std::reverse(path.begin(), path.end());
            }
            for (const Vector2 point : path) {
                appendUnique(boundary, point);
            }
        }
        appendUnique(boundary, sidePoint(increasing ? 1.0 : 0.0));
    };

    std::vector<Vector2> boundary;
    if (strongIsU) {
        appendSide(boundary, true, 0.0, true, true, true);
        appendSide(boundary, false, 1.0, true, false, false);
        appendSide(boundary, true, 1.0, false, true, false);
        appendSide(boundary, false, 0.0, false, false, true);
    } else {
        appendSide(boundary, true, 0.0, true, false, true);
        appendSide(boundary, false, 1.0, true, true, false);
        appendSide(boundary, true, 1.0, false, false, false);
        appendSide(boundary, false, 0.0, false, true, true);
    }
    ClosePath(boundary);
    return boundary;
}

struct AutomaticStripNotchSpec {
    int stripIndex = 0;
    double weakParameter = 0.0;
    double halfMouthParameter = 0.0;
    bool fromMinimumStrongSide = true;
    double depthRatio = 0.45;
};

bool ProtectedPathIntersectsStripNotch(
    const NamedUvPath& path,
    bool strongIsU,
    double strongMinimum,
    double strongMaximum,
    double weakMinimum,
    double weakMaximum,
    double depthRatio,
    bool fromMinimumStrongSide)
{
    constexpr double clearance = 0.006;
    const double depthLimit = fromMinimumStrongSide
        ? strongMinimum + (strongMaximum - strongMinimum) * depthRatio
        : strongMaximum - (strongMaximum - strongMinimum) * depthRatio;
    return std::any_of(path.points.begin(), path.points.end(), [&](Vector2 point) {
        const double strong = strongIsU ? point.x : point.y;
        const double weak = strongIsU ? point.y : point.x;
        const bool withinStrong = fromMinimumStrongSide
            ? strong >= strongMinimum - clearance && strong <= depthLimit + clearance
            : strong <= strongMaximum + clearance && strong >= depthLimit - clearance;
        return withinStrong
            && weak >= weakMinimum - clearance && weak <= weakMaximum + clearance;
    });
}

std::vector<AutomaticStripNotchSpec> BuildAutomaticStripNotchSpecs(
    const Plate& plate,
    bool strongIsU,
    const std::vector<double>& stripParameters,
    const PlateFlatPatternOptions& options,
    const std::vector<NamedUvPath>& protectedPaths)
{
    constexpr double degreesToRadians = 0.0174532925199432957692;
    const double spacing = EffectiveFeatureSpacing(options, options.reliefCutSpacingMillimeters);
    std::vector<AutomaticStripNotchSpec> notches;
    for (int strip = 0; strip + 1 < static_cast<int>(stripParameters.size()); ++strip) {
        const double strongMinimum = stripParameters[static_cast<std::size_t>(strip)];
        const double strongMaximum = stripParameters[static_cast<std::size_t>(strip + 1)];
        const double strongMiddle = (strongMinimum + strongMaximum) * 0.5;
        const double weakLength = SpatialPathLength(plate, !strongIsU, strongMiddle);
        const int intervalCount = std::clamp(
            static_cast<int>(std::ceil(weakLength / spacing)), 3, 200);
        for (int interval = 1; interval < intervalCount; ++interval) {
            const double weak = static_cast<double>(interval) / intervalCount;
            const double delta = std::min(0.02, 0.45 / intervalCount);
            const Vector3 firstNormal = strongIsU
                ? PlateNormal(plate, strongMiddle, weak - delta)
                : PlateNormal(plate, weak - delta, strongMiddle);
            const Vector3 secondNormal = strongIsU
                ? PlateNormal(plate, strongMiddle, weak + delta)
                : PlateNormal(plate, weak + delta, strongMiddle);
            if (NormalAngleDegrees(firstNormal, secondNormal) + 1.0e-9
                < options.minimumFoldAngleDegrees) {
                continue;
            }

            const Vector3 minimumPoint = strongIsU
                ? plate.Evaluate(strongMinimum, weak, 0.5)
                : plate.Evaluate(weak, strongMinimum, 0.5);
            const Vector3 maximumPoint = strongIsU
                ? plate.Evaluate(strongMaximum, weak, 0.5)
                : plate.Evaluate(weak, strongMaximum, 0.5);
            const double physicalDepth = (maximumPoint - minimumPoint).Length()
                * options.reliefCutDepthRatio;
            const double requestedHalfMouth = physicalDepth
                * std::tan(options.reliefNotchAngleDegrees * degreesToRadians * 0.5);
            const double halfMouthParameter = std::min(
                requestedHalfMouth / std::max(weakLength, 1.0e-9),
                0.4 / intervalCount);
            if (halfMouthParameter <= 1.0e-6) {
                continue;
            }

            const auto sideIsBlocked = [&](
                                           bool fromMinimumStrongSide,
                                           double depthRatio) {
                return std::any_of(protectedPaths.begin(), protectedPaths.end(),
                    [&](const NamedUvPath& path) {
                        return ProtectedPathIntersectsStripNotch(
                            path,
                            strongIsU,
                            strongMinimum,
                            strongMaximum,
                            weak - halfMouthParameter,
                            weak + halfMouthParameter,
                            depthRatio,
                            fromMinimumStrongSide);
                    });
            };
            const bool preferredMinimumStrongSide = (strip + interval) % 2 == 0;
            bool fromMinimumStrongSide = preferredMinimumStrongSide;
            double selectedDepthRatio = 0.0;
            for (const double depthScale : {1.0, 0.75, 0.5, 0.3}) {
                const double candidateDepth = options.reliefCutDepthRatio * depthScale;
                for (const bool candidateSide : {
                         preferredMinimumStrongSide, !preferredMinimumStrongSide}) {
                    if (!sideIsBlocked(candidateSide, candidateDepth)) {
                        fromMinimumStrongSide = candidateSide;
                        selectedDepthRatio = candidateDepth;
                        break;
                    }
                }
                if (selectedDepthRatio > 0.0) {
                    break;
                }
            }
            if (selectedDepthRatio <= 0.0) {
                continue;
            }
            notches.push_back({
                strip,
                weak,
                halfMouthParameter * selectedDepthRatio / options.reliefCutDepthRatio,
                fromMinimumStrongSide,
                selectedDepthRatio,
            });
        }
    }
    return notches;
}

std::vector<Vector2> BuildStripNotchUvPath(
    const AutomaticStripNotchSpec& notch,
    bool strongIsU,
    const std::vector<double>& stripParameters)
{
    const double strongMinimum = stripParameters[static_cast<std::size_t>(notch.stripIndex)];
    const double strongMaximum = stripParameters[static_cast<std::size_t>(notch.stripIndex + 1)];
    const double side = notch.fromMinimumStrongSide ? strongMinimum : strongMaximum;
    const double tip = notch.fromMinimumStrongSide
        ? strongMinimum + (strongMaximum - strongMinimum) * notch.depthRatio
        : strongMaximum - (strongMaximum - strongMinimum) * notch.depthRatio;
    if (strongIsU) {
        return {
            {side, notch.weakParameter - notch.halfMouthParameter},
            {tip, notch.weakParameter},
            {side, notch.weakParameter + notch.halfMouthParameter},
        };
    }
    return {
        {notch.weakParameter - notch.halfMouthParameter, side},
        {notch.weakParameter, tip},
        {notch.weakParameter + notch.halfMouthParameter, side},
    };
}

double PointSegmentDistance(Vector2 point, Vector2 first, Vector2 second)
{
    const Vector2 edge = second - first;
    const double squaredLength = edge.x * edge.x + edge.y * edge.y;
    if (squaredLength <= 1.0e-18) {
        return Length(point - first);
    }
    const double parameter = std::clamp(
        ((point.x - first.x) * edge.x + (point.y - first.y) * edge.y) / squaredLength,
        0.0,
        1.0);
    return Length(point - (first + edge * parameter));
}

bool IncorporateNotchInBoundary(
    std::vector<Vector2>& boundary,
    const std::vector<Vector2>& notch)
{
    if (boundary.size() < 4 || notch.size() < 3) {
        return false;
    }
    const std::size_t edgeCount = boundary.size() - 1;
    const auto nearestEdge = [&](Vector2 point) {
        std::size_t best = 0;
        double bestDistance = std::numeric_limits<double>::infinity();
        for (std::size_t edge = 0; edge < edgeCount; ++edge) {
            const double distance = PointSegmentDistance(
                point, boundary[edge], boundary[(edge + 1) % edgeCount]);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = edge;
            }
        }
        return std::pair{best, bestDistance};
    };
    const auto [firstEdge, firstDistance] = nearestEdge(notch.front());
    const auto [lastEdge, lastDistance] = nearestEdge(notch.back());
    if (firstDistance > 1.0e-5 || lastDistance > 1.0e-5) {
        return false;
    }

    std::vector<double> boundaryDistance(edgeCount + 1, 0.0);
    for (std::size_t edge = 0; edge < edgeCount; ++edge) {
        boundaryDistance[edge + 1] = boundaryDistance[edge]
            + Length(boundary[edge + 1] - boundary[edge]);
    }
    const double perimeter = boundaryDistance.back();
    const auto boundaryPosition = [&](Vector2 point, std::size_t edge) {
        const Vector2 edgeVector = boundary[edge + 1] - boundary[edge];
        const double squaredLength = edgeVector.x * edgeVector.x + edgeVector.y * edgeVector.y;
        const double parameter = squaredLength <= 1.0e-18
            ? 0.0
            : std::clamp(
                ((point.x - boundary[edge].x) * edgeVector.x
                    + (point.y - boundary[edge].y) * edgeVector.y) / squaredLength,
                0.0,
                1.0);
        return boundaryDistance[edge] + std::sqrt(squaredLength) * parameter;
    };
    const double firstPosition = boundaryPosition(notch.front(), firstEdge);
    const double lastPosition = boundaryPosition(notch.back(), lastEdge);
    const auto forwardLength = [&](double from, double to) {
        return to >= from ? to - from : perimeter - from + to;
    };
    const double firstToLast = forwardLength(firstPosition, lastPosition);
    const double lastToFirst = forwardLength(lastPosition, firstPosition);

    Vector2 keptStart;
    Vector2 keptEnd;
    std::size_t keptStartEdge;
    std::size_t keptEndEdge;
    std::vector<Vector2> replacement = notch;
    if (firstToLast <= lastToFirst) {
        keptStart = notch.back();
        keptStartEdge = lastEdge;
        keptEnd = notch.front();
        keptEndEdge = firstEdge;
    } else {
        std::reverse(replacement.begin(), replacement.end());
        keptStart = notch.front();
        keptStartEdge = firstEdge;
        keptEnd = notch.back();
        keptEndEdge = lastEdge;
    }

    std::vector<Vector2> rebuilt = replacement;
    const auto appendUnique = [](std::vector<Vector2>& points, Vector2 point) {
        if (points.empty() || !AlmostSame(points.back(), point, 1.0e-8)) {
            points.push_back(point);
        }
    };
    appendUnique(rebuilt, keptStart);
    std::size_t edge = (keptStartEdge + 1) % edgeCount;
    while (edge != keptEndEdge) {
        appendUnique(rebuilt, boundary[edge]);
        edge = (edge + 1) % edgeCount;
    }
    appendUnique(rebuilt, boundary[keptEndEdge]);
    appendUnique(rebuilt, keptEnd);
    ClosePath(rebuilt);
    boundary = std::move(rebuilt);
    return true;
}

std::optional<std::vector<Vector2>> BoundaryOpeningCut(
    const std::vector<Vector2>& boundary,
    const std::vector<Vector2>& closedOpening)
{
    if (boundary.size() < 4 || closedOpening.size() < 4) {
        return std::nullopt;
    }
    std::vector<Vector2> opening = closedOpening;
    if (AlmostSame(opening.front(), opening.back(), 1.0e-7)) {
        opening.pop_back();
    }
    if (opening.size() < 3) {
        return std::nullopt;
    }

    const auto onBoundary = [&](Vector2 point) {
        for (std::size_t edge = 0; edge + 1 < boundary.size(); ++edge) {
            if (PointSegmentDistance(point, boundary[edge], boundary[edge + 1]) <= 1.0e-5) {
                return true;
            }
        }
        return false;
    };

    std::optional<std::vector<Vector2>> best;
    double bestLength = 0.0;
    for (std::size_t start = 0; start < opening.size(); ++start) {
        if (!onBoundary(opening[start])) {
            continue;
        }
        std::vector<Vector2> candidate{opening[start]};
        double length = 0.0;
        for (std::size_t step = 1; step < opening.size(); ++step) {
            const std::size_t index = (start + step) % opening.size();
            length += Length(opening[index] - candidate.back());
            candidate.push_back(opening[index]);
            if (onBoundary(opening[index])) {
                if (candidate.size() >= 3 && length > bestLength) {
                    best = candidate;
                    bestLength = length;
                }
                break;
            }
        }
    }
    return best;
}

bool IncorporateOpeningInBoundary(
    std::vector<Vector2>& boundary,
    const std::vector<Vector2>& closedOpening)
{
    const auto cut = BoundaryOpeningCut(boundary, closedOpening);
    return cut.has_value() && IncorporateNotchInBoundary(boundary, *cut);
}

class PapercraftGrid {
public:
    PapercraftGrid(
        const Plate& plate,
        bool splitAlongU,
        std::vector<double> stripParameters,
        int weakSegments,
        bool splitAcrossWeak,
        const std::vector<NamedUvPath>& splitLines,
        const PlateFlatPatternOptions& options)
        : plate_(plate),
          splitAlongU_(splitAlongU),
          stripParameters_(std::move(stripParameters)),
          weakSegments_(std::max(2, weakSegments)),
          splitAcrossWeak_(splitAcrossWeak)
    {
        openingRefinementDepth_ = std::clamp(
            2 + options.papercraftFidelity / 2, 2, 6);
        BuildMesh();
        BuildAdjacency();
        MarkTransverseCuts();
        MarkManualCuts(splitLines);
        DevelopPieces(options);
    }

    void AddOpeningPath(const NamedUvPath& path)
    {
        const auto mapped = MapPath(path);
        if (mapped.first >= 0) {
            PlateFlatPatternPiece& piece = pieces_[static_cast<std::size_t>(mapped.first)];
            PlateFlatPatternPath opening{path.name, mapped.second};
            opening.incorporatedInOuterBoundary = IncorporateOpeningInBoundary(
                piece.outerBoundary.points, opening.points);
            piece.openings.push_back(std::move(opening));
            return;
        }

        int fragmentIndex = 0;
        for (int strip = 0; strip + 1 < static_cast<int>(stripParameters_.size()); ++strip) {
            const double strongMinimum = stripParameters_[static_cast<std::size_t>(strip)];
            const double strongMaximum = stripParameters_[static_cast<std::size_t>(strip + 1)];
            const int weakCellCount = splitAcrossWeak_ ? weakSegments_ : 1;
            for (int weakCell = 0; weakCell < weakCellCount; ++weakCell) {
                const double weakMinimum = splitAcrossWeak_
                    ? static_cast<double>(weakCell) / weakSegments_
                    : 0.0;
                const double weakMaximum = splitAcrossWeak_
                    ? static_cast<double>(weakCell + 1) / weakSegments_
                    : 1.0;
                std::vector<Vector2> clipped = splitAlongU_
                    ? ClipToRectangle(
                        path.points, strongMinimum, strongMaximum, weakMinimum, weakMaximum)
                    : ClipToRectangle(
                        path.points, weakMinimum, weakMaximum, strongMinimum, strongMaximum);
                if (clipped.size() < 3) {
                    continue;
                }
                double twiceArea = 0.0;
                for (std::size_t point = 0; point < clipped.size(); ++point) {
                    const Vector2 first = clipped[point];
                    const Vector2 second = clipped[(point + 1) % clipped.size()];
                    twiceArea += Cross2(first, second);
                }
                if (std::abs(twiceArea) <= 1.0e-10) {
                    continue;
                }
                ClosePath(clipped);
                int pieceIndex = -1;
                bool crossesManualBoundary = false;
                std::vector<Vector2> flat;
                flat.reserve(clipped.size());
                for (const Vector2 point : clipped) {
                    const auto fragmentPoint = splitAcrossWeak_
                        ? EvaluateInCell(point, strip, weakCell)
                        : EvaluateInStrip(point, strip);
                    if (pieceIndex < 0) {
                        pieceIndex = fragmentPoint.first;
                    } else if (pieceIndex != fragmentPoint.first) {
                        crossesManualBoundary = true;
                        break;
                    }
                    flat.push_back(fragmentPoint.second);
                }
                if (crossesManualBoundary || pieceIndex < 0) {
                    throw std::invalid_argument(
                        "A plate opening crosses a manual papercraft split: " + path.name);
                }
                PlateFlatPatternPiece& piece = pieces_[static_cast<std::size_t>(pieceIndex)];
                PlateFlatPatternPath opening{
                    path.name + "_fragment_" + std::to_string(++fragmentIndex),
                    std::move(flat),
                };
                opening.incorporatedInOuterBoundary = IncorporateOpeningInBoundary(
                    piece.outerBoundary.points, opening.points);
                piece.openings.push_back(std::move(opening));
            }
        }
        if (fragmentIndex == 0) {
            throw std::invalid_argument(
                "A plate opening could not be assigned to a papercraft piece: " + path.name);
        }
    }

    void AddReliefPath(const NamedUvPath& path)
    {
        const auto mapped = MapPath(path);
        if (mapped.first < 0) {
            throw std::invalid_argument(
                "A relief cut crosses a papercraft piece boundary: " + path.name);
        }
        pieces_[static_cast<std::size_t>(mapped.first)].reliefCuts.push_back({
            path.name, mapped.second,
        });
    }

    int AddAutomaticNotches(
        const std::vector<AutomaticStripNotchSpec>& notches,
        const PlateFlatPatternOptions& options)
    {
        int addedCount = 0;
        for (const AutomaticStripNotchSpec& notch : notches) {
            const std::vector<Vector2> uv = BuildStripNotchUvPath(
                notch, splitAlongU_, stripParameters_);
            int pieceIndex = -1;
            bool crossesPieceBoundary = false;
            std::vector<Vector2> flat = BuildMappedNotchPath(
                uv, options, [&](Vector2 point) {
                const auto mapped = EvaluateInStrip(point, notch.stripIndex);
                if (pieceIndex < 0) {
                    pieceIndex = mapped.first;
                } else if (pieceIndex != mapped.first) {
                    crossesPieceBoundary = true;
                }
                return mapped.second;
            });
            if (crossesPieceBoundary || pieceIndex < 0
                || pieceIndex >= static_cast<int>(pieces_.size())) {
                continue;
            }
            PlateFlatPatternPiece& piece = pieces_[static_cast<std::size_t>(pieceIndex)];
            if (!IncorporateNotchInBoundary(piece.outerBoundary.points, flat)) {
                continue;
            }
            piece.reliefCuts.push_back({
                "auto_" + std::string(
                    options.notchStyle == ReliefNotchStyle::CurvedV
                        ? "curved_v_notch_"
                        : "v_notch_") + std::to_string(++addedCount),
                std::move(flat),
                true,
            });
        }
        return addedCount;
    }

    [[nodiscard]] const std::vector<PlateFlatPatternPiece>& Pieces() const noexcept { return pieces_; }
    [[nodiscard]] double MaximumEdgeDistortion() const noexcept { return maximumEdgeDistortion_; }
    [[nodiscard]] double RootMeanSquareEdgeDistortion() const noexcept { return rmsEdgeDistortion_; }

    [[nodiscard]] PlateAssemblyMotion BuildAssemblyMotion(
        std::string plateName,
        double progress,
        const std::vector<NamedUvPath>& openings) const
    {
        PlateAssemblyMotion motion;
        motion.plateName = std::move(plateName);
        motion.progress = std::clamp(progress, 0.0, 1.0);
        motion.pieceCount = static_cast<int>(pieces_.size());
        motion.panels.reserve(triangles_.size());
        motion.pieceIndices.reserve(triangles_.size());
        motion.panelThicknessMillimeters.reserve(triangles_.size());
        motion.panelDeviationMillimeters.reserve(triangles_.size());

        Vector3 layoutOrigin = plate_.Evaluate(0.0, 0.0, 0.5);
        Vector3 layoutNormal = PlateNormal(plate_, 0.01, 0.01);
        Vector3 layoutX = plate_.Evaluate(0.01, 0.0, 0.5) - layoutOrigin;
        layoutX = layoutX - layoutNormal * geometry::Dot(layoutX, layoutNormal);
        if (layoutX.LengthSquared() <= 1.0e-18) {
            layoutX = plate_.Evaluate(0.0, 0.01, 0.5) - layoutOrigin;
            layoutX = layoutX - layoutNormal * geometry::Dot(layoutX, layoutNormal);
        }
        layoutX = layoutX.Normalized();
        Vector3 layoutY = geometry::Cross(layoutNormal, layoutX).Normalized();
        const Vector3 sourceV = plate_.Evaluate(0.0, 0.01, 0.5) - layoutOrigin;
        if (geometry::Dot(layoutY, sourceV) < 0.0) {
            layoutY = -layoutY;
            layoutNormal = -layoutNormal;
        }

        std::vector<std::array<Vector3, 3>> current(triangles_.size());
        for (std::size_t triangleIndex = 0; triangleIndex < triangles_.size(); ++triangleIndex) {
            const int pieceIndex = trianglePiece_[triangleIndex];
            for (int corner = 0; corner < 3; ++corner) {
                const Vector2 point = triangleFlat_[triangleIndex][corner]
                    + pieceOffsets_[static_cast<std::size_t>(pieceIndex)];
                current[triangleIndex][corner]
                    = layoutOrigin + layoutX * point.x + layoutY * point.y;
            }
        }

        const auto pointForVertex = [&](int triangleIndex, int vertex) {
            const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
            const auto position = std::find(vertices.begin(), vertices.end(), vertex);
            if (position == vertices.end()) {
                throw std::logic_error("Assembly hinge vertex was not found.");
            }
            return current[static_cast<std::size_t>(triangleIndex)]
                [static_cast<std::size_t>(std::distance(vertices.begin(), position))];
        };
        const auto sourceTriangle = [&](int triangleIndex) {
            std::array<Vector3, 3> result;
            const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
            for (int corner = 0; corner < 3; ++corner) {
                result[static_cast<std::size_t>(corner)]
                    = spatial_[static_cast<std::size_t>(vertices[corner])];
            }
            return result;
        };
        const auto panelDeviation = [&](int triangleIndex) {
            const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
            const std::array<Vector2, 3> triangleUv{{
                uv_[static_cast<std::size_t>(vertices[0])],
                uv_[static_cast<std::size_t>(vertices[1])],
                uv_[static_cast<std::size_t>(vertices[2])],
            }};
            const std::array<Vector3, 3> target = sourceTriangle(triangleIndex);
            const Vector3 normal = TriangleNormal(target);
            const std::array<std::array<double, 3>, 4> samples{{
                {{0.5, 0.5, 0.0}},
                {{0.0, 0.5, 0.5}},
                {{0.5, 0.0, 0.5}},
                {{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}},
            }};
            double maximum = 0.0;
            for (const auto& weights : samples) {
                const Vector2 parameter = triangleUv[0] * weights[0]
                    + triangleUv[1] * weights[1]
                    + triangleUv[2] * weights[2];
                const Vector3 source = plate_.Evaluate(
                    parameter.x, parameter.y, 0.5);
                maximum = std::max(
                    maximum, std::abs(geometry::Dot(source - target[0], normal)));
            }
            return maximum;
        };

        for (int pieceIndex = 0; pieceIndex < static_cast<int>(pieces_.size()); ++pieceIndex) {
            std::vector<int> component;
            for (int triangleIndex = 0;
                 triangleIndex < static_cast<int>(triangles_.size()); ++triangleIndex) {
                if (trianglePiece_[static_cast<std::size_t>(triangleIndex)] == pieceIndex) {
                    component.push_back(triangleIndex);
                }
            }
            if (component.empty()) {
                continue;
            }

            const int root = component.front();
            std::vector<int> parent(triangles_.size(), -2);
            std::vector<std::pair<int, int>> parentEdge(triangles_.size(), {-1, -1});
            std::vector<int> traversal;
            std::queue<int> pending;
            parent[static_cast<std::size_t>(root)] = -1;
            pending.push(root);
            while (!pending.empty()) {
                const int currentIndex = pending.front();
                pending.pop();
                traversal.push_back(currentIndex);
                const auto& vertices = triangles_[static_cast<std::size_t>(currentIndex)].vertices;
                for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                    const auto edge = std::minmax(
                        vertices[edgeIndex], vertices[(edgeIndex + 1) % 3]);
                    if (cutEdges_.contains(edge)) {
                        continue;
                    }
                    for (const int neighbor : edgeTriangles_.at(edge)) {
                        if (neighbor == currentIndex
                            || trianglePiece_[static_cast<std::size_t>(neighbor)] != pieceIndex
                            || parent[static_cast<std::size_t>(neighbor)] != -2) {
                            continue;
                        }
                        parent[static_cast<std::size_t>(neighbor)] = currentIndex;
                        parentEdge[static_cast<std::size_t>(neighbor)] = edge;
                        pending.push(neighbor);
                    }
                }
            }

            const std::array<Vector3, 3> flatRoot = current[static_cast<std::size_t>(root)];
            const std::array<Vector3, 3> targetRoot = sourceTriangle(root);
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
                firstAngle = 3.14159265358979323846;
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
            for (const int triangleIndex : component) {
                for (Vector3& point : current[static_cast<std::size_t>(triangleIndex)]) {
                    Vector3 relative = point - flatRoot[0];
                    relative = RotateAroundAxis(
                        relative, firstAxis, firstAngle * motion.progress);
                    relative = RotateAroundAxis(
                        relative, partialNormal, twistAngle * motion.progress);
                    point = rootPosition + relative;
                }
            }

            std::vector<RigidTransform> cumulative(triangles_.size());
            for (std::size_t traversalIndex = 1;
                 traversalIndex < traversal.size(); ++traversalIndex) {
                const int child = traversal[traversalIndex];
                const int parentIndex = parent[static_cast<std::size_t>(child)];
                const auto edge = parentEdge[static_cast<std::size_t>(child)];
                const Vector3 axisStart = pointForVertex(parentIndex, edge.first);
                const Vector3 axisEnd = pointForVertex(parentIndex, edge.second);
                const Vector3 sourceAxis
                    = (spatial_[static_cast<std::size_t>(edge.second)]
                        - spatial_[static_cast<std::size_t>(edge.first)]).Normalized();
                const Vector3 parentNormal = TriangleNormal(sourceTriangle(parentIndex));
                const Vector3 childNormal = TriangleNormal(sourceTriangle(child));
                const double foldAngle = std::atan2(
                    geometry::Dot(
                        sourceAxis, geometry::Cross(parentNormal, childNormal)),
                    geometry::Dot(parentNormal, childNormal));
                const RigidTransform hinge = RotationAroundLine(
                    axisStart,
                    axisEnd - axisStart,
                    foldAngle * motion.progress);
                cumulative[static_cast<std::size_t>(child)] = Compose(
                    hinge, cumulative[static_cast<std::size_t>(parentIndex)]);
                for (Vector3& point : current[static_cast<std::size_t>(child)]) {
                    point = cumulative[static_cast<std::size_t>(child)].Apply(point);
                }
            }

            const auto pointInTriangle = [](Vector2 point, const std::array<Vector2, 3>& triangle) {
                constexpr double tolerance = 1.0e-10;
                const double first = Cross2(triangle[1] - triangle[0], point - triangle[0]);
                const double second = Cross2(triangle[2] - triangle[1], point - triangle[1]);
                const double third = Cross2(triangle[0] - triangle[2], point - triangle[2]);
                const bool hasNegative = first < -tolerance || second < -tolerance || third < -tolerance;
                const bool hasPositive = first > tolerance || second > tolerance || third > tolerance;
                return !(hasNegative && hasPositive);
            };
            const auto openingBoundaryIntersects = [&](const std::array<Vector2, 3>& triangle) {
                for (const NamedUvPath& opening : openings) {
                    if (std::any_of(opening.points.begin(), opening.points.end(),
                            [&](Vector2 point) { return pointInTriangle(point, triangle); })) {
                        return true;
                    }
                    for (std::size_t point = 1; point < opening.points.size(); ++point) {
                        for (int edge = 0; edge < 3; ++edge) {
                            if (SegmentsIntersect(
                                    opening.points[point - 1], opening.points[point],
                                    triangle[static_cast<std::size_t>(edge)],
                                    triangle[static_cast<std::size_t>((edge + 1) % 3)])) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            };
            for (const int triangleIndex : component) {
                const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
                const std::array<Vector2, 3> baseUv{{
                    uv_[static_cast<std::size_t>(vertices[0])],
                    uv_[static_cast<std::size_t>(vertices[1])],
                    uv_[static_cast<std::size_t>(vertices[2])],
                }};
                const std::array<Vector3, 3> basePanel
                    = current[static_cast<std::size_t>(triangleIndex)];
                const double deviation = panelDeviation(triangleIndex);
                std::function<void(
                    const std::array<Vector2, 3>&,
                    const std::array<Vector3, 3>&,
                    int)> emitPanel;
                emitPanel = [&](const std::array<Vector2, 3>& panelUv,
                                const std::array<Vector3, 3>& panel,
                                int depth) {
                    const Vector2 uvCenter = (panelUv[0] + panelUv[1] + panelUv[2]) * (1.0 / 3.0);
                    const auto insideOpening = [&](Vector2 point) {
                        return std::any_of(openings.begin(), openings.end(),
                            [&](const NamedUvPath& opening) {
                                return PointInsideUvPath(point, opening);
                            });
                    };
                    const bool allInside = insideOpening(panelUv[0])
                        && insideOpening(panelUv[1]) && insideOpening(panelUv[2]);
                    const bool boundaryCrosses = openingBoundaryIntersects(panelUv);
                    if (allInside && !boundaryCrosses) {
                        return;
                    }
                    if (boundaryCrosses && depth < openingRefinementDepth_) {
                        const std::array<Vector2, 3> uvMid{{
                            (panelUv[0] + panelUv[1]) * 0.5,
                            (panelUv[1] + panelUv[2]) * 0.5,
                            (panelUv[2] + panelUv[0]) * 0.5,
                        }};
                        const std::array<Vector3, 3> mid{{
                            (panel[0] + panel[1]) * 0.5,
                            (panel[1] + panel[2]) * 0.5,
                            (panel[2] + panel[0]) * 0.5,
                        }};
                        emitPanel({{panelUv[0], uvMid[0], uvMid[2]}},
                            {{panel[0], mid[0], mid[2]}}, depth + 1);
                        emitPanel({{uvMid[0], panelUv[1], uvMid[1]}},
                            {{mid[0], panel[1], mid[1]}}, depth + 1);
                        emitPanel({{uvMid[2], uvMid[1], panelUv[2]}},
                            {{mid[2], mid[1], panel[2]}}, depth + 1);
                        emitPanel({{uvMid[0], uvMid[1], uvMid[2]}},
                            {{mid[0], mid[1], mid[2]}}, depth + 1);
                        return;
                    }
                    if (insideOpening(uvCenter)) {
                        return;
                    }
                    for (int corner = 0; corner < 3; ++corner) {
                        const Vector3 target = plate_.Evaluate(
                            panelUv[static_cast<std::size_t>(corner)].x,
                            panelUv[static_cast<std::size_t>(corner)].y,
                            0.5);
                        motion.maximumTargetMismatchMillimeters = std::max(
                            motion.maximumTargetMismatchMillimeters,
                            (panel[static_cast<std::size_t>(corner)] - target).Length());
                    }
                    motion.panels.push_back(panel);
                    motion.pieceIndices.push_back(pieceIndex);
                    motion.panelThicknessMillimeters.push_back(plate_.Thickness(uvCenter.y));
                    motion.panelDeviationMillimeters.push_back(deviation);
                    motion.maximumPanelDeviationMillimeters = std::max(
                        motion.maximumPanelDeviationMillimeters, deviation);
                    motion.materialAreaSquareMillimeters += geometry::Cross(
                        panel[1] - panel[0], panel[2] - panel[0]).Length() * 0.5;
                };
                emitPanel(baseUv, basePanel, 0);
            }
        }
        return motion;
    }

private:
    [[nodiscard]] int VertexIndex(int strip, int side, int weakIndex) const noexcept
    {
        return strip * 2 * (weakSegments_ + 1) + side * (weakSegments_ + 1) + weakIndex;
    }

    void BuildMesh()
    {
        const int stripCount = static_cast<int>(stripParameters_.size()) - 1;
        uv_.reserve(static_cast<std::size_t>(stripCount * 2 * (weakSegments_ + 1)));
        spatial_.reserve(uv_.capacity());
        for (int strip = 0; strip < stripCount; ++strip) {
            for (int side = 0; side < 2; ++side) {
                const double strong = stripParameters_[static_cast<std::size_t>(strip + side)];
                for (int weakIndex = 0; weakIndex <= weakSegments_; ++weakIndex) {
                    const double weak = static_cast<double>(weakIndex) / weakSegments_;
                    const Vector2 point = splitAlongU_ ? Vector2{strong, weak} : Vector2{weak, strong};
                    uv_.push_back(point);
                    spatial_.push_back(plate_.Evaluate(point.x, point.y, 0.5));
                }
            }
            for (int weakIndex = 0; weakIndex < weakSegments_; ++weakIndex) {
                int lowerLeft;
                int lowerRight;
                int upperLeft;
                int upperRight;
                if (splitAlongU_) {
                    lowerLeft = VertexIndex(strip, 0, weakIndex);
                    lowerRight = VertexIndex(strip, 1, weakIndex);
                    upperLeft = VertexIndex(strip, 0, weakIndex + 1);
                    upperRight = VertexIndex(strip, 1, weakIndex + 1);
                } else {
                    lowerLeft = VertexIndex(strip, 0, weakIndex);
                    lowerRight = VertexIndex(strip, 0, weakIndex + 1);
                    upperLeft = VertexIndex(strip, 1, weakIndex);
                    upperRight = VertexIndex(strip, 1, weakIndex + 1);
                }
                triangles_.push_back({{lowerLeft, lowerRight, upperRight}});
                triangles_.push_back({{lowerLeft, upperRight, upperLeft}});
            }
        }
    }

    void BuildAdjacency()
    {
        for (int triangleIndex = 0; triangleIndex < static_cast<int>(triangles_.size()); ++triangleIndex) {
            const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                edgeTriangles_[std::minmax(vertices[edgeIndex], vertices[(edgeIndex + 1) % 3])]
                    .push_back(triangleIndex);
            }
        }
    }

    void MarkTransverseCuts()
    {
        if (!splitAcrossWeak_) {
            return;
        }
        for (const auto& [edge, adjacent] : edgeTriangles_) {
            if (adjacent.size() != 2) {
                continue;
            }
            const Vector2 first = uv_[static_cast<std::size_t>(edge.first)];
            const Vector2 second = uv_[static_cast<std::size_t>(edge.second)];
            const double firstWeak = splitAlongU_ ? first.y : first.x;
            const double secondWeak = splitAlongU_ ? second.y : second.x;
            const double firstStrong = splitAlongU_ ? first.x : first.y;
            const double secondStrong = splitAlongU_ ? second.x : second.y;
            if (std::abs(firstWeak - secondWeak) <= 1.0e-10
                && std::abs(firstStrong - secondStrong) > 1.0e-10) {
                cutEdges_.insert(edge);
            }
        }
    }

    void MarkManualCuts(const std::vector<NamedUvPath>& splitLines)
    {
        for (const NamedUvPath& split : splitLines) {
            const bool alreadyASeam = std::any_of(
                stripParameters_.begin() + 1, stripParameters_.end() - 1,
                [&](double seam) {
                    double minimumWeak = 1.0;
                    double maximumWeak = 0.0;
                    for (const Vector2 point : split.points) {
                        const double strong = splitAlongU_ ? point.x : point.y;
                        if (std::abs(strong - seam) > 1.0e-3) {
                            return false;
                        }
                        const double weak = splitAlongU_ ? point.y : point.x;
                        minimumWeak = std::min(minimumWeak, weak);
                        maximumWeak = std::max(maximumWeak, weak);
                    }
                    return minimumWeak <= 1.0e-3 && maximumWeak >= 1.0 - 1.0e-3;
                });
            if (alreadyASeam) {
                continue;
            }
            bool intersectsMesh = false;
            for (const auto& [edge, adjacent] : edgeTriangles_) {
                if (adjacent.size() != 2) {
                    continue;
                }
                for (std::size_t index = 1; index < split.points.size(); ++index) {
                    if (SegmentsIntersect(
                            uv_[static_cast<std::size_t>(edge.first)],
                            uv_[static_cast<std::size_t>(edge.second)],
                            split.points[index - 1],
                            split.points[index])) {
                        intersectsMesh = true;
                        cutEdges_.insert(edge);
                        break;
                    }
                }
            }
            if (!intersectsMesh) {
                throw std::invalid_argument("Split line does not cross the papercraft fold mesh: " + split.name);
            }
        }
    }

    void DevelopPieces(const PlateFlatPatternOptions& options)
    {
        std::vector<int> trianglePiece(triangles_.size(), -1);
        std::vector<std::vector<int>> components;
        for (int seed = 0; seed < static_cast<int>(triangles_.size()); ++seed) {
            if (trianglePiece[static_cast<std::size_t>(seed)] >= 0) {
                continue;
            }
            const int pieceIndex = static_cast<int>(components.size());
            components.emplace_back();
            std::queue<int> pending;
            pending.push(seed);
            trianglePiece[static_cast<std::size_t>(seed)] = pieceIndex;
            while (!pending.empty()) {
                const int current = pending.front();
                pending.pop();
                components.back().push_back(current);
                const auto& vertices = triangles_[static_cast<std::size_t>(current)].vertices;
                for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                    const auto edge = std::minmax(vertices[edgeIndex], vertices[(edgeIndex + 1) % 3]);
                    if (cutEdges_.contains(edge)) {
                        continue;
                    }
                    for (const int neighbor : edgeTriangles_[edge]) {
                        if (trianglePiece[static_cast<std::size_t>(neighbor)] < 0) {
                            trianglePiece[static_cast<std::size_t>(neighbor)] = pieceIndex;
                            pending.push(neighbor);
                        }
                    }
                }
            }
        }

        trianglePiece_ = std::move(trianglePiece);
        triangleFlat_.resize(triangles_.size());
        pieces_.resize(components.size());
        pieceOffsets_.resize(components.size());
        double squaredError = 0.0;
        std::size_t measuredEdges = 0;
        double cursorX = 0.0;
        for (std::size_t pieceIndex = 0; pieceIndex < components.size(); ++pieceIndex) {
            const auto& component = components[pieceIndex];
            std::map<int, Vector2> flat;
            const auto& initial = triangles_[static_cast<std::size_t>(component.front())].vertices;
            flat[initial[0]] = {0.0, 0.0};
            flat[initial[1]] = {(spatial_[initial[1]] - spatial_[initial[0]]).Length(), 0.0};
            flat[initial[2]] = PlaceTriangleVertex(
                flat[initial[0]], flat[initial[1]],
                (spatial_[initial[2]] - spatial_[initial[0]]).Length(),
                (spatial_[initial[2]] - spatial_[initial[1]]).Length(),
                {0.0, -1.0});
            std::set<int> processed{component.front()};
            std::queue<int> pending;
            pending.push(component.front());
            while (!pending.empty()) {
                const int currentIndex = pending.front();
                pending.pop();
                const auto& current = triangles_[static_cast<std::size_t>(currentIndex)].vertices;
                for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                    const int sharedFirst = current[edgeIndex];
                    const int sharedSecond = current[(edgeIndex + 1) % 3];
                    const int reference = current[(edgeIndex + 2) % 3];
                    const auto edge = std::minmax(sharedFirst, sharedSecond);
                    if (cutEdges_.contains(edge)) {
                        continue;
                    }
                    for (const int neighborIndex : edgeTriangles_[edge]) {
                        if (trianglePiece_[static_cast<std::size_t>(neighborIndex)] != static_cast<int>(pieceIndex)
                            || processed.contains(neighborIndex)) {
                            continue;
                        }
                        const auto& neighbor = triangles_[static_cast<std::size_t>(neighborIndex)].vertices;
                        const int target = *std::find_if(neighbor.begin(), neighbor.end(), [&](int vertex) {
                            return vertex != sharedFirst && vertex != sharedSecond;
                        });
                        if (!flat.contains(target)) {
                            flat[target] = PlaceTriangleVertex(
                                flat[sharedFirst], flat[sharedSecond],
                                (spatial_[target] - spatial_[sharedFirst]).Length(),
                                (spatial_[target] - spatial_[sharedSecond]).Length(),
                                flat[reference]);
                        }
                        processed.insert(neighborIndex);
                        pending.push(neighborIndex);
                    }
                }
            }
            for (const int triangleIndex : component) {
                const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
                for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
                    triangleFlat_[static_cast<std::size_t>(triangleIndex)][vertexIndex]
                        = flat.at(vertices[vertexIndex]);
                }
                for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                    const int first = vertices[edgeIndex];
                    const int second = vertices[(edgeIndex + 1) % 3];
                    const double spatialLength = (spatial_[second] - spatial_[first]).Length();
                    const double flatLength = Length(flat.at(second) - flat.at(first));
                    const double error = std::abs(flatLength - spatialLength);
                    maximumEdgeDistortion_ = std::max(maximumEdgeDistortion_, error);
                    squaredError += error * error;
                    ++measuredEdges;
                }
            }

            PlateFlatPatternPiece& piece = pieces_[pieceIndex];
            piece.name = "piece_" + std::to_string(pieceIndex + 1);
            piece.outerBoundary.name = piece.name;
            piece.outerBoundary.points = BuildBoundary(component, static_cast<int>(pieceIndex), flat);
            if (options.includeFoldLines) {
                AddFoldLines(piece, component, static_cast<int>(pieceIndex), flat, options);
            }
            double minimumX = std::numeric_limits<double>::infinity();
            double minimumY = std::numeric_limits<double>::infinity();
            double maximumX = -std::numeric_limits<double>::infinity();
            for (const Vector2 point : piece.outerBoundary.points) {
                minimumX = std::min(minimumX, point.x);
                minimumY = std::min(minimumY, point.y);
                maximumX = std::max(maximumX, point.x);
            }
            pieceOffsets_[pieceIndex] = {cursorX - minimumX, -minimumY};
            TranslatePiece(piece, pieceOffsets_[pieceIndex]);
            cursorX += maximumX - minimumX + options.marginMillimeters;
        }
        if (measuredEdges > 0) {
            rmsEdgeDistortion_ = std::sqrt(squaredError / static_cast<double>(measuredEdges));
        }
    }

    std::vector<Vector2> BuildBoundary(
        const std::vector<int>& component,
        int pieceIndex,
        const std::map<int, Vector2>& flat) const
    {
        std::map<int, std::vector<int>> boundaryNeighbors;
        for (const int triangleIndex : component) {
            const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                const int first = vertices[edgeIndex];
                const int second = vertices[(edgeIndex + 1) % 3];
                const auto edge = std::minmax(first, second);
                int samePieceNeighbors = 0;
                for (const int adjacent : edgeTriangles_.at(edge)) {
                    if (trianglePiece_[static_cast<std::size_t>(adjacent)] == pieceIndex
                        && !cutEdges_.contains(edge)) {
                        ++samePieceNeighbors;
                    }
                }
                if (samePieceNeighbors < 2) {
                    boundaryNeighbors[first].push_back(second);
                    boundaryNeighbors[second].push_back(first);
                }
            }
        }
        if (boundaryNeighbors.empty()) {
            throw std::logic_error("Papercraft piece has no boundary.");
        }
        const int start = boundaryNeighbors.begin()->first;
        int previous = -1;
        int current = start;
        std::vector<Vector2> boundary;
        do {
            boundary.push_back(flat.at(current));
            const auto& neighbors = boundaryNeighbors.at(current);
            if (neighbors.empty()) {
                throw std::logic_error("Papercraft boundary is open.");
            }
            const int next = neighbors.front() == previous && neighbors.size() > 1
                ? neighbors[1]
                : neighbors.front();
            previous = current;
            current = next;
            if (boundary.size() > boundaryNeighbors.size() + 1) {
                throw std::logic_error("Papercraft boundary could not be ordered.");
            }
        } while (current != start);
        ClosePath(boundary);
        return boundary;
    }

    void AddFoldLines(
        PlateFlatPatternPiece& piece,
        const std::vector<int>& component,
        int pieceIndex,
        const std::map<int, Vector2>& flat,
        const PlateFlatPatternOptions& options) const
    {
        std::set<std::pair<int, int>> added;
        for (const int triangleIndex : component) {
            const auto& vertices = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex) {
                const auto edge = std::minmax(vertices[edgeIndex], vertices[(edgeIndex + 1) % 3]);
                if (!added.insert(edge).second || cutEdges_.contains(edge)) {
                    continue;
                }
                const Vector2 firstUv = uv_[static_cast<std::size_t>(edge.first)];
                const Vector2 secondUv = uv_[static_cast<std::size_t>(edge.second)];
                const double strongDelta = std::abs(
                    (splitAlongU_ ? firstUv.x : firstUv.y)
                    - (splitAlongU_ ? secondUv.x : secondUv.y));
                const double weakDelta = std::abs(
                    (splitAlongU_ ? firstUv.y : firstUv.x)
                    - (splitAlongU_ ? secondUv.y : secondUv.x));
                if (strongDelta > 1.0e-10 && weakDelta > 1.0e-10) {
                    continue;
                }
                const auto& adjacent = edgeTriangles_.at(edge);
                if (adjacent.size() != 2
                    || trianglePiece_[static_cast<std::size_t>(adjacent[0])] != pieceIndex
                    || trianglePiece_[static_cast<std::size_t>(adjacent[1])] != pieceIndex) {
                    continue;
                }
                const auto normal = [&](int index) {
                    const auto& triangle = triangles_[static_cast<std::size_t>(index)].vertices;
                    return geometry::Cross(
                        spatial_[triangle[1]] - spatial_[triangle[0]],
                        spatial_[triangle[2]] - spatial_[triangle[0]]).Normalized();
                };
                if (NormalAngleDegrees(normal(adjacent[0]), normal(adjacent[1])) + 1.0e-9
                    < options.minimumFoldAngleDegrees) {
                    continue;
                }
                piece.foldLines.push_back({
                    "facet_fold_" + std::to_string(pieceIndex + 1) + "_" + std::to_string(piece.foldLines.size() + 1),
                    {flat.at(edge.first), flat.at(edge.second)},
                });
            }
        }
    }

    static void TranslatePath(PlateFlatPatternPath& path, Vector2 offset)
    {
        for (Vector2& point : path.points) {
            point = point + offset;
        }
    }

    static void TranslatePiece(PlateFlatPatternPiece& piece, Vector2 offset)
    {
        TranslatePath(piece.outerBoundary, offset);
        for (PlateFlatPatternPath& path : piece.foldLines) {
            TranslatePath(path, offset);
        }
    }

    [[nodiscard]] std::pair<int, std::vector<Vector2>> MapPath(const NamedUvPath& path) const
    {
        int pieceIndex = -1;
        std::vector<Vector2> flatPoints;
        flatPoints.reserve(path.points.size());
        for (const Vector2 uv : path.points) {
            const auto [mappedPiece, point] = Evaluate(uv);
            if (pieceIndex < 0) {
                pieceIndex = mappedPiece;
            } else if (pieceIndex != mappedPiece) {
                return {-1, {}};
            }
            if (flatPoints.empty() || !AlmostSame(flatPoints.back(), point, 1.0e-7)) {
                flatPoints.push_back(point);
            }
        }
        return {pieceIndex, std::move(flatPoints)};
    }

    [[nodiscard]] std::pair<int, Vector2> EvaluateInCell(
        Vector2 uv,
        int strip,
        int requestedWeakIndex) const
    {
        strip = std::clamp(strip, 0, static_cast<int>(stripParameters_.size()) - 2);
        const int weakIndex = std::clamp(
            requestedWeakIndex, 0, weakSegments_ - 1);
        const int cellIndex = strip * weakSegments_ + weakIndex;
        const double minimumU = splitAlongU_ ? stripParameters_[static_cast<std::size_t>(strip)]
                                             : static_cast<double>(weakIndex) / weakSegments_;
        const double maximumU = splitAlongU_ ? stripParameters_[static_cast<std::size_t>(strip + 1)]
                                             : static_cast<double>(weakIndex + 1) / weakSegments_;
        const double minimumV = splitAlongU_ ? static_cast<double>(weakIndex) / weakSegments_
                                             : stripParameters_[static_cast<std::size_t>(strip)];
        const double maximumV = splitAlongU_ ? static_cast<double>(weakIndex + 1) / weakSegments_
                                             : stripParameters_[static_cast<std::size_t>(strip + 1)];
        const double localU = (uv.x - minimumU) / (maximumU - minimumU);
        const double localV = (uv.y - minimumV) / (maximumV - minimumV);
        const int triangleIndex = cellIndex * 2 + (localV <= localU ? 0 : 1);
        const auto& triangle = triangles_[static_cast<std::size_t>(triangleIndex)].vertices;
        const auto& flat = triangleFlat_[static_cast<std::size_t>(triangleIndex)];
        const Vector2 a = uv_[static_cast<std::size_t>(triangle[0])];
        const Vector2 b = uv_[static_cast<std::size_t>(triangle[1])];
        const Vector2 c = uv_[static_cast<std::size_t>(triangle[2])];
        const double denominator = Cross2(b - a, c - a);
        const double weightB = Cross2(uv - a, c - a) / denominator;
        const double weightC = Cross2(b - a, uv - a) / denominator;
        const double weightA = 1.0 - weightB - weightC;
        const int pieceIndex = trianglePiece_[static_cast<std::size_t>(triangleIndex)];
        return {
            pieceIndex,
            flat[0] * weightA + flat[1] * weightB + flat[2] * weightC
                + pieceOffsets_[static_cast<std::size_t>(pieceIndex)],
        };
    }

    [[nodiscard]] std::pair<int, Vector2> EvaluateInStrip(Vector2 uv, int strip) const
    {
        const double weak = splitAlongU_ ? uv.y : uv.x;
        const int weakIndex = std::min(
            static_cast<int>(std::clamp(weak, 0.0, 1.0) * weakSegments_),
            weakSegments_ - 1);
        return EvaluateInCell(uv, strip, weakIndex);
    }

    [[nodiscard]] std::pair<int, Vector2> Evaluate(Vector2 uv) const
    {
        const double strong = splitAlongU_ ? uv.x : uv.y;
        const auto upper = std::upper_bound(
            stripParameters_.begin(), stripParameters_.end(), strong + 1.0e-12);
        const int strip = std::clamp(
            static_cast<int>(std::distance(stripParameters_.begin(), upper)) - 1,
            0,
            static_cast<int>(stripParameters_.size()) - 2);
        return EvaluateInStrip(uv, strip);
    }

    const Plate& plate_;
    bool splitAlongU_ = true;
    std::vector<double> stripParameters_;
    int weakSegments_ = 2;
    int openingRefinementDepth_ = 4;
    bool splitAcrossWeak_ = false;
    std::vector<Vector2> uv_;
    std::vector<Vector3> spatial_;
    std::vector<Triangle> triangles_;
    std::map<std::pair<int, int>, std::vector<int>> edgeTriangles_;
    std::set<std::pair<int, int>> cutEdges_;
    std::vector<int> trianglePiece_;
    std::vector<std::array<Vector2, 3>> triangleFlat_;
    std::vector<Vector2> pieceOffsets_;
    std::vector<PlateFlatPatternPiece> pieces_;
    double maximumEdgeDistortion_ = 0.0;
    double rmsEdgeDistortion_ = 0.0;
};

void AddGeneratedFoldAndReliefPaths(
    PlateFlatPattern& pattern,
    const Project& project,
    const NamedPlate& namedPlate,
    const Plate& plate,
    const DevelopmentGrid& grid,
    const PlateFlatPatternOptions& options)
{
    const std::vector<double> uParameters = FoldParameters(plate, true, options);
    const std::vector<double> vParameters = FoldParameters(plate, false, options);
    if (options.includeFoldLines) {
        for (std::size_t index = 0; index < uParameters.size(); ++index) {
            pattern.foldLines.push_back({
                "fold_u_" + std::to_string(index + 1),
                grid.ConstantU(uParameters[index]),
            });
        }
        for (std::size_t index = 0; index < vParameters.size(); ++index) {
            pattern.foldLines.push_back({
                "fold_v_" + std::to_string(index + 1),
                grid.ConstantV(vParameters[index]),
            });
        }
    }

    if (!options.includeAutomaticReliefCuts
        || !options.allowAutomaticNotches
        || pattern.analysis.classification != PlateDevelopability::DoubleCurved
        || options.assemblyStrategy == PlateAssemblyStrategy::SplitPieces) {
        return;
    }
    const bool strongIsU = TotalNormalChangeDegrees(plate, true)
        >= TotalNormalChangeDegrees(plate, false);
    std::vector<NamedUvPath> protectedPaths;
    for (const std::string& openingName : namedPlate.openingWireNames) {
        protectedPaths.push_back({openingName, BuildPlateWireUvPath(
            project, namedPlate, openingName, options.openingSamples, true)});
    }
    for (const std::string& reliefName : namedPlate.reliefCutWireNames) {
        protectedPaths.push_back({reliefName, BuildPlateWireUvPath(
            project, namedPlate, reliefName, options.openingSamples, false)});
    }
    const std::vector<AutomaticNotchSpec> notches = BuildAutomaticNotchSpecs(
        plate, strongIsU, options, protectedPaths);
    std::vector<PlateFlatPatternPath> generatedNotches;
    pattern.outerBoundary.points = BuildNotchedBoundary(
        grid, strongIsU, notches, options, generatedNotches);
    pattern.analysis.automaticNotchCount = static_cast<int>(generatedNotches.size());
    pattern.reliefCuts.insert(
        pattern.reliefCuts.end(), generatedNotches.begin(), generatedNotches.end());
}

void ValidatePattern(const PlateFlatPattern& pattern)
{
    const auto validatePath = [](const PlateFlatPatternPath& path, bool outer) {
        if (path.points.size() < 4 || !AlmostSame(path.points.front(), path.points.back())) {
            throw std::invalid_argument(outer
                    ? "Plate flat pattern has no usable closed outer boundary."
                    : "Plate flat pattern contains an unusable opening boundary.");
        }
        for (const Vector2 point : path.points) {
            if (!point.IsFinite()) {
                throw std::invalid_argument("Plate flat pattern contains invalid coordinates.");
            }
        }
        const std::size_t edgeCount = path.points.size() - 1;
        for (std::size_t edge = 0; edge < edgeCount; ++edge) {
            if (Length(path.points[edge + 1] - path.points[edge]) <= 1.0e-8) {
                throw std::invalid_argument("Plate flat pattern contains a collapsed cutting edge.");
            }
            for (std::size_t other = edge + 1; other < edgeCount; ++other) {
                if (other == edge + 1
                    || (edge == 0 && other + 1 == edgeCount)) {
                    continue;
                }
                if (SegmentsIntersect(
                        path.points[edge], path.points[edge + 1],
                        path.points[other], path.points[other + 1])) {
                    throw std::invalid_argument(
                        "Plate flat pattern contains a self-intersecting cutting boundary.");
                }
            }
        }
    };
    validatePath(pattern.outerBoundary, true);
    for (const PlateFlatPatternPiece& piece : pattern.pieces) {
        validatePath(piece.outerBoundary, true);
    }
    for (const auto& opening : pattern.openings) {
        validatePath(opening, false);
    }
    const auto validateOpenPath = [](const PlateFlatPatternPath& path) {
        if (path.points.size() < 2) {
            throw std::invalid_argument("Plate flat pattern contains an unusable open path.");
        }
        for (const Vector2 point : path.points) {
            if (!point.IsFinite()) {
                throw std::invalid_argument("Plate flat pattern contains invalid coordinates.");
            }
        }
    };
    for (const auto& fold : pattern.foldLines) {
        validateOpenPath(fold);
    }
    for (const auto& cut : pattern.reliefCuts) {
        validateOpenPath(cut);
    }
}

void WriteDxfPair(std::ostream& output, int code, std::string_view value)
{
    output << code << '\n' << value << '\n';
}

void WriteDxfPath(std::ostream& output, const PlateFlatPatternPath& path, std::string_view layer)
{
    WriteDxfPair(output, 0, "POLYLINE");
    WriteDxfPair(output, 8, layer);
    WriteDxfPair(output, 66, "1");
    const bool closed = path.points.size() > 1 && AlmostSame(path.points.front(), path.points.back());
    WriteDxfPair(output, 70, closed ? "1" : "0");
    const std::size_t count = closed
        ? path.points.size() - 1
        : path.points.size();
    for (std::size_t index = 0; index < count; ++index) {
        WriteDxfPair(output, 0, "VERTEX");
        WriteDxfPair(output, 8, layer);
        output << "10\n" << path.points[index].x
               << "\n20\n" << path.points[index].y
               << "\n30\n0.000000000\n";
    }
    WriteDxfPair(output, 0, "SEQEND");
}

} // namespace

double PlateFlatPatternAnalysis::MaximumEstimatedErrorMillimeters() const noexcept
{
    return std::max(maximumEdgeDistortionMillimeters, maximumBoundaryApproximationMillimeters);
}

PlateFlatPattern BuildPlateFlatPattern(
    const Project& project,
    const NamedPlate& namedPlate,
    PlateFlatPatternOptions options)
{
    ValidateOptions(options);
    PlateFlatPattern pattern;
    pattern.plateName = namedPlate.name;
    pattern.outerBoundary.name = namedPlate.name;
    const Plate& plate = namedPlate.plate;
    pattern.analysis.classification = plate.AnalyzeDevelopability().classification;

    const bool automaticPapercraft = options.includeAutomaticReliefCuts
        && pattern.analysis.classification == PlateDevelopability::DoubleCurved;
    const bool papercraftMode = automaticPapercraft || !namedPlate.splitWireNames.empty();
    if (papercraftMode) {
        PlateFlatPatternOptions papercraftOptions = options;
        papercraftOptions.includeAutomaticReliefCuts = automaticPapercraft;
        std::vector<NamedUvPath> openingPaths;
        std::vector<NamedUvPath> reliefPaths;
        std::vector<NamedUvPath> splitPaths;
        std::vector<NamedUvPath> notchProtectedPaths;
        for (const std::string& openingName : namedPlate.openingWireNames) {
            NamedUvPath openingPath{
                openingName,
                BuildPlateWireUvPath(project, namedPlate, openingName, options.openingSamples, true),
            };
            notchProtectedPaths.push_back(openingPath);
            if (options.includeOpenings) {
                openingPaths.push_back({
                    openingPath.name,
                    std::move(openingPath.points),
                });
            }
        }
        for (const std::string& cutName : namedPlate.reliefCutWireNames) {
            reliefPaths.push_back({
                cutName,
                BuildPlateWireUvPath(project, namedPlate, cutName, options.openingSamples, false),
            });
        }
        for (const std::string& splitName : namedPlate.splitWireNames) {
            splitPaths.push_back({
                splitName,
                BuildPlateWireUvPath(project, namedPlate, splitName, options.openingSamples, false),
            });
        }
        notchProtectedPaths.insert(
            notchProtectedPaths.end(), reliefPaths.begin(), reliefPaths.end());
        const bool splitAlongU
            = options.cutDirection != PapercraftCutDirection::Horizontal;
        const bool splitAcrossWeak = automaticPapercraft
            && options.cutDirection == PapercraftCutDirection::Both;
        std::vector<double> stripParameters = PapercraftStripParameters(
            plate, splitAlongU, papercraftOptions, reliefPaths);
        const Vector3 strongStart = splitAlongU
            ? plate.Evaluate(0.0, 0.5, 0.5)
            : plate.Evaluate(0.5, 0.0, 0.5);
        const Vector3 strongEnd = splitAlongU
            ? plate.Evaluate(1.0, 0.5, 0.5)
            : plate.Evaluate(0.5, 1.0, 0.5);
        if (stripParameters.size() == 2
            && (strongStart - strongEnd).Length() <= 1.0e-7) {
            const auto crossesProtectedPath = [&](double candidate) {
                return std::any_of(
                    reliefPaths.begin(), reliefPaths.end(), [&](const NamedUvPath& path) {
                    if (path.points.empty()) {
                        return false;
                    }
                    double minimum = 1.0;
                    double maximum = 0.0;
                    for (const Vector2 point : path.points) {
                        const double coordinate = splitAlongU ? point.x : point.y;
                        minimum = std::min(minimum, coordinate);
                        maximum = std::max(maximum, coordinate);
                    }
                    return candidate > minimum + 1.0e-5
                        && candidate < maximum - 1.0e-5;
                });
            };
            for (const NamedUvPath& split : splitPaths) {
                double minimumStrong = 1.0;
                double maximumStrong = 0.0;
                double minimumWeak = 1.0;
                double maximumWeak = 0.0;
                for (const Vector2 point : split.points) {
                    const double strong = splitAlongU ? point.x : point.y;
                    const double weak = splitAlongU ? point.y : point.x;
                    minimumStrong = std::min(minimumStrong, strong);
                    maximumStrong = std::max(maximumStrong, strong);
                    minimumWeak = std::min(minimumWeak, weak);
                    maximumWeak = std::max(maximumWeak, weak);
                }
                const double candidate = (minimumStrong + maximumStrong) * 0.5;
                if (maximumStrong - minimumStrong <= 1.0e-3
                    && minimumWeak <= 1.0e-3 && maximumWeak >= 1.0 - 1.0e-3
                    && candidate > 1.0e-3 && candidate < 1.0 - 1.0e-3
                    && !crossesProtectedPath(candidate)) {
                    stripParameters.insert(stripParameters.begin() + 1, candidate);
                    break;
                }
            }
            for (const double candidate : {0.5, 0.25, 0.75, 0.125, 0.875}) {
                if (stripParameters.size() > 2) {
                    break;
                }
                if (!crossesProtectedPath(candidate)) {
                    stripParameters.insert(stripParameters.begin() + 1, candidate);
                }
            }
            if (stripParameters.size() == 2) {
                throw std::invalid_argument(
                    "No safe seam remains between openings on this closed surface.");
            }
        }
        const double weakLength = SpatialPathLength(plate, !splitAlongU, 0.5);
        const int weakSegments = std::clamp(
            static_cast<int>(std::ceil(
                weakLength / FidelityTargetSpacing(papercraftOptions.papercraftFidelity))),
            2,
            128);
        PapercraftGrid grid(
            plate,
            splitAlongU,
            stripParameters,
            weakSegments,
            splitAcrossWeak,
            splitPaths,
            papercraftOptions);
        for (const NamedUvPath& opening : openingPaths) {
            grid.AddOpeningPath(opening);
        }
        for (const NamedUvPath& relief : reliefPaths) {
            grid.AddReliefPath(relief);
        }
        if (options.includeAutomaticReliefCuts
            && options.allowAutomaticNotches
            && pattern.analysis.classification == PlateDevelopability::DoubleCurved) {
            const std::vector<AutomaticStripNotchSpec> notches
                = BuildAutomaticStripNotchSpecs(
                    plate,
                    splitAlongU,
                    stripParameters,
                    options,
                    notchProtectedPaths);
            pattern.analysis.automaticNotchCount = grid.AddAutomaticNotches(notches, options);
        }
        pattern.pieces = grid.Pieces();
        pattern.analysis.maximumEdgeDistortionMillimeters = grid.MaximumEdgeDistortion();
        pattern.analysis.rootMeanSquareEdgeDistortionMillimeters = grid.RootMeanSquareEdgeDistortion();
        pattern.analysis.maximumBoundaryApproximationMillimeters
            = FidelityTargetSpacing(papercraftOptions.papercraftFidelity) * 0.005;
        pattern.analysis.pieceCount = static_cast<int>(pattern.pieces.size());
        pattern.outerBoundary = pattern.pieces.front().outerBoundary;
        for (const PlateFlatPatternPiece& piece : pattern.pieces) {
            pattern.openings.insert(pattern.openings.end(), piece.openings.begin(), piece.openings.end());
            pattern.foldLines.insert(pattern.foldLines.end(), piece.foldLines.begin(), piece.foldLines.end());
            pattern.reliefCuts.insert(pattern.reliefCuts.end(), piece.reliefCuts.begin(), piece.reliefCuts.end());
        }
    } else if (plate.SourceSurface().Kind() == SurfaceKind::Planar) {
        pattern.outerBoundary.points = BuildPlanarBoundary(namedPlate, options.openingSamples);
        if (options.includeOpenings) {
            for (const std::string& openingName : namedPlate.openingWireNames) {
                pattern.openings.push_back({
                    openingName,
                    BuildPlanarWirePath(project, openingName, plate.SourceSurface(), options.openingSamples, true),
                });
            }
        }
        for (const std::string& cutName : namedPlate.reliefCutWireNames) {
            pattern.reliefCuts.push_back({
                cutName,
                BuildPlanarWirePath(project, cutName, plate.SourceSurface(), options.openingSamples, false),
            });
        }
    } else {
        const DevelopmentGrid grid(plate, options.uSegments, options.vSegments);
        pattern.outerBoundary.points = grid.Boundary();
        pattern.analysis.maximumEdgeDistortionMillimeters = grid.MaximumEdgeDistortion();
        pattern.analysis.rootMeanSquareEdgeDistortionMillimeters = grid.RootMeanSquareEdgeDistortion();
        pattern.analysis.maximumBoundaryApproximationMillimeters = grid.MaximumBoundaryApproximation();
        if (options.includeOpenings) {
            for (const std::string& openingName : namedPlate.openingWireNames) {
                pattern.openings.push_back({
                    openingName,
                    BuildDevelopedWirePath(project, namedPlate, openingName, grid, options.openingSamples, true),
                });
            }
        }
        for (const std::string& cutName : namedPlate.reliefCutWireNames) {
            pattern.reliefCuts.push_back({
                cutName,
                BuildDevelopedWirePath(project, namedPlate, cutName, grid, options.openingSamples, false),
            });
        }
        AddGeneratedFoldAndReliefPaths(pattern, project, namedPlate, plate, grid, options);
    }
    if (pattern.pieces.empty()) {
        pattern.pieces.push_back({
            namedPlate.name,
            pattern.outerBoundary,
            pattern.openings,
            pattern.foldLines,
            pattern.reliefCuts,
        });
    }
    NormalizeClosedPath(pattern.outerBoundary);
    for (PlateFlatPatternPath& opening : pattern.openings) {
        NormalizeClosedPath(opening);
    }
    for (PlateFlatPatternPath& fold : pattern.foldLines) {
        NormalizeOpenPath(fold);
    }
    for (PlateFlatPatternPath& cut : pattern.reliefCuts) {
        NormalizeOpenPath(cut);
    }
    for (PlateFlatPatternPiece& piece : pattern.pieces) {
        NormalizeClosedPath(piece.outerBoundary);
        for (PlateFlatPatternPath& opening : piece.openings) {
            NormalizeClosedPath(opening);
        }
        for (PlateFlatPatternPath& fold : piece.foldLines) {
            NormalizeOpenPath(fold);
        }
        for (PlateFlatPatternPath& cut : piece.reliefCuts) {
            NormalizeOpenPath(cut);
        }
    }
    pattern.analysis.pieceCount = static_cast<int>(pattern.pieces.size());
    ValidatePattern(pattern);
    return pattern;
}

PlateAssemblyGuide BuildPlateAssemblyGuide(
    const Project& project,
    const NamedPlate& namedPlate,
    PlateFlatPatternOptions options)
{
    ValidateOptions(options);
    PlateAssemblyGuide guide;
    guide.plateName = namedPlate.name;
    const Plate& plate = namedPlate.plate;

    for (const std::string& cutName : namedPlate.reliefCutWireNames) {
        guide.reliefCuts.push_back({
            cutName,
            BuildAssemblyWirePath(
                project, namedPlate, cutName, options.openingSamples),
        });
    }
    for (const std::string& splitName : namedPlate.splitWireNames) {
        guide.splitLines.push_back({
            splitName,
            BuildAssemblyWirePath(project, namedPlate, splitName, options.openingSamples),
        });
    }
    if (plate.SourceSurface().Kind() == SurfaceKind::Planar) {
        return guide;
    }

    const std::vector<double> uParameters = FoldParameters(plate, true, options);
    const std::vector<double> vParameters = FoldParameters(plate, false, options);
    if (options.includeFoldLines) {
        for (std::size_t index = 0; index < uParameters.size(); ++index) {
            guide.foldLines.push_back({
                "fold_u_" + std::to_string(index + 1),
                BuildConstantAssemblyPath(
                    plate, true, uParameters[index], 0.0, 1.0, options.vSegments),
            });
        }
        for (std::size_t index = 0; index < vParameters.size(); ++index) {
            guide.foldLines.push_back({
                "fold_v_" + std::to_string(index + 1),
                BuildConstantAssemblyPath(
                    plate, false, vParameters[index], 0.0, 1.0, options.uSegments),
            });
        }
    }

    if (!options.includeAutomaticReliefCuts
        || plate.AnalyzeDevelopability().classification != PlateDevelopability::DoubleCurved) {
        return guide;
    }
    const bool cutAtConstantU
        = options.cutDirection != PapercraftCutDirection::Horizontal;
    std::vector<NamedUvPath> notchProtectedPaths;
    std::vector<NamedUvPath> seamProtectedPaths;
    for (const std::string& openingName : namedPlate.openingWireNames) {
        notchProtectedPaths.push_back({
            openingName,
            BuildPlateWireUvPath(project, namedPlate, openingName, options.openingSamples, true),
        });
    }
    for (const std::string& reliefName : namedPlate.reliefCutWireNames) {
        NamedUvPath relief{
            reliefName,
            BuildPlateWireUvPath(project, namedPlate, reliefName, options.openingSamples, false),
        };
        notchProtectedPaths.push_back(relief);
        seamProtectedPaths.push_back(std::move(relief));
    }
    PlateFlatPatternOptions stripOptions = options;
    stripOptions.includeAutomaticReliefCuts = true;
    const std::vector<double> parameters = PapercraftStripParameters(
        plate, cutAtConstantU, stripOptions, seamProtectedPaths);
    for (std::size_t index = 1; index + 1 < parameters.size(); ++index) {
        guide.splitLines.push_back({
            "papercraft_split_primary_" + std::to_string(index),
            BuildConstantAssemblyPath(
                plate,
                cutAtConstantU,
                parameters[index],
                0.0,
                1.0,
                cutAtConstantU ? options.vSegments : options.uSegments),
        });
    }
    if (options.cutDirection == PapercraftCutDirection::Both) {
        const std::vector<double> secondaryParameters = PapercraftStripParameters(
            plate, false, stripOptions, seamProtectedPaths);
        for (std::size_t index = 1; index + 1 < secondaryParameters.size(); ++index) {
            guide.splitLines.push_back({
                "papercraft_split_secondary_" + std::to_string(index),
                BuildConstantAssemblyPath(
                    plate,
                    false,
                    secondaryParameters[index],
                    0.0,
                    1.0,
                    options.uSegments),
            });
        }
    }
    if (options.allowAutomaticNotches) {
        const std::vector<AutomaticStripNotchSpec> notches
            = BuildAutomaticStripNotchSpecs(
                plate, cutAtConstantU, parameters, options, notchProtectedPaths);
        for (std::size_t index = 0; index < notches.size(); ++index) {
            const std::vector<Vector2> uv = BuildStripNotchUvPath(
                notches[index], cutAtConstantU, parameters);
            PlateAssemblyGuidePath path;
            path.name = options.notchStyle == ReliefNotchStyle::CurvedV
                ? "piece_curved_v_notch_" + std::to_string(index + 1)
                : "piece_v_notch_" + std::to_string(index + 1);
            constexpr int samplesPerArm = 8;
            for (std::size_t arm = 0; arm + 1 < uv.size(); ++arm) {
                for (int sample = arm == 0 ? 0 : 1; sample <= samplesPerArm; ++sample) {
                    const double fraction = static_cast<double>(sample) / samplesPerArm;
                    const Vector2 point = uv[arm] * (1.0 - fraction) + uv[arm + 1] * fraction;
                    path.points.push_back(plate.Evaluate(point.x, point.y, 1.0));
                }
            }
            guide.reliefCuts.push_back(std::move(path));
        }
    }
    return guide;
}

PlateAssemblyApproximation BuildPlateAssemblyApproximation(
    const Project& project,
    const NamedPlate& namedPlate,
    PlateFlatPatternOptions options)
{
    ValidateOptions(options);
    PlateAssemblyApproximation approximation;
    approximation.plateName = namedPlate.name;
    approximation.guide = BuildPlateAssemblyGuide(project, namedPlate, options);
    const Plate& plate = namedPlate.plate;
    const PlateDevelopability classification
        = plate.AnalyzeDevelopability().classification;
    approximation.pieceCount = std::max(
        1, 1 + static_cast<int>(namedPlate.splitWireNames.size()));

    const auto uniformParameters = [](int intervalCount) {
        std::vector<double> parameters;
        parameters.reserve(static_cast<std::size_t>(intervalCount) + 1);
        for (int interval = 0; interval <= intervalCount; ++interval) {
            parameters.push_back(static_cast<double>(interval) / intervalCount);
        }
        return parameters;
    };
    const double targetSpacing = FidelityTargetSpacing(options.papercraftFidelity);
    int uIntervals = std::clamp(
        static_cast<int>(std::ceil(SpatialPathLength(plate, true, 0.5) / targetSpacing)),
        1,
        96);
    int vIntervals = std::clamp(
        static_cast<int>(std::ceil(SpatialPathLength(plate, false, 0.5) / targetSpacing)),
        1,
        96);
    if (classification == PlateDevelopability::Planar) {
        uIntervals = 1;
        vIntervals = 1;
    }
    std::vector<double> uParameters = uniformParameters(uIntervals);
    std::vector<double> vParameters = uniformParameters(vIntervals);
    const bool strongIsU
        = options.cutDirection != PapercraftCutDirection::Horizontal;
    if (options.includeAutomaticReliefCuts
        && classification == PlateDevelopability::DoubleCurved) {
        std::vector<NamedUvPath> seamProtectedPaths;
        for (const std::string& reliefName : namedPlate.reliefCutWireNames) {
            seamProtectedPaths.push_back({
                reliefName,
                BuildPlateWireUvPath(
                    project, namedPlate, reliefName, options.openingSamples, false),
            });
        }
        PlateFlatPatternOptions stripOptions = options;
        stripOptions.includeAutomaticReliefCuts = true;
        std::vector<double> strongParameters = PapercraftStripParameters(
            plate, strongIsU, stripOptions, seamProtectedPaths);
        approximation.pieceCount = std::max(
            approximation.pieceCount,
            static_cast<int>(strongParameters.size()) - 1
                + static_cast<int>(namedPlate.splitWireNames.size()));
        if (strongIsU) {
            uParameters = std::move(strongParameters);
        } else {
            vParameters = std::move(strongParameters);
        }
        if (options.cutDirection == PapercraftCutDirection::Both) {
            std::vector<double> secondaryParameters = PapercraftStripParameters(
                plate, false, stripOptions, seamProtectedPaths);
            vParameters = std::move(secondaryParameters);
            approximation.pieceCount = std::max(
                approximation.pieceCount,
                static_cast<int>((uParameters.size() - 1) * (vParameters.size() - 1))
                    + static_cast<int>(namedPlate.splitWireNames.size()));
        }
    }

    std::vector<NamedUvPath> openings;
    for (const std::string& openingName : namedPlate.openingWireNames) {
        openings.push_back({
            openingName,
            BuildPlateWireUvPath(
                project, namedPlate, openingName, options.openingSamples, true),
        });
    }
    const auto pointInsidePath = [](Vector2 point, const NamedUvPath& path) {
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
    };
    const auto insideOpening = [&](Vector2 point) {
        return std::any_of(openings.begin(), openings.end(),
            [&](const NamedUvPath& opening) {
                return opening.points.size() >= 3 && pointInsidePath(point, opening);
            });
    };
    const auto panelDeviation = [&](const std::array<Vector2, 3>& uv,
                                    const std::array<Vector3, 3>& points) {
        const Vector3 normal = geometry::Cross(
            points[1] - points[0], points[2] - points[0]).Normalized();
        const std::array<std::array<double, 3>, 4> samples{{
            {{0.5, 0.5, 0.0}},
            {{0.0, 0.5, 0.5}},
            {{0.5, 0.0, 0.5}},
            {{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}},
        }};
        double maximum = 0.0;
        for (const auto& weights : samples) {
            const Vector2 parameter = uv[0] * weights[0]
                + uv[1] * weights[1] + uv[2] * weights[2];
            const Vector3 source = plate.Evaluate(parameter.x, parameter.y, 0.5);
            maximum = std::max(maximum, std::abs(geometry::Dot(source - points[0], normal)));
        }
        return maximum;
    };

    double squaredDeviation = 0.0;
    std::size_t measuredPanels = 0;
    for (std::size_t uIndex = 0; uIndex + 1 < uParameters.size(); ++uIndex) {
        for (std::size_t vIndex = 0; vIndex + 1 < vParameters.size(); ++vIndex) {
            const double u0 = uParameters[uIndex];
            const double u1 = uParameters[uIndex + 1];
            const double v0 = vParameters[vIndex];
            const double v1 = vParameters[vIndex + 1];
            const std::array<std::array<Vector2, 3>, 2> triangleUv{{
                {{{u0, v0}, {u1, v0}, {u1, v1}}},
                {{{u0, v0}, {u1, v1}, {u0, v1}}},
            }};
            for (const auto& uv : triangleUv) {
                const Vector2 center = (uv[0] + uv[1] + uv[2]) * (1.0 / 3.0);
                if (insideOpening(center)) {
                    continue;
                }
                PlateAssemblyApproximationPanel panel;
                for (std::size_t point = 0; point < uv.size(); ++point) {
                    panel.points[point] = plate.Evaluate(uv[point].x, uv[point].y, 0.5);
                }
                panel.pieceIndex = options.cutDirection == PapercraftCutDirection::Both
                    ? static_cast<int>(vIndex * (uParameters.size() - 1) + uIndex)
                    : (strongIsU
                        ? static_cast<int>(uIndex)
                        : static_cast<int>(vIndex));
                panel.maximumDeviationMillimeters = panelDeviation(uv, panel.points);
                approximation.maximumDeviationMillimeters = std::max(
                    approximation.maximumDeviationMillimeters,
                    panel.maximumDeviationMillimeters);
                squaredDeviation += panel.maximumDeviationMillimeters
                    * panel.maximumDeviationMillimeters;
                ++measuredPanels;
                approximation.panels.push_back(std::move(panel));
            }
        }
    }
    if (measuredPanels > 0) {
        approximation.rootMeanSquareDeviationMillimeters = std::sqrt(
            squaredDeviation / static_cast<double>(measuredPanels));
    }
    return approximation;
}

PlateAssemblyMotion BuildPlateAssemblyMotion(
    const Project& project,
    const NamedPlate& namedPlate,
    double progress,
    PlateFlatPatternOptions options)
{
    ValidateOptions(options);
    if (!std::isfinite(progress)) {
        throw std::invalid_argument("Plate assembly progress must be finite.");
    }

    const Plate& plate = namedPlate.plate;
    const bool splitAlongU
        = options.cutDirection != PapercraftCutDirection::Horizontal;
    std::vector<NamedUvPath> splitPaths;
    std::vector<NamedUvPath> protectedPaths;
    for (const std::string& splitName : namedPlate.splitWireNames) {
        splitPaths.push_back({
            splitName,
            BuildPlateWireUvPath(
                project, namedPlate, splitName, options.openingSamples, false),
        });
    }
    for (const std::string& reliefName : namedPlate.reliefCutWireNames) {
        protectedPaths.push_back({
            reliefName,
            BuildPlateWireUvPath(
                project, namedPlate, reliefName, options.openingSamples, false),
        });
    }
    std::vector<NamedUvPath> openings;
    if (options.includeOpenings) {
        for (const std::string& openingName : namedPlate.openingWireNames) {
            openings.push_back({
                openingName,
                BuildPlateWireUvPath(
                    project, namedPlate, openingName, options.openingSamples, true),
            });
        }
    }

    PlateFlatPatternOptions motionOptions = options;
    motionOptions.includeAutomaticReliefCuts
        = options.includeAutomaticReliefCuts
        && plate.AnalyzeDevelopability().classification == PlateDevelopability::DoubleCurved;
    std::vector<double> stripParameters = PapercraftStripParameters(
        plate, splitAlongU, motionOptions, protectedPaths);
    const double weakLength = SpatialPathLength(plate, !splitAlongU, 0.5);
    const int weakSegments = std::clamp(
        static_cast<int>(std::ceil(
            weakLength / FidelityTargetSpacing(options.papercraftFidelity))),
        2,
        128);
    PapercraftGrid grid(
        plate,
        splitAlongU,
        std::move(stripParameters),
        weakSegments,
        motionOptions.includeAutomaticReliefCuts
            && options.cutDirection == PapercraftCutDirection::Both,
        splitPaths,
        motionOptions);
    return grid.BuildAssemblyMotion(namedPlate.name, progress, openings);
}

void WritePlateFlatPatternSvg(
    std::ostream& output,
    const PlateFlatPattern& pattern,
    PlateFlatPatternOptions options)
{
    ValidateOptions(options);
    ValidatePattern(pattern);
    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    const auto includePath = [&](const PlateFlatPatternPath& path) {
        for (const Vector2 point : path.points) {
            minimumX = std::min(minimumX, point.x);
            minimumY = std::min(minimumY, point.y);
            maximumX = std::max(maximumX, point.x);
            maximumY = std::max(maximumY, point.y);
        }
    };
    if (pattern.pieces.empty()) {
        includePath(pattern.outerBoundary);
    } else {
        for (const PlateFlatPatternPiece& piece : pattern.pieces) {
            includePath(piece.outerBoundary);
        }
    }
    for (const auto& opening : pattern.openings) {
        if (!opening.incorporatedInOuterBoundary) {
            includePath(opening);
        }
    }
    for (const auto& fold : pattern.foldLines) {
        includePath(fold);
    }
    for (const auto& cut : pattern.reliefCuts) {
        includePath(cut);
    }
    std::vector<std::pair<Vector2, std::string>> pieceLabels;
    pieceLabels.reserve(pattern.pieces.size());
    for (std::size_t pieceIndex = 0; pieceIndex < pattern.pieces.size(); ++pieceIndex) {
        const PlateFlatPatternPiece& piece = pattern.pieces[pieceIndex];
        double pieceMinimumX = std::numeric_limits<double>::infinity();
        double pieceMaximumX = -std::numeric_limits<double>::infinity();
        double pieceMinimumY = std::numeric_limits<double>::infinity();
        for (const Vector2 point : piece.outerBoundary.points) {
            pieceMinimumX = std::min(pieceMinimumX, point.x);
            pieceMaximumX = std::max(pieceMaximumX, point.x);
            pieceMinimumY = std::min(pieceMinimumY, point.y);
        }
        const Vector2 labelPosition{
            (pieceMinimumX + pieceMaximumX) * 0.5,
            pieceMinimumY - 2.5,
        };
        minimumX = std::min(minimumX, labelPosition.x - 2.0);
        maximumX = std::max(maximumX, labelPosition.x + 2.0);
        minimumY = std::min(minimumY, labelPosition.y - 1.2);
        pieceLabels.push_back({labelPosition, "P" + std::to_string(pieceIndex + 1)});
    }
    const double width = maximumX - minimumX + options.marginMillimeters * 2.0;
    const double height = maximumY - minimumY + options.marginMillimeters * 2.0;
    const auto writePath = [&](const PlateFlatPatternPath& path, std::string_view indent) {
        output << indent << "<polyline points=\"";
        for (std::size_t index = 0; index < path.points.size(); ++index) {
            if (index > 0) {
                output << ' ';
            }
            output << path.points[index].x - minimumX + options.marginMillimeters << ','
                   << maximumY - path.points[index].y + options.marginMillimeters;
        }
        output << "\"/>\n";
    };

    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6);
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
           << "mm\" height=\"" << height << "mm\" viewBox=\"0 0 " << width << ' ' << height << "\">\n"
           << "  <g id=\"CUT_OUTER\" fill=\"none\" stroke=\"#000000\" stroke-width=\"0.1\" stroke-linejoin=\"round\">\n";
    if (pattern.pieces.empty()) {
        writePath(pattern.outerBoundary, "    ");
    } else {
        for (const PlateFlatPatternPiece& piece : pattern.pieces) {
            writePath(piece.outerBoundary, "    ");
        }
    }
    output << "  </g>\n"
           << "  <g id=\"CUT_OPENING\" fill=\"none\" stroke=\"#d12f3f\" stroke-width=\"0.1\" stroke-linejoin=\"round\">\n";
    for (const auto& opening : pattern.openings) {
        if (!opening.incorporatedInOuterBoundary) {
            writePath(opening, "    ");
        }
    }
    output << "  </g>\n"
           << "  <g id=\"RELIEF_CUT\" fill=\"none\" stroke=\"#d12f3f\" stroke-width=\"0.14\" stroke-linecap=\"round\">\n";
    for (const auto& cut : pattern.reliefCuts) {
        if (!cut.incorporatedInOuterBoundary
            && cut.cutKind != PapercraftCutKind::SeparatingSeam) {
            writePath(cut, "    ");
        }
    }
    output << "  </g>\n"
           << "  <g id=\"SEPARATING_SEAM\" fill=\"none\" stroke=\"#a65b00\" stroke-width=\"0.14\" stroke-linecap=\"round\">\n";
    for (const auto& cut : pattern.reliefCuts) {
        if (!cut.incorporatedInOuterBoundary
            && cut.cutKind == PapercraftCutKind::SeparatingSeam) {
            writePath(cut, "    ");
        }
    }
    output << "  </g>\n"
           << "  <g id=\"FOLD\" fill=\"none\" stroke=\"#4c5963\" stroke-width=\"0.1\" stroke-dasharray=\"2,1\">\n";
    for (const auto& fold : pattern.foldLines) {
        writePath(fold, "    ");
    }
    output << "  </g>\n"
           << "  <g id=\"ASSEMBLY_LABEL\" fill=\"#26323a\" font-family=\"sans-serif\" font-size=\"2.4\" text-anchor=\"middle\">\n";
    for (const auto& [position, label] : pieceLabels) {
        output << "    <text x=\"" << position.x - minimumX + options.marginMillimeters
               << "\" y=\"" << maximumY - position.y + options.marginMillimeters
               << "\">" << label << "</text>\n";
    }
    output << "  </g>\n</svg>\n";
    if (!output) {
        throw std::runtime_error("Failed to write plate flat-pattern SVG.");
    }
}

void WritePlateFlatPatternDxf(std::ostream& output, const PlateFlatPattern& pattern)
{
    ValidatePattern(pattern);
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(9);
    WriteDxfPair(output, 0, "SECTION");
    WriteDxfPair(output, 2, "HEADER");
    WriteDxfPair(output, 9, "$INSUNITS");
    WriteDxfPair(output, 70, "4");
    WriteDxfPair(output, 0, "ENDSEC");
    WriteDxfPair(output, 0, "SECTION");
    WriteDxfPair(output, 2, "ENTITIES");
    if (pattern.pieces.empty()) {
        WriteDxfPath(output, pattern.outerBoundary, "CUT_OUTER");
    } else {
        for (const PlateFlatPatternPiece& piece : pattern.pieces) {
            WriteDxfPath(output, piece.outerBoundary, "CUT_OUTER");
        }
    }
    for (const auto& opening : pattern.openings) {
        if (!opening.incorporatedInOuterBoundary) {
            WriteDxfPath(output, opening, "CUT_OPENING");
        }
    }
    for (const auto& cut : pattern.reliefCuts) {
        if (!cut.incorporatedInOuterBoundary) {
            WriteDxfPath(
                output,
                cut,
                cut.cutKind == PapercraftCutKind::SeparatingSeam
                    ? "SEPARATING_SEAM"
                    : "RELIEF_CUT");
        }
    }
    for (const auto& fold : pattern.foldLines) {
        WriteDxfPath(output, fold, "FOLD");
    }
    for (std::size_t pieceIndex = 0; pieceIndex < pattern.pieces.size(); ++pieceIndex) {
        const PlateFlatPatternPiece& piece = pattern.pieces[pieceIndex];
        double minimumX = std::numeric_limits<double>::infinity();
        double maximumX = -std::numeric_limits<double>::infinity();
        double minimumY = std::numeric_limits<double>::infinity();
        for (const Vector2 point : piece.outerBoundary.points) {
            minimumX = std::min(minimumX, point.x);
            maximumX = std::max(maximumX, point.x);
            minimumY = std::min(minimumY, point.y);
        }
        WriteDxfPair(output, 0, "TEXT");
        WriteDxfPair(output, 8, "ASSEMBLY_LABEL");
        output << "10\n" << (minimumX + maximumX) * 0.5
               << "\n20\n" << minimumY - 2.5
               << "\n30\n0.000000000\n40\n2.400000000\n1\nP"
               << pieceIndex + 1 << '\n';
    }
    WriteDxfPair(output, 0, "ENDSEC");
    WriteDxfPair(output, 0, "EOF");
    if (!output) {
        throw std::runtime_error("Failed to write plate flat-pattern DXF.");
    }
}

PlateAssemblyModelResult AddPlateAssemblyMotionModel(
    Project& project,
    const NamedPlate& sourcePlate,
    const PlateAssemblyMotion& motion,
    std::string namePrefix,
    std::optional<int> selectedPieceIndex)
{
    if (namePrefix.empty()) {
        throw std::invalid_argument("Assembly-state model name must not be empty.");
    }
    if (motion.panels.empty()
        || motion.panels.size() != motion.pieceIndices.size()
        || motion.panels.size() != motion.panelThicknessMillimeters.size()) {
        throw std::invalid_argument("Assembly-state panels are incomplete.");
    }
    if (selectedPieceIndex.has_value()
        && (*selectedPieceIndex < 0 || *selectedPieceIndex >= motion.pieceCount)) {
        throw std::invalid_argument("Selected assembly piece is outside the available range.");
    }

    PlateAssemblyModelResult result;
    if (motion.preferContinuousModel && !motion.continuousPieces.empty()) {
        for (const PlateAssemblyContinuousPiece& piece : motion.continuousPieces) {
            if (selectedPieceIndex.has_value()
                && piece.pieceIndex != *selectedPieceIndex) {
                continue;
            }
            const auto panelPosition = std::find(
                motion.pieceIndices.begin(), motion.pieceIndices.end(),
                piece.pieceIndex);
            if (panelPosition == motion.pieceIndices.end()) {
                throw std::invalid_argument(
                    "Continuous assembly piece contains no verification panel.");
            }
            const std::size_t panelIndex = static_cast<std::size_t>(
                std::distance(motion.pieceIndices.begin(), panelPosition));
            PlateAssemblyMotion single;
            single.plateName = motion.plateName;
            single.panels.push_back(motion.panels[panelIndex]);
            single.pieceIndices.push_back(0);
            single.panelThicknessMillimeters.push_back(
                motion.panelThicknessMillimeters[panelIndex]);
            single.panelDeviationMillimeters.push_back(
                panelIndex < motion.panelDeviationMillimeters.size()
                    ? motion.panelDeviationMillimeters[panelIndex]
                    : 0.0);
            single.continuousSections = piece.sections;
            single.openingPaths = piece.openingPaths;
            single.reliefCutPaths = piece.reliefCutPaths;
            single.preferContinuousModel = true;
            single.pieceCount = 1;
            const auto added = AddPlateAssemblyMotionModel(
                project,
                sourcePlate,
                single,
                namePrefix + "_piece_" + std::to_string(piece.pieceIndex + 1),
                std::nullopt);
            result.outerWireNames.insert(
                result.outerWireNames.end(),
                added.outerWireNames.begin(), added.outerWireNames.end());
            result.surfaceNames.insert(
                result.surfaceNames.end(),
                added.surfaceNames.begin(), added.surfaceNames.end());
            result.plateNames.insert(
                result.plateNames.end(),
                added.plateNames.begin(), added.plateNames.end());
            result.pieceIndices.insert(
                result.pieceIndices.end(),
                added.plateNames.size(), piece.pieceIndex);
        }
        if (result.plateNames.empty()) {
            throw std::invalid_argument(
                "Selected continuous assembly piece contains no model.");
        }
        return result;
    }
    if (motion.preferContinuousModel
        && motion.continuousSections.size() >= 3
        && (!selectedPieceIndex.has_value() || *selectedPieceIndex == 0)) {
        std::vector<std::string> sectionNames;
        sectionNames.reserve(motion.continuousSections.size());
        for (std::size_t sectionIndex = 0;
             sectionIndex < motion.continuousSections.size(); ++sectionIndex) {
            const auto& points = motion.continuousSections[sectionIndex];
            if (points.size() < 4) {
                throw std::invalid_argument(
                    "Continuous assembly section needs at least four points.");
            }
            const std::string sectionName = namePrefix + "_section_"
                + std::to_string(sectionIndex + 1);
            project.AddWire(
                sectionName, Wire::InterpolatingCubicBSpline(points));
            sectionNames.push_back(sectionName);
        }
        const std::string surfaceName = namePrefix + "_continuous_surface";
        const std::string plateName = namePrefix + "_continuous_plate";
        project.AddLoftSurface(surfaceName, sectionNames);
        if (sourcePlate.plate.HasVariableThickness()) {
            project.AddPlate(
                plateName,
                surfaceName,
                sourcePlate.plate.Thickness(),
                sourcePlate.plate.EndThickness(),
                sourcePlate.plate.Direction(),
                sourcePlate.material);
        } else {
            project.AddPlate(
                plateName,
                surfaceName,
                sourcePlate.plate.Thickness(),
                sourcePlate.plate.Direction(),
                sourcePlate.material);
        }

        const auto& firstSection = motion.continuousSections.front();
        const auto& lastSection = motion.continuousSections.back();
        const std::size_t middle = firstSection.size() / 2;
        Vector3 projectionNormal = geometry::Cross(
            firstSection[std::min(middle + 1, firstSection.size() - 1)]
                - firstSection[middle > 0 ? middle - 1 : middle],
            lastSection[middle] - firstSection[middle]);
        if (projectionNormal.LengthSquared() <= 1.0e-18) {
            projectionNormal = sourcePlate.plate.SourceSurface().Normal(0.5, 0.5);
        } else {
            projectionNormal = projectionNormal.Normalized();
        }
        for (std::size_t openingIndex = 0;
             openingIndex < motion.openingPaths.size(); ++openingIndex) {
            std::vector<Vector3> sourcePoints
                = motion.openingPaths[openingIndex].points;
            if (sourcePoints.size() < 4) {
                continue;
            }
            if ((sourcePoints.front() - sourcePoints.back()).Length()
                > 1.0e-7) {
                sourcePoints.push_back(sourcePoints.front());
            }
            for (Vector3& point : sourcePoints) {
                point = point + projectionNormal * 2.0;
            }
            const std::string sourceName = namePrefix + "_opening_source_"
                + std::to_string(openingIndex + 1);
            const std::string projectedName = namePrefix + "_opening_"
                + std::to_string(openingIndex + 1);
            project.AddWire(sourceName, Wire::Polyline(std::move(sourcePoints)));
            project.AddProjectedWire(
                projectedName,
                sourceName,
                surfaceName,
                -projectionNormal);
            project.AddPlateOpening(plateName, projectedName);
            project.SetWireVisible(sourceName, false);
        }
        for (std::size_t cutIndex = 0;
             cutIndex < motion.reliefCutPaths.size(); ++cutIndex) {
            if (motion.reliefCutPaths[cutIndex].points.size() < 2) {
                continue;
            }
            project.AddWire(
                namePrefix + "_relief_guide_" + std::to_string(cutIndex + 1),
                Wire::Polyline(motion.reliefCutPaths[cutIndex].points));
        }
        result.outerWireNames = std::move(sectionNames);
        result.surfaceNames.push_back(surfaceName);
        result.plateNames.push_back(plateName);
        result.pieceIndices.push_back(0);
        return result;
    }
    for (std::size_t panelIndex = 0; panelIndex < motion.panels.size(); ++panelIndex) {
        const int pieceIndex = motion.pieceIndices[panelIndex];
        if (selectedPieceIndex.has_value() && pieceIndex != *selectedPieceIndex) {
            continue;
        }
        const auto& panel = motion.panels[panelIndex];
        const double thickness = motion.panelThicknessMillimeters[panelIndex];
        if (!std::isfinite(thickness) || thickness <= 0.0) {
            throw std::invalid_argument("Assembly-state panel thickness must be positive.");
        }
        if (!panel[0].IsFinite() || !panel[1].IsFinite() || !panel[2].IsFinite()
            || geometry::Cross(panel[1] - panel[0], panel[2] - panel[0]).LengthSquared()
                <= 1.0e-16) {
            throw std::invalid_argument("Assembly-state panel is collapsed.");
        }

        const std::string baseName = namePrefix
            + "_piece_" + std::to_string(pieceIndex + 1)
            + "_panel_" + std::to_string(panelIndex + 1);
        const std::string outerName = baseName + "_outer";
        const std::string surfaceName = baseName + "_surface";
        const std::string plateName = baseName + "_plate";
        project.AddWire(outerName, Wire::Polyline({
            panel[0], panel[1], panel[2], panel[0],
        }));
        project.AddPlanarSurface(surfaceName, outerName);
        project.AddPlate(
            plateName,
            surfaceName,
            thickness,
            model::PlateThicknessDirection::Centered,
            sourcePlate.material);
        result.outerWireNames.push_back(outerName);
        result.surfaceNames.push_back(surfaceName);
        result.plateNames.push_back(plateName);
        result.pieceIndices.push_back(pieceIndex);
    }
    if (result.plateNames.empty()) {
        throw std::invalid_argument("Selected assembly piece contains no panels.");
    }
    return result;
}

PlateFlatPatternModelResult AddPlateFlatPatternModel(
    Project& project,
    const NamedPlate& sourcePlate,
    const PlateFlatPattern& pattern,
    model::WorkPlane targetPlane,
    std::string namePrefix,
    double reliefCutWidthMillimeters)
{
    ValidatePattern(pattern);
    if (namePrefix.empty()) {
        throw std::invalid_argument("Flat-pattern model name must not be empty.");
    }
    if (!std::isfinite(reliefCutWidthMillimeters)
        || reliefCutWidthMillimeters <= 0.0 || reliefCutWidthMillimeters > 10.0) {
        throw std::invalid_argument("Relief-cut width must be between 0 and 10 mm.");
    }

    const Plate sourceGeometry = sourcePlate.plate;
    const std::string sourceMaterial = sourcePlate.material;
    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    for (const PlateFlatPatternPiece& piece : pattern.pieces) {
        for (const Vector2 point : piece.outerBoundary.points) {
            minimumX = std::min(minimumX, point.x);
            minimumY = std::min(minimumY, point.y);
        }
    }
    const auto toWorld = [&](const PlateFlatPatternPath& path, double height) {
        std::vector<Vector3> points;
        points.reserve(path.points.size());
        for (const Vector2 point : path.points) {
            points.push_back(targetPlane.ToWorld(point.x - minimumX, point.y - minimumY, height));
        }
        return points;
    };
    const auto makeMetadata = [&](bool construction) {
        model::WireMetadata metadata;
        metadata.sourcePlaneName = namePrefix + "_plane";
        metadata.planePolicy = model::WirePlanePolicy::LockedToPlane;
        metadata.construction = construction;
        return metadata;
    };
    const auto slotContour = [&](const PlateFlatPatternPath& cut) {
        std::vector<Vector2> centerline;
        for (const Vector2 point : cut.points) {
            if (centerline.empty() || !AlmostSame(centerline.back(), point, 1.0e-8)) {
                centerline.push_back(point);
            }
        }
        if (centerline.size() < 2) {
            throw std::invalid_argument("Relief cut is too short to form a 3D slot.");
        }
        if (AlmostSame(centerline.front(), centerline.back())) {
            centerline.pop_back();
        }
        const double halfWidth = reliefCutWidthMillimeters * 0.5;
        std::vector<Vector2> left;
        std::vector<Vector2> right;
        left.reserve(centerline.size());
        right.reserve(centerline.size());
        for (std::size_t index = 0; index < centerline.size(); ++index) {
            Vector2 tangent;
            if (index == 0) {
                tangent = centerline[1] - centerline[0];
            } else if (index + 1 == centerline.size()) {
                tangent = centerline[index] - centerline[index - 1];
            } else {
                tangent = centerline[index + 1] - centerline[index - 1];
            }
            const double tangentLength = Length(tangent);
            if (tangentLength <= 1.0e-9) {
                throw std::invalid_argument("Relief cut contains a collapsed segment.");
            }
            const Vector2 offset{-tangent.y * halfWidth / tangentLength,
                tangent.x * halfWidth / tangentLength};
            left.push_back(centerline[index] + offset);
            right.push_back(centerline[index] - offset);
        }
        PlateFlatPatternPath contour;
        contour.name = cut.name + "_slot";
        contour.points = std::move(left);
        for (auto position = right.rbegin(); position != right.rend(); ++position) {
            contour.points.push_back(*position);
        }
        ClosePath(contour.points);
        return contour;
    };

    PlateFlatPatternModelResult result;
    result.workPlaneName = namePrefix + "_plane";
    project.AddWorkPlane(result.workPlaneName, targetPlane);
    const Vector3 projectionDirection = targetPlane.Normal() * -1.0;
    const auto addProjectedPath = [&](
        const PlateFlatPatternPath& path,
        const std::string& baseName,
        const std::string& surfaceName) {
        const std::string drawingName = baseName + "_drawing";
        project.AddWire(drawingName, Wire::Polyline(toWorld(path, 1.0)));
        project.AddProjectedWire(baseName, drawingName, surfaceName, projectionDirection);
        project.SetWireVisible(drawingName, false);
        return baseName;
    };
    std::size_t openingIndex = 0;
    std::size_t foldIndex = 0;
    std::size_t reliefIndex = 0;
    for (std::size_t pieceIndex = 0; pieceIndex < pattern.pieces.size(); ++pieceIndex) {
        const PlateFlatPatternPiece& piece = pattern.pieces[pieceIndex];
        const std::string pieceSuffix = pattern.pieces.size() == 1
            ? std::string{}
            : "_piece_" + std::to_string(pieceIndex + 1);
        const std::string outerName = namePrefix + pieceSuffix + "_outer";
        const std::string surfaceName = namePrefix + pieceSuffix + "_surface";
        const std::string plateName = namePrefix + pieceSuffix + "_plate";
        project.AddWire(outerName, Wire::Polyline(toWorld(piece.outerBoundary, 0.0)), makeMetadata(false));
        project.AddPlanarSurface(surfaceName, outerName);
        if (sourceGeometry.HasVariableThickness()) {
            project.AddPlate(
                plateName,
                surfaceName,
                sourceGeometry.Thickness(),
                sourceGeometry.EndThickness(),
                sourceGeometry.Direction(),
                sourceMaterial);
        } else {
            project.AddPlate(
                plateName,
                surfaceName,
                sourceGeometry.Thickness(),
                sourceGeometry.Direction(),
                sourceMaterial);
        }
        result.outerWireNames.push_back(outerName);
        result.surfaceNames.push_back(surfaceName);
        result.plateNames.push_back(plateName);
        if (pieceIndex == 0) {
            result.outerWireName = outerName;
            result.surfaceName = surfaceName;
            result.plateName = plateName;
        }

        for (const PlateFlatPatternPath& opening : piece.openings) {
            if (opening.incorporatedInOuterBoundary) {
                continue;
            }
            const std::string name = namePrefix + "_opening_" + std::to_string(++openingIndex);
            addProjectedPath(opening, name, surfaceName);
            project.AddPlateOpening(plateName, name);
            result.openingWireNames.push_back(name);
        }
        for (const PlateFlatPatternPath& fold : piece.foldLines) {
            const std::string name = namePrefix + "_fold_" + std::to_string(++foldIndex);
            project.AddWire(name, Wire::Polyline(toWorld(fold, 0.0)), makeMetadata(true));
            result.foldWireNames.push_back(name);
        }
        for (const PlateFlatPatternPath& relief : piece.reliefCuts) {
            const std::string name = namePrefix + "_relief_" + std::to_string(++reliefIndex);
            if (relief.incorporatedInOuterBoundary) {
                project.AddWire(
                    name, Wire::Polyline(toWorld(relief, 0.0)), makeMetadata(false));
                result.reliefCutWireNames.push_back(name);
                continue;
            }
            addProjectedPath(relief, name, surfaceName);
            project.AddPlateReliefCut(plateName, name);
            result.reliefCutWireNames.push_back(name);

            const PlateFlatPatternPath slot = slotContour(relief);
            const std::string slotName = namePrefix + "_relief_slot_" + std::to_string(reliefIndex);
            addProjectedPath(slot, slotName, surfaceName);
            project.AddPlateOpening(plateName, slotName);
            result.openingWireNames.push_back(slotName);
        }
    }
    return result;
}

} // namespace kachakacha::io
