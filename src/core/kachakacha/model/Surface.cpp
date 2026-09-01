#include "kachakacha/model/Surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace kachakacha::model {

using geometry::Cross;
using geometry::Dot;
using geometry::Vector3;

namespace {

constexpr double kParameterStep = 1.0e-5;

struct PlanePoint {
    double u = 0.0;
    double v = 0.0;
};

PlanePoint operator-(PlanePoint first, PlanePoint second)
{
    return {first.u - second.u, first.v - second.v};
}

double Cross2(PlanePoint first, PlanePoint second)
{
    return first.u * second.v - first.v * second.u;
}

double Dot2(PlanePoint first, PlanePoint second)
{
    return first.u * second.u + first.v * second.v;
}

bool NonAdjacentSegmentsIntersect(
    PlanePoint firstStart,
    PlanePoint firstEnd,
    PlanePoint secondStart,
    PlanePoint secondEnd,
    double tolerance)
{
    const PlanePoint firstDirection = firstEnd - firstStart;
    const PlanePoint secondDirection = secondEnd - secondStart;
    const PlanePoint delta = secondStart - firstStart;
    const double denominator = Cross2(firstDirection, secondDirection);
    const double scale = std::max({
        1.0,
        std::hypot(firstDirection.u, firstDirection.v),
        std::hypot(secondDirection.u, secondDirection.v),
    });
    if (std::abs(denominator) > tolerance * scale) {
        const double firstParameter = Cross2(delta, secondDirection) / denominator;
        const double secondParameter = Cross2(delta, firstDirection) / denominator;
        return firstParameter >= -1.0e-10 && firstParameter <= 1.0 + 1.0e-10
            && secondParameter >= -1.0e-10 && secondParameter <= 1.0 + 1.0e-10;
    }

    if (std::abs(Cross2(delta, firstDirection)) > tolerance * scale) {
        return false;
    }
    const double lengthSquared = Dot2(firstDirection, firstDirection);
    if (lengthSquared <= tolerance * tolerance) {
        return false;
    }
    double first = Dot2(secondStart - firstStart, firstDirection) / lengthSquared;
    double second = Dot2(secondEnd - firstStart, firstDirection) / lengthSquared;
    if (first > second) {
        std::swap(first, second);
    }
    return std::min(1.0, second) - std::max(0.0, first) > 1.0e-10;
}

void ValidateSimplePlanarBoundary(
    const Wire& boundary,
    const WorkPlane& plane,
    double tolerance)
{
    std::vector<Vector3> worldPoints;
    if (boundary.Kind() == WireKind::Polyline) {
        worldPoints = boundary.ControlPoints();
    } else {
        worldPoints.reserve(257);
        for (int sample = 0; sample <= 256; ++sample) {
            worldPoints.push_back(boundary.Evaluate(
                static_cast<double>(sample) / 256.0));
        }
    }
    constexpr std::size_t kMaximumIntersectionSamples = 1024;
    if (worldPoints.size() > kMaximumIntersectionSamples + 1) {
        const std::size_t segmentCount = worldPoints.size() - 1;
        const std::size_t stride = (segmentCount + kMaximumIntersectionSamples - 1)
            / kMaximumIntersectionSamples;
        std::vector<Vector3> reduced;
        reduced.reserve(kMaximumIntersectionSamples + 1);
        for (std::size_t index = 0; index < segmentCount; index += stride) {
            reduced.push_back(worldPoints[index]);
        }
        reduced.push_back(worldPoints.back());
        worldPoints = std::move(reduced);
    }
    std::vector<PlanePoint> points;
    points.reserve(worldPoints.size());
    for (const Vector3 point : worldPoints) {
        const auto projected = plane.Project(point);
        const PlanePoint next{projected.u, projected.v};
        if (points.empty()
            || std::hypot(next.u - points.back().u, next.v - points.back().v)
                > tolerance) {
            points.push_back(next);
        }
    }
    if (points.size() < 4) {
        throw std::invalid_argument(
            "Planar surface boundary does not contain enough distinct edges.");
    }
    points.back() = points.front();
    const std::size_t segmentCount = points.size() - 1;
    struct SegmentBounds {
        std::size_t index = 0;
        double minimumU = 0.0;
        double maximumU = 0.0;
        double minimumV = 0.0;
        double maximumV = 0.0;
    };
    std::vector<SegmentBounds> bounds;
    bounds.reserve(segmentCount);
    for (std::size_t index = 0; index < segmentCount; ++index) {
        bounds.push_back({
            index,
            std::min(points[index].u, points[index + 1].u),
            std::max(points[index].u, points[index + 1].u),
            std::min(points[index].v, points[index + 1].v),
            std::max(points[index].v, points[index + 1].v),
        });
    }
    std::sort(bounds.begin(), bounds.end(), [](const auto& first, const auto& second) {
        return first.minimumU < second.minimumU;
    });
    for (std::size_t firstOrder = 0; firstOrder < bounds.size(); ++firstOrder) {
        const SegmentBounds& firstBounds = bounds[firstOrder];
        for (std::size_t secondOrder = firstOrder + 1;
             secondOrder < bounds.size(); ++secondOrder) {
            const SegmentBounds& secondBounds = bounds[secondOrder];
            if (secondBounds.minimumU > firstBounds.maximumU + tolerance) {
                break;
            }
            if (secondBounds.minimumV > firstBounds.maximumV + tolerance
                || firstBounds.minimumV > secondBounds.maximumV + tolerance) {
                continue;
            }
            const std::size_t first = firstBounds.index;
            const std::size_t second = secondBounds.index;
            if (second == first + 1
                || first == second + 1
                || (std::min(first, second) == 0
                    && std::max(first, second) + 1 == segmentCount)) {
                continue;
            }
            if (NonAdjacentSegmentsIntersect(
                    points[first], points[first + 1],
                    points[second], points[second + 1], tolerance)) {
                throw std::invalid_argument(
                    "Planar surface boundary crosses or overlaps itself.");
            }
        }
    }
}

Vector3 DerivativeU(const Surface& surface, double u, double v)
{
    const double before = std::max(0.0, u - kParameterStep);
    const double after = std::min(1.0, u + kParameterStep);
    return (surface.Evaluate(after, v) - surface.Evaluate(before, v)) / (after - before);
}

Vector3 DerivativeV(const Surface& surface, double u, double v)
{
    const double before = std::max(0.0, v - kParameterStep);
    const double after = std::min(1.0, v + kParameterStep);
    return (surface.Evaluate(u, after) - surface.Evaluate(u, before)) / (after - before);
}

std::optional<SurfaceProjection> RefineProjection(
    const Surface& surface,
    Vector3 sourcePoint,
    Vector3 direction,
    double initialU,
    double initialV,
    double tolerance)
{
    double u = initialU;
    double v = initialV;
    double distance = Dot(surface.Evaluate(u, v) - sourcePoint, direction) / direction.LengthSquared();
    for (int iteration = 0; iteration < 32; ++iteration) {
        const Vector3 surfacePoint = surface.Evaluate(u, v);
        const Vector3 residual = surfacePoint - sourcePoint - direction * distance;
        if (residual.Length() <= tolerance) {
            if (u >= -1.0e-7 && u <= 1.0 + 1.0e-7 && v >= -1.0e-7 && v <= 1.0 + 1.0e-7) {
                const double clampedU = std::clamp(u, 0.0, 1.0);
                const double clampedV = std::clamp(v, 0.0, 1.0);
                return SurfaceProjection{surface.Evaluate(clampedU, clampedV), clampedU, clampedV, distance};
            }
            return std::nullopt;
        }

        const Vector3 uDerivative = DerivativeU(surface, u, v);
        const Vector3 vDerivative = DerivativeV(surface, u, v);
        const Vector3 distanceDerivative = direction * -1.0;
        const double determinant = Dot(uDerivative, Cross(vDerivative, distanceDerivative));
        if (std::abs(determinant) <= 1.0e-13) {
            return std::nullopt;
        }
        const Vector3 rightHandSide = residual * -1.0;
        double deltaU = Dot(rightHandSide, Cross(vDerivative, distanceDerivative)) / determinant;
        double deltaV = Dot(uDerivative, Cross(rightHandSide, distanceDerivative)) / determinant;
        double deltaDistance = Dot(uDerivative, Cross(vDerivative, rightHandSide)) / determinant;
        deltaU = std::clamp(deltaU, -0.25, 0.25);
        deltaV = std::clamp(deltaV, -0.25, 0.25);
        deltaDistance = std::clamp(deltaDistance, -1000.0, 1000.0);
        u += deltaU;
        v += deltaV;
        distance += deltaDistance;
        if (u < -0.5 || u > 1.5 || v < -0.5 || v > 1.5 || !std::isfinite(distance)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

int ProjectionSampleCount(const Wire& wire, int requested)
{
    const int minimum = wire.Kind() == WireKind::Line ? 24
        : wire.Kind() == WireKind::Polyline ? static_cast<int>((wire.ControlPoints().size() - 1) * 16)
        : wire.Kind() == WireKind::Circle ? 192
                                         : 96;
    return std::clamp(std::max(requested, minimum), 8, 2048);
}

std::vector<Wire> PrepareSections(std::vector<Wire> sections, std::size_t requiredCount)
{
    if (sections.size() < requiredCount) {
        throw std::invalid_argument(requiredCount == 2
                ? "Ruled surface requires two sections."
                : "Loft surface requires at least three sections.");
    }
    const bool closed = sections.front().IsClosed();
    for (std::size_t index = 1; index < sections.size(); ++index) {
        if (sections[index].IsClosed() != closed) {
            throw std::invalid_argument("Surface sections must all be open or all be closed.");
        }
        const double sameDirection = (sections[index - 1].Start() - sections[index].Start()).LengthSquared()
            + (sections[index - 1].End() - sections[index].End()).LengthSquared();
        const double reversedDirection = (sections[index - 1].Start() - sections[index].End()).LengthSquared()
            + (sections[index - 1].End() - sections[index].Start()).LengthSquared();
        if (reversedDirection + 1.0e-12 < sameDirection) {
            sections[index] = sections[index].Reversed();
        }

        double separation = 0.0;
        for (int sample = 0; sample <= 32; ++sample) {
            const double u = static_cast<double>(sample) / 32.0;
            separation = std::max(separation, (sections[index - 1].Evaluate(u) - sections[index].Evaluate(u)).Length());
        }
        if (separation <= 1.0e-8) {
            throw std::invalid_argument("Adjacent surface sections must be separated.");
        }
    }
    return sections;
}

Vector3 EvaluateSmoothLoft(
    const std::vector<Wire>& sections,
    double u,
    double v)
{
    const std::size_t count = sections.size();
    std::vector<Vector3> values;
    values.reserve(count);
    for (const Wire& section : sections) {
        values.push_back(section.Evaluate(u));
    }

    std::vector<double> lower(count, 1.0);
    std::vector<double> diagonal(count, 4.0);
    std::vector<double> upper(count, 1.0);
    std::vector<Vector3> rightHandSide(count);
    lower.front() = 0.0;
    diagonal.front() = 2.0;
    upper.back() = 0.0;
    diagonal.back() = 2.0;
    for (std::size_t index = 1; index + 1 < count; ++index) {
        rightHandSide[index] = (
            values[index + 1] - values[index] * 2.0 + values[index - 1]) * 6.0;
    }

    for (std::size_t index = 1; index < count; ++index) {
        const double factor = lower[index] / diagonal[index - 1];
        diagonal[index] -= factor * upper[index - 1];
        rightHandSide[index] =
            rightHandSide[index] - rightHandSide[index - 1] * factor;
    }
    std::vector<Vector3> secondDerivatives(count);
    secondDerivatives.back() = rightHandSide.back() / diagonal.back();
    for (std::size_t index = count - 1; index-- > 0;) {
        secondDerivatives[index] = (
            rightHandSide[index] - secondDerivatives[index + 1] * upper[index])
            / diagonal[index];
    }

    const double scaledV = v * static_cast<double>(sections.size() - 1);
    const std::size_t segment = static_cast<std::size_t>(std::min(
        scaledV,
        static_cast<double>(sections.size() - 2)));
    const double t = scaledV - static_cast<double>(segment);
    const double oneMinusT = 1.0 - t;
    return values[segment] * oneMinusT
        + values[segment + 1] * t
        + secondDerivatives[segment]
            * ((oneMinusT * oneMinusT * oneMinusT - oneMinusT) / 6.0)
        + secondDerivatives[segment + 1]
            * ((t * t * t - t) / 6.0);
}

struct ClosestWirePoint {
    double parameter = 0.0;
    double distance = std::numeric_limits<double>::infinity();
};

ClosestWirePoint ClosestPointOnWire(const Wire& wire, Vector3 point)
{
    constexpr int kSamples = 256;
    int bestSample = 0;
    double bestDistanceSquared = std::numeric_limits<double>::infinity();
    for (int sample = 0; sample <= kSamples; ++sample) {
        const double parameter = static_cast<double>(sample) / kSamples;
        const double distanceSquared = (wire.Evaluate(parameter) - point).LengthSquared();
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestSample = sample;
        }
    }

    double minimum = std::max(0.0,
        static_cast<double>(bestSample - 1) / kSamples);
    double maximum = std::min(1.0,
        static_cast<double>(bestSample + 1) / kSamples);
    for (int iteration = 0; iteration < 32; ++iteration) {
        const double first = minimum + (maximum - minimum) / 3.0;
        const double second = maximum - (maximum - minimum) / 3.0;
        if ((wire.Evaluate(first) - point).LengthSquared()
            <= (wire.Evaluate(second) - point).LengthSquared()) {
            maximum = second;
        } else {
            minimum = first;
        }
    }
    const double parameter = (minimum + maximum) * 0.5;
    return {parameter, (wire.Evaluate(parameter) - point).Length()};
}

double PiecewiseMap(
    double parameter,
    const std::vector<double>& source,
    const std::vector<double>& target)
{
    if (source.size() != target.size() || source.size() < 2) {
        throw std::logic_error("Guided loft parameter map is incomplete.");
    }
    parameter = std::clamp(parameter, 0.0, 1.0);
    const auto upper = std::upper_bound(source.begin(), source.end(), parameter);
    const std::size_t second = upper == source.end()
        ? source.size() - 1
        : static_cast<std::size_t>(std::distance(source.begin(), upper));
    const std::size_t first = second == 0 ? 0 : second - 1;
    if (first == second || source[second] - source[first] <= 1.0e-12) {
        return target[first];
    }
    const double local = (parameter - source[first])
        / (source[second] - source[first]);
    return target[first] * (1.0 - local) + target[second] * local;
}

std::pair<std::vector<double>, std::vector<double>> BuildGuideMap(
    const std::vector<double>& sectionParameters,
    const std::vector<double>& guideParameters)
{
    if (sectionParameters.size() != guideParameters.size()) {
        throw std::logic_error("Guided loft parameter data is incomplete.");
    }
    std::vector<double> source{0.0};
    std::vector<double> target{0.0};
    source.reserve(sectionParameters.size() + 2);
    target.reserve(sectionParameters.size() + 2);
    for (std::size_t index = 0; index < sectionParameters.size(); ++index) {
        if (sectionParameters[index] > 1.0e-8
            && sectionParameters[index] < 1.0 - 1.0e-8) {
            source.push_back(sectionParameters[index]);
            target.push_back(guideParameters[index]);
        }
    }
    source.push_back(1.0);
    target.push_back(1.0);
    return {std::move(source), std::move(target)};
}

Vector3 EvaluateGuidedSectionLoft(
    const std::vector<Wire>& sections,
    const std::vector<double>& sectionParameters,
    const Wire& firstGuide,
    const Wire& secondGuide,
    const std::vector<double>& firstGuideParameters,
    const std::vector<double>& secondGuideParameters,
    double u,
    double v)
{
    std::vector<double> parameters;
    parameters.reserve(sectionParameters.size() + 2);
    const bool virtualStart = sectionParameters.front() > 1.0e-8;
    const bool virtualEnd = sectionParameters.back() < 1.0 - 1.0e-8;
    if (virtualStart) {
        parameters.push_back(0.0);
    }
    parameters.insert(parameters.end(), sectionParameters.begin(), sectionParameters.end());
    if (virtualEnd) {
        parameters.push_back(1.0);
    }

    const auto evaluateSection = [&](std::size_t index) {
        if (virtualStart && index == 0) {
            return firstGuide.Evaluate(0.0) * (1.0 - u)
                + secondGuide.Evaluate(0.0) * u;
        }
        const std::size_t actualIndex = index - (virtualStart ? 1U : 0U);
        if (actualIndex < sections.size()) {
            return sections[actualIndex].Evaluate(u);
        }
        return firstGuide.Evaluate(1.0) * (1.0 - u)
            + secondGuide.Evaluate(1.0) * u;
    };

    const auto upper = std::upper_bound(parameters.begin(), parameters.end(), v);
    const std::size_t second = upper == parameters.end()
        ? parameters.size() - 1
        : static_cast<std::size_t>(std::distance(parameters.begin(), upper));
    const std::size_t first = second == 0 ? 0 : second - 1;
    if (first == second || parameters[second] - parameters[first] <= 1.0e-12) {
        return evaluateSection(first);
    }

    const double local = (v - parameters[first])
        / (parameters[second] - parameters[first]);
    const Vector3 p1 = evaluateSection(first);
    const Vector3 p2 = evaluateSection(second);
    const std::size_t previous = first > 0 ? first - 1 : first;
    const std::size_t next = second + 1 < parameters.size() ? second + 1 : second;
    const Vector3 p0 = previous == first
        ? p1 * 2.0 - p2
        : evaluateSection(previous);
    const Vector3 p3 = next == second
        ? p2 * 2.0 - p1
        : evaluateSection(next);
    const double previousParameter = previous == first
        ? parameters[first] - (parameters[second] - parameters[first])
        : parameters[previous];
    const double nextParameter = next == second
        ? parameters[second] + (parameters[second] - parameters[first])
        : parameters[next];
    const Vector3 tangent1 = (p2 - p0)
        * ((parameters[second] - parameters[first])
            / (parameters[second] - previousParameter));
    const Vector3 tangent2 = (p3 - p1)
        * ((parameters[second] - parameters[first])
            / (nextParameter - parameters[first]));
    const double local2 = local * local;
    const double local3 = local2 * local;
    const Vector3 base = p1 * (2.0 * local3 - 3.0 * local2 + 1.0)
        + tangent1 * (local3 - 2.0 * local2 + local)
        + p2 * (-2.0 * local3 + 3.0 * local2)
        + tangent2 * (local3 - local2);

    const auto [firstMapParameters, firstMap]
        = BuildGuideMap(sectionParameters, firstGuideParameters);
    const auto [secondMapParameters, secondMap]
        = BuildGuideMap(sectionParameters, secondGuideParameters);
    const Vector3 firstBoundary = firstGuide.Evaluate(
        PiecewiseMap(v, firstMapParameters, firstMap));
    const Vector3 secondBoundary = secondGuide.Evaluate(
        PiecewiseMap(v, secondMapParameters, secondMap));
    const Vector3 baseFirst = [&] {
        const double savedU = 0.0;
        const auto eval = [&](std::size_t index) {
            if (virtualStart && index == 0) {
                return firstGuide.Evaluate(0.0);
            }
            const std::size_t actual = index - (virtualStart ? 1U : 0U);
            return actual < sections.size()
                ? sections[actual].Evaluate(savedU)
                : firstGuide.Evaluate(1.0);
        };
        const Vector3 q1 = eval(first);
        const Vector3 q2 = eval(second);
        const Vector3 q0 = previous == first ? q1 * 2.0 - q2 : eval(previous);
        const Vector3 q3 = next == second ? q2 * 2.0 - q1 : eval(next);
        return q1 * (2.0 * local3 - 3.0 * local2 + 1.0)
            + (q2 - q0) * ((parameters[second] - parameters[first])
                / (parameters[second] - previousParameter))
                * (local3 - 2.0 * local2 + local)
            + q2 * (-2.0 * local3 + 3.0 * local2)
            + (q3 - q1) * ((parameters[second] - parameters[first])
                / (nextParameter - parameters[first]))
                * (local3 - local2);
    }();
    const Vector3 baseSecond = [&] {
        const auto eval = [&](std::size_t index) {
            if (virtualStart && index == 0) {
                return secondGuide.Evaluate(0.0);
            }
            const std::size_t actual = index - (virtualStart ? 1U : 0U);
            return actual < sections.size()
                ? sections[actual].Evaluate(1.0)
                : secondGuide.Evaluate(1.0);
        };
        const Vector3 q1 = eval(first);
        const Vector3 q2 = eval(second);
        const Vector3 q0 = previous == first ? q1 * 2.0 - q2 : eval(previous);
        const Vector3 q3 = next == second ? q2 * 2.0 - q1 : eval(next);
        return q1 * (2.0 * local3 - 3.0 * local2 + 1.0)
            + (q2 - q0) * ((parameters[second] - parameters[first])
                / (parameters[second] - previousParameter))
                * (local3 - 2.0 * local2 + local)
            + q2 * (-2.0 * local3 + 3.0 * local2)
            + (q3 - q1) * ((parameters[second] - parameters[first])
                / (nextParameter - parameters[first]))
                * (local3 - local2);
    }();
    return base
        + (firstBoundary - baseFirst) * (1.0 - u)
        + (secondBoundary - baseSecond) * u;
}

} // namespace

Surface::Surface(
    SurfaceKind kind,
    std::vector<Wire> boundaries,
    std::optional<WorkPlane> planarWorkPlane,
    double minimumU,
    double minimumV,
    double maximumU,
    double maximumV,
    std::vector<Wire> guides,
    std::vector<double> sectionParameters,
    std::vector<double> firstGuideParameters,
    std::vector<double> secondGuideParameters)
    : kind_(kind),
      boundaries_(std::move(boundaries)),
      guides_(std::move(guides)),
      sectionParameters_(std::move(sectionParameters)),
      firstGuideParameters_(std::move(firstGuideParameters)),
      secondGuideParameters_(std::move(secondGuideParameters)),
      planarWorkPlane_(std::move(planarWorkPlane)),
      minimumU_(minimumU),
      minimumV_(minimumV),
      maximumU_(maximumU),
      maximumV_(maximumV)
{
}

Surface Surface::Planar(Wire closedBoundary, double tolerance)
{
    if (!closedBoundary.IsClosed(tolerance)) {
        throw std::invalid_argument("Planar surface requires a closed boundary wire.");
    }

    WorkPlane plane = [&] {
        if (closedBoundary.Kind() == WireKind::Circle) {
            const auto arc = closedBoundary.ArcData();
            return WorkPlane::FromOriginAxes(arc.center, arc.uAxis, Cross(arc.uAxis, arc.vAxis));
        }
        const auto& points = closedBoundary.ControlPoints();
        for (std::size_t second = 1; second < points.size(); ++second) {
            for (std::size_t third = second + 1; third < points.size(); ++third) {
                if (Cross(points[second] - points[0], points[third] - points[0]).LengthSquared() > 1.0e-18) {
                    return WorkPlane::FromThreePoints(points[0], points[second], points[third]);
                }
            }
        }
        throw std::invalid_argument("Planar surface boundary must enclose an area.");
    }();

    double minimumU = std::numeric_limits<double>::infinity();
    double minimumV = std::numeric_limits<double>::infinity();
    double maximumU = -std::numeric_limits<double>::infinity();
    double maximumV = -std::numeric_limits<double>::infinity();
    for (int sample = 0; sample <= 256; ++sample) {
        const auto coordinates = plane.Project(closedBoundary.Evaluate(static_cast<double>(sample) / 256.0));
        if (std::abs(coordinates.w) > tolerance) {
            throw std::invalid_argument("Planar surface boundary is not planar.");
        }
        minimumU = std::min(minimumU, coordinates.u);
        minimumV = std::min(minimumV, coordinates.v);
        maximumU = std::max(maximumU, coordinates.u);
        maximumV = std::max(maximumV, coordinates.v);
    }
    if (maximumU - minimumU <= tolerance || maximumV - minimumV <= tolerance) {
        throw std::invalid_argument("Planar surface boundary must enclose an area.");
    }
    ValidateSimplePlanarBoundary(closedBoundary, plane, tolerance);
    std::vector<Wire> boundaries;
    boundaries.push_back(std::move(closedBoundary));
    return {SurfaceKind::Planar, std::move(boundaries), plane, minimumU, minimumV, maximumU, maximumV};
}

Surface Surface::Ruled(Wire firstSection, Wire secondSection)
{
    std::vector<Wire> sections;
    sections.push_back(std::move(firstSection));
    sections.push_back(std::move(secondSection));
    return {SurfaceKind::Ruled, PrepareSections(std::move(sections), 2), std::nullopt, 0.0, 0.0, 1.0, 1.0};
}

Surface Surface::Loft(std::vector<Wire> sections)
{
    return {SurfaceKind::Loft, PrepareSections(std::move(sections), 3), std::nullopt, 0.0, 0.0, 1.0, 1.0};
}

Surface Surface::GuidedLoft(
    Wire firstGuide,
    Wire secondGuide,
    std::vector<Wire> sections,
    double connectionToleranceMillimeters)
{
    if (sections.empty()) {
        throw std::invalid_argument(
            "Guided loft requires at least one cross section.");
    }
    if (firstGuide.IsClosed() || secondGuide.IsClosed()
        || !std::isfinite(connectionToleranceMillimeters)
        || connectionToleranceMillimeters <= 0.0) {
        throw std::invalid_argument(
            "Guided loft guides must be open and the connection tolerance must be positive.");
    }
    for (const Wire& section : sections) {
        if (section.IsClosed()) {
            throw std::invalid_argument(
                "Guided loft cross sections must be open wires.");
        }
    }

    const auto orientationCost = [&](const Wire& candidateSecondGuide) {
        double total = 0.0;
        for (const Wire& section : sections) {
            const double forward = ClosestPointOnWire(firstGuide, section.Start()).distance
                + ClosestPointOnWire(candidateSecondGuide, section.End()).distance;
            const double reversed = ClosestPointOnWire(firstGuide, section.End()).distance
                + ClosestPointOnWire(candidateSecondGuide, section.Start()).distance;
            total += std::min(forward, reversed);
        }
        return total;
    };
    const Wire reversedSecondGuide = secondGuide.Reversed();
    if (orientationCost(reversedSecondGuide) + 1.0e-12
        < orientationCost(secondGuide)) {
        secondGuide = reversedSecondGuide;
    }

    struct ConnectedSection {
        Wire wire;
        double parameter = 0.0;
        double firstGuideParameter = 0.0;
        double secondGuideParameter = 0.0;
    };
    std::vector<ConnectedSection> connected;
    connected.reserve(sections.size());
    for (Wire section : sections) {
        const auto firstStart = ClosestPointOnWire(firstGuide, section.Start());
        const auto secondEnd = ClosestPointOnWire(secondGuide, section.End());
        const auto firstEnd = ClosestPointOnWire(firstGuide, section.End());
        const auto secondStart = ClosestPointOnWire(secondGuide, section.Start());
        if (firstEnd.distance + secondStart.distance
            < firstStart.distance + secondEnd.distance) {
            section = section.Reversed();
        }
        const auto first = ClosestPointOnWire(firstGuide, section.Start());
        const auto second = ClosestPointOnWire(secondGuide, section.End());
        if (first.distance > connectionToleranceMillimeters
            || second.distance > connectionToleranceMillimeters) {
            throw std::invalid_argument(
                "Each guided-loft cross section must touch both guide wires within the connection tolerance.");
        }
        connected.push_back({
            std::move(section),
            (first.parameter + second.parameter) * 0.5,
            first.parameter,
            second.parameter,
        });
    }
    std::sort(connected.begin(), connected.end(), [](const auto& first, const auto& second) {
        return first.parameter < second.parameter;
    });
    for (std::size_t index = 1; index < connected.size(); ++index) {
        if (connected[index].parameter - connected[index - 1].parameter <= 1.0e-5
            || connected[index].firstGuideParameter
                <= connected[index - 1].firstGuideParameter + 1.0e-6
            || connected[index].secondGuideParameter
                <= connected[index - 1].secondGuideParameter + 1.0e-6) {
            throw std::invalid_argument(
                "Guided-loft cross sections cross or occupy the same guide position.");
        }
    }

    std::vector<Wire> preparedSections;
    std::vector<double> parameters;
    std::vector<double> firstParameters;
    std::vector<double> secondParameters;
    preparedSections.reserve(connected.size());
    parameters.reserve(connected.size());
    firstParameters.reserve(connected.size());
    secondParameters.reserve(connected.size());
    for (ConnectedSection& item : connected) {
        preparedSections.push_back(std::move(item.wire));
        parameters.push_back(item.parameter);
        firstParameters.push_back(item.firstGuideParameter);
        secondParameters.push_back(item.secondGuideParameter);
    }
    std::vector<Wire> guides;
    guides.push_back(std::move(firstGuide));
    guides.push_back(std::move(secondGuide));
    return {
        SurfaceKind::GuidedLoft,
        std::move(preparedSections),
        std::nullopt,
        0.0, 0.0, 1.0, 1.0,
        std::move(guides),
        std::move(parameters),
        std::move(firstParameters),
        std::move(secondParameters),
    };
}

Vector3 Surface::Evaluate(double u, double v) const
{
    if (!std::isfinite(u) || !std::isfinite(v)) {
        throw std::invalid_argument("Surface parameters must be finite.");
    }
    const double clampedU = std::clamp(u, 0.0, 1.0);
    const double clampedV = std::clamp(v, 0.0, 1.0);
    if (kind_ == SurfaceKind::Planar) {
        return planarWorkPlane_->ToWorld(
            minimumU_ + (maximumU_ - minimumU_) * clampedU,
            minimumV_ + (maximumV_ - minimumV_) * clampedV);
    }
    if (kind_ == SurfaceKind::Loft) {
        return EvaluateSmoothLoft(boundaries_, clampedU, clampedV);
    }
    if (kind_ == SurfaceKind::GuidedLoft) {
        return EvaluateGuidedSectionLoft(
            boundaries_, sectionParameters_, guides_[0], guides_[1],
            firstGuideParameters_, secondGuideParameters_, clampedU, clampedV);
    }
    const double scaledV = clampedV * static_cast<double>(boundaries_.size() - 1);
    const std::size_t segment = static_cast<std::size_t>(std::min(
        scaledV,
        static_cast<double>(boundaries_.size() - 2)));
    const double localV = scaledV - static_cast<double>(segment);
    const Vector3 first = boundaries_[segment].Evaluate(clampedU);
    const Vector3 second = boundaries_[segment + 1].Evaluate(clampedU);
    return first * (1.0 - localV) + second * localV;
}

Vector3 Surface::Normal(double u, double v) const
{
    if (!std::isfinite(u) || !std::isfinite(v)) {
        throw std::invalid_argument("Surface parameters must be finite.");
    }

    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const auto normalAt = [&](double candidateU, double candidateV) {
        candidateU = std::clamp(candidateU, 0.0, 1.0);
        candidateV = std::clamp(candidateV, 0.0, 1.0);
        return Cross(
            DerivativeU(*this, candidateU, candidateV),
            DerivativeV(*this, candidateU, candidateV));
    };

    Vector3 normal = normalAt(u, v);
    if (normal.IsFinite() && normal.LengthSquared() > 1.0e-18) {
        return normal.Normalized();
    }

    // A guided loft may legitimately converge to one point at an outer edge.
    // The point itself has no unique differential normal, so use the limiting
    // normal from just inside the surface while preserving the U position.
    constexpr std::array<double, 4> steps{1.0e-5, 1.0e-4, 1.0e-3, 1.0e-2};
    for (const double step : steps) {
        const double inwardV = v <= 0.5 ? v + step : v - step;
        const double inwardU = u <= 0.5 ? u + step : u - step;
        for (const auto& candidate : std::array<std::pair<double, double>, 5>{
                 std::pair{u, inwardV},
                 std::pair{inwardU, v},
                 std::pair{inwardU, inwardV},
                 std::pair{u - step, inwardV},
                 std::pair{u + step, inwardV},
             }) {
            normal = normalAt(candidate.first, candidate.second);
            if (normal.IsFinite() && normal.LengthSquared() > 1.0e-18) {
                return normal.Normalized();
            }
        }
    }
    throw std::invalid_argument("Surface normal is undefined in this region.");
}

SurfaceCurvature Surface::Curvature(double u, double v) const
{
    if (!std::isfinite(u) || !std::isfinite(v)) {
        throw std::invalid_argument("Surface parameters must be finite.");
    }
    constexpr double step = 1.0e-3;
    u = std::clamp(u, step, 1.0 - step);
    v = std::clamp(v, step, 1.0 - step);

    const Vector3 center = Evaluate(u, v);
    const Vector3 beforeU = Evaluate(u - step, v);
    const Vector3 afterU = Evaluate(u + step, v);
    const Vector3 beforeV = Evaluate(u, v - step);
    const Vector3 afterV = Evaluate(u, v + step);
    const Vector3 derivativeU = (afterU - beforeU) / (2.0 * step);
    const Vector3 derivativeV = (afterV - beforeV) / (2.0 * step);
    const Vector3 normalVector = Cross(derivativeU, derivativeV);
    if (normalVector.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Surface curvature is undefined at this position.");
    }
    const Vector3 normal = normalVector.Normalized();
    const Vector3 secondU = (afterU - center * 2.0 + beforeU) / (step * step);
    const Vector3 secondV = (afterV - center * 2.0 + beforeV) / (step * step);
    const Vector3 secondUV = (
        Evaluate(u + step, v + step)
        - Evaluate(u + step, v - step)
        - Evaluate(u - step, v + step)
        + Evaluate(u - step, v - step))
        / (4.0 * step * step);

    const double firstE = Dot(derivativeU, derivativeU);
    const double firstF = Dot(derivativeU, derivativeV);
    const double firstG = Dot(derivativeV, derivativeV);
    const double denominator = firstE * firstG - firstF * firstF;
    if (denominator <= 1.0e-18) {
        throw std::invalid_argument("Surface curvature is undefined at this position.");
    }
    const double secondL = Dot(secondU, normal);
    const double secondM = Dot(secondUV, normal);
    const double secondN = Dot(secondV, normal);
    const double gaussian =
        (secondL * secondN - secondM * secondM) / denominator;
    const double mean =
        (firstE * secondN - 2.0 * firstF * secondM + firstG * secondL)
        / (2.0 * denominator);
    const double discriminant = std::sqrt(std::max(0.0, mean * mean - gaussian));
    return {
        gaussian,
        mean,
        mean - discriminant,
        mean + discriminant,
    };
}

bool Surface::ContainsPlanarPoint(double u, double v, double tolerance) const
{
    const auto point = planarWorkPlane_->ToWorld(u, v);
    bool inside = false;
    auto previous = planarWorkPlane_->Project(boundaries_.front().Evaluate(1.0));
    for (int sample = 0; sample <= 256; ++sample) {
        const auto current = planarWorkPlane_->Project(boundaries_.front().Evaluate(static_cast<double>(sample) / 256.0));
        const Vector3 segmentStart = planarWorkPlane_->ToWorld(previous.u, previous.v);
        const Vector3 segmentEnd = planarWorkPlane_->ToWorld(current.u, current.v);
        const Vector3 segment = segmentEnd - segmentStart;
        if (segment.LengthSquared() > 1.0e-24) {
            const double parameter = std::clamp(Dot(point - segmentStart, segment) / segment.LengthSquared(), 0.0, 1.0);
            if ((point - (segmentStart + segment * parameter)).Length() <= tolerance) {
                return true;
            }
        }
        if ((current.v > v) != (previous.v > v)) {
            const double crossingU = (previous.u - current.u) * (v - current.v) / (previous.v - current.v) + current.u;
            if (u < crossingU) {
                inside = !inside;
            }
        }
        previous = current;
    }
    return inside;
}

SurfaceProjection Surface::ProjectPointAlongDirection(Vector3 sourcePoint, Vector3 direction, double tolerance) const
{
    if (!sourcePoint.IsFinite() || !direction.IsFinite() || direction.LengthSquared() <= 1.0e-18
        || !std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Projection point, direction, and tolerance must be valid.");
    }
    direction = direction.Normalized();
    if (kind_ == SurfaceKind::Planar) {
        const double denominator = Dot(direction, planarWorkPlane_->Normal());
        if (std::abs(denominator) <= 1.0e-12) {
            throw std::invalid_argument("Projection direction is parallel to the planar surface.");
        }
        const double distance = Dot(planarWorkPlane_->Origin() - sourcePoint, planarWorkPlane_->Normal()) / denominator;
        const Vector3 projected = sourcePoint + direction * distance;
        const auto coordinates = planarWorkPlane_->Project(projected);
        if (!ContainsPlanarPoint(coordinates.u, coordinates.v, tolerance)) {
            throw std::invalid_argument("Projected point is outside the planar surface boundary.");
        }
        return {
            projected,
            (coordinates.u - minimumU_) / (maximumU_ - minimumU_),
            (coordinates.v - minimumV_) / (maximumV_ - minimumV_),
            distance,
        };
    }

    std::optional<SurfaceProjection> best;
    for (int uIndex = 0; uIndex <= 16; ++uIndex) {
        for (int vIndex = 0; vIndex <= 10; ++vIndex) {
            const double u = static_cast<double>(uIndex) / 16.0;
            const double v = static_cast<double>(vIndex) / 10.0;
            const auto candidate = RefineProjection(*this, sourcePoint, direction, u, v, tolerance);
            if (candidate.has_value()
                && (!best.has_value()
                    || std::abs(candidate->distanceAlongDirection) < std::abs(best->distanceAlongDirection))) {
                best = candidate;
            }
        }
    }
    if (!best.has_value()) {
        throw std::invalid_argument("Projection line does not intersect the surface.");
    }
    return *best;
}

std::vector<SurfaceProjection> Surface::ProjectPointsAlongDirection(
    const std::vector<Vector3>& sourcePoints,
    Vector3 direction,
    double tolerance) const
{
    if (!direction.IsFinite() || direction.LengthSquared() <= 1.0e-18
        || !std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument(
            "Projection direction and tolerance must be valid.");
    }

    direction = direction.Normalized();
    std::vector<SurfaceProjection> result;
    result.reserve(sourcePoints.size());
    std::optional<SurfaceProjection> previous;
    for (const Vector3 sourcePoint : sourcePoints) {
        if (!sourcePoint.IsFinite()) {
            throw std::invalid_argument("Projection points must be finite.");
        }
        std::optional<SurfaceProjection> projection;
        if (kind_ != SurfaceKind::Planar && previous.has_value()) {
            projection = RefineProjection(
                *this,
                sourcePoint,
                direction,
                previous->u,
                previous->v,
                tolerance);
        }
        if (!projection.has_value()) {
            projection = ProjectPointAlongDirection(
                sourcePoint, direction, tolerance);
        }
        result.push_back(*projection);
        previous = projection;
    }
    return result;
}

Wire Surface::ProjectWireAlongDirection(const Wire& sourceWire, Vector3 direction, int samples, double tolerance) const
{
    const int sampleCount = ProjectionSampleCount(sourceWire, samples);
    std::vector<Vector3> sourcePoints;
    sourcePoints.reserve(static_cast<std::size_t>(sampleCount) + 1);
    for (int sample = 0; sample <= sampleCount; ++sample) {
        sourcePoints.push_back(sourceWire.Evaluate(
            static_cast<double>(sample) / sampleCount));
    }
    const auto projections = ProjectPointsAlongDirection(
        sourcePoints, direction, tolerance);
    std::vector<Vector3> points;
    points.reserve(projections.size());
    for (const SurfaceProjection& projection : projections) {
        const Vector3 projected = projection.point;
        if (points.empty() || (projected - points.back()).LengthSquared() > tolerance * tolerance) {
            points.push_back(projected);
        }
    }
    if (points.size() < 2) {
        throw std::invalid_argument("Projected wire collapsed to a point.");
    }
    if (sourceWire.IsClosed()) {
        if ((points.front() - points.back()).LengthSquared() > tolerance * tolerance) {
            points.push_back(points.front());
        } else {
            points.back() = points.front();
        }
    }
    return Wire::Polyline(std::move(points));
}

} // namespace kachakacha::model
