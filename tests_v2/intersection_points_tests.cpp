// 交点に作図点(app/IntersectionPoints.h)。V1 の「交点に点」。
#include "kachakacha/app/IntersectionPoints.h"
#include "kachakacha/base/TestHarness.h"

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

KACHA_V2_TEST_MAIN("intersection_points")
