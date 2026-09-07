// WP-04 の受入(曲線Segment)。geometry-contract §2。
// 大事なのは「種類を保つ」こと。近似の折れ線へ落として通したことにしない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <cmath>
#include <limits>
#include <vector>

using kachakacha::v2::geometry::Bounds3;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Distance;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::geometry::Normalized;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {
constexpr double kTolerance = 1.0e-9;

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "the fixture line is valid");
    return made.Value();
}

[[nodiscard]] CurveSegment UnitArcXY(double startRad, double sweepRad, double radius = 1.0)
{
    const auto made = CurveSegment::MakeCircularArc({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
        {1.0, 0.0, 0.0}, radius, startRad, sweepRad);
    Require(made.HasValue(), "the fixture arc is valid");
    return made.Value();
}
} // namespace

// ---- 作れない入力は診断つきで断る ----

KACHA_V2_TEST(curve, degenerate_and_non_finite_input_is_refused)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    Require(!CurveSegment::MakeLine({0, 0, 0}, {0, 0, 0}).HasValue(),
        "a zero length line is refused");
    Require(!CurveSegment::MakeLine({nan, 0, 0}, {1, 0, 0}).HasValue(),
        "a NaN endpoint is refused");
    Require(!CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 0.0, 0.0, 1.0)
                 .HasValue(),
        "a zero radius arc is refused");
    Require(!CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 1.0, 0.0, 0.0)
                 .HasValue(),
        "a zero sweep arc is refused");
    Require(!CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 0}, {1, 0, 0}, 1.0, 0.0, 1.0)
                 .HasValue(),
        "an arc with no normal is refused");
    Require(!CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, 1.0, 0.0, 1.0)
                 .HasValue(),
        "a reference direction parallel to the normal is refused");
    Require(!CurveSegment::MakeCubicBezier({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}).HasValue(),
        "a bezier with three control points is refused");
    Require(!CurveSegment::MakeCubicBSpline({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}).HasValue(),
        "a bspline with three control points is refused");
}

KACHA_V2_TEST(curve, refusals_carry_a_diagnostic_code)
{
    const auto refused = CurveSegment::MakeLine({0, 0, 0}, {0, 0, 0});
    Require(!refused.HasValue(), "the refusal has no value");
    Require(refused.Diagnostics().size() == 1, "the refusal carries one diagnostic");
    Require(!refused.Diagnostics().front().code.empty(),
        "the diagnostic has a stable code, not just a message");
    Require(!refused.Diagnostics().front().summaryJa.empty(),
        "the diagnostic has a Japanese summary");
}

// ---- 種類が保たれる ----

KACHA_V2_TEST(curve, each_kind_keeps_its_own_data)
{
    const CurveSegment line = Line({0, 0, 0}, {10, 0, 0});
    Require(line.Kind() == CurveKind::Line, "a line stays a line");
    Require(line.ControlPoints().size() == 2, "a line keeps its two points");

    const CurveSegment arc = UnitArcXY(0.25, 1.5, 7.0);
    Require(arc.Kind() == CurveKind::CircularArc, "an arc stays an arc");
    RequireNear(arc.Radius(), 7.0, 0.0, "the arc keeps its radius");
    RequireNear(arc.StartAngleRad(), 0.25, 0.0, "the arc keeps its start angle");
    RequireNear(arc.SweepAngleRad(), 1.5, 0.0, "the arc keeps its sweep angle");
    RequireNear(arc.Center().x, 0.0, 0.0, "the arc keeps its centre");
    RequireNear(arc.Normal().z, 1.0, kTolerance, "the arc keeps its normal");

    const auto circle = CurveSegment::MakeCircle({1, 2, 3}, {0, 0, 1}, {1, 0, 0}, 5.0);
    Require(circle.HasValue() && circle.Value().Kind() == CurveKind::Circle,
        "a circle is its own kind");
    RequireNear(circle.Value().SweepAngleRad(), 2.0 * kPi, kTolerance,
        "a circle sweeps a full turn");
}

KACHA_V2_TEST(curve, the_reference_direction_is_projected_into_the_plane)
{
    // 法線成分を含む基準方向を渡しても、面内の向きへ直してから保持する。
    const auto made = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 5}, 2.0,
        0.0, kPi);
    Require(made.HasValue(), "a reference direction with a normal component is accepted");
    RequireNear(Dot(made.Value().ReferenceDirection(), Vector3{0, 0, 1}), 0.0, kTolerance,
        "the stored reference direction lies in the plane");
    RequireNear(made.Value().ReferenceDirection().Length(), 1.0, kTolerance,
        "the stored reference direction is a unit vector");
}

// ---- 評価 ----

KACHA_V2_TEST(curve, a_line_evaluates_linearly)
{
    const CurveSegment line = Line({0, 0, 0}, {10, 20, 30});
    RequireNear(line.Evaluate(0.0).x, 0.0, kTolerance, "t=0 is the start");
    RequireNear(line.Evaluate(1.0).z, 30.0, kTolerance, "t=1 is the end");
    RequireNear(line.Evaluate(0.5).y, 10.0, kTolerance, "t=0.5 is the middle");
    RequireNear(line.FirstDerivative(0.3).x, 10.0, kTolerance, "the tangent is constant");
    RequireNear(line.SecondDerivative(0.3).Length(), 0.0, kTolerance,
        "a line has no curvature");
}

KACHA_V2_TEST(curve, an_arc_lies_on_its_circle)
{
    const CurveSegment arc = UnitArcXY(0.0, kPi / 2.0, 3.0);
    for (int index = 0; index <= 20; ++index) {
        const double t = static_cast<double>(index) / 20.0;
        const Vector3 point = arc.Evaluate(t);
        RequireNear(point.Length(), 3.0, 1.0e-12, "every point sits on the radius");
        RequireNear(point.z, 0.0, 1.0e-12, "every point stays in the plane");
    }
    RequireNear(arc.Evaluate(0.0).x, 3.0, kTolerance, "the arc starts on the reference axis");
    RequireNear(arc.Evaluate(1.0).y, 3.0, kTolerance, "a quarter turn ends on the binormal");
}

KACHA_V2_TEST(curve, the_arc_tangent_is_perpendicular_to_the_radius)
{
    const CurveSegment arc = UnitArcXY(0.4, 2.0, 2.5);
    for (int index = 0; index <= 10; ++index) {
        const double t = static_cast<double>(index) / 10.0;
        const Vector3 radial = arc.Evaluate(t) - arc.Center();
        const Vector3 tangent = arc.FirstDerivative(t);
        RequireNear(Dot(Normalized(radial), Normalized(tangent)), 0.0, 1.0e-9,
            "the tangent is perpendicular to the radius");
    }
}

KACHA_V2_TEST(curve, a_bezier_interpolates_its_end_control_points)
{
    const auto made = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}});
    Require(made.HasValue(), "the bezier is valid");
    const CurveSegment bezier = made.Value();
    RequireNear(Distance(bezier.Evaluate(0.0), Vector3{0, 0, 0}), 0.0, kTolerance,
        "t=0 is the first control point");
    RequireNear(Distance(bezier.Evaluate(1.0), Vector3{10, 0, 0}), 0.0, kTolerance,
        "t=1 is the last control point");
    // 端の接線は制御多角形の辺に沿う。
    RequireNear(Dot(Normalized(bezier.FirstDerivative(0.0)), Vector3{0, 1, 0}), 1.0, 1.0e-9,
        "the start tangent follows the first leg");
}

KACHA_V2_TEST(curve, a_bspline_stays_inside_the_control_hull)
{
    const auto made = CurveSegment::MakeCubicBSpline(
        {{0, 0, 0}, {10, 0, 0}, {20, 10, 0}, {30, 10, 0}, {40, 0, 0}});
    Require(made.HasValue(), "the bspline is valid");
    const CurveSegment spline = made.Value();
    Require(spline.Kind() == CurveKind::CubicBSpline, "a bspline stays a bspline");
    Require(spline.ControlPoints().size() == 5, "the control points are kept");
    for (int index = 0; index <= 40; ++index) {
        const Vector3 point = spline.Evaluate(static_cast<double>(index) / 40.0);
        Require(point.x >= -1.0e-9 && point.x <= 40.0 + 1.0e-9,
            "x stays within the control hull");
        Require(point.y >= -1.0e-9 && point.y <= 10.0 + 1.0e-9,
            "y stays within the control hull");
    }
}

KACHA_V2_TEST(curve, out_of_range_parameters_are_refused_not_clamped)
{
    const CurveSegment line = Line({0, 0, 0}, {1, 0, 0});
    Require(!line.EvaluateChecked(-0.1).HasValue(), "t below 0 is refused");
    Require(!line.EvaluateChecked(1.1).HasValue(), "t above 1 is refused");
    Require(!line.EvaluateChecked(std::numeric_limits<double>::quiet_NaN()).HasValue(),
        "a NaN parameter is refused");
    Require(line.EvaluateChecked(0.0).HasValue(), "t=0 is allowed");
    Require(line.EvaluateChecked(1.0).HasValue(), "t=1 is allowed");
}

// ---- 弧長・外接箱 ----

KACHA_V2_TEST(curve, line_and_arc_lengths_are_analytic)
{
    RequireNear(Line({0, 0, 0}, {3, 4, 0}).TotalLength(1.0e-9), 5.0, 1.0e-12,
        "a line length is exact, not sampled");
    RequireNear(UnitArcXY(0.0, 2.0 * kPi, 1.0).TotalLength(1.0e-9), 2.0 * kPi, 1.0e-12,
        "a full circle is exactly 2 pi r");
    RequireNear(UnitArcXY(0.0, kPi / 2.0, 4.0).TotalLength(1.0e-9), 2.0 * kPi, 1.0e-12,
        "a quarter of radius 4 is 2 pi");
    RequireNear(Line({0, 0, 0}, {10, 0, 0}).ArcLength(0.25, 0.75, 1.0e-9), 5.0, 1.0e-12,
        "a partial line length scales with the parameter span");
}

KACHA_V2_TEST(curve, a_sampled_length_converges)
{
    const auto made = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}});
    const CurveSegment bezier = made.Value();
    const double coarse = bezier.TotalLength(1.0e-4);
    const double fine = bezier.TotalLength(1.0e-10);
    Require(fine >= coarse - 1.0e-9, "a finer sampling never shortens the curve");
    RequireNear(fine, coarse, 0.05, "the two samplings agree to within 0.05mm");
}

KACHA_V2_TEST(curve, bounds_contain_every_sampled_point)
{
    const CurveSegment arc = UnitArcXY(0.0, 2.0 * kPi, 5.0);
    const Bounds3 bounds = arc.Bounds(1.0e-9);
    Require(!bounds.empty, "the bounds are populated");
    RequireNear(bounds.minimum.x, -5.0, 1.0e-6, "the circle reaches minus the radius");
    RequireNear(bounds.maximum.y, 5.0, 1.0e-6, "the circle reaches plus the radius");
    for (int index = 0; index <= 100; ++index) {
        const Vector3 point = arc.Evaluate(static_cast<double>(index) / 100.0);
        Require(point.x >= bounds.minimum.x - 1.0e-9
                && point.x <= bounds.maximum.x + 1.0e-9,
            "every sampled point lies inside the bounds");
    }
}

// ---- 分割は種類を保つ ----

KACHA_V2_TEST(curve, splitting_keeps_the_kind)
{
    const auto lineSplit = Line({0, 0, 0}, {10, 0, 0}).Split(0.5);
    Require(lineSplit.HasValue(), "a line splits");
    Require(lineSplit.Value().first->Kind() == CurveKind::Line, "the first half is a line");
    Require(lineSplit.Value().second->Kind() == CurveKind::Line, "the second half is a line");
    RequireNear(lineSplit.Value().first->EndPoint().x, 5.0, kTolerance,
        "the halves meet in the middle");

    const auto arcSplit = UnitArcXY(0.0, kPi, 2.0).Split(0.5);
    Require(arcSplit.HasValue(), "an arc splits");
    Require(arcSplit.Value().first->Kind() == CurveKind::CircularArc,
        "the first half is still an arc, not a polyline");
    RequireNear(arcSplit.Value().first->SweepAngleRad(), kPi / 2.0, kTolerance,
        "the sweep is halved");
    RequireNear(arcSplit.Value().first->Radius(), 2.0, kTolerance,
        "the radius survives the split");
    RequireNear(Distance(arcSplit.Value().first->EndPoint(),
                    arcSplit.Value().second->StartPoint()),
        0.0, kTolerance, "the halves join exactly");

    const auto bezier = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}});
    const auto bezierSplit = bezier.Value().Split(0.3);
    Require(bezierSplit.HasValue(), "a bezier splits");
    Require(bezierSplit.Value().first->Kind() == CurveKind::CubicBezier,
        "the first half is still a cubic bezier");
    // 分割しても形が変わらないこと。
    for (int index = 0; index <= 10; ++index) {
        const double t = static_cast<double>(index) / 10.0;
        const Vector3 whole = bezier.Value().Evaluate(0.3 * t);
        const Vector3 part = bezierSplit.Value().first->Evaluate(t);
        RequireNear(Distance(whole, part), 0.0, 1.0e-9,
            "the split half traces the same shape");
    }
}

KACHA_V2_TEST(curve, splitting_at_an_endpoint_is_refused)
{
    Require(!Line({0, 0, 0}, {1, 0, 0}).Split(0.0).HasValue(), "t=0 cannot split");
    Require(!Line({0, 0, 0}, {1, 0, 0}).Split(1.0).HasValue(), "t=1 cannot split");
}

KACHA_V2_TEST(curve, an_unsupported_split_is_refused_not_approximated)
{
    // B-splineの分割はまだ無い。折れ線へ落として「できた」ことにしてはならない。
    const auto spline = CurveSegment::MakeCubicBSpline(
        {{0, 0, 0}, {10, 0, 0}, {20, 10, 0}, {30, 10, 0}});
    const auto split = spline.Value().Split(0.5);
    Require(!split.HasValue(), "the unsupported split is refused");
    RequireEqual(split.Diagnostics().front().code, "GEO-C014",
        "the refusal names the missing capability instead of degrading the curve");
}

// ---- 最近点 ----

KACHA_V2_TEST(curve, closest_point_on_a_line_is_the_perpendicular_foot)
{
    const CurveSegment line = Line({0, 0, 0}, {10, 0, 0});
    const auto result = line.ClosestPoint({3.0, 4.0, 0.0});
    RequireNear(result.parameter, 0.3, 1.0e-6, "the foot is at t=0.3");
    RequireNear(result.distance, 4.0, 1.0e-6, "the distance is the perpendicular offset");
    RequireNear(result.point.x, 3.0, 1.0e-6, "the foot sits under the query point");
}

KACHA_V2_TEST(curve, closest_point_on_a_circle_is_the_radial_projection)
{
    const CurveSegment circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value();
    const auto result = circle.ClosestPoint({20.0, 0.0, 0.0});
    RequireNear(result.distance, 15.0, 1.0e-5,
        "the distance is the query distance minus the radius");
    RequireNear(result.point.Length(), 5.0, 1.0e-6, "the foot sits on the circle");
}

KACHA_V2_TEST(curve, a_point_on_the_curve_reports_zero_distance)
{
    const auto bezier = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}}).Value();
    const Vector3 onCurve = bezier.Evaluate(0.62);
    const auto result = bezier.ClosestPoint(onCurve);
    RequireNear(result.distance, 0.0, 1.0e-6, "a point on the curve is at distance zero");
    RequireNear(result.parameter, 0.62, 1.0e-4, "the parameter is recovered");
}

// ---- 閉じているか ----

KACHA_V2_TEST(curve, closedness_is_reported)
{
    Require(CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 2.0)
                .Value()
                .IsClosed(1.0e-9),
        "a circle is closed");
    Require(!Line({0, 0, 0}, {1, 0, 0}).IsClosed(1.0e-9), "a line is not closed");
    Require(!UnitArcXY(0.0, kPi, 1.0).IsClosed(1.0e-9), "a half arc is not closed");
    Require(UnitArcXY(0.0, 2.0 * kPi, 1.0).IsClosed(1.0e-9),
        "a full turn arc closes on itself");
}

KACHA_V2_TEST_MAIN("curve_tests")
