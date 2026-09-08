// 測定(PRD-070〜072)。V1の測定機能と同等であることを確かめる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/Measurement.h"

#include <cmath>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Distance;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::geometry::MeasureCurveCurvatureNormal;
using kachakacha::v2::geometry::MeasureCurveLength;
using kachakacha::v2::geometry::MeasureCurveRadius;
using kachakacha::v2::geometry::MeasureCurveTangent;
using kachakacha::v2::geometry::MeasureCurveToCurve;
using kachakacha::v2::geometry::MeasureDirectionToPlaneAngle;
using kachakacha::v2::geometry::MeasureDirections;
using kachakacha::v2::geometry::MeasureNormalAngle;
using kachakacha::v2::geometry::MeasurePlaneToPlaneAngle;
using kachakacha::v2::geometry::MeasurePointToCurve;
using kachakacha::v2::geometry::MeasureSignedPointToPlaneDistance;
using kachakacha::v2::geometry::MeasureTangentAngle;
using kachakacha::v2::geometry::MeasureThreePointAngle;
using kachakacha::v2::geometry::MeasureTwoPoints;
using kachakacha::v2::geometry::Plane;
using kachakacha::v2::geometry::ToDegrees;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

namespace {
[[nodiscard]] CurveSegment L(Vector3 a, Vector3 b)
{
    return CurveSegment::MakeLine(a, b).Value();
}
} // namespace

// ---- 2点距離(PRD-070) ----

KACHA_V2_TEST(measure, two_points_report_everything_prd070_asks_for)
{
    const auto result = MeasureTwoPoints({0, 0, 0}, {3, 4, 12});
    Require(result.HasValue(), "the measurement succeeds");
    const auto& m = result.Value();
    RequireNear(m.distanceMm, 13.0, 1.0e-12, "3-4-12 gives 13");
    RequireNear(m.deltaXMm, 3.0, 1.0e-12, "dX");
    RequireNear(m.deltaYMm, 4.0, 1.0e-12, "dY");
    RequireNear(m.deltaZMm, 12.0, 1.0e-12, "dZ");
    RequireNear(m.projectedOnXYMm, 5.0, 1.0e-12, "the XY projection drops Z");
    RequireNear(m.projectedOnYZMm, std::sqrt(160.0), 1.0e-12, "the YZ projection drops X");
    RequireNear(m.projectedOnZXMm, std::sqrt(153.0), 1.0e-12, "the ZX projection drops Y");
}

KACHA_V2_TEST(measure, two_points_report_the_angle_to_each_axis)
{
    const auto result = MeasureTwoPoints({0, 0, 0}, {1, 0, 0});
    const auto& m = result.Value();
    RequireNear(ToDegrees(m.angleToXAxis).Value(), 0.0, 1.0e-9, "along X is 0 degrees to X");
    RequireNear(ToDegrees(m.angleToYAxis).Value(), 90.0, 1.0e-9, "and 90 to Y");
    RequireNear(ToDegrees(m.angleToZAxis).Value(), 90.0, 1.0e-9, "and 90 to Z");

    const auto diagonal = MeasureTwoPoints({0, 0, 0}, {1, 1, 0});
    RequireNear(ToDegrees(diagonal.Value().angleToXAxis).Value(), 45.0, 1.0e-9,
        "a 45 degree diagonal reads 45 to X");
}

KACHA_V2_TEST(measure, the_same_point_twice_gives_zero_not_an_error)
{
    const auto result = MeasureTwoPoints({5, 5, 5}, {5, 5, 5});
    Require(result.HasValue(), "measuring a point against itself is allowed");
    RequireNear(result.Value().distanceMm, 0.0, 0.0, "the distance is zero");
}

// ---- 角度 ----

KACHA_V2_TEST(measure, directions_report_directed_and_acute_angles)
{
    const auto right = MeasureDirections({1, 0, 0}, {0, 1, 0});
    RequireNear(ToDegrees(right.Value().directed).Value(), 90.0, 1.0e-9, "a right angle");
    RequireNear(ToDegrees(right.Value().acute).Value(), 90.0, 1.0e-9, "acute is also 90");

    const auto obtuse = MeasureDirections({1, 0, 0}, {-1, 1, 0});
    RequireNear(ToDegrees(obtuse.Value().directed).Value(), 135.0, 1.0e-9,
        "the directed angle keeps the obtuse value");
    RequireNear(ToDegrees(obtuse.Value().acute).Value(), 45.0, 1.0e-9,
        "the acute angle folds it back");
}

KACHA_V2_TEST(measure, a_zero_direction_is_refused)
{
    Require(!MeasureDirections({0, 0, 0}, {1, 0, 0}).HasValue(),
        "a zero length direction cannot be measured");
}

KACHA_V2_TEST(measure, three_point_angle_matches_the_corner)
{
    const auto result = MeasureThreePointAngle({0, 0, 0}, {10, 0, 0}, {0, 10, 0});
    RequireNear(ToDegrees(result.Value().directed).Value(), 90.0, 1.0e-9,
        "a right angle corner");
    const auto flat = MeasureThreePointAngle({0, 0, 0}, {10, 0, 0}, {-10, 0, 0});
    RequireNear(ToDegrees(flat.Value().directed).Value(), 180.0, 1.0e-9,
        "a straight line reads 180");
    Require(!MeasureThreePointAngle({0, 0, 0}, {0, 0, 0}, {1, 0, 0}).HasValue(),
        "a point coincident with the vertex is refused");
}

KACHA_V2_TEST(measure, tangent_angle_between_two_curves)
{
    // 直線と、90度回った直線。
    const auto result = MeasureTangentAngle(L({0, 0, 0}, {10, 0, 0}), 0.5,
        L({0, 0, 0}, {0, 10, 0}), 0.5);
    RequireNear(ToDegrees(result.Value().directed).Value(), 90.0, 1.0e-9,
        "two perpendicular lines meet at 90 degrees");

    // 円の頂点での接線は水平。
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value();
    const auto mixed = MeasureTangentAngle(L({0, 0, 0}, {10, 0, 0}), 0.5, circle, 0.25);
    RequireNear(ToDegrees(mixed.Value().acute).Value(), 0.0, 1.0e-6,
        "at a quarter turn the circle tangent is parallel to the x axis");
}

KACHA_V2_TEST(measure, normal_angle_needs_curvature)
{
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value();
    const auto both = MeasureNormalAngle(circle, 0.0, circle, 0.25);
    Require(both.HasValue(), "two points on a circle have normals");
    RequireNear(ToDegrees(both.Value().directed).Value(), 90.0, 1.0e-6,
        "a quarter turn rotates the normal by 90 degrees");

    const auto straight = MeasureNormalAngle(L({0, 0, 0}, {10, 0, 0}), 0.5, circle, 0.0);
    Require(!straight.HasValue(), "a straight line has no curvature normal");
}

// ---- 距離 ----

KACHA_V2_TEST(measure, point_to_curve_distance)
{
    const auto result = MeasurePointToCurve({5, 4, 0}, L({0, 0, 0}, {10, 0, 0}));
    RequireNear(result.distanceMm, 4.0, 1.0e-6, "the perpendicular distance");
    RequireNear(result.secondPoint.x, 5.0, 1.0e-6, "the foot is directly below");
    RequireNear(result.secondParameter, 0.5, 1.0e-6, "the foot is at the midpoint");
}

KACHA_V2_TEST(measure, curve_to_curve_distance_for_parallel_lines)
{
    const auto result = MeasureCurveToCurve(L({0, 0, 0}, {10, 0, 0}), L({0, 6, 0}, {10, 6, 0}));
    RequireNear(result.distanceMm, 6.0, 1.0e-6, "parallel lines are 6mm apart");
}

KACHA_V2_TEST(measure, curve_to_curve_distance_is_zero_when_they_cross)
{
    const auto result =
        MeasureCurveToCurve(L({-5, 0, 0}, {5, 0, 0}), L({0, -5, 0}, {0, 5, 0}));
    RequireNear(result.distanceMm, 0.0, 1.0e-5, "crossing lines touch");
    RequireNear(Distance(result.firstPoint, Vector3{0, 0, 0}), 0.0, 1.0e-4,
        "they touch at the origin");
}

KACHA_V2_TEST(measure, curve_to_curve_distance_for_a_line_and_a_circle)
{
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value();
    const auto result = MeasureCurveToCurve(L({-20, 12, 0}, {20, 12, 0}), circle);
    RequireNear(result.distanceMm, 7.0, 1.0e-4,
        "a line 12mm from the centre is 7mm from a radius 5 circle");
}

// ---- 曲線そのもの ----

KACHA_V2_TEST(measure, curve_length_radius_tangent_and_curvature)
{
    const auto arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 4.0, 0.0, kPi).Value();
    RequireNear(MeasureCurveLength(arc, 1.0e-9), 4.0 * kPi, 1.0e-9, "half of radius 4");
    Require(MeasureCurveRadius(arc).has_value(), "an arc has a radius");
    RequireNear(*MeasureCurveRadius(arc), 4.0, 1.0e-12, "the radius is 4");
    Require(!MeasureCurveRadius(L({0, 0, 0}, {1, 0, 0})).has_value(),
        "a line has no radius");

    RequireNear(MeasureCurveTangent(arc, 0.0).Length(), 1.0, 1.0e-9,
        "the tangent is a unit vector");
    const auto normal = MeasureCurveCurvatureNormal(arc, 0.0);
    Require(normal.has_value(), "an arc has a curvature normal");
    // 円弧の曲率法線は中心を向く。
    RequireNear(Dot(*normal, Vector3{-1, 0, 0}), 1.0, 1.0e-6,
        "the curvature normal points at the centre");
    Require(!MeasureCurveCurvatureNormal(L({0, 0, 0}, {10, 0, 0}), 0.5).has_value(),
        "a line has no curvature normal");
}

// ---- 平面 ----

KACHA_V2_TEST(measure, direction_to_plane_angle)
{
    const Plane xy{{0, 0, 0}, {0, 0, 1}};
    RequireNear(ToDegrees(MeasureDirectionToPlaneAngle({1, 0, 0}, xy).Value()).Value(), 0.0,
        1.0e-9, "a direction lying in the plane makes 0 degrees");
    RequireNear(ToDegrees(MeasureDirectionToPlaneAngle({0, 0, 1}, xy).Value()).Value(), 90.0,
        1.0e-9, "the normal direction makes 90 degrees");
    RequireNear(ToDegrees(MeasureDirectionToPlaneAngle({1, 0, 1}, xy).Value()).Value(), 45.0,
        1.0e-9, "a 45 degree slope reads 45");
}

KACHA_V2_TEST(measure, plane_to_plane_angle)
{
    const Plane xy{{0, 0, 0}, {0, 0, 1}};
    const Plane yz{{0, 0, 0}, {1, 0, 0}};
    RequireNear(ToDegrees(MeasurePlaneToPlaneAngle(xy, yz).Value()).Value(), 90.0, 1.0e-9,
        "two perpendicular planes");
    RequireNear(ToDegrees(MeasurePlaneToPlaneAngle(xy, xy).Value()).Value(), 0.0, 1.0e-9,
        "the same plane twice");
    // 法線が逆向きでも、面としては同じ向き(鋭角で返す)。
    const Plane flipped{{0, 0, 0}, {0, 0, -1}};
    RequireNear(ToDegrees(MeasurePlaneToPlaneAngle(xy, flipped).Value()).Value(), 0.0,
        1.0e-9, "a flipped normal still reads as the same plane");
}

KACHA_V2_TEST(measure, signed_point_to_plane_distance)
{
    const Plane xy{{0, 0, 0}, {0, 0, 1}};
    RequireNear(MeasureSignedPointToPlaneDistance({1, 2, 5}, xy).Value(), 5.0, 1.0e-12,
        "above the plane is positive");
    RequireNear(MeasureSignedPointToPlaneDistance({1, 2, -3}, xy).Value(), -3.0, 1.0e-12,
        "below the plane is negative");
    RequireNear(MeasureSignedPointToPlaneDistance({9, 9, 0}, xy).Value(), 0.0, 1.0e-12,
        "on the plane is zero");
    Require(!MeasureSignedPointToPlaneDistance({0, 0, 0}, Plane{{0, 0, 0}, {0, 0, 0}})
                 .HasValue(),
        "a plane with no normal is refused");
}

KACHA_V2_TEST_MAIN("measurement_tests")
