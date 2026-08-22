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

} // namespace kachakacha::model
