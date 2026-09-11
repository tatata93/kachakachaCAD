// 「選択に正対」の決め方(V1 の CadViewport::AlignToSelection の移植)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/view/FacingPlan.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;
using namespace kachakacha::v2::view;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

//! 真上から見ている姿勢の視線(目 → 模型)。
const Vector3 kFromAbove{0.0, 0.0, -1.0};

//! XY 平面に置いた 40 × 20 の四角。
[[nodiscard]] std::vector<Vector3> FlatRectangle()
{
    return {{-20.0, -10.0, 0.0}, {20.0, -10.0, 0.0}, {20.0, 10.0, 0.0}, {-20.0, 10.0, 0.0}};
}

} // namespace

KACHA_V2_TEST(facing_plan, 平面の法線へ正対する)
{
    const auto plan = PlanFacingSelection(FlatRectangle(), Vector3{0.0, 0.0, 1.0},
        Vector3{1.0, 0.0, 0.0}, kFromAbove);
    Require(plan.HasValue(), "XY の四角に正対できる");
    const Vector3 forward = ForwardOf(plan.Value().orientation);
    RequireNear(forward.z, -1.0, 1.0e-9, "真上から見下ろす向きになる");
}

KACHA_V2_TEST(facing_plan, 選んだものを画面の真ん中へ持ってくる)
{
    // 原点から離れたところに置く。向きだけ変えて中身が画面の外にあると
    // 「正対がきいていない」ようにしか見えない。
    std::vector<Vector3> moved;
    for (const Vector3& point : FlatRectangle()) {
        moved.push_back(point + Vector3{300.0, -150.0, 25.0});
    }
    const auto plan = PlanFacingSelection(moved, Vector3{0.0, 0.0, 1.0},
        Vector3{1.0, 0.0, 0.0}, kFromAbove);
    Require(plan.HasValue(), "離れた四角にも正対できる");
    RequireNear(plan.Value().center.x, 300.0, 1.0e-9, "注視点は囲みの中心(x)");
    RequireNear(plan.Value().center.y, -150.0, 1.0e-9, "注視点は囲みの中心(y)");
    RequireNear(plan.Value().center.z, 25.0, 1.0e-9, "注視点は囲みの中心(z)");
}

KACHA_V2_TEST(facing_plan, 画面に収める大きさは囲みの大きいほう)
{
    const auto plan = PlanFacingSelection(FlatRectangle(), Vector3{0.0, 0.0, 1.0},
        Vector3{1.0, 0.0, 0.0}, kFromAbove);
    Require(plan.HasValue(), "正対できる");
    RequireNear(plan.Value().spanMm, 40.0, 1.0e-9, "横 40 と縦 20 の大きいほう");
}

KACHA_V2_TEST(facing_plan, 点1つでも寄りすぎない)
{
    const auto plan = PlanFacingSelection({Vector3{5.0, 5.0, 0.0}}, Vector3{0.0, 0.0, 1.0},
        Vector3{1.0, 0.0, 0.0}, kFromAbove);
    Require(plan.HasValue(), "点1つにも正対できる");
    RequireNear(plan.Value().spanMm, 10.0, 1.0e-9, "下限の 10mm まで");
}

KACHA_V2_TEST(facing_plan, いま見ている側に留まる)
{
    // 下から見上げているときに、上向きの面へ正対しても裏返らない。
    const Vector3 fromBelow{0.0, 0.0, 1.0};
    const auto plan = PlanFacingSelection(FlatRectangle(), Vector3{0.0, 0.0, 1.0},
        Vector3{1.0, 0.0, 0.0}, fromBelow);
    Require(plan.HasValue(), "正対できる");
    const Vector3 forward = ForwardOf(plan.Value().orientation);
    Require(Dot(forward, fromBelow) > 0.0, "見上げたまま。裏へ回り込まない");
}

KACHA_V2_TEST(facing_plan, 横の見当から上向きを作る)
{
    // 横を +Y にすると、縦は 法線 × 横 = -X。画面の上が -X を向く。
    const auto plan = PlanFacingSelection(FlatRectangle(), Vector3{0.0, 0.0, 1.0},
        Vector3{0.0, 1.0, 0.0}, kFromAbove);
    Require(plan.HasValue(), "正対できる");
    const Vector3 up = UpOf(plan.Value().orientation);
    RequireNear(up.x, -1.0, 1.0e-9, "上は -X");
    RequireNear(up.y, 0.0, 1.0e-9, "上に Y は混ざらない");
}

KACHA_V2_TEST(facing_plan, 傾いた面にも正対する)
{
    const Vector3 normal{0.0, 1.0, 1.0};
    const std::vector<Vector3> points{
        {-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {10.0, 10.0, -10.0}, {-10.0, 10.0, -10.0}};
    const auto plan = PlanFacingSelection(points, normal, Vector3{1.0, 0.0, 0.0}, kFromAbove);
    Require(plan.HasValue(), "45度の面に正対できる");
    const Vector3 forward = ForwardOf(plan.Value().orientation);
    const Vector3 expected{0.0, -1.0 / std::sqrt(2.0), -1.0 / std::sqrt(2.0)};
    RequireNear(Dot(forward, expected), 1.0, 1.0e-9, "面の法線の逆を向く");
}

KACHA_V2_TEST(facing_plan, 空の形は断る)
{
    const auto plan = PlanFacingSelection({}, Vector3{0.0, 0.0, 1.0}, Vector3{1.0, 0.0, 0.0},
        kFromAbove);
    Require(!plan.HasValue(), "点が無ければ断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kFacingNoPoints), "UI-V008");
}

KACHA_V2_TEST(facing_plan, 長さ0の法線は断る)
{
    const auto plan = PlanFacingSelection(FlatRectangle(), Vector3{}, Vector3{1.0, 0.0, 0.0},
        kFromAbove);
    Require(!plan.HasValue(), "法線が決まらなければ断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string(kFacingNoPlane), "UI-V009");
}

KACHA_V2_TEST(facing_plan, 数値でない点は断る)
{
    const std::vector<Vector3> points{{0.0, 0.0, 0.0},
        {std::nan(""), 0.0, 0.0}};
    const auto plan = PlanFacingSelection(points, Vector3{0.0, 0.0, 1.0},
        Vector3{1.0, 0.0, 0.0}, kFromAbove);
    Require(!plan.HasValue(), "数値でない点は断る");
    RequireEqual(FirstCode(plan.Diagnostics()), std::string("UI-V001"), "UI-V001");
}

KACHA_V2_TEST(facing_plan, 点の並びから平面を推す)
{
    const auto normal = BestFitNormal(FlatRectangle(), kFromAbove);
    Require(normal.HasValue(), "四角から平面が出る");
    const Vector3 unit = kachakacha::v2::geometry::Normalized(normal.Value());
    RequireNear(std::abs(unit.z), 1.0, 1.0e-9, "XY 平面の法線は Z");
}

KACHA_V2_TEST(facing_plan, 一直線の線はいまの視線から法線を作る)
{
    // 直線1本には平面が無い。V1 と同じく、いまの視線に直交を落として使う。
    // こうしないと、直線を選んで正対するたびに向きが飛ぶ。
    const std::vector<Vector3> line{{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {20.0, 0.0, 0.0}};
    const auto normal = BestFitNormal(line, kFromAbove);
    Require(normal.HasValue(), "直線でも法線が出る");
    const Vector3 unit = kachakacha::v2::geometry::Normalized(normal.Value());
    RequireNear(Dot(unit, Vector3{1.0, 0.0, 0.0}), 0.0, 1.0e-9, "線と直交する");
    RequireNear(std::abs(unit.z), 1.0, 1.0e-9, "いまの視線にいちばん近い向き");
}

KACHA_V2_TEST(facing_plan, 視線に重なる直線でも決まった向きへ逃がす)
{
    const std::vector<Vector3> line{{0.0, 0.0, 0.0}, {0.0, 0.0, 10.0}};
    const auto first = BestFitNormal(line, kFromAbove);
    const auto second = BestFitNormal(line, kFromAbove);
    Require(first.HasValue() && second.HasValue(), "逃がし方がある");
    RequireNear(Dot(kachakacha::v2::geometry::Normalized(first.Value()),
                    kachakacha::v2::geometry::Normalized(second.Value())),
        1.0, 1.0e-9, "2度呼んでも同じ向き");
}

KACHA_V2_TEST(facing_plan, 点1つは向きを変えない)
{
    const auto normal = BestFitNormal({Vector3{1.0, 2.0, 3.0}}, kFromAbove);
    Require(normal.HasValue(), "点1つでも断らない");
    const Vector3 unit = kachakacha::v2::geometry::Normalized(normal.Value());
    RequireNear(Dot(unit, kFromAbove), -1.0, 1.0e-9, "いまの視線のまま寄るだけ");
}

KACHA_V2_TEST(facing_plan, 点が無ければ平面も推せない)
{
    const auto normal = BestFitNormal({}, kFromAbove);
    Require(!normal.HasValue(), "空は断る");
    RequireEqual(FirstCode(normal.Diagnostics()), std::string(kFacingNoPoints), "UI-V008");
}

KACHA_V2_TEST_MAIN("facing_plan_tests")
