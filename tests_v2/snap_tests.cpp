// スナップ(v1-drawing-parity.md §4)。V1の8種すべてと、仕様から漏れていた2種。
// 「グリッドが端点を奪わない」「画面だけの交差へ吸着しない」がここの肝。
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

KACHA_V2_TEST(intersection, 曲線が平面の中にあるかを見分ける)
{
    const Vector3 origin{0.0, 0.0, 0.0};
    const Vector3 normal{0.0, 0.0, 1.0};
    const double tolerance = Tolerance().modelLinearMm;
    Require(CurveLiesInPlane(Line({0, 0, 0}, {100, 20, 0}), origin, normal, tolerance),
        "平面上の直線");
    Require(!CurveLiesInPlane(Line({0, 0, 0}, {100, 20, 1}), origin, normal, tolerance),
        "片端が浮いた直線");
    Require(!CurveLiesInPlane(Line({0, 0, 10}, {100, 20, 10}), origin, normal, tolerance),
        "平行に浮いた直線");
    const auto flat = CurveSegment::MakeCircle({50, 0, 0}, {0, 0, 1}, {1, 0, 0}, 20.0);
    Require(flat.HasValue() && CurveLiesInPlane(flat.Value(), origin, normal, tolerance),
        "平面上の円");
    const auto tilted = CurveSegment::MakeCircle({50, 0, 0}, {0, 0.6, 0.8}, {1, 0, 0}, 20.0);
    Require(tilted.HasValue() && !CurveLiesInPlane(tilted.Value(), origin, normal, tolerance),
        "中心は平面上だが傾いた円");
}

KACHA_V2_TEST(intersection, 円の平面判定は中心のずれと傾きのはみ出しを足して見る)
{
    const Vector3 origin{0.0, 0.0, 0.0};
    const Vector3 normal{0.0, 0.0, 1.0};
    const double tolerance = 0.01;
    const double radius = 20.0;
    // 中心を share × 許容差だけ浮かせ、傾きによるはみ出しも share × 許容差にした円。
    const auto circle = [&](double centerShare, double tiltShare) {
        const double sine = tiltShare * tolerance / radius;
        const auto made = CurveSegment::MakeCircle({50.0, 0.0, centerShare * tolerance},
            {0.0, sine, std::sqrt(1.0 - sine * sine)}, {1.0, 0.0, 0.0}, radius);
        Require(made.HasValue(), "円が作れること");
        return made.Value();
    };
    const auto farthest = [&](const CurveSegment& segment) {
        double maximum = 0.0;
        for (int index = 0; index < 360; ++index) {
            maximum = std::max(maximum, std::abs(segment.Evaluate(index / 360.0).z));
        }
        return maximum;
    };

    Require(CurveLiesInPlane(circle(0.75, 0.0), origin, normal, tolerance), "中心だけ 0.75");
    Require(CurveLiesInPlane(circle(0.0, 0.75), origin, normal, tolerance), "傾きだけ 0.75");
    const CurveSegment both = circle(0.75, 0.75);
    Require(farthest(both) > 1.4 * tolerance,
        "前提: 円周の一部は許容差の 1.5 倍離れていること (" + std::to_string(farthest(both))
            + "mm)");
    Require(!CurveLiesInPlane(both, origin, normal, tolerance),
        "中心 0.75 と傾き 0.75 の和は許容差を越えるので平面の中ではない");
    Require(CurveLiesInPlane(circle(0.45, 0.45), origin, normal, tolerance),
        "中心 0.45 と傾き 0.45 の和は許容差の中");
}

KACHA_V2_TEST(intersection, 透視投影で直線の途中へ画面上の最寄りを返す)
{
    const ScreenMapping mapping = MakePerspectiveMapping({50.0, -150.0, 120.0},
        {50.0, 50.0, 0.0}, {0.0, 0.0, 1.0}, 0.7, 1000.0, 1000.0, 1.0, 10000.0);
    const CurveSegment line = Line({20.0, 30.0, 10.0}, {80.0, 70.0, 10.0});
    // 直線上の点の真上。標本は端点2つだけだが、弦の上で測るので途中を返す。
    const Vector3 onLine = line.Evaluate(0.3);
    const auto approach = ApproachToCurveOnScreen(line, mapping, At(mapping, onLine), 1.0);
    Require(approach.has_value(), "最寄りが出ること");
    RequireNear(approach->distancePx, 0.0, 1e-6, "画面距離");
    // 画面上の割合をそのまま使うと透視で奥へずれる。3D上の割合へ戻していること。
    RequireNear(approach->parameter, 0.3, 1e-9, "パラメータ");
    RequireNear((approach->point - onLine).Length(), 0.0, 1e-9, "3D位置");
}

namespace {

//! 画面上の最短距離の基準値(試験用)。細かく刻んだ標本の谷ごとに、両隣の標本の間を三分探索で詰める。
//! 標本は曲線上の実際の点なので、基準値は真の最短距離以上になる。
[[nodiscard]] double BruteForceScreenDistance(const CurveSegment& segment,
    const ScreenMapping& mapping, const ScreenPoint& pointer)
{
    constexpr int kCount = 100000;
    const auto distanceAt = [&](double t) {
        const auto projected = mapping.Project(segment.Evaluate(std::clamp(t, 0.0, 1.0)));
        return projected.has_value() ? ScreenDistance(*projected, pointer)
                                     : std::numeric_limits<double>::infinity();
    };
    std::vector<double> distances(static_cast<std::size_t>(kCount) + 1);
    for (int index = 0; index <= kCount; ++index) {
        distances[static_cast<std::size_t>(index)] =
            distanceAt(static_cast<double>(index) / kCount);
    }
    double best = *std::min_element(distances.begin(), distances.end());
    const double infinity = std::numeric_limits<double>::infinity();
    for (int index = 0; index <= kCount; ++index) {
        const double here = distances[static_cast<std::size_t>(index)];
        const double left = index > 0 ? distances[static_cast<std::size_t>(index - 1)] : infinity;
        const double right =
            index < kCount ? distances[static_cast<std::size_t>(index + 1)] : infinity;
        if (!(here <= left && here <= right) || here > best + 1.0) {
            continue;
        }
        double low = std::max(0.0, static_cast<double>(index - 1) / kCount);
        double high = std::min(1.0, static_cast<double>(index + 1) / kCount);
        for (int iteration = 0; iteration < 100; ++iteration) {
            const double a = low + (high - low) / 3.0;
            const double b = high - (high - low) / 3.0;
            if (distanceAt(a) < distanceAt(b)) {
                high = b;
            } else {
                low = a;
            }
        }
        best = std::min(best, distanceAt(0.5 * (low + high)));
    }
    return best;
}

//! 曲線上の点から画面で少しずらした位置をいくつも指し、総当たりの基準値と比べる。
//! 半径の境目も見る。基準値より 0.01px 広い半径なら返し、0.01px 狭い半径なら返さない。
void RequireApproachMatchesBruteForce(const std::string& label, const CurveSegment& segment,
    const ScreenMapping& mapping, const std::vector<double>& parameters)
{
    const std::vector<ScreenPoint> offsets{
        {5.0, 0.0}, {0.0, -6.0}, {-4.2, 4.2}, {8.5, 8.5}, {-11.0, 3.0}, {2.0, 11.5}};
    int checked = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto base = mapping.Project(segment.Evaluate(parameters[index]));
        if (!base.has_value()) {
            continue;
        }
        const ScreenPoint& offset = offsets[index % offsets.size()];
        const ScreenPoint pointer{base->x + offset.x, base->y + offset.y};
        const double reference = BruteForceScreenDistance(segment, mapping, pointer);
        const std::string at = label + " t=" + std::to_string(parameters[index]);
        const auto approach = ApproachToCurveOnScreen(segment, mapping, pointer,
            std::numeric_limits<double>::infinity());
        Require(approach.has_value(), at + ": 最寄りが出ること");
        Require(approach->distancePx <= reference + kCurveScreenApproachTolerancePx + 1.0e-9,
            at + ": 総当たりより遠い点を返さないこと (" + std::to_string(approach->distancePx)
                + " / " + std::to_string(reference) + ")");
        Require(approach->distancePx >= reference - 1.0e-3,
            at + ": 総当たりより近すぎないこと (" + std::to_string(approach->distancePx) + " / "
                + std::to_string(reference) + ")");
        const auto projected = mapping.Project(approach->point);
        Require(projected.has_value(), at + ": 返した点を画面へ写せること");
        RequireNear(ScreenDistance(*projected, pointer), approach->distancePx, 1.0e-9,
            at + ": 距離は返した点までの距離");
        RequireNear((segment.Evaluate(approach->parameter) - approach->point).Length(), 0.0,
            1.0e-12, at + ": パラメータと点は同じ位置");
        Require(ApproachToCurveOnScreen(segment, mapping, pointer, reference + 0.01).has_value(),
            at + ": 半径が最短距離より 0.01px 広ければ返すこと");
        if (reference > 0.02) {
            Require(!ApproachToCurveOnScreen(segment, mapping, pointer, reference - 0.01)
                         .has_value(),
                at + ": 半径が最短距離より 0.01px 狭ければ返さないこと");
        }
        ++checked;
    }
    Require(checked >= 3, label + ": 試した位置が足りること (" + std::to_string(checked) + ")");
}

[[nodiscard]] CurveSegment Bezier(std::vector<Vector3> points)
{
    const auto made = CurveSegment::MakeCubicBezier(std::move(points));
    Require(made.HasValue(), "Bezierが作れること");
    return made.Value();
}

[[nodiscard]] std::vector<double> SpreadParameters(double center, double step, int count)
{
    std::vector<double> parameters;
    for (int index = 0; index < count; ++index) {
        parameters.push_back(center + (index - 0.5 * (count - 1)) * step);
    }
    return parameters;
}

} // namespace

KACHA_V2_TEST(intersection, 画面上の最寄りは強い変曲とループでも総当たりと一致する)
{
    // 強い変曲。t=0.5 の点 (15,0) は両端を結ぶ弦のちょうど真ん中に乗るので、
    // 弦の中央だけを見る分割では割られず、途中の大きな膨らみを見落とす。
    const CurveSegment inflection = Bezier({{0, 0, 0}, {30, 40, 0}, {0, -40, 0}, {30, 0, 0}});
    RequireNear((inflection.Evaluate(0.5) - Vector3{15.0, 0.0, 0.0}).Length(), 0.0, 1e-12,
        "前提: 真ん中は弦の上");
    const ScreenMapping top = MakeOrthographicMapping({15.0, 0.0, 0.0}, {0.0, 0.0, -1.0},
        {0.0, 1.0, 0.0}, 60.0, 1000.0, 1000.0);
    RequireApproachMatchesBruteForce("強い変曲", inflection, top, SpreadParameters(0.5, 0.1, 10));

    // 自分と交わるループ。
    const CurveSegment loop = Bezier({{0, 0, 0}, {40, 30, 0}, {-10, 30, 0}, {30, 0, 0}});
    RequireApproachMatchesBruteForce("ループ", loop, top, SpreadParameters(0.5, 0.1, 10));

    // 一様3次B-spline(区分ごとにBezierへ直して囲む)を斜めの平行投影で。
    const auto spline = CurveSegment::MakeCubicBSpline({{0, 0, 0}, {10, 20, 5}, {20, -15, 0},
        {30, 25, -5}, {40, -10, 0}, {50, 5, 3}});
    Require(spline.HasValue(), "B-splineが作れること");
    const ScreenMapping oblique = MakeOrthographicMapping({25.0, 0.0, 0.0}, {0.3, -0.4, -0.87},
        {0.0, 0.0, 1.0}, 70.0, 1000.0, 1000.0);
    RequireApproachMatchesBruteForce("B-spline", spline.Value(), oblique,
        SpreadParameters(0.5, 0.1, 10));
}

KACHA_V2_TEST(intersection, 画面上の最寄りは透視と高倍率とカメラの後ろにかかる曲線でも総当たりと一致する)
{
    // 3次元に浮いた曲線を斜めの透視で。
    const CurveSegment spatial = Bezier({{0, 0, 0}, {20, 30, 15}, {40, -20, -10}, {60, 10, 5}});
    const ScreenMapping perspective = MakePerspectiveMapping({30.0, -80.0, 60.0},
        {30.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.6, 1000.0, 800.0, 1.0, 10000.0);
    RequireApproachMatchesBruteForce("透視", spatial, perspective, SpreadParameters(0.5, 0.1, 10));

    // 傾いた円を透視で。
    const auto tilted = CurveSegment::MakeCircle({0, 0, 0}, {0.0, 0.6, 0.8}, {1, 0, 0}, 20.0);
    Require(tilted.HasValue(), "傾いた円が作れること");
    RequireApproachMatchesBruteForce("傾いた円", tilted.Value(),
        MakePerspectiveMapping({40.0, -60.0, 50.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.5,
            1000.0, 1000.0, 1.0, 10000.0),
        SpreadParameters(0.5, 0.1, 10));

    // 高倍率の透視。視点を強い変曲の曲線のすぐ上に置く。曲線の一部は視点の後ろへ回り込む。
    const CurveSegment inflection = Bezier({{0, 0, 0}, {30, 40, 0}, {0, -40, 0}, {30, 0, 0}});
    const ScreenMapping close = MakePerspectiveMapping({15.0, -1.0, 3.0}, {15.0, 0.0, 0.0},
        {0.0, 0.0, 1.0}, 0.2, 1000.0, 1000.0, 0.1, 1000.0);
    Require(close.PixelsPerMillimeterAt({15.0, 0.0, 0.0}) > 1000.0,
        "前提: 1mm が 1000px を越える ("
            + std::to_string(close.PixelsPerMillimeterAt({15.0, 0.0, 0.0})) + ")");
    const bool partlyBehind = [&] {
        for (int index = 0; index <= 1000; ++index) {
            if (!close.Project(inflection.Evaluate(index / 1000.0)).has_value()) {
                return true;
            }
        }
        return false;
    }();
    Require(partlyBehind, "前提: 曲線の一部は視点の後ろにある");
    RequireApproachMatchesBruteForce("高倍率の透視", inflection, close,
        SpreadParameters(0.5, 0.0004, 8));

    // 視点の後ろから前へ抜ける直線。
    const CurveSegment through = Line({-3.0, -30.0, 1.0}, {3.0, 40.0, -1.0});
    const ScreenMapping behind = MakePerspectiveMapping({0.0, -10.0, 0.5}, {0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0}, 0.8, 1000.0, 1000.0, 0.1, 1000.0);
    Require(!behind.Project(through.StartPoint()).has_value(), "前提: 始点は視点の後ろ");
    RequireApproachMatchesBruteForce("視点の後ろから抜ける直線", through, behind,
        SpreadParameters(0.6, 0.05, 8));
}

KACHA_V2_TEST(snap, 作業平面から外れた線でも透視表示で線の真上なら最近点へ吸着する)
{
    // 作業平面は z=0(アプリでは常に有効)。線は z=10 にあり、斜めの透視投影で見る。
    SceneBuilder builder;
    builder.EnablePlane();
    builder.AddLine({20.0, 30.0, 10.0}, {80.0, 70.0, 10.0});
    const ScreenMapping mapping = MakePerspectiveMapping({50.0, -150.0, 120.0},
        {50.0, 50.0, 0.0}, {0.0, 0.0, 1.0}, 0.7, 1000.0, 1000.0, 1.0, 10000.0);
    const GeometryTolerance tolerance = Tolerance();
    // 端点・中点から離れた線の途中の、画面で 3px ずれた位置。
    ScreenPoint pointer = At(mapping, builder.scene.curves[0].segment.Evaluate(0.3));
    pointer.y += 3.0;

    // 前提: 平面へ戻した点の3D最近点は、画面では吸着半径の外になる。
    const auto planeHit = mapping.UnprojectOntoPlane(pointer, builder.scene.workPlane.origin,
        builder.scene.workPlane.normal);
    Require(planeHit.has_value(), "平面へ戻せること");
    const double viaPlanePx = ScreenDistance(
        At(mapping, builder.scene.curves[0].segment.ClosestPoint(*planeHit).point), pointer);
    Require(viaPlanePx > tolerance.snapPickPx,
        "平面経由の最近点は画面で遠いこと (" + std::to_string(viaPlanePx) + "px)");

    const SnapSettings settings{};
    const auto candidates =
        CollectSnapCandidates(builder.scene, mapping, pointer, settings, tolerance);
    const auto closest = std::find_if(candidates.begin(), candidates.end(),
        [](const SnapCandidate& candidate) { return candidate.kind == SnapKind::ClosestOnCurve; });
    Require(closest != candidates.end(), "曲線上の最近点が候補に出ること");
    Require(closest->screenDistancePx <= tolerance.snapPickPx,
        "吸着半径の中 (" + std::to_string(closest->screenDistancePx) + "px)");
    const double projectedPx = ScreenDistance(At(mapping, closest->position), pointer);
    Require(projectedPx <= 3.0 + 1e-6,
        "置く位置がポインタの近くに写ること (" + std::to_string(projectedPx) + "px)");
    RequireNear(builder.scene.curves[0].segment.ClosestPoint(closest->position).distance, 0.0,
        1e-9, "置く位置が線の上にあること");

    const auto chosen = ChooseSnap(candidates, settings);
    Require(chosen.has_value() && chosen->kind == SnapKind::ClosestOnCurve,
        "最近点へ吸着すること(実際: "
            + std::string(chosen.has_value() ? SnapKindLabelJa(chosen->kind) : "候補なし") + ")");
}

KACHA_V2_TEST(snap, 作業平面の中の線でも寝た視点では画面で近い最近点へ吸着する)
{
    // 作業平面は z=0、線も z=0。仰角 5° の平行投影で、平面をほぼ真横から見る。
    SceneBuilder builder;
    builder.EnablePlane();
    builder.AddLine({-40.0, -40.0, 0.0}, {40.0, 40.0, 0.0});
    const double elevation = 5.0 * 3.141592653589793 / 180.0;
    const CurveSegment& line = builder.scene.curves[0].segment;
    const Vector3 onLine = line.Evaluate(0.3);   // 端点・中点から離れた位置
    const ScreenMapping mapping = MakeOrthographicMapping(onLine,
        {0.0, std::cos(elevation), -std::sin(elevation)}, {0.0, 0.0, 1.0}, 200.0, 1000.0,
        1000.0);
    const GeometryTolerance tolerance = Tolerance();
    Require(CurveLiesInPlane(line, builder.scene.workPlane.origin, builder.scene.workPlane.normal,
                tolerance.modelLinearMm),
        "前提: 線は作業平面の中");
    ScreenPoint pointer = At(mapping, onLine);
    pointer.y += 3.0;

    // 前提: 平面へ戻した点の3D最近点は、画面では吸着半径の外になる。
    const auto planeHit = mapping.UnprojectOntoPlane(pointer, builder.scene.workPlane.origin,
        builder.scene.workPlane.normal);
    Require(planeHit.has_value(), "平面へ戻せること");
    const double viaPlanePx = ScreenDistance(At(mapping, line.ClosestPoint(*planeHit).point),
        pointer);
    Require(viaPlanePx > tolerance.snapPickPx,
        "平面経由の最近点は画面で遠いこと (" + std::to_string(viaPlanePx) + "px)");

    const SnapSettings settings{};
    const auto candidates =
        CollectSnapCandidates(builder.scene, mapping, pointer, settings, tolerance);
    const auto closest = std::find_if(candidates.begin(), candidates.end(),
        [](const SnapCandidate& candidate) { return candidate.kind == SnapKind::ClosestOnCurve; });
    Require(closest != candidates.end(), "曲線上の最近点が候補に出ること");
    const double projectedPx = ScreenDistance(At(mapping, closest->position), pointer);
    Require(projectedPx <= 3.0 + 1e-6,
        "置く位置がポインタの近くに写ること (" + std::to_string(projectedPx) + "px)");
    RequireNear(closest->screenDistancePx, projectedPx, 1e-9, "候補の距離は置く位置の距離");
    RequireNear(line.ClosestPoint(closest->position).distance, 0.0, 1e-9,
        "置く位置が線の上にあること");

    const auto chosen = ChooseSnap(candidates, settings);
    Require(chosen.has_value() && chosen->kind == SnapKind::ClosestOnCurve,
        "最近点へ吸着すること(実際: "
            + std::string(chosen.has_value() ? SnapKindLabelJa(chosen->kind) : "候補なし") + ")");
}

KACHA_V2_TEST(snap, 拡大表示でも円の最近点は画面へ写した円で測る)
{
    // 1mm = 1000px。弦誤差 interactiveJoinMm(0.01mm)は最大 10px になる。
    // 作業平面を使わず、画面上の最寄りだけで最近点を決める経路を通す。
    SceneBuilder builder;
    builder.AddCircle({0.0, 0.0, 0.0}, 20.0);
    const CurveSegment& circle = builder.scene.curves[0].segment;
    const GeometryTolerance tolerance = Tolerance();
    // 弦がいちばん円から離れる区間の真ん中を見る。
    const auto samples = kachakacha::v2::geometry::SampleCurve(circle, tolerance.interactiveJoinMm);
    double middleParameter = 0.0;
    double sagMm = -1.0;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const double parameter = 0.5 * (samples[index - 1].parameter + samples[index].parameter);
        const Vector3 chordMiddle = (samples[index - 1].position + samples[index].position) * 0.5;
        const double sag = (circle.Evaluate(parameter) - chordMiddle).Length();
        if (sag > sagMm) {
            sagMm = sag;
            middleParameter = parameter;
        }
    }
    const Vector3 onCircle = circle.Evaluate(middleParameter);
    const ScreenMapping mapping = MakeOrthographicMapping(onCircle, {0.0, 0.0, -1.0},
        {0.0, 1.0, 0.0}, 1.0, 1000.0, 1000.0);
    Require(sagMm * mapping.PixelsPerMillimeterAt(onCircle) > 3.0,
        "前提: 弦は円から 3px より離れている (" + std::to_string(sagMm * 1000.0) + "px)");
    const Vector3 outward = kachakacha::v2::geometry::Normalized(onCircle);
    // 円の外側へ 9px。円からは 9px(12px の内側)だが、弦からは 9px + 弦のずれ。
    const ScreenPoint pointer = At(mapping, onCircle + outward * 0.009);

    const auto candidates = CollectSnapCandidates(builder.scene, mapping, pointer, {}, tolerance);
    const auto closest = std::find_if(candidates.begin(), candidates.end(),
        [](const SnapCandidate& candidate) { return candidate.kind == SnapKind::ClosestOnCurve; });
    Require(closest != candidates.end(), "曲線上の最近点が候補に出ること");
    RequireNear(closest->screenDistancePx, 9.0, 1e-3, "円までの画面距離");
    RequireNear((closest->position - onCircle).Length(), 0.0, 1e-6, "円の上のポインタ直下");
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

KACHA_V2_TEST(snap, 作図点と端点は交点より優先される)
{
    // ui-ux-integrated-spec.md §6.1: 1. 端点、作図点 2. 交点。
    // 交点と同じ画面距離に置き、順位だけで決まることを見る。
    const ScreenMapping mapping = TopView();
    const ScreenPoint pointer = At(mapping, {50.15, 50.15, 0.0});
    {
        SceneBuilder builder;
        builder.AddLine({0, 50, 0}, {100, 50, 0});
        builder.AddLine({50, 0, 0}, {50, 100, 0});
        builder.AddPoint({50.3, 50.3, 0.0});   // すぐ近くの作図点
        builder.EnablePlane();
        const auto candidates =
            CollectSnapCandidates(builder.scene, mapping, pointer, {}, Tolerance());
        Require(HasKind(candidates, SnapKind::Intersection), "交点の候補はあること");
        const auto chosen = ChooseSnap(candidates, {});
        Require(chosen.has_value(), "吸着すること");
        Require(chosen->kind == SnapKind::DrawingPoint, "作図点が勝つこと");
    }
    {
        SceneBuilder builder;
        builder.AddLine({0, 50, 0}, {100, 50, 0});
        builder.AddLine({50, 0, 0}, {50, 100, 0});
        builder.AddLine({50.3, 50.3, 0}, {70, 70, 0});   // 端点がすぐ近く。他とは交わらない
        builder.EnablePlane();
        const auto candidates =
            CollectSnapCandidates(builder.scene, mapping, pointer, {}, Tolerance());
        Require(HasKind(candidates, SnapKind::Intersection), "交点の候補はあること");
        const auto chosen = ChooseSnap(candidates, {});
        Require(chosen.has_value(), "吸着すること");
        Require(chosen->kind == SnapKind::Endpoint, "端点が勝つこと");
    }
}

KACHA_V2_TEST(snap, 同じ順位なら画面距離の近いほうを選ぶ)
{
    // 端点と作図点は同じ順位。種類の並びではなく、近さで決まる。
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {50, 50, 0});   // 端点 (50, 50)
    builder.AddPoint({51.0, 50.0, 0.0});         // 作図点 (51, 50)
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();

    const auto nearPoint = ChooseSnap(CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.8, 50.0, 0.0}), {}, Tolerance()), {});
    Require(nearPoint.has_value(), "吸着すること");
    Require(nearPoint->kind == SnapKind::DrawingPoint, "作図点(2px)が端点(8px)に勝つこと");

    const auto nearEnd = ChooseSnap(CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {50.2, 50.0, 0.0}), {}, Tolerance()), {});
    Require(nearEnd.has_value(), "吸着すること");
    Require(nearEnd->kind == SnapKind::Endpoint, "端点(2px)が作図点(8px)に勝つこと");
}

KACHA_V2_TEST(snap, 順位は統合仕様の6段に従う)
{
    const auto same = [](SnapKind a, SnapKind b) {
        return SnapPriorityRank(a) == SnapPriorityRank(b);
    };
    const auto stronger = [](SnapKind a, SnapKind b) {
        return SnapPriorityRank(a) < SnapPriorityRank(b);
    };
    Require(same(SnapKind::Endpoint, SnapKind::DrawingPoint), "1. 端点と作図点は同じ順位");
    Require(stronger(SnapKind::DrawingPoint, SnapKind::Intersection), "1 > 2. 交点");
    Require(stronger(SnapKind::Intersection, SnapKind::Midpoint), "2 > 3. 中点");
    Require(same(SnapKind::Midpoint, SnapKind::Center)
            && same(SnapKind::Midpoint, SnapKind::Quadrant),
        "3. 中点・中心・四分点は同じ順位");
    // 線上の格子は最近点より強い(2026-09-21 オーナー報告)。最近点は距離がほぼ 0 なので
    // 同じ順位では線の上のグリッドの点が選ばれない。
    Require(stronger(SnapKind::Quadrant, SnapKind::GridOnCurve), "3 > 線上の格子");
    Require(stronger(SnapKind::GridOnCurve, SnapKind::ClosestOnCurve), "線上の格子 > 4. 最近点");
    Require(stronger(SnapKind::ClosestOnCurve, SnapKind::Perpendicular), "4 > 5. 垂足");
    Require(same(SnapKind::Perpendicular, SnapKind::Tangent), "5. 垂足と接点は同じ順位");
    // §6.1 に無い平面へ投影の置き場所は暫定(オーナー判断待ち)。
    Require(stronger(SnapKind::Tangent, SnapKind::ProjectedOnPlane), "5 > 平面へ投影(暫定)");
    Require(stronger(SnapKind::ProjectedOnPlane, SnapKind::GridMajor),
        "平面へ投影 > 6. グリッド(暫定)");
    Require(same(SnapKind::GridMajor, SnapKind::GridMinor), "6. 主点と副点は同じ順位");
    // 延長線はグリッドより弱い(2026-09-21 オーナー判断)。線の案内より点を採る。
    Require(stronger(SnapKind::GridMinor, SnapKind::Extension), "グリッド > 延長線");
    Require(stronger(SnapKind::Extension, SnapKind::FreeOnPlane), "延長線 > 自由点");
    Require(stronger(SnapKind::FreeOnPlane, SnapKind::ScreenIntersection), "画面交差は最後");
}

KACHA_V2_TEST(snap, 延長線と重なってもグリッドの点を採る)
{
    // オーナー報告 2026-09-21:「作図するときほかの線の延長線だとグリッドが反応しない」。
    // 延長線は「線」の案内、グリッドは「点」。格子を狙っているのに、たまたま近くの線の
    // 延長と重なっただけで格子から外れた点が入っていた。
    SceneBuilder builder;
    // y = 50 の線。その延長(x > 50)が格子の横線と重なる。
    builder.AddLine({20, 50, 0}, {50, 50, 0});
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    // 格子の点 (70, 50) のすぐ near。延長線の候補も格子の候補も範囲に入る。
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {70.2, 50.2, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::Extension), "延長線の候補は出ている");
    Require(HasKind(candidates, SnapKind::GridMajor), "格子の候補も出ている");
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value(), "どれかに吸着する");
    RequireEqual(std::string(SnapKindLabelJa(chosen->kind)), std::string("主点"),
        "格子の点を採る(延長線ではなく)");
    RequireNear(chosen->position.x, 70.0, 1e-9, "格子の上に乗る");
    RequireNear(chosen->position.y, 50.0, 1e-9, "格子の上に乗る");
}

KACHA_V2_TEST(snap, 直線の上でもグリッドの点に吸着する)
{
    // オーナー報告 2026-09-21:「直線上なのに曲線上って出てる。線上のグリッドにスナップできない」。
    // 最近点はポインタ直下に付くので距離がほぼ 0。グリッドと同じか上の順位だと、
    // 線の上ではグリッドの点がいつまでも選ばれなかった。
    SceneBuilder builder;
    builder.AddLine({-100, 0, 0}, {100, 0, 0});
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    const auto candidates = CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {30.3, 0.2, 0.0}), {}, Tolerance());
    Require(HasKind(candidates, SnapKind::ClosestOnCurve), "線上の最近点も候補に出ている");
    const auto chosen = ChooseSnap(candidates, {});
    Require(chosen.has_value() && chosen->kind == SnapKind::GridOnCurve,
        "線の上の格子の点を採る");
    RequireNear(chosen->position.x, 30.0, 1e-9, "格子の上に乗る");
    RequireNear(chosen->position.y, 0.0, 1e-9, "線の上に乗る");
    RequireEqual(std::string(SnapKindLabelJa(chosen->kind)), std::string("線上の格子"),
        "何に吸着したかを言う");
}

KACHA_V2_TEST(snap, 斜めの線ではグリッドの線を横切る点に吸着する)
{
    SceneBuilder builder;
    builder.AddLine({0, 0, 0}, {100, 50, 0});   // y = x / 2
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    // x = 40 の縦線を横切る点は (40, 20)。そのすぐ脇を指す。
    const auto chosen = ChooseSnap(CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {40.3, 20.1, 0.0}), {}, Tolerance()), {});
    Require(chosen.has_value() && chosen->kind == SnapKind::GridOnCurve, "線上の格子を採る");
    RequireNear(chosen->position.x, 40.0, 1e-9, "縦線の上");
    RequireNear(chosen->position.y, 20.0, 1e-9, "斜めの線の上");
}

KACHA_V2_TEST(snap, 格子から離れた線の上では最近点のまま)
{
    SceneBuilder builder;
    builder.AddLine({-100, 0, 0}, {100, 0, 0});
    builder.EnablePlane();
    builder.EnableGrid(10.0, 0);
    const ScreenMapping mapping = TopView();
    const auto chosen = ChooseSnap(CollectSnapCandidates(builder.scene, mapping,
        At(mapping, {35.0, 0.2, 0.0}), {}, Tolerance()), {});
    Require(chosen.has_value(), "吸着する");
    Require(chosen->kind == SnapKind::ClosestOnCurve, "格子から 50px 離れたところは最近点");
    RequireNear(chosen->position.y, 0.0, 1e-9, "線の上に乗る");
    RequireEqual(std::string(SnapKindLabelJa(SnapKind::ClosestOnCurve)), std::string("線上"),
        "直線でも曲線でも「線上」と言う");
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

KACHA_V2_TEST(snap, 吸着半径の外の端点は近くの中点を奪わない)
{
    // 順位が上でも、snapPickPx(12px)の外にある候補は選ばない。
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

KACHA_V2_TEST(snap, 垂足の上でも同じ線の最近点が統合仕様の順位どおり勝つ)
{
    // ui-ux-integrated-spec.md §6.1: 4. 曲線上の最近点 > 5. 垂足、接点。例外は置かない。
    // 同じ線の最近点は垂足と同じか近いので、この順位のままでは垂足が選ばれる場面が無い。
    // 例外を設けるかはオーナー判断待ち(v1-drawing-parity.md §4)。候補としては出す。
    SceneBuilder builder;
    builder.AddLine({0, 50, 0}, {100, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    SnapSettings settings;
    settings.referencePoint = Vector3{30.0, 80.0, 0.0};   // 垂足は (30, 50)

    for (const double offsetMm : {0.0, 0.4}) {   // 垂足の真上と、4px ずれた位置
        const auto candidates = CollectSnapCandidates(builder.scene, mapping,
            At(mapping, {30.0 + offsetMm, 50.0, 0.0}), settings, Tolerance());
        const auto foot = std::find_if(candidates.begin(), candidates.end(),
            [](const SnapCandidate& candidate) { return candidate.kind == SnapKind::Perpendicular; });
        Require(foot != candidates.end(), "垂足は候補に出ること");
        RequireNear(foot->position.x, 30.0, 1e-9, "垂足そのもののx");
        Require(HasKind(candidates, SnapKind::ClosestOnCurve), "同じ線の最近点も候補に残ること");
        const auto chosen = ChooseSnap(candidates, settings);
        Require(chosen.has_value() && chosen->kind == SnapKind::ClosestOnCurve,
            "順位どおり最近点へ吸着すること(実際: "
                + std::string(chosen.has_value() ? SnapKindLabelJa(chosen->kind) : "候補なし")
                + ")");
        RequireNear(chosen->position.x, 30.0 + offsetMm, 1e-9, "ポインタ直下の線上");
        RequireNear(chosen->position.y, 50.0, 1e-9, "線の上");
    }
}

KACHA_V2_TEST(snap, 接点の上でも同じ円の最近点が統合仕様の順位どおり勝つ)
{
    SceneBuilder builder;
    builder.AddCircle({50, 50, 0}, 10.0);
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    SnapSettings settings;
    settings.referencePoint = Vector3{70.0, 50.0, 0.0};
    // 中心から 20mm 離れた基準点。接点は中心から見て 60° の方向で、四半点からは 50px 以上離れている。
    const Vector3 touch{55.0, 50.0 + 10.0 * std::sqrt(3.0) / 2.0, 0.0};

    const auto candidates =
        CollectSnapCandidates(builder.scene, mapping, At(mapping, touch), settings, Tolerance());
    Require(HasKind(candidates, SnapKind::Tangent), "接点は候補に出ること");
    const auto chosen = ChooseSnap(candidates, settings);
    Require(chosen.has_value() && chosen->kind == SnapKind::ClosestOnCurve,
        "順位どおり最近点へ吸着すること(実際: "
            + std::string(chosen.has_value() ? SnapKindLabelJa(chosen->kind) : "候補なし") + ")");
    RequireNear((chosen->position - touch).Length(), 0.0, 1e-6, "ポインタ直下の円上");
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
        SnapKind::GridMinor, SnapKind::FreeOnPlane, SnapKind::ScreenIntersection,
        SnapKind::GridOnCurve};
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

// ---------------------------------------------------------------- 吸着半径

KACHA_V2_TEST(snap, 吸着半径はズームに依らず画面の12pxで決まる)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {80, 50, 0});   // 端点 (80, 50)
    builder.EnablePlane();
    // 1mm = 10px と 1mm = 1px。どちらでも端から 11px なら吸着し、13px なら出ない。
    const ScreenMapping close = TopView();
    const ScreenMapping wide = MakeOrthographicMapping({50.0, 50.0, 0.0}, {0.0, 0.0, -1.0},
        {0.0, 1.0, 0.0}, 1000.0, 1000.0, 1000.0);
    RequireNear(wide.PixelsPerMillimeterAt({50.0, 50.0, 0.0}), 1.0, 1e-6, "引いた視点は1px/mm");
    for (const ScreenMapping* mapping : {&close, &wide}) {
        const double mmPerPx = 1.0 / mapping->PixelsPerMillimeterAt({80.0, 50.0, 0.0});
        const auto inside = CollectSnapCandidates(builder.scene, *mapping,
            At(*mapping, {80.0 + 11.0 * mmPerPx, 50.0, 0.0}), {}, Tolerance());
        const auto chosen = ChooseSnap(inside, {});
        Require(chosen.has_value() && chosen->kind == SnapKind::Endpoint,
            "11px なら端点へ吸着すること");
        RequireNear(chosen->screenDistancePx, 11.0, 1e-6, "画面距離はpxで測ること");

        const auto outside = CollectSnapCandidates(builder.scene, *mapping,
            At(*mapping, {80.0 + 13.0 * mmPerPx, 50.0, 0.0}), {}, Tolerance());
        Require(!HasKind(outside, SnapKind::Endpoint), "13px なら端点を出さないこと");
    }
}

KACHA_V2_TEST(snap, 吸着半径は点や線の拾い半径と別に効く)
{
    SceneBuilder builder;
    builder.AddLine({20, 50, 0}, {80, 50, 0});
    builder.EnablePlane();
    const ScreenMapping mapping = TopView();
    const ScreenPoint pointer = At(mapping, {81.1, 50.0, 0.0});   // 端から 11px

    GeometryTolerance narrow = Tolerance();
    narrow.snapPickPx = 4.0;
    // 点や線の拾い半径を広げても、スナップの半径は変わらない。
    narrow.displayPickPx = 50.0;
    narrow.edgePickPx = 50.0;
    Require(!HasKind(CollectSnapCandidates(builder.scene, mapping, pointer, {}, narrow),
                SnapKind::Endpoint),
        "snapPickPx を狭めれば 11px の端点は出ないこと");

    GeometryTolerance wide = Tolerance();
    wide.displayPickPx = 1.0;
    wide.edgePickPx = 1.0;
    Require(HasKind(CollectSnapCandidates(builder.scene, mapping, pointer, {}, wide),
                SnapKind::Endpoint),
        "点や線の拾い半径を狭めても 11px の端点は出ること");
}

// ---------------------------------------------------------------- ヒステリシス

KACHA_V2_TEST_MAIN("snap_tests")
