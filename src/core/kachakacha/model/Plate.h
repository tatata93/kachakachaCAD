#pragma once

#include "kachakacha/model/Surface.h"

#include <utility>

namespace kachakacha::model {

enum class PlateThicknessDirection {
    Positive,
    Centered,
    Negative,
};

enum class PlateDevelopability {
    Planar,
    Developable,
    DoubleCurved,
};

struct PlateDevelopabilityAnalysis {
    PlateDevelopability classification = PlateDevelopability::DoubleCurved;
    double maximumAbsoluteGaussianCurvature = 0.0;
    double maximumNormalAngleRadians = 0.0;
};

enum class PlateSplitAxis {
    U,
    V,
};

struct PlateSurfaceRange {
    double minimumU = 0.0;
    double maximumU = 1.0;
    double minimumV = 0.0;
    double maximumV = 1.0;

    [[nodiscard]] bool IsFull(double tolerance = 1.0e-12) const noexcept;
};

class Plate {
public:
    Plate(
        Surface sourceSurface,
        double thickness,
        PlateThicknessDirection direction,
        PlateSurfaceRange range = {});
    Plate(
        Surface sourceSurface,
        double startThickness,
        double endThickness,
        PlateThicknessDirection direction,
        PlateSurfaceRange range = {});

    [[nodiscard]] const Surface& SourceSurface() const noexcept { return sourceSurface_; }
    [[nodiscard]] double Thickness() const noexcept { return thickness_; }
    [[nodiscard]] double EndThickness() const noexcept { return endThickness_; }
    [[nodiscard]] double Thickness(double localV) const noexcept;
    [[nodiscard]] bool HasVariableThickness(double tolerance = 1.0e-12) const noexcept;
    [[nodiscard]] PlateThicknessDirection Direction() const noexcept { return direction_; }
    [[nodiscard]] const PlateSurfaceRange& Range() const noexcept { return range_; }
    [[nodiscard]] double MinimumOffset() const noexcept;
    [[nodiscard]] double MaximumOffset() const noexcept;
    [[nodiscard]] double MinimumOffset(double localV) const noexcept;
    [[nodiscard]] double MaximumOffset(double localV) const noexcept;
    [[nodiscard]] double SourceU(double localU) const noexcept;
    [[nodiscard]] double SourceV(double localV) const noexcept;
    [[nodiscard]] geometry::Vector3 Evaluate(double u, double v, double throughThickness) const;
    [[nodiscard]] std::pair<Plate, Plate> Split(PlateSplitAxis axis, double parameter) const;
    [[nodiscard]] PlateDevelopabilityAnalysis AnalyzeDevelopability(
        int uSamples = 10,
        int vSamples = 10) const;

private:
    Surface sourceSurface_;
    double thickness_ = 0.0;
    double endThickness_ = 0.0;
    PlateThicknessDirection direction_ = PlateThicknessDirection::Positive;
    PlateSurfaceRange range_;
};

} // namespace kachakacha::model
