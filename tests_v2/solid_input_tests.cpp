// 立体を作る(回転体・ロフト立体・スイープ、modeling/SolidInput)の入力検査と予測。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/SolidInput.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::AnalyzeLoftSolid;
using kachakacha::v2::modeling::AnalyzeRevolveSolid;
using kachakacha::v2::modeling::AnalyzeSweepSolid;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::LoftSolidRequest;
using kachakacha::v2::modeling::RevolveSolidRequest;
using kachakacha::v2::modeling::RevolveVolumeMatches;
using kachakacha::v2::modeling::SweepSolidRequest;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

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

//! xy の矩形(z の高さ)。
[[nodiscard]] ExtrudeProfile Rectangle(double x0, double y0, double x1, double y1, double z = 0.0)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {Line({x0, y0, z}, {x1, y0, z}), Line({x1, y0, z}, {x1, y1, z}),
        Line({x1, y1, z}, {x0, y1, z}), Line({x0, y1, z}, {x0, y0, z})};
    return profile;
}

[[nodiscard]] ExtrudeProfile Circle(Vector3 center, double radius)
{
    ExtrudeProfile profile;
    profile.closed = true;
    const auto made = CurveSegment::MakeCircle(center, {0, 0, 1}, {1, 0, 0}, radius);
    Require(made.HasValue(), "円が作れること");
    profile.segments = {made.Value()};
    return profile;
}

[[nodiscard]] std::string FirstCode(const std::vector<kachakacha::v2::base::Diagnostic>& found)
{
    return found.empty() ? std::string("(なし)") : found.front().code;
}

} // namespace

KACHA_V2_TEST(solid_input, 回転体はPappusで体積を予測し角度と穴も数える)
{
    // x 10〜20、y 0〜30 の矩形を y 軸まわりに一周: π(20² − 10²) × 30。
    RevolveSolidRequest request;
    request.profiles = {Rectangle(10, 0, 20, 30)};
    request.axisPoint = {0, 0, 0};
    request.axisDirection = {0, 1, 0};
    const auto full = AnalyzeRevolveSolid(request, Tolerance());
    Require(full.HasValue(), "回せる: " + full.FirstSummaryJa());
    RequireNear(full.Value().predictedVolumeMm3, kPi * (400.0 - 100.0) * 30.0, 1.0e-3, "一周の体積");
    Require(RevolveVolumeMatches(full.Value(), kPi * 300.0 * 30.0), "実物がその体積なら合う");
    Require(!RevolveVolumeMatches(full.Value(), kPi * 300.0 * 30.0 * 1.02), "2% 違えば合わない");
    request.angleRad = kPi / 2.0;
    request.symmetric = true;
    const auto quarter = AnalyzeRevolveSolid(request, Tolerance());
    Require(quarter.HasValue(), "90° も回せる");
    RequireNear(quarter.Value().predictedVolumeMm3, kPi * 300.0 * 30.0 / 4.0, 1.0e-3, "4 分の 1");
    RequireNear(quarter.Value().startAngleRad, -kPi / 4.0, 1.0e-12, "対称は -45° から");
    // 穴: 外周 x 10〜30 × y 0〜20、穴 x 15〜25 × y 5〜15。重心はどちらも x = 20。
    RevolveSolidRequest holed;
    holed.profiles = {Rectangle(10, 0, 30, 20), Rectangle(15, 5, 25, 15)};
    holed.axisDirection = {0, 1, 0};
    const auto withHole = AnalyzeRevolveSolid(holed, Tolerance());
    Require(withHole.HasValue(), "穴のある輪郭も回せる: " + withHole.FirstSummaryJa());
    RequireNear(withHole.Value().predictedVolumeMm3, 2.0 * kPi * (400.0 - 100.0) * 20.0, 1.0e-3,
        "穴のぶんを引く");
}

KACHA_V2_TEST(solid_input, 回転軸が平面に無いか輪郭を横切るか角度が変なら理由を言って断る)
{
    RevolveSolidRequest request;
    request.profiles = {Rectangle(10, 0, 20, 30)};
    request.axisDirection = {0, 1, 0};
    request.axisPoint = {15, 0, 0};   // 輪郭の真ん中を通る
    const auto crossing = AnalyzeRevolveSolid(request, Tolerance());
    Require(!crossing.HasValue() && FirstCode(crossing.Diagnostics()) == "SOL-004",
        "軸が輪郭を横切ると断る: " + crossing.FirstSummaryJa());
    request.axisPoint = {0, 0, 5};   // 平面から浮いている
    const auto floating = AnalyzeRevolveSolid(request, Tolerance());
    Require(!floating.HasValue() && FirstCode(floating.Diagnostics()) == "SOL-003", "浮いた軸は断る");
    request.axisPoint = {0, 0, 0};
    request.axisDirection = {0, 1, 1};   // 平面から傾いている
    Require(FirstCode(AnalyzeRevolveSolid(request, Tolerance()).Diagnostics()) == "SOL-003",
        "傾いた軸は断る");
    request.axisDirection = {0, 1, 0};
    request.angleRad = 0.0;
    Require(FirstCode(AnalyzeRevolveSolid(request, Tolerance()).Diagnostics()) == "SOL-005", "0° は断る");
    request.angleRad = kPi * 2.5;
    Require(FirstCode(AnalyzeRevolveSolid(request, Tolerance()).Diagnostics()) == "SOL-005",
        "360° を超えたら断る");
    // 輪郭の縁が軸に沿う(半分の輪郭を軸で回す)のは断らない。
    request.angleRad = kPi * 2.0;
    request.profiles = {Rectangle(0, 0, 10, 30)};
    const auto touching = AnalyzeRevolveSolid(request, Tolerance());
    Require(touching.HasValue(), "縁が軸に沿う輪郭は回せる: " + touching.FirstSummaryJa());
    RequireNear(touching.Value().predictedVolumeMm3, kPi * 100.0 * 30.0, 1.0e-3, "円柱");
}

KACHA_V2_TEST(solid_input, ロフト立体は閉じた断面2つ以上で両端は平らで重ならない)
{
    LoftSolidRequest request;
    request.sections = {Rectangle(0, 0, 20, 10, 0), Rectangle(2, 2, 18, 8, 30)};
    Require(AnalyzeLoftSolid(request, Tolerance()).HasValue(), "2 つの矩形から作れる");
    LoftSolidRequest one;
    one.sections = {Rectangle(0, 0, 20, 10)};
    Require(FirstCode(AnalyzeLoftSolid(one, Tolerance()).Diagnostics()) == "SOL-001", "1 つは断る");
    LoftSolidRequest open = request;
    open.sections[1].segments.pop_back();
    Require(FirstCode(AnalyzeLoftSolid(open, Tolerance()).Diagnostics()) == "SOL-001",
        "開いた断面は断る");
    LoftSolidRequest same;
    same.sections = {Rectangle(0, 0, 20, 10, 0), Rectangle(0, 0, 20, 10, 0)};
    Require(FirstCode(AnalyzeLoftSolid(same, Tolerance()).Diagnostics()) == "SOL-007",
        "重なった断面は断る");
    // 端の断面が平らでない(1 つの角を持ち上げた四辺形)。
    ExtrudeProfile bent;
    bent.closed = true;
    bent.segments = {Line({0, 0, 0}, {20, 0, 0}), Line({20, 0, 0}, {20, 10, 3}),
        Line({20, 10, 3}, {0, 10, 0}), Line({0, 10, 0}, {0, 0, 0})};
    LoftSolidRequest bentEnd;
    bentEnd.sections = {bent, Rectangle(0, 0, 20, 10, 30)};
    Require(FirstCode(AnalyzeLoftSolid(bentEnd, Tolerance()).Diagnostics()) == "SOL-002",
        "平らでない端の断面は断る");
}

KACHA_V2_TEST(solid_input, スイープは経路を1本につなぎ輪郭の平面から始まる向きにする)
{
    SweepSolidRequest request;
    request.profiles = {Circle({0, 0, 0}, 5)};
    request.path = {Line({0, 0, 0}, {0, 0, 20}), Line({0, 0, 20}, {0, 10, 40})};
    const auto ok = AnalyzeSweepSolid(request, Tolerance());
    Require(ok.HasValue(), "掃ける: " + ok.FirstSummaryJa());
    Require(ok.Value().orderedPath.size() == 2
            && std::abs(ok.Value().orderedPath.front().StartPoint().z) < 1.0e-9,
        "経路は輪郭の平面から始まる");
    // 逆向き・逆順に渡しても、輪郭の平面から始まる向きに直す。
    request.path = {Line({0, 10, 40}, {0, 0, 20}), Line({0, 0, 20}, {0, 0, 0})};
    const auto flipped = AnalyzeSweepSolid(request, Tolerance());
    Require(flipped.HasValue() && std::abs(flipped.Value().orderedPath.front().StartPoint().z) < 1.0e-9,
        "逆向きの経路も直す: " + flipped.FirstSummaryJa());
    // 切れた経路・平面に沿う経路・平面から始まらない経路は断る。
    request.path = {Line({0, 0, 0}, {0, 0, 20}), Line({0, 0, 25}, {0, 0, 40})};
    Require(FirstCode(AnalyzeSweepSolid(request, Tolerance()).Diagnostics()) == "SOL-006",
        "切れた経路は断る");
    request.path = {Line({0, 0, 0}, {50, 0, 0})};
    Require(FirstCode(AnalyzeSweepSolid(request, Tolerance()).Diagnostics()) == "SOL-008",
        "平面に沿う経路は断る");
    request.path = {Line({0, 0, 10}, {0, 0, 50})};
    Require(FirstCode(AnalyzeSweepSolid(request, Tolerance()).Diagnostics()) == "SOL-008",
        "平面から始まらない経路は断る");
}

KACHA_V2_TEST_MAIN("solid_input_tests")
