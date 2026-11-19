// V1同等性(docs/v2/v1-drawing-parity.md)の編集アクションと、geometry-contract §5。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/geometry/PlanarWireOffset.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/CurveRange.h"
#include "kachakacha/geometry/CurveTrim.h"
#include "kachakacha/geometry/WireConnect.h"
#include "kachakacha/geometry/CornerCurves.h"

#include <cmath>
#include <algorithm>

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
using kachakacha::v2::geometry::OffsetCurvesInPlane;
using kachakacha::v2::geometry::RotateCurve;
using kachakacha::v2::geometry::ScaleCurve;
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
    RequireNear(result.Value().first.value().TotalLength(1.0e-9), 8.0, 1.0e-9,
        "the first line is shortened by the setback");
    RequireNear(result.Value().second.value().TotalLength(1.0e-9), 8.0, 1.0e-9,
        "the second line is shortened by the setback");
}

KACHA_V2_TEST(edit, 非対称の切戻しと残す側を指定できる)
{
    // A は 2mm、B は 5mm 切り戻す(V1 の「A の切戻し」「B の切戻し」)。
    kachakacha::v2::geometry::CornerOptions options;
    options.secondSetbackMm = 5.0;
    const auto result = ChamferLines(L({0, 10, 0}, {0, 0, 0}), L({0, 0, 0}, {10, 0, 0}), 2.0,
        options, 1.0e-6);
    Require(result.HasValue(), "非対称でも作れる");
    RequireNear(result.Value().first.value().TotalLength(1.0e-9), 8.0, 1.0e-9, "A は 2 縮む");
    RequireNear(result.Value().second.value().TotalLength(1.0e-9), 5.0, 1.0e-9, "B は 5 縮む");
    RequireNear(result.Value().corner.TotalLength(1.0e-9), std::sqrt(29.0), 1.0e-9,
        "面取りの線は sqrt(2^2+5^2)");

    // 残す側: 角をまたぐ線で、始点側を残すか終点側を残すかを選べる。
    // A: (-10,0)-(10,0)、B: (0,-10)-(0,10)。交点は原点。自動なら遠い方(どちらも 10)。
    kachakacha::v2::geometry::CornerOptions keep;
    keep.firstKeepSide = 1;    // A の始点側 (-10,0) を残す
    keep.secondKeepSide = 2;   // B の終点側 (0,10) を残す
    const auto sided = ChamferLines(L({-10, 0, 0}, {10, 0, 0}), L({0, -10, 0}, {0, 10, 0}), 3.0,
        keep, 1.0e-6);
    Require(sided.HasValue(), "残す側を指定して作れる");
    RequireNear(sided.Value().first.value().StartPoint().x, -10.0, 1.0e-9, "A は始点側が残る");
    RequireNear(sided.Value().first.value().EndPoint().x, -3.0, 1.0e-9, "A は角から 3 で切れる");
    RequireNear(sided.Value().second.value().EndPoint().y, 10.0, 1.0e-9, "B は終点側が残る");
    RequireNear(sided.Value().second.value().StartPoint().y, 3.0, 1.0e-9, "B は角から 3 で切れる");
    // 残す側が角の近くの端(長さ 0)なら断る。
    keep.firstKeepSide = 2;
    const auto bad = ChamferLines(L({-10, 0, 0}, {0, 0, 0}), L({0, -10, 0}, {0, 10, 0}), 3.0,
        keep, 1.0e-6);
    Require(!bad.HasValue(), "残す側に長さが無ければ断る");
}

KACHA_V2_TEST(edit, a_chamfer_joins_end_to_end)
{
    const auto result = ChamferLines(L({0, 10, 0}, {0, 0, 0}), L({0, 0, 0}, {10, 0, 0}), 3.0,
        1.0e-6);
    Require(result.HasValue(), "the chamfer is made");
    RequireNear(Distance(result.Value().first.value().EndPoint(),
                    result.Value().corner.StartPoint()),
        0.0, 1.0e-9, "the first line meets the chamfer");
    RequireNear(Distance(result.Value().corner.EndPoint(),
                    result.Value().second.value().StartPoint()),
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
    RequireNear(result.Value().first.value().TotalLength(1.0e-9), 7.0, 1.0e-9,
        "the first line is shortened by the radius");
    RequireNear(std::abs(result.Value().corner.SweepAngleRad()), kPi / 2.0, 1.0e-9,
        "a right angle is rounded by a quarter turn");
    RequireNear(Distance(result.Value().first.value().EndPoint(),
                    result.Value().corner.StartPoint()),
        0.0, 1.0e-9, "the line meets the arc exactly");
    RequireNear(Distance(result.Value().corner.EndPoint(),
                    result.Value().second.value().StartPoint()),
        0.0, 1.0e-9, "the arc meets the second line exactly");
}

KACHA_V2_TEST(edit, a_fillet_is_tangent_to_both_lines)
{
    const auto result = FilletLines(L({0, 10, 0}, {0, 0, 0}), L({0, 0, 0}, {10, 0, 0}), 2.5,
        1.0e-6);
    Require(result.HasValue(), "the fillet is made");
    const Vector3 lineTangent =
        Normalized(result.Value().first.value().EndPoint() - result.Value().first.value().StartPoint());
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

KACHA_V2_TEST(edit, corner_operations_accept_curves_now)
{
    // かつては直線以外を断っていたが、CornerCurves.h に委譲するようになったので
    // 直線×円弧でも作れる(円弧は始点(5,0)で線の内側に触れ、その接線(0,1)は線に垂直)。
    const auto arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, 1.0).Value();
    const auto chamfered = ChamferLines(arc, L({0, 0, 0}, {10, 0, 0}), 1.0, 1.0e-6);
    Require(chamfered.HasValue(), "chamfer now accepts a line and an arc");
    Require(chamfered.Value().corner.Kind() == CurveKind::Line,
        "the chamfer corner is a straight line");

    const auto filleted = FilletLines(arc, L({0, 0, 0}, {10, 0, 0}), 1.0, 1.0e-6);
    Require(filleted.HasValue(), "fillet now accepts a line and an arc");
    Require(filleted.Value().corner.Kind() == CurveKind::CircularArc,
        "the fillet corner is a circular arc");
}

KACHA_V2_TEST(edit, exact_length_corner_consumes_edges_without_degenerate_output)
{
    for(bool fillet : {false,true})for(bool swap : {false,true}){
        const auto shortEdge=L({0,0,0},{0,1,0});
        const auto longEdge=L({0,1,0},{3,1,0});
        const auto a=swap?longEdge:shortEdge,b=swap?shortEdge:longEdge;
        const auto made=fillet?FilletLines(a,b,1,1e-6):ChamferLines(a,b,1,1e-6);
        Require(made.HasValue(),"1mm edge accepts R1/C1 in either selection order");
        Require(made.Value().Segments().size()==2,"consumed line omitted, not stored as zero length");
        Require(swap?!made.Value().second:!made.Value().first,"correct source edge consumed");
        if(fillet)RequireNear(made.Value().corner.Radius(),1,1e-12,"radius never reduced");
        const auto tooBig=fillet?FilletLines(a,b,1.001,1e-6):ChamferLines(a,b,1.001,1e-6);
        Require(!tooBig.HasValue(),"genuinely oversized cut refused");
        const auto both=fillet?FilletLines(L({0,0,0},{0,0,1}),L({0,0,1},{1,0,1}),1,1e-6)
            :ChamferLines(L({0,0,0},{0,0,1}),L({0,0,1},{1,0,1}),1,1e-6);
        Require(both.HasValue()&&both.Value().Segments().size()==1,"both edges can be consumed in 3D");
        const auto smaller=fillet?FilletLines(a,b,0.999,1e-6):ChamferLines(a,b,0.999,1e-6);
        Require(smaller.HasValue()&&smaller.Value().Segments().size()==3,"real short remaining edges preserved");
    }
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

KACHA_V2_TEST(edit, free_curve_offset_keeps_one_curve_and_reports_bounded_approximation)
{
    const auto bezier = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}}).Value();
    const auto offset = OffsetCurveInPlane(bezier, {0, 0, 1}, 2.0);
    Require(offset.HasValue(),offset.FirstMessageJa());
    Require(offset.Value().Kind()==CurveKind::CubicBSpline,"the offset stays one smooth curve");
    RequireEqual(offset.Diagnostics().front().code,"GEO-I001","the approximation is explicitly reported");
    for(int i=0;i<=2000;++i){
        const double t=double(i)/2000;
        const auto ideal=bezier.Evaluate(t)+Normalized(Cross(Vector3{0,0,1},bezier.FirstDerivative(t)))*2.;
        Require((offset.Value().Evaluate(t)-ideal).Length()<=1e-6,"the entire offset follows the requested side and distance");
    }
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

KACHA_V2_TEST(edit, scaling_keeps_the_kind_and_scales_about_the_centre)
{
    // 直線: 中心 (1,0) から 2 倍。
    const auto line = ScaleCurve(L({2, 0, 0}, {4, 2, 0}), {1, 0, 0}, 2.0);
    Require(line.HasValue() && line.Value().Kind() == CurveKind::Line, "a line stays a line");
    RequireNear(Distance(line.Value().StartPoint(), Vector3{3, 0, 0}), 0.0, 1.0e-12, "start scaled");
    RequireNear(Distance(line.Value().EndPoint(), Vector3{7, 4, 0}), 0.0, 1.0e-12, "end scaled");
    // 円弧: 中心を写し、半径に倍率。どの点も中心から倍率ぶん離れる。
    const auto arc =
        CurveSegment::MakeCircularArc({5, 0, 0}, {0, 0, 1}, {1, 0, 0}, 3.0, 0.0, 1.2).Value();
    const auto scaled = ScaleCurve(arc, {0, 0, 0}, 0.5);
    Require(scaled.HasValue() && scaled.Value().Kind() == CurveKind::CircularArc, "an arc stays an arc");
    RequireNear(scaled.Value().Radius(), 1.5, 1.0e-12, "radius times the factor");
    for (int index = 0; index <= 10; ++index) {
        const double t = static_cast<double>(index) / 10.0;
        const Vector3 source = arc.Evaluate(t);
        RequireNear(Distance(scaled.Value().Evaluate(t), source * 0.5), 0.0, 1.0e-9,
            "every point is scaled about the centre");
    }
    // 倍率 0 以下は断る。
    Require(!ScaleCurve(arc, {0, 0, 0}, 0.0).HasValue(), "a zero factor is refused");
    Require(!ScaleCurve(arc, {0, 0, 0}, -2.0).HasValue(), "a negative factor is refused");
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

namespace {
std::vector<CurveSegment> RoundedSteppedContour() {
    return {L({3,-10,0},{8,-10,0}),
        CurveSegment::MakeCircularArc({8,-8,0},{0,0,1},{1,0,0},2,-kPi/2,kPi/2).Value(),
        L({10,-8,0},{10,40,0}),L({10,40,0},{-10,40,0}),L({-10,40,0},{-10,-8,0}),
        CurveSegment::MakeCircularArc({-8,-8,0},{0,0,1},{1,0,0},2,kPi,kPi/2).Value(),
        L({-8,-10,0},{-3,-10,0}),L({-3,-10,0},{-3,-11,0}),
        L({-3,-11,0},{3,-11,0}),L({3,-11,0},{3,-10,0})};
}
void RequireClosedOffset(const std::vector<CurveSegment>& curves) {
    Require(!curves.empty(),"a contour is present");
    for(std::size_t i=0;i<curves.size();++i)
        RequireNear(Distance(curves[i].EndPoint(),curves[(i+1)%curves.size()].StartPoint()),
            0.,1e-6,"all offset corners meet without connector fragments");
}
}
KACHA_V2_TEST(edit, closed_mixed_contour_offset_preserves_arcs_notches_and_three_dimensional_placement)
{
    using namespace kachakacha::v2::geometry;
    const auto source=RoundedSteppedContour();
    for(const double angle:{0.,.7,1.8}){
        std::vector<CurveSegment> placed;
        for(const auto& curve:source)placed.push_back(TranslateCurve(
            RotateCurve(curve,{0,0,0},{1,0,0},angle).Value(),{7,-3,9}));
        const Vector3 normal{0,-std::sin(angle),std::cos(angle)};
        const auto made=OffsetCurvesInPlane(placed,normal,.55);
        Require(made.HasValue(),"a concave contour with rounded corners has a connected inset");
        RequireClosedOffset(made.Value());
        Require(made.Value().size()==source.size(),"no line fragments or extra joins are introduced");
        int arcs=0;
        for(const auto& curve:made.Value())if(curve.Kind()==CurveKind::CircularArc){
            ++arcs;RequireNear(curve.Radius(),1.45,1e-9,"both opposite rounded corners inset consistently");
        }
        Require(arcs==2,"two exact arcs remain arcs");
        std::vector<CurveSegment> reversed;
        for(const auto& curve:placed)reversed.push_back(ReverseCurve(curve).Value());
        const auto other=OffsetCurvesInPlane(reversed,normal,-.55);
        Require(other.HasValue(),"reversing traversal and offset sign keeps the same geometry");
        RequireClosedOffset(other.Value());
        for(const auto& curve:made.Value())for(double t:{0.,.25,.5,.75,1.}){
            const auto p=curve.Evaluate(t);double nearest=1e9;
            for(const auto& candidate:other.Value())nearest=std::min(nearest,
                Distance(p,ClosestOnCurve(candidate,p).point));
            RequireNear(nearest,0.,1e-6,"reversed traversal follows the same exact offset curves");
        }
    }
}
KACHA_V2_TEST(edit, offset_trims_a_line_and_semicircle_to_their_new_intersections)
{
    using namespace kachakacha::v2::geometry;
    const auto arc=CurveSegment::MakeCircularArc({0,0,0},{0,0,1},{1,0,0},4,0,kPi).Value();
    const std::vector<CurveSegment> source{L({-4,0,0},{4,0,0}),arc};
    for(double distance:{.5,-.5}){
        const auto made=OffsetCurvesInPlane(source,{0,0,1},distance);
        Require(made.HasValue(),"a line/arc corner can be trimmed inward or extended outward");
        RequireClosedOffset(made.Value());
        RequireNear(made.Value()[1].Radius(),4-distance,1e-9,"the arc remains an exact parallel circle");
        RequireNear(made.Value()[0].StartPoint().y,distance,1e-9,"the chord stays parallel");
    }
}
KACHA_V2_TEST(edit, offset_trims_two_arcs_at_circle_intersections)
{
    using namespace kachakacha::v2::geometry;
    const auto a=CurveSegment::MakeCircularArc({-2,0,0},{0,0,1},{1,0,0},4,-kPi/3,2*kPi/3).Value();
    const auto b=CurveSegment::MakeCircularArc({2,0,0},{0,0,1},{1,0,0},4,2*kPi/3,2*kPi/3).Value();
    const auto made=OffsetCurvesInPlane({a,b},{0,0,1},.5);
    Require(made.HasValue(),"a curved lens has a connected inset");RequireClosedOffset(made.Value());
    for(const auto& curve:made.Value()){
        RequireNear(curve.Radius(),3.5,1e-9,"each edge stays on its parallel circle");
        RequireNear(std::abs(curve.StartPoint().y),std::sqrt(3.5*3.5-4),1e-6,
            "the corners use the new intersection rather than the old vertex");
    }
}
KACHA_V2_TEST(edit, collapsed_and_self_crossing_contour_offsets_are_rejected)
{
    using namespace kachakacha::v2::geometry;
    const std::vector<CurveSegment> small{L({0,0,0},{1,0,0}),L({1,0,0},{1,1,0}),
        L({1,1,0},{0,1,0}),L({0,1,0},{0,0,0})};
    Require(!OffsetCurvesInPlane(small,{0,0,1},.6).HasValue(),"an inset cannot invert an edge");
    const std::vector<CurveSegment> crossed{L({0,0,0},{4,4,0}),L({4,4,0},{0,4,0}),
        L({0,4,0},{4,0,0}),L({4,0,0},{0,0,0})};
    Require(!OffsetCurvesInPlane(crossed,{0,0,1},.1).HasValue(),"a self-crossing source has no implicit material side");
    Require(!OffsetCurvesInPlane(RoundedSteppedContour(),{1,0,0},.2).HasValue(),
        "a wrong processing plane is not silently accepted");
}

namespace {
void RequireOpenOffset(const std::vector<CurveSegment>& curves) {
    Require(curves.size()>1,"the open chain has several original edges");
    for(std::size_t i=1;i<curves.size();++i)
        RequireNear(Distance(curves[i-1].EndPoint(),curves[i].StartPoint()),0.,1e-6,
            "adjacent offset edges meet without connector fragments");
    Require(Distance(curves.front().StartPoint(),curves.back().EndPoint())>1e-3,
        "offsetting an open chain does not close its ends");
}
std::vector<CurveSegment> ThreeArcOpenCrown() {
    const auto left=CurveSegment::MakeCircularArc({-13.5,0,27},{0,1,0},{-1,0,0},
        4.5,0,1.459455312453022).Value();
    const auto middle=CurveSegment::MakeCircularArc({-2.0818902157770935e-12,0,-63.35728066542623},
        {0,1,0},{-.14605046067248964,0,.989277141622788},95.85728066542625,
        0,.29314947238302).Value();
    const auto right=CurveSegment::MakeCircularArc({13.5,0,27},{0,1,0},{1,0,0},
        4.5,-1.459455312453022,1.459455312453022).Value();
    return {left,middle,right};
}
}
KACHA_V2_TEST(edit, open_line_chain_offsets_keep_corners_free_ends_and_three_dimensional_placement)
{
    using namespace kachakacha::v2::geometry;
    const std::vector<CurveSegment> original{L({0,0,0},{8,0,0}),L({8,0,0},{8,5,0}),
        L({8,5,0},{12,5,0})};
    for(double angle:{0.,.8})for(double distance:{.2,-.2}){
        std::vector<CurveSegment> source;
        for(const auto& c:original)source.push_back(TranslateCurve(
            RotateCurve(c,{0,0,0},{1,0,0},angle).Value(),{7,-3,9}));
        const Vector3 normal{0,-std::sin(angle),std::cos(angle)};
        const auto made=OffsetCurvesInPlane(source,normal,distance);
        Require(made.HasValue(),"a strictly connected open polyline can be offset");
        RequireOpenOffset(made.Value());Require(made.Value().size()==source.size(),"edge count is retained");
        const auto first=OffsetCurveInPlane(source.front(),normal,distance).Value();
        const auto last=OffsetCurveInPlane(source.back(),normal,distance).Value();
        RequireNear(Distance(made.Value().front().StartPoint(),first.StartPoint()),0.,1e-9,"the free start stays offset in place");
        RequireNear(Distance(made.Value().back().EndPoint(),last.EndPoint()),0.,1e-9,"the free end stays offset in place");
        for(std::size_t i=0;i<source.size();++i)RequireNear(Distance(source[i].Evaluate(.5),
            TranslateCurve(RotateCurve(original[i],{0,0,0},{1,0,0},angle).Value(),{7,-3,9}).Evaluate(.5)),
            0.,1e-12,"source geometry is unchanged");
    }
}
KACHA_V2_TEST(edit, open_tangent_line_arc_chain_uses_the_same_material_side_for_either_plane_normal)
{
    using namespace kachakacha::v2::geometry;
    const std::vector<CurveSegment> source{L({-5,0,0},{0,0,0}),
        CurveSegment::MakeCircularArc({0,1,0},{0,0,1},{0,-1,0},1,0,kPi/2).Value(),
        L({1,1,0},{1,5,0})};
    for(double normalSign:{1.,-1.})for(double distance:{.2,-.2}){
        const auto made=OffsetCurvesInPlane(source,{0,0,normalSign},distance);
        Require(made.HasValue(),"line/arc/line remains connected");RequireOpenOffset(made.Value());
        Require(made.Value().size()==3&&made.Value()[1].Kind()==CurveKind::CircularArc,
            "the original exact arc is not replaced by short lines");
        RequireNear(made.Value()[1].Radius(),1-normalSign*distance,1e-9,"plane orientation defines the same side for lines and arcs");
        RequireNear(made.Value().front().StartPoint().y,normalSign*distance,1e-9,"the straight edge uses the same side");
    }
}
KACHA_V2_TEST(edit, open_nontangent_arc_chain_offsets_trim_intersections_and_preserve_source_radii)
{
    using namespace kachakacha::v2::geometry;
    const auto original=ThreeArcOpenCrown();
    for(double angle:{0.,.63})for(double distance:{.05,-.05}){
        std::vector<CurveSegment> source;
        for(const auto& c:original)source.push_back(TranslateCurve(
            RotateCurve(c,{0,0,0},{1,0,0},angle).Value(),{3,-6,11}));
        const Vector3 normal{0,std::cos(angle),std::sin(angle)};
        const auto made=OffsetCurvesInPlane(source,normal,distance);
        Require(made.HasValue(),"a three arc open crown has connected offset intersections");
        RequireOpenOffset(made.Value());Require(made.Value().size()==3,"no tiny gap filler edges are created");
        for(std::size_t i=0;i<source.size();++i){
            Require(made.Value()[i].Kind()==CurveKind::CircularArc,"every curve stays an exact arc");
            RequireNear(made.Value()[i].Radius(),source[i].Radius()-distance,1e-9,"only the requested radial offset is applied");
            RequireNear(Distance(made.Value()[i].Center(),source[i].Center()),0.,1e-9,"the original centers are preserved");
            RequireNear(source[i].Radius(),original[i].Radius(),1e-12,"source radii remain unchanged");
        }
        const auto first=OffsetCurveInPlane(source.front(),normal,-distance).Value();
        const auto last=OffsetCurveInPlane(source.back(),normal,-distance).Value();
        RequireNear(Distance(made.Value().front().StartPoint(),first.StartPoint()),0.,1e-9,"the first free angle is preserved");
        RequireNear(Distance(made.Value().back().EndPoint(),last.EndPoint()),0.,1e-9,"the last free angle is preserved");
        std::vector<CurveSegment> reversed;
        for(auto it=source.rbegin();it!=source.rend();++it)reversed.push_back(ReverseCurve(*it).Value());
        const auto other=OffsetCurvesInPlane(reversed,normal,-distance);
        Require(other.HasValue(),"reversed traversal and sign produce the same open crown");RequireOpenOffset(other.Value());
        for(std::size_t i=0;i<source.size();++i)for(double t:{0.,.25,.5,.75,1.})
            RequireNear(Distance(made.Value()[i].Evaluate(t),other.Value()[source.size()-i-1].Evaluate(1-t)),
                0.,1e-6,"reverse traversal preserves the offset locus");
    }
}
KACHA_V2_TEST(edit, open_offset_does_not_join_disconnected_near_or_branched_inputs)
{
    using namespace kachakacha::v2::geometry;
    for(double gap:{.0005,2.}){
        const std::vector<CurveSegment> source{L({0,0,0},{4,0,0}),L({4+gap,0,0},{4+gap,4,0})};
        const auto made=OffsetCurvesInPlane(source,{0,0,1},.2);
        Require(made.HasValue()&&made.Value().size()==2,"disconnected inputs retain standalone semantics");
        for(std::size_t i=0;i<source.size();++i){
            const auto expected=OffsetCurveInPlane(source[i],{0,0,1},.2).Value();
            RequireNear(Distance(made.Value()[i].StartPoint(),expected.StartPoint()),0.,1e-9,"no source gap is silently repaired");
            RequireNear(Distance(made.Value()[i].EndPoint(),expected.EndPoint()),0.,1e-9,"no endpoint is pulled to a new join");
        }
    }
    const auto arc=ThreeArcOpenCrown().front();
    const auto single=OffsetCurvesInPlane({arc},{0,1,0},.2);
    Require(single.HasValue(),"the single-arc API remains available");
    RequireNear(single.Value().front().Radius(),arc.Radius()+.2,1e-9,"standalone arc radius semantics remain compatible");
    const std::vector<CurveSegment> branch{L({-3,0,0},{0,0,0}),L({0,0,0},{3,0,0}),L({0,0,0},{0,3,0})};
    const auto separate=OffsetCurvesInPlane(branch,{0,0,1},.2);
    Require(separate.HasValue()&&separate.Value().size()==3,"a branch is not turned into an arbitrary connected chain");
}
KACHA_V2_TEST(edit, open_offset_rejects_collapse_crossings_and_wrong_processing_planes)
{
    using namespace kachakacha::v2::geometry;
    const std::vector<CurveSegment> narrow{L({0,4,0},{0,0,0}),L({0,0,0},{1,0,0}),L({1,0,0},{1,4,0})};
    Require(!OffsetCurvesInPlane(narrow,{0,0,1},.6).HasValue(),"an open inset cannot reverse its middle edge");
    const std::vector<CurveSegment> crossed{L({0,0,0},{4,4,0}),L({4,4,0},{0,4,0}),L({0,4,0},{4,0,0})};
    Require(!OffsetCurvesInPlane(crossed,{0,0,1},.1).HasValue(),"a self-crossing open chain is rejected");
    Require(!OffsetCurvesInPlane(narrow,{1,0,0},.1).HasValue(),"a connected chain is not moved out of its processing plane");
}

// ADR 0033 / AT-WIR: geometry is checked against the normal-offset definition,
// not against the fitting implementation or a screen polyline.
namespace {
void RequireCurveOffset(const CurveSegment& source,const CurveSegment& shifted,Vector3 normal,double distance) {
    for(int i=0;i<=2000;++i){
        const double t=double(i)/2000;
        // The public spline derivative uses a one-sided 1e-6 step at its ends;
        // use a finer independent difference for this model-tolerance check.
        const double a=std::max(0.,t-1e-8),b=std::min(1.,t+1e-8);
        const auto tangent=source.Kind()==CurveKind::CubicBSpline
            ?(source.Evaluate(b)-source.Evaluate(a))*(1/(b-a)):source.FirstDerivative(t);
        const auto expected=source.Evaluate(t)+Normalized(Cross(normal,tangent))*distance;
        RequireNear(Distance(shifted.Evaluate(t),expected),0.,1e-6,"the requested signed normal distance is retained");
    }
}
void RequireOffsetChain(const std::vector<CurveSegment>& made,bool closed) {
    using namespace kachakacha::v2::geometry;
    Require(!made.empty(),"the result is not empty");
    for(std::size_t i=1;i<made.size();++i)
        RequireNear(Distance(made[i-1].EndPoint(),made[i].StartPoint()),0.,1e-6,"the result stays connected");
    if(closed)RequireNear(Distance(made.back().EndPoint(),made.front().StartPoint()),0.,1e-6,"the loop stays closed");
    Require(FindSelfIntersections(made,closed,{}).HasValue(),"the result does not cross itself");
}
}
KACHA_V2_TEST(edit, curved_offset_accepts_bspline_as_one_curve) {
    const auto source=CurveSegment::MakeCubicBSpline({{-3,-1,0},{-2,0,0},{0,1,0},{2,-1,0},{4,0,0},{5,1,0}}).Value();
    for(double distance:{-.2,.2}){
        const auto made=OffsetCurvesInPlane({source},{0,0,1},distance);
        Require(made.HasValue(),made.FirstMessageJa());Require(made.Value().size()==1,"one spline stays one selectable curve");
        RequireCurveOffset(source,made.Value().front(),{0,0,1},distance);
        RequireOffsetChain(made.Value(),false);
    }
}
KACHA_V2_TEST(edit, curved_offset_is_independent_of_plane_orientation_position_and_direction) {
    using namespace kachakacha::v2::geometry;
    const auto n=Normalized(Vector3{.3,-.4,1}),u=Normalized(Vector3{1,0,-.3}),v=Cross(n,u);
    const auto point=[&](double x,double y){return Vector3{1000,-2000,3000}+u*x+v*y;};
    const auto source=CurveSegment::MakeCubicBezier({point(0,0),point(0,10),point(10,10),point(10,0)}).Value();
    const auto poles=source.ControlPoints();
    for(double distance:{-.4,.4}){
        const auto made=OffsetCurvesInPlane({source},n,distance);Require(made.HasValue(),made.FirstMessageJa());
        RequireCurveOffset(source,made.Value().front(),n,distance);
        const auto reverse=OffsetCurvesInPlane({ReverseCurve(source).Value()},n,-distance);
        Require(reverse.HasValue(),reverse.FirstMessageJa());
        for(double t:{0.,.17,.37,.6,.83,1.})RequireNear(Distance(made.Value().front().Evaluate(t),reverse.Value().front().Evaluate(1-t)),
            0.,2e-6,"reversing traversal and distance preserves the locus");
    }
    Require(source.ControlPoints()==poles,"the original control points remain untouched");
}
KACHA_V2_TEST(edit, curved_offset_keeps_a_mixed_rounded_loop_closed) {
    using namespace kachakacha::v2::geometry;
    const std::vector<CurveSegment> source{L({-10,10,0},{10,10,0}),L({10,10,0},{10,-4,0}),
        CurveSegment::MakeCubicBezier({{10,-4,0},{10,-7.3137085,0},{7.3137085,-10,0},{4,-10,0}}).Value(),
        L({4,-10,0},{-4,-10,0}),
        CurveSegment::MakeCubicBezier({{-4,-10,0},{-7.3137085,-10,0},{-10,-7.3137085,0},{-10,-4,0}}).Value(),
        L({-10,-4,0},{-10,10,0})};
    for(double distance:{-.35,.35}){
        auto shuffled=source;std::swap(shuffled[1],shuffled[4]);
        const auto made=OffsetCurvesInPlane(shuffled,{0,0,1},distance);
        Require(made.HasValue(),made.FirstMessageJa());RequireOffsetChain(made.Value(),true);
        Require(made.Value().size()==source.size(),"smooth curved corners do not become fragments");
        Require(std::count_if(made.Value().begin(),made.Value().end(),[](const auto& c){return c.Kind()==CurveKind::CubicBSpline;})==2,
            "each curved side remains one spline");
    }
}
KACHA_V2_TEST(edit, curved_offset_connects_outer_and_inner_sharp_corners) {
    const auto source=CurveSegment::MakeCubicBezier({{0,0,0},{3,0,0},{7,4,0},{10,4,0}}).Value();
    for(double distance:{-.5,.5}){
        const auto made=OffsetCurvesInPlane({source,L({10,4,0},{10,12,0})},{0,0,1},distance);
        Require(made.HasValue(),made.FirstMessageJa());RequireOffsetChain(made.Value(),false);
        Require(std::count_if(made.Value().begin(),made.Value().end(),[](const auto& c){return c.Kind()==CurveKind::CubicBSpline;})==1,
            "the sharp connection does not split the curved edge");
        RequireNear(Distance(made.Value().front().StartPoint(),Vector3{0,distance,0}),0.,1e-6,"the free start is the shifted original start");
        RequireNear(Distance(made.Value().back().EndPoint(),Vector3{10-distance,12,0}),0.,1e-6,"the free end is preserved");
    }
}
KACHA_V2_TEST(edit, curved_offset_preserves_one_closed_spline) {
    const auto source=CurveSegment::MakeCubicBSpline({{-10,0,0},{0,10,0},{10,0,0},{0,-10,0},{-10,0,0},{0,10,0},{10,0,0}}).Value();
    for(double distance:{-.3,.3}){
        const auto made=OffsetCurvesInPlane({source},{0,0,1},distance);
        Require(made.HasValue(),made.FirstMessageJa());Require(made.Value().size()==1,"a smooth closed spline remains one curve");
        RequireOffsetChain(made.Value(),true);RequireCurveOffset(source,made.Value().front(),{0,0,1},distance);
    }
}
KACHA_V2_TEST(edit, curved_offset_refuses_collapse_nonplanar_input_and_partial_results) {
    const auto source=CurveSegment::MakeCubicBezier({{0,0,0},{0,10,0},{10,10,0},{10,0,0}}).Value();
    Require(!OffsetCurvesInPlane({source},{0,0,1},-5).HasValue(),"an offset beyond the curvature radius is refused");
    Require(!OffsetCurvesInPlane({source},{1,0,0},.3).HasValue(),"the input is not silently flattened");
    Require(!OffsetCurvesInPlane({L({40,0,0},{45,0,0}),source},{0,0,1},-5).HasValue(),"a failure does not return an earlier successful edge");
    const auto unchanged=OffsetCurvesInPlane({source},{0,0,1},0);
    Require(unchanged.HasValue()&&unchanged.Value().front().ControlPoints()==source.ControlPoints(),"zero distance preserves the original curve exactly");
}
KACHA_V2_TEST(edit, exact_spline_restriction_keeps_geometry_with_three_pieces_at_most) {
    using namespace kachakacha::v2::geometry;
    std::vector<Vector3> poles;
    for(int i=0;i<80;++i)poles.push_back({double(i),std::sin(i*.17)*4,std::cos(i*.23)});
    const auto source=CurveSegment::MakeCubicBSpline(poles).Value();const double n=static_cast<double>(poles.size()-3);
    for(const auto range:std::vector<std::pair<double,double>>{{0,1},{0,.731},{.137,1},{.137,.854},{.5,.5001},{10/n,35/n}}) {
        const auto made=ExtractCurveRange(source,range.first,range.second);Require(made.HasValue(),made.FirstMessageJa());
        Require(made.Value().size()<=3,"restriction does not expand the spline into its sampling spans");
        std::vector<double> stops{range.first};
        const double firstKnot=std::ceil(range.first*n-1e-12)/n,lastKnot=std::floor(range.second*n+1e-12)/n;
        if(range.first>0&&firstKnot>range.first&&firstKnot<range.second)stops.push_back(firstKnot);
        if(range.second<1&&lastKnot>stops.back()&&lastKnot<range.second)stops.push_back(lastKnot);
        stops.push_back(range.second);
        Require(made.Value().size()+1==stops.size(),"only exact end restrictions and one interior are returned");
        for(std::size_t j=0;j<made.Value().size();++j)for(int i=0;i<=100;++i) {
            const double t=i/100.,original=stops[j]+(stops[j+1]-stops[j])*t;
            RequireNear(Distance(made.Value()[j].Evaluate(t),source.Evaluate(original)),0.,1e-10,"all retained points stay on the unchanged source locus");
            Require(Dot(Normalized(made.Value()[j].FirstDerivative(t)),Normalized(source.FirstDerivative(original)))>1-1e-10,"the retained tangent is exact");
        }
    }
    Require(source.ControlPoints()==poles,"restriction never edits source controls");
    Require(!ExtractCurveRange(source,-.1,.4).HasValue()&&!ExtractCurveRange(source,.7,.2).HasValue(),"invalid ranges are rejected");
}
KACHA_V2_TEST(edit, analytic_spline_derivatives_remain_correct_at_endpoints_and_many_knots) {
    using namespace kachakacha::v2::geometry;
    std::vector<Vector3> poles;for(int i=0;i<4099;++i)poles.push_back({i*.01,std::sin(i*.01),0});
    const auto spline=CurveSegment::MakeCubicBSpline(poles).Value();const double n=static_cast<double>(poles.size()-3);
    const auto expectedFirst=(poles[2]-poles[0])*(n*.5),expectedLast=(poles.back()-poles[poles.size()-3])*(n*.5);
    RequireNear(Distance(spline.FirstDerivative(0),expectedFirst),0.,1e-9,"first endpoint derivative is analytic");
    RequireNear(Distance(spline.FirstDerivative(1),expectedLast),0.,1e-9,"last endpoint derivative is analytic");
    for(double t:{0.,.001, .137,.5,.999,1.}) {
        const double low=std::max(0.,t-1e-8),high=std::min(1.,t+1e-8);
        const auto numerical=(spline.Evaluate(high)-spline.Evaluate(low))*(1/(high-low));
        RequireNear(Distance(spline.FirstDerivative(t),numerical),0.,.0001,"dense-knot first derivatives match an independent positional difference");
    }
}
KACHA_V2_TEST(edit, spline_chamfer_and_fillet_retain_all_unedited_sections) {
    using namespace kachakacha::v2::geometry;
    const auto spline=CurveSegment::MakeCubicBSpline({{-10,0,0},{-6,0,0},{-2,2,0},{2,5,0},{6,7,0},{10,8,0},{14,8,0}}).Value();
    const auto line=L(spline.EndPoint(),spline.EndPoint()+Vector3{8,-6,0});
    CornerOptions options;options.firstKeepSide=1;options.secondKeepSide=2;
    for(bool fillet:{false,true}) {
        const auto made=fillet?FilletLines(spline,line,.5,options,1e-7):ChamferLines(spline,line,.5,options,1e-7);
        Require(made.HasValue(),made.FirstMessageJa());const auto pieces=made.Value().Segments();
        Require(pieces.size()>=3&&pieces.size()<=5,"the spline corner keeps compact exact pieces");
        RequireNear(Distance(pieces.front().StartPoint(),spline.StartPoint()),0.,1e-9,"the unedited start is retained");
        RequireNear(Distance(pieces.back().EndPoint(),line.EndPoint()),0.,1e-9,"the unedited other end is retained");
        for(std::size_t i=1;i<pieces.size();++i) {
            RequireNear(Distance(pieces[i-1].EndPoint(),pieces[i].StartPoint()),0.,1e-6,"the corrected corner remains connected");
            if(fillet)Require(Dot(Normalized(pieces[i-1].FirstDerivative(1)),Normalized(pieces[i].FirstDerivative(0)))>1-1e-5,"the R has tangent continuity without internal corners");
        }
        for(const auto& c:made.Value().firstPrefix)for(double t:{0.,.3,.7,1.})
            RequireNear(spline.ClosestPoint(c.Evaluate(t)).distance,0.,1e-6,"uncut spline geometry is unchanged");
    }
}
KACHA_V2_TEST(edit, offset_spline_can_be_chamfered_without_polyline_conversion) {
    using namespace kachakacha::v2::geometry;
    const auto spline=CurveSegment::MakeCubicBSpline({{-10,0,0},{-6,0,0},{-2,2,0},{2,5,0},{6,7,0},{10,8,0},{14,8,0}}).Value();
    const auto shifted=OffsetCurvesInPlane({spline},{0,0,1},.3);Require(shifted.HasValue(),shifted.FirstMessageJa());
    const auto& curve=shifted.Value().front();const auto other=L(curve.EndPoint(),curve.EndPoint()+Vector3{8,-6,0});
    const auto result=ChamferLines(curve,other,.4,1e-7);Require(result.HasValue(),result.FirstMessageJa());
    Require(result.Value().firstPrefix.size()<=2,"offset knots are not made into many editable edges");
    RequireNear(Distance(result.Value().Segments().front().StartPoint(),curve.StartPoint()),0.,1e-8,"chamfer does not move the opposite end");
}
KACHA_V2_TEST(edit, spline_trim_keeps_exact_remainders_and_split_accepts_spline) {
    using namespace kachakacha::v2::geometry;
    const auto spline=CurveSegment::MakeCubicBSpline({{-12,0,0},{-8,1,0},{-4,-1,0},{0,1,0},{4,-1,0},{8,1,0},{12,0,0}}).Value();
    TrimSpan span;span.low=.21;span.high=.73;
    const auto remainder=RemoveSpan(spline,span);Require(remainder.HasValue(),remainder.FirstMessageJa());
    Require(remainder.Value().before.size()<=3&&remainder.Value().after.size()<=3,"trim keeps two compact continuous remainders");
    RequireNear(Distance(remainder.Value().before.back().EndPoint(),spline.Evaluate(.21)),0.,1e-10,"the first real cut is retained");
    RequireNear(Distance(remainder.Value().after.front().StartPoint(),spline.Evaluate(.73)),0.,1e-10,"the second real cut is retained");
    const auto blade=L({0,-5,0},{0,5,0});const auto split=SplitCurveAtIntersections(spline,{blade},1e-7);
    Require(split.HasValue(),split.FirstMessageJa());Require(split.Value().size()>=2&&split.Value().size()<=6,"a spline intersection split is exact and compact");
}
KACHA_V2_TEST(edit, disconnected_lines_auto_extend_for_C_and_R_in_a_tilted_plane) {
    const Vector3 origin{13,-17,8},u=Normalized(Vector3{1,2,.3}),n=Normalized(Cross(u,Vector3{.2,-.3,1})),v=Cross(n,u);
    for(bool reverse:{false,true})for(bool fillet:{false,true}) {
        auto a=L(origin+u*2,origin+u*12),b=L(origin+v*3,origin+v*11);
        if(reverse){a=ReverseCurve(a).Value();b=ReverseCurve(b).Value();}
        const auto result=fillet?FilletLines(a,b,.8,1e-7):ChamferLines(a,b,.8,1e-7);
        Require(result.HasValue(),result.FirstMessageJa());const auto parts=result.Value().Segments();
        Require(parts.size()==3,"auto extension returns only two retained lines and the corner");
        RequireNear(Distance(parts.front().StartPoint(),origin+u*12),0.,1e-7,"the far end of A is unchanged");
        RequireNear(Distance(parts.back().EndPoint(),origin+v*11),0.,1e-7,"the far end of B is unchanged");
        for(std::size_t i=1;i<parts.size();++i)RequireNear(Distance(parts[i-1].EndPoint(),parts[i].StartPoint()),0.,1e-7,"extended pieces meet the bridge");
        RequireNear(Distance(parts.front().EndPoint(),origin+u*.8),0.,1e-7,"the near gap is filled up to the requested setback");
    }
}
KACHA_V2_TEST(edit, corner_uses_model_tolerance_without_silently_flattening_skew_lines) {
    const auto a=L({2,0,0},{12,0,0}),b=L({0,3,.003},{0,11,.003});
    Require(FilletLines(a,b,.8,.01).HasValue(),"small extraction deviations obey the declared model tolerance");
    Require(!FilletLines(a,b,.8,.0001).HasValue(),"a real skew pair beyond tolerance is refused");
    Require(!ChamferLines(a,L({0,3,.1},{0,11,.1}),.8,.01).HasValue(),"another plane is never silently projected");
    Require(!FilletLines(a,L({2,3,0},{12,3,0}),.8,.01).HasValue(),"parallel lines have no extension corner");
    RequireNear(a.StartPoint().x,2.,0.,"the input is never mutated");
}
KACHA_V2_TEST_MAIN("wire_edit_tests")
