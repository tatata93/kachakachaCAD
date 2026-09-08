// WP-04 の受入(ワイヤーと連結解析)。geometry-contract §3。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/WireChain.h"

#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::geometry::AnalyzeChain;
using kachakacha::v2::geometry::ChainInput;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::WireEntity;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

struct Fixture {
    DeterministicIdGenerator generator{1};
    EntityId entity = generator.NextTyped<IdKind::Entity>();
    GeometryTolerance tolerance = GeometryTolerance::Default();

    [[nodiscard]] ChainInput Line(Vector3 a, Vector3 b)
    {
        const auto made = CurveSegment::MakeLine(a, b);
        Require(made.HasValue(), "the fixture line is valid");
        return ChainInput{entity, generator.NextTyped<IdKind::Segment>(), made.Value()};
    }
};

} // namespace

// ---- WireEntity ----

KACHA_V2_TEST(wire, a_wire_requires_its_segments_to_touch)
{
    Fixture fixture;
    const auto a = CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value();
    const auto b = CurveSegment::MakeLine({10, 0, 0}, {10, 10, 0}).Value();
    const auto gapped = CurveSegment::MakeLine({11, 0, 0}, {11, 10, 0}).Value();
    DeterministicIdGenerator ids(2);

    const auto joined = WireEntity::Make(fixture.entity,
        {{ids.NextTyped<IdKind::Segment>(), a}, {ids.NextTyped<IdKind::Segment>(), b}},
        1.0e-6);
    Require(joined.HasValue(), "touching segments make a wire");
    Require(!joined.Value().IsClosed(), "an L shape is not closed");
    RequireNear(joined.Value().TotalLength(1.0e-9), 20.0, 1.0e-9, "the lengths add up");

    const auto broken = WireEntity::Make(fixture.entity,
        {{ids.NextTyped<IdKind::Segment>(), a},
            {ids.NextTyped<IdKind::Segment>(), gapped}},
        1.0e-6);
    Require(!broken.HasValue(), "a gap between segments is refused");
    RequireEqual(broken.Diagnostics().front().code, "GEO-W001",
        "the refusal names the disconnection");
}

KACHA_V2_TEST(wire, a_closed_loop_is_detected)
{
    DeterministicIdGenerator ids(3);
    EntityId entity = ids.NextTyped<IdKind::Entity>();
    std::vector<WireEntity::Item> items;
    const std::vector<Vector3> corners = {
        {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}, {0, 0, 0}};
    for (std::size_t index = 0; index + 1 < corners.size(); ++index) {
        items.push_back({ids.NextTyped<IdKind::Segment>(),
            CurveSegment::MakeLine(corners[index], corners[index + 1]).Value()});
    }
    const auto square = WireEntity::Make(entity, std::move(items), 1.0e-6);
    Require(square.HasValue(), "the square is a valid wire");
    Require(square.Value().IsClosed(), "the square is closed");
    RequireNear(square.Value().TotalLength(1.0e-9), 40.0, 1.0e-9, "the perimeter is 40");
}

KACHA_V2_TEST(wire, a_circle_alone_is_a_closed_wire)
{
    DeterministicIdGenerator ids(4);
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value();
    const auto wire = WireEntity::Make(ids.NextTyped<IdKind::Entity>(),
        {{ids.NextTyped<IdKind::Segment>(), circle}}, 1.0e-6);
    Require(wire.HasValue(), "a lone circle is a wire");
    Require(wire.Value().IsClosed(), "a lone circle is closed");
}

// ---- 連結解析 ----

KACHA_V2_TEST(chain, an_open_chain_is_ordered_and_oriented)
{
    Fixture fixture;
    // わざと逆向きと入れ替えた順で渡す。解析が直すこと。
    std::vector<ChainInput> inputs = {
        fixture.Line({10, 0, 0}, {10, 10, 0}),
        fixture.Line({0, 0, 0}, {10, 0, 0}),
    };
    const auto result = AnalyzeChain(inputs, fixture.tolerance);
    Require(result.HasValue(), "the chain resolves");
    Require(!result.Value().order.closed, "an L shape is open");
    Require(result.Value().order.segments.size() == 2, "both segments are used");
}

KACHA_V2_TEST(chain, a_reversed_segment_is_marked_reversed)
{
    Fixture fixture;
    std::vector<ChainInput> inputs = {
        fixture.Line({0, 0, 0}, {10, 0, 0}),
        fixture.Line({10, 10, 0}, {10, 0, 0}),  // 逆向きに描かれている
    };
    const auto result = AnalyzeChain(inputs, fixture.tolerance);
    Require(result.HasValue(), "the chain resolves");
    bool sawReversed = false;
    for (const auto& segment : result.Value().order.segments) {
        sawReversed = sawReversed || segment.reversed;
    }
    Require(sawReversed, "the backwards segment is marked reversed, not silently redrawn");
}

KACHA_V2_TEST(chain, a_closed_chain_is_recognised)
{
    Fixture fixture;
    std::vector<ChainInput> inputs = {
        fixture.Line({0, 0, 0}, {10, 0, 0}),
        fixture.Line({10, 0, 0}, {10, 10, 0}),
        fixture.Line({10, 10, 0}, {0, 10, 0}),
        fixture.Line({0, 10, 0}, {0, 0, 0}),
    };
    const auto result = AnalyzeChain(inputs, fixture.tolerance);
    Require(result.HasValue(), "the square resolves");
    Require(result.Value().order.closed, "the square is closed");
    Require(result.Value().order.segments.size() == 4, "all four sides are used");
}

KACHA_V2_TEST(chain, the_result_does_not_depend_on_the_input_order)
{
    // 同じ図形を違う順番で渡しても、同じ順序が出ること(DOC-013 / DOC-014)。
    const std::vector<std::pair<Vector3, Vector3>> sides = {
        {{0, 0, 0}, {10, 0, 0}},
        {{10, 0, 0}, {10, 10, 0}},
        {{10, 10, 0}, {0, 10, 0}},
        {{0, 10, 0}, {0, 0, 0}},
    };
    const auto build = [&sides](const std::vector<int>& permutation) {
        DeterministicIdGenerator ids(11);
        const EntityId entity = ids.NextTyped<IdKind::Entity>();
        // SegmentId は図形の辺ごとに固定する(並べ替えてもIDは同じ辺に付く)。
        std::vector<SegmentId> segmentIds;
        for (std::size_t index = 0; index < sides.size(); ++index) {
            segmentIds.push_back(ids.NextTyped<IdKind::Segment>());
        }
        std::vector<ChainInput> inputs;
        for (const int index : permutation) {
            inputs.push_back({entity, segmentIds[static_cast<std::size_t>(index)],
                CurveSegment::MakeLine(sides[static_cast<std::size_t>(index)].first,
                    sides[static_cast<std::size_t>(index)].second)
                    .Value()});
        }
        return AnalyzeChain(inputs, GeometryTolerance::Default());
    };

    const auto forward = build({0, 1, 2, 3});
    const auto shuffled = build({2, 0, 3, 1});
    Require(forward.HasValue() && shuffled.HasValue(), "both orders resolve");
    Require(forward.Value().order.segments.size()
            == shuffled.Value().order.segments.size(),
        "both use the same number of segments");
    for (std::size_t index = 0; index < forward.Value().order.segments.size(); ++index) {
        RequireEqual(forward.Value().order.segments[index].segmentId.ToString(),
            shuffled.Value().order.segments[index].segmentId.ToString(),
            "the resolved order is the same whatever order the inputs arrive in");
    }
}

KACHA_V2_TEST(chain, a_disconnected_selection_is_refused)
{
    Fixture fixture;
    std::vector<ChainInput> inputs = {
        fixture.Line({0, 0, 0}, {10, 0, 0}),
        fixture.Line({100, 0, 0}, {110, 0, 0}),  // 遠く離れている
    };
    const auto result = AnalyzeChain(inputs, fixture.tolerance);
    Require(!result.HasValue(), "two separate pieces do not make one chain");
    RequireEqual(result.Diagnostics().front().code, "GEO-W001",
        "the refusal is DisconnectedChain");
}

KACHA_V2_TEST(chain, a_branch_is_reported_with_candidates)
{
    Fixture fixture;
    // 1点から3本出ている。自動では順番を決められない。
    std::vector<ChainInput> inputs = {
        fixture.Line({0, 0, 0}, {10, 0, 0}),
        fixture.Line({10, 0, 0}, {20, 0, 0}),
        fixture.Line({10, 0, 0}, {10, 10, 0}),
    };
    const auto result = AnalyzeChain(inputs, fixture.tolerance);
    Require(!result.HasValue(), "a branch is not resolved automatically");
    RequireEqual(result.Diagnostics().front().code, "GEO-W002",
        "the refusal is BranchedChain");
}

KACHA_V2_TEST(chain, a_near_miss_is_reported_and_nothing_is_moved)
{
    Fixture fixture;
    // modelLinearMm(1e-6) より遠く、interactiveJoinMm(0.01) より近い。
    std::vector<ChainInput> inputs = {
        fixture.Line({0, 0, 0}, {10, 0, 0}),
        fixture.Line({10.005, 0, 0}, {10.005, 10, 0}),
    };
    const auto result = AnalyzeChain(inputs, fixture.tolerance);
    Require(!result.HasValue(), "an almost-touching pair does not silently snap");
    RequireEqual(result.Diagnostics().front().code, "GEO-W003",
        "the refusal is EndpointGap");
    Require(result.Diagnostics().front().detailsJa.find("勝手に") != std::string::npos,
        "the message says the points are not moved on their own");
}

KACHA_V2_TEST(chain, a_duplicate_segment_is_refused)
{
    Fixture fixture;
    const ChainInput line = fixture.Line({0, 0, 0}, {10, 0, 0});
    std::vector<ChainInput> inputs = {line, line};
    const auto result = AnalyzeChain(inputs, fixture.tolerance);
    Require(!result.HasValue(), "the same segment twice is refused");
    RequireEqual(result.Diagnostics().front().code, "GEO-W006",
        "the refusal is DuplicateSegment");
}

KACHA_V2_TEST(chain, an_empty_selection_is_refused)
{
    const auto result = AnalyzeChain({}, GeometryTolerance::Default());
    Require(!result.HasValue(), "an empty selection is refused");
}

KACHA_V2_TEST(chain, a_lone_circle_resolves_as_a_closed_chain)
{
    DeterministicIdGenerator ids(21);
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 3.0).Value();
    std::vector<ChainInput> inputs = {
        {ids.NextTyped<IdKind::Entity>(), ids.NextTyped<IdKind::Segment>(), circle}};
    const auto result = AnalyzeChain(inputs, GeometryTolerance::Default());
    Require(result.HasValue(), "a lone circle resolves");
    Require(result.Value().order.closed, "it is a closed chain");
}

KACHA_V2_TEST(chain, mixed_curve_kinds_chain_together)
{
    // 直線と円弧の混成鎖。§3.3 が必須と定めているケース。
    DeterministicIdGenerator ids(31);
    const EntityId entity = ids.NextTyped<IdKind::Entity>();
    const auto line = CurveSegment::MakeLine({-10, 0, 0}, {0, 0, 0}).Value();
    const auto arc = CurveSegment::MakeCircularArc({0, 5, 0}, {0, 0, 1}, {0, -1, 0}, 5.0,
        0.0, 3.14159265358979323846)
                         .Value();
    std::vector<ChainInput> inputs = {
        {entity, ids.NextTyped<IdKind::Segment>(), line},
        {entity, ids.NextTyped<IdKind::Segment>(), arc},
    };
    const auto result = AnalyzeChain(inputs, GeometryTolerance::Default());
    Require(result.HasValue(), "a line and an arc chain together");
    Require(result.Value().order.segments.size() == 2, "both are used");
}

KACHA_V2_TEST(chain, まっすぐな鎖は自己交差しない)
{
    const std::vector<CurveSegment> segments{
        CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value(),
        CurveSegment::MakeLine({10, 0, 0}, {10, 10, 0}).Value(),
        CurveSegment::MakeLine({10, 10, 0}, {0, 10, 0}).Value()};
    const auto result = kachakacha::v2::geometry::FindSelfIntersections(segments, false,
        GeometryTolerance{});
    Require(result.HasValue(), "交わっていない");
    Require(result.Value().empty(), "0箇所");
}

KACHA_V2_TEST(chain, 閉じた四角も自己交差しない)
{
    const std::vector<CurveSegment> segments{
        CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value(),
        CurveSegment::MakeLine({10, 0, 0}, {10, 10, 0}).Value(),
        CurveSegment::MakeLine({10, 10, 0}, {0, 10, 0}).Value(),
        CurveSegment::MakeLine({0, 10, 0}, {0, 0, 0}).Value()};
    const auto result = kachakacha::v2::geometry::FindSelfIntersections(segments, true,
        GeometryTolerance{});
    Require(result.HasValue(), "交わっていない");
    Require(result.Value().empty(), "0箇所");
}

KACHA_V2_TEST(chain, 8の字は自己交差として断る)
{
    // 対角線が交わる形。閉じているが、自分と交わっている。
    const std::vector<CurveSegment> segments{
        CurveSegment::MakeLine({0, 0, 0}, {10, 10, 0}).Value(),
        CurveSegment::MakeLine({10, 10, 0}, {10, 0, 0}).Value(),
        CurveSegment::MakeLine({10, 0, 0}, {0, 10, 0}).Value(),
        CurveSegment::MakeLine({0, 10, 0}, {0, 0, 0}).Value()};
    const auto result = kachakacha::v2::geometry::FindSelfIntersections(segments, true,
        GeometryTolerance{});
    Require(!result.HasValue(), "断る");
    RequireEqual(result.Diagnostics().front().code, "GEO-W004", "自己交差");
    Require(result.Diagnostics().front().detailsJa.find("本目") != std::string::npos,
        "何本目どうしかを言う");
}

KACHA_V2_TEST(chain, 離れた線どうしが交わるのも断る)
{
    // 1本目と3本目が交わる。隣どうしではないので、端点で触れている言い訳がきかない。
    const std::vector<CurveSegment> segments{
        CurveSegment::MakeLine({0, 5, 0}, {20, 5, 0}).Value(),
        CurveSegment::MakeLine({20, 5, 0}, {20, 20, 0}).Value(),
        CurveSegment::MakeLine({10, 20, 0}, {10, 0, 0}).Value()};
    const auto result = kachakacha::v2::geometry::FindSelfIntersections(segments, false,
        GeometryTolerance{});
    Require(!result.HasValue(), "断る");
    RequireEqual(result.Diagnostics().front().code, "GEO-W004", "自己交差");
}

KACHA_V2_TEST(chain, 隣どうしが端点で触れるのは交差ではない)
{
    // まっすぐ折り返す。端点は共有するが、交差ではない。
    const std::vector<CurveSegment> segments{
        CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value(),
        CurveSegment::MakeLine({10, 0, 0}, {10, 5, 0}).Value()};
    const auto result = kachakacha::v2::geometry::FindSelfIntersections(segments, false,
        GeometryTolerance{});
    Require(result.HasValue(), "交わっていない");
}

KACHA_V2_TEST(chain, 円と直線の自己交差も見つける)
{
    // 円の中を線が通る形。曲線でも見つかること。
    const std::vector<CurveSegment> segments{
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0).Value(),
        CurveSegment::MakeLine({-20, 0, 0}, {20, 0, 0}).Value()};
    const auto result = kachakacha::v2::geometry::FindSelfIntersections(segments, false,
        GeometryTolerance{});
    Require(!result.HasValue(), "断る");
    RequireEqual(result.Diagnostics().front().code, "GEO-W004", "自己交差");
}

KACHA_V2_TEST_MAIN("wire_chain_tests")
