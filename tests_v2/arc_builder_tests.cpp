// PRD-052 円弧の作り方。どの作り方でも、円弧の持ち物(中心・半径・角度)が正しく決まること。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/ArcBuilders.h"

#include <cmath>

using kachakacha::v2::geometry::ArcFromEndpointsAndRadius;
using kachakacha::v2::geometry::ArcFromStartTangentRadiusLength;
using kachakacha::v2::geometry::ArcFromStartTangentRadiusSweep;
using kachakacha::v2::geometry::ArcThroughThreePoints;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::Distance;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::geometry::Normalized;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

KACHA_V2_TEST(arc, three_points_on_the_unit_circle)
{
    // (1,0) (0,1) (-1,0) を通る半円。
    const auto made = ArcThroughThreePoints({1, 0, 0}, {0, 1, 0}, {-1, 0, 0});
    Require(made.HasValue(), "the arc is built");
    const auto& arc = made.Value();
    Require(arc.Kind() == CurveKind::CircularArc, "the result is an arc, not a polyline");
    RequireNear(arc.Radius(), 1.0, 1.0e-9, "the radius is 1");
    RequireNear(arc.Center().Length(), 0.0, 1.0e-9, "the centre is the origin");
    RequireNear(std::abs(arc.SweepAngleRad()), kPi, 1.0e-9, "it sweeps half a turn");
    RequireNear(Distance(arc.StartPoint(), Vector3{1, 0, 0}), 0.0, 1.0e-9,
        "it starts at the first point");
    RequireNear(Distance(arc.EndPoint(), Vector3{-1, 0, 0}), 0.0, 1.0e-9,
        "it ends at the third point");
}

KACHA_V2_TEST(arc, three_points_passes_through_the_middle_point)
{
    const auto made = ArcThroughThreePoints({0, 0, 0}, {5, 3, 0}, {10, 0, 0});
    Require(made.HasValue(), "the arc is built");
    const auto& arc = made.Value();
    // 中間点が円弧上にあること。
    const auto closest = arc.ClosestPoint({5, 3, 0});
    RequireNear(closest.distance, 0.0, 1.0e-6, "the middle point lies on the arc");
    // 3点すべてが同じ半径の位置にあること。
    for (const Vector3 point : {Vector3{0, 0, 0}, Vector3{5, 3, 0}, Vector3{10, 0, 0}}) {
        RequireNear(Distance(point, arc.Center()), arc.Radius(), 1.0e-9,
            "every input point sits on the circle");
    }
}

KACHA_V2_TEST(arc, three_points_in_a_tilted_plane)
{
    // 座標平面に乗っていない3点でも作れること。
    const auto made = ArcThroughThreePoints({0, 0, 0}, {3, 4, 5}, {10, 2, 8});
    Require(made.HasValue(), "a tilted arc is built");
    const auto& arc = made.Value();
    for (const Vector3 point : {Vector3{0, 0, 0}, Vector3{3, 4, 5}, Vector3{10, 2, 8}}) {
        RequireNear(Distance(point, arc.Center()), arc.Radius(), 1.0e-8,
            "every input point sits on the circle");
        RequireNear(Dot(point - arc.Center(), arc.Normal()), 0.0, 1.0e-8,
            "every input point lies in the arc plane");
    }
}

KACHA_V2_TEST(arc, collinear_points_are_refused)
{
    const auto made = ArcThroughThreePoints({0, 0, 0}, {5, 0, 0}, {10, 0, 0});
    Require(!made.HasValue(), "three points on a line cannot make an arc");
    RequireEqual(made.Diagnostics().front().code, "GEO-C020",
        "the refusal names the collinear case");
}

KACHA_V2_TEST(arc, endpoints_and_radius_small_arc)
{
    const auto made = ArcFromEndpointsAndRadius({0, 0, 0}, {10, 0, 0}, 10.0, {0, 0, 1},
        false, false);
    Require(made.HasValue(), "the arc is built");
    const auto& arc = made.Value();
    RequireNear(arc.Radius(), 10.0, 1.0e-9, "the radius is kept");
    RequireNear(Distance(arc.StartPoint(), Vector3{0, 0, 0}), 0.0, 1.0e-9,
        "it starts where asked");
    RequireNear(Distance(arc.EndPoint(), Vector3{10, 0, 0}), 0.0, 1.0e-8,
        "it ends where asked");
    Require(std::abs(arc.SweepAngleRad()) <= kPi + 1.0e-9, "the small arc is chosen");
}

KACHA_V2_TEST(arc, endpoints_and_radius_large_arc)
{
    const auto made = ArcFromEndpointsAndRadius({0, 0, 0}, {10, 0, 0}, 10.0, {0, 0, 1},
        true, false);
    Require(made.HasValue(), "the large arc is built");
    Require(std::abs(made.Value().SweepAngleRad()) >= kPi - 1.0e-9,
        "the large arc really is the long way round");
    RequireNear(Distance(made.Value().EndPoint(), Vector3{10, 0, 0}), 0.0, 1.0e-8,
        "it still ends where asked");
}

KACHA_V2_TEST(arc, a_radius_smaller_than_half_the_chord_is_refused)
{
    const auto made = ArcFromEndpointsAndRadius({0, 0, 0}, {10, 0, 0}, 4.0, {0, 0, 1},
        false, false);
    Require(!made.HasValue(), "a radius that cannot reach both points is refused");
    RequireEqual(made.Diagnostics().front().code, "GEO-C021",
        "the refusal names the radius");
    Require(made.Diagnostics().front().detailsJa.find("5") != std::string::npos,
        "the message says how large the radius must be");
}

KACHA_V2_TEST(arc, exactly_half_the_chord_gives_a_half_turn)
{
    const auto made = ArcFromEndpointsAndRadius({0, 0, 0}, {10, 0, 0}, 5.0, {0, 0, 1},
        false, false);
    Require(made.HasValue(), "a radius of exactly half the chord works");
    RequireNear(made.Value().Radius(), 5.0, 1.0e-9, "the radius is kept");
    RequireNear(Distance(made.Value().Center(), Vector3{5, 0, 0}), 0.0, 1.0e-8,
        "the centre sits at the chord midpoint");
}

KACHA_V2_TEST(arc, start_tangent_radius_and_sweep)
{
    // 原点から +x 方向へ出て、半径5で反時計回りに90度。
    const auto made = ArcFromStartTangentRadiusSweep({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 5.0,
        kPi / 2.0);
    Require(made.HasValue(), "the arc is built");
    const auto& arc = made.Value();
    RequireNear(Distance(arc.StartPoint(), Vector3{0, 0, 0}), 0.0, 1.0e-9,
        "it starts at the given point");
    RequireNear(arc.Radius(), 5.0, 1.0e-9, "the radius is kept");
    RequireNear(arc.SweepAngleRad(), kPi / 2.0, 1.0e-12, "the sweep is kept");
    // 始点での接線が指定どおりであること。
    RequireNear(Dot(Normalized(arc.FirstDerivative(0.0)), Vector3{1, 0, 0}), 1.0, 1.0e-9,
        "the arc leaves along the requested tangent");
    RequireNear(Distance(arc.EndPoint(), Vector3{5, 5, 0}), 0.0, 1.0e-8,
        "a quarter of radius 5 lands at (5,5)");
}

KACHA_V2_TEST(arc, a_negative_sweep_curves_the_other_way)
{
    const auto made = ArcFromStartTangentRadiusSweep({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 5.0,
        -kPi / 2.0);
    Require(made.HasValue(), "the arc is built");
    RequireNear(Distance(made.Value().EndPoint(), Vector3{5, -5, 0}), 0.0, 1.0e-8,
        "a negative sweep curves to the other side");
    RequireNear(Dot(Normalized(made.Value().FirstDerivative(0.0)), Vector3{1, 0, 0}), 1.0,
        1.0e-9, "the start tangent is still as asked");
}

KACHA_V2_TEST(arc, start_tangent_radius_and_arc_length)
{
    // 半径4、円弧長 2pi なら中心角は pi/2。
    const auto made = ArcFromStartTangentRadiusLength({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 4.0,
        2.0 * kPi);
    Require(made.HasValue(), "the arc is built");
    RequireNear(made.Value().SweepAngleRad(), kPi / 2.0, 1.0e-12,
        "arc length divided by radius is the sweep");
    RequireNear(made.Value().TotalLength(1.0e-9), 2.0 * kPi, 1.0e-9,
        "the finished arc really has the requested length");
}

KACHA_V2_TEST(arc, degenerate_inputs_are_refused)
{
    Require(!ArcFromStartTangentRadiusSweep({0, 0, 0}, {0, 0, 0}, {0, 0, 1}, 5.0, 1.0)
                 .HasValue(),
        "a zero tangent is refused");
    Require(!ArcFromStartTangentRadiusSweep({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, 5.0, 1.0)
                 .HasValue(),
        "a tangent parallel to the plane normal is refused");
    Require(!ArcFromStartTangentRadiusSweep({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, -5.0, 1.0)
                 .HasValue(),
        "a negative radius is refused");
    Require(!ArcFromStartTangentRadiusLength({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 0.0, 10.0)
                 .HasValue(),
        "a zero radius is refused");
    Require(!ArcFromEndpointsAndRadius({0, 0, 0}, {0, 0, 0}, 5.0, {0, 0, 1}, false, false)
                 .HasValue(),
        "coincident endpoints are refused");
}

KACHA_V2_TEST_MAIN("arc_builder_tests")
