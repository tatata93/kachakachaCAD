// 移動・複製・鏡映・回転が、置いた点から何を意味するか(v1-input-parity.md §3)。
//
// この4つは道具箱に並んでいたのに、点を集めるだけで何も起きなかった。
// 「置いた点 → 変換の中身」を決めるのはここなので、ここを試験で押さえる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/TransformInput.h"

#include <cmath>
#include <limits>
#include <string>

using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::PlanTransform;
using kachakacha::v2::modeling::ToolIsTransform;
using kachakacha::v2::modeling::TransformKind;
using kachakacha::v2::modeling::TransformPointCount;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] Vector3 UpNormal()
{
    return Vector3{0.0, 0.0, 1.0};
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    return GeometryTolerance::Default();
}

[[nodiscard]] std::string FirstCode(
    const kachakacha::v2::base::Result<kachakacha::v2::modeling::TransformPlan>& result)
{
    return result.Diagnostics().empty() ? std::string() : result.Diagnostics().front().code;
}

} // namespace

KACHA_V2_TEST(transform_input, 変換の道具はこの4つだけ)
{
    Require(ToolIsTransform(DrawingTool::Move), "移動は変換");
    Require(ToolIsTransform(DrawingTool::Copy), "コピーは変換");
    Require(ToolIsTransform(DrawingTool::Mirror), "ミラーは変換");
    Require(ToolIsTransform(DrawingTool::Rotate), "回転は変換");
    Require(!ToolIsTransform(DrawingTool::Line), "直線は変換ではない");
    Require(!ToolIsTransform(DrawingTool::Trim), "トリムは変換ではない");
    Require(TransformPointCount(DrawingTool::Move) == 2, "移動は2点");
    Require(TransformPointCount(DrawingTool::Rotate) == 3, "回転は3点");
    Require(TransformPointCount(DrawingTool::Line) == 0, "変換でなければ0点");
}

KACHA_V2_TEST(transform_input, 移動は2点の差になる)
{
    const auto plan = PlanTransform(DrawingTool::Move,
        {Vector3{10.0, 5.0, 0.0}, Vector3{30.0, 5.0, 0.0}}, UpNormal(), Tolerance());
    Require(plan.HasValue(), "決まる");
    Require(plan.Value().kind == TransformKind::Move, "移動である");
    Require(std::abs(plan.Value().vectorArgument.x - 20.0) < 1e-9, "20mm 動く");
    Require(std::abs(plan.Value().vectorArgument.y) < 1e-9, "横へは動かない");
    Require(!plan.Value().keepsSource, "移動は元を残さない");
    Require(plan.Value().summaryJa.find("20") != std::string::npos, "帯に距離が出る");
}

KACHA_V2_TEST(transform_input, コピーは元を残す)
{
    const auto plan = PlanTransform(DrawingTool::Copy,
        {Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 12.0, 0.0}}, UpNormal(), Tolerance());
    Require(plan.HasValue(), "決まる");
    Require(plan.Value().kind == TransformKind::Copy, "コピーである");
    Require(plan.Value().keepsSource, "コピーは元を残す");
    Require(std::abs(plan.Value().vectorArgument.y - 12.0) < 1e-9, "12mm 先へ置く");
}

KACHA_V2_TEST(transform_input, 同じ場所へは動かせない)
{
    const auto plan = PlanTransform(DrawingTool::Move,
        {Vector3{4.0, 4.0, 0.0}, Vector3{4.0, 4.0, 0.0}}, UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-X002"), "距離が無いと言う");
}

KACHA_V2_TEST(transform_input, 鏡の法線は引いた線と直角になる)
{
    // X軸に沿って引いた線が鏡なら、法線はY方向。作業平面の中に寝ている。
    const auto plan = PlanTransform(DrawingTool::Mirror,
        {Vector3{0.0, 0.0, 0.0}, Vector3{50.0, 0.0, 0.0}}, UpNormal(), Tolerance());
    Require(plan.HasValue(), "決まる");
    Require(plan.Value().kind == TransformKind::Mirror, "ミラーである");
    const Vector3 normal = plan.Value().vectorArgument;
    Require(std::abs(std::abs(normal.y) - 1.0) < 1e-9, "法線はY方向");
    Require(std::abs(normal.z) < 1e-9, "作業平面から立ち上がらない");
    Require(std::abs(normal.Length() - 1.0) < 1e-9, "長さ1にそろえてある");
    Require(plan.Value().keepsSource, "ミラーは元を残す");
    Require(std::abs(plan.Value().pointArgument.x) < 1e-9, "面上の点は1点目");
}

KACHA_V2_TEST(transform_input, つぶれた鏡は断る)
{
    const auto plan = PlanTransform(DrawingTool::Mirror,
        {Vector3{1.0, 1.0, 0.0}, Vector3{1.0, 1.0, 0.0}}, UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-X003"), "鏡が決まらないと言う");
}

KACHA_V2_TEST(transform_input, 作業平面に垂直な鏡の線は断る)
{
    // 作業平面が水平(法線はZ)のとき、Z方向へ引いた線は鏡にならない。
    const auto plan = PlanTransform(DrawingTool::Mirror,
        {Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 0.0, 20.0}}, UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-X003"), "鏡が決まらないと言う");
}

KACHA_V2_TEST(transform_input, 回転は向き付きで測る)
{
    // 中心(0,0)、X方向から Y方向へ。作業平面の法線から見て反時計回りの90度。
    const auto plan = PlanTransform(DrawingTool::Rotate,
        {Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}, Vector3{0.0, 10.0, 0.0}},
        UpNormal(), Tolerance());
    Require(plan.HasValue(), "決まる");
    Require(plan.Value().kind == TransformKind::Rotate, "回転である");
    Require(std::abs(plan.Value().angleRad - kPi / 2.0) < 1e-9, "+90度");
    Require(std::abs(plan.Value().vectorArgument.z - 1.0) < 1e-9, "軸は作業平面の法線");
    Require(!plan.Value().keepsSource, "回転は元を残さない");
}

KACHA_V2_TEST(transform_input, 逆回りは負の角になる)
{
    // acos だけで測ると、どちら回りかが消えて、いつも同じ向きへ回ってしまう。
    const auto plan = PlanTransform(DrawingTool::Rotate,
        {Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 10.0, 0.0}, Vector3{10.0, 0.0, 0.0}},
        UpNormal(), Tolerance());
    Require(plan.HasValue(), "決まる");
    Require(std::abs(plan.Value().angleRad + kPi / 2.0) < 1e-9, "-90度");
}

KACHA_V2_TEST(transform_input, 回さない指定は断る)
{
    const auto plan = PlanTransform(DrawingTool::Rotate,
        {Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}, Vector3{20.0, 0.0, 0.0}},
        UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-X004"), "角度が決まらないと言う");
}

KACHA_V2_TEST(transform_input, 中心と重なった点は断る)
{
    const auto plan = PlanTransform(DrawingTool::Rotate,
        {Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}},
        UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-X004"), "角度が決まらないと言う");
}

KACHA_V2_TEST(transform_input, 点の数が足りなければ断る)
{
    const auto plan = PlanTransform(DrawingTool::Rotate,
        {Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}}, UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-T001"), "点が足りないと言う");
}

KACHA_V2_TEST(transform_input, 変換でない道具は断る)
{
    const auto plan = PlanTransform(DrawingTool::Line,
        {Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}}, UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-T003"), "その道具ではないと言う");
}

KACHA_V2_TEST(transform_input, 作業平面の向きが無ければ断る)
{
    const auto plan = PlanTransform(DrawingTool::Mirror,
        {Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}}, Vector3{0.0, 0.0, 0.0},
        Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-X001"), "平面が無いと言う");
}

KACHA_V2_TEST(transform_input, 数値でない点は断る)
{
    const auto plan = PlanTransform(DrawingTool::Move,
        {Vector3{0.0, 0.0, 0.0},
            Vector3{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}},
        UpNormal(), Tolerance());
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan), std::string("UI-G004"), "数でないと言う");
}

KACHA_V2_TEST_MAIN("transform_input_tests")
