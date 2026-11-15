// 交点に作図点(app/IntersectionPoints.h)。V1 の「交点に点」。
#include "kachakacha/app/IntersectionPoints.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveIntersection.h"

#include <algorithm>
#include <cmath>
#include <string>

using kachakacha::v2::app::IntersectionPointsOf;
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

KACHA_V2_TEST(intersection_points, 十字の交点が1つ出る)
{
    const auto points = IntersectionPointsOf(
        {Line({-10, 0, 0}, {10, 0, 0}), Line({0, -10, 0}, {0, 10, 0})}, 0.01);
    Require(points.HasValue(), "交点がある");
    RequireEqual(std::to_string(points.Value().size()), std::string("1"), "1つ");
    RequireNear(points.Value().front().x, 0.0, 1e-9, "x");
    RequireNear(points.Value().front().y, 0.0, 1e-9, "y");
}

KACHA_V2_TEST(intersection_points, 3本なら組ごとの交点が出て同じ場所はまとめる)
{
    // 三角形の3辺は端点で互いに触れる。同じ頂点が2度出ない。
    const auto points = IntersectionPointsOf({Line({0, 0, 0}, {10, 0, 0}),
                                                 Line({10, 0, 0}, {5, 8, 0}), Line({5, 8, 0}, {0, 0, 0})},
        0.01);
    Require(points.HasValue(), "交点がある");
    RequireEqual(std::to_string(points.Value().size()), std::string("3"), "頂点3つ");
    // 井桁は4つ。
    const auto grid = IntersectionPointsOf({Line({-10, 1, 0}, {10, 1, 0}),
                                               Line({-10, -1, 0}, {10, -1, 0}), Line({1, -10, 0}, {1, 10, 0}),
                                               Line({-1, -10, 0}, {-1, 10, 0})},
        0.01);
    Require(grid.HasValue(), "井桁");
    RequireEqual(std::to_string(grid.Value().size()), std::string("4"), "4つ");
}

KACHA_V2_TEST(intersection_points, 交わらないときと1本のときは断る)
{
    const auto parallel = IntersectionPointsOf(
        {Line({0, 0, 0}, {10, 0, 0}), Line({0, 5, 0}, {10, 5, 0})}, 0.01);
    Require(!parallel.HasValue(), "平行は断る");
    RequireEqual(parallel.Diagnostics().front().code, std::string("UI-X101"), "理由の番号");
    const auto one = IntersectionPointsOf({Line({0, 0, 0}, {10, 0, 0})}, 0.01);
    Require(!one.HasValue(), "1本は断る");
    RequireEqual(one.Diagnostics().front().code, std::string("UI-X101"), "理由の番号");
}

KACHA_V2_TEST(intersection_points, arc_and_two_wires_find_all_crossings_in_either_order)
{
    const auto arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1},
        {1, 0, 0}, 20, 3.0, -2.9).Value();
    const auto shallow = Line({-200, 19.99, 0}, {400, 19.99, 0});
    const auto vertical = Line({3, -30, 0}, {3, 30, 0});
    const double x = std::sqrt((20.0 - 19.99) * (20.0 + 19.99));
    for (const auto& curves : {std::vector{shallow, vertical, arc}, std::vector{arc, vertical, shallow}}) {
        const auto points = IntersectionPointsOf(curves, 1e-6);
        Require(points.HasValue() && points.Value().size() == 4, "all four distinct intersections");
        for (const Vector3 expected : {Vector3{-x, 19.99, 0}, Vector3{x, 19.99, 0},
                Vector3{3, std::sqrt(391.0), 0}, Vector3{3, 19.99, 0}}) {
            Require(std::any_of(points.Value().begin(), points.Value().end(), [&](const auto& point) {
                return (point - expected).Length() < 1e-8;
            }), "exact intersection in the finite arc and lines");
        }
    }
}

KACHA_V2_TEST(intersection_points, arc_and_cubic_wires_agree_at_model_tolerance)
{
    const auto arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1},
        {1, 0, 0}, 20, 0.1, 2.9).Value();
    const auto bezier = CurveSegment::MakeCubicBezier(
        {{-20, 20.03, 0}, {-8, 19.98, 0}, {8, 19.98, 0}, {20, 20.03, 0}}).Value();
    const auto spline = CurveSegment::MakeCubicBSpline(
        {{-60, 20.03, 0}, {-20, 19.99, 0}, {0, 19.98, 0},
            {20, 19.99, 0}, {60, 20.03, 0}}).Value();
    for (const auto& curve : {bezier, spline}) {
        const auto points = IntersectionPointsOf({curve, arc}, 1e-6);
        Require(points.HasValue() && points.Value().size() == 2, "two near-tangent intersections");
        for (const auto& hit : points.Value()) {
            RequireNear(arc.ClosestPoint(hit).distance, 0, 1e-8, "point on original arc");
            RequireNear(curve.ClosestPoint(hit).distance, 0, 1e-8, "point on original cubic");
        }
        const auto separated = CurveSegment::MakeCircularArc({0, 0, 0.005}, {0, 0, 1},
            {1, 0, 0}, 20, 0.1, 2.9).Value();
        Require(!IntersectionPointsOf({separated, curve}, 1e-6).HasValue(), "ignore apparent intersections");
    }
}

KACHA_V2_TEST(intersection_points, arc_intersections_respect_finite_sweeps_plane_and_view)
{
    using namespace kachakacha::v2::geometry;
    const auto arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1},
        {1, 0, 0}, 10, 0.1, 2.9).Value();
    const auto line = Line({6, 8, -4}, {6, 8, 12});
    const auto real = IntersectCurves(arc, line, GeometryTolerance::Default());
    Require(real.size() == 1, "the line passes through the arc plane");
    RequireNear(Distance(real[0].position, {6, 8, 0}), 0, 1e-9, "exact plane intersection");
    Require(IntersectCurves(arc, Line({6, -8, -4}, {6, -8, 12}), GeometryTolerance::Default()).empty(),
        "the other side is beyond the arc sweep");
    const auto map = MakePerspectiveMapping({0, 0, 100}, {0, 0, 0}, {0, 1, 0}, 1, 1000, 800, 0.1, 1000);
    const auto projected = IntersectCurvesOnScreen(line, arc, map, 12, GeometryTolerance::Default());
    Require(std::any_of(projected.begin(), projected.end(), [](const auto& hit) {
        return hit.real && Distance(hit.position, {6, 8, 0}) < 1e-9;
    }), "perspective retains the actual intersection");
    const auto separate = IntersectCurvesOnScreen(Line({-20, 8, 0.02}, {20, 8, 0.02}),
        arc, MakeOrthographicMapping({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 100, 1000, 1000),
        12, GeometryTolerance::Default());
    Require(separate.size() == 2, "two projected intersections");
    Require(std::none_of(separate.begin(), separate.end(), [](const auto& hit) { return hit.real; }),
        "different depths are never true intersections");
    const auto upright = CurveSegment::MakeCircle({6, 0, 0}, {1, 0, 0}, {0, 1, 0}, 8).Value();
    const auto round = IntersectCurves(upright, arc, GeometryTolerance::Default());
    Require(round.size() == 1, "nonparallel circles respect trimmed sweep");
    RequireNear(Distance(round[0].position, {6, 8, 0}), 0, 1e-9, "two supporting planes");
}

KACHA_V2_TEST_MAIN("intersection_points")
