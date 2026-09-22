// 「線から面」(app/LoopFaces)と、その土台の輪グラフ(app/LoopGraph)の検証。
//
// ここは仕様書(loop-faces)を丸写しにしない。実際に PlanLoopFaces / LoopFaceTable /
// CloseLoopGap を動かして得た値を確かめる。数が仕様と違えば、弱めずにその値を報告する。
#include "kachakacha/app/LoopFaces.h"

#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/base/Uuid.h"
#include "kachakacha/geometry/Units.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using kachakacha::v2::app::CloseLoopGap;
using kachakacha::v2::app::LoopFace;
using kachakacha::v2::app::LoopFaceMethod;
using kachakacha::v2::app::LoopFaceMethodLabelJa;
using kachakacha::v2::app::LoopFacePlan;
using kachakacha::v2::app::LoopFaceTable;
using kachakacha::v2::app::LoopGap;
using kachakacha::v2::app::LoopGapTextJa;
using kachakacha::v2::app::PlanLoopFaces;
using kachakacha::v2::app::MakeLoopEdge;
using kachakacha::v2::app::LoopEdge;
using kachakacha::v2::app::LoopGraph;
using kachakacha::v2::app::FindLoopCycles;
using kachakacha::v2::app::PruneDeadEnds;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::Uuid;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Distance;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideTable;
using kachakacha::v2::modeling::GuideTableSelection;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

//! 選択ごとに違う元ワイヤーidが要る(UI-R006)。番号をそのままバイト列にする。
[[nodiscard]] EntityId WireId(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = number;
    return EntityId(Uuid(bytes));
}

[[nodiscard]] CurveSegment L(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "fixture line is valid");
    return made.Value();
}

[[nodiscard]] CurveSegment Arc(Vector3 center, Vector3 normal, Vector3 reference, double radius,
    double startRad, double sweepRad)
{
    const auto made =
        CurveSegment::MakeCircularArc(center, normal, reference, radius, startRad, sweepRad);
    Require(made.HasValue(), "fixture arc is valid");
    return made.Value();
}

[[nodiscard]] GuideTableSelection Sel(std::uint8_t id, std::string label, CurveSegment segment)
{
    GuideTableSelection selection;
    selection.sourceWireId = WireId(id);
    selection.label = std::move(label);
    selection.segments = {std::move(segment)};
    return selection;
}

[[nodiscard]] GeometryTolerance MakeTolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1e-4;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

//! 表の1行の並びが、隣どうしの端点でつながる1周の鎖になっているか。
void RequireClosedChain(const std::vector<CurveSegment>& segments, std::string_view label)
{
    Require(!segments.empty(), std::string(label) + ": row has segments");
    for (std::size_t at = 0; at + 1 < segments.size(); ++at) {
        const double gap = Distance(segments[at].EndPoint(), segments[at + 1].StartPoint());
        Require(gap <= 0.01, std::string(label) + ": consecutive segments touch");
    }
    const double wrap = Distance(segments.back().EndPoint(), segments.front().StartPoint());
    Require(wrap <= 0.01, std::string(label) + ": last segment returns to first");
}

//! ある選択番号が、面の一覧の中で何回使われているか(表裏の判定に使う)。
[[nodiscard]] std::map<std::size_t, int> CountUsage(const std::vector<LoopFace>& faces)
{
    std::map<std::size_t, int> counts;
    for (const LoopFace& face : faces) {
        for (const std::size_t selection : face.selections) {
            ++counts[selection];
        }
    }
    return counts;
}

} // namespace

// ---- 1. 立方体のワイヤーフレーム: 平面6枚 ----

KACHA_V2_TEST(loop_faces, cube_wireframe_gives_six_planar_faces)
{
    const Vector3 c000{0, 0, 0};
    const Vector3 c100{1, 0, 0};
    const Vector3 c110{1, 1, 0};
    const Vector3 c010{0, 1, 0};
    const Vector3 c001{0, 0, 1};
    const Vector3 c101{1, 0, 1};
    const Vector3 c111{1, 1, 1};
    const Vector3 c011{0, 1, 1};

    std::vector<GuideTableSelection> selections;
    std::uint8_t id = 0;
    const auto add = [&](Vector3 a, Vector3 b) {
        selections.push_back(Sel(id, "e" + std::to_string(id), L(a, b)));
        ++id;
    };
    // 底面
    add(c000, c100);
    add(c100, c110);
    add(c110, c010);
    add(c010, c000);
    // 天面
    add(c001, c101);
    add(c101, c111);
    add(c111, c011);
    add(c011, c001);
    // 縦
    add(c000, c001);
    add(c100, c101);
    add(c110, c111);
    add(c010, c011);
    Require(selections.size() == 12, "twelve cube edges");

    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(plan.HasValue(), "cube wireframe plans");
    const LoopFacePlan& value = plan.Value();
    RequireEqual(std::to_string(value.faces.size()), "6", "six faces on a cube");
    for (const LoopFace& face : value.faces) {
        Require(face.method == LoopFaceMethod::Planar, "cube face is planar");
        Require(face.selections.size() == 4, "cube face has four edges");
    }
    Require(value.gaps.empty(), "no gaps on a closed cube");
    Require(value.unused.empty(), "no unused edges on a closed cube");
    RequireEqual(value.summaryJa, "平面 6・四辺面 0・境界面 0", "cube summary line");

    const std::map<std::size_t, int> usage = CountUsage(value.faces);
    Require(usage.size() == 12, "all twelve edges used");
    for (const auto& [selection, count] : usage) {
        Require(count == 2, "each cube edge borders exactly two faces");
    }
}

// ---- 2. 四面体: 三角の平面4枚、辺は表裏2枚ずつ ----

KACHA_V2_TEST(loop_faces, tetrahedron_gives_four_triangular_faces)
{
    const Vector3 p0{0, 0, 0};
    const Vector3 p1{2, 0, 0};
    const Vector3 p2{1, 2, 0};
    const Vector3 p3{1, 1, 2};

    std::vector<GuideTableSelection> selections;
    std::uint8_t id = 0;
    const auto add = [&](Vector3 a, Vector3 b) {
        selections.push_back(Sel(id, "e" + std::to_string(id), L(a, b)));
        ++id;
    };
    add(p0, p1);
    add(p0, p2);
    add(p0, p3);
    add(p1, p2);
    add(p1, p3);
    add(p2, p3);

    const GeometryTolerance tolerance = MakeTolerance();
    const auto plan = PlanLoopFaces(selections, tolerance);
    Require(plan.HasValue(), "tetrahedron plans");
    const LoopFacePlan& value = plan.Value();
    RequireEqual(std::to_string(value.faces.size()), "4", "four triangular faces");
    for (const LoopFace& face : value.faces) {
        Require(face.method == LoopFaceMethod::Planar, "tetrahedron face is planar");
        Require(face.selections.size() == 3, "tetrahedron face has three edges");
    }
    const std::map<std::size_t, int> usage = CountUsage(value.faces);
    Require(usage.size() == 6, "all six edges used");
    for (const auto& [selection, count] : usage) {
        Require(count == 2, "each tetrahedron edge borders exactly two faces");
    }

    for (const LoopFace& face : value.faces) {
        const auto table = LoopFaceTable(selections, face, tolerance);
        Require(table.HasValue(), "tetrahedron face makes a table");
        const GuideTable& built = table.Value();
        Require(built.method == GuideSurfaceMethod::PlanarBoundary, "planar table method");
        Require(built.rows.size() == 1, "planar table has one row");
        RequireClosedChain(built.rows.front().segments, "tetrahedron face");
    }
}

// ---- 3. 対角線つきの正方形: 4辺の輪は弦持ちで外れ、三角2枚になる ----

KACHA_V2_TEST(loop_faces, square_with_diagonal_gives_two_triangles_not_the_square)
{
    const Vector3 a{0, 0, 0};
    const Vector3 b{2, 0, 0};
    const Vector3 c{2, 2, 0};
    const Vector3 d{0, 2, 0};

    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "ab", L(a, b)));
    selections.push_back(Sel(1, "bc", L(b, c)));
    selections.push_back(Sel(2, "cd", L(c, d)));
    selections.push_back(Sel(3, "da", L(d, a)));
    selections.push_back(Sel(4, "ac", L(a, c)));   // 対角線

    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(plan.HasValue(), "square with diagonal plans");
    const LoopFacePlan& value = plan.Value();
    RequireEqual(std::to_string(value.faces.size()), "2", "two triangular faces, not the square");
    for (const LoopFace& face : value.faces) {
        Require(face.method == LoopFaceMethod::Planar, "triangle face is planar");
        Require(face.selections.size() == 3, "triangle face has three edges");
    }
    Require(value.unused.empty(), "every line is used by some triangle");
    const std::map<std::size_t, int> usage = CountUsage(value.faces);
    Require(usage.at(4) == 2, "the diagonal borders both triangles");
}

// ---- 4. 鞍形の四辺: 平らでないので四辺面(Coons) ----

KACHA_V2_TEST(loop_faces, saddle_quad_gives_four_edge_patch)
{
    const Vector3 a{0, 0, 0};
    const Vector3 b{2, 0, 1};
    const Vector3 c{2, 2, 0};
    const Vector3 d{0, 2, 1};

    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "ab", L(a, b)));
    selections.push_back(Sel(1, "bc", L(b, c)));
    selections.push_back(Sel(2, "cd", L(c, d)));
    selections.push_back(Sel(3, "da", L(d, a)));

    const GeometryTolerance tolerance = MakeTolerance();
    const auto plan = PlanLoopFaces(selections, tolerance);
    Require(plan.HasValue(), "saddle quad plans");
    const LoopFacePlan& value = plan.Value();
    RequireEqual(std::to_string(value.faces.size()), "1", "one face for the saddle quad");
    const LoopFace& face = value.faces.front();
    Require(face.method == LoopFaceMethod::FourEdge, "saddle quad is a four-edge patch");
    RequireNear(face.planeDeviationMm, 0.5, 1e-6, "saddle plane deviation");

    const auto table = LoopFaceTable(selections, face, tolerance);
    Require(table.HasValue(), "saddle quad makes a table");
    const GuideTable& built = table.Value();
    Require(built.method == GuideSurfaceMethod::FourEdgePatch, "four-edge patch method");
    Require(built.rows.size() == 4, "four rows, one per edge");
    for (const auto& row : built.rows) {
        Require(row.role == ChainRole::BoundarySide, "four-edge row role is BoundarySide");
    }
}

// ---- 5. 一頂点が持ち上がった五角形: 境界面(近似) ----

KACHA_V2_TEST(loop_faces, lifted_pentagon_gives_boundary_fill)
{
    const Vector3 a{0, 0, 0};
    const Vector3 b{2, 0, 0};
    const Vector3 c{3, 2, 0};
    const Vector3 d{1, 3, 1};   // 持ち上げた頂点
    const Vector3 e{-1, 2, 0};

    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "ab", L(a, b)));
    selections.push_back(Sel(1, "bc", L(b, c)));
    selections.push_back(Sel(2, "cd", L(c, d)));
    selections.push_back(Sel(3, "de", L(d, e)));
    selections.push_back(Sel(4, "ea", L(e, a)));

    const GeometryTolerance tolerance = MakeTolerance();
    const auto plan = PlanLoopFaces(selections, tolerance);
    Require(plan.HasValue(), "lifted pentagon plans");
    const LoopFacePlan& value = plan.Value();
    RequireEqual(std::to_string(value.faces.size()), "1", "one face for the pentagon");
    const LoopFace& face = value.faces.front();
    Require(face.method == LoopFaceMethod::BoundaryFill, "non-planar 5-edge loop is boundary fill");
    RequireEqual(LoopFaceMethodLabelJa(LoopFaceMethod::BoundaryFill), "境界面(近似)",
        "boundary fill label");

    const auto table = LoopFaceTable(selections, face, tolerance);
    Require(table.HasValue(), "pentagon makes a table");
    const GuideTable& built = table.Value();
    Require(built.method == GuideSurfaceMethod::BoundaryFill, "boundary fill method");
    Require(built.rows.size() == 5, "five rows, one per edge");
    for (const auto& row : built.rows) {
        Require(row.role == ChainRole::BoundarySide, "boundary fill row role is BoundarySide");
    }
}

// ---- 6. 半月(直線+半円): 2辺の輪も面になる ----

KACHA_V2_TEST(loop_faces, half_moon_two_edge_cycle_is_a_face)
{
    const Vector3 left{-5, 0, 0};
    const Vector3 right{5, 0, 0};
    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "chord", L(left, right)));
    selections.push_back(
        Sel(1, "arc", Arc(Vector3{0, 0, 0}, Vector3{0, 0, 1}, Vector3{1, 0, 0}, 5.0, 0.0, kPi)));

    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(plan.HasValue(), "half moon plans");
    const LoopFacePlan& value = plan.Value();
    RequireEqual(std::to_string(value.faces.size()), "1", "one face for the half moon");
    const LoopFace& face = value.faces.front();
    Require(face.method == LoopFaceMethod::Planar, "half moon face is planar");
    Require(face.selections.size() == 2, "half moon face has two selections");
}

// ---- 7. ずれ(直線どうし): 見つけて、寄せて、面になる ----

KACHA_V2_TEST(loop_faces, gap_between_two_lines_is_found_and_closed)
{
    const Vector3 p0{0, 0, 0};
    const Vector3 p1{2, 0, 0};
    const Vector3 p2{1, 2, 0};
    const Vector3 p2b{1, 2, 0.3};   // rの始点は0.3mm離れている

    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "p", L(p0, p1)));
    selections.push_back(Sel(1, "q", L(p1, p2)));
    selections.push_back(Sel(2, "r", L(p2b, p0)));
    constexpr std::size_t qIndex = 1;
    constexpr std::size_t rIndex = 2;

    const GeometryTolerance tolerance = MakeTolerance();
    const auto plan = PlanLoopFaces(selections, tolerance);
    Require(plan.HasValue(), "gap plan succeeds because a gap was found");
    const LoopFacePlan& value = plan.Value();
    Require(value.faces.empty(), "no closed loop yet");
    Require(value.gaps.size() == 1, "exactly one gap");
    Require(value.unused.size() == 3, "all three lines are unused (no face yet)");

    const LoopGap& gap = value.gaps.front();
    const bool firstIsQ = gap.firstSelection == qIndex;
    const bool firstIsR = gap.firstSelection == rIndex;
    Require(firstIsQ || firstIsR, "gap's first side is q or r");
    const bool matchedPair = (firstIsQ && gap.secondSelection == rIndex)
        || (firstIsR && gap.secondSelection == qIndex);
    Require(matchedPair, "gap is exactly between q and r");
    // qのその端はatEnd、rのその端はatEndでない側。
    const bool qAtEnd = firstIsQ ? gap.firstAtEnd : gap.secondAtEnd;
    const bool rAtEnd = firstIsQ ? gap.secondAtEnd : gap.firstAtEnd;
    Require(qAtEnd, "q's far end is its end point");
    Require(!rAtEnd, "r's far end is its start point");
    RequireNear(gap.distanceMm, 0.3, 1e-9, "gap distance is 0.3mm");
    Require(gap.movable, "both sides are straight lines, so it is movable");

    const std::string text = LoopGapTextJa(selections, gap);
    Require(text.find("0.300 mm") != std::string::npos, "gap text mentions 0.300 mm");

    const auto fix = CloseLoopGap(selections, gap);
    Require(fix.HasValue(), "closing the gap succeeds");
    const auto& fixed = fix.Value();
    RequireNear(fixed.movedMm, 0.15, 1e-9, "both lines move half the gap");
    Require(fixed.first.has_value(), "first line moved");
    Require(fixed.second.has_value(), "second line moved");
    const Vector3 midpoint{1.0, 2.0, 0.15};
    const Vector3 firstPoint =
        gap.firstAtEnd ? fixed.first->EndPoint() : fixed.first->StartPoint();
    const Vector3 secondPoint =
        gap.secondAtEnd ? fixed.second->EndPoint() : fixed.second->StartPoint();
    RequireNear(Distance(firstPoint, midpoint), 0.0, 1e-9, "first line moved to the midpoint");
    RequireNear(Distance(secondPoint, midpoint), 0.0, 1e-9, "second line moved to the midpoint");

    // 直した線を入れ直して、面になることを確かめる。
    std::vector<GuideTableSelection> fixedSelections = selections;
    fixedSelections[gap.firstSelection].segments = {fixed.first.value()};
    fixedSelections[gap.secondSelection].segments = {fixed.second.value()};
    const auto replanned = PlanLoopFaces(fixedSelections, tolerance);
    Require(replanned.HasValue(), "replanning after the fix succeeds");
    const LoopFacePlan& replan = replanned.Value();
    RequireEqual(std::to_string(replan.faces.size()), "1", "one face after closing the gap");
    Require(replan.faces.front().method == LoopFaceMethod::Planar, "closed loop is planar");
    Require(replan.gaps.empty(), "no gaps remain after the fix");
}

// ---- 8. ずれ(片方が円弧): 直線だけが動く ----

KACHA_V2_TEST(loop_faces, gap_between_arc_and_line_moves_only_the_line)
{
    const CurveSegment arc =
        Arc(Vector3{0, 0, 0}, Vector3{0, 0, 1}, Vector3{1, 0, 0}, 5.0, 0.0, kPi / 2.0);
    RequireNear(Distance(arc.StartPoint(), Vector3{5, 0, 0}), 0.0, 1e-9, "arc starts at (5,0,0)");
    RequireNear(Distance(arc.EndPoint(), Vector3{0, 5, 0}), 0.0, 1e-9, "arc ends at (0,5,0)");
    const CurveSegment line = L(Vector3{0, 5.2, 0}, Vector3{5, 0, 0});

    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "arc", arc));
    selections.push_back(Sel(1, "line", line));
    constexpr std::size_t arcIndex = 0;
    constexpr std::size_t lineIndex = 1;

    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(plan.HasValue(), "arc/line gap plan succeeds");
    const LoopFacePlan& value = plan.Value();
    Require(value.faces.empty(), "no loop closes (both edges are dead ends)");
    Require(value.gaps.size() == 1, "exactly one gap");
    const LoopGap& gap = value.gaps.front();
    const bool firstIsArc = gap.firstSelection == arcIndex;
    const bool matchedPair = (firstIsArc && gap.secondSelection == lineIndex)
        || (gap.firstSelection == lineIndex && gap.secondSelection == arcIndex);
    Require(matchedPair, "gap is between the arc and the line");
    RequireNear(gap.distanceMm, 0.2, 1e-9, "gap distance is 0.2mm");
    Require(gap.movable, "the line side makes it movable");

    const auto fix = CloseLoopGap(selections, gap);
    Require(fix.HasValue(), "closing the arc/line gap succeeds");
    const auto& fixed = fix.Value();
    RequireNear(fixed.movedMm, 0.2, 1e-9, "the whole gap is closed by moving the line");
    if (firstIsArc) {
        Require(!fixed.first.has_value(), "the arc side is not moved");
        Require(fixed.second.has_value(), "the line side is moved");
        const Vector3 point =
            gap.secondAtEnd ? fixed.second->EndPoint() : fixed.second->StartPoint();
        RequireNear(Distance(point, Vector3{0, 5, 0}), 0.0, 1e-9, "line moved to the arc's end");
    } else {
        Require(!fixed.second.has_value(), "the arc side is not moved");
        Require(fixed.first.has_value(), "the line side is moved");
        const Vector3 point = gap.firstAtEnd ? fixed.first->EndPoint() : fixed.first->StartPoint();
        RequireNear(Distance(point, Vector3{0, 5, 0}), 0.0, 1e-9, "line moved to the arc's end");
    }
}

// ---- 9. ずれ(両方が円弧): 寄せられない ----

KACHA_V2_TEST(loop_faces, gap_between_two_arcs_cannot_be_closed)
{
    const CurveSegment arc1 =
        Arc(Vector3{0, 0, 0}, Vector3{0, 0, 1}, Vector3{1, 0, 0}, 1.0, 0.0, kPi / 2.0);
    RequireNear(Distance(arc1.EndPoint(), Vector3{0, 1, 0}), 0.0, 1e-9, "arc1 ends at (0,1,0)");
    // arc2の始点を、arc1の終点から0.2mmだけ離す。arc2のもう一方の端は遠くへ逃がす。
    const CurveSegment arc2 =
        Arc(Vector3{-1, 1.2, 0}, Vector3{0, 0, 1}, Vector3{1, 0, 0}, 1.0, 0.0, kPi / 2.0);
    RequireNear(Distance(arc2.StartPoint(), Vector3{0, 1.2, 0}), 0.0, 1e-9,
        "arc2 starts at (0,1.2,0)");

    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "arc1", arc1));
    selections.push_back(Sel(1, "arc2", arc2));

    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(plan.HasValue(), "two-arc gap plan succeeds");
    const LoopFacePlan& value = plan.Value();
    Require(value.faces.empty(), "no loop closes");
    Require(value.gaps.size() == 1, "exactly one gap");
    const LoopGap& gap = value.gaps.front();
    RequireNear(gap.distanceMm, 0.2, 1e-9, "gap distance is 0.2mm");
    Require(!gap.movable, "neither side is a straight line");

    const auto fix = CloseLoopGap(selections, gap);
    Require(!fix.HasValue(), "closing an arc/arc gap is refused");
    RequireEqual(fix.Diagnostics().front().code, "GEO-E011", "refusal code is GEO-E011");

    const std::string text = LoopGapTextJa(selections, gap);
    Require(text.find("寄せられません") != std::string::npos, "gap text says it cannot be closed");
}

// ---- 10. 断りの入り口: 空・輪もずれも無い・線が多すぎる ----

KACHA_V2_TEST(loop_faces, empty_selection_is_refused)
{
    const std::vector<GuideTableSelection> selections;
    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(!plan.HasValue(), "empty selection is refused");
    RequireEqual(plan.Diagnostics().front().code, "UI-R004", "no-input code is UI-R004");
}

KACHA_V2_TEST(loop_faces, two_unrelated_lines_have_no_loop_and_no_gap)
{
    std::vector<GuideTableSelection> selections;
    selections.push_back(Sel(0, "far1", L(Vector3{0, 0, 0}, Vector3{1, 0, 0})));
    selections.push_back(Sel(1, "far2", L(Vector3{100, 100, 0}, Vector3{101, 100, 0})));
    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(!plan.HasValue(), "two unrelated lines are refused");
    RequireEqual(plan.Diagnostics().front().code, "UI-R011", "no-loop code is UI-R011");
}

KACHA_V2_TEST(loop_faces, too_many_disconnected_lines_are_refused)
{
    std::vector<GuideTableSelection> selections;
    for (int index = 0; index < 25; ++index) {
        const double base = static_cast<double>(index) * 100.0;
        selections.push_back(Sel(static_cast<std::uint8_t>(index), "l" + std::to_string(index),
            L(Vector3{base, 0, 0}, Vector3{base + 1.0, 0, 0})));
    }
    Require(selections.size() == 25, "twenty-five lines");
    const auto plan = PlanLoopFaces(selections, MakeTolerance());
    Require(!plan.HasValue(), "too many lines are refused");
    RequireEqual(plan.Diagnostics().front().code, "UI-R012", "too-many code is UI-R012");
}

// ---- 11. LoopGraph そのもの ----

KACHA_V2_TEST(loop_graph, node_of_merges_within_tolerance_and_counts_distinct)
{
    LoopGraph graph(0.01);
    const std::size_t a = graph.NodeOf(Vector3{0, 0, 0});
    const std::size_t aAgain = graph.NodeOf(Vector3{0, 0, 0.005});   // 許容差の内側
    const std::size_t b = graph.NodeOf(Vector3{0, 0, 1});            // 許容差の外側
    Require(a == aAgain, "points within tolerance share a node");
    Require(a != b, "points outside tolerance get different nodes");
    Require(graph.NodeCount() == 2, "two distinct nodes were created");
}

KACHA_V2_TEST(loop_graph, find_loop_cycles_finds_the_triangle)
{
    LoopGraph graph(0.01);
    const Vector3 a{0, 0, 0};
    const Vector3 b{2, 0, 0};
    const Vector3 c{1, 2, 0};
    std::vector<LoopEdge> edges;
    edges.push_back(MakeLoopEdge(0, {L(a, b)}, graph));
    edges.push_back(MakeLoopEdge(1, {L(b, c)}, graph));
    edges.push_back(MakeLoopEdge(2, {L(c, a)}, graph));

    const auto cycles = FindLoopCycles(edges);
    Require(!cycles.empty(), "at least one cycle is found");
    const bool hasTriangle = std::any_of(cycles.begin(), cycles.end(),
        [](const auto& cycle) { return cycle.edges.size() == 3; });
    Require(hasTriangle, "one of the cycles has exactly three edges");
}

KACHA_V2_TEST(loop_graph, prune_dead_ends_marks_only_the_dangling_edge)
{
    LoopGraph graph(0.01);
    const Vector3 a{0, 0, 0};
    const Vector3 b{2, 0, 0};
    const Vector3 c{1, 2, 0};
    const Vector3 dangling{1, 5, 0};
    std::vector<LoopEdge> edges;
    edges.push_back(MakeLoopEdge(0, {L(a, b)}, graph));
    edges.push_back(MakeLoopEdge(1, {L(b, c)}, graph));
    edges.push_back(MakeLoopEdge(2, {L(c, a)}, graph));
    edges.push_back(MakeLoopEdge(3, {L(c, dangling)}, graph));   // 行き止まり

    PruneDeadEnds(edges, graph.NodeCount());
    Require(edges[0].alive, "triangle edge 0 survives");
    Require(edges[1].alive, "triangle edge 1 survives");
    Require(edges[2].alive, "triangle edge 2 survives");
    Require(!edges[3].alive, "the dangling edge is pruned");
}

KACHA_V2_TEST_MAIN("loop_faces_tests")
