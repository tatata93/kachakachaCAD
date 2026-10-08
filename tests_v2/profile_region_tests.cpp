#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <numbers>

using namespace kachakacha::v2;
using base::IdKind;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;
using modeling::SnapCurve;
using modeling::SnapScene;
using test::Require;

namespace {

struct Bench {
    base::DeterministicIdGenerator ids{41};
    SnapScene scene;

    base::EntityId Line(Vector3 from, Vector3 to)
    {
        const auto curve = CurveSegment::MakeLine(from, to);
        Require(curve.HasValue(), "直線を作れる");
        const auto entity = ids.NextTyped<IdKind::Entity>();
        scene.curves.push_back({entity, ids.NextTyped<IdKind::Segment>(), curve.Value()});
        return entity;
    }

    void Rectangle(double x0, double y0, double x1, double y1, double z = 0.0)
    {
        Line({x0, y0, z}, {x1, y0, z});
        Line({x1, y0, z}, {x1, y1, z});
        Line({x1, y1, z}, {x0, y1, z});
        Line({x0, y1, z}, {x0, y0, z});
    }

    base::EntityId ClosedWireRectangle(double x0, double y0, double x1, double y1)
    {
        const auto entity = ids.NextTyped<IdKind::Entity>();
        const std::vector<std::pair<Vector3, Vector3>> sides{
            {{x0, y0, 0}, {x1, y0, 0}}, {{x1, y0, 0}, {x1, y1, 0}},
            {{x1, y1, 0}, {x0, y1, 0}}, {{x0, y1, 0}, {x0, y0, 0}}};
        for (const auto& [from, to] : sides) {
            const auto curve = CurveSegment::MakeLine(from, to);
            Require(curve.HasValue(), "矩形の辺を作れる");
            scene.curves.push_back(
                {entity, ids.NextTyped<IdKind::Segment>(), curve.Value()});
        }
        return entity;
    }
};

KACHA_V2_TEST(profile_region, five_mixed_curves_become_one_region)
{
    Bench bench;
    bench.Line({0, 0, 0}, {20, 0, 0});
    bench.Line({20, 0, 0}, {20, 10, 0});
    const auto arc = CurveSegment::MakeCircularArc({15, 10, 0}, {0, 0, 1}, {1, 0, 0},
        5.0, 0.0, std::numbers::pi);
    Require(arc.HasValue(), "円弧を作れる");
    bench.scene.curves.push_back({bench.ids.NextTyped<IdKind::Entity>(),
        bench.ids.NextTyped<IdKind::Segment>(), arc.Value()});
    bench.Line({10, 10, 0}, {0, 10, 0});
    bench.Line({0, 10, 0}, {0, 0, 0});

    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 1, "5本の直線と曲線を1領域にする");
    Require(regions.front().outer.segments.size() == 5, "曲線を正本のまま5本保持する");
}

KACHA_V2_TEST(profile_region, hole_and_multiple_planes_remain_distinct)
{
    Bench bench;
    bench.Rectangle(0, 0, 30, 20);
    bench.Rectangle(5, 5, 10, 10);
    bench.Rectangle(40, 0, 50, 10);
    bench.Rectangle(0, 0, 8, 8, 12.0);

    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 3, "穴は領域に数えず、別領域と別平面を数える");
    const auto holed = std::find_if(regions.begin(), regions.end(), [](const auto& region) {
        return !region.holes.empty();
    });
    Require(holed != regions.end(), "内側の輪を穴として持つ");
    Require(app::ProfileRegionContains(*holed, {2, 2, 0}, 0.01), "外周内を拾う");
    Require(!app::ProfileRegionContains(*holed, {7, 7, 0}, 0.01), "穴の中は拾わない");
}

KACHA_V2_TEST(profile_region, open_and_branched_curves_do_not_poison_closed_region)
{
    Bench bench;
    bench.Rectangle(0, 0, 10, 10);
    bench.Line({20, 0, 0}, {30, 0, 0});
    bench.Line({25, 0, 0}, {25, 5, 0});
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 1, "開いた線や分岐があっても閉領域は残る");
}

KACHA_V2_TEST(profile_region, coincident_closed_wire_entities_stay_independent)
{
    Bench bench;
    bench.ClosedWireRectangle(0, 0, 10, 10);
    bench.ClosedWireRectangle(0, 0, 10, 10);
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 2, "端点が重なる別Wireの閉輪郭を分岐扱いしない");
}

} // namespace

KACHA_V2_TEST(profile_region, regions_are_grouped_by_the_plane_they_lie_on)
{
    // 押し出しの輪郭が違う平面にあるとき、平面ごとに別の押し出しにする(入力の数)。
    Bench bench;
    bench.Rectangle(0, 0, 30, 20);            // z = 0
    bench.Rectangle(5, 5, 10, 10);            // z = 0 の穴(同じ領域)
    bench.Rectangle(40, 0, 50, 10);           // z = 0 の別の領域
    bench.Rectangle(0, 0, 8, 8, 12.0);        // z = 12(平行な別の平面)
    // x = 60 の縦の四角(向きの違う平面)
    bench.Line({60, 0, 0}, {60, 10, 0});
    bench.Line({60, 10, 0}, {60, 10, 8});
    bench.Line({60, 10, 8}, {60, 0, 8});
    bench.Line({60, 0, 8}, {60, 0, 0});

    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 4, "領域は 4 つ(穴は数えない)");
    const auto groups = app::GroupProfileRegionsByPlane(regions, 1.0e-5);
    Require(groups.size() == 3, "平面は 3 つ(z = 0 / z = 12 / x = 60): "
            + std::to_string(groups.size()));
    std::size_t together = 0;
    for (const auto& group : groups) {
        together = std::max(together, group.size());
        const auto& owner = regions[group.front()].plane;
        for (const std::size_t index : group) {
            for (const Vector3& point : regions[index].outer.sampled) {
                Require(std::abs(geometry::Dot(point - owner.origin, owner.normal)) <= 1.0e-5,
                    "同じ組の領域は同じ平面に載る");
            }
        }
    }
    Require(together == 2, "z = 0 の 2 つの領域が 1 組になる");

    // 1 つの平面だけなら 1 組。
    Bench flat;
    flat.Rectangle(0, 0, 10, 10);
    flat.Rectangle(20, 0, 30, 10);
    const auto flatGroups = app::GroupProfileRegionsByPlane(
        app::DetectProfileRegions(flat.scene, GeometryTolerance::Default()), 1.0e-5);
    Require(flatGroups.size() == 1 && flatGroups.front().size() == 2, "同じ平面の 2 つは 1 組");
}

KACHA_V2_TEST(profile_region, t_and_x_junctions_form_individual_regions)
{
    Bench bench;
    bench.Rectangle(0, 0, 10, 10);
    bench.Line({5, -2, 0}, {5, 12, 0});
    bench.Line({-2, 5, 0}, {12, 5, 0});
    auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 4, "実交点を分けて4区画: " + std::to_string(regions.size()));
    for (const auto& region : regions) Require(std::abs(region.areaMm2 - 25) < 1e-5,
        "飛び出した線は区画に含めない");
    std::reverse(bench.scene.curves.begin(), bench.scene.curves.end());
    regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 4, "選択順に依存しない");
}

KACHA_V2_TEST(profile_region, spatial_branch_does_not_destroy_planar_faces)
{
    Bench bench;
    bench.Rectangle(0, 0, 10, 10);
    bench.Line({0, 0, 0}, {0, 0, 10});
    bench.Line({0, 0, 10}, {10, 0, 10});
    bench.Line({10, 0, 10}, {10, 0, 0});
    bench.Line({5, 0, 0}, {5, 5, 7});
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 2, "3Dの共有辺と空間分岐でも平面を2枚検出: "
        + std::to_string(regions.size()));
}

KACHA_V2_TEST(profile_region, closed_polyline_can_be_partitioned)
{
    Bench bench;
    bench.ClosedWireRectangle(0, 0, 10, 10);
    bench.Line({3, 0, 0}, {3, 10, 0});
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 2, "1本の閉ポリラインでも途中で2区画: "
        + std::to_string(regions.size()));
    Require(std::abs(regions[0].areaMm2 - 30) < 1e-5
        && std::abs(regions[1].areaMm2 - 70) < 1e-5, "隣の区画を穴と誤認しない");
}

KACHA_V2_TEST(profile_region, curved_t_junction_keeps_exact_arc)
{
    Bench bench;
    const auto arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0},
        10, 0, std::numbers::pi).Value();
    bench.scene.curves.push_back({bench.ids.NextTyped<IdKind::Entity>(),
        bench.ids.NextTyped<IdKind::Segment>(), arc});
    bench.Line({-10, 0, 0}, {10, 0, 0});
    bench.Line({0, 0, 0}, {0, 10, 0});
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 2, "円弧の途中への接続で2区画: " + std::to_string(regions.size()));
    for (const auto& region : regions) {
        Require(std::any_of(region.outer.segments.begin(), region.outer.segments.end(),
            [](const auto& c) { return c.Kind() == geometry::CurveKind::CircularArc
                && std::abs(c.Radius() - 10) < 1e-10; }), "円弧の種類と半径を保持");
    }
}

KACHA_V2_TEST(profile_region, projected_crossing_and_nonplanar_loop_are_not_flattened)
{
    Bench bench;
    bench.Rectangle(0, 0, 10, 10);
    bench.Line({5, -2, 2}, {5, 12, 2});
    bench.Line({20, 0, 0}, {30, 0, 0});
    bench.Line({30, 0, 0}, {30, 10, 3});
    bench.Line({30, 10, 3}, {20, 10, 0});
    bench.Line({20, 10, 0}, {20, 0, 0});
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 1 && std::abs(regions.front().areaMm2 - 100) < 1e-5,
        "画面だけの交差と非平面輪郭を平面へ投影しない");
}

KACHA_V2_TEST(profile_region, corner_diagonal_and_selected_cell)
{
    Bench bench;
    bench.ClosedWireRectangle(0, 0, 10, 10);
    bench.Line({0, 0, 0}, {10, 10, 0});
    auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 2, "角同士を結ぶ対角線でも区画を分ける");
    const auto a = app::ProfileRegionInterior(regions[0]);
    const auto b = app::ProfileRegionInterior(regions[1]);
    app::SelectionSet selected;
    app::PickCandidate first, second;
    first.entityId = second.entityId = bench.scene.curves.front().entityId;
    first.profileSeed = a;
    second.profileSeed = b;
    selected = app::ApplySelection(selected, first, app::SelectionMode::Add);
    selected = app::ApplySelection(selected, second, app::SelectionMode::Add);
    Require(app::SelectedProfileSeeds(selected).size() == 2, "同じワイヤーの隣接区画を別々に選択");
    selected = app::ApplySelection(selected, first, app::SelectionMode::Subtract);
    app::FilterProfileRegions(regions, app::SelectedProfileSeeds(selected), 1e-6);
    Require(regions.size() == 1 && app::ProfileRegionContains(regions[0], b, 1e-6),
        "片方を外しても隣の区画は維持");
}

KACHA_V2_TEST(profile_region, bezier_t_junction_and_tilted_plane)
{
    Bench bench;
    const auto curve = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {0, 4, 4}, {10, 4, 4}, {10, 0, 0}}).Value();
    bench.scene.curves.push_back({bench.ids.NextTyped<IdKind::Entity>(),
        bench.ids.NextTyped<IdKind::Segment>(), curve});
    bench.Line({0, 0, 0}, {10, 0, 0});
    bench.Line({5, 0, 0}, curve.Evaluate(0.5));
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 2, "斜めの平面内でベジェ途中へのT接続を検出");
    for (const auto& region : regions)
        Require(std::any_of(region.outer.segments.begin(), region.outer.segments.end(),
            [](const auto& c) { return c.Kind() == geometry::CurveKind::CubicBezier; }),
            "ベジェを折れ線へ置換しない");
}

KACHA_V2_TEST(profile_region, bridge_to_hole_does_not_create_a_disconnected_boundary)
{
    Bench bench;
    bench.Rectangle(0, 0, 10, 10);
    bench.Rectangle(3, 3, 7, 7);
    bench.Line({0, 0, 0}, {3, 3, 0});
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 1 && regions[0].holes.size() == 1,
        "穴へつながる枝があっても外周と穴を別の閉輪にする");
    Require(std::abs(regions[0].areaMm2 - 84) < 1e-6, "枝を面積へ混ぜない");
    const auto& segments = regions[0].outer.segments;
    for (std::size_t i = 0; i < segments.size(); ++i)
        Require(geometry::Distance(segments[i].EndPoint(),
            segments[(i + 1) % segments.size()].StartPoint()) < 1e-6,
            "外周の途中で穴へ飛ばない");
}

KACHA_V2_TEST(profile_region, coincident_multi_segment_wires_remain_separate)
{
    Bench bench;
    bench.ClosedWireRectangle(0, 0, 10, 10);
    bench.ClosedWireRectangle(0, 0, 10, 10);
    const auto regions = app::DetectProfileRegions(bench.scene, GeometryTolerance::Default());
    Require(regions.size() == 2, "重なった閉ポリラインを別々に選べる");
    Require(app::ProfileRegionEntityIds(regions[0]) != app::ProfileRegionEntityIds(regions[1]),
        "輪郭の出所を混ぜない");
}

int main()
{
    return test::Registry::Instance().RunAll("profile_region_tests");
}
