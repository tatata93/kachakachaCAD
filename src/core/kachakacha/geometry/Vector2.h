#pragma once

#include <cmath>

namespace kachakacha::geometry {

struct Vector2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vector2() = default;
    constexpr Vector2(double xValue, double yValue)
        : x(xValue), y(yValue) {}

    [[nodiscard]] bool IsFinite() const noexcept
    {
        return std::isfinite(x) && std::isfinite(y);
    }
};

[[nodiscard]] constexpr Vector2 operator+(const Vector2& lhs, const Vector2& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

[[nodiscard]] constexpr Vector2 operator-(const Vector2& lhs, const Vector2& rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

[[nodiscard]] constexpr Vector2 operator*(const Vector2& value, double scale) noexcept
{
    return {value.x * scale, value.y * scale};
}

[[nodiscard]] constexpr Vector2 operator*(double scale, const Vector2& value) noexcept
{
    return value * scale;
}

} // namespace kachakacha::geometry

