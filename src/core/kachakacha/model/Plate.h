#pragma once

#include "kachakacha/model/Surface.h"

namespace kachakacha::model {

enum class PlateThicknessDirection {
    Positive,
    Centered,
    Negative,
};

class Plate {
public:
    Plate(Surface sourceSurface, double thickness, PlateThicknessDirection direction);

    [[nodiscard]] const Surface& SourceSurface() const noexcept { return sourceSurface_; }
    [[nodiscard]] double Thickness() const noexcept { return thickness_; }
    [[nodiscard]] PlateThicknessDirection Direction() const noexcept { return direction_; }
    [[nodiscard]] double MinimumOffset() const noexcept;
    [[nodiscard]] double MaximumOffset() const noexcept;
    [[nodiscard]] geometry::Vector3 Evaluate(double u, double v, double throughThickness) const;

private:
    Surface sourceSurface_;
    double thickness_ = 0.0;
    PlateThicknessDirection direction_ = PlateThicknessDirection::Positive;
};

} // namespace kachakacha::model
