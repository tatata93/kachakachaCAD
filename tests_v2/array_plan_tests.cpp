// 配列(並べて複製する)。棚卸し B-2。
#include "kachakacha/app/ArrayPlan.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::app::CircularArrayStep;
using kachakacha::v2::app::PlanCircularArray;
using kachakacha::v2::app::PlanLinearArray;
using kachakacha::v2::app::kArrayAngleZero;
using kachakacha::v2::app::kArrayAxisUnknown;
using kachakacha::v2::app::kArrayCountTooLarge;
using kachakacha::v2::app::kArrayCountTooSmall;
using kachakacha::v2::app::kArrayDirectionUnknown;
using kachakacha::v2::app::kMaximumArrayCount;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

constexpr double kPi = 3.14159265358979323846;

} // namespace

KACHA_V2_TEST(array_plan, 間隔で並べる)
{
    // 窓を 5 個、20mm おき。元は動かさないので、移動量は 4 個。
    const auto plan = PlanLinearArray(Vector3{20.0, 0.0, 0.0}, 5, false);
    Require(plan.HasValue(), "並べられる");
    Require(plan.Value().size() == 4, "元を除いて4個");
    RequireNear(plan.Value().front().x, 20.0, 1.0e-9, "1つ目は 20mm");
    RequireNear(plan.Value().back().x, 80.0, 1.0e-9, "4つ目は 80mm");
}

KACHA_V2_TEST(array_plan, 全体の長さで並べる)
{
    // 「端から端まで 80mm の間に 5 個」。手で 80/4 を割らなくてよい。
    const auto plan = PlanLinearArray(Vector3{80.0, 0.0, 0.0}, 5, true);
    Require(plan.HasValue(), "並べられる");
    Require(plan.Value().size() == 4, "元を除いて4個");
    RequireNear(plan.Value().front().x, 20.0, 1.0e-9, "間隔は 20mm");
    RequireNear(plan.Value().back().x, 80.0, 1.0e-9, "最後が端に来る");
}

KACHA_V2_TEST(array_plan, 斜めにも並べられる)
{
    const auto plan = PlanLinearArray(Vector3{3.0, 4.0, 0.0}, 3, false);
    Require(plan.HasValue(), "並べられる");
    RequireNear(plan.Value()[1].x, 6.0, 1.0e-9, "2つ目の x");
    RequireNear(plan.Value()[1].y, 8.0, 1.0e-9, "2つ目の y");
}

KACHA_V2_TEST(array_plan, 2個が最小)
{
    Require(PlanLinearArray(Vector3{10.0, 0.0, 0.0}, 2, false).HasValue(), "2個は並ぶ");
    const auto one = PlanLinearArray(Vector3{10.0, 0.0, 0.0}, 1, false);
    Require(!one.HasValue(), "1個は並びでない");
    RequireEqual(FirstCode(one.Diagnostics()), std::string(kArrayCountTooSmall), "UI-A001");
}

KACHA_V2_TEST(array_plan, 多すぎる数は断る)
{
    // 20000 と打ち間違えたときに、画面が固まる前に断る。
    const auto plan = PlanLinearArray(Vector3{1.0, 0.0, 0.0}, kMaximumArrayCount + 1, false);
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kArrayCountTooLarge), "UI-A002");
    Require(PlanLinearArray(Vector3{1.0, 0.0, 0.0}, kMaximumArrayCount, false).HasValue(),
        "上限ちょうどは通る");
}

KACHA_V2_TEST(array_plan, 長さ0の間隔は断る)
{
    const auto plan = PlanLinearArray(Vector3{}, 5, false);
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kArrayDirectionUnknown), "UI-A003");
}

KACHA_V2_TEST(array_plan, 数値でない間隔は断る)
{
    const auto plan = PlanLinearArray(Vector3{std::nan(""), 0.0, 0.0}, 5, false);
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kArrayDirectionUnknown), "UI-A003");
}

KACHA_V2_TEST(array_plan, 円に並べる)
{
    // 90 度の中に 4 個(両端に置く)。間は 30 度。
    const auto plan = PlanCircularArray(Vector3{0.0, 0.0, 1.0}, 90.0, 4);
    Require(plan.HasValue(), "並べられる");
    Require(plan.Value().size() == 3, "元を除いて3個");
    RequireNear(plan.Value().front().angleRad, 30.0 * kPi / 180.0, 1.0e-9, "30度");
    RequireNear(plan.Value().back().angleRad, 90.0 * kPi / 180.0, 1.0e-9, "端は90度");
}

KACHA_V2_TEST(array_plan, 一周は最後を元に重ねない)
{
    // 360 度に 6 個なら 60 度おき。両端に置くと 6 個目が元の上に重なる。
    const auto plan = PlanCircularArray(Vector3{0.0, 0.0, 1.0}, 360.0, 6);
    Require(plan.HasValue(), "並べられる");
    Require(plan.Value().size() == 5, "元を除いて5個");
    RequireNear(plan.Value().front().angleRad, 60.0 * kPi / 180.0, 1.0e-9, "60度おき");
    RequireNear(plan.Value().back().angleRad, 300.0 * kPi / 180.0, 1.0e-9, "最後は300度");
}

KACHA_V2_TEST(array_plan, 逆回りにも並べる)
{
    const auto plan = PlanCircularArray(Vector3{0.0, 0.0, 1.0}, -90.0, 4);
    Require(plan.HasValue(), "並べられる");
    Require(plan.Value().front().angleRad < 0.0, "負の向き");
    RequireNear(plan.Value().back().angleRad, -90.0 * kPi / 180.0, 1.0e-9, "端は-90度");
}

KACHA_V2_TEST(array_plan, 角度0は断る)
{
    const auto plan = PlanCircularArray(Vector3{0.0, 0.0, 1.0}, 0.0, 4);
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kArrayAngleZero), "UI-A004");
}

KACHA_V2_TEST(array_plan, 長さ0の軸は断る)
{
    const auto plan = PlanCircularArray(Vector3{}, 90.0, 4);
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kArrayAxisUnknown), "UI-A005");
}

KACHA_V2_TEST(array_plan, 円でも2個が最小)
{
    const auto plan = PlanCircularArray(Vector3{0.0, 0.0, 1.0}, 90.0, 1);
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kArrayCountTooSmall), "UI-A001");
}

KACHA_V2_TEST_MAIN("array_plan_tests")
