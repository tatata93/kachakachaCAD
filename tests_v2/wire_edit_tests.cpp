// V1同等性(docs/v2/v1-drawing-parity.md)の編集アクションと、geometry-contract §5。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/WireEdit.h"

#include <cmath>

using kachakacha::v2::geometry::ChamferLines;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Distance;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::ExtendCurve;
using kachakacha::v2::geometry::ExtendCurveToBoundary;
using kachakacha::v2::geometry::FilletLines;
using kachakacha::v2::geometry::IntersectCurvesForEditing;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::geometry::MeetLines;
using kachakacha::v2::geometry::MirrorCurve;
using kachakacha::v2::geometry::Normalized;
using kachakacha::v2::geometry::OffsetCurveInPlane;
using kachakacha::v2::geometry::RotateCurve;
using kachakacha::v2::geometry::TranslateCurve;
using kachakacha::v2::geometry::TrimCurve;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {
[[nodiscard]] CurveSegment L(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "the fixture line is valid");
    return made.Value();
}
} // namespace

// ---- 交差 ----

KACHA_V2_TEST(edit, two_crossing_lines_intersect_once)
{
    const auto hits = IntersectCurvesForEditing(L({-10, 0, 0}, {10, 0, 0}), L({0, -10, 0}, {0, 10, 0}),
        1.0e-6);
    Require(hits.size() == 1, "an X crossing has one intersection");
    RequireNear(Distance(hits[0].point, Vector3{0, 0, 0}), 0.0, 1.0e-9,
        "the intersection is at the origin");
    RequireNear(hits[0].parameterA, 0.5, 1.0e-9, "the parameter on the first line is 0.5");
}

KACHA_V2_TEST(edit, parallel_and_skew_lines_do_not_intersect)
{
    Require(IntersectCurvesForEditing(L({0, 0, 0}, {10, 0, 0}), L({0, 5, 0}, {10, 5, 0}), 1.0e-6)
                .empty(),
        "parallel lines do not meet");
    Require(IntersectCurvesForEditing(L({0, 0, 0}, {10, 0, 0}), L({5, -5, 3}, {5, 5, 3}), 1.0e-6)
                .empty(),
        "skew lines do not meet in 3D");
    Require(IntersectCurvesForEditing(L({0, 0, 0}, {4, 0, 0}), L({8, -5, 0}, {8, 5, 0}), 1.0e-6)
                .empty(),
        "lines that would meet only if extended do not count");
}

// ---- 延長 ----

KACHA_V2_TEST(edit, a_line_extends_along_itself)
{
    const auto extended = ExtendCurve(L({0, 0, 0}, {10, 0, 0}), 1, 5.0);
    Require(extended.HasValue(), "the line extends");
    Require(extended.Value().Kind() == CurveKind::Line, "it is still a line");
    RequireNear(extended.Value().EndPoint().x, 15.0, 1.0e-9, "the end moved out by 5");
    RequireNear(extended.Value().StartPoint().x, 0.0, 1.0e-9, "the start did not move");

    const auto back = ExtendCurve(L({0, 0, 0}, {10, 0, 0}), 0, 5.0);
    RequireNear(back.Value().StartPoint().x, -5.0, 1.0e-9, "the start end extends backwards");
}

KACHA_V2_TEST(edit, an_arc_extends_by_sweep_not_by_polyline)
{
    const auto arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0, kPi / 2.0)
            .Value();
    const auto extended = ExtendCurve(arc, 1, 10.0 * kPi / 2.0);
    Require(extended.HasValue(), "the arc extends");
    Require(extended.Value().Kind() == CurveKind::CircularArc,
        "it stays an arc, not a polyline");
    RequireNear(extended.Value().Radius(), 10.0, 1.0e-9, "the radius is unchanged");
    RequireNear(Distance(extended.Value().Center(), Vector3{0, 0, 0}), 0.0, 1.0e-9,
        "the centre is unchanged");
    RequireNear(extended.Value().SweepAngleRad(), kPi, 1.0e-9, "the sweep grew to half a turn");
}

KACHA_V2_TEST(edit, a_circle_cannot_be_extended)
{
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value();
    const auto extended = ExtendCurve(circle, 1, 1.0);
    Require(!extended.HasValue(), "a closed circle refuses to extend");
    RequireEqual(extended.Diagnostics().front().code, "GEO-E002",
        "the refusal names the closed shape");
}

KACHA_V2_TEST(edit, a_line_extends_to_meet_a_boundary)
{
    // 0..4 の線を、x=8 の縦線まで延ばす。
    const auto extended = ExtendCurveToBoundary(L({0, 0, 0}, {4, 0, 0}), 1,
        L({8, -5, 0}, {8, 5, 0}), 1.0e-4);
    Require(extended.HasValue(), "the line reaches the boundary");
    RequireNear(extended.Value().EndPoint().x, 8.0, 1.0e-3,
        "it stops at the first real intersection");
}

KACHA_V2_TEST(edit, extending_to_an_unreachable_boundary_is_refused)
{
    const auto extended = ExtendCurveToBoundary(L({0, 0, 0}, {4, 0, 0}), 1,
        L({0, 10, 0}, {4, 10, 0}), 1.0e-4);
    Require(!extended.HasValue(), "a parallel boundary is never reached");
    RequireEqual(extended.Diagnostics().front().code, "GEO-E001",
        "the refusal says there is no intersection");
}

// ---- トリム ----

KACHA_V2_TEST(edit, trimming_removes_the_clicked_side)
{
    // 0..10 の線を x=6 の縦線で切り、右側(クリック位置0.9)を捨てる。
    const auto trimmed = TrimCurve(L({0, 0, 0}, {10, 0, 0}), L({6, -5, 0}, {6, 5, 0}), 0.9,
        1.0e-6);
    Require(trimmed.HasValue(), "the trim succeeds");
    Require(trimmed.Value().Kind() == CurveKind::Line, "the remainder is still a line");
    RequireNear(trimmed.Value().StartPoint().x, 0.0, 1.0e-6, "the left end is kept");
    RequireNear(trimmed.Value().EndPoint().x, 6.0, 1.0e-6, "it now ends at the boundary");
}

KACHA_V2_TEST(edit, trimming_the_other_side_keeps_the_other_half)
{
    const auto trimmed = TrimCurve(L({0, 0, 0}, {10, 0, 0}), L({6, -5, 0}, {6, 5, 0}), 0.1,
        1.0e-6);
    Require(trimmed.HasValue(), "the trim succeeds");
    RequireNear(trimmed.Value().StartPoint().x, 6.0, 1.0e-6, "it now starts at the boundary");
    RequireNear(trimmed.Value().EndPoint().x, 10.0, 1.0e-6, "the right end is kept");
}

KACHA_V2_TEST(edit, trimming_without_an_intersection_is_refused)
{
    const auto trimmed = TrimCurve(L({0, 0, 0}, {10, 0, 0}), L({0, 5, 0}, {10, 5, 0}), 0.5,
        1.0e-6);
    Require(!trimmed.HasValue(), "a boundary that does not cross cannot trim");
    RequireEqual(trimmed.Diagnostics().front().code, "GEO-E001",
        "the refusal says there is no intersection");
}

// ---- 面取り・丸め ----

KACHA_V2_TEST(edit, a_chamfer_cuts_a_right_angle)
{
    // (0,10)-(0,0) と (0,0)-(10,0) の直角。切戻し 2mm。
    const auto result = ChamferLines(L({0, 10, 0}, {0, 0, 0}), L({0, 0, 0}, {10, 0, 0}), 2.0,
        1.0e-6);
    Require(result.HasValue(), "the chamfer is made");
    Require(result.Value().corner.Kind() == CurveKind::Line, "the chamfer is a straight line");
    RequireNear(result.Value().corner.TotalLength(1.0e-9), 2.0 * std::sqrt(2.0), 1.0e-9,
        "a 2mm setback on a right angle gives a 2*sqrt(2) chamfer");
    RequireNear(result.Value().first.TotalLength(1.0e-9), 8.0, 1.0e-9,
        "the first line is shortened by the setback");
    RequireNear(result.Value().second.TotalLength(1.0e-9), 8.0, 1.0e-9,
        "the second line is shortened by the setback");
}

KACHA_V2_TEST(edit, a_chamfer_joins_end_to_end)
{
    const auto result = ChamferLines(L({0, 10, 0}, {0, 0, 0}), L({0, 0, 0}, {10, 0, 0}), 3.0,
        1.0e-6);
    Require(result.HasValue(), "the chamfer is made");
    RequireNear(Distance(result.Value().first.EndPoint(),
                    result.Value().corner.StartPoint()),
        0.0, 1.0e-9, "the first line meets the chamfer");
    RequireNear(Distance(result.Value().corner.EndPoint(),
                    result.Value().second.StartPoint()),
        0.0, 1.0e-9, "the chamfer meets the second line");
}

KACHA_V2_TEST(edit, chamfering_lines_that_only_meet_when_extended_still_works)
{
    // V1の挙動: 離れている線は交点まで自動で延ばしてから落とす。
    const auto result = ChamferLines(L({0, 10, 0}, {0, 4, 0}), L({4, 0, 0}, {10, 0, 0}), 2.0,
        1.0e-6);
    Require(result.HasValue(), "lines that do not touch are still chamfered");
    RequireNear(result.Value().corner.TotalLength(1.0e-9), 2.0 * std::sqrt(2.0), 1.0e-9,
        "the chamfer size is the same as for touching lines");
}

KACHA_V2_TEST(edit, a_fillet_rounds_a_right_angle)
{
    const auto result = FilletLines(L({0, 10, 0}, {0, 0, 0}), L({0, 0, 0}, {10, 0, 0}), 3.0,
        1.0e-6);
    Require(result.HasValue(), "the fillet is made");
    Require(result.Value().corner.Kind() == CurveKind::CircularArc,
        "the rounded corner is an arc, not a polyline");
    RequireNear(result.Value().corner.Radius(), 3.0, 1.0e-9, "the radius is as asked");
    // 直角なら接点までの距離は半径と同じ。
    RequireNear(result.Value().first.TotalLength(1.0e-9), 7.0, 1.0e-9,
        "the first line is shortened by the radius");
    RequireNear(std::abs(result.Value().corner.SweepAngleRad()), kPi / 2.0, 1.0e-9,
        "a right angle is rounded by a quarter turn");
    RequireNear(Distance(result.Value().first.EndPoint(),
                    result.Value().corner.StartPoint()),
        0.0, 1.0e-9, "the line meets the arc exactly");
    RequireNear(Distance(result.Value().corner.EndPoint(),
                    result.Value().second.StartPoint()),
        0.0, 1.0e-9, "the arc meets the second line exactly");
}

KACHA_V2_TEST(edit, a_fillet_is_tangent_to_both_lines)
{
    const auto result = FilletLines(L({0, 10, 0}, {0, 0, 0}), L({0, 0, 0}, {10, 0, 0}), 2.5,
        1.0e-6);
    Require(result.HasValue(), "the fillet is made");
    const Vector3 lineTangent =
        Normalized(result.Value().first.EndPoint() - result.Value().first.StartPoint());
    const Vector3 arcTangent = Normalized(result.Value().corner.FirstDerivative(0.0));
    RequireNear(std::abs(Dot(lineTangent, arcTangent)), 1.0, 1.0e-8,
        "the arc leaves along the line direction (G1)");
}

KACHA_V2_TEST(edit, an_oversized_fillet_is_refused_with_the_needed_length)
{
    const auto result = FilletLines(L({0, 3, 0}, {0, 0, 0}), L({0, 0, 0}, {3, 0, 0}), 10.0,
        1.0e-6);
    Require(!result.HasValue(), "a radius that does not fit is refused");
    Require(result.Diagnostics().front().detailsJa.find("mm") != std::string::npos,
        "the message says how much length would be needed");
}

KACHA_V2_TEST(edit, corner_operations_refuse_non_lines)
{
    const auto arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, 1.0).Value();
    Require(!ChamferLines(arc, L({0, 0, 0}, {10, 0, 0}), 1.0, 1.0e-6).HasValue(),
        "chamfer refuses an arc");
    Require(!FilletLines(arc, L({0, 0, 0}, {10, 0, 0}), 1.0, 1.0e-6).HasValue(),
        "fillet refuses an arc");
}

KACHA_V2_TEST(edit, meet_lines_pulls_both_to_the_intersection)
{
    const auto result = MeetLines(L({0, 10, 0}, {0, 4, 0}), L({4, 0, 0}, {10, 0, 0}),
        1.0e-6);
    Require(result.HasValue(), "the two lines meet");
    RequireNear(Distance(result.Value().first.EndPoint(), Vector3{0, 0, 0}), 0.0, 1.0e-9,
        "the first line now ends at the corner");
    RequireNear(Distance(result.Value().second.EndPoint(), Vector3{0, 0, 0}), 0.0, 1.0e-9,
        "the second line now ends at the corner");
}

// ---- オフセット ----

KACHA_V2_TEST(edit, a_line_offsets_sideways_in_the_plane)
{
    const auto offset = OffsetCurveInPlane(L({0, 0, 0}, {10, 0, 0}), {0, 0, 1}, 3.0);
    Require(offset.HasValue(), "the line offsets");
    Require(offset.Value().Kind() == CurveKind::Line, "it is still a line");
    RequireNear(offset.Value().TotalLength(1.0e-9), 10.0, 1.0e-9, "the length is unchanged");
    RequireNear(std::abs(offset.Value().StartPoint().y), 3.0, 1.0e-9,
        "it moved 3mm sideways");
    RequireNear(offset.Value().StartPoint().z, 0.0, 1.0e-9, "it stayed in the plane");
}

KACHA_V2_TEST(edit, an_arc_offsets_concentrically)
{
    const auto arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0, kPi).Value();
    const auto offset = OffsetCurveInPlane(arc, {0, 0, 1}, 2.0);
    Require(offset.HasValue(), "the arc offsets");
    Require(offset.Value().Kind() == CurveKind::CircularArc, "it is still an arc");
    RequireNear(offset.Value().Radius(), 12.0, 1.0e-9, "the radius grew by the distance");
    RequireNear(Distance(offset.Value().Center(), arc.Center()), 0.0, 1.0e-9,
        "the centre did not move");
    RequireNear(offset.Value().SweepAngleRad(), arc.SweepAngleRad(), 1.0e-12,
        "the sweep did not change");
}

KACHA_V2_TEST(edit, offsetting_an_arc_inside_out_is_refused)
{
    const auto arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, kPi).Value();
    Require(!OffsetCurveInPlane(arc, {0, 0, 1}, -10.0).HasValue(),
        "offsetting inward past the centre is refused");
}

KACHA_V2_TEST(edit, offsetting_a_free_curve_is_refused_not_approximated)
{
    const auto bezier = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}}).Value();
    const auto offset = OffsetCurveInPlane(bezier, {0, 0, 1}, 2.0);
    Require(!offset.HasValue(), "a free curve offset is refused");
    RequireEqual(offset.Diagnostics().front().code, "GEO-E003",
        "the refusal says the capability is missing, rather than degrading the curve");
}

// ---- 変換 ----

KACHA_V2_TEST(edit, translation_keeps_the_kind_and_the_shape)
{
    const auto arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 4.0, 0.2, 1.1).Value();
    const CurveSegment moved = TranslateCurve(arc, {10, 20, 30});
    Require(moved.Kind() == CurveKind::CircularArc, "an arc stays an arc");
    RequireNear(moved.Radius(), 4.0, 1.0e-12, "the radius is unchanged");
    RequireNear(moved.SweepAngleRad(), 1.1, 1.0e-12, "the sweep is unchanged");
    RequireNear(Distance(moved.Center(), Vector3{10, 20, 30}), 0.0, 1.0e-12,
        "the centre moved by the offset");
    RequireNear(Distance(moved.StartPoint(), arc.StartPoint() + Vector3{10, 20, 30}), 0.0,
        1.0e-12, "every point moved by the offset");
}

KACHA_V2_TEST(edit, rotation_keeps_the_kind_and_the_length)
{
    const CurveSegment line = L({10, 0, 0}, {20, 0, 0});
    const auto rotated = RotateCurve(line, {0, 0, 0}, {0, 0, 1}, kPi / 2.0);
    Require(rotated.HasValue(), "the line rotates");
    Require(rotated.Value().Kind() == CurveKind::Line, "it is still a line");
    RequireNear(rotated.Value().TotalLength(1.0e-9), 10.0, 1.0e-9, "the length is unchanged");
    RequireNear(Distance(rotated.Value().StartPoint(), Vector3{0, 10, 0}), 0.0, 1.0e-9,
        "a quarter turn about z maps +x to +y");
}

KACHA_V2_TEST(edit, an_arc_rotates_as_an_arc)
{
    const auto arc =
        CurveSegment::MakeCircularArc({5, 0, 0}, {0, 0, 1}, {1, 0, 0}, 3.0, 0.0, 1.0).Value();
    const auto rotated = RotateCurve(arc, {0, 0, 0}, {0, 0, 1}, kPi);
    Require(rotated.HasValue(), "the arc rotates");
    Require(rotated.Value().Kind() == CurveKind::CircularArc, "it is still an arc");
    RequireNear(rotated.Value().Radius(), 3.0, 1.0e-9, "the radius survives");
    RequireNear(Distance(rotated.Value().Center(), Vector3{-5, 0, 0}), 0.0, 1.0e-9,
        "the centre rotated with it");
}

KACHA_V2_TEST(edit, mirroring_keeps_the_kind_and_reflects_the_points)
{
    const CurveSegment line = L({1, 2, 0}, {5, 8, 0});
    const auto mirrored = MirrorCurve(line, {0, 0, 0}, {1, 0, 0});
    Require(mirrored.HasValue(), "the line mirrors");
    RequireNear(mirrored.Value().StartPoint().x, -1.0, 1.0e-12, "x is negated");
    RequireNear(mirrored.Value().StartPoint().y, 2.0, 1.0e-12, "y is untouched");
    RequireNear(mirrored.Value().TotalLength(1.0e-9), line.TotalLength(1.0e-9), 1.0e-9,
        "the length is unchanged");
}

KACHA_V2_TEST(edit, a_mirrored_arc_traces_the_reflected_points)
{
    const auto arc =
        CurveSegment::MakeCircularArc({5, 0, 0}, {0, 0, 1}, {1, 0, 0}, 3.0, 0.0, 1.2).Value();
    const auto mirrored = MirrorCurve(arc, {0, 0, 0}, {1, 0, 0});
    Require(mirrored.HasValue(), "the arc mirrors");
    Require(mirrored.Value().Kind() == CurveKind::CircularArc, "it is still an arc");
    for (int index = 0; index <= 10; ++index) {
        const double t = static_cast<double>(index) / 10.0;
        const Vector3 source = arc.Evaluate(t);
        const Vector3 expected{-source.x, source.y, source.z};
        RequireNear(Distance(mirrored.Value().Evaluate(t), expected), 0.0, 1.0e-9,
            "every point is the reflection of the original");
    }
}

KACHA_V2_TEST(edit, degenerate_transforms_are_refused)
{
    const CurveSegment line = L({0, 0, 0}, {10, 0, 0});
    Require(!RotateCurve(line, {0, 0, 0}, {0, 0, 0}, 1.0).HasValue(),
        "a zero rotation axis is refused");
    Require(!MirrorCurve(line, {0, 0, 0}, {0, 0, 0}).HasValue(),
        "a zero plane normal is refused");
}

KACHA_V2_TEST_MAIN("wire_edit_tests")
