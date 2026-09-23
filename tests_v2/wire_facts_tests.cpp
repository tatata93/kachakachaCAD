//! 線の事実の行(app/WireFacts)。
//! 載る平面・長さ・端のつながり(重なる / T 字 / ずれ / なし)を、線を選んだ時点で言えるか。

#include "kachakacha/app/WireFacts.h"

#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/base/Uuid.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using kachakacha::v2::app::DescribeWire;
using kachakacha::v2::app::WireEndRelation;
using kachakacha::v2::app::WireFacts;
using kachakacha::v2::app::WireFactsTextJa;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::Uuid;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::GuideTableSelection;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

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

[[nodiscard]] GuideTableSelection Sel(std::uint8_t id, std::string label, CurveSegment segment)
{
    GuideTableSelection selection;
    selection.sourceWireId = WireId(id);
    selection.label = std::move(label);
    selection.segments = {std::move(segment)};
    return selection;
}

[[nodiscard]] GeometryTolerance Tol()
{
    GeometryTolerance tolerance;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[nodiscard]] bool Contains(const std::string& text, const std::string& piece)
{
    return text.find(piece) != std::string::npos;
}

} // namespace

KACHA_V2_TEST(wire_facts, a_triangle_side_reports_plane_length_and_both_joined_ends)
{
    std::vector<GuideTableSelection> wires;
    wires.push_back(Sel(1, "直線 1", L({0, 0, 0}, {40, 0, 0})));
    wires.push_back(Sel(2, "直線 2", L({40, 0, 0}, {20, 30, 0})));
    wires.push_back(Sel(3, "直線 3", L({20, 30, 0}, {0, 0, 0})));
    const WireFacts facts = DescribeWire(wires, 1, Tol());
    RequireEqual(facts.plane.nameJa, std::string("上面 XY"), "a line at z = 0 lies on the top plane");
    RequireNear(facts.lengthMm, std::sqrt(20.0 * 20.0 + 30.0 * 30.0), 1.0e-6, "length is reported");
    Require(!facts.closed, "an open line is not closed");
    Require(facts.start.relation == WireEndRelation::Joined, "start meets line 1's end");
    Require(facts.start.other == 0 && facts.start.otherAtEnd, "start meets the END of line 1");
    Require(facts.end.relation == WireEndRelation::Joined, "end meets line 3's start");
    Require(facts.end.other == 2 && !facts.end.otherAtEnd, "end meets the START of line 3");
    const std::string text = WireFactsTextJa(wires, facts);
    Require(Contains(text, "載る面: 上面 XY"), "text names the plane: " + text);
    Require(Contains(text, "始点 → 直線 1 の終点(0.000 mm)"), "text names the start partner: " + text);
    Require(Contains(text, "終点 → 直線 3 の始点(0.000 mm)"), "text names the end partner: " + text);
}

KACHA_V2_TEST(wire_facts, a_gap_is_reported_with_its_distance_and_can_be_closed)
{
    std::vector<GuideTableSelection> wires;
    wires.push_back(Sel(1, "直線 1", L({0, 0, 0}, {40, 0, 0})));
    wires.push_back(Sel(2, "直線 2", L({40, 0, 0}, {20, 30, 0})));
    wires.push_back(Sel(3, "直線 3", L({20, 33, 0}, {0, 0, 0})));   // 3 mm 離れている
    const WireFacts facts = DescribeWire(wires, 2, Tol());
    Require(facts.start.relation == WireEndRelation::Near, "the start is near but not joined");
    RequireNear(facts.start.distanceMm, 3.0, 1.0e-9, "the gap distance is 3 mm");
    Require(facts.start.other == 1 && facts.start.otherAtEnd, "the partner is the END of line 2");
    Require(facts.start.movable, "a straight line's end can be moved");
    RequireNear(facts.start.target.y, 30.0, 1.0e-9, "closing moves the end to the partner's end");
    Require(facts.end.relation == WireEndRelation::Joined, "the other end is joined");
    const std::string text = WireFactsTextJa(wires, facts);
    Require(Contains(text, "始点 → 直線 2 の終点まで 3.000 mm 離れています"), "text says the gap: " + text);
}

KACHA_V2_TEST(wire_facts, an_end_on_the_middle_of_another_line_is_a_t_junction)
{
    std::vector<GuideTableSelection> wires;
    wires.push_back(Sel(1, "直線 1", L({0, 0, 0}, {40, 0, 0})));
    wires.push_back(Sel(2, "直線 2", L({20, 0, 0}, {20, 30, 0})));
    const WireFacts facts = DescribeWire(wires, 1, Tol());
    Require(facts.start.relation == WireEndRelation::OnMiddle, "start sits on the middle of line 1");
    Require(facts.start.other == 0, "the T-junction partner is line 1");
    Require(facts.end.relation == WireEndRelation::Free, "the far end touches nothing");
    const std::string text = WireFactsTextJa(wires, facts);
    Require(Contains(text, "始点 → 直線 1 の途中(T 字, 0.000 mm)"), "text says T: " + text);
    Require(Contains(text, "終点 → なし"), "text says the free end: " + text);
}

KACHA_V2_TEST(wire_facts, plane_names_follow_where_the_line_lies)
{
    std::vector<GuideTableSelection> wires;
    wires.push_back(Sel(1, "a", L({0, 5, 0}, {40, 5, 10})));      // y = 5: XZ に平行
    wires.push_back(Sel(2, "b", L({0, 0, 3}, {40, 20, 30})));     // どの軸平面にも載らない直線
    wires.push_back(Sel(3, "c", L({0, 0, 0}, {40, 0, 0})));       // x 軸上: XZ と XY の両方
    Require(Contains(DescribeWire(wires, 0, Tol()).plane.nameJa, "XZ に平行(y = 5.000 mm)"),
        "an offset plane is named with its value: " + DescribeWire(wires, 0, Tol()).plane.nameJa);
    Require(Contains(DescribeWire(wires, 1, Tol()).plane.nameJa, "斜めの直線"),
        "a skew line has no plane: " + DescribeWire(wires, 1, Tol()).plane.nameJa);
    const std::string onAxis = DescribeWire(wires, 2, Tol()).plane.nameJa;
    Require(Contains(onAxis, "正面 XZ") && Contains(onAxis, "上面 XY"),
        "a line on the x axis lies on both planes: " + onAxis);
}

KACHA_V2_TEST(wire_facts, a_circle_is_closed_and_has_no_end_facts)
{
    std::vector<GuideTableSelection> wires;
    const auto circle = CurveSegment::MakeCircle(Vector3{0, 0, 0}, Vector3{0, 0, 1}, Vector3{1, 0, 0}, 10.0);
    Require(circle.HasValue(), "fixture circle is valid");
    wires.push_back(Sel(1, "円 1", circle.Value()));
    const WireFacts facts = DescribeWire(wires, 0, Tol());
    Require(facts.closed, "a circle is closed");
    RequireNear(facts.lengthMm, 2.0 * kPi * 10.0, 1.0e-3, "circumference");
    RequireEqual(facts.plane.nameJa, std::string("上面 XY"), "a circle at z = 0 lies on XY");
    Require(Contains(WireFactsTextJa(wires, facts), "閉じた線"), "text says closed");
}

KACHA_V2_TEST(wire_facts, a_far_away_end_is_free_not_a_gap)
{
    std::vector<GuideTableSelection> wires;
    wires.push_back(Sel(1, "直線 1", L({0, 0, 0}, {10, 0, 0})));
    wires.push_back(Sel(2, "直線 2", L({100, 0, 0}, {110, 0, 0})));
    const WireFacts facts = DescribeWire(wires, 0, Tol());
    Require(facts.end.relation == WireEndRelation::Free, "90 mm away is not a near miss");
    Require(facts.start.relation == WireEndRelation::Free, "the start touches nothing either");
}

KACHA_V2_TEST_MAIN("wire_facts_tests")
