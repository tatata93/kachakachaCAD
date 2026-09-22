// CornerCurves.h(直線×曲線・曲線どうしの面取り・丸め)の受入試験。
// 数値は実装を動かして確かめた「正解」で、ここでは緩めない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CornerCurves.h"
#include "kachakacha/geometry/WireEdit.h"

#include <cmath>

using kachakacha::v2::geometry::ChamferLines;
using kachakacha::v2::geometry::CornerBetweenCurves;
using kachakacha::v2::geometry::CornerKind;
using kachakacha::v2::geometry::CornerOptions;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Distance;
using kachakacha::v2::geometry::FilletLines;
using kachakacha::v2::geometry::kPi;
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

[[nodiscard]] CurveSegment Arc(Vector3 center, Vector3 normal, Vector3 reference, double radius,
    double startAngleRad, double sweepAngleRad)
{
    const auto made = CurveSegment::MakeCircularArc(center, normal, reference, radius,
        startAngleRad, sweepAngleRad);
    Require(made.HasValue(), "the fixture arc is valid");
    return made.Value();
}

[[nodiscard]] CurveSegment Circ(Vector3 center, Vector3 normal, Vector3 reference, double radius)
{
    const auto made = CurveSegment::MakeCircle(center, normal, reference, radius);
    Require(made.HasValue(), "the fixture circle is valid");
    return made.Value();
}
} // namespace

// ---- 直線×円弧 ----

KACHA_V2_TEST(corner_curves, fillet_line_and_arc_that_already_meet)
{
    // 線 (10,-5)-(10,0) と、円弧 中心(10,5) 半径5 が (10,0)→(15,5) を描く。
    // 線の接線は(0,1)、円弧の接線(始点)は(1,0)で直交するので丸められる。
    const auto line = L({10, -5, 0}, {10, 0, 0});
    const auto arc = Arc({10, 5, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, -kPi / 2.0, kPi / 2.0);
    const auto result = FilletLines(line, arc, 2.0, 1.0e-6);
    Require(result.HasValue(), "the fillet is made between a line and an arc");
    const auto& corner = result.Value().corner;
    Require(corner.Kind() == CurveKind::CircularArc, "the corner is an arc");
    RequireNear(corner.Radius(), 2.0, 1.0e-6, "the requested radius is used");
    RequireNear(Distance(corner.Center(), Vector3{12, -1.7082, 0}), 0.0, 1.0e-3,
        "the fillet centre matches the verified fixture");
    RequireNear(Distance(result.Value().first.StartPoint(), Vector3{10, -5, 0}), 0.0, 1.0e-3,
        "the first piece keeps its far end");
    RequireNear(Distance(result.Value().first.EndPoint(), Vector3{10, -1.7082, 0}), 0.0, 1.0e-3,
        "the first piece is cut back to the tangent point");
    RequireNear(Distance(result.Value().second.StartPoint(), Vector3{11.4286, 0.2084, 0}), 0.0,
        1.0e-3, "the second piece starts at the tangent point on the arc");
    RequireNear(Distance(result.Value().second.EndPoint(), Vector3{15, 5, 0}), 0.0, 1.0e-3,
        "the second piece keeps the arc's far end");
    // 接する: 中心から線・円弧までの距離が、どちらも半径と一致する。
    RequireNear(line.ClosestPoint(corner.Center()).distance, 2.0, 1.0e-6,
        "the centre is exactly radius away from the line");
    RequireNear(arc.ClosestPoint(corner.Center()).distance, 2.0, 1.0e-6,
        "the centre is exactly radius away from the arc");
    // つながり: first→corner→second が端点で一致する。
    RequireNear(Distance(result.Value().first.EndPoint(), corner.StartPoint()), 0.0, 1.0e-6,
        "the first piece meets the fillet");
    RequireNear(Distance(corner.EndPoint(), result.Value().second.StartPoint()), 0.0, 1.0e-6,
        "the fillet meets the second piece");
}

// ---- 直線×円(丸ごとの円は角で円弧に読み替える) ----

KACHA_V2_TEST(corner_curves, fillet_line_and_full_circle_with_hints)
{
    const auto line = L({-10, 0, 0}, {10, 0, 0});
    const auto circle = Circ({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0);
    CornerOptions options;
    options.firstHint = Vector3{8, 0, 0};
    options.secondHint = Vector3{0, 5, 0};
    const auto result = FilletLines(line, circle, 1.0, options, 1.0e-6);
    Require(result.HasValue(), "the fillet is made between a line and a full circle");
    const auto& corner = result.Value().corner;
    RequireNear(Distance(corner.Center(), Vector3{5.9161, 1, 0}), 0.0, 1.0e-3,
        "the fillet centre matches the verified fixture");
    RequireNear(corner.Radius(), 1.0, 1.0e-6, "the requested radius is used");
    RequireNear(Distance(corner.Center(), Vector3{0, 0, 0}), 6.0, 1.0e-6,
        "the centre sits 6mm from the circle's own centre (5mm radius + 1mm fillet)");
    RequireNear(Distance(result.Value().first.StartPoint(), Vector3{10, 0, 0}), 0.0, 1.0e-3,
        "the first piece keeps the hinted-away end");
    RequireNear(Distance(result.Value().first.EndPoint(), Vector3{5.9161, 0, 0}), 0.0, 1.0e-3,
        "the first piece is cut back to the tangent point");
    RequireNear(Distance(result.Value().second.StartPoint(), Vector3{4.9301, 0.8333, 0}), 0.0,
        1.0e-3, "the second piece starts at the tangent point on the circle");
    RequireNear(Distance(result.Value().second.EndPoint(), Vector3{-5, 0, 0}), 0.0, 1.0e-3,
        "the second piece runs to the far side of the circle");
    Require(result.Value().second.Kind() == CurveKind::CircularArc,
        "the closed circle became an open arc once it was cut at the corner");
}

// ---- 円弧×円弧 ----

KACHA_V2_TEST(corner_curves, fillet_and_chamfer_between_two_crossing_arcs)
{
    const auto a3 = Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, kPi);
    const auto a4 = Arc({6, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, kPi);
    CornerOptions options;
    options.firstHint = Vector3{-5, 0.5, 0};
    options.secondHint = Vector3{11, 0.5, 0};

    const auto filleted = FilletLines(a3, a4, 1.0, options, 1.0e-6);
    Require(filleted.HasValue(), "the two crossing arcs fillet");
    const auto& corner = filleted.Value().corner;
    RequireNear(Distance(corner.Center(), Vector3{3, 5.1962, 0}), 0.0, 1.0e-3,
        "the fillet centre matches the verified fixture");
    RequireNear(Distance(corner.Center(), Vector3{0, 0, 0}), 6.0, 1.0e-6,
        "the centre is 6mm from a3's centre (5mm radius + 1mm fillet)");
    RequireNear(Distance(corner.Center(), Vector3{6, 0, 0}), 6.0, 1.0e-6,
        "the centre is 6mm from a4's centre too");
    RequireNear(Distance(filleted.Value().first.StartPoint(), Vector3{-5, 0, 0}), 0.0, 1.0e-3,
        "the first piece keeps a3's hinted-away end");
    RequireNear(Distance(filleted.Value().first.EndPoint(), Vector3{2.5, 4.3301, 0}), 0.0, 1.0e-3,
        "the first piece is cut back to the tangent point on a3");
    RequireNear(Distance(filleted.Value().second.StartPoint(), Vector3{3.5, 4.3301, 0}), 0.0,
        1.0e-3, "the second piece starts at the tangent point on a4");
    RequireNear(Distance(filleted.Value().second.EndPoint(), Vector3{11, 0, 0}), 0.0, 1.0e-3,
        "the second piece keeps a4's hinted-away end");

    const auto chamfered = ChamferLines(a3, a4, 1.0, options, 1.0e-6);
    Require(chamfered.HasValue(), "the two crossing arcs chamfer");
    Require(chamfered.Value().corner.Kind() == CurveKind::Line, "the chamfer corner is straight");
    RequireNear(Distance(chamfered.Value().corner.StartPoint(), Vector3{2.1455, 4.5163, 0}), 0.0,
        1.0e-3, "the chamfer starts where the fixture says");
    RequireNear(Distance(chamfered.Value().corner.EndPoint(), Vector3{3.8545, 4.5163, 0}), 0.0,
        1.0e-3, "the chamfer ends where the fixture says");
    RequireNear(Distance(chamfered.Value().corner.StartPoint(), Vector3{0, 0, 0}), 5.0, 1.0e-6,
        "the chamfer's first end sits on a3's circle");
    RequireNear(Distance(chamfered.Value().corner.EndPoint(), Vector3{6, 0, 0}), 5.0, 1.0e-6,
        "the chamfer's second end sits on a4's circle");
    RequireNear(Distance(chamfered.Value().first.EndPoint(), Vector3{2.1455, 4.5163, 0}), 0.0,
        1.0e-3, "the first piece is cut back to the chamfer");
}

// ---- 直線×円弧: 半径の限界 ----

KACHA_V2_TEST(corner_curves, fillet_line_and_arc_has_a_maximum_radius)
{
    const auto a3 = Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, kPi);
    const auto line = L({-2, 3, 0}, {10, 3, 0});
    CornerOptions options;
    options.firstHint = Vector3{1, 3, 0};
    options.secondHint = Vector3{0, 5, 0};

    const auto small = FilletLines(line, a3, 0.5, options, 1.0e-6);
    Require(small.HasValue(), "a 0.5mm fillet fits");
    RequireNear(Distance(small.Value().corner.Center(), Vector3{2.8284, 3.5, 0}), 0.0, 1.0e-3,
        "the fillet centre matches the verified fixture");
    RequireNear(small.Value().corner.Radius(), 0.5, 1.0e-6, "the requested radius is used");
    RequireNear(Distance(small.Value().first.StartPoint(), Vector3{-2, 3, 0}), 0.0, 1.0e-3,
        "the first piece keeps its far end");
    RequireNear(Distance(small.Value().first.EndPoint(), Vector3{2.8284, 3, 0}), 0.0, 1.0e-3,
        "the first piece is cut back to the tangent point");
    RequireNear(Distance(small.Value().second.StartPoint(), Vector3{3.1427, 3.8889, 0}), 0.0,
        1.0e-3, "the second piece starts at the tangent point on the arc");
    RequireNear(Distance(small.Value().second.EndPoint(), Vector3{-5, 0, 0}), 0.0, 1.0e-3,
        "the second piece keeps the arc's far end");

    // 1.0mm はちょうど収まる限界(重根)で、実装はこれを断る。
    const auto tooBig = FilletLines(line, a3, 1.0, options, 1.0e-6);
    Require(!tooBig.HasValue(), "a 1.0mm fillet is right at the edge and is refused");
}

KACHA_V2_TEST(corner_curves, chamfer_line_and_arc_with_asymmetric_setback)
{
    const auto a3 = Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.0, kPi);
    const auto line = L({0, 3, 0}, {10, 3, 0});
    CornerOptions options;
    options.firstHint = Vector3{1, 3, 0};
    options.secondHint = Vector3{0, 5, 0};
    options.secondSetbackMm = 2.0;

    const auto result = ChamferLines(line, a3, 1.0, options, 1.0e-6);
    Require(result.HasValue(), "the asymmetric chamfer is made");
    RequireNear(Distance(result.Value().corner.StartPoint(), Vector3{3, 3, 0}), 0.0, 1.0e-3,
        "the chamfer starts at the 1mm setback on the line");
    RequireNear(Distance(result.Value().corner.EndPoint(), Vector3{2.5160, 4.3209, 0}), 0.0,
        1.0e-3, "the chamfer ends at the 2mm (arc-length) setback on the arc");
    RequireNear(Distance(result.Value().first.StartPoint(), Vector3{0, 3, 0}), 0.0, 1.0e-3,
        "the first piece keeps its far end");
    RequireNear(Distance(result.Value().second.EndPoint(), Vector3{-5, 0, 0}), 0.0, 1.0e-3,
        "the second piece keeps the arc's far end");
    RequireNear(Distance(result.Value().corner.EndPoint(), Vector3{0, 0, 0}), 5.0, 1.0e-6,
        "the chamfer's arc-side end sits on the circle");
    // 弧長での切戻し: (4,3) から (2.5160,4.3209) までの弧長が 2.0mm になっている。
    const double angleAtFour = std::atan2(3.0, 4.0);
    const double angleAtCut = std::atan2(4.3209, 2.5160);
    RequireNear(std::abs(angleAtCut - angleAtFour) * 5.0, 2.0, 1.0e-3,
        "the setback along the arc is 2mm of arc length, not a straight-line distance");
}

// ---- 端で触れているだけの円弧は伸ばしてから探す ----

KACHA_V2_TEST(corner_curves, fillet_extends_an_arc_to_find_the_corner)
{
    const auto a5 = Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, 0.3, 1.0);
    const auto line = L({0, 0, 0}, {10, 0, 0});
    const auto result = FilletLines(line, a5, 1.0, 1.0e-6);
    Require(result.HasValue(), "the fillet is made even though the arc must be extended");
    RequireNear(Distance(result.Value().corner.Center(), Vector3{5.9161, 1, 0}), 0.0, 1.0e-3,
        "the fillet centre matches the verified fixture");
    RequireNear(Distance(result.Value().first.StartPoint(), Vector3{10, 0, 0}), 0.0, 1.0e-3,
        "the first piece keeps its far end");
    RequireNear(Distance(result.Value().first.EndPoint(), Vector3{5.9161, 0, 0}), 0.0, 1.0e-3,
        "the first piece is cut back to the tangent point");
    RequireNear(Distance(result.Value().second.StartPoint(), Vector3{4.9301, 0.8333, 0}), 0.0,
        1.0e-3, "the second piece starts at the tangent point on the extended arc");
    RequireNear(Distance(result.Value().second.EndPoint(), Vector3{1.3375, 4.8178, 0}), 0.0,
        1.0e-3, "the second piece keeps the arc's own (unextended) far end");
}

// ---- 直線×ベジェ ----

KACHA_V2_TEST(corner_curves, fillet_line_and_bezier)
{
    const auto bezier = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {3, 4, 0}, {6, 4, 0}, {10, 0, 0}}).Value();
    const auto line = L({5, -5, 0}, {5, 5, 0});
    CornerOptions options;
    options.firstHint = Vector3{5, -2, 0};
    options.secondHint = Vector3{0, 0, 0};
    const auto result = FilletLines(line, bezier, 1.0, options, 1.0e-6);
    Require(result.HasValue(), "the fillet is made between a line and a bezier");
    const auto& corner = result.Value().corner;
    RequireNear(Distance(corner.Center(), Vector3{4, 1.9320, 0}), 0.0, 1.0e-3,
        "the fillet centre matches the verified fixture");
    RequireNear(corner.Radius(), 1.0, 1.0e-6, "the requested radius is used");
    RequireNear(Distance(result.Value().first.StartPoint(), Vector3{5, -5, 0}), 0.0, 1.0e-3,
        "the first piece keeps its far end");
    RequireNear(Distance(result.Value().first.EndPoint(), Vector3{5, 1.9320, 0}), 0.0, 1.0e-3,
        "the first piece is cut back to the tangent point");
    RequireNear(Distance(result.Value().second.StartPoint(), Vector3{3.7850, 2.9086, 0}), 0.0,
        1.0e-3, "the second piece starts at the tangent point on the bezier");
    RequireNear(Distance(result.Value().second.EndPoint(), Vector3{0, 0, 0}), 0.0, 1.0e-3,
        "the second piece keeps the bezier's far end");
    RequireNear(bezier.ClosestPoint(corner.Center()).distance, 1.0, 1.0e-6,
        "the centre is exactly radius away from the bezier");
}

// ---- 断る場合 ----

KACHA_V2_TEST(corner_curves, tangent_line_and_arc_are_refused)
{
    // 線 (0,0)-(10,0) と円弧(中心(10,5)、半径5、始点(10,0))は、(10,0)で同じ接線(1,0)を持つ。
    const auto line = L({0, 0, 0}, {10, 0, 0});
    const auto arc = Arc({10, 5, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, -kPi / 2.0, kPi / 2.0);

    const auto filleted = FilletLines(line, arc, 1.0, 1.0e-6);
    Require(!filleted.HasValue(), "a fillet cannot pick a side when the tangents coincide");
    RequireEqual(filleted.Diagnostics().front().code, "GEO-E004",
        "the refusal names the degenerate corner");

    const auto chamfered = ChamferLines(line, arc, 1.0, 1.0e-6);
    Require(!chamfered.HasValue(), "a chamfer cannot pick a side when the tangents coincide");
    RequireEqual(chamfered.Diagnostics().front().code, "GEO-E004",
        "the refusal names the degenerate corner");
}

KACHA_V2_TEST(corner_curves, bspline_is_refused_as_not_supported)
{
    const auto bspline = CurveSegment::MakeCubicBSpline(
        {{0, 0, 0}, {3, 4, 0}, {6, 4, 0}, {10, 0, 0}}).Value();
    const auto line = L({5, -5, 0}, {5, 5, 0});
    const auto result = FilletLines(line, bspline, 1.0, 1.0e-6);
    Require(!result.HasValue(), "a b-spline cannot be split mid-curve, so the corner is refused");
    RequireEqual(result.Diagnostics().front().code, "GEO-E003",
        "the refusal says the capability is missing");
}

// ---- 直線どうし(既存の解析解の経路)も CornerCurves 経由の欄で確かめる ----

KACHA_V2_TEST(corner_curves, fillet_line_and_line_with_hints_still_uses_the_analytic_path)
{
    const auto first = L({-5, 0, 0}, {5, 0, 0});
    const auto second = L({0, -5, 0}, {0, 5, 0});
    CornerOptions options;
    options.firstHint = Vector3{2, 0, 0};
    options.secondHint = Vector3{0, 2, 0};
    const auto result = FilletLines(first, second, 1.0, options, 1.0e-6);
    Require(result.HasValue(), "two lines with hints still fillet");
    RequireNear(Distance(result.Value().corner.Center(), Vector3{1, 1, 0}), 0.0, 1.0e-6,
        "the fillet centre matches the verified fixture");
    RequireNear(Distance(result.Value().first.StartPoint(), Vector3{5, 0, 0}), 0.0, 1.0e-6,
        "the first piece keeps the hinted-away end");
    RequireNear(Distance(result.Value().first.EndPoint(), Vector3{1, 0, 0}), 0.0, 1.0e-6,
        "the first piece is cut back to the tangent point");
    RequireNear(Distance(result.Value().second.StartPoint(), Vector3{0, 1, 0}), 0.0, 1.0e-6,
        "the second piece starts at the tangent point");
    RequireNear(Distance(result.Value().second.EndPoint(), Vector3{0, 5, 0}), 0.0, 1.0e-6,
        "the second piece keeps the hinted-away end");

    // 欄無し(押した点が無い)でも、既定の「遠い端を残す」で作れる(退行がないことの確認)。
    const auto withoutHints = FilletLines(first, second, 1.0, 1.0e-6);
    Require(withoutHints.HasValue(), "two lines without hints still fillet by default");
}

KACHA_V2_TEST(corner_curves, a_non_positive_size_is_refused)
{
    const auto line = L({10, -5, 0}, {10, 0, 0});
    const auto arc = Arc({10, 5, 0}, {0, 0, 1}, {1, 0, 0}, 5.0, -kPi / 2.0, kPi / 2.0);
    const auto result = CornerBetweenCurves(line, arc, CornerKind::Fillet, 0.0, CornerOptions{},
        1.0e-6);
    Require(!result.HasValue(), "a zero radius is refused");
    RequireEqual(result.Diagnostics().front().code, "GEO-E004",
        "the refusal names the degenerate size");
}

KACHA_V2_TEST_MAIN("corner_curves_tests")
