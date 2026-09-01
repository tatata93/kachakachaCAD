#include "kachakacha/io/BentSheetPapercraft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
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

struct ReliefPath {
    std::string name;
    std::vector<Vector2> uv;
    PlateFlatPatternPath flat;
    bool closedCutout = false;
    bool manual = false;
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

void AppendUnique(std::vector<Vector2>& points, Vector2 point)
{
    if (points.empty() || !AlmostSame(points.back(), point)) {
        points.push_back(point);
    }
}

void ClosePath(std::vector<Vector2>& points)
{
    if (points.size() < 3) {
        throw std::invalid_argument("A bent-sheet papercraft boundary collapsed.");
    }
    if (!AlmostSame(points.front(), points.back())) {
        points.push_back(points.front());
    } else {
        points.back() = points.front();
    }
}

const NamedWire& RequireNamedWire(const Project& project, std::string_view name)
{
    const auto position = std::find_if(
        project.Wires().begin(), project.Wires().end(),
        [&](const NamedWire& wire) { return wire.name == name; });
    if (position == project.Wires().end()) {
        throw std::invalid_argument(
            "Bent-sheet papercraft source wire was not found: "
            + std::string(name));
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
            "A bent-sheet opening or cut must be projected onto the selected plate: "
            + projected.name);
    }
    const NamedWire& source = RequireNamedWire(
        project, projected.projection->sourceWireName);
    const auto& plate = namedPlate.plate;
    const auto& range = plate.Range();
    const int samples = source.wire.Kind() == WireKind::Polyline
        ? std::max(requestedSamples,
            static_cast<int>(source.wire.ControlPoints().size()) * 12)
        : requestedSamples;
    std::vector<Vector3> sourcePoints;
    sourcePoints.reserve(static_cast<std::size_t>(samples) + 1);
    for (int sample = 0; sample <= samples; ++sample) {
        sourcePoints.push_back(source.wire.Evaluate(
            static_cast<double>(sample) / samples));
    }
    const auto projections = plate.SourceSurface().ProjectPointsAlongDirection(
        sourcePoints, projected.projection->direction);
    std::vector<Vector2> result;
    result.reserve(projections.size());
    for (const auto& hit : projections) {
        const double u = (hit.u - range.minimumU)
            / (range.maximumU - range.minimumU);
        const double v = (hit.v - range.minimumV)
            / (range.maximumV - range.minimumV);
        constexpr double rangeTolerance = 1.0e-5;
        if (u < -rangeTolerance || u > 1.0 + rangeTolerance
            || v < -rangeTolerance || v > 1.0 + rangeTolerance) {
            throw std::invalid_argument(
                "Bent-sheet feature lies outside the selected plate: "
                + projected.name);
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

bool PointInsidePath(Vector2 point, const std::vector<Vector2>& path)
{
    if (path.size() < 4) {
        return false;
    }
    bool inside = false;
    for (std::size_t first = 0, second = path.size() - 1;
         first < path.size(); second = first++) {
        const Vector2 a = path[first];
        const Vector2 b = path[second];
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
    constexpr double tolerance = 1.0e-11;
    const double first = Cross2(b - a, c - a);
    const double second = Cross2(b - a, d - a);
    const double third = Cross2(d - c, a - c);
    const double fourth = Cross2(d - c, b - c);
    return ((first > tolerance && second < -tolerance)
            || (first < -tolerance && second > tolerance))
        && ((third > tolerance && fourth < -tolerance)
            || (third < -tolerance && fourth > tolerance));
}

double PointSegmentDistance(Vector2 point, Vector2 first, Vector2 second)
{
    const Vector2 edge = second - first;
    const double squaredLength = edge.x * edge.x + edge.y * edge.y;
    if (squaredLength <= 1.0e-18) {
        return Length(point - first);
    }
    const double parameter = std::clamp(
        ((point.x - first.x) * edge.x + (point.y - first.y) * edge.y)
            / squaredLength,
        0.0, 1.0);
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
                point, boundary[edge], boundary[edge + 1]);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = edge;
            }
        }
        return std::pair{best, bestDistance};
    };
    const auto [firstEdge, firstDistance] = nearestEdge(notch.front());
    const auto [lastEdge, lastDistance] = nearestEdge(notch.back());
    if (firstDistance > 0.02 || lastDistance > 0.02) {
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
        const double squaredLength
            = edgeVector.x * edgeVector.x + edgeVector.y * edgeVector.y;
        const double parameter = squaredLength <= 1.0e-18
            ? 0.0
            : std::clamp(
                ((point.x - boundary[edge].x) * edgeVector.x
                    + (point.y - boundary[edge].y) * edgeVector.y)
                    / squaredLength,
                0.0, 1.0);
        return boundaryDistance[edge] + std::sqrt(squaredLength) * parameter;
    };
    const double firstPosition = boundaryPosition(notch.front(), firstEdge);
    const double lastPosition = boundaryPosition(notch.back(), lastEdge);
    const auto forwardLength = [&](double from, double to) {
        return to >= from ? to - from : perimeter - from + to;
    };

    Vector2 keptStart;
    Vector2 keptEnd;
    std::size_t keptStartEdge;
    std::size_t keptEndEdge;
    std::vector<Vector2> replacement = notch;
    if (forwardLength(firstPosition, lastPosition)
        <= forwardLength(lastPosition, firstPosition)) {
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
    AppendUnique(rebuilt, keptStart);
    std::size_t edge = (keptStartEdge + 1) % edgeCount;
    while (edge != keptEndEdge) {
        AppendUnique(rebuilt, boundary[edge]);
        edge = (edge + 1) % edgeCount;
    }
    AppendUnique(rebuilt, boundary[keptEndEdge]);
    AppendUnique(rebuilt, keptEnd);
    ClosePath(rebuilt);
    boundary = std::move(rebuilt);
    return true;
}

Vector3 PlateNormal(const Plate& plate, double u, double v)
{
    return plate.SourceSurface().Normal(
        plate.SourceU(std::clamp(u, 1.0e-5, 1.0 - 1.0e-5)),
        plate.SourceV(std::clamp(v, 1.0e-5, 1.0 - 1.0e-5)));
}

double NormalAngleDegrees(Vector3 first, Vector3 second)
{
    return std::acos(std::clamp(geometry::Dot(first, second), -1.0, 1.0))
        * 180.0 / kPi;
}

double DirectionNormalChange(const Plate& plate, bool alongU)
{
    double total = 0.0;
    for (int cross = 1; cross <= 5; ++cross) {
        const double fixed = static_cast<double>(cross) / 6.0;
        Vector3 previous = PlateNormal(
            plate, alongU ? 0.01 : fixed, alongU ? fixed : 0.01);
        for (int sample = 1; sample <= 32; ++sample) {
            const double parameter = 0.01 + 0.98 * sample / 32.0;
            const Vector3 current = PlateNormal(
                plate,
                alongU ? parameter : fixed,
                alongU ? fixed : parameter);
            total += NormalAngleDegrees(previous, current);
            previous = current;
        }
    }
    return total / 5.0;
}

class ArcLengthMapper {
public:
    ArcLengthMapper(const Plate& plate, bool bendAlongU)
        : plate_(plate), bendAlongU_(bendAlongU)
    {
        strongArc_.resize(kWeakSteps + 1);
        strongTotal_.resize(kWeakSteps + 1);
        for (int weakIndex = 0; weakIndex <= kWeakSteps; ++weakIndex) {
            const double weak = static_cast<double>(weakIndex) / kWeakSteps;
            auto& row = strongArc_[static_cast<std::size_t>(weakIndex)];
            row.resize(kStrongSteps + 1, 0.0);
            const Vector2 firstUv = ToUv(0.0, weak);
            Vector3 previous = plate_.Evaluate(firstUv.x, firstUv.y, 0.5);
            for (int strongIndex = 1; strongIndex <= kStrongSteps; ++strongIndex) {
                const double strong
                    = static_cast<double>(strongIndex) / kStrongSteps;
                const Vector2 uv = ToUv(strong, weak);
                const Vector3 current = plate_.Evaluate(uv.x, uv.y, 0.5);
                row[static_cast<std::size_t>(strongIndex)]
                    = row[static_cast<std::size_t>(strongIndex - 1)]
                    + (current - previous).Length();
                previous = current;
            }
            strongTotal_[static_cast<std::size_t>(weakIndex)] = row.back();
        }

        weakArc_.resize(kWeakSteps + 1, 0.0);
        const Vector2 firstWeakUv = ToUv(0.5, 0.0);
        Vector3 previous = plate_.Evaluate(
            firstWeakUv.x, firstWeakUv.y, 0.5);
        for (int weakIndex = 1; weakIndex <= kWeakSteps; ++weakIndex) {
            const double weak = static_cast<double>(weakIndex) / kWeakSteps;
            const Vector2 uv = ToUv(0.5, weak);
            const Vector3 current = plate_.Evaluate(uv.x, uv.y, 0.5);
            weakArc_[static_cast<std::size_t>(weakIndex)]
                = weakArc_[static_cast<std::size_t>(weakIndex - 1)]
                + (current - previous).Length();
            previous = current;
        }
    }

    [[nodiscard]] Vector2 ToUv(double strong, double weak) const noexcept
    {
        return bendAlongU_
            ? Vector2{strong, weak}
            : Vector2{weak, strong};
    }

    [[nodiscard]] Vector2 Map(Vector2 uv) const
    {
        const double strong = bendAlongU_ ? uv.x : uv.y;
        const double weak = bendAlongU_ ? uv.y : uv.x;
        const double weakPosition = std::clamp(weak, 0.0, 1.0) * kWeakSteps;
        const int weakFirst = std::min(
            static_cast<int>(std::floor(weakPosition)), kWeakSteps - 1);
        const int weakSecond = weakFirst + 1;
        const double weakBlend = weakPosition - weakFirst;
        const double firstArc = RowArc(weakFirst, strong);
        const double secondArc = RowArc(weakSecond, strong);
        const double arc = firstArc * (1.0 - weakBlend) + secondArc * weakBlend;
        const double total
            = strongTotal_[static_cast<std::size_t>(weakFirst)] * (1.0 - weakBlend)
            + strongTotal_[static_cast<std::size_t>(weakSecond)] * weakBlend;
        const double weakLength
            = weakArc_[static_cast<std::size_t>(weakFirst)] * (1.0 - weakBlend)
            + weakArc_[static_cast<std::size_t>(weakSecond)] * weakBlend;
        return {arc - total * 0.5, weakLength};
    }

    [[nodiscard]] double StrongLength(double weak) const
    {
        const double position = std::clamp(weak, 0.0, 1.0) * kWeakSteps;
        const int first = std::min(
            static_cast<int>(std::floor(position)), kWeakSteps - 1);
        const int second = first + 1;
        const double blend = position - first;
        return strongTotal_[static_cast<std::size_t>(first)] * (1.0 - blend)
            + strongTotal_[static_cast<std::size_t>(second)] * blend;
    }

    [[nodiscard]] double WeakLength() const noexcept
    {
        return weakArc_.back();
    }

private:
    [[nodiscard]] double RowArc(int weakIndex, double strong) const
    {
        const double position = std::clamp(strong, 0.0, 1.0) * kStrongSteps;
        const int first = std::min(
            static_cast<int>(std::floor(position)), kStrongSteps - 1);
        const int second = first + 1;
        const double blend = position - first;
        const auto& row = strongArc_[static_cast<std::size_t>(weakIndex)];
        return row[static_cast<std::size_t>(first)] * (1.0 - blend)
            + row[static_cast<std::size_t>(second)] * blend;
    }

    static constexpr int kStrongSteps = 256;
    static constexpr int kWeakSteps = 128;
    const Plate& plate_;
    bool bendAlongU_ = true;
    std::vector<std::vector<double>> strongArc_;
    std::vector<double> strongTotal_;
    std::vector<double> weakArc_;
};

bool PointInTriangle(Vector2 point, const std::array<Vector2, 3>& triangle)
{
    constexpr double tolerance = 1.0e-11;
    const double first = Cross2(triangle[1] - triangle[0], point - triangle[0]);
    const double second = Cross2(triangle[2] - triangle[1], point - triangle[1]);
    const double third = Cross2(triangle[0] - triangle[2], point - triangle[2]);
    const bool negative = first < -tolerance || second < -tolerance
        || third < -tolerance;
    const bool positive = first > tolerance || second > tolerance
        || third > tolerance;
    return !(negative && positive);
}

bool TriangleTouchesPath(
    const std::array<Vector2, 3>& triangle,
    const std::vector<Vector2>& path)
{
    if (path.size() < 2) {
        return false;
    }
    if (std::any_of(path.begin(), path.end(),
            [&](Vector2 point) { return PointInTriangle(point, triangle); })) {
        return true;
    }
    for (std::size_t point = 1; point < path.size(); ++point) {
        for (int edge = 0; edge < 3; ++edge) {
            if (SegmentsIntersect(
                    path[point - 1], path[point],
                    triangle[static_cast<std::size_t>(edge)],
                    triangle[static_cast<std::size_t>((edge + 1) % 3)])) {
                return true;
            }
        }
    }
    return false;
}

Vector3 TriangleNormal(const std::array<Vector3, 3>& triangle)
{
    return geometry::Cross(
        triangle[1] - triangle[0], triangle[2] - triangle[0]).Normalized();
}

struct Quaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Quaternion FrameRotation(
    Vector3 flatX,
    Vector3 flatY,
    Vector3 flatZ,
    Vector3 targetX,
    Vector3 targetY,
    Vector3 targetZ)
{
    const double m00 = targetX.x * flatX.x + targetY.x * flatY.x
        + targetZ.x * flatZ.x;
    const double m01 = targetX.x * flatX.y + targetY.x * flatY.y
        + targetZ.x * flatZ.y;
    const double m02 = targetX.x * flatX.z + targetY.x * flatY.z
        + targetZ.x * flatZ.z;
    const double m10 = targetX.y * flatX.x + targetY.y * flatY.x
        + targetZ.y * flatZ.x;
    const double m11 = targetX.y * flatX.y + targetY.y * flatY.y
        + targetZ.y * flatZ.y;
    const double m12 = targetX.y * flatX.z + targetY.y * flatY.z
        + targetZ.y * flatZ.z;
    const double m20 = targetX.z * flatX.x + targetY.z * flatY.x
        + targetZ.z * flatZ.x;
    const double m21 = targetX.z * flatX.y + targetY.z * flatY.y
        + targetZ.z * flatZ.y;
    const double m22 = targetX.z * flatX.z + targetY.z * flatY.z
        + targetZ.z * flatZ.z;

    Quaternion result;
    const double trace = m00 + m11 + m22;
    if (trace > 0.0) {
        const double scale = std::sqrt(trace + 1.0) * 2.0;
        result.w = 0.25 * scale;
        result.x = (m21 - m12) / scale;
        result.y = (m02 - m20) / scale;
        result.z = (m10 - m01) / scale;
    } else if (m00 > m11 && m00 > m22) {
        const double scale = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        result.w = (m21 - m12) / scale;
        result.x = 0.25 * scale;
        result.y = (m01 + m10) / scale;
        result.z = (m02 + m20) / scale;
    } else if (m11 > m22) {
        const double scale = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        result.w = (m02 - m20) / scale;
        result.x = (m01 + m10) / scale;
        result.y = 0.25 * scale;
        result.z = (m12 + m21) / scale;
    } else {
        const double scale = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        result.w = (m10 - m01) / scale;
        result.x = (m02 + m20) / scale;
        result.y = (m12 + m21) / scale;
        result.z = 0.25 * scale;
    }
    const double length = std::sqrt(
        result.w * result.w + result.x * result.x
        + result.y * result.y + result.z * result.z);
    result.w /= length;
    result.x /= length;
    result.y /= length;
    result.z /= length;
    if (result.w < 0.0) {
        result.w = -result.w;
        result.x = -result.x;
        result.y = -result.y;
        result.z = -result.z;
    }
    return result;
}

Quaternion InterpolateFromIdentity(Quaternion target, double amount)
{
    const double halfAngle = std::acos(std::clamp(target.w, -1.0, 1.0));
    if (halfAngle <= 1.0e-9) {
        return {};
    }
    const double sine = std::sin(halfAngle);
    const double targetScale = std::sin(amount * halfAngle) / sine;
    Quaternion result;
    result.w = std::cos(amount * halfAngle);
    result.x = target.x * targetScale;
    result.y = target.y * targetScale;
    result.z = target.z * targetScale;
    return result;
}

Vector3 Rotate(Quaternion rotation, Vector3 value)
{
    const Vector3 axis{rotation.x, rotation.y, rotation.z};
    return value * (rotation.w * rotation.w - geometry::Dot(axis, axis))
        + axis * (2.0 * geometry::Dot(axis, value))
        + geometry::Cross(axis, value) * (2.0 * rotation.w);
}

double TargetSpacing(int fidelity)
{
    const double parameter
        = static_cast<double>(std::clamp(fidelity, 1, 10) - 1) / 9.0;
    return 12.0 * std::pow(1.8 / 12.0, parameter);
}

class BentSheetGenerator {
public:
    BentSheetGenerator(
        const Project& project,
        const NamedPlate& namedPlate,
        PlateFlatPatternOptions options)
        : project_(project), namedPlate_(namedPlate), plate_(namedPlate.plate),
          options_(std::move(options)), bendAlongU_(ChooseBendDirection()),
          mapper_(plate_, bendAlongU_)
    {
        ValidateOptions();
        ReadFeaturePaths();
        BuildOuterBoundary();
        BuildOpenings();
        BuildAutomaticReliefs();
        AddManualCuts();
        MeasureFlatDistortion();
    }

    [[nodiscard]] PlateFlatPattern Pattern() const
    {
        PlateFlatPattern result;
        result.plateName = namedPlate_.name + "_bent_sheet_papercraft";
        result.pieces.push_back(piece_);
        result.outerBoundary = piece_.outerBoundary;
        result.openings = piece_.openings;
        result.foldLines = piece_.foldLines;
        result.reliefCuts = piece_.reliefCuts;
        result.analysis.classification
            = plate_.AnalyzeDevelopability().classification;
        result.analysis.maximumEdgeDistortionMillimeters = maximumEdgeError_;
        result.analysis.rootMeanSquareEdgeDistortionMillimeters = rmsEdgeError_;
        const PlateAssemblyMotion assembled = Motion(1.0);
        result.analysis.maximumBoundaryApproximationMillimeters
            = assembled.maximumTargetMismatchMillimeters;
        result.analysis.maximumReconstructedDeviationMillimeters
            = assembled.maximumTargetMismatchMillimeters;
        result.analysis.rootMeanSquareReconstructedDeviationMillimeters
            = assembled.rootMeanSquareTargetMismatchMillimeters;
        result.analysis.maximumMaterialEdgeErrorMillimeters
            = assembled.maximumMaterialEdgeErrorMillimeters;
        result.analysis.rootMeanSquareMaterialEdgeErrorMillimeters
            = assembled.rootMeanSquareMaterialEdgeErrorMillimeters;
        result.analysis.maximumSeamMismatchMillimeters
            = assembled.maximumSeamMismatchMillimeters;
        result.analysis.maximumPanelConnectionMismatchMillimeters
            = assembled.maximumPanelConnectionMismatchMillimeters;
        const auto pathLength = [](const PlateFlatPatternPath& path) {
            double length = 0.0;
            for (std::size_t point = 1; point < path.points.size(); ++point) {
                length += Length(
                    path.points[point] - path.points[point - 1]);
            }
            return length;
        };
        const auto addLength = [&](const PlateFlatPatternPath& path) {
            result.analysis.totalCutLengthMillimeters += pathLength(path);
        };
        addLength(piece_.outerBoundary);
        for (const PlateFlatPatternPath& opening : piece_.openings) {
            addLength(opening);
        }
        for (const PlateFlatPatternPath& relief : piece_.reliefCuts) {
            addLength(relief);
            if (relief.cutKind == PapercraftCutKind::SeparatingSeam) {
                ++result.analysis.separatingSeamCount;
                result.analysis.separatingSeamLengthMillimeters
                    += pathLength(relief);
            } else {
                ++result.analysis.nonSeparatingReliefCutCount;
                result.analysis.reliefCutLengthMillimeters
                    += pathLength(relief);
            }
        }
        result.analysis.pieceCount = 1;
        result.analysis.automaticNotchCount = automaticReliefCount_;
        return result;
    }

    [[nodiscard]] PlateAssemblyGuide Guide(double progress = 1.0) const
    {
        if (!std::isfinite(progress)) {
            throw std::invalid_argument(
                "Bent-sheet guide progress must be finite.");
        }
        progress = std::clamp(progress, 0.0, 1.0);
        const double eased = progress * progress * (3.0 - 2.0 * progress);
        PlateAssemblyGuide guide;
        guide.plateName = namedPlate_.name + "_bent_sheet_papercraft";
        for (const ReliefPath& relief : reliefs_) {
            PlateAssemblyGuidePath path;
            path.name = relief.name;
            path.points.reserve(relief.uv.size());
            for (const Vector2 uv : relief.uv) {
                path.points.push_back(plate_.Evaluate(uv.x, uv.y, 0.5));
            }
            if (relief.closedCutout && path.points.size() >= 5) {
                const double sideStrong = bendAlongU_
                    ? relief.uv.front().x
                    : relief.uv.front().y;
                std::size_t tip = 0;
                double tipDistance = -1.0;
                for (std::size_t point = 0; point < relief.uv.size(); ++point) {
                    const double strong = bendAlongU_
                        ? relief.uv[point].x
                        : relief.uv[point].y;
                    const double distance = std::abs(strong - sideStrong);
                    if (distance > tipDistance) {
                        tipDistance = distance;
                        tip = point;
                    }
                }
                const std::size_t pairCount = std::min(
                    tip + 1, path.points.size() - tip);
                for (std::size_t pair = 0; pair < pairCount; ++pair) {
                    const std::size_t first = tip - pair;
                    const std::size_t second = tip + pair;
                    const Vector3 midpoint
                        = (path.points[first] + path.points[second]) * 0.5;
                    path.points[first] = path.points[first] * (1.0 - eased)
                        + midpoint * eased;
                    path.points[second] = path.points[second] * (1.0 - eased)
                        + midpoint * eased;
                }
            }
            if (relief.manual) {
                guide.splitLines.push_back(std::move(path));
            } else {
                guide.reliefCuts.push_back(std::move(path));
            }
        }
        return guide;
    }

    [[nodiscard]] PlateAssemblyMotion Motion(
        double progress,
        bool interactivePreview = false) const
    {
        if (!std::isfinite(progress)) {
            throw std::invalid_argument(
                "Bent-sheet assembly progress must be finite.");
        }
        PlateAssemblyMotion motion;
        motion.plateName = namedPlate_.name + "_bent_sheet_papercraft";
        motion.progress = std::clamp(progress, 0.0, 1.0);
        motion.pieceCount = 1;

        const Vector2 rootUv = mapper_.ToUv(0.0, 0.0);
        const Vector3 layoutOrigin = plate_.Evaluate(rootUv.x, rootUv.y, 0.5);
        Vector3 layoutNormal = PlateNormal(plate_, rootUv.x + 0.001, rootUv.y + 0.001);
        const Vector2 strongUv = mapper_.ToUv(0.01, 0.0);
        Vector3 layoutX = plate_.Evaluate(strongUv.x, strongUv.y, 0.5)
            - layoutOrigin;
        layoutX = layoutX - layoutNormal * geometry::Dot(layoutX, layoutNormal);
        layoutX = layoutX.Normalized();
        const Vector2 weakUv = mapper_.ToUv(0.0, 0.01);
        Vector3 layoutY = plate_.Evaluate(weakUv.x, weakUv.y, 0.5)
            - layoutOrigin;
        layoutY = layoutY - layoutNormal * geometry::Dot(layoutY, layoutNormal)
            - layoutX * geometry::Dot(layoutY, layoutX);
        if (layoutY.LengthSquared() <= 1.0e-18) {
            layoutY = geometry::Cross(layoutNormal, layoutX);
        }
        layoutY = layoutY.Normalized();
        if (geometry::Dot(
                geometry::Cross(layoutX, layoutY), layoutNormal) < 0.0) {
            layoutY = -layoutY;
            layoutNormal = -layoutNormal;
        }
        const Vector2 flatRoot = mapper_.Map(rootUv);
        const double eased = motion.progress * motion.progress
            * (3.0 - 2.0 * motion.progress);

        const double maximumStrongLength = std::max({
            mapper_.StrongLength(0.0), mapper_.StrongLength(0.5),
            mapper_.StrongLength(1.0)});
        const double target = TargetSpacing(options_.papercraftFidelity);
        const int strongIntervals = std::clamp(
            static_cast<int>(std::ceil(maximumStrongLength / target)), 6, 64);
        const int weakIntervals = std::clamp(
            static_cast<int>(std::ceil(mapper_.WeakLength() / target)), 3, 40);
        const int refinementDepth = std::clamp(
            2 + options_.papercraftFidelity / 3, 2, 5);

        const auto flatWorld = [&](Vector2 uv) {
            const Vector2 flat = mapper_.Map(uv) - flatRoot;
            return layoutOrigin + layoutX * flat.x + layoutY * flat.y;
        };
        const auto insideClosedCutout = [&](Vector2 uv) {
            if (std::any_of(openings_.begin(), openings_.end(),
                    [&](const UvPath& opening) {
                        return PointInsidePath(uv, opening.points);
                    })) {
                return true;
            }
            return std::any_of(reliefs_.begin(), reliefs_.end(),
                [&](const ReliefPath& relief) {
                    return relief.closedCutout
                        && PointInsidePath(uv, relief.uv);
                });
        };
        const auto closeToOpenCut = [&](Vector2 uv) {
            const Vector2 flat = mapper_.Map(uv);
            for (const ReliefPath& relief : reliefs_) {
                if (relief.closedCutout || !relief.manual
                    || relief.flat.points.size() < 2) {
                    continue;
                }
                for (std::size_t point = 1;
                     point < relief.flat.points.size(); ++point) {
                    if (PointSegmentDistance(
                            flat,
                            relief.flat.points[point - 1],
                            relief.flat.points[point]) <= 0.10) {
                        return true;
                    }
                }
            }
            return false;
        };

        struct AlignedSlit {
            double weak = 0.0;
            double minimumStrong = 0.0;
            double maximumStrong = 0.0;
        };
        std::vector<AlignedSlit> alignedSlits;
        std::vector<double> strongCoordinates;
        std::vector<double> weakCoordinates;
        strongCoordinates.reserve(
            static_cast<std::size_t>(strongIntervals + 1)
            + reliefs_.size() * 2);
        weakCoordinates.reserve(
            static_cast<std::size_t>(weakIntervals + 1) + reliefs_.size());
        for (int strong = 0; strong <= strongIntervals; ++strong) {
            strongCoordinates.push_back(
                static_cast<double>(strong) / strongIntervals);
        }
        for (int weak = 0; weak <= weakIntervals; ++weak) {
            weakCoordinates.push_back(static_cast<double>(weak) / weakIntervals);
        }
        for (const ReliefPath& relief : reliefs_) {
            if (relief.closedCutout || relief.manual || relief.uv.size() < 2) {
                continue;
            }
            const auto strongOf = [&](Vector2 uv) {
                return bendAlongU_ ? uv.x : uv.y;
            };
            const auto weakOf = [&](Vector2 uv) {
                return bendAlongU_ ? uv.y : uv.x;
            };
            const double weak = weakOf(relief.uv.front());
            if (!std::all_of(
                    relief.uv.begin(), relief.uv.end(), [&](Vector2 uv) {
                        return std::abs(weakOf(uv) - weak) <= 1.0e-9;
                    })) {
                continue;
            }
            double minimumStrong = 1.0;
            double maximumStrong = 0.0;
            for (const Vector2 uv : relief.uv) {
                minimumStrong = std::min(minimumStrong, strongOf(uv));
                maximumStrong = std::max(maximumStrong, strongOf(uv));
            }
            if (maximumStrong - minimumStrong <= 1.0e-6) {
                continue;
            }
            alignedSlits.push_back({weak, minimumStrong, maximumStrong});
            weakCoordinates.push_back(weak);
            strongCoordinates.push_back(minimumStrong);
            strongCoordinates.push_back(maximumStrong);
        }
        const auto sortUnique = [](std::vector<double>& coordinates) {
            std::sort(coordinates.begin(), coordinates.end());
            coordinates.erase(
                std::unique(
                    coordinates.begin(), coordinates.end(),
                    [](double first, double second) {
                        return std::abs(first - second) <= 1.0e-10;
                    }),
                coordinates.end());
        };
        sortUnique(strongCoordinates);
        sortUnique(weakCoordinates);
        const auto touchesFeature = [&](const std::array<Vector2, 3>& triangle) {
            if (std::any_of(openings_.begin(), openings_.end(),
                    [&](const UvPath& opening) {
                        return TriangleTouchesPath(triangle, opening.points);
                    })) {
                return true;
            }
            return std::any_of(reliefs_.begin(), reliefs_.end(),
                [&](const ReliefPath& relief) {
                    return (relief.closedCutout || relief.manual)
                        && TriangleTouchesPath(triangle, relief.uv);
                });
        };

        std::map<std::tuple<long long, long long, int>, int> vertexByParameter;
        std::vector<Vector2> vertexParameters;
        std::vector<std::array<int, 3>> triangles;
        const auto vertexIndex = [&](Vector2 uv, Vector2 triangleCenter) {
            constexpr double quantization = 1.0e10;
            const double strong = bendAlongU_ ? uv.x : uv.y;
            const double weak = bendAlongU_ ? uv.y : uv.x;
            const double centerWeak
                = bendAlongU_ ? triangleCenter.y : triangleCenter.x;
            int slitSide = 0;
            for (const AlignedSlit& slit : alignedSlits) {
                constexpr double endpointTolerance = 1.0e-9;
                if (std::abs(weak - slit.weak) <= 1.0e-10
                    && strong > slit.minimumStrong + endpointTolerance
                    && strong < slit.maximumStrong - endpointTolerance) {
                    slitSide = centerWeak < slit.weak ? -1 : 1;
                    break;
                }
            }
            const auto key = std::tuple{
                std::llround(uv.x * quantization),
                std::llround(uv.y * quantization),
                slitSide,
            };
            const auto found = vertexByParameter.find(key);
            if (found != vertexByParameter.end()) {
                return found->second;
            }
            const int index = static_cast<int>(vertexParameters.size());
            vertexByParameter.emplace(key, index);
            vertexParameters.push_back(uv);
            return index;
        };

        std::function<void(const std::array<Vector2, 3>&, int)> emitTriangle;
        emitTriangle = [&](const std::array<Vector2, 3>& triangleUv, int depth) {
            if (depth < refinementDepth && touchesFeature(triangleUv)) {
                const std::array<Vector2, 3> midpoint{{
                    (triangleUv[0] + triangleUv[1]) * 0.5,
                    (triangleUv[1] + triangleUv[2]) * 0.5,
                    (triangleUv[2] + triangleUv[0]) * 0.5,
                }};
                emitTriangle({{triangleUv[0], midpoint[0], midpoint[2]}}, depth + 1);
                emitTriangle({{midpoint[0], triangleUv[1], midpoint[1]}}, depth + 1);
                emitTriangle({{midpoint[2], midpoint[1], triangleUv[2]}}, depth + 1);
                emitTriangle({{midpoint[0], midpoint[1], midpoint[2]}}, depth + 1);
                return;
            }
            const Vector2 center = (triangleUv[0] + triangleUv[1] + triangleUv[2])
                * (1.0 / 3.0);
            if (insideClosedCutout(center) || closeToOpenCut(center)) {
                return;
            }
            const std::array<int, 3> triangle{{
                vertexIndex(triangleUv[0], center),
                vertexIndex(triangleUv[1], center),
                vertexIndex(triangleUv[2], center),
            }};
            if (triangle[0] != triangle[1]
                && triangle[1] != triangle[2]
                && triangle[2] != triangle[0]) {
                triangles.push_back(triangle);
            }
        };

        for (std::size_t weak = 1; weak < weakCoordinates.size(); ++weak) {
            const double weak0 = weakCoordinates[weak - 1];
            const double weak1 = weakCoordinates[weak];
            for (std::size_t strong = 1;
                 strong < strongCoordinates.size(); ++strong) {
                const double strong0 = strongCoordinates[strong - 1];
                const double strong1 = strongCoordinates[strong];
                const Vector2 lowerLeft = mapper_.ToUv(strong0, weak0);
                const Vector2 lowerRight = mapper_.ToUv(strong1, weak0);
                const Vector2 upperLeft = mapper_.ToUv(strong0, weak1);
                const Vector2 upperRight = mapper_.ToUv(strong1, weak1);
                emitTriangle({{lowerLeft, lowerRight, upperRight}}, 0);
                emitTriangle({{lowerLeft, upperRight, upperLeft}}, 0);
            }
        }
        if (triangles.empty()) {
            throw std::invalid_argument(
                "Bent-sheet openings and cuts removed the whole paper skin.");
        }

        std::vector<Vector3> flatPositions;
        std::vector<Vector3> targetPositions;
        std::vector<Vector3> desiredPositions;
        std::vector<Vector3> positions;
        flatPositions.reserve(vertexParameters.size());
        targetPositions.reserve(vertexParameters.size());
        desiredPositions.resize(vertexParameters.size());
        positions.reserve(vertexParameters.size());
        for (const Vector2 uv : vertexParameters) {
            const Vector3 flat = flatWorld(uv);
            const Vector3 targetPoint = plate_.Evaluate(uv.x, uv.y, 0.5);
            flatPositions.push_back(flat);
            targetPositions.push_back(targetPoint);
        }

        // Interpolate local material frames and integrate their edge vectors.
        // This is the local/global idea used by as-rigid-as-possible shape
        // interpolation: computational triangles only sample one smooth
        // rotation field and cannot choose independent buckling directions.
        const auto strongOf = [&](Vector2 uv) {
            return bendAlongU_ ? uv.x : uv.y;
        };
        const auto weakOf = [&](Vector2 uv) {
            return bendAlongU_ ? uv.y : uv.x;
        };
        constexpr int fieldStrongIntervals = 64;
        constexpr int fieldWeakIntervals = 32;
        const int fieldRowSize = fieldStrongIntervals + 1;
        const auto fieldIndex = [&](int strong, int weak) {
            return weak * fieldRowSize + strong;
        };
        const std::size_t fieldVertexCount
            = static_cast<std::size_t>(fieldRowSize)
            * static_cast<std::size_t>(fieldWeakIntervals + 1);
        std::vector<Vector3> continuousFieldPositions;
        const bool completed = eased >= 1.0 - 1.0e-12;
        if (completed) {
            desiredPositions = targetPositions;
            positions = desiredPositions;
        } else if (eased <= 1.0e-12) {
            desiredPositions = flatPositions;
            positions = flatPositions;
        } else {
            struct EdgeVectorAverage {
                Vector3 sum;
                int count = 0;
            };
            std::map<std::pair<int, int>, EdgeVectorAverage> edgeVectors;
            const auto surfaceFrame = [&](Vector2 uv) {
                constexpr double delta = 1.0e-4;
                const double strong = strongOf(uv);
                const double weak = weakOf(uv);
                const Vector2 strongFirst = mapper_.ToUv(
                    std::max(0.0, strong - delta), weak);
                const Vector2 strongSecond = mapper_.ToUv(
                    std::min(1.0, strong + delta), weak);
                const Vector2 weakFirst = mapper_.ToUv(
                    strong, std::max(0.0, weak - delta));
                const Vector2 weakSecond = mapper_.ToUv(
                    strong, std::min(1.0, weak + delta));
                Vector3 targetNormal = PlateNormal(plate_, uv.x, uv.y);
                Vector3 targetX = plate_.Evaluate(
                    strongSecond.x, strongSecond.y, 0.5)
                    - plate_.Evaluate(strongFirst.x, strongFirst.y, 0.5);
                targetX = (targetX
                    - targetNormal * geometry::Dot(targetX, targetNormal)).Normalized();
                const Vector3 weakTangent = plate_.Evaluate(
                    weakSecond.x, weakSecond.y, 0.5)
                    - plate_.Evaluate(weakFirst.x, weakFirst.y, 0.5);
                Vector3 targetY = geometry::Cross(targetNormal, targetX).Normalized();
                if (geometry::Dot(targetY, weakTangent) < 0.0) {
                    targetY = -targetY;
                    targetNormal = -targetNormal;
                }
                return std::array<Vector3, 3>{{targetX, targetY, targetNormal}};
            };

            std::vector<Vector2> fieldUv(fieldVertexCount);
            std::vector<Vector3> fieldFlat(fieldVertexCount);
            std::vector<Vector3> fieldTarget(fieldVertexCount);
            continuousFieldPositions.resize(fieldVertexCount);
            std::vector<Vector3>& fieldPositions = continuousFieldPositions;
            Vector3 desiredCentroid;
            for (int weak = 0; weak <= fieldWeakIntervals; ++weak) {
                for (int strong = 0; strong <= fieldStrongIntervals; ++strong) {
                    const int index = fieldIndex(strong, weak);
                    const Vector2 uv = mapper_.ToUv(
                        static_cast<double>(strong) / fieldStrongIntervals,
                        static_cast<double>(weak) / fieldWeakIntervals);
                    fieldUv[static_cast<std::size_t>(index)] = uv;
                    fieldFlat[static_cast<std::size_t>(index)] = flatWorld(uv);
                    fieldTarget[static_cast<std::size_t>(index)]
                        = plate_.Evaluate(uv.x, uv.y, 0.5);
                    fieldPositions[static_cast<std::size_t>(index)]
                        = fieldFlat[static_cast<std::size_t>(index)] * (1.0 - eased)
                        + fieldTarget[static_cast<std::size_t>(index)] * eased;
                    desiredCentroid = desiredCentroid
                        + fieldPositions[static_cast<std::size_t>(index)];
                }
            }
            desiredCentroid = desiredCentroid
                / static_cast<double>(fieldVertexCount);

            const auto addFieldTriangle = [&](std::array<int, 3> triangle) {
                const Vector2 center = (
                    fieldUv[static_cast<std::size_t>(triangle[0])]
                    + fieldUv[static_cast<std::size_t>(triangle[1])]
                    + fieldUv[static_cast<std::size_t>(triangle[2])])
                    * (1.0 / 3.0);
                const auto targetFrame = surfaceFrame(center);
                const Quaternion rotation = InterpolateFromIdentity(
                    FrameRotation(
                        layoutX,
                        layoutY,
                        layoutNormal,
                        targetFrame[0],
                        targetFrame[1],
                        targetFrame[2]),
                    eased);
                for (int edge = 0; edge < 3; ++edge) {
                    const int first = triangle[static_cast<std::size_t>(edge)];
                    const int second = triangle[
                        static_cast<std::size_t>((edge + 1) % 3)];
                    const auto key = std::minmax(first, second);
                    Vector3 desired = Rotate(
                        rotation,
                        fieldFlat[static_cast<std::size_t>(second)]
                            - fieldFlat[static_cast<std::size_t>(first)]);
                    if (first != key.first) {
                        desired = -desired;
                    }
                    EdgeVectorAverage& average = edgeVectors[key];
                    average.sum = average.sum + desired;
                    ++average.count;
                }
            };
            for (int weak = 0; weak < fieldWeakIntervals; ++weak) {
                for (int strong = 0; strong < fieldStrongIntervals; ++strong) {
                    const int lowerLeft = fieldIndex(strong, weak);
                    const int lowerRight = fieldIndex(strong + 1, weak);
                    const int upperLeft = fieldIndex(strong, weak + 1);
                    const int upperRight = fieldIndex(strong + 1, weak + 1);
                    addFieldTriangle({{lowerLeft, lowerRight, upperRight}});
                    addFieldTriangle({{lowerLeft, upperRight, upperLeft}});
                }
            }
            struct ShapeEdge {
                int first = 0;
                int second = 0;
                Vector3 vector;
            };
            std::vector<ShapeEdge> shapeEdges;
            shapeEdges.reserve(edgeVectors.size());
            for (const auto& [key, average] : edgeVectors) {
                shapeEdges.push_back({
                    key.first,
                    key.second,
                    average.sum / static_cast<double>(average.count),
                });
            }
            std::vector<double> diagonal(fieldVertexCount, 0.0);
            std::vector<Vector3> rightHandSide(fieldVertexCount);
            for (const ShapeEdge& edge : shapeEdges) {
                diagonal[static_cast<std::size_t>(edge.first)] += 1.0;
                diagonal[static_cast<std::size_t>(edge.second)] += 1.0;
                rightHandSide[static_cast<std::size_t>(edge.first)]
                    = rightHandSide[static_cast<std::size_t>(edge.first)]
                    - edge.vector;
                rightHandSide[static_cast<std::size_t>(edge.second)]
                    = rightHandSide[static_cast<std::size_t>(edge.second)]
                    + edge.vector;
            }
            constexpr double anchorWeight = 10.0;
            diagonal.front() += anchorWeight;
            rightHandSide.front() = rightHandSide.front()
                + fieldPositions.front() * anchorWeight;
            const auto applySystem = [&](const std::vector<Vector3>& values) {
                std::vector<Vector3> result(values.size());
                for (std::size_t vertex = 0; vertex < values.size(); ++vertex) {
                    result[vertex] = values[vertex] * diagonal[vertex];
                }
                for (const ShapeEdge& edge : shapeEdges) {
                    result[static_cast<std::size_t>(edge.first)]
                        = result[static_cast<std::size_t>(edge.first)]
                        - values[static_cast<std::size_t>(edge.second)];
                    result[static_cast<std::size_t>(edge.second)]
                        = result[static_cast<std::size_t>(edge.second)]
                        - values[static_cast<std::size_t>(edge.first)];
                }
                return result;
            };
            const auto innerProduct = [](const std::vector<Vector3>& first,
                                          const std::vector<Vector3>& second) {
                double result = 0.0;
                for (std::size_t index = 0; index < first.size(); ++index) {
                    result += geometry::Dot(first[index], second[index]);
                }
                return result;
            };
            const std::vector<Vector3> initialSystem
                = applySystem(fieldPositions);
            std::vector<Vector3> residual(fieldVertexCount);
            std::vector<Vector3> preconditioned(fieldVertexCount);
            for (std::size_t vertex = 0; vertex < fieldVertexCount; ++vertex) {
                residual[vertex] = rightHandSide[vertex] - initialSystem[vertex];
                preconditioned[vertex] = residual[vertex] / diagonal[vertex];
            }
            std::vector<Vector3> direction = preconditioned;
            double residualProduct = innerProduct(residual, preconditioned);
            const double initialResidualProduct = residualProduct;
            const int maximumIterations = interactivePreview ? 350 : 700;
            for (int iteration = 0;
                 iteration < maximumIterations
                    && residualProduct > initialResidualProduct * 1.0e-18;
                 ++iteration) {
                const std::vector<Vector3> systemDirection
                    = applySystem(direction);
                const double denominator
                    = innerProduct(direction, systemDirection);
                if (std::abs(denominator) <= 1.0e-30) {
                    break;
                }
                const double step = residualProduct / denominator;
                for (std::size_t vertex = 0; vertex < fieldVertexCount; ++vertex) {
                    fieldPositions[vertex]
                        = fieldPositions[vertex] + direction[vertex] * step;
                    residual[vertex]
                        = residual[vertex] - systemDirection[vertex] * step;
                    preconditioned[vertex] = residual[vertex] / diagonal[vertex];
                }
                const double nextResidualProduct
                    = innerProduct(residual, preconditioned);
                if (nextResidualProduct <= initialResidualProduct * 1.0e-18) {
                    residualProduct = nextResidualProduct;
                    break;
                }
                const double directionScale
                    = nextResidualProduct / residualProduct;
                for (std::size_t vertex = 0; vertex < fieldVertexCount; ++vertex) {
                    direction[vertex] = preconditioned[vertex]
                        + direction[vertex] * directionScale;
                }
                residualProduct = nextResidualProduct;
            }
            Vector3 solvedCentroid;
            for (const Vector3 point : fieldPositions) {
                solvedCentroid = solvedCentroid + point;
            }
            const Vector3 translation = desiredCentroid
                - solvedCentroid / static_cast<double>(fieldVertexCount);
            for (Vector3& point : fieldPositions) {
                point = point + translation;
            }

            positions.resize(vertexParameters.size());
            for (std::size_t vertex = 0; vertex < vertexParameters.size(); ++vertex) {
                const double strongPosition = std::clamp(
                    strongOf(vertexParameters[vertex]), 0.0, 1.0)
                    * fieldStrongIntervals;
                const double weakPosition = std::clamp(
                    weakOf(vertexParameters[vertex]), 0.0, 1.0)
                    * fieldWeakIntervals;
                const int strongFirst = std::min(
                    static_cast<int>(std::floor(strongPosition)),
                    fieldStrongIntervals - 1);
                const int weakFirst = std::min(
                    static_cast<int>(std::floor(weakPosition)),
                    fieldWeakIntervals - 1);
                const int strongSecond = strongFirst + 1;
                const int weakSecond = weakFirst + 1;
                const double strongBlend = strongPosition - strongFirst;
                const double weakBlend = weakPosition - weakFirst;
                const Vector3 lower = fieldPositions[static_cast<std::size_t>(
                    fieldIndex(strongFirst, weakFirst))] * (1.0 - strongBlend)
                    + fieldPositions[static_cast<std::size_t>(
                        fieldIndex(strongSecond, weakFirst))] * strongBlend;
                const Vector3 upper = fieldPositions[static_cast<std::size_t>(
                    fieldIndex(strongFirst, weakSecond))] * (1.0 - strongBlend)
                    + fieldPositions[static_cast<std::size_t>(
                        fieldIndex(strongSecond, weakSecond))] * strongBlend;
                positions[vertex] = lower * (1.0 - weakBlend) + upper * weakBlend;
            }

            desiredPositions = positions;
        }

        if (continuousFieldPositions.empty()) {
            continuousFieldPositions.resize(fieldVertexCount);
            for (int weak = 0; weak <= fieldWeakIntervals; ++weak) {
                for (int strong = 0; strong <= fieldStrongIntervals; ++strong) {
                    const Vector2 uv = mapper_.ToUv(
                        static_cast<double>(strong) / fieldStrongIntervals,
                        static_cast<double>(weak) / fieldWeakIntervals);
                    const Vector3 flat = flatWorld(uv);
                    const Vector3 targetPoint = plate_.Evaluate(uv.x, uv.y, 0.5);
                    continuousFieldPositions[static_cast<std::size_t>(
                        fieldIndex(strong, weak))]
                        = flat * (1.0 - eased) + targetPoint * eased;
                }
            }
        }
        const auto evaluateContinuousField = [&](Vector2 uv) {
            const double strongPosition = std::clamp(strongOf(uv), 0.0, 1.0)
                * fieldStrongIntervals;
            const double weakPosition = std::clamp(weakOf(uv), 0.0, 1.0)
                * fieldWeakIntervals;
            const int strongFirst = std::min(
                static_cast<int>(std::floor(strongPosition)),
                fieldStrongIntervals - 1);
            const int weakFirst = std::min(
                static_cast<int>(std::floor(weakPosition)),
                fieldWeakIntervals - 1);
            const double strongBlend = strongPosition - strongFirst;
            const double weakBlend = weakPosition - weakFirst;
            const Vector3 lower = continuousFieldPositions[static_cast<std::size_t>(
                fieldIndex(strongFirst, weakFirst))] * (1.0 - strongBlend)
                + continuousFieldPositions[static_cast<std::size_t>(
                    fieldIndex(strongFirst + 1, weakFirst))] * strongBlend;
            const Vector3 upper = continuousFieldPositions[static_cast<std::size_t>(
                fieldIndex(strongFirst, weakFirst + 1))] * (1.0 - strongBlend)
                + continuousFieldPositions[static_cast<std::size_t>(
                    fieldIndex(strongFirst + 1, weakFirst + 1))] * strongBlend;
            return lower * (1.0 - weakBlend) + upper * weakBlend;
        };
        motion.preferContinuousModel = true;
        constexpr int sectionStep = 4;
        for (int weak = 0; weak <= fieldWeakIntervals; weak += sectionStep) {
            std::vector<Vector3> section;
            section.reserve(static_cast<std::size_t>(fieldStrongIntervals + 1));
            for (int strong = 0; strong <= fieldStrongIntervals; ++strong) {
                section.push_back(continuousFieldPositions[static_cast<std::size_t>(
                    fieldIndex(strong, weak))]);
            }
            motion.continuousSections.push_back(std::move(section));
        }
        for (const UvPath& opening : openings_) {
            PlateAssemblyMotionPath path;
            path.name = opening.name;
            path.points.reserve(opening.points.size());
            for (const Vector2 uv : opening.points) {
                path.points.push_back(evaluateContinuousField(uv));
            }
            motion.openingPaths.push_back(std::move(path));
        }
        for (const ReliefPath& relief : reliefs_) {
            PlateAssemblyMotionPath path;
            path.name = relief.name;
            path.points.reserve(relief.uv.size());
            for (const Vector2 uv : relief.uv) {
                path.points.push_back(evaluateContinuousField(uv));
            }
            motion.reliefCutPaths.push_back(std::move(path));
        }

        struct MaterialEdge {
            int first = 0;
            int second = 0;
            double restLength = 0.0;
        };
        std::set<std::pair<int, int>> materialEdgeKeys;
        std::vector<MaterialEdge> materialEdges;
        for (const auto& triangle : triangles) {
            for (int edge = 0; edge < 3; ++edge) {
                const auto key = std::minmax(
                    triangle[static_cast<std::size_t>(edge)],
                    triangle[static_cast<std::size_t>((edge + 1) % 3)]);
                if (materialEdgeKeys.insert(key).second) {
                    materialEdges.push_back({
                        key.first,
                        key.second,
                        (flatPositions[static_cast<std::size_t>(key.second)]
                            - flatPositions[static_cast<std::size_t>(key.first)]).Length(),
                    });
                }
            }
        }

        struct SeamConstraint {
            int first = 0;
            int second = 0;
            double openDistance = 0.0;
        };
        const auto nearestVertex = [&](Vector2 uv) {
            const Vector2 flatPoint = mapper_.Map(uv);
            int nearest = -1;
            double distance = std::numeric_limits<double>::infinity();
            for (int vertex = 0;
                 vertex < static_cast<int>(vertexParameters.size()); ++vertex) {
                const double candidate = Length(
                    mapper_.Map(vertexParameters[static_cast<std::size_t>(vertex)])
                    - flatPoint);
                if (candidate < distance) {
                    nearest = vertex;
                    distance = candidate;
                }
            }
            return std::pair{nearest, distance};
        };
        const auto interpolatePath = [](const std::vector<Vector2>& path,
                                         double index) {
            const std::size_t first = std::min(
                static_cast<std::size_t>(std::floor(index)), path.size() - 2);
            const std::size_t second = first + 1;
            const double blend = index - static_cast<double>(first);
            return path[first] * (1.0 - blend) + path[second] * blend;
        };
        std::set<std::pair<int, int>> seamKeys;
        std::set<int> seamVertices;
        std::vector<SeamConstraint> seamConstraints;
        const double seamSearchTolerance = std::max(
            0.5,
            target / std::pow(2.0, refinementDepth) * 2.5);
        for (const ReliefPath& relief : reliefs_) {
            if (!relief.closedCutout || relief.uv.size() < 5) {
                continue;
            }
            const bool strongIsU = bendAlongU_;
            const double sideStrong = strongIsU
                ? relief.uv.front().x
                : relief.uv.front().y;
            std::size_t tip = 0;
            double tipDistance = -1.0;
            for (std::size_t point = 0; point < relief.uv.size(); ++point) {
                const double strong = strongIsU
                    ? relief.uv[point].x
                    : relief.uv[point].y;
                const double distance = std::abs(strong - sideStrong);
                if (distance > tipDistance) {
                    tipDistance = distance;
                    tip = point;
                }
            }
            if (tip == 0 || tip + 1 >= relief.uv.size()) {
                continue;
            }
            constexpr int seamSamples = 12;
            for (int sample = 1; sample <= seamSamples; ++sample) {
                const double fraction
                    = static_cast<double>(sample) / seamSamples;
                const Vector2 firstPoint = interpolatePath(
                    relief.uv,
                    static_cast<double>(tip) * (1.0 - fraction));
                const Vector2 secondPoint = interpolatePath(
                    relief.uv,
                    static_cast<double>(tip)
                        + static_cast<double>(relief.uv.size() - 1 - tip)
                            * fraction);
                const auto first = nearestVertex(firstPoint);
                const auto second = nearestVertex(secondPoint);
                if (first.first < 0 || second.first < 0
                    || first.first == second.first
                    || first.second > seamSearchTolerance
                    || second.second > seamSearchTolerance) {
                    continue;
                }
                const auto key = std::minmax(first.first, second.first);
                const bool sharesTriangle = std::any_of(
                    triangles.begin(), triangles.end(), [&](const auto& triangle) {
                        return std::find(
                                   triangle.begin(), triangle.end(), key.first)
                                != triangle.end()
                            && std::find(
                                   triangle.begin(), triangle.end(), key.second)
                                != triangle.end();
                    });
                if (materialEdgeKeys.contains(key)
                    || sharesTriangle
                    || seamVertices.contains(key.first)
                    || seamVertices.contains(key.second)
                    || !seamKeys.insert(key).second) {
                    continue;
                }
                seamVertices.insert(key.first);
                seamVertices.insert(key.second);
                seamConstraints.push_back({
                    key.first,
                    key.second,
                    (flatPositions[static_cast<std::size_t>(key.second)]
                        - flatPositions[static_cast<std::size_t>(key.first)]).Length(),
                });
            }
        }

        const bool lockClosedSeams = completed;
        std::vector<int> seamMate(positions.size(), -1);
        if (lockClosedSeams) {
            for (const SeamConstraint& seam : seamConstraints) {
                seamMate[static_cast<std::size_t>(seam.first)] = seam.second;
                seamMate[static_cast<std::size_t>(seam.second)] = seam.first;
                const Vector3 midpoint = (
                    positions[static_cast<std::size_t>(seam.first)]
                    + positions[static_cast<std::size_t>(seam.second)]) * 0.5;
                positions[static_cast<std::size_t>(seam.first)] = midpoint;
                positions[static_cast<std::size_t>(seam.second)] = midpoint;
            }
        }

        const auto translateVertexGroup = [&](int vertex, Vector3 translation) {
            positions[static_cast<std::size_t>(vertex)]
                = positions[static_cast<std::size_t>(vertex)] + translation;
            const int mate = seamMate[static_cast<std::size_t>(vertex)];
            if (mate >= 0) {
                positions[static_cast<std::size_t>(mate)]
                    = positions[static_cast<std::size_t>(mate)] + translation;
            }
        };

        const auto projectDistance = [&](int first,
                                          int second,
                                          double desiredLength,
                                          double stiffness) {
            if (seamMate[static_cast<std::size_t>(first)] == second) {
                return;
            }
            Vector3 delta = positions[static_cast<std::size_t>(second)]
                - positions[static_cast<std::size_t>(first)];
            const double length = delta.Length();
            if (length <= 1.0e-12) {
                return;
            }
            const double firstInverseMass
                = seamMate[static_cast<std::size_t>(first)] >= 0 ? 0.5 : 1.0;
            const double secondInverseMass
                = seamMate[static_cast<std::size_t>(second)] >= 0 ? 0.5 : 1.0;
            const double inverseMassSum = firstInverseMass + secondInverseMass;
            const Vector3 correction = delta
                * ((length - desiredLength) / length * stiffness);
            translateVertexGroup(
                first, correction * (firstInverseMass / inverseMassSum));
            translateVertexGroup(
                second, -correction * (secondInverseMass / inverseMassSum));
        };
        if (eased > 1.0e-12 && !completed) {
            const int smoothLengthIterations
                = options_.papercraftFidelity >= 8 ? 0 : 24;
            for (int iteration = 0; iteration < smoothLengthIterations; ++iteration) {
                for (const MaterialEdge& edge : materialEdges) {
                    projectDistance(
                        edge.first, edge.second, edge.restLength, 0.35);
                }
                for (std::size_t vertex = 0; vertex < positions.size(); ++vertex) {
                    positions[vertex] = positions[vertex]
                        + (desiredPositions[vertex] - positions[vertex]) * 0.015;
                }
            }
        }
        if (eased > 1.0e-12 && completed) {
            const int iterations = interactivePreview
                ? (completed ? 120 : 64)
                : (completed
                    ? std::clamp(
                        200 + options_.papercraftFidelity * 20, 220, 400)
                    : std::clamp(
                        110 + options_.papercraftFidelity * 8, 120, 190));
            std::vector<bool> attracted(positions.size(), false);
            for (int iteration = 0; iteration < iterations; ++iteration) {
                const double attraction = completed
                    ? std::clamp(
                        0.025 + options_.papercraftFidelity * 0.0075,
                        0.03,
                        0.10)
                    : std::clamp(
                        0.16 + options_.papercraftFidelity * 0.006,
                        0.16,
                        0.22);
                std::fill(attracted.begin(), attracted.end(), false);
                for (std::size_t vertex = 0; vertex < positions.size(); ++vertex) {
                    if (attracted[vertex]) {
                        continue;
                    }
                    const Vector3 desired = desiredPositions[vertex];
                    const int mate = seamMate[vertex];
                    if (mate >= 0) {
                        const Vector3 mateDesired
                            = desiredPositions[static_cast<std::size_t>(mate)];
                        translateVertexGroup(
                            static_cast<int>(vertex),
                            ((desired + mateDesired) * 0.5 - positions[vertex])
                                * attraction);
                        attracted[static_cast<std::size_t>(mate)] = true;
                    } else {
                        translateVertexGroup(
                            static_cast<int>(vertex),
                            (desired - positions[vertex]) * attraction);
                    }
                    attracted[vertex] = true;
                }
                for (const SeamConstraint& seam : seamConstraints) {
                    projectDistance(
                        seam.first,
                        seam.second,
                        seam.openDistance * (1.0 - eased),
                        1.0);
                }
                for (int sweep = 0; sweep < 3; ++sweep) {
                    for (const MaterialEdge& edge : materialEdges) {
                        projectDistance(
                            edge.first, edge.second, edge.restLength, 1.0);
                    }
                }
            }
            const int settlingIterations = interactivePreview
                ? (completed ? 160 : 24)
                : (completed ? 600 : 60);
            for (int iteration = 0; iteration < settlingIterations; ++iteration) {
                for (const SeamConstraint& seam : seamConstraints) {
                    projectDistance(
                        seam.first,
                        seam.second,
                        seam.openDistance * (1.0 - eased),
                        0.35);
                }
                for (int sweep = 0; sweep < 3; ++sweep) {
                    for (const MaterialEdge& edge : materialEdges) {
                        projectDistance(
                            edge.first, edge.second, edge.restLength, 1.0);
                    }
                }
                if (!completed) {
                    for (std::size_t vertex = 0; vertex < positions.size(); ++vertex) {
                        translateVertexGroup(
                            static_cast<int>(vertex),
                            (desiredPositions[vertex] - positions[vertex]) * 0.025);
                    }
                }
            }
            const int materialIterations = interactivePreview
                ? (completed ? 60 : 80)
                : (completed ? 150 : 240);
            for (int iteration = 0; iteration < materialIterations; ++iteration) {
                for (const MaterialEdge& edge : materialEdges) {
                    projectDistance(
                        edge.first, edge.second, edge.restLength, 1.0);
                }
            }
        }

        double squaredSeamMismatch = 0.0;
        for (const SeamConstraint& seam : seamConstraints) {
            const double desired = seam.openDistance * (1.0 - eased);
            const double mismatch = std::abs((
                positions[static_cast<std::size_t>(seam.second)]
                - positions[static_cast<std::size_t>(seam.first)]).Length()
                - desired);
            motion.maximumSeamMismatchMillimeters = std::max(
                motion.maximumSeamMismatchMillimeters, mismatch);
            squaredSeamMismatch += mismatch * mismatch;
        }
        if (!seamConstraints.empty()) {
            motion.rootMeanSquareSeamMismatchMillimeters = std::sqrt(
                squaredSeamMismatch / static_cast<double>(seamConstraints.size()));
        }

        motion.panels.reserve(triangles.size());
        motion.pieceIndices.reserve(triangles.size());
        motion.panelThicknessMillimeters.reserve(triangles.size());
        motion.panelDeviationMillimeters.reserve(triangles.size());
        std::vector<std::vector<Vector3>> outputVertexPositions(
            vertexParameters.size());
        double squaredOutputTargetMismatch = 0.0;
        std::size_t outputTargetSampleCount = 0;
        double squaredOutputEdgeError = 0.0;
        std::size_t outputEdgeCount = 0;
        for (const auto& triangle : triangles) {
            std::array<Vector3, 3> referencePanel;
            std::array<Vector3, 3> flatPanel;
            std::array<Vector3, 3> targetPanel;
            Vector2 center;
            double deviation = 0.0;
            for (int corner = 0; corner < 3; ++corner) {
                const std::size_t vertex = static_cast<std::size_t>(
                    triangle[static_cast<std::size_t>(corner)]);
                referencePanel[static_cast<std::size_t>(corner)]
                    = positions[vertex];
                flatPanel[static_cast<std::size_t>(corner)]
                    = flatPositions[vertex];
                targetPanel[static_cast<std::size_t>(corner)]
                    = targetPositions[vertex];
                center = center + vertexParameters[vertex] * (1.0 / 3.0);
            }
            const std::array<Vector3, 3> panel = referencePanel;
            for (int corner = 0; corner < 3; ++corner) {
                deviation = std::max(
                    deviation,
                    (panel[static_cast<std::size_t>(corner)]
                        - targetPanel[static_cast<std::size_t>(corner)]).Length());
                const double targetMismatch = (
                    panel[static_cast<std::size_t>(corner)]
                    - targetPanel[static_cast<std::size_t>(corner)]).Length();
                motion.maximumTargetMismatchMillimeters = std::max(
                    motion.maximumTargetMismatchMillimeters, targetMismatch);
                squaredOutputTargetMismatch += targetMismatch * targetMismatch;
                ++outputTargetSampleCount;
                const std::size_t vertex = static_cast<std::size_t>(
                    triangle[static_cast<std::size_t>(corner)]);
                outputVertexPositions[vertex].push_back(
                    panel[static_cast<std::size_t>(corner)]);
            }
            const Vector3 targetCenter = plate_.Evaluate(
                center.x, center.y, 0.5);
            const Vector3 panelCenter
                = (panel[0] + panel[1] + panel[2]) * (1.0 / 3.0);
            deviation = std::max(deviation, (panelCenter - targetCenter).Length());
            const double centerMismatch = (panelCenter - targetCenter).Length();
            motion.maximumTargetMismatchMillimeters = std::max(
                motion.maximumTargetMismatchMillimeters, centerMismatch);
            squaredOutputTargetMismatch += centerMismatch * centerMismatch;
            ++outputTargetSampleCount;
            motion.panels.push_back(panel);
            motion.pieceIndices.push_back(0);
            motion.panelThicknessMillimeters.push_back(plate_.Thickness(center.y));
            motion.panelDeviationMillimeters.push_back(deviation);
            motion.maximumPanelDeviationMillimeters = std::max(
                motion.maximumPanelDeviationMillimeters, deviation);
            motion.materialAreaSquareMillimeters += geometry::Cross(
                flatPanel[1] - flatPanel[0],
                flatPanel[2] - flatPanel[0]).Length() * 0.5;
            for (int edge = 0; edge < 3; ++edge) {
                const int next = (edge + 1) % 3;
                const double error = std::abs((
                    panel[static_cast<std::size_t>(next)]
                    - panel[static_cast<std::size_t>(edge)]).Length()
                    - (flatPanel[static_cast<std::size_t>(next)]
                        - flatPanel[static_cast<std::size_t>(edge)]).Length());
                motion.maximumMaterialEdgeErrorMillimeters = std::max(
                    motion.maximumMaterialEdgeErrorMillimeters, error);
                squaredOutputEdgeError += error * error;
                ++outputEdgeCount;
            }
        }
        if (outputEdgeCount > 0) {
            motion.rootMeanSquareMaterialEdgeErrorMillimeters = std::sqrt(
                squaredOutputEdgeError / static_cast<double>(outputEdgeCount));
        }
        if (outputTargetSampleCount > 0) {
            motion.rootMeanSquareTargetMismatchMillimeters = std::sqrt(
                squaredOutputTargetMismatch
                / static_cast<double>(outputTargetSampleCount));
        }
        double squaredConnectionMismatch = 0.0;
        std::size_t connectionSampleCount = 0;
        for (const auto& occurrences : outputVertexPositions) {
            if (occurrences.size() < 2) {
                continue;
            }
            Vector3 center;
            for (const Vector3 point : occurrences) {
                center = center + point;
            }
            center = center / static_cast<double>(occurrences.size());
            for (const Vector3 point : occurrences) {
                const double mismatch = (point - center).Length();
                motion.maximumPanelConnectionMismatchMillimeters = std::max(
                    motion.maximumPanelConnectionMismatchMillimeters,
                    mismatch);
                squaredConnectionMismatch += mismatch * mismatch;
                ++connectionSampleCount;
            }
        }
        if (connectionSampleCount > 0) {
            motion.rootMeanSquarePanelConnectionMismatchMillimeters = std::sqrt(
                squaredConnectionMismatch
                / static_cast<double>(connectionSampleCount));
        }
        return motion;
    }

private:
    [[nodiscard]] bool ChooseBendDirection() const
    {
        if (options_.cutDirection == PapercraftCutDirection::Vertical) {
            return true;
        }
        if (options_.cutDirection == PapercraftCutDirection::Horizontal) {
            return false;
        }
        return DirectionNormalChange(plate_, true)
            >= DirectionNormalChange(plate_, false);
    }

    void ValidateOptions() const
    {
        if (options_.papercraftFidelity < 1 || options_.papercraftFidelity > 10
            || options_.openingSamples < 8 || options_.openingSamples > 100000
            || !std::isfinite(options_.minimumFoldAngleDegrees)
            || options_.minimumFoldAngleDegrees < 0.0
            || !std::isfinite(options_.reliefCutDepthRatio)
            || options_.reliefCutDepthRatio <= 0.0
            || options_.reliefCutDepthRatio >= 1.0
            || !std::isfinite(options_.reliefCutSpacingMillimeters)
            || options_.reliefCutSpacingMillimeters <= 0.0
            || !std::isfinite(options_.reliefNotchAngleDegrees)
            || options_.reliefNotchAngleDegrees <= 0.0
            || options_.reliefNotchAngleDegrees >= 170.0
            || !std::isfinite(options_.reliefNotchCurveStrength)
            || options_.reliefNotchCurveStrength < 0.0
            || options_.reliefNotchCurveStrength > 1.0
            || !std::isfinite(options_.maximumShapeErrorMillimeters)
            || options_.maximumShapeErrorMillimeters <= 0.0
            || options_.maximumShapeErrorMillimeters > 100.0
            || !std::isfinite(options_.minimumPartWidthMillimeters)
            || options_.minimumPartWidthMillimeters <= 0.0
            || options_.minimumPartWidthMillimeters > 1000.0) {
            throw std::invalid_argument(
                "Bent-sheet papercraft options are invalid.");
        }
    }

    void ReadFeaturePaths()
    {
        const int featureSamples = std::max(
            options_.openingSamples,
            192 + options_.papercraftFidelity * 24);
        if (options_.includeOpenings) {
            for (const std::string& name : namedPlate_.openingWireNames) {
                openings_.push_back({name, BuildPlateWireUvPath(
                    project_, namedPlate_, name, featureSamples, true)});
            }
        }
        for (const std::string& name : namedPlate_.splitWireNames) {
            manualCuts_.push_back({name, BuildPlateWireUvPath(
                project_, namedPlate_, name, featureSamples, false)});
        }
    }

    void BuildOuterBoundary()
    {
        piece_.name = "continuous_skin_1";
        piece_.outerBoundary.name = piece_.name;
        const int samples = std::max(128, options_.papercraftFidelity * 28);
        for (int sample = 0; sample <= samples; ++sample) {
            const double weak = static_cast<double>(sample) / samples;
            AppendUnique(piece_.outerBoundary.points,
                mapper_.Map(mapper_.ToUv(0.0, weak)));
        }
        for (int sample = 1; sample <= samples; ++sample) {
            const double strong = static_cast<double>(sample) / samples;
            AppendUnique(piece_.outerBoundary.points,
                mapper_.Map(mapper_.ToUv(strong, 1.0)));
        }
        for (int sample = 1; sample <= samples; ++sample) {
            const double weak = 1.0 - static_cast<double>(sample) / samples;
            AppendUnique(piece_.outerBoundary.points,
                mapper_.Map(mapper_.ToUv(1.0, weak)));
        }
        for (int sample = 1; sample <= samples; ++sample) {
            const double strong = 1.0 - static_cast<double>(sample) / samples;
            AppendUnique(piece_.outerBoundary.points,
                mapper_.Map(mapper_.ToUv(strong, 0.0)));
        }
        ClosePath(piece_.outerBoundary.points);
    }

    void BuildOpenings()
    {
        for (const UvPath& opening : openings_) {
            PlateFlatPatternPath flat;
            flat.name = opening.name;
            flat.points.reserve(opening.points.size());
            for (const Vector2 uv : opening.points) {
                AppendUnique(flat.points, mapper_.Map(uv));
            }
            ClosePath(flat.points);
            piece_.openings.push_back(std::move(flat));
        }
    }

    [[nodiscard]] bool PointInsideOpening(Vector2 uv) const
    {
        return std::any_of(openings_.begin(), openings_.end(),
            [&](const UvPath& opening) {
                return PointInsidePath(uv, opening.points);
            });
    }

    [[nodiscard]] bool PathTouchesOpening(const std::vector<Vector2>& path) const
    {
        for (const UvPath& opening : openings_) {
            if (std::any_of(path.begin(), path.end(),
                    [&](Vector2 point) {
                        return PointInsidePath(point, opening.points);
                    })) {
                return true;
            }
            for (std::size_t first = 1; first < path.size(); ++first) {
                for (std::size_t second = 1;
                     second < opening.points.size(); ++second) {
                    if (SegmentsIntersect(
                            path[first - 1], path[first],
                            opening.points[second - 1], opening.points[second])) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] double SecondaryBendAngle(double weak, double halfSpan) const
    {
        const double firstWeak = std::max(0.001, weak - halfSpan);
        const double secondWeak = std::min(0.999, weak + halfSpan);
        double maximum = 0.0;
        for (int sample = 1; sample <= 7; ++sample) {
            const double strong = static_cast<double>(sample) / 8.0;
            const Vector2 first = mapper_.ToUv(strong, firstWeak);
            const Vector2 second = mapper_.ToUv(strong, secondWeak);
            maximum = std::max(maximum, NormalAngleDegrees(
                PlateNormal(plate_, first.x, first.y),
                PlateNormal(plate_, second.x, second.y)));
        }
        return maximum;
    }

    [[nodiscard]] std::vector<Vector2> BuildNotchUv(
        bool fromMinimumStrong,
        double weak,
        double depthRatio,
        double halfMouthParameter) const
    {
        const double side = fromMinimumStrong ? 0.0 : 1.0;
        const double tip = fromMinimumStrong ? depthRatio : 1.0 - depthRatio;
        const double curveStrength
            = options_.notchStyle == ReliefNotchStyle::CurvedV
            ? options_.reliefNotchCurveStrength
            : 0.0;
        if (options_.notchStyle == ReliefNotchStyle::SharpV) {
            return {
                mapper_.ToUv(side, weak - halfMouthParameter),
                mapper_.ToUv(tip, weak),
                mapper_.ToUv(side, weak + halfMouthParameter),
            };
        }

        std::vector<Vector2> path;
        constexpr int sideSamples = 16;
        for (int sample = 0; sample <= sideSamples; ++sample) {
            const double t = static_cast<double>(sample) / sideSamples;
            const double strong = side * (1.0 - t) + tip * t;
            const double baseWeak
                = weak - halfMouthParameter * (1.0 - t);
            const double bow = -halfMouthParameter * 0.35 * curveStrength
                * std::sin(kPi * t);
            path.push_back(mapper_.ToUv(
                strong, std::clamp(baseWeak + bow, 0.0, 1.0)));
        }
        for (int sample = 1; sample <= sideSamples; ++sample) {
            const double t = static_cast<double>(sample) / sideSamples;
            const double strong = tip * (1.0 - t) + side * t;
            const double baseWeak = weak + halfMouthParameter * t;
            const double bow = halfMouthParameter * 0.35 * curveStrength
                * std::sin(kPi * t);
            path.push_back(mapper_.ToUv(
                strong, std::clamp(baseWeak + bow, 0.0, 1.0)));
        }
        return path;
    }

    bool AddVNotch(
        bool fromMinimumStrong,
        double weak,
        double maximumWeakShift,
        double depthScale,
        int index)
    {
        const double maximumDepth = std::min(options_.reliefCutDepthRatio, 0.46)
            * std::clamp(depthScale, 0.20, 1.0);
        for (const double shiftScale : {0.0, -0.45, 0.45, -0.80, 0.80}) {
            const double shiftedWeak = std::clamp(
                weak + maximumWeakShift * shiftScale, 0.01, 0.99);
            for (const double scale : {1.0, 0.78, 0.58, 0.40, 0.25, 0.14}) {
                const double depth = maximumDepth * scale;
                const double depthMillimeters
                    = mapper_.StrongLength(shiftedWeak) * depth;
                const double halfMouthMillimeters = std::clamp(
                    depthMillimeters
                        * std::tan(options_.reliefNotchAngleDegrees * kPi / 360.0),
                    0.35,
                    std::max(0.4, mapper_.WeakLength() * 0.08));
                const double halfMouthParameter = std::min(
                    halfMouthMillimeters / std::max(mapper_.WeakLength(), 1.0e-9),
                    std::min(shiftedWeak - 0.005, 0.995 - shiftedWeak));
                if (halfMouthParameter <= 1.0e-4) {
                    continue;
                }
                std::vector<Vector2> uv = BuildNotchUv(
                    fromMinimumStrong,
                    shiftedWeak,
                    depth,
                    halfMouthParameter);
                if (PathTouchesOpening(uv)) {
                    continue;
                }
                std::vector<Vector2> flat;
                flat.reserve(uv.size());
                for (const Vector2 point : uv) {
                    flat.push_back(mapper_.Map(point));
                }
                if (!IncorporateNotchInBoundary(
                        piece_.outerBoundary.points, flat)) {
                    continue;
                }
                PlateFlatPatternPath flatPath{
                    "bent_" + std::string(
                        options_.notchStyle == ReliefNotchStyle::CurvedV
                            ? "curved_v_" : "sharp_v_")
                        + std::to_string(index),
                    flat,
                    true,
                };
                flatPath.cutKind = PapercraftCutKind::NonSeparatingReliefCut;
                piece_.reliefCuts.push_back(flatPath);
                reliefs_.push_back({
                    flatPath.name,
                    std::move(uv),
                    std::move(flatPath),
                    true,
                    false,
                });
                ++automaticReliefCount_;
                return true;
            }
        }
        return false;
    }

    void AddBridgedSlit(double weak, int index)
    {
        constexpr int samples = 192;
        const double strongLength = mapper_.StrongLength(weak);
        const double bridgeMillimeters = std::clamp(
            std::max(strongLength * 0.10, options_.minimumPartWidthMillimeters),
            std::min(1.5, strongLength * 0.25),
            std::max(4.0, strongLength * 0.25));
        const double bridgeHalf = bridgeMillimeters
            / std::max(2.0 * strongLength, 1.0e-9);
        const double bridgeCenter = index % 2 == 0 ? 0.43 : 0.57;
        std::vector<Vector2> current;
        auto flush = [&]() {
            if (current.size() < 2) {
                current.clear();
                return;
            }
            PlateFlatPatternPath flat;
            flat.name = "bent_slit_" + std::to_string(index)
                + "_" + std::to_string(piece_.reliefCuts.size() + 1);
            flat.points.reserve(current.size());
            for (const Vector2 uv : current) {
                flat.points.push_back(mapper_.Map(uv));
            }
            if (Length(flat.points.back() - flat.points.front()) >= 0.5) {
                flat.cutKind = PapercraftCutKind::NonSeparatingReliefCut;
                piece_.reliefCuts.push_back(flat);
                reliefs_.push_back({
                    flat.name, current, std::move(flat), false, false,
                });
                ++automaticReliefCount_;
            }
            current.clear();
        };
        for (int sample = 0; sample <= samples; ++sample) {
            const double strong = 0.035 + 0.93 * sample / samples;
            const bool bridge = std::abs(strong - bridgeCenter) < bridgeHalf;
            const Vector2 uv = mapper_.ToUv(strong, weak);
            if (bridge || PointInsideOpening(uv)) {
                flush();
                continue;
            }
            current.push_back(uv);
        }
        flush();
    }

    void BuildAutomaticReliefs()
    {
        if (!options_.includeAutomaticReliefCuts
            || plate_.AnalyzeDevelopability().classification
                != PlateDevelopability::DoubleCurved) {
            return;
        }
        const double fidelityParameter
            = static_cast<double>(options_.papercraftFidelity - 1) / 9.0;
        double spacing = options_.fidelityControlsFeatureSpacing
            ? 13.0 * std::pow(2.2 / 13.0, fidelityParameter)
            : options_.reliefCutSpacingMillimeters;
        const double priorityScale
            = options_.panelPriority == PapercraftPanelPriority::PartFirst
            ? 1.35
            : options_.panelPriority == PapercraftPanelPriority::Balanced
            ? 1.0
            : 0.72;
        const double toleranceScale = std::clamp(
            std::sqrt(options_.maximumShapeErrorMillimeters / 0.10),
            0.45,
            2.0);
        spacing *= priorityScale * toleranceScale;
        const int stationLimit
            = options_.panelPriority == PapercraftPanelPriority::PartFirst
            ? 10
            : options_.panelPriority == PapercraftPanelPriority::Balanced
            ? 15
            : 20;
        const int stationCount = std::clamp(
            static_cast<int>(std::floor(mapper_.WeakLength() / spacing)),
            1, stationLimit);
        const double halfSpan = 0.42 / (stationCount + 1);
        int stationIndex = 0;
        for (int station = 1; station <= stationCount; ++station) {
            const double weak = static_cast<double>(station) / (stationCount + 1);
            if (SecondaryBendAngle(weak, halfSpan) + 1.0e-9
                < options_.minimumFoldAngleDegrees) {
                continue;
            }
            ++stationIndex;
            const bool useV = options_.allowAutomaticNotches
                && stationIndex == 1;
            if (useV) {
                const double depthScale = 1.0
                    / static_cast<double>(stationCount);
                const bool first = AddVNotch(
                    true,
                    weak,
                    halfSpan,
                    depthScale,
                    stationIndex * 2 - 1);
                const bool second = AddVNotch(
                    false,
                    weak,
                    halfSpan,
                    depthScale,
                    stationIndex * 2);
                if (!first && !second) {
                    AddBridgedSlit(weak, stationIndex);
                }
            } else {
                AddBridgedSlit(weak, stationIndex);
            }
        }
        if (automaticReliefCount_ == 0) {
            AddBridgedSlit(0.5, 1);
        }
    }

    void AddManualCuts()
    {
        for (const UvPath& cut : manualCuts_) {
            PlateFlatPatternPath flat;
            flat.name = cut.name;
            flat.points.reserve(cut.points.size());
            for (const Vector2 uv : cut.points) {
                flat.points.push_back(mapper_.Map(uv));
            }
            flat.cutKind = PapercraftCutKind::SeparatingSeam;
            piece_.reliefCuts.push_back(flat);
            reliefs_.push_back({
                cut.name, cut.points, std::move(flat), false, true,
            });
        }
    }

    void MeasureFlatDistortion()
    {
        constexpr int strongIntervals = 32;
        constexpr int weakIntervals = 16;
        double squared = 0.0;
        std::size_t count = 0;
        const auto measure = [&](Vector2 firstUv, Vector2 secondUv) {
            const double spatial = (
                plate_.Evaluate(firstUv.x, firstUv.y, 0.5)
                - plate_.Evaluate(secondUv.x, secondUv.y, 0.5)).Length();
            const double flat = Length(
                mapper_.Map(firstUv) - mapper_.Map(secondUv));
            const double error = std::abs(flat - spatial);
            maximumEdgeError_ = std::max(maximumEdgeError_, error);
            squared += error * error;
            ++count;
        };
        for (int weak = 0; weak <= weakIntervals; ++weak) {
            const double w = static_cast<double>(weak) / weakIntervals;
            for (int strong = 0; strong < strongIntervals; ++strong) {
                measure(
                    mapper_.ToUv(static_cast<double>(strong) / strongIntervals, w),
                    mapper_.ToUv(static_cast<double>(strong + 1) / strongIntervals, w));
            }
        }
        for (int strong = 0; strong <= strongIntervals; ++strong) {
            const double s = static_cast<double>(strong) / strongIntervals;
            for (int weak = 0; weak < weakIntervals; ++weak) {
                measure(
                    mapper_.ToUv(s, static_cast<double>(weak) / weakIntervals),
                    mapper_.ToUv(s, static_cast<double>(weak + 1) / weakIntervals));
            }
        }
        if (count > 0) {
            rmsEdgeError_ = std::sqrt(squared / static_cast<double>(count));
        }
    }

    const Project& project_;
    const NamedPlate& namedPlate_;
    const Plate& plate_;
    PlateFlatPatternOptions options_;
    bool bendAlongU_ = true;
    ArcLengthMapper mapper_;
    std::vector<UvPath> openings_;
    std::vector<UvPath> manualCuts_;
    PlateFlatPatternPiece piece_;
    std::vector<ReliefPath> reliefs_;
    int automaticReliefCount_ = 0;
    double maximumEdgeError_ = 0.0;
    double rmsEdgeError_ = 0.0;
};

} // namespace

PlateFlatPattern BuildBentSheetPapercraftPattern(
    const Project& project,
    const NamedPlate& plate,
    PlateFlatPatternOptions options)
{
    options.includeAutomaticReliefCuts = true;
    return BentSheetGenerator(project, plate, std::move(options)).Pattern();
}

PlateAssemblyGuide BuildBentSheetPapercraftGuide(
    const Project& project,
    const NamedPlate& plate,
    PlateFlatPatternOptions options)
{
    options.includeAutomaticReliefCuts = true;
    return BentSheetGenerator(project, plate, std::move(options)).Guide(1.0);
}

PlateAssemblyMotion BuildBentSheetPapercraftMotion(
    const Project& project,
    const NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options)
{
    options.includeAutomaticReliefCuts = true;
    return BentSheetGenerator(project, plate, std::move(options)).Motion(progress);
}

BentSheetPapercraftPreview BuildBentSheetPapercraftPreview(
    const Project& project,
    const NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options)
{
    options.includeAutomaticReliefCuts = true;
    BentSheetGenerator generator(project, plate, std::move(options));
    return {generator.Guide(progress), generator.Motion(progress, true)};
}

} // namespace kachakacha::io
