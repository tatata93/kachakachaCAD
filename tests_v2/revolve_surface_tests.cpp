// 回転体(app/RevolveSurface.h)。V1 の「回転面」= 回した断面の並びをロフトする。
#include "kachakacha/app/RevolveSurface.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::app::BuildRevolvedSections;
using kachakacha::v2::app::MakeRevolveRequest;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れる");
    return made.Value();
}

} // namespace

KACHA_V2_TEST(revolve, 断面を4分割で一周させると90度ずつの写しができる)
{
    // 断面: x=10 の縦線。軸: z 軸。
    const auto request = MakeRevolveRequest({Line({10, 0, 0}, {10, 0, 20})},
        {Line({0, 0, 0}, {0, 0, 1})}, 360.0, 4);
    Require(request.HasValue(), "要求が作れる");
    const auto sections = BuildRevolvedSections(request.Value(), 0.01);
    Require(sections.HasValue(), "回せる");
    RequireEqual(std::to_string(sections.Value().size()), std::string("4"), "写しは 4 つ");
    // 90 度: (10,0) → (0,10)。180 度: (-10,0)。360 度: 元と重なる。
    RequireNear(sections.Value()[0].segments.front().StartPoint().x, 0.0, 1e-9, "90° x");
    RequireNear(sections.Value()[0].segments.front().StartPoint().y, 10.0, 1e-9, "90° y");
    RequireNear(sections.Value()[1].segments.front().StartPoint().x, -10.0, 1e-9, "180° x");
    RequireNear(sections.Value()[3].segments.front().StartPoint().x, 10.0, 1e-9, "360° x");
    RequireNear(sections.Value()[3].segments.front().StartPoint().y, 0.0, 1e-9, "360° y");
    RequireNear(sections.Value()[3].segments.front().EndPoint().z, 20.0, 1e-9, "z は変わらない");
    RequireNear(sections.Value()[3].angleRad, 2.0 * 3.14159265358979323846, 1e-9, "角度");
}

KACHA_V2_TEST(revolve, 軸の位置は線の始点で向きは線の向き)
{
    // 軸を x=5 に置くと、x=10 の断面は半径 5 で回る。180 度で x=0。
    const auto request = MakeRevolveRequest({Line({10, 0, 0}, {10, 0, 20})},
        {Line({5, 0, -3}, {5, 0, 7})}, 180.0, 2);
    Require(request.HasValue(), "要求が作れる");
    RequireNear(request.Value().axisPoint.x, 5.0, 1e-9, "軸の点");
    RequireNear(request.Value().axisDirection.z, 1.0, 1e-9, "軸の向きは単位");
    const auto sections = BuildRevolvedSections(request.Value(), 0.01);
    Require(sections.HasValue(), "回せる");
    RequireNear(sections.Value().back().segments.front().StartPoint().x, 0.0, 1e-9, "180° で x=0");
}

KACHA_V2_TEST(revolve, 断面なしと軸が直線でないのと範囲外と軸上の断面は断る)
{
    const CurveSegment axis = Line({0, 0, 0}, {0, 0, 1});
    const CurveSegment profile = Line({10, 0, 0}, {10, 0, 20});
    const auto none = MakeRevolveRequest({}, {axis}, 360.0, 8);
    Require(!none.HasValue(), "断面なし");
    RequireEqual(none.Diagnostics().front().code, std::string("REV-E001"), "理由");
    const auto twoAxes = MakeRevolveRequest({profile}, {axis, axis}, 360.0, 8);
    Require(!twoAxes.HasValue(), "軸が 2 本");
    RequireEqual(twoAxes.Diagnostics().front().code, std::string("REV-E002"), "理由");
    const auto arcAxis = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0,
        0.0, 1.0);
    Require(arcAxis.HasValue(), "円弧");
    Require(!MakeRevolveRequest({profile}, {arcAxis.Value()}, 360.0, 8).HasValue(),
        "軸が円弧");
    const auto zero = MakeRevolveRequest({profile}, {axis}, 0.0, 8);
    Require(!zero.HasValue(), "角度 0");
    RequireEqual(zero.Diagnostics().front().code, std::string("REV-E003"), "理由");
    Require(!MakeRevolveRequest({profile}, {axis}, 361.0, 8).HasValue(), "角度 361");
    const auto one = MakeRevolveRequest({profile}, {axis}, 360.0, 1);
    Require(!one.HasValue(), "断面 1 つ");
    RequireEqual(one.Diagnostics().front().code, std::string("REV-E004"), "理由");
    Require(!MakeRevolveRequest({profile}, {axis}, 360.0, 73).HasValue(), "断面 73");
    // 軸の上の断面は回しても面にならない。
    const auto onAxis = MakeRevolveRequest({Line({0, 0, 0}, {0, 0, 20})}, {axis}, 360.0, 8);
    Require(onAxis.HasValue(), "要求は作れる");
    const auto refused = BuildRevolvedSections(onAxis.Value(), 0.01);
    Require(!refused.HasValue(), "軸上は断る");
    RequireEqual(refused.Diagnostics().front().code, std::string("REV-E005"), "理由");
}

KACHA_V2_TEST_MAIN("revolve")
