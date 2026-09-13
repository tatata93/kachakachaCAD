// スナップの持ち越し(ヒステリシス)。UI-P1-007。
// 揺れても吸着先が点滅しないこと、順位が上の候補には即座に乗り換えること、
// 同じ位置に重なった別の形へ持ち越しが移らないことを見る。
// 元は snap_tests.cpp にあったが、1ファイル1500行の門を超えたので分けた。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::geometry::kCurveScreenApproachTolerancePx;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::geometry::ApproachToCurveOnScreen;
using kachakacha::v2::geometry::CurveLiesInPlane;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::ExtensionPoint;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::IntersectCurves;
using kachakacha::v2::geometry::MakeOrthographicMapping;
using kachakacha::v2::geometry::MakePerspectiveMapping;
using kachakacha::v2::geometry::PerpendicularFeet;
using kachakacha::v2::geometry::QuadrantPoints;
using kachakacha::v2::geometry::ScreenDistance;
using kachakacha::v2::geometry::ScreenMapping;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::TangentPoints;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChooseSnap;
using kachakacha::v2::modeling::CollectSnapCandidates;
using kachakacha::v2::modeling::SnapCandidate;
using kachakacha::v2::modeling::SnapCurve;
using kachakacha::v2::modeling::SnapDrawingPoint;
using kachakacha::v2::modeling::SnapHysteresis;
using kachakacha::v2::modeling::SnapKind;
using kachakacha::v2::modeling::SnapKindLabelJa;
using kachakacha::v2::modeling::SnapPriorityRank;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::modeling::SnapSettings;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

//! 真上から見た平行投影。1mm = 10px。画面は 1000x1000。
[[nodiscard]] ScreenMapping TopView()
{
    return MakeOrthographicMapping({50.0, 50.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0},
        100.0, 1000.0, 1000.0);
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

struct SceneBuilder {
    DeterministicIdGenerator ids{21};
    SnapScene scene;

    void AddLine(Vector3 a, Vector3 b)
    {
        scene.curves.push_back(SnapCurve{ids.NextTyped<IdKind::Entity>(),
            ids.NextTyped<IdKind::Segment>(), Line(a, b), false});
    }
    void AddCircle(Vector3 center, double radius)
    {
        const auto made =
            CurveSegment::MakeCircle(center, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, radius);
        Require(made.HasValue(), "円が作れること");
        scene.curves.push_back(SnapCurve{ids.NextTyped<IdKind::Entity>(),
            ids.NextTyped<IdKind::Segment>(), made.Value(), false});
    }
    void AddArc(Vector3 center, double radius, double start, double sweep)
    {
        const auto made = CurveSegment::MakeCircularArc(center, {0.0, 0.0, 1.0},
            {1.0, 0.0, 0.0}, radius, start, sweep);
        Require(made.HasValue(), "円弧が作れること");
        scene.curves.push_back(SnapCurve{ids.NextTyped<IdKind::Entity>(),
            ids.NextTyped<IdKind::Segment>(), made.Value(), false});
    }
    void AddPoint(Vector3 position)
    {
        scene.points.push_back(
            SnapDrawingPoint{ids.NextTyped<IdKind::Entity>(), position});
    }
    void EnablePlane()
    {
        scene.workPlane.active = true;
        scene.workPlane.origin = {0.0, 0.0, 0.0};
        scene.workPlane.normal = {0.0, 0.0, 1.0};
    }
    void EnableGrid(double spacing, int subdivision)
    {
        scene.grid.visible = true;
        scene.grid.majorSpacingMm = spacing;
        scene.grid.subdivision = subdivision;
        scene.grid.uDirection = {1.0, 0.0, 0.0};
        scene.grid.vDirection = {0.0, 1.0, 0.0};
    }
};

//! 世界座標を画面座標へ写す。試験でポインタ位置を作るのに使う。
[[nodiscard]] ScreenPoint At(const ScreenMapping& mapping, Vector3 world)
{
    const auto projected = mapping.Project(world);
    Require(projected.has_value(), "画面へ写せること");
    return *projected;
}

[[nodiscard]] bool HasKind(const std::vector<SnapCandidate>& candidates, SnapKind kind)
{
    return std::any_of(candidates.begin(), candidates.end(),
        [&](const SnapCandidate& candidate) { return candidate.kind == kind; });
}

} // namespace

// ---------------------------------------------------------------- 画面への写し方

KACHA_V2_TEST(snap, 同じ順位の候補の間で揺れても入れ替わらない)
{
    SceneBuilder builder;
    builder.AddPoint({50.0, 50.0, 0.0});   // A
    builder.AddPoint({50.6, 50.0, 0.0});   // B。A から 6px
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    SnapHysteresis hysteresis;

    const auto first = hysteresis.Resolve(builder.scene, mapping,
        At(mapping, {50.28, 50.0, 0.0}), {}, Tolerance());
    Require(first.has_value() && first->kind == SnapKind::DrawingPoint, "作図点へ吸着");
    RequireNear(first->position.x, 50.0, 1e-9, "近いAを選ぶ");

    // 0.4px だけ揺れて、B のほうがわずかに近くなる。
    const ScreenPoint jittered = At(mapping, {50.32, 50.0, 0.0});
    const auto stateless = ChooseSnap(
        CollectSnapCandidates(builder.scene, mapping, jittered, {}, Tolerance()), {});
    Require(stateless.has_value(), "持ち越し無しでも吸着はする");
    RequireNear(stateless->position.x, 50.6, 1e-9, "持ち越し無しなら B へ移ってしまう");
    const auto held = hysteresis.Resolve(builder.scene, mapping, jittered, {}, Tolerance());
    Require(held.has_value() && held->held, "持ち越した候補であること");
    RequireNear(held->position.x, 50.0, 1e-9, "持ち越すと A のまま");

    // 揺れの幅(4px)より明らかに B が近ければ乗り換える。
    const auto moved = hysteresis.Resolve(builder.scene, mapping,
        At(mapping, {50.6, 50.0, 0.0}), {}, Tolerance());
    Require(moved.has_value(), "吸着すること");
    RequireNear(moved->position.x, 50.6, 1e-9, "B へ乗り換える");
    Require(hysteresis.Held().has_value(), "新しい候補を持つ");
    RequireNear(hysteresis.Held()->position.x, 50.6, 1e-9, "持っているのは B");
}

KACHA_V2_TEST(snap, 半径の境目で揺れても吸着が点滅しない)
{
    SceneBuilder builder;
    builder.AddPoint({50.0, 50.0, 0.0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    SnapHysteresis hysteresis;
    const auto at = [&](double px) {
        return hysteresis.Resolve(builder.scene, mapping,
            At(mapping, {50.0 + px / 10.0, 50.0, 0.0}), {}, Tolerance());
    };
    const auto isPoint = [](const std::optional<SnapCandidate>& chosen) {
        return chosen.has_value() && chosen->kind == SnapKind::DrawingPoint;
    };

    Require(isPoint(at(11.5)), "12px の内側で吸着する");
    Require(isPoint(at(12.5)), "持ち越し中は 12px を越えても離さない");
    Require(isPoint(at(11.8)), "戻っても同じ");
    Require(isPoint(at(15.5)), "12 + 4px までは離さない");
    Require(!isPoint(at(16.5)), "12 + 4px を越えたら離す");
    Require(!isPoint(at(12.5)), "離したあとは 12px の外で拾い直さない");
    Require(isPoint(at(11.5)), "12px の内側へ戻れば拾い直す");
}

KACHA_V2_TEST(snap, 順位が上の候補が現れたらすぐ乗り換える)
{
    SceneBuilder builder;
    builder.AddPoint({51.5, 50.0, 0.0});   // グリッド点 (50, 50) から 15px
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    SnapHysteresis hysteresis;

    const auto grid = hysteresis.Resolve(builder.scene, mapping,
        At(mapping, {50.0, 50.0, 0.0}), {}, Tolerance());
    Require(grid.has_value() && grid->kind == SnapKind::GridMajor, "作図点は半径の外なので主点");

    // 作図点(11px)が半径に入った。持ち越した主点(4px)のほうが近くても、順位が上なので乗り換える。
    const auto point = hysteresis.Resolve(builder.scene, mapping,
        At(mapping, {50.4, 50.0, 0.0}), {}, Tolerance());
    Require(point.has_value() && point->kind == SnapKind::DrawingPoint, "作図点へ乗り換える");
    RequireNear(point->position.x, 51.5, 1e-9, "作図点の位置");
}

KACHA_V2_TEST(snap, 持ち越しの同一視は位置ではなく吸着先の鍵で決まる)
{
    using kachakacha::v2::modeling::SameSnapTarget;
    using kachakacha::v2::modeling::TargetKeyOf;
    DeterministicIdGenerator ids{5};
    const EntityId curveA = ids.NextTyped<IdKind::Entity>();
    const EntityId curveB = ids.NextTyped<IdKind::Entity>();
    const SegmentId segment = ids.NextTyped<IdKind::Segment>();
    const auto make = [](SnapKind kind, Vector3 position, EntityId entity, SegmentId seg,
                          std::int64_t feature = 0) {
        SnapCandidate candidate;
        candidate.kind = kind;
        candidate.position = position;
        candidate.entityId = entity;
        candidate.segmentId = seg;
        candidate.featureIndex = feature;
        return candidate;
    };

    // 曲線上の最近点はポインタに付いて動く。同じ曲線なら位置が違っても同じ吸着先。
    Require(SameSnapTarget(make(SnapKind::ClosestOnCurve, {60, 50, 0}, curveA, segment),
                make(SnapKind::ClosestOnCurve, {62, 50, 0}, curveA, segment)),
        "同じ線の最近点は同じ");
    Require(!SameSnapTarget(make(SnapKind::ClosestOnCurve, {60, 50, 0}, curveA, segment),
                make(SnapKind::ClosestOnCurve, {60, 50, 0}, curveB, segment)),
        "別の線の最近点は別");
    // 端点は持ち主と端の側で見る。位置の近さでは同じにしない。
    Require(!SameSnapTarget(make(SnapKind::Endpoint, {80, 50, 0}, curveA, segment),
                make(SnapKind::Endpoint, {80.001, 50, 0}, curveB, segment)),
        "interactiveJoinMm より近くても別の形の端点は別");
    Require(!SameSnapTarget(make(SnapKind::Endpoint, {80, 50, 0}, curveA, segment, 0),
                make(SnapKind::Endpoint, {80, 50, 0}, curveA, segment, 1)),
        "閉じた線の始点と終点は同じ位置でも別");
    Require(SameSnapTarget(make(SnapKind::Endpoint, {80, 50, 0}, curveA, segment, 1),
                make(SnapKind::Endpoint, {80, 50, 0}, curveA, segment, 1)),
        "同じ形の同じ側の端点は同じ");
    Require(!SameSnapTarget(make(SnapKind::Endpoint, {80, 50, 0}, curveA, segment),
                make(SnapKind::Midpoint, {80, 50, 0}, curveA, segment)),
        "種類が違えば別");
    // 鍵の並びは全順序で、等しいときだけ前後が付かない。
    const auto first = TargetKeyOf(make(SnapKind::Endpoint, {80, 50, 0}, curveA, segment, 0));
    const auto second = TargetKeyOf(make(SnapKind::Endpoint, {80, 50, 0}, curveB, segment, 0));
    Require((first < second) != (second < first), "別の鍵には前後が付く");
    Require(!(first < first), "同じ鍵には前後が付かない");
}

KACHA_V2_TEST(snap, 近くの別の形の端点へ持ち越しが移らない)
{
    // 1mm = 1000px。A の終点 (0,0) と B の始点 (0.006,0) は画面で 6px 離れている。
    // interactiveJoinMm(0.01mm)より近いが、別の形の別の吸着先である。
    SceneBuilder builder;
    builder.AddLine({-1.0, 0.0, 0.0}, {0.0, 0.0, 0.0});     // A
    builder.AddLine({0.006, 0.0, 0.0}, {1.0, 0.0, 0.0});    // B
    builder.EnablePlane();
    const EntityId a = builder.scene.curves[0].entityId;
    const EntityId b = builder.scene.curves[1].entityId;
    const ScreenMapping mapping = MakeOrthographicMapping({0.003, 0.0, 0.0}, {0.0, 0.0, -1.0},
        {0.0, 1.0, 0.0}, 1.0, 1000.0, 1000.0);
    RequireNear(mapping.PixelsPerMillimeterAt({0.0, 0.0, 0.0}), 1000.0, 1e-6, "前提: 1000px/mm");
    const auto pointerAt = [&](double px) { return At(mapping, {px / 1000.0, 0.001, 0.0}); };
    const auto owner = [](const std::optional<SnapCandidate>& chosen) {
        Require(chosen.has_value() && chosen->kind == SnapKind::Endpoint, "端点へ吸着すること");
        return chosen->entityId;
    };

    SnapHysteresis hysteresis;
    Require(owner(hysteresis.Resolve(builder.scene, mapping, pointerAt(2.8), {}, Tolerance())) == a,
        "A に近いので A");
    // 0.4px 揺れて B のほうがわずかに近くなる。
    Require(owner(ChooseSnap(CollectSnapCandidates(builder.scene, mapping, pointerAt(3.2), {},
                                 Tolerance()), {})) == b,
        "持ち越し無しなら B");
    const auto held = hysteresis.Resolve(builder.scene, mapping, pointerAt(3.2), {}, Tolerance());
    Require(owner(held) == a && held->held, "持ち越すと A のまま");
    RequireNear(held->position.x, 0.0, 1e-12, "位置も A の端点");
    // 揺れの幅(4px)より明らかに B が近ければ乗り換える。
    Require(owner(hysteresis.Resolve(builder.scene, mapping, pointerAt(5.8), {}, Tolerance())) == b,
        "B が明らかに近ければ B へ");
}

KACHA_V2_TEST(snap, 同じ位置に重なった別の形の端点は場面の並び順に依らず持ち主が決まる)
{
    // A の終点と B の始点が (50,50) で重なる。どちらも端点の候補として別に残す。
    SceneBuilder builder;
    builder.AddLine({20.0, 50.0, 0.0}, {50.0, 50.0, 0.0});   // A
    builder.AddLine({50.0, 50.0, 0.0}, {50.0, 80.0, 0.0});   // B
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const ScreenPoint pointer = At(mapping, {50.2, 49.9, 0.0});
    SnapScene reversed = builder.scene;
    std::reverse(reversed.curves.begin(), reversed.curves.end());
    const EntityId lower = std::min(builder.scene.curves[0].entityId,
        builder.scene.curves[1].entityId);
    const EntityId higher = builder.scene.curves[0].entityId == lower
        ? builder.scene.curves[1].entityId
        : builder.scene.curves[0].entityId;

    const auto candidates = CollectSnapCandidates(builder.scene, mapping, pointer, {}, Tolerance());
    RequireCount(static_cast<std::size_t>(std::count_if(candidates.begin(), candidates.end(),
                     [](const SnapCandidate& candidate) {
                         return candidate.kind == SnapKind::Endpoint
                             && candidate.position.x == 50.0 && candidate.position.y == 50.0;
                     })),
        2, "同じ位置でも2つの形の端点を別に残す");
    for (const SnapScene* scene : {&builder.scene, &reversed}) {
        const auto chosen = ChooseSnap(
            CollectSnapCandidates(*scene, mapping, pointer, {}, Tolerance()), {});
        Require(chosen.has_value() && chosen->kind == SnapKind::Endpoint, "端点へ吸着すること");
        Require(chosen->entityId == lower, "持ち越し無しでは ID の小さい形。場面の並び順に依らない");
    }

    // ID の大きい形の端点を持ち越していれば、並び順を逆にしてもその形のまま。
    const auto higherEnd = std::find_if(candidates.begin(), candidates.end(),
        [&](const SnapCandidate& candidate) {
            return candidate.kind == SnapKind::Endpoint && candidate.entityId == higher
                && candidate.position.x == 50.0 && candidate.position.y == 50.0;
        });
    Require(higherEnd != candidates.end(), "前提: ID の大きい形の端点がある");
    SnapSettings settings;
    settings.heldSnap = *higherEnd;
    for (const SnapScene* scene : {&builder.scene, &reversed}) {
        const auto chosen = ChooseSnap(
            CollectSnapCandidates(*scene, mapping, pointer, settings, Tolerance()), settings);
        Require(chosen.has_value() && chosen->entityId == higher && chosen->held,
            "持ち越した形のまま。場面の並び順に依らない");
        RequireEqual(std::to_string(chosen->featureIndex),
            std::to_string(higherEnd->featureIndex), "端の側も同じ");
    }
}

KACHA_V2_TEST(snap, 同じ位置に重なった別の線の最近点と延長線へ持ち越しが移らない)
{
    const ScreenMapping mapping = TopView();
    const GeometryTolerance tolerance = Tolerance();
    const auto owner = [](const std::optional<SnapCandidate>& chosen, SnapKind kind) {
        Require(chosen.has_value(), "吸着すること");
        Require(chosen->kind == kind,
            std::string("種類(実際: ") + std::string(SnapKindLabelJa(chosen->kind)) + ")");
        return chosen->entityId;
    };
    const auto reversed = [](SnapScene scene) {
        std::reverse(scene.curves.begin(), scene.curves.end());
        return scene;
    };

    // 最近点。A は x=0..60、B は x=40..100 で重なる。重なりの中では両方の最近点が同じ位置に出る。
    {
        SceneBuilder builder;
        builder.EnablePlane();
        builder.AddLine({0.0, 50.0, 0.0}, {60.0, 50.0, 0.0});     // A
        builder.AddLine({40.0, 50.0, 0.0}, {100.0, 50.0, 0.0});   // B
        const EntityId b = builder.scene.curves[1].entityId;
        SnapHysteresis hysteresis;
        const auto resolve = [&](double x) {
            return hysteresis.Resolve(builder.scene, mapping, At(mapping, {x, 50.1, 0.0}), {},
                tolerance);
        };
        Require(owner(resolve(80.0), SnapKind::ClosestOnCurve) == b, "B だけの所で B を持つ");
        Require(owner(resolve(75.0), SnapKind::ClosestOnCurve) == b, "B のまま");
        Require(owner(resolve(58.0), SnapKind::ClosestOnCurve) == b, "A と重なっても B のまま");
        Require(owner(resolve(50.0), SnapKind::ClosestOnCurve) == b, "重なりの中でも B のまま");

        const auto overlapping = [&](const SnapScene& scene) {
            const auto candidates = CollectSnapCandidates(scene, mapping,
                At(mapping, {50.0, 50.1, 0.0}), {}, tolerance);
            RequireCount(static_cast<std::size_t>(std::count_if(candidates.begin(),
                             candidates.end(),
                             [](const SnapCandidate& candidate) {
                                 return candidate.kind == SnapKind::ClosestOnCurve;
                             })),
                2, "同じ位置でも2本の最近点を別に残す");
            return owner(ChooseSnap(candidates, {}), SnapKind::ClosestOnCurve);
        };
        Require(overlapping(builder.scene) == overlapping(reversed(builder.scene)),
            "持ち越し無しの答えは場面の並び順に依らない");
    }

    // 延長線。A は x=0..40、B は x=60..100。間の x=45..55 では両方の延長線が同じ位置に出る。
    {
        SceneBuilder builder;
        builder.EnablePlane();
        builder.AddLine({0.0, 50.0, 0.0}, {40.0, 50.0, 0.0});     // A
        builder.AddLine({100.0, 50.0, 0.0}, {60.0, 50.0, 0.0});   // B
        const EntityId b = builder.scene.curves[1].entityId;
        SnapSettings settings;
        settings.maximumExtensionMm = 15.0;
        SnapHysteresis hysteresis;
        const auto resolve = [&](double x) {
            return hysteresis.Resolve(builder.scene, mapping, At(mapping, {x, 50.1, 0.0}),
                settings, tolerance);
        };
        Require(owner(resolve(58.0), SnapKind::Extension) == b, "B の延長線だけの所で B を持つ");
        Require(owner(resolve(54.0), SnapKind::Extension) == b, "A の延長線と重なっても B のまま");
        Require(owner(resolve(50.0), SnapKind::Extension) == b, "真ん中でも B のまま");

        const auto between = [&](const SnapScene& scene) {
            return owner(ChooseSnap(CollectSnapCandidates(scene, mapping,
                                        At(mapping, {50.0, 50.1, 0.0}), settings, tolerance),
                             settings),
                SnapKind::Extension);
        };
        Require(between(builder.scene) == between(reversed(builder.scene)),
            "持ち越し無しの答えは場面の並び順に依らない");
    }
}

KACHA_V2_TEST(snap, S中と取消では持ち越しを捨てる)
{
    SceneBuilder builder;
    builder.AddPoint({50.0, 50.0, 0.0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const ScreenPoint inside = At(mapping, {51.15, 50.0, 0.0});    // 11.5px
    const ScreenPoint boundary = At(mapping, {51.25, 50.0, 0.0});  // 12.5px
    SnapSettings suppressed;
    suppressed.suppressed = true;

    SnapHysteresis hysteresis;
    Require(hysteresis.Resolve(builder.scene, mapping, inside, {}, Tolerance()).has_value(),
        "吸着する");
    Require(!hysteresis.Resolve(builder.scene, mapping, boundary, suppressed, Tolerance())
                 .has_value(),
        "S を押している間は吸着しない");
    Require(!hysteresis.Held().has_value(), "持ち越しも捨てる");
    const auto released = hysteresis.Resolve(builder.scene, mapping, boundary, {}, Tolerance());
    Require(released.has_value() && released->kind == SnapKind::FreeOnPlane,
        "S を離したあと、12px の外の古い候補へ戻らない");

    Require(hysteresis.Resolve(builder.scene, mapping, inside, {}, Tolerance()).has_value(),
        "また吸着する");
    hysteresis.Reset();
    Require(!hysteresis.Held().has_value(), "Reset で持ち越しが無くなる");
    const auto afterReset = hysteresis.Resolve(builder.scene, mapping, boundary, {}, Tolerance());
    Require(afterReset.has_value() && afterReset->kind == SnapKind::FreeOnPlane,
        "Reset のあとは 12px の外で拾わない");
}

KACHA_V2_TEST_MAIN("snap_hysteresis_tests")
