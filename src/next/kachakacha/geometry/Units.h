#pragma once

//! 単位。文書内部は長さmm・角度rad、画面表示の角度はdeg(geometry-contract §1)。
//! 型を分けて、degとradの取り違えをコンパイル時に落とす。

#include <cmath>

namespace kachakacha::v2::geometry {

//! 有限なdoubleだけを通す。NaN と Infinity は幾何へ入れない。
[[nodiscard]] inline bool IsFinite(double value) noexcept
{
    return std::isfinite(value);
}

//! ラジアン。文書内部の角度はこれ。
class Radians {
public:
    constexpr Radians() noexcept = default;
    explicit constexpr Radians(double value) noexcept : value_(value) {}
    [[nodiscard]] constexpr double Value() const noexcept { return value_; }

    friend constexpr Radians operator+(Radians l, Radians r) noexcept
    {
        return Radians(l.value_ + r.value_);
    }
    friend constexpr Radians operator-(Radians l, Radians r) noexcept
    {
        return Radians(l.value_ - r.value_);
    }
    friend constexpr bool operator==(Radians l, Radians r) noexcept
    {
        return l.value_ == r.value_;
    }
    friend constexpr bool operator!=(Radians l, Radians r) noexcept { return !(l == r); }

private:
    double value_ = 0.0;
};

//! 度。画面と入力欄だけで使う。保存してはならない。
class Degrees {
public:
    constexpr Degrees() noexcept = default;
    explicit constexpr Degrees(double value) noexcept : value_(value) {}
    [[nodiscard]] constexpr double Value() const noexcept { return value_; }

    friend constexpr bool operator==(Degrees l, Degrees r) noexcept
    {
        return l.value_ == r.value_;
    }
    friend constexpr bool operator!=(Degrees l, Degrees r) noexcept { return !(l == r); }

private:
    double value_ = 0.0;
};

inline constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] constexpr Radians ToRadians(Degrees degrees) noexcept
{
    return Radians(degrees.Value() * kPi / 180.0);
}

[[nodiscard]] constexpr Degrees ToDegrees(Radians radians) noexcept
{
    return Degrees(radians.Value() * 180.0 / kPi);
}

//! ミリメートル。文書内部の長さはこれ。
class Millimeters {
public:
    constexpr Millimeters() noexcept = default;
    explicit constexpr Millimeters(double value) noexcept : value_(value) {}
    [[nodiscard]] constexpr double Value() const noexcept { return value_; }

    friend constexpr Millimeters operator+(Millimeters l, Millimeters r) noexcept
    {
        return Millimeters(l.value_ + r.value_);
    }
    friend constexpr Millimeters operator-(Millimeters l, Millimeters r) noexcept
    {
        return Millimeters(l.value_ - r.value_);
    }
    friend constexpr bool operator<(Millimeters l, Millimeters r) noexcept
    {
        return l.value_ < r.value_;
    }
    friend constexpr bool operator==(Millimeters l, Millimeters r) noexcept
    {
        return l.value_ == r.value_;
    }
    friend constexpr bool operator!=(Millimeters l, Millimeters r) noexcept
    {
        return !(l == r);
    }

private:
    double value_ = 0.0;
};

//! 入力欄の単位接尾辞から mm へ直す。対応しない単位は受け付けない。
enum class LengthUnit { Millimeter, Centimeter, Meter, Inch };

[[nodiscard]] constexpr double LengthUnitToMillimeters(LengthUnit unit) noexcept
{
    switch (unit) {
    case LengthUnit::Millimeter: return 1.0;
    case LengthUnit::Centimeter: return 10.0;
    case LengthUnit::Meter:      return 1000.0;
    case LengthUnit::Inch:       return 25.4;
    }
    return 1.0;
}

} // namespace kachakacha::v2::geometry
