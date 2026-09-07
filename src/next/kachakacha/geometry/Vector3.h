#pragma once

//! V2の3次元ベクトル。値型で、有限値かどうかを自分で答えられる。

#include "kachakacha/geometry/Units.h"

#include <cmath>

namespace kachakacha::v2::geometry {

struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] constexpr Vector3 operator+(const Vector3& o) const noexcept
    {
        return {x + o.x, y + o.y, z + o.z};
    }
    [[nodiscard]] constexpr Vector3 operator-(const Vector3& o) const noexcept
    {
        return {x - o.x, y - o.y, z - o.z};
    }
    [[nodiscard]] constexpr Vector3 operator*(double s) const noexcept
    {
        return {x * s, y * s, z * s};
    }
    [[nodiscard]] constexpr Vector3 operator-() const noexcept { return {-x, -y, -z}; }

    [[nodiscard]] constexpr double LengthSquared() const noexcept
    {
        return x * x + y * y + z * z;
    }
    [[nodiscard]] double Length() const noexcept { return std::sqrt(LengthSquared()); }

    //! NaN も Infinity も含まないこと。幾何へ入れる前に必ず通す。
    [[nodiscard]] bool IsFinite() const noexcept
    {
        return geometry::IsFinite(x) && geometry::IsFinite(y) && geometry::IsFinite(z);
    }

    friend constexpr bool operator==(const Vector3& l, const Vector3& r) noexcept
    {
        return l.x == r.x && l.y == r.y && l.z == r.z;
    }
    friend constexpr bool operator!=(const Vector3& l, const Vector3& r) noexcept
    {
        return !(l == r);
    }
};

[[nodiscard]] constexpr double Dot(const Vector3& l, const Vector3& r) noexcept
{
    return l.x * r.x + l.y * r.y + l.z * r.z;
}

[[nodiscard]] constexpr Vector3 Cross(const Vector3& l, const Vector3& r) noexcept
{
    return {l.y * r.z - l.z * r.y, l.z * r.x - l.x * r.z, l.x * r.y - l.y * r.x};
}

//! 長さが許容差以下なら、正規化せずに零ベクトルを返す。0除算を結果へ残さない。
[[nodiscard]] inline Vector3 Normalized(const Vector3& value, double epsilon = 1.0e-12)
{
    const double length = value.Length();
    if (!(length > epsilon)) {
        return {0.0, 0.0, 0.0};
    }
    return value * (1.0 / length);
}

[[nodiscard]] inline double Distance(const Vector3& l, const Vector3& r) noexcept
{
    return (l - r).Length();
}

//! 軸並行の外接箱。
struct Bounds3 {
    Vector3 minimum{};
    Vector3 maximum{};
    bool empty = true;

    void Add(const Vector3& point) noexcept
    {
        if (empty) {
            minimum = point;
            maximum = point;
            empty = false;
            return;
        }
        minimum = {std::fmin(minimum.x, point.x), std::fmin(minimum.y, point.y),
            std::fmin(minimum.z, point.z)};
        maximum = {std::fmax(maximum.x, point.x), std::fmax(maximum.y, point.y),
            std::fmax(maximum.z, point.z)};
    }

    [[nodiscard]] Vector3 Size() const noexcept
    {
        return empty ? Vector3{} : maximum - minimum;
    }
    [[nodiscard]] double DiagonalLength() const noexcept { return Size().Length(); }
};

} // namespace kachakacha::v2::geometry
