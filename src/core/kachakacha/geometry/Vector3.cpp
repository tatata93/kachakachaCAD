#include "kachakacha/geometry/Vector3.h"

namespace kachakacha::geometry {

bool Vector3::IsFinite() const noexcept
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

double Vector3::LengthSquared() const noexcept
{
    return Dot(*this, *this);
}

double Vector3::Length() const noexcept
{
    return std::sqrt(LengthSquared());
}

Vector3 Vector3::Normalized(double epsilon) const
{
    const double length = Length();
    if (length <= epsilon) {
        throw std::invalid_argument("Cannot normalize a near-zero vector.");
    }

    return *this / length;
}

bool AlmostEqual(double lhs, double rhs, double epsilon) noexcept
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool AlmostEqual(const Vector3& lhs, const Vector3& rhs, double epsilon) noexcept
{
    return AlmostEqual(lhs.x, rhs.x, epsilon)
        && AlmostEqual(lhs.y, rhs.y, epsilon)
        && AlmostEqual(lhs.z, rhs.z, epsilon);
}

} // namespace kachakacha::geometry

