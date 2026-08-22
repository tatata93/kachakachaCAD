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

} // namespace

Surface::Surface(
    SurfaceKind kind,
    std::vector<Wire> boundaries,
    std::optional<WorkPlane> planarWorkPlane,
    double minimumU,
    double minimumV,
    double maximumU,
    double maximumV)
    : kind_(kind),
      boundaries_(std::move(boundaries)),
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
    const Vector3 normal = Cross(DerivativeU(*this, std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0)),
        DerivativeV(*this, std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0)));
    if (normal.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Surface normal is undefined at this position.");
    }
    return normal.Normalized();
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

Wire Surface::ProjectWireAlongDirection(const Wire& sourceWire, Vector3 direction, int samples, double tolerance) const
{
    const int sampleCount = ProjectionSampleCount(sourceWire, samples);
    std::vector<Vector3> points;
    points.reserve(static_cast<std::size_t>(sampleCount) + 1);
    for (int sample = 0; sample <= sampleCount; ++sample) {
        const Vector3 projected = ProjectPointAlongDirection(
            sourceWire.Evaluate(static_cast<double>(sample) / sampleCount), direction, tolerance).point;
        if (points.empty() || (projected - points.back()).LengthSquared() > tolerance * tolerance) {
            points.push_back(projected);
        }
    }
    if (points.size() < 2) {
        throw std::invalid_argument("Projected wire collapsed to a point.");
    }
    if (sourceWire.IsClosed() && (points.front() - points.back()).LengthSquared() > tolerance * tolerance) {
        points.push_back(points.front());
    }
    return Wire::Polyline(std::move(points));
}

} // namespace kachakacha::model
