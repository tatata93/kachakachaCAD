// ワイヤーの制御点を掴んで動かす(v1-input-parity.md §1-2 の 12)。
//
// いちばん大事なのは「種類が変わらないこと」である。
// 動かすたびに折れ線へ落ちると、直すほど形が崩れていく。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/ControlPointEdit.h"

#include <cmath>
#include <string>

using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::EditableControlPointsOf;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::WithControlPointMoved;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] CurveSegment Line()
{
    return CurveSegment::MakeLine(Vector3{0, 0, 0}, Vector3{10, 0, 0}).Value();
}

[[nodiscard]] CurveSegment Arc()
{
    return CurveSegment::MakeCircularArc(Vector3{0, 0, 0}, Vector3{0, 0, 1},
        Vector3{1, 0, 0}, 10.0, 0.0, kPi / 2.0).Value();
}

[[nodiscard]] CurveSegment Bezier()
{
    return CurveSegment::MakeCubicBezier({Vector3{0, 0, 0}, Vector3{3, 5, 0},
        Vector3{7, 5, 0}, Vector3{10, 0, 0}}).Value();
}

} // namespace

KACHA_V2_TEST(control_point_edit, 直線は端点2つが掴める)
{
    const auto points = EditableControlPointsOf(Line());
    Require(points.size() == 2, "2つ");
    RequireEqual(std::string(points[0].labelJa), std::string("始点"), "始点と言う");
    RequireEqual(std::string(points[1].labelJa), std::string("終点"), "終点と言う");
    Require(std::abs(points[1].position.x - 10.0) < 1e-9, "終点の位置");
}

KACHA_V2_TEST(control_point_edit, 直線の端点を動かしても直線のまま)
{
    const auto moved = WithControlPointMoved(Line(), 1, Vector3{10, 20, 0});
    Require(moved.HasValue(), "動かせる");
    Require(moved.Value().Kind() == CurveKind::Line, "直線のまま");
    Require(std::abs(moved.Value().EndPoint().y - 20.0) < 1e-9, "終点が動いた");
    Require(moved.Value().StartPoint().Length() < 1e-9, "始点は動かない");
}

KACHA_V2_TEST(control_point_edit, つぶれた直線は断る)
{
    const auto moved = WithControlPointMoved(Line(), 1, Vector3{0, 0, 0});
    Require(!moved.HasValue(), "断る");
}

KACHA_V2_TEST(control_point_edit, 円弧は始点終点中心の3つ)
{
    const auto points = EditableControlPointsOf(Arc());
    Require(points.size() == 3, "3つ");
    RequireEqual(std::string(points[2].labelJa), std::string("中心"), "中心と言う");
}

KACHA_V2_TEST(control_point_edit, 円弧の終点を動かしても円弧のまま)
{
    // 90度の円弧の終点を、中心から見て 180度の向きへ動かす。
    const auto moved = WithControlPointMoved(Arc(), 1, Vector3{-10, 0, 0});
    Require(moved.HasValue(), "動かせる");
    Require(moved.Value().Kind() == CurveKind::CircularArc, "円弧のまま");
    Require(std::abs(moved.Value().Radius() - 10.0) < 1e-9, "半径は始点で決まる");
    Require(std::abs(moved.Value().SweepAngleRad() - kPi) < 1e-9, "180度になる");
}

KACHA_V2_TEST(control_point_edit, 円弧の中心を動かすと円弧ごと動く)
{
    // 中心だけ動かすと半径と角が同時に変わって形が読めない。まるごと動かす。
    const auto moved = WithControlPointMoved(Arc(), 2, Vector3{5, 0, 0});
    Require(moved.HasValue(), "動かせる");
    Require(std::abs(moved.Value().Radius() - 10.0) < 1e-9, "半径は変わらない");
    Require(std::abs(moved.Value().SweepAngleRad() - kPi / 2.0) < 1e-9,
        "掃引角も変わらない");
    Require(std::abs(moved.Value().Center().x - 5.0) < 1e-9, "中心が動いた");
}

KACHA_V2_TEST(control_point_edit, ベジエは制御点4つが掴める)
{
    const auto points = EditableControlPointsOf(Bezier());
    Require(points.size() == 4, "4つ");
    RequireEqual(std::string(points[1].labelJa), std::string("制御点2"), "番号が付く");
}

KACHA_V2_TEST(control_point_edit, ベジエの制御点を動かしてもベジエのまま)
{
    const auto moved = WithControlPointMoved(Bezier(), 1, Vector3{3, 30, 0});
    Require(moved.HasValue(), "動かせる");
    Require(moved.Value().Kind() == CurveKind::CubicBezier, "ベジエのまま");
    Require(moved.Value().ControlPoints().size() == 4, "制御点は4つのまま");
    Require(std::abs(moved.Value().ControlPoints()[1].y - 30.0) < 1e-9, "動いている");
    Require(moved.Value().StartPoint().Length() < 1e-9, "端点は動かない");
}

KACHA_V2_TEST(control_point_edit, 円は中心と半径が掴める)
{
    const auto circle = CurveSegment::MakeCircle(Vector3{0, 0, 0}, Vector3{0, 0, 1},
        Vector3{1, 0, 0}, 10.0).Value();
    const auto points = EditableControlPointsOf(circle);
    Require(points.size() == 2, "2つ");
    const auto bigger = WithControlPointMoved(circle, 1, Vector3{25, 0, 0});
    Require(bigger.HasValue(), "動かせる");
    Require(bigger.Value().Kind() == CurveKind::Circle, "円のまま");
    Require(std::abs(bigger.Value().Radius() - 25.0) < 1e-9, "半径が変わる");
    Require(!WithControlPointMoved(circle, 1, Vector3{0, 0, 0}).HasValue(),
        "半径0は断る");
}

KACHA_V2_TEST(control_point_edit, 無い番号は断る)
{
    Require(!WithControlPointMoved(Line(), 5, Vector3{1, 1, 0}).HasValue(), "断る");
}

KACHA_V2_TEST(control_point_edit, 数でない行き先は断る)
{
    const auto moved = WithControlPointMoved(Line(), 0,
        Vector3{std::nan(""), 0.0, 0.0});
    Require(!moved.HasValue(), "断る");
}

KACHA_V2_TEST_MAIN("control_point_edit_tests")
