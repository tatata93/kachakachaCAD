#include "kachakacha/io/PlateFlatPattern.h"

#include <algorithm>
#include <array>
#include <cmath>
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
        || options.reliefCutDepthRatio <= 0.0 || options.reliefCutDepthRatio >= 1.0) {
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

std::vector<Vector2> BuildDevelopedWirePath(
    const Project& project,
    const NamedPlate& namedPlate,
    std::string_view openingName,
    const DevelopmentGrid& grid,
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
        const Vector2 flatPoint = grid.Evaluate(localU, localV);
        if (points.empty() || !AlmostSame(points.back(), flatPoint, 1.0e-7)) {
            points.push_back(flatPoint);
        }
    }
    if (closePath) {
        ClosePath(points);
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

std::vector<double> FoldParameters(
    const Plate& plate,
    bool alongU,
    const PlateFlatPatternOptions& options)
{
    const double length = SpatialPathLength(plate, alongU, 0.5);
    const int intervalCount = std::clamp(
        static_cast<int>(std::ceil(length / options.foldSpacingMillimeters)), 1, 200);
    std::vector<double> parameters;
    for (int interval = 1; interval < intervalCount; ++interval) {
        const double parameter = static_cast<double>(interval) / intervalCount;
        const double delta = std::min(0.02, 0.45 / intervalCount);
        const Vector3 firstNormal = alongU
            ? PlateNormal(plate, parameter - delta, 0.5)
            : PlateNormal(plate, 0.5, parameter - delta);
        const Vector3 secondNormal = alongU
            ? PlateNormal(plate, parameter + delta, 0.5)
            : PlateNormal(plate, 0.5, parameter + delta);
        if (NormalAngleDegrees(firstNormal, secondNormal) + 1.0e-9
            >= options.minimumFoldAngleDegrees) {
            parameters.push_back(parameter);
        }
    }
    return parameters;
}

double TotalNormalChangeDegrees(const Plate& plate, bool alongU)
{
    constexpr int samples = 32;
    double total = 0.0;
    Vector3 previous = alongU ? PlateNormal(plate, 0.0, 0.5) : PlateNormal(plate, 0.5, 0.0);
    for (int sample = 1; sample <= samples; ++sample) {
        const double parameter = static_cast<double>(sample) / samples;
        const Vector3 current = alongU
            ? PlateNormal(plate, parameter, 0.5)
            : PlateNormal(plate, 0.5, parameter);
        total += NormalAngleDegrees(previous, current);
        previous = current;
    }
    return total;
}

void AddGeneratedFoldAndReliefPaths(
    PlateFlatPattern& pattern,
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
        || pattern.analysis.classification != PlateDevelopability::DoubleCurved) {
        return;
    }
    const bool cutAtConstantU = TotalNormalChangeDegrees(plate, true)
        >= TotalNormalChangeDegrees(plate, false);
    const std::vector<double>& parameters = cutAtConstantU ? uParameters : vParameters;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const bool fromMinimumSide = index % 2 == 0;
        const double first = fromMinimumSide ? 0.0 : 1.0;
        const double last = fromMinimumSide
            ? options.reliefCutDepthRatio
            : 1.0 - options.reliefCutDepthRatio;
        pattern.reliefCuts.push_back({
            "auto_relief_" + std::to_string(index + 1),
            cutAtConstantU
                ? grid.ConstantU(parameters[index], first, last)
                : grid.ConstantV(parameters[index], first, last),
        });
    }
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
    };
    validatePath(pattern.outerBoundary, true);
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

    if (plate.SourceSurface().Kind() == SurfaceKind::Planar) {
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
        AddGeneratedFoldAndReliefPaths(pattern, plate, grid, options);
    }
    ValidatePattern(pattern);
    return pattern;
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
    includePath(pattern.outerBoundary);
    for (const auto& opening : pattern.openings) {
        includePath(opening);
    }
    for (const auto& fold : pattern.foldLines) {
        includePath(fold);
    }
    for (const auto& cut : pattern.reliefCuts) {
        includePath(cut);
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
    writePath(pattern.outerBoundary, "    ");
    output << "  </g>\n"
           << "  <g id=\"CUT_OPENING\" fill=\"none\" stroke=\"#d12f3f\" stroke-width=\"0.1\" stroke-linejoin=\"round\">\n";
    for (const auto& opening : pattern.openings) {
        writePath(opening, "    ");
    }
    output << "  </g>\n"
           << "  <g id=\"RELIEF_CUT\" fill=\"none\" stroke=\"#d12f3f\" stroke-width=\"0.14\" stroke-linecap=\"round\">\n";
    for (const auto& cut : pattern.reliefCuts) {
        writePath(cut, "    ");
    }
    output << "  </g>\n"
           << "  <g id=\"FOLD\" fill=\"none\" stroke=\"#4c5963\" stroke-width=\"0.1\" stroke-dasharray=\"2,1\">\n";
    for (const auto& fold : pattern.foldLines) {
        writePath(fold, "    ");
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
    WriteDxfPath(output, pattern.outerBoundary, "CUT_OUTER");
    for (const auto& opening : pattern.openings) {
        WriteDxfPath(output, opening, "CUT_OPENING");
    }
    for (const auto& cut : pattern.reliefCuts) {
        WriteDxfPath(output, cut, "RELIEF_CUT");
    }
    for (const auto& fold : pattern.foldLines) {
        WriteDxfPath(output, fold, "FOLD");
    }
    WriteDxfPair(output, 0, "ENDSEC");
    WriteDxfPair(output, 0, "EOF");
    if (!output) {
        throw std::runtime_error("Failed to write plate flat-pattern DXF.");
    }
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
    for (const Vector2 point : pattern.outerBoundary.points) {
        minimumX = std::min(minimumX, point.x);
        minimumY = std::min(minimumY, point.y);
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
    result.outerWireName = namePrefix + "_outer";
    result.surfaceName = namePrefix + "_surface";
    result.plateName = namePrefix + "_plate";
    project.AddWorkPlane(result.workPlaneName, targetPlane);
    project.AddWire(
        result.outerWireName,
        Wire::Polyline(toWorld(pattern.outerBoundary, 0.0)),
        makeMetadata(false));
    project.AddPlanarSurface(result.surfaceName, result.outerWireName);
    if (sourceGeometry.HasVariableThickness()) {
        project.AddPlate(
            result.plateName,
            result.surfaceName,
            sourceGeometry.Thickness(),
            sourceGeometry.EndThickness(),
            sourceGeometry.Direction(),
            sourceMaterial);
    } else {
        project.AddPlate(
            result.plateName,
            result.surfaceName,
            sourceGeometry.Thickness(),
            sourceGeometry.Direction(),
            sourceMaterial);
    }

    const Vector3 projectionDirection = targetPlane.Normal() * -1.0;
    const auto addProjectedPath = [&](const PlateFlatPatternPath& path, const std::string& baseName) {
        const std::string drawingName = baseName + "_drawing";
        project.AddWire(drawingName, Wire::Polyline(toWorld(path, 1.0)));
        project.AddProjectedWire(baseName, drawingName, result.surfaceName, projectionDirection);
        project.SetWireVisible(drawingName, false);
        return baseName;
    };

    for (std::size_t index = 0; index < pattern.openings.size(); ++index) {
        const std::string name = namePrefix + "_opening_" + std::to_string(index + 1);
        addProjectedPath(pattern.openings[index], name);
        project.AddPlateOpening(result.plateName, name);
        result.openingWireNames.push_back(name);
    }
    for (std::size_t index = 0; index < pattern.foldLines.size(); ++index) {
        const std::string name = namePrefix + "_fold_" + std::to_string(index + 1);
        project.AddWire(name, Wire::Polyline(toWorld(pattern.foldLines[index], 0.0)), makeMetadata(true));
        result.foldWireNames.push_back(name);
    }
    for (std::size_t index = 0; index < pattern.reliefCuts.size(); ++index) {
        const std::string name = namePrefix + "_relief_" + std::to_string(index + 1);
        addProjectedPath(pattern.reliefCuts[index], name);
        project.AddPlateReliefCut(result.plateName, name);
        result.reliefCutWireNames.push_back(name);

        const PlateFlatPatternPath slot = slotContour(pattern.reliefCuts[index]);
        const std::string slotName = namePrefix + "_relief_slot_" + std::to_string(index + 1);
        addProjectedPath(slot, slotName);
        project.AddPlateOpening(result.plateName, slotName);
        result.openingWireNames.push_back(slotName);
    }
    return result;
}

} // namespace kachakacha::io
