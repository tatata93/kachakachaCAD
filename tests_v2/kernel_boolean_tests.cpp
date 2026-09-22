// 部品どうしの足す・引く・交差をカーネルで実際に行う層の試験(kernel/OcctBoolean)。
//
// 約束は 2 つ。何も変わらなかったら成功したことにしない。何も残らなかったら断る。
// 断るときの理由は演算ごとに違う(「触れていない」と一律に言わない)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctExtrude.h"

#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BooleanOperation;
using kachakacha::v2::kernel::BuildBoolean;
using kachakacha::v2::kernel::BuildExtrude;
using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::ExtrudeRequest;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! 箱(xy の矩形を z から高さぶん押し出す)。作れなければ空の番号。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle Box(double x0, double y0, double x1, double y1, double z,
    double height)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {
        Line({x0, y0, z}, {x1, y0, z}),
        Line({x1, y0, z}, {x1, y1, z}),
        Line({x1, y1, z}, {x0, y1, z}),
        Line({x0, y1, z}, {x0, y0, z}),
    };
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = height;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    if (!analysis.HasValue()) {
        return {};
    }
    const auto built = BuildExtrude(request, analysis.Value(), Tolerance(), {});
    if (!built.HasValue() || built.Value().parts.empty()) {
        return {};
    }
    return built.Value().parts.front().handle;
}

[[nodiscard]] std::string FirstCode(const std::vector<kachakacha::v2::base::Diagnostic>& found)
{
    return found.empty() ? std::string("(なし)") : found.front().code;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_boolean_absent, カーネルが無い版は足し引きせずに断る)
{
    const auto built = BuildBoolean(BooleanOperation::Intersection, {}, {}, 0.01);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-B005", "カーネル不在の診断コード");
}

#else

KACHA_V2_TEST(kernel_boolean, 重なる2つの箱を足す引く交差すると体積が合う)
{
    // A: 40×20×30 = 24000、B: x を 20 ずらした同じ箱。重なりは 20×20×30 = 12000。
    const auto a = Box(0, 0, 40, 20, 0, 30);
    const auto b = Box(20, 0, 60, 20, 0, 30);
    Require(a.Valid() && b.Valid(), "箱が作れること");
    const auto sum = BuildBoolean(BooleanOperation::Union, a, b, 0.01);
    Require(sum.HasValue(), "足せる: " + sum.FirstSummaryJa());
    RequireNear(sum.Value().volumeMm3, 36000.0, 1.0e-3, "足すと 36000");
    RequireNear(sum.Value().previousVolumeMm3, 24000.0, 1.0e-3, "前の体積は土台の 24000");
    const auto rest = BuildBoolean(BooleanOperation::Difference, a, b, 0.01);
    Require(rest.HasValue(), "引ける: " + rest.FirstSummaryJa());
    RequireNear(rest.Value().volumeMm3, 12000.0, 1.0e-3, "引くと 12000");
    const auto common = BuildBoolean(BooleanOperation::Intersection, a, b, 0.01);
    Require(common.HasValue(), "交差できる: " + common.FirstSummaryJa());
    RequireNear(common.Value().volumeMm3, 12000.0, 1.0e-3, "交差は重なりの 12000");
    Require(common.Value().handle.Valid(), "出来た形の番号がある");
}

KACHA_V2_TEST(kernel_boolean, 何も変わらないか何も残らないときは演算ごとの理由で断る)
{
    const auto big = Box(0, 0, 40, 40, 0, 40);
    const auto inside = Box(10, 10, 20, 20, 10, 10);
    const auto apart = Box(100, 0, 120, 20, 0, 20);
    const auto around = Box(-10, -10, 50, 50, -10, 60);
    Require(big.Valid() && inside.Valid() && apart.Valid() && around.Valid(), "箱が作れること");
    // 足す: 相手が土台の中なら何も変わらない。
    const auto union0 = BuildBoolean(BooleanOperation::Union, big, inside, 0.01);
    Require(!union0.HasValue() && FirstCode(union0.Diagnostics()) == "KER-B003"
            && union0.FirstSummaryJa().find("収まって") != std::string::npos,
        "足す: 土台の中に収まっていると言う: " + union0.FirstSummaryJa());
    // 引く: 重ならなければ何も変わらない。
    const auto cut0 = BuildBoolean(BooleanOperation::Difference, big, apart, 0.01);
    Require(!cut0.HasValue() && FirstCode(cut0.Diagnostics()) == "KER-B003"
            && cut0.FirstSummaryJa().find("重なっていない") != std::string::npos,
        "引く: 重なっていないと言う: " + cut0.FirstSummaryJa());
    // 交差: 重ならなければ何も残らない。相手が土台を全部含めば土台のまま。
    const auto common0 = BuildBoolean(BooleanOperation::Intersection, big, apart, 0.01);
    Require(!common0.HasValue() && FirstCode(common0.Diagnostics()) == "KER-B004"
            && common0.FirstSummaryJa().find("共通部分がありません") != std::string::npos,
        "交差: 共通部分が無いと言う: " + common0.FirstSummaryJa());
    const auto common1 = BuildBoolean(BooleanOperation::Intersection, big, around, 0.01);
    Require(!common1.HasValue() && FirstCode(common1.Diagnostics()) == "KER-B003",
        "交差: 土台のままなら何も変わらないと言う: " + common1.FirstSummaryJa());
    // 引き切れば何も残らない。
    const auto cut1 = BuildBoolean(BooleanOperation::Difference, big, around, 0.01);
    Require(!cut1.HasValue() && FirstCode(cut1.Diagnostics()) == "KER-B004",
        "引き切れば断る: " + cut1.FirstSummaryJa());
}

#endif

KACHA_V2_TEST_MAIN("kernel_boolean_tests")
