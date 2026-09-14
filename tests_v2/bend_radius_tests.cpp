// 曲げ具合と半径の結びつき、AUTO と LOCK(§30・§31)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/BendRadius.h"

#include <cmath>
#include <string>

using kachakacha::v2::fabrication::BendRadius;
using kachakacha::v2::fabrication::DescribeBendRadiusJa;
using kachakacha::v2::fabrication::FullSweepAngleRad;
using kachakacha::v2::fabrication::LockRadiusAtPercent;
using kachakacha::v2::fabrication::RadiusAtPercent;
using kachakacha::v2::fabrication::RefitRadius;
using kachakacha::v2::fabrication::SweepAngleRadAt;
using kachakacha::v2::fabrication::UnlockRadius;
using kachakacha::v2::fabrication::ValueLock;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

namespace {

//! HO の前頭部の肩あたりを想定。長さ 34mm、100% で R = 21.63mm。
[[nodiscard]] BendRadius Shoulder()
{
    BendRadius bend;
    bend.flatLengthMm = 34.0;
    bend.radiusMm = 21.63;
    return bend;
}

} // namespace

KACHA_V2_TEST(bend_radius, 曲げても面内長が変わらない)
{
    // §29 のいちばん大事な決まり。板は伸びない。
    const BendRadius bend = Shoulder();
    for (const double percent : {10.0, 25.0, 50.0, 75.0, 100.0}) {
        const auto angle = SweepAngleRadAt(bend, percent);
        Require(angle.HasValue(), "曲げ角が出る");
        const auto radius = RadiusAtPercent(bend, percent);
        Require(radius.has_value(), "半径が出る");
        // 弧の長さ = R * θ。どの曲げ具合でも板の長さと同じでなければならない。
        RequireNear(*radius * angle.Value(), bend.flatLengthMm, 1.0e-9,
            "弧の長さが板の長さと同じ(" + std::to_string(percent) + "%)");
    }
}

KACHA_V2_TEST(bend_radius, 曲げるほど半径が小さくなる)
{
    const BendRadius bend = Shoulder();
    double previous = 1.0e30;
    for (const double percent : {10.0, 25.0, 50.0, 75.0, 100.0}) {
        const auto radius = RadiusAtPercent(bend, percent);
        Require(radius.has_value(), "半径が出る");
        Require(*radius < previous, "曲げるほど小さくなる");
        previous = *radius;
    }
    RequireNear(previous, 21.63, 1.0e-9, "100% で指定の半径になる");
}

KACHA_V2_TEST(bend_radius, 平らなときは半径が決まらない)
{
    const BendRadius bend = Shoulder();
    Require(!RadiusAtPercent(bend, 0.0).has_value(), "0% では半径が無い");
    const auto text = DescribeBendRadiusJa(bend, 0.0);
    Require(text.find("平ら") != std::string::npos, "平らだと言う");
}

KACHA_V2_TEST(bend_radius, 半径を入れると固定になり作り直しても戻らない)
{
    // §31。21.63mm AUTO のところへ 22.00mm を入れたら 22.00mm LOCK。
    BendRadius bend = Shoulder();
    Require(bend.lock == ValueLock::Auto, "はじめは自動");
    const auto locked = LockRadiusAtPercent(bend, 22.0, 100.0);
    Require(locked.HasValue(), "半径を入れられる");
    bend = locked.Value();
    Require(bend.lock == ValueLock::Locked, "固定になる");
    RequireNear(*RadiusAtPercent(bend, 100.0), 22.0, 1.0e-9, "入れた値になる");

    // 近似をやり直しても、固定は戻らない。
    bend = RefitRadius(bend, 21.63);
    RequireNear(*RadiusAtPercent(bend, 100.0), 22.0, 1.0e-9,
        "作り直しても自動値へ戻らない");
    Require(bend.lock == ValueLock::Locked, "固定のまま");

    // 固定を外すと自動へ戻る。
    bend = UnlockRadius(bend, 21.63);
    Require(bend.lock == ValueLock::Auto, "自動へ戻る");
    RequireNear(*RadiusAtPercent(bend, 100.0), 21.63, 1.0e-9, "測った値になる");
}

KACHA_V2_TEST(bend_radius, 自動なら作り直しで更新される)
{
    BendRadius bend = Shoulder();
    bend = RefitRadius(bend, 18.5);
    RequireNear(*RadiusAtPercent(bend, 100.0), 18.5, 1.0e-9, "測り直した値になる");
}

KACHA_V2_TEST(bend_radius, 途中の曲げ具合でも半径を入れられる)
{
    // §30。70% のときに R = 22mm と入れたら、70% で 22mm になる。
    BendRadius bend = Shoulder();
    const auto locked = LockRadiusAtPercent(bend, 22.0, 70.0);
    Require(locked.HasValue(), "途中でも入れられる");
    bend = locked.Value();
    RequireNear(*RadiusAtPercent(bend, 70.0), 22.0, 1.0e-9, "その曲げ具合で合う");
    // 面内長は変わっていない。
    const auto angle = SweepAngleRadAt(bend, 70.0);
    RequireNear(22.0 * angle.Value(), bend.flatLengthMm, 1.0e-9, "板の長さは同じ");
}

KACHA_V2_TEST(bend_radius, 作れない値は断る)
{
    BendRadius bend = Shoulder();
    Require(!LockRadiusAtPercent(bend, 0.0, 100.0).HasValue(), "半径0は断る");
    Require(!LockRadiusAtPercent(bend, -5.0, 100.0).HasValue(), "負の半径は断る");
    Require(!LockRadiusAtPercent(bend, 22.0, 0.0).HasValue(),
        "平らな状態では半径を決められない");
    Require(!SweepAngleRadAt(bend, 120.0).HasValue(), "100% を超える曲げは断る");
    BendRadius broken;
    Require(!FullSweepAngleRad(broken).HasValue(), "長さも半径も無ければ断る");
}

KACHA_V2_TEST(bend_radius, 画面に出す一文に自動か固定かが出る)
{
    BendRadius bend = Shoulder();
    const auto autoText = DescribeBendRadiusJa(bend, 100.0);
    Require(autoText.find("21.63") != std::string::npos, "半径を出す");
    Require(autoText.find("自動") != std::string::npos, "自動と言う");
    bend = LockRadiusAtPercent(bend, 22.0, 100.0).Value();
    const auto lockedText = DescribeBendRadiusJa(bend, 100.0);
    Require(lockedText.find("22.00") != std::string::npos, "入れた半径を出す");
    Require(lockedText.find("固定") != std::string::npos, "固定と言う");
    // 内部の言葉を出さない。
    Require(lockedText.find("Lock") == std::string::npos, "内部の言葉を出さない");
}

KACHA_V2_TEST_MAIN("bend_radius_tests")
