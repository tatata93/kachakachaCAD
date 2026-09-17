#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
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

} // namespace

int main()
{
    return test::Registry::Instance().RunAll("profile_region_tests");
}
