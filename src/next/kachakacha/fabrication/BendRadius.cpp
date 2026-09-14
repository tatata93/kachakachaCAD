#include "kachakacha/fabrication/BendRadius.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;

constexpr const char* kBadLength = "FAB-N001";
constexpr const char* kBadRadius = "FAB-N002";
constexpr const char* kBadPercent = "FAB-N003";

[[nodiscard]] bool PercentIsSane(double percent)
{
    return std::isfinite(percent) && percent >= 0.0 && percent <= 100.0;
}

} // namespace

std::string_view ValueLockNameJa(ValueLock value) noexcept
{
    switch (value) {
    case ValueLock::Auto:   return "自動";
    case ValueLock::Locked: return "固定";
    }
    return "不明";
}

Result<double> FullSweepAngleRad(const BendRadius& bend)
{
    using Out = Result<double>;
    if (!(bend.flatLengthMm > 0.0) || !std::isfinite(bend.flatLengthMm)) {
        return Out::Failure(MakeError(kBadLength, "板の長さが正の数ではありません。",
            "曲げる向きの長さが決まっていません。"));
    }
    if (!(bend.radiusMm > 0.0) || !std::isfinite(bend.radiusMm)) {
        return Out::Failure(MakeError(kBadRadius, "半径が正の数ではありません。",
            "半径 0 では曲げられません。"));
    }
    return Out::Success(bend.flatLengthMm / bend.radiusMm);
}

Result<double> SweepAngleRadAt(const BendRadius& bend, double percent)
{
    using Out = Result<double>;
    if (!PercentIsSane(percent)) {
        return Out::Failure(MakeError(kBadPercent, "曲げ具合は 0〜100% の間です。", {}));
    }
    const auto full = FullSweepAngleRad(bend);
    if (!full.HasValue()) {
        return Out::Failure(full.Diagnostics());
    }
    return Out::Success(full.Value() * percent / 100.0);
}

std::optional<double> RadiusAtPercent(const BendRadius& bend, double percent)
{
    if (!PercentIsSane(percent) || percent <= 0.0) {
        return std::nullopt;   // 平ら。半径は決まらない。
    }
    const auto angle = SweepAngleRadAt(bend, percent);
    if (!angle.HasValue() || !(angle.Value() > 0.0)) {
        return std::nullopt;
    }
    // 面内長は変わらないので、R = L / θ。
    return bend.flatLengthMm / angle.Value();
}

Result<BendRadius> LockRadiusAtPercent(BendRadius bend, double radiusMm, double percent)
{
    using Out = Result<BendRadius>;
    if (!(radiusMm > 0.0) || !std::isfinite(radiusMm)) {
        return Out::Failure(MakeError(kBadRadius, "半径が正の数ではありません。",
            "作れる半径を入れてください。"));
    }
    if (!PercentIsSane(percent) || percent <= 0.0) {
        return Out::Failure(MakeError(kBadPercent,
            "平らな状態では半径を決められません。",
            "先に少し曲げてから半径を入れてください。"));
    }
    if (!(bend.flatLengthMm > 0.0) || !std::isfinite(bend.flatLengthMm)) {
        return Out::Failure(MakeError(kBadLength, "板の長さが正の数ではありません。", {}));
    }
    // いまの曲げ具合で入れた半径になるように、100% の半径を決める。
    // θ(p) = L / R(入力)、θ100 = θ(p) * 100 / p、R100 = L / θ100。
    bend.radiusMm = radiusMm * percent / 100.0;
    bend.lock = ValueLock::Locked;
    return Out::Success(bend);
}

BendRadius UnlockRadius(BendRadius bend, double measuredRadiusMm)
{
    bend.lock = ValueLock::Auto;
    if (measuredRadiusMm > 0.0 && std::isfinite(measuredRadiusMm)) {
        bend.radiusMm = measuredRadiusMm;
    }
    return bend;
}

BendRadius RefitRadius(BendRadius bend, double measuredRadiusMm)
{
    // 固定してあるなら触らない。勝手に自動値へ戻さない(§31)。
    if (bend.lock == ValueLock::Locked) {
        return bend;
    }
    if (measuredRadiusMm > 0.0 && std::isfinite(measuredRadiusMm)) {
        bend.radiusMm = measuredRadiusMm;
    }
    return bend;
}

std::string DescribeBendRadiusJa(const BendRadius& bend, double percent)
{
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    const auto radius = RadiusAtPercent(bend, percent);
    if (!radius.has_value()) {
        text << "平ら(0%)。曲げると半径が決まります。";
        return text.str();
    }
    text << "R = " << *radius << " mm " << ValueLockNameJa(bend.lock) << "(曲げ "
         << std::setprecision(0) << percent << "%)";
    return text.str();
}

} // namespace kachakacha::v2::fabrication
