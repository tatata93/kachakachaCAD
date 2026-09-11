// 作図面の上のものだけを相手にする(薄くする・掴む)。
#include "kachakacha/app/PlaneFocus.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/WorkPlane.h"

using kachakacha::v2::app::CurveLiesOnPlane;
using kachakacha::v2::app::DimsOffPlaneCurve;
using kachakacha::v2::app::PickableOffPlaneCurve;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::test::Require;

namespace {

//! 直線を1本。作れなければ試験そのものが成り立たないので、そこで止める。
[[nodiscard]] CurveSegment Line(const Vector3& from, const Vector3& to)
{
    const auto made = CurveSegment::MakeLine(from, to);
    Require(made.HasValue(), "試験に使う直線が作れる");
    return made.Value();
}

} // namespace

KACHA_V2_TEST(plane_focus, 面の上の線は上と見る)
{
    const auto plane = StandardPlane(StandardPlaneKind::XY);
    Require(CurveLiesOnPlane(Line({0.0, 0.0, 0.0}, {40.0, 10.0, 0.0}), plane), "XY の上");
}

KACHA_V2_TEST(plane_focus, 浮いている線は上と見ない)
{
    const auto plane = StandardPlane(StandardPlaneKind::XY);
    Require(!CurveLiesOnPlane(Line({0.0, 0.0, 5.0}, {40.0, 10.0, 5.0}), plane), "5mm 浮いている");
}

KACHA_V2_TEST(plane_focus, 片端だけ浮いていても上と見ない)
{
    // 端だけ見て真ん中を見ないと、面をまたぐ線を「上」と読んでしまう。
    const auto plane = StandardPlane(StandardPlaneKind::XY);
    Require(!CurveLiesOnPlane(Line({0.0, 0.0, 0.0}, {40.0, 0.0, 5.0}), plane), "斜めは上でない");
}

KACHA_V2_TEST(plane_focus, 許容の中なら上と見る)
{
    const auto plane = StandardPlane(StandardPlaneKind::XY);
    Require(CurveLiesOnPlane(Line({0.0, 0.0, 1.0e-5}, {40.0, 0.0, 1.0e-5}), plane),
        "1/100000 mm は上");
}

KACHA_V2_TEST(plane_focus, 作図中だけ薄くする)
{
    Require(DimsOffPlaneCurve(true, true, false, false), "作図中で面の外なら薄い");
    Require(!DimsOffPlaneCurve(false, true, false, false), "選択道具なら薄くしない");
    Require(!DimsOffPlaneCurve(true, false, false, false), "印を外せば薄くしない");
    Require(!DimsOffPlaneCurve(true, true, false, true), "面の上なら薄くしない");
}

KACHA_V2_TEST(plane_focus, 選んでいるものは薄くしない)
{
    // 選んだものが消えたように見えてはいけない。
    Require(!DimsOffPlaneCurve(true, true, true, false), "選択中は薄くしない");
}

KACHA_V2_TEST(plane_focus, 薄くしているものは掴まない)
{
    // 別の面の線を誤って掴むのが、面が何枚もあるCADでいちばん困る事故である。
    Require(!PickableOffPlaneCurve(true, true, false), "作図中に面の外は掴まない");
    Require(PickableOffPlaneCurve(true, true, true), "面の上なら掴む");
}

KACHA_V2_TEST(plane_focus, 選択道具なら別の面でも掴める)
{
    // 掴めないと、別の面のものを直せなくなる。
    Require(PickableOffPlaneCurve(false, true, false), "選択道具は全部掴める");
}

KACHA_V2_TEST(plane_focus, 印を外せば作図中でも掴める)
{
    Require(PickableOffPlaneCurve(true, false, false), "印を外せば掴める");
}

KACHA_V2_TEST_MAIN("plane_focus_tests")
