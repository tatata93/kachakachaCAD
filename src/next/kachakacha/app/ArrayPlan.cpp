#include "kachakacha/app/ArrayPlan.h"

#include <cmath>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;

namespace {

constexpr double kPi = 3.14159265358979323846;
//! 一周とみなすしきい(度)。
constexpr double kFullTurnEpsilonDeg = 1.0e-6;

[[nodiscard]] base::Result<int> CheckedCount(int count)
{
    if (count < 2) {
        return Result<int>::Failure(MakeError(kArrayCountTooSmall,
            "並べる数は 2 以上にしてください。",
            std::to_string(count) + " 個では並びません。元のものを含めた数です。"));
    }
    if (count > kMaximumArrayCount) {
        return Result<int>::Failure(MakeError(kArrayCountTooLarge,
            "並べる数が多すぎます。",
            std::to_string(count) + " 個です。" + std::to_string(kMaximumArrayCount)
                + " 個までにしてください。"));
    }
    return Result<int>::Success(count);
}

} // namespace

Result<std::vector<Vector3>> PlanLinearArray(const Vector3& vector, int count,
    bool spanIsTotal)
{
    using Out = Result<std::vector<Vector3>>;
    const auto checked = CheckedCount(count);
    if (!checked.HasValue()) {
        return Out::Failure(checked.Diagnostics());
    }
    if (!vector.IsFinite() || vector.LengthSquared() <= 0.0) {
        return Out::Failure(MakeError(kArrayDirectionUnknown, "並べる向きが決まりません。",
            "間隔(または全体の長さ)が 0 です。"));
    }
    // 全体の長さで来たら、1つ分の間隔へ直す。両端に置くので count-1 で割る。
    const Vector3 step = spanIsTotal
        ? vector * (1.0 / static_cast<double>(count - 1))
        : vector;
    std::vector<Vector3> offsets;
    offsets.reserve(static_cast<std::size_t>(count - 1));
    for (int index = 1; index < count; ++index) {
        offsets.push_back(step * static_cast<double>(index));
    }
    return Out::Success(std::move(offsets));
}

Result<std::vector<CircularArrayStep>> PlanCircularArray(const Vector3& axis,
    double totalAngleDeg, int count)
{
    using Out = Result<std::vector<CircularArrayStep>>;
    const auto checked = CheckedCount(count);
    if (!checked.HasValue()) {
        return Out::Failure(checked.Diagnostics());
    }
    if (!axis.IsFinite() || axis.LengthSquared() <= 0.0) {
        return Out::Failure(MakeError(kArrayAxisUnknown, "回す軸が決まりません。",
            "軸の長さが 0 です。"));
    }
    if (!std::isfinite(totalAngleDeg) || std::abs(totalAngleDeg) <= kFullTurnEpsilonDeg) {
        return Out::Failure(MakeError(kArrayAngleZero, "回す角度が 0 です。",
            "0 度では、全部が同じ場所に重なります。"));
    }
    // 一周なら最後の1つが元の上に重なる。重ねずに count 個で割る。
    const bool fullTurn = std::abs(std::abs(totalAngleDeg) - 360.0) <= kFullTurnEpsilonDeg;
    const double divisor = fullTurn ? static_cast<double>(count)
                                    : static_cast<double>(count - 1);
    const double stepDeg = totalAngleDeg / divisor;
    std::vector<CircularArrayStep> steps;
    steps.reserve(static_cast<std::size_t>(count - 1));
    for (int index = 1; index < count; ++index) {
        steps.push_back(CircularArrayStep{stepDeg * static_cast<double>(index)
            * kPi / 180.0});
    }
    return Out::Success(std::move(steps));
}

} // namespace kachakacha::v2::app
