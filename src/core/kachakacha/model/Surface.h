#pragma once

#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <optional>

namespace kachakacha::model {

enum class SurfaceKind {
    Planar,
    Ruled,
};

struct SurfaceProjection {
    geometry::Vector3 point;
    double u = 0.0;
    double v = 0.0;
    double distanceAlongDirection = 0.0;
};

class Surface {
public:
    [[nodiscard]] static Surface Planar(Wire closedBoundary, double tolerance = 1.0e-7);
    [[nodiscard]] static Surface Ruled(Wire firstSection, Wire secondSection);

    [[nodiscard]] SurfaceKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const Wire& FirstBoundary() const noexcept { return firstBoundary_; }
    [[nodiscard]] const std::optional<Wire>& SecondBoundary() const noexcept { return secondBoundary_; }
    [[nodiscard]] const std::optional<WorkPlane>& PlanarWorkPlane() const noexcept { return planarWorkPlane_; }
    [[nodiscard]] geometry::Vector3 Evaluate(double u, double v) const;
    [[nodiscard]] geometry::Vector3 Normal(double u, double v) const;
    [[nodiscard]] SurfaceProjection ProjectPointAlongDirection(
        geometry::Vector3 sourcePoint,
        geometry::Vector3 direction,
        double tolerance = 1.0e-7) const;
    [[nodiscard]] Wire ProjectWireAlongDirection(
        const Wire& sourceWire,
        geometry::Vector3 direction,
        int samples = 96,
        double tolerance = 1.0e-7) const;

private:
    Surface(
        SurfaceKind kind,
        Wire firstBoundary,
        std::optional<Wire> secondBoundary,
        std::optional<WorkPlane> planarWorkPlane,
        double minimumU,
        double minimumV,
        double maximumU,
        double maximumV);

    [[nodiscard]] bool ContainsPlanarPoint(double u, double v, double tolerance) const;

    SurfaceKind kind_;
    Wire firstBoundary_;
    std::optional<Wire> secondBoundary_;
    std::optional<WorkPlane> planarWorkPlane_;
    double minimumU_ = 0.0;
    double minimumV_ = 0.0;
    double maximumU_ = 0.0;
    double maximumV_ = 0.0;
};

} // namespace kachakacha::model
