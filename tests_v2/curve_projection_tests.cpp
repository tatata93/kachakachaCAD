// 線を平面へ落とす(AT-FAB-007)。
//
// 見ているのは「線の種類を勝手に変えないか」である。
// 折れ線で近似すれば何でも通せてしまうが、それは落としたことにならない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveProjection.h"

#include <cmath>
#include <string>

using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::ProjectCurveOntoPlane;
using kachakacha::v2::geometry::ProjectCurvesOntoPlane;
using kachakacha::v2::geometry::ProjectionPlane;
using kachakacha::v2::geometry::ProjectPointOntoPlane;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;

namespace {

const ProjectionPlane kXY{Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 0.0, 1.0}};

[[nodiscard]] bool Near(double a, double b)
{
    return std::abs(a - b) < 1.0e-9;
}

} // namespace

KACHA_V2_TEST(projection, 点はまっすぐ落ちる)
{
    const Vector3 dropped = ProjectPointOntoPlane(Vector3{3.0, 4.0, 7.0}, kXY);
    Require(Near(dropped.x, 3.0) && Near(dropped.y, 4.0) && Near(dropped.z, 0.0),
        "z だけが消える");
}

KACHA_V2_TEST(projection, 直線は直線のまま落ちる)
{
    GeometryTolerance tolerance;
    const auto line = CurveSegment::MakeLine({0.0, 0.0, 5.0}, {10.0, 0.0, 9.0}).Value();
    const auto dropped = ProjectCurveOntoPlane(line, kXY, tolerance);
    Require(dropped.HasValue(), "落ちる");
    Require(dropped.Value().Kind() == CurveKind::Line, "直線のまま");
    Require(Near(dropped.Value().StartPoint().z, 0.0)
            && Near(dropped.Value().EndPoint().z, 0.0),
        "面の上に載る");
    Require(Near(dropped.Value().EndPoint().x, 10.0), "面内の座標は変わらない");
}

KACHA_V2_TEST(projection, 面に垂直な線は点になるので断る)
{
    // 点は線ではない。長さ0の線を返すと、そのあとで何をしても意味が決まらない。
    GeometryTolerance tolerance;
    const auto line = CurveSegment::MakeLine({1.0, 2.0, 0.0}, {1.0, 2.0, 9.0}).Value();
    const auto dropped = ProjectCurveOntoPlane(line, kXY, tolerance);
    Require(!dropped.HasValue(), "断る");
    Require(!dropped.Diagnostics().empty(), "理由が出る");
}

KACHA_V2_TEST(projection, 平行な面へなら円は円のまま落ちる)
{
    GeometryTolerance tolerance;
    const auto circle = CurveSegment::MakeCircle(Vector3{2.0, 3.0, 8.0},
        Vector3{0.0, 0.0, 1.0}, Vector3{1.0, 0.0, 0.0}, 5.0).Value();
    const auto dropped = ProjectCurveOntoPlane(circle, kXY, tolerance);
    Require(dropped.HasValue(), "落ちる");
    Require(dropped.Value().Kind() == CurveKind::Circle, "円のまま");
    Require(Near(dropped.Value().Radius(), 5.0), "半径は変わらない");
    Require(Near(dropped.Value().Center().z, 0.0), "中心が面に載る");
}

KACHA_V2_TEST(projection, 斜めの面へ落とすと楕円になるので断る)
{
    // 折れ線で近似すれば通せてしまうが、それは円を落としたことにならない。
    GeometryTolerance tolerance;
    const auto circle = CurveSegment::MakeCircle(Vector3{0.0, 0.0, 0.0},
        Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}, 5.0).Value();
    const auto dropped = ProjectCurveOntoPlane(circle, kXY, tolerance);
    Require(!dropped.HasValue(), "断る");
    Require(dropped.Diagnostics().front().code == "GEO-E017", "楕円になると言う");
}

KACHA_V2_TEST(projection, ベジエは制御点を落とせば同じ次数のまま)
{
    // 射影は一次変換なので、これは近似ではない。
    GeometryTolerance tolerance;
    const auto bezier = CurveSegment::MakeCubicBezier({Vector3{0.0, 0.0, 1.0},
        Vector3{1.0, 2.0, 4.0}, Vector3{3.0, 2.0, -2.0}, Vector3{4.0, 0.0, 6.0}}).Value();
    const auto dropped = ProjectCurveOntoPlane(bezier, kXY, tolerance);
    Require(dropped.HasValue(), "落ちる");
    Require(dropped.Value().Kind() == CurveKind::CubicBezier, "ベジエのまま");
    Require(dropped.Value().ControlPoints().size() == 4, "制御点の数も同じ");
    for (const Vector3& point : dropped.Value().ControlPoints()) {
        Require(Near(point.z, 0.0), "制御点が面に載る");
    }
    // 曲線の上の点も、落としてから評価したものと一致する。
    const Vector3 直接 = ProjectPointOntoPlane(bezier.Evaluate(0.37), kXY);
    const Vector3 落として = dropped.Value().Evaluate(0.37);
    Require(Near(直接.x, 落として.x) && Near(直接.y, 落として.y), "同じ点になる");
}

KACHA_V2_TEST(projection, 1本でも落とせなければ全部やめる)
{
    // 半分だけ落ちた形を返すと、どこまで落ちたのかが分からなくなる。
    GeometryTolerance tolerance;
    std::vector<CurveSegment> curves;
    curves.push_back(CurveSegment::MakeLine({0.0, 0.0, 3.0}, {5.0, 0.0, 3.0}).Value());
    curves.push_back(CurveSegment::MakeCircle(Vector3{0.0, 0.0, 0.0},
        Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}, 5.0).Value());
    const auto dropped = ProjectCurvesOntoPlane(curves, kXY, tolerance);
    Require(!dropped.HasValue(), "断る");
    curves.pop_back();
    const auto ok = ProjectCurvesOntoPlane(curves, kXY, tolerance);
    Require(ok.HasValue() && ok.Value().size() == 1, "残りは落ちる");
}

KACHA_V2_TEST(projection, 面が決まっていなければ断る)
{
    GeometryTolerance tolerance;
    const ProjectionPlane broken{Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 0.0, 0.0}};
    const auto line = CurveSegment::MakeLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}).Value();
    const auto dropped = ProjectCurveOntoPlane(line, broken, tolerance);
    Require(!dropped.HasValue(), "断る");
    Require(dropped.Diagnostics().front().code == "GEO-E018", "面が決まっていないと言う");
}

KACHA_V2_TEST_MAIN("curve_projection_tests")
