// 交点に作図点(app/IntersectionPoints.h)。V1 の「交点に点」。
#include "kachakacha/app/IntersectionPoints.h"
#include "kachakacha/base/TestHarness.h"

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

KACHA_V2_TEST_MAIN("intersection_points")
