#pragma once

#include <optional>

namespace kachakacha::model {

struct WireLineConstraints {
    std::optional<double> lengthMillimeters;
    std::optional<double> angleDegrees;

    [[nodiscard]] bool Empty() const noexcept
    {
        return !lengthMillimeters.has_value() && !angleDegrees.has_value();
    }
};

struct WireCurveConstraints {
    std::optional<double> radiusMillimeters;

    [[nodiscard]] bool Empty() const noexcept
    {
        return !radiusMillimeters.has_value();
    }
};

} // namespace kachakacha::model
