// スナップ(v1-drawing-parity.md §4)。V1の8種すべてと、仕様から漏れていた2種。
// 「グリッドが端点を奪わない」「画面だけの交差へ吸着しない」がここの肝。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::ExtensionPoint;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::IntersectCurves;
using kachakacha::v2::geometry::MakeOrthographicMapping;
using kachakacha::v2::geometry::MakePerspectiveMapping;
using kachakacha::v2::geometry::PerpendicularFeet;
using kachakacha::v2::geometry::QuadrantPoints;
using kachakacha::v2::geometry::ScreenMapping;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::TangentPoints;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChooseSnap;
using kachakacha::v2::modeling::CollectSnapCandidates;
using kachakacha::v2::modeling::SnapCandidate;
using kachakacha::v2::modeling::SnapCurve;
using kachakacha::v2::modeling::SnapDrawingPoint;
using kachakacha::v2::modeling::SnapKind;
using kachakacha::v2::modeling::SnapKindLabelJa;
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

KACHA_V2_TEST(screen, 平行投影で往復できる)
{
    const ScreenMapping mapping = TopView();
    // 中心は画面の真ん中。
    const auto center = mapping.Project({50.0, 50.0, 0.0});
    Require(center.has_value(), "写せること");
    RequireNear(center->x, 500.0, 1e-9, "中心x");
    RequireNear(center->y, 500.0, 1e-9, "中心y");
    // 1mm 動くと 10px 動く。
    const auto shifted = mapping.Project({51.0, 50.0, 0.0});
    RequireNear(shifted->x - center->x, 10.0, 1e-9, "1mmは10px");
    RequireNear(mapping.PixelsPerMillimeterAt({50.0, 50.0, 0.0}), 10.0, 1e-6, "px/mm");

    // 画面から平面へ戻す。
    const auto back = mapping.UnprojectOntoPlane(*center, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    Require(back.has_value(), "戻せること");
    RequireNear(back->x, 50.0, 1e-9, "x");
    RequireNear(back->y, 50.0, 1e-9, "y");
    RequireNear(back->z, 0.0, 1e-9, "z");
}

KACHA_V2_TEST(screen, 透視投影でも往復できる)
{
    const ScreenMapping mapping = MakePerspectiveMapping({0.0, -200.0, 100.0},
        {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.7, 1200.0, 800.0, 1.0, 10000.0);
    const Vector3 world{10.0, 5.0, 3.0};
    const auto projected = mapping.Project(world);
    Require(projected.has_value(), "写せること");
    const auto ray = mapping.RayThrough(*projected);
    Require(ray.has_value(), "視線が引けること");
    // 視線の上にその点があること。
    const Vector3 toPoint = world - ray->origin;
    const double along = Dot(toPoint, ray->direction);
    const double offset = (toPoint - ray->direction * along).Length();
    Require(offset < 1e-6, "視線から外れないこと (" + std::to_string(offset) + ")");
}

KACHA_V2_TEST(screen, カメラの後ろの点は写さない)
{
    const ScreenMapping mapping = MakePerspectiveMapping({0.0, -200.0, 0.0},
        {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.7, 1000.0, 1000.0, 1.0, 10000.0);
    Require(!mapping.Project({0.0, -300.0, 0.0}).has_value(), "後ろは写さない");
}

// ---------------------------------------------------------------- 交差

KACHA_V2_TEST(intersection, 交わる2直線の交点を出す)
{
    const auto crossings = IntersectCurves(Line({0, 0, 0}, {100, 0, 0}),
        Line({50, -50, 0}, {50, 50, 0}), Tolerance());
    RequireCount(crossings.size(), 1, "交点の数");
    RequireNear(crossings[0].position.x, 50.0, 1e-6, "x");
    RequireNear(crossings[0].position.y, 0.0, 1e-6, "y");
    Require(crossings[0].real, "実交点であること");
}

KACHA_V2_TEST(intersection, 離れた2直線に交点は無い)
{
    const auto crossings = IntersectCurves(Line({0, 0, 0}, {100, 0, 0}),
        Line({50, -50, 10}, {50, 50, 10}), Tolerance());
    Require(crossings.empty(), "交点なし");
}

KACHA_V2_TEST(intersection, 直線と円の交点を2つ出す)
{
    const auto circle =
        CurveSegment::MakeCircle({50, 0, 0}, {0, 0, 1}, {1, 0, 0}, 20.0).Value();
    const auto crossings = IntersectCurves(Line({0, 0, 0}, {100, 0, 0}), circle, Tolerance());
    RequireCount(crossings.size(), 2, "交点の数");
    std::vector<double> xs;
    for (const auto& crossing : crossings) {
        xs.push_back(crossing.position.x);
    }
    std::sort(xs.begin(), xs.end());
    RequireNear(xs[0], 30.0, 1e-4, "左の交点");
    RequireNear(xs[1], 70.0, 1e-4, "右の交点");
}

KACHA_V2_TEST(intersection, 接している円と直線は1点)
{
    const auto circle =
        CurveSegment::MakeCircle({50, 20, 0}, {0, 0, 1}, {1, 0, 0}, 20.0).Value();
    const auto crossings = IntersectCurves(Line({0, 0, 0}, {100, 0, 0}), circle, Tolerance());
    Require(crossings.size() >= 1, "接点が出ること");
    RequireNear(crossings[0].position.x, 50.0, 1e-3, "接点x");
}

KACHA_V2_TEST(intersection, 画面では交わるが3Dでは離れている場合を見分ける)
{
    const ScreenMapping mapping = TopView();
    // 上から見ると十字だが、z が 10mm 離れている。
    const auto crossings = kachakacha::v2::geometry::IntersectCurvesOnScreen(
        Line({0, 50, 0}, {100, 50, 0}), Line({50, 0, 10}, {50, 100, 10}), mapping, 8.0,
        Tolerance());
    RequireCount(crossings.size(), 1, "見かけの交差が1つ");
    Require(!crossings[0].real, "実交点ではないこと");
    RequireNear(crossings[0].gapMm, 10.0, 1e-3, "隙間");
}

// ---------------------------------------------------------------- スナップの基本

KACHA_V2_TEST(snap, 端点に吸着する)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {80, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    // 端点から 0.3mm(=3px)ずれた位置を指す。
    const ScreenPoint pointer = At(mapping, {80.3, 50.0, 0.0});
    const auto candidates =
        CollectSnapCandidates(builder.scene, mapping, pointer, {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    Require(chosen->kind == SnapKind::Endpoint, "端点であること");
    RequireNear(chosen->position.x, 80.0, 1e-9, "位置");
}

KACHA_V2_TEST(snap, 中点に吸着する)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {80, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.2, 50.0, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::Midpoint), "中点の候補があること");
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    RequireNear(chosen->position.x, 50.0, 1e-9, "中点");
}

KACHA_V2_TEST(snap, 円の中心に吸着する)
{
    SceneBuilder builder;
    builder.AddCircle({50, 50, 0}, 20.0);
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.3, 50.2, 0.0}), {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    Require(chosen->kind == SnapKind::Center, "中心であること");
    RequireNear(chosen->position.x, 50.0, 1e-9, "中心x");
}

KACHA_V2_TEST(snap, 作図点に吸着する)
{
    SceneBuilder builder;
    builder.AddPoint({33.0, 44.0, 0.0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {33.2, 44.1, 0.0}), {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    Require(chosen->kind == SnapKind::DrawingPoint, "作図点であること");
}

KACHA_V2_TEST(snap, 交点に吸着する)
{
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {100, 50, 0});
    builder.AddLine({50, 0, 0}, {50, 100, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.2, 50.2, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::Intersection), "交点の候補があること");
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    Require(chosen->kind == SnapKind::Intersection, "交点であること");
    RequireNear(chosen->position.x, 50.0, 1e-4, "x");
    RequireNear(chosen->position.y, 50.0, 1e-4, "y");
}

KACHA_V2_TEST(snap, 四半点に吸着する)
{
    SceneBuilder builder;
    builder.AddCircle({50, 50, 0}, 20.0);
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    // 円の右端(70, 50)。
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {70.2, 50.1, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::Quadrant), "四半点の候補があること");
}

// ---------------------------------------------------------------- V1にあった2種

KACHA_V2_TEST(snap, 延長線上に吸着する)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {50, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    // 線の外、まっすぐ伸ばした先。
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {70.0, 50.2, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::Extension), "延長線の候補があること");
    const auto found = std::find_if(candidates.begin(), candidates.end(),
        [](const SnapCandidate& candidate) {
            return candidate.kind == SnapKind::Extension;
        });
    RequireNear(found->position.y, 50.0, 1e-9, "延長線の上に乗ること");
}

KACHA_V2_TEST(snap, 線の内側では延長線を出さない)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {80, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {40.0, 50.2, 0.0}), {}, Tolerance());
    Require(!HasKind(candidates, SnapKind::Extension), "内側では出さないこと");
}

KACHA_V2_TEST(snap, 円弧の延長線も出す)
{
    SceneBuilder builder;
    // 中心(50,50)、半径20、0度から90度までの円弧。
    builder.AddArc({50, 50, 0}, 20.0, 0.0, 1.5707963267948966);
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    // 円弧の外側、-30度あたり。
    const double angle = -0.5;
    const Vector3 outside{50.0 + 20.2 * std::cos(angle), 50.0 + 20.2 * std::sin(angle), 0.0};
    const auto candidates =
        CollectSnapCandidates(builder.scene, mapping, At(mapping, outside), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::Extension), "円弧の延長の候補があること");
}

KACHA_V2_TEST(snap, 作業平面へ法線投影した点に吸着する)
{
    SceneBuilder builder;
    // 平面から 30mm 浮いた作図点。
    builder.AddPoint({40.0, 60.0, 30.0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    // 真上から見ているので、投影点は同じ画面位置に来る。
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {40.0, 60.0, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::ProjectedOnPlane), "投影点の候補があること");
    const auto found = std::find_if(candidates.begin(), candidates.end(),
        [](const SnapCandidate& candidate) {
            return candidate.kind == SnapKind::ProjectedOnPlane;
        });
    RequireNear(found->position.z, 0.0, 1e-9, "平面の上に落ちること");
    RequireNear(found->position.x, 40.0, 1e-9, "x は変わらない");
}

// ---------------------------------------------------------------- 優先順位

KACHA_V2_TEST(snap, グリッドが端点を奪わない)
{
    SceneBuilder builder;
    // 端点(52, 50)。グリッドは10mm間隔なので (50,50) に主点がある。
    builder.AddLine({20, 50, 0}, {52, 50, 0});
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    // 端点から 0.2mm(2px)、グリッド点から 1.8mm(18px)の位置。
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {51.8, 50.0, 0.0}), {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    Require(chosen->kind == SnapKind::Endpoint, "端点が勝つこと");
}

KACHA_V2_TEST(snap, 交点が端点より優先される)
{
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {100, 50, 0});
    builder.AddLine({50, 0, 0}, {50, 100, 0});
    builder.AddPoint({50.3, 50.3, 0.0});   // すぐ近くの作図点
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.15, 50.15, 0.0}), {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    Require(chosen->kind == SnapKind::Intersection, "交点が勝つこと");
}

KACHA_V2_TEST(snap, 画面だけの交差には吸着しない)
{
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {100, 50, 0});
    builder.AddLine({50, 0, 10}, {50, 100, 10});   // z が 10mm 違う
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.0, 50.0, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::ScreenIntersection), "候補としては出すこと");
    Require(!HasKind(candidates, SnapKind::Intersection), "実交点にはしないこと");
    const auto chosen = ChooseSnap(candidates, {});
    if (chosen.has_value()) {
        Require(chosen->kind != SnapKind::ScreenIntersection, "そこへ吸着しないこと");
    }
}

KACHA_V2_TEST(snap, ずっと近い低優先の候補は高優先より勝つ)
{
    // §6.1「距離が大きく違う場合は、優先度より画面距離を優先する」。
    SceneBuilder builder;
    // 中点(50,50)ちょうどを指す。端点は 5.5mm 離れている。
    builder.AddLine({44.5, 50, 0}, {55.5, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.0, 50.0, 0.0}), {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    RequireNear(chosen->position.x, 50.0, 1e-9, "中点へ行くこと");
}

KACHA_V2_TEST(snap, 修飾キーで完全に止まる)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {80, 50, 0});
    builder.AddPoint({50.0, 50.0, 0.0});
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    SnapSettings settings;
    settings.suppressed = true;
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.0, 50.0, 0.0}), settings, Tolerance());
    Require(candidates.empty(), "候補を1つも出さないこと");
    Require(!ChooseSnap(candidates, settings).has_value(), "吸着しないこと");
}

KACHA_V2_TEST(snap, 何も近くに無ければ平面上の自由点になる)
{
    SceneBuilder builder;
    builder.AddLine({0, 0, 0}, {10, 0, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {90.0, 90.0, 0.0}), {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "自由点が出ること");
    Require(chosen->kind == SnapKind::FreeOnPlane, "自由点であること");
    RequireNear(chosen->position.x, 90.0, 1e-9, "指した位置");
}

KACHA_V2_TEST(snap, 作業平面が無ければグリッドも自由点も出ない)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {80, 50, 0});
    builder.EnableGrid(10.0, 0);   // 平面は有効にしない
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {90.0, 90.0, 0.0}), {}, Tolerance());
    Require(!HasKind(candidates, SnapKind::GridMajor), "グリッドが出ないこと");
    Require(!HasKind(candidates, SnapKind::FreeOnPlane), "自由点が出ないこと");
}

// ---------------------------------------------------------------- グリッド

KACHA_V2_TEST(snap, 主グリッド点に吸着する)
{
    SceneBuilder builder;
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.4, 60.3, 0.0}), {}, Tolerance());
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "吸着すること");
    Require(chosen->kind == SnapKind::GridMajor, "主点であること");
    RequireNear(chosen->position.x, 50.0, 1e-9, "x");
    RequireNear(chosen->position.y, 60.0, 1e-9, "y");
}

KACHA_V2_TEST(snap, 副グリッド点は画面が細かすぎると出さない)
{
    SceneBuilder builder;
    builder.EnablePlane();
    builder.EnableGrid(10.0, 4);   // 副点は 2.5mm ごと
    const ScreenMapping mapping = TopView();   // 1mm = 10px なので 副点間隔 25px
    const auto near = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {52.4, 60.0, 0.0}), {}, Tolerance());
    Require(HasKind(near, SnapKind::GridMinor), "十分に広ければ副点が出ること");

    // ぐっと引いた視点。1mm = 0.1px。
    const ScreenMapping farView = MakeOrthographicMapping({50.0, 50.0, 0.0},
        {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, 10000.0, 1000.0, 1000.0);
    const auto farCandidates = CollectSnapCandidates(builder.scene, farView,
        At(farView, {52.5, 60.0, 0.0}), {}, Tolerance());
    Require(!HasKind(farCandidates, SnapKind::GridMinor), "細かすぎれば出さないこと");
}

KACHA_V2_TEST(snap, グリッドを消せば出ない)
{
    SceneBuilder builder;
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.0, 60.0, 0.0}), {}, Tolerance());
    Require(!HasKind(candidates, SnapKind::GridMajor), "主点が出ないこと");
}

// ---------------------------------------------------------------- 接点と垂足

KACHA_V2_TEST(snap, 基準点があれば垂足が出る)
{
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {100, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    SnapSettings settings;
    settings.referencePoint = Vector3{30.0, 80.0, 0.0};
    // 垂足は (30, 50)。
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {30.2, 50.0, 0.0}), settings, Tolerance());
    Require(HasKind(candidates, SnapKind::Perpendicular), "垂足の候補があること");
    const auto found = std::find_if(candidates.begin(), candidates.end(),
        [](const SnapCandidate& candidate) {
            return candidate.kind == SnapKind::Perpendicular;
        });
    RequireNear(found->position.x, 30.0, 1e-4, "垂足のx");
}

KACHA_V2_TEST(snap, 基準点が無ければ垂足も接点も出ない)
{
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {100, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {30.0, 50.0, 0.0}), {}, Tolerance());
    Require(!HasKind(candidates, SnapKind::Perpendicular), "垂足が出ないこと");
    Require(!HasKind(candidates, SnapKind::Tangent), "接点が出ないこと");
}

KACHA_V2_TEST(geometry, 円への接点を求められる)
{
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0).Value();
    // 中心から 20mm 離れた点。接点は2つあり、中心からの距離は半径。
    const auto touches = TangentPoints(circle, {20.0, 0.0, 0.0}, Tolerance());
    Require(touches.size() >= 2, "接点が2つ出ること (" + std::to_string(touches.size()) + ")");
    for (const Vector3& touch : touches) {
        RequireNear(touch.Length(), 10.0, 1e-3, "半径の上にあること");
        // 接線条件: (接点 - 外の点) と 半径ベクトルが直交する。
        const Vector3 toOutside = Vector3{20.0, 0.0, 0.0} - touch;
        RequireNear(Dot(touch, toOutside) / (touch.Length() * toOutside.Length()), 0.0, 1e-3,
            "直交すること");
    }
}

KACHA_V2_TEST(geometry, 円弧の四半点は掃引の中だけ)
{
    // 0度から90度の円弧。中に四半点は無い(両端が0度と90度なので)。
    const auto quarter = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0},
        10.0, 0.0, 1.5707963267948966)
                             .Value();
    Require(QuadrantPoints(quarter).empty(), "中に四半点は無い");

    // 0度から270度の円弧。90度と180度が中に入る。
    const auto threeQuarters = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1},
        {1, 0, 0}, 10.0, 0.0, 3.0 * 1.5707963267948966)
                                   .Value();
    RequireCount(QuadrantPoints(threeQuarters).size(), 2, "四半点が2つ");
}

KACHA_V2_TEST(geometry, 延長線は伸ばしすぎない)
{
    const CurveSegment line = Line({0, 0, 0}, {10, 0, 0});
    Require(ExtensionPoint(line, {15.0, 0.0, 0.0}, 100.0).has_value(), "近ければ出る");
    Require(!ExtensionPoint(line, {500.0, 0.0, 0.0}, 100.0).has_value(), "遠すぎれば出ない");
    Require(!ExtensionPoint(line, {5.0, 0.0, 0.0}, 100.0).has_value(), "内側は出ない");

    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0).Value();
    Require(!ExtensionPoint(circle, {20.0, 0.0, 0.0}, 100.0).has_value(),
        "円に延長線は無い");
}

// ---------------------------------------------------------------- 決定性と表示

KACHA_V2_TEST(snap, 何度呼んでも同じ候補が同じ順で出る)
{
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {100, 50, 0});
    builder.AddLine({50, 0, 0}, {50, 100, 0});
    builder.AddCircle({50, 50, 0}, 20.0);
    builder.AddPoint({50.0, 50.0, 0.0});
    builder.EnablePlane();
    builder.EnableGrid(10.0, 2);
    const ScreenMapping mapping = TopView();
    const ScreenPoint pointer = At(mapping, {50.1, 50.1, 0.0});
    const auto first = CollectSnapCandidates(builder.scene, mapping, pointer, {},
        Tolerance());
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = CollectSnapCandidates(builder.scene, mapping, pointer, {},
            Tolerance());
        RequireCount(again.size(), first.size(), "候補の数");
        for (std::size_t at = 0; at < first.size(); ++at) {
            Require(again[at].kind == first[at].kind, "種類の並び");
            RequireNear(again[at].screenDistancePx, first[at].screenDistancePx, 0.0,
                "画面距離");
        }
    }
}

KACHA_V2_TEST(snap, 全種類に日本語のラベルがある)
{
    const SnapKind kinds[]{SnapKind::Intersection, SnapKind::Endpoint, SnapKind::Center,
        SnapKind::DrawingPoint, SnapKind::Tangent, SnapKind::Perpendicular,
        SnapKind::Midpoint, SnapKind::Quadrant, SnapKind::Extension,
        SnapKind::ProjectedOnPlane, SnapKind::ClosestOnCurve, SnapKind::GridMajor,
        SnapKind::GridMinor, SnapKind::FreeOnPlane, SnapKind::ScreenIntersection};
    for (const SnapKind kind : kinds) {
        Require(!SnapKindLabelJa(kind).empty(),
            "ラベルがあること: " + std::to_string(static_cast<int>(kind)));
    }
}

KACHA_V2_TEST(snap, V1の8種がすべて出せる)
{
    // 作図点・交点・端点・中点・中心・平面へ投影・延長線・グリッド。
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {50, 50, 0});     // 端点・中点・延長線
    builder.AddLine({30, 0, 0}, {30, 100, 0});    // 交点
    builder.AddCircle({30, 50, 0}, 15.0);         // 中心
    builder.AddPoint({30.0, 50.0, 20.0});         // 平面へ投影される作図点
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();

    const auto atCross = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {30.0, 50.0, 0.0}), {}, Tolerance());
    Require(HasKind(atCross, SnapKind::Intersection), "交点");
    Require(HasKind(atCross, SnapKind::Center), "中心");
    Require(HasKind(atCross, SnapKind::ProjectedOnPlane), "平面へ投影");

    const auto atEnd = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.0, 50.0, 0.0}), {}, Tolerance());
    Require(HasKind(atEnd, SnapKind::Endpoint), "端点");

    const auto atMid = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {25.0, 50.0, 0.0}), {}, Tolerance());
    Require(HasKind(atMid, SnapKind::Midpoint), "中点");

    const auto atExtension = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {70.0, 50.0, 0.0}), {}, Tolerance());
    Require(HasKind(atExtension, SnapKind::Extension), "延長線");

    const auto atGrid = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {80.0, 80.0, 0.0}), {}, Tolerance());
    Require(HasKind(atGrid, SnapKind::GridMajor), "グリッド");

    SceneBuilder pointOnly;
    pointOnly.AddPoint({40.0, 40.0, 0.0});
    pointOnly.EnablePlane();
    const auto atPoint = CollectSnapCandidates(pointOnly.scene, mapping,
        At(mapping, {40.0, 40.0, 0.0}), {}, Tolerance());
    Require(HasKind(atPoint, SnapKind::DrawingPoint), "作図点");
}

KACHA_V2_TEST_MAIN("snap_tests")
