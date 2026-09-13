// 矢印ハンドルを引いたときの距離(オーナー指示 2026-09-14 §6)。
#include "kachakacha/app/ExtrudeDrag.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::app::ExtrudeDistanceAfterDrag;
using kachakacha::v2::app::ExtrudeDistanceFromPointer;
using kachakacha::v2::app::ExtrudeDistanceLabel;
using kachakacha::v2::app::ExtrudeHandle;
using kachakacha::v2::app::ExtrudeHandleTip;
using kachakacha::v2::geometry::MakeOrthographicMapping;
using kachakacha::v2::geometry::ScreenMapping;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

namespace {

//! 正面から見る。押し出しの向き(Z)が画面の縦になる。
[[nodiscard]] ScreenMapping FrontView()
{
    return MakeOrthographicMapping(Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0},
        Vector3{0.0, 0.0, 1.0}, 200.0, 800.0, 600.0);
}

//! 真上から見る。押し出しの向き(Z)が画面と垂直になり、点にしか見えない。
[[nodiscard]] ScreenMapping TopView()
{
    return MakeOrthographicMapping(Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 0.0, -1.0},
        Vector3{0.0, 1.0, 0.0}, 200.0, 800.0, 600.0);
}

[[nodiscard]] ExtrudeHandle UpHandle(double distanceMm = 0.0)
{
    ExtrudeHandle handle;
    handle.origin = Vector3{0.0, 0.0, 0.0};
    handle.direction = Vector3{0.0, 0.0, 1.0};
    handle.distanceMm = distanceMm;
    return handle;
}

} // namespace

KACHA_V2_TEST(extrude_drag, 先端は根元から距離ぶん進んだところ)
{
    const auto tip = ExtrudeHandleTip(UpHandle(12.5));
    RequireNear(tip.z, 12.5, 1.0e-12, "先端");
    RequireNear(tip.x, 0.0, 1.0e-12, "横へは動かない");
}

KACHA_V2_TEST(extrude_drag, 画面の動きが向きに沿ったミリになる)
{
    const ScreenMapping mapping = FrontView();
    const auto handle = UpHandle();
    // 根元の画面位置を求め、そこから 20mm ぶん上へ動かした位置を作る。
    const auto base = mapping.Project(handle.origin);
    Require(base.has_value(), "根元を写せる");
    const auto twenty = mapping.Project(Vector3{0.0, 0.0, 20.0});
    Require(twenty.has_value(), "20mm 先も写せる");
    const auto measured = ExtrudeDistanceFromPointer(handle, mapping,
        ScreenPoint{twenty->x, twenty->y});
    Require(measured.has_value(), "決められる");
    RequireNear(*measured, 20.0, 0.05, "20mm と読む");
}

KACHA_V2_TEST(extrude_drag, 横へずれても向きに沿った分だけ数える)
{
    // 矢印から外れたところでマウスを動かしても、距離は向きに沿った成分だけ。
    // そうしないと、横へ払っただけで距離が変わる。
    const ScreenMapping mapping = FrontView();
    const auto handle = UpHandle();
    const auto twenty = mapping.Project(Vector3{0.0, 0.0, 20.0});
    Require(twenty.has_value(), "写せる");
    const auto measured = ExtrudeDistanceFromPointer(handle, mapping,
        ScreenPoint{twenty->x + 150.0, twenty->y});
    Require(measured.has_value(), "決められる");
    RequireNear(*measured, 20.0, 0.05, "横へずれても 20mm");
}

KACHA_V2_TEST(extrude_drag, 逆へ引けば負になる)
{
    const ScreenMapping mapping = FrontView();
    const auto handle = UpHandle();
    const auto back = mapping.Project(Vector3{0.0, 0.0, -8.0});
    Require(back.has_value(), "写せる");
    const auto measured = ExtrudeDistanceFromPointer(handle, mapping,
        ScreenPoint{back->x, back->y});
    Require(measured.has_value(), "決められる");
    RequireNear(*measured, -8.0, 0.05, "-8mm と読む");
}

KACHA_V2_TEST(extrude_drag, 画面と垂直な向きでは決めない)
{
    // 真上から見ると、上向きの矢印は点にしか見えない。
    // ここで適当な値を返すと、少し動かしただけで距離が跳ねる。
    const ScreenMapping mapping = TopView();
    const auto measured = ExtrudeDistanceFromPointer(UpHandle(), mapping,
        ScreenPoint{400.0, 200.0});
    Require(!measured.has_value(), "決めない");
}

KACHA_V2_TEST(extrude_drag, 掴んだ場所を基準にするので飛ばない)
{
    // 矢印のどこを掴んでも、掴んだ瞬間に距離が変わってはいけない。
    const ScreenMapping mapping = FrontView();
    const auto handle = UpHandle(10.0);
    const auto grabbed = mapping.Project(Vector3{0.0, 0.0, 4.0});
    Require(grabbed.has_value(), "写せる");
    const ScreenPoint pressed{grabbed->x, grabbed->y};
    // 掴んだだけで、まだ動かしていない。
    RequireNear(ExtrudeDistanceAfterDrag(handle, mapping, pressed, pressed), 10.0, 1.0e-9,
        "掴んだだけでは変わらない");
    // そこから 6mm ぶん上へ動かす。
    const auto moved = mapping.Project(Vector3{0.0, 0.0, 10.0});
    Require(moved.has_value(), "写せる");
    RequireNear(
        ExtrudeDistanceAfterDrag(handle, mapping, pressed, ScreenPoint{moved->x, moved->y}),
        16.0, 0.05, "10 + 6 = 16mm");
}

KACHA_V2_TEST(extrude_drag, 決められないときは動かさない)
{
    const ScreenMapping mapping = TopView();
    const auto handle = UpHandle(7.0);
    RequireNear(ExtrudeDistanceAfterDrag(handle, mapping, ScreenPoint{100.0, 100.0},
                    ScreenPoint{400.0, 300.0}),
        7.0, 1.0e-12, "そのまま");
}

KACHA_V2_TEST(extrude_drag, 矢印の横に出す文字)
{
    // 0.01mm まで出すと、引いている間ずっと数字が踊る。
    Require(ExtrudeDistanceLabel(15.0) == std::string("15.0 mm"), "15.0 mm");
    Require(ExtrudeDistanceLabel(2.345) == std::string("2.3 mm"), "2.3 mm");
    Require(ExtrudeDistanceLabel(-4.0) == std::string("-4.0 mm"), "負も出せる");
}

KACHA_V2_TEST_MAIN("extrude_drag_tests")
