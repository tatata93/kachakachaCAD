#pragma once

#include <cmath>
#include <stdexcept>

namespace kachakacha::geometry {

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vector3() = default;
    constexpr Vector3(double xValue, double yValue, double zValue)
        : x(xValue), y(yValue), z(zValue) {}

    [[nodiscard]] bool IsFinite() const noexcept;
    [[nodiscard]] double LengthSquared() const noexcept;
    [[nodiscard]] double Length() const noexcept;
    [[nodiscard]] Vector3 Normalized(double epsilon = 1.0e-9) const;
};

[[nodiscard]] constexpr Vector3 operator+(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] constexpr Vector3 operator-(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] constexpr Vector3 operator-(const Vector3& value) noexcept
{
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] constexpr Vector3 operator*(const Vector3& value, double scale) noexcept
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] constexpr Vector3 operator*(double scale, const Vector3& value) noexcept
{
    return value * scale;
}

[[nodiscard]] constexpr Vector3 operator/(const Vector3& value, double scale)
{
    return {value.x / scale, value.y / scale, value.z / scale};
}

[[nodiscard]] constexpr double Dot(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] constexpr Vector3 Cross(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] bool AlmostEqual(double lhs, double rhs, double epsilon = 1.0e-9) noexcept;
[[nodiscard]] bool AlmostEqual(const Vector3& lhs, const Vector3& rhs, double epsilon = 1.0e-9) noexcept;

} // namespace kachakacha::geometry

