// 立体を作る(回転体・ロフト立体・スイープ、kernel/OcctSolid)をカーネルで実際に行う層の試験。
//
// core が入力を調べ(modeling/SolidInput)、核が形を作る。体積を解析解と突き合わせる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctSolid.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BuildLoftSolid;
using kachakacha::v2::kernel::BuildRevolveSolid;
using kachakacha::v2::kernel::BuildSweepSolid;
using kachakacha::v2::modeling::AnalyzeLoftSolid;
using kachakacha::v2::modeling::AnalyzeRevolveSolid;
using kachakacha::v2::modeling::AnalyzeSweepSolid;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::LoftSolidRequest;
using kachakacha::v2::modeling::RevolveSolidRequest;
using kachakacha::v2::modeling::SweepSolidRequest;
using kachakacha::v2::test::Require;

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

[[nodiscard]] ExtrudeProfile Rectangle(double x0, double y0, double x1, double y1, double z = 0.0)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {Line({x0, y0, z}, {x1, y0, z}), Line({x1, y0, z}, {x1, y1, z}),
        Line({x1, y1, z}, {x0, y1, z}), Line({x0, y1, z}, {x0, y0, z})};
    return profile;
}

[[maybe_unused]] [[nodiscard]] ExtrudeProfile Circle(Vector3 center, double radius)
{
    ExtrudeProfile profile;
    profile.closed = true;
    const auto made = CurveSegment::MakeCircle(center, {0, 0, 1}, {1, 0, 0}, radius);
    Require(made.HasValue(), "円が作れること");
    profile.segments = {made.Value()};
    return profile;
}

[[maybe_unused]] [[nodiscard]] bool Close(double actual, double expected, double ratio)
{
    return std::abs(actual - expected) <= std::abs(expected) * ratio;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_solid_absent, カーネルが無い版は立体を作らずに断る)
{
    RevolveSolidRequest request;
    request.profiles = {Rectangle(10, 0, 20, 30)};
    request.axisDirection = {0, 1, 0};
    const auto analysis = AnalyzeRevolveSolid(request, Tolerance());
    Require(analysis.HasValue(), "入力は通る");
    const auto built = BuildRevolveSolid(request, analysis.Value(), Tolerance());
    Require(!built.HasValue() && built.Diagnostics().front().code == "KER-O003",
        "カーネル不在の診断コード");
}

#else

KACHA_V2_TEST(kernel_solid, 回転体の体積がPappusどおりで角度と対称も効く)
{
    RevolveSolidRequest request;
    request.profiles = {Rectangle(10, 0, 20, 30)};
    request.axisDirection = {0, 1, 0};
    const auto analysis = AnalyzeRevolveSolid(request, Tolerance());
    Require(analysis.HasValue(), "入力が通る: " + analysis.FirstSummaryJa());
    const auto full = BuildRevolveSolid(request, analysis.Value(), Tolerance());
    Require(full.HasValue(), "一周の回転体が作れる: " + full.FirstSummaryJa());
    Require(Close(full.Value().volumeMm3, kPi * 300.0 * 30.0, 1.0e-4),
        "体積は π(20² − 10²) × 30: " + std::to_string(full.Value().volumeMm3));
    request.angleRad = kPi / 2.0;
    request.symmetric = true;
    const auto quarterAnalysis = AnalyzeRevolveSolid(request, Tolerance());
    Require(quarterAnalysis.HasValue(), "90° も通る");
    const auto quarter = BuildRevolveSolid(request, quarterAnalysis.Value(), Tolerance());
    Require(quarter.HasValue() && Close(quarter.Value().volumeMm3, kPi * 300.0 * 30.0 / 4.0, 1.0e-4),
        "対称 90° は 4 分の 1: " + quarter.FirstSummaryJa());
}

KACHA_V2_TEST(kernel_solid, ロフト立体は2つの矩形の間を埋め体積が角錐台の式に合う)
{
    // 20×10(z=0)と 16×6(z=30)。直線でつなぐと中間は 18×8: V = h/6 (A1 + 4Am + A2)。
    LoftSolidRequest request;
    request.sections = {Rectangle(0, 0, 20, 10, 0), Rectangle(2, 2, 18, 8, 30)};
    const auto analysis = AnalyzeLoftSolid(request, Tolerance());
    Require(analysis.HasValue(), "入力が通る: " + analysis.FirstSummaryJa());
    const auto built = BuildLoftSolid(request, analysis.Value(), Tolerance());
    Require(built.HasValue(), "ロフト立体が作れる: " + built.FirstSummaryJa());
    const double expected = 30.0 / 6.0 * (200.0 + 4.0 * 144.0 + 96.0);
    Require(Close(built.Value().volumeMm3, expected, 1.0e-2),
        "体積は角錐台の式: " + std::to_string(built.Value().volumeMm3));
}

KACHA_V2_TEST(kernel_solid, スイープは円を真っすぐの経路で運ぶと円柱になる)
{
    SweepSolidRequest request;
    request.profiles = {Circle({0, 0, 0}, 5)};
    request.path = {Line({0, 0, 0}, {0, 0, 40})};
    const auto analysis = AnalyzeSweepSolid(request, Tolerance());
    Require(analysis.HasValue(), "入力が通る: " + analysis.FirstSummaryJa());
    const auto built = BuildSweepSolid(request, analysis.Value(), Tolerance());
    Require(built.HasValue(), "スイープが作れる: " + built.FirstSummaryJa());
    Require(Close(built.Value().volumeMm3, kPi * 25.0 * 40.0, 1.0e-4),
        "体積は π × 5² × 40: " + std::to_string(built.Value().volumeMm3));
}

#endif

KACHA_V2_TEST_MAIN("kernel_solid_tests")
