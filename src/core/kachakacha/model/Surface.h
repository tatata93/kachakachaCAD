#pragma once

#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <optional>
#include <vector>

namespace kachakacha::model {

enum class SurfaceKind {
    Planar,
    Ruled,
    Loft,
    GuidedLoft,
};

struct SurfaceProjection {
    geometry::Vector3 point;
    double u = 0.0;
    double v = 0.0;
    double distanceAlongDirection = 0.0;
};

struct SurfaceCurvature {
    double gaussian = 0.0;
    double mean = 0.0;
    double principalMinimum = 0.0;
    double principalMaximum = 0.0;
};

class Surface {
public:
    [[nodiscard]] static Surface Planar(Wire closedBoundary, double tolerance = 1.0e-7);
    [[nodiscard]] static Surface Ruled(Wire firstSection, Wire secondSection);
    [[nodiscard]] static Surface Loft(std::vector<Wire> sections);
    [[nodiscard]] static Surface GuidedLoft(
        Wire firstGuide,
        Wire secondGuide,
        std::vector<Wire> sections,
        double connectionToleranceMillimeters = 0.05);

    [[nodiscard]] SurfaceKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const Wire& FirstBoundary() const noexcept { return boundaries_.front(); }
    [[nodiscard]] const std::vector<Wire>& Boundaries() const noexcept { return boundaries_; }
    [[nodiscard]] const std::vector<Wire>& Guides() const noexcept { return guides_; }
    [[nodiscard]] const std::optional<WorkPlane>& PlanarWorkPlane() const noexcept { return planarWorkPlane_; }
    [[nodiscard]] geometry::Vector3 Evaluate(double u, double v) const;
    [[nodiscard]] geometry::Vector3 Normal(double u, double v) const;
    [[nodiscard]] SurfaceCurvature Curvature(double u, double v) const;
    [[nodiscard]] SurfaceProjection ProjectPointAlongDirection(
        geometry::Vector3 sourcePoint,
        geometry::Vector3 direction,
        double tolerance = 1.0e-7) const;
    [[nodiscard]] std::vector<SurfaceProjection> ProjectPointsAlongDirection(
        const std::vector<geometry::Vector3>& sourcePoints,
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
        std::vector<Wire> boundaries,
        std::optional<WorkPlane> planarWorkPlane,
        double minimumU,
        double minimumV,
        double maximumU,
        double maximumV,
        std::vector<Wire> guides = {},
        std::vector<double> sectionParameters = {},
        std::vector<double> firstGuideParameters = {},
        std::vector<double> secondGuideParameters = {});

    [[nodiscard]] bool ContainsPlanarPoint(double u, double v, double tolerance) const;

    SurfaceKind kind_;
    std::vector<Wire> boundaries_;
    std::vector<Wire> guides_;
    std::vector<double> sectionParameters_;
    std::vector<double> firstGuideParameters_;
    std::vector<double> secondGuideParameters_;
    std::optional<WorkPlane> planarWorkPlane_;
    double minimumU_ = 0.0;
    double minimumV_ = 0.0;
    double maximumU_ = 0.0;
    double maximumV_ = 0.0;
};

} // namespace kachakacha::model
