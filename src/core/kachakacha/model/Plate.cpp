#include "kachakacha/model/Plate.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace kachakacha::model {

Plate::Plate(Surface sourceSurface, double thickness, PlateThicknessDirection direction)
    : sourceSurface_(std::move(sourceSurface)), thickness_(thickness), direction_(direction)
{
    if (!std::isfinite(thickness_) || thickness_ <= 0.0) {
        throw std::invalid_argument("Plate thickness must be a positive finite value.");
    }
    switch (direction_) {
    case PlateThicknessDirection::Positive:
    case PlateThicknessDirection::Centered:
    case PlateThicknessDirection::Negative:
        break;
    default:
        throw std::invalid_argument("Plate thickness direction is invalid.");
    }
}

double Plate::MinimumOffset() const noexcept
{
    if (direction_ == PlateThicknessDirection::Negative) {
        return -thickness_;
    }
    if (direction_ == PlateThicknessDirection::Centered) {
        return -thickness_ * 0.5;
    }
    return 0.0;
}

double Plate::MaximumOffset() const noexcept
{
    if (direction_ == PlateThicknessDirection::Positive) {
        return thickness_;
    }
    if (direction_ == PlateThicknessDirection::Centered) {
        return thickness_ * 0.5;
    }
    return 0.0;
}

geometry::Vector3 Plate::Evaluate(double u, double v, double throughThickness) const
{
    if (!std::isfinite(throughThickness)) {
        throw std::invalid_argument("Plate thickness parameter must be finite.");
    }
    const double offset = MinimumOffset()
        + (MaximumOffset() - MinimumOffset()) * std::clamp(throughThickness, 0.0, 1.0);
    return sourceSurface_.Evaluate(u, v) + sourceSurface_.Normal(u, v) * offset;
}

} // namespace kachakacha::model
