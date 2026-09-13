#include "kachakacha/geometry/CurveIntersection.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace kachakacha::v2::geometry {

namespace {


//! 粗い点列で近い場所を見つけ、そこから細かく詰める。
//! 解析解を種類ごとに書き分けると、種類が増えるたびに組合せが増えて必ず抜けが出る。
//! ここは共通の詰め方1つで通す。
struct Bracket {
    double firstParameter = 0.0;
    double secondParameter = 0.0;
    double distance = 0.0;
};

[[nodiscard]] double DistanceAt(const CurveSegment& first, const CurveSegment& second,
    double s, double t)
{
    return (first.Evaluate(std::clamp(s, 0.0, 1.0)) - second.Evaluate(std::clamp(t, 0.0, 1.0)))
        .Length();
}

//! 局所的に最小へ詰める。座標降下を交互に行う。
void Refine(const CurveSegment& first, const CurveSegment& second, Bracket& bracket,
    double initialStep)
{
    double step = initialStep;
    for (int iteration = 0; iteration < 60 && step > 1.0e-12; ++iteration) {
        bool improved = false;
        const double candidates[4][2]{{step, 0.0}, {-step, 0.0}, {0.0, step}, {0.0, -step}};
        for (const auto& delta : candidates) {
            const double s = std::clamp(bracket.firstParameter + delta[0], 0.0, 1.0);
            const double t = std::clamp(bracket.secondParameter + delta[1], 0.0, 1.0);
            const double distance = DistanceAt(first, second, s, t);
            if (distance < bracket.distance) {
                bracket.firstParameter = s;
                bracket.secondParameter = t;
                bracket.distance = distance;
                improved = true;
            }
        }
        if (!improved) {
            step *= 0.5;
        }
    }
}

//! 近づいている場所をすべて拾う。同じ場所を何度も返さない。
[[nodiscard]] std::vector<Bracket> FindApproaches(const CurveSegment& first,
    const CurveSegment& second, double samplingToleranceMm, double acceptMm)
{
    const std::vector<CurvePoint> firstPoints = SampleCurve(first, samplingToleranceMm);
    const std::vector<CurvePoint> secondPoints = SampleCurve(second, samplingToleranceMm);
    std::vector<Bracket> brackets;
    // 粗い格子で局所最小になっている組を種にする。
    for (std::size_t a = 0; a < firstPoints.size(); ++a) {
        for (std::size_t b = 0; b < secondPoints.size(); ++b) {
            const double distance =
                (firstPoints[a].position - secondPoints[b].position).Length();
            bool minimal = true;
            for (int da = -1; da <= 1 && minimal; ++da) {
                for (int db = -1; db <= 1 && minimal; ++db) {
                    if (da == 0 && db == 0) {
                        continue;
                    }
                    const std::ptrdiff_t na = static_cast<std::ptrdiff_t>(a) + da;
                    const std::ptrdiff_t nb = static_cast<std::ptrdiff_t>(b) + db;
                    if (na < 0 || nb < 0
                        || na >= static_cast<std::ptrdiff_t>(firstPoints.size())
                        || nb >= static_cast<std::ptrdiff_t>(secondPoints.size())) {
                        continue;
                    }
                    const double other =
                        (firstPoints[static_cast<std::size_t>(na)].position
                            - secondPoints[static_cast<std::size_t>(nb)].position)
                            .Length();
                    if (other < distance) {
                        minimal = false;
                    }
                }
            }
            if (!minimal) {
                continue;
            }
            Bracket bracket{firstPoints[a].parameter, secondPoints[b].parameter, distance};
            Refine(first, second, bracket, 0.05);
            if (bracket.distance > acceptMm) {
                continue;
            }
            const bool known = std::any_of(brackets.begin(), brackets.end(),
                [&](const Bracket& other) {
                    return std::abs(other.firstParameter - bracket.firstParameter) < 1.0e-6
                        && std::abs(other.secondParameter - bracket.secondParameter) < 1.0e-6;
                });
            if (!known) {
                brackets.push_back(bracket);
            }
        }
    }
    return brackets;
}

[[nodiscard]] double SamplingToleranceMm(const CurveSegment& first, const CurveSegment& second,
    const GeometryTolerance& tolerance)
{
    const double size = std::max(first.TotalLength(tolerance.modelLinearMm),
        second.TotalLength(tolerance.modelLinearMm));
    return std::max(size / 200.0, tolerance.modelLinearMm * 10.0);
}

} // namespace

std::vector<CurveIntersection> IntersectCurves(const CurveSegment& first,
    const CurveSegment& second, const GeometryTolerance& tolerance)
{
    const double sampling = SamplingToleranceMm(first, second, tolerance);
    std::vector<CurveIntersection> results;
    for (const Bracket& bracket :
        FindApproaches(first, second, sampling, tolerance.modelLinearMm)) {
        CurveIntersection intersection;
        intersection.firstParameter = bracket.firstParameter;
        intersection.secondParameter = bracket.secondParameter;
        intersection.gapMm = bracket.distance;
        intersection.real = true;
        intersection.position = (first.Evaluate(bracket.firstParameter)
                                    + second.Evaluate(bracket.secondParameter))
            * 0.5;
        // 閉じた曲線は t=0 と t=1 が同じ場所なので、同じ交点が2回出る。
        // 位置で見て、すでにある交点と同じなら足さない。
        const double sameSpot = std::max(tolerance.modelLinearMm * 100.0, 1.0e-6);
        const bool known = std::any_of(results.begin(), results.end(),
            [&](const CurveIntersection& other) {
                return (other.position - intersection.position).Length() <= sameSpot;
            });
        if (!known) {
            results.push_back(intersection);
        }
    }
    return results;
}

std::vector<CurveIntersection> IntersectCurvesOnScreen(const CurveSegment& first,
    const CurveSegment& second, const ScreenMapping& mapping, double screenTolerancePx,
    const GeometryTolerance& tolerance)
{
    // 画面上での交差は、投影した点列どうしで見る。
    const double sampling = SamplingToleranceMm(first, second, tolerance);
    const std::vector<CurvePoint> firstPoints = SampleCurve(first, sampling);
    const std::vector<CurvePoint> secondPoints = SampleCurve(second, sampling);
    std::vector<CurveIntersection> results;
    for (std::size_t a = 0; a + 1 < firstPoints.size(); ++a) {
        const auto a0 = mapping.Project(firstPoints[a].position);
        const auto a1 = mapping.Project(firstPoints[a + 1].position);
        if (!a0.has_value() || !a1.has_value()) {
            continue;
        }
        for (std::size_t b = 0; b + 1 < secondPoints.size(); ++b) {
            const auto b0 = mapping.Project(secondPoints[b].position);
            const auto b1 = mapping.Project(secondPoints[b + 1].position);
            if (!b0.has_value() || !b1.has_value()) {
                continue;
            }
            // 2つの線分が画面上で交わるか。
            const double d1x = a1->x - a0->x;
            const double d1y = a1->y - a0->y;
            const double d2x = b1->x - b0->x;
            const double d2y = b1->y - b0->y;
            const double denominator = d1x * d2y - d1y * d2x;
            if (std::abs(denominator) < 1.0e-12) {
                continue;
            }
            const double dx = b0->x - a0->x;
            const double dy = b0->y - a0->y;
            const double s = (dx * d2y - dy * d2x) / denominator;
            const double t = (dx * d1y - dy * d1x) / denominator;
            if (s < 0.0 || s > 1.0 || t < 0.0 || t > 1.0) {
                continue;
            }
            const double firstParameter = firstPoints[a].parameter
                + (firstPoints[a + 1].parameter - firstPoints[a].parameter) * s;
            const double secondParameter = secondPoints[b].parameter
                + (secondPoints[b + 1].parameter - secondPoints[b].parameter) * t;
            Bracket bracket{firstParameter, secondParameter,
                DistanceAt(first, second, firstParameter, secondParameter)};
            Refine(first, second, bracket, 0.02);

            CurveIntersection intersection;
            intersection.firstParameter = bracket.firstParameter;
            intersection.secondParameter = bracket.secondParameter;
            intersection.gapMm = bracket.distance;
            intersection.real = bracket.distance <= tolerance.modelLinearMm;
            intersection.position = intersection.real
                ? (first.Evaluate(bracket.firstParameter)
                      + second.Evaluate(bracket.secondParameter))
                    * 0.5
                : first.Evaluate(firstParameter);
            const bool known = std::any_of(results.begin(), results.end(),
                [&](const CurveIntersection& other) {
                    return (other.position - intersection.position).Length()
                        <= tolerance.interactiveJoinMm;
                });
            if (!known) {
                results.push_back(intersection);
            }
        }
    }
    (void)screenTolerancePx;
    return results;
}

ClosestPointResult ClosestOnCurve(const CurveSegment& segment, const Vector3& point)
{
    return segment.ClosestPoint(point);
}

std::vector<Vector3> QuadrantPoints(const CurveSegment& segment)
{
    std::vector<Vector3> points;
    switch (segment.Kind()) {
    case CurveKind::Circle: {
        for (int index = 0; index < 4; ++index) {
            points.push_back(segment.Evaluate(static_cast<double>(index) / 4.0));
        }
        break;
    }
    case CurveKind::CircularArc: {
        // 掃引の中で 90 度ごとの位置。0度・90度・180度・270度に当たるところ。
        const double start = segment.StartAngleRad();
        const double sweep = segment.SweepAngleRad();
        if (std::abs(sweep) < 1.0e-12) {
            break;
        }
        const double direction = sweep > 0.0 ? 1.0 : -1.0;
        for (int quarter = -8; quarter <= 8; ++quarter) {
            const double angle = static_cast<double>(quarter) * (kPi * 0.5);
            const double t = (angle - start) / sweep;
            if (t <= 0.0 || t >= 1.0) {
                continue;
            }
            points.push_back(segment.Evaluate(t));
        }
        (void)direction;
        break;
    }
    default:
        for (int index = 1; index < 4; ++index) {
            points.push_back(segment.Evaluate(static_cast<double>(index) / 4.0));
        }
        break;
    }
    return points;
}

namespace {

struct ScreenSegmentApproach {
    double distancePx = 0.0;
    double fraction = 0.0;
};

//! 点と画面上の線分の距離(px)と、線分上の割合。
[[nodiscard]] ScreenSegmentApproach ApproachToScreenSegment(const ScreenPoint& point,
    const ScreenPoint& start, const ScreenPoint& end)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (!(lengthSquared > 0.0)) {
        const double ex = point.x - start.x;
        const double ey = point.y - start.y;
        return {std::sqrt(ex * ex + ey * ey), 0.0};
    }
    double t = ((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = start.x + t * dx;
    const double cy = start.y + t * dy;
    const double ex = point.x - cx;
    const double ey = point.y - cy;
    return {std::sqrt(ex * ex + ey * ey), t};
}

//! クリップ座標の w(視点からの奥行きに比例する)。平行投影では一定。
[[nodiscard]] double ClipW(const ScreenMapping& mapping, const Vector3& world)
{
    const auto& m = mapping.matrix;
    return m[12] * world.x + m[13] * world.y + m[14] * world.z + m[15];
}

//! 曲線上のパラメータ t の点が、画面でポインタから何px離れて見えるか。写せなければ無限大。
[[nodiscard]] double ScreenDistanceAt(const CurveSegment& segment, const ScreenMapping& mapping,
    const ScreenPoint& pointer, double t)
{
    const auto screen = mapping.Project(segment.Evaluate(std::clamp(t, 0.0, 1.0)));
    return screen.has_value() ? ScreenDistance(*screen, pointer)
                              : std::numeric_limits<double>::infinity();
}

//! 曲線の一区間を必ず囲む凸包の頂点(3D)。点数は2〜4で、先頭と末尾は区間の両端の曲線上の点。
struct CurveHull {
    std::array<Vector3, 4> points{};
    int count = 0;
};

//! 3次Bezierのブロッサム値。B(a,a,a)、B(a,a,b)、B(a,b,b)、B(b,b,b) が区間 [a,b] だけの制御点になる。
[[nodiscard]] Vector3 Blossom(const std::array<Vector3, 4>& control, double u1, double u2,
    double u3)
{
    const auto lerp = [](const Vector3& a, const Vector3& b, double u) {
        return a + (b - a) * u;
    };
    const Vector3 q0 = lerp(control[0], control[1], u1);
    const Vector3 q1 = lerp(control[1], control[2], u1);
    const Vector3 q2 = lerp(control[2], control[3], u1);
    return lerp(lerp(q0, q1, u2), lerp(q1, q2, u2), u3);
}

[[nodiscard]] CurveHull BezierHull(const std::array<Vector3, 4>& control, double u0, double u1)
{
    CurveHull hull;
    hull.points = {Blossom(control, u0, u0, u0), Blossom(control, u0, u0, u1),
        Blossom(control, u0, u1, u1), Blossom(control, u1, u1, u1)};
    hull.count = 4;
    return hull;
}

//! 区間 [t0, t1] の曲線を囲む凸包。
//! - 直線: 両端。
//! - 円・円弧: 両端と、両端の接線の交点の三角形。区間の掃引が90度以下であること(InitialHullSpans)。
//! - 3次Bezier: 区間だけの制御点。曲線は制御点の凸包の中にある。
//! - 一様3次B-spline: 区分をBezierへ直してから同じく区間の制御点。区間は区分をまたがないこと。
[[nodiscard]] CurveHull HullOf(const CurveSegment& segment, double t0, double t1)
{
    CurveHull hull;
    switch (segment.Kind()) {
    case CurveKind::Line:
        hull.points[0] = segment.Evaluate(t0);
        hull.points[1] = segment.Evaluate(t1);
        hull.count = 2;
        break;
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        const double halfSweep = 0.5 * segment.SweepAngleRad() * (t1 - t0);
        const Vector3 middle = segment.Evaluate(0.5 * (t0 + t1));
        hull.points[0] = segment.Evaluate(t0);
        hull.points[1] = segment.Center()
            + (middle - segment.Center()) * (1.0 / std::cos(halfSweep));
        hull.points[2] = segment.Evaluate(t1);
        hull.count = 3;
        break;
    }
    case CurveKind::CubicBezier: {
        const auto& c = segment.ControlPoints();
        hull = BezierHull({c[0], c[1], c[2], c[3]}, t0, t1);
        break;
    }
    case CurveKind::CubicBSpline: {
        // CurveSegment::Evaluate と同じ区分け。
        const auto& c = segment.ControlPoints();
        const std::size_t spans = c.size() - 3;
        const double scale = static_cast<double>(spans);
        const std::size_t span = std::min(
            static_cast<std::size_t>(std::floor(0.5 * (t0 + t1) * scale)), spans - 1);
        const Vector3& p0 = c[span];
        const Vector3& p1 = c[span + 1];
        const Vector3& p2 = c[span + 2];
        const Vector3& p3 = c[span + 3];
        const std::array<Vector3, 4> bezier{(p0 + p1 * 4.0 + p2) * (1.0 / 6.0),
            (p1 * 2.0 + p2) * (1.0 / 3.0), (p1 + p2 * 2.0) * (1.0 / 3.0),
            (p1 + p2 * 4.0 + p3) * (1.0 / 6.0)};
        const double offset = static_cast<double>(span);
        hull = BezierHull(bezier, t0 * scale - offset, t1 * scale - offset);
        break;
    }
    }
    return hull;
}

//! 探索を始める区間。円・円弧は掃引90度以下ずつ、B-splineは区分ごと、ほかは全体。
[[nodiscard]] std::vector<std::pair<double, double>> InitialHullSpans(
    const CurveSegment& segment)
{
    std::size_t count = 1;
    switch (segment.Kind()) {
    case CurveKind::CircularArc:
    case CurveKind::Circle:
        count = static_cast<std::size_t>(std::max(1.0,
            std::ceil(std::abs(segment.SweepAngleRad()) / (kPi * 0.5) - 1.0e-9)));
        break;
    case CurveKind::CubicBSpline:
        count = segment.ControlPoints().size() - 3;
        break;
    default:
        break;
    }
    std::vector<std::pair<double, double>> spans;
    spans.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        spans.emplace_back(static_cast<double>(index) / static_cast<double>(count),
            index + 1 == count ? 1.0
                               : static_cast<double>(index + 1) / static_cast<double>(count));
    }
    return spans;
}

//! (b - a) × (p - a)。
[[nodiscard]] double Cross2(const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

//! 画面へ写した凸包までの距離。凸包の中なら0。
//! 平面の点集合の凸包は、その中の3点でできる三角形の和であり(カラテオドリ)、
//! 外の点から凸包までの距離は、2点を結ぶ線分までの距離の最小に等しい。
//! 3点が一直線に並ぶ三角形は、その直線上の点を中と見なす(下限としては安全側)。
[[nodiscard]] double DistanceToScreenHull(const std::array<ScreenPoint, 4>& points, int count,
    const ScreenPoint& pointer)
{
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            for (int k = j + 1; k < count; ++k) {
                const double d1 = Cross2(points[i], points[j], pointer);
                const double d2 = Cross2(points[j], points[k], pointer);
                const double d3 = Cross2(points[k], points[i], pointer);
                const bool negative = d1 < 0.0 || d2 < 0.0 || d3 < 0.0;
                const bool positive = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
                if (!(negative && positive)) {
                    return 0.0;
                }
            }
        }
    }
    double nearest = std::numeric_limits<double>::infinity();
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            nearest = std::min(nearest,
                ApproachToScreenSegment(pointer, points[i], points[j]).distancePx);
        }
    }
    return nearest;
}

//! 探索中の区間。lowerBoundPx は、この区間の曲線がポインタへ近づける保証された下限。
struct HullSpan {
    double startParameter = 0.0;
    double endParameter = 0.0;
    double lowerBoundPx = 0.0;
    int depth = 0;
};

//! 分割の深さと回数の上限。数値が尽きる所で必ず止めるためのもので、
//! 通常の曲線はこの手前で kCurveScreenApproachTolerancePx の差まで詰まる。
constexpr int kHullMaximumDepth = 52;
constexpr int kHullMaximumSplits = 8192;

} // namespace

std::optional<CurveScreenApproach> ApproachToCurveOnScreen(const CurveSegment& segment,
    const ScreenMapping& mapping, const ScreenPoint& pointer, double maximumDistancePx)
{
    // 上限側: 実際に曲線上の点を写して測った距離のうち、いちばん近いもの。
    CurveScreenApproach best{std::numeric_limits<double>::infinity(), 0.0, Vector3{}};
    double bestSpanWidth = 1.0;
    const auto consider = [&](double t, double spanWidth) {
        const double clamped = std::clamp(t, 0.0, 1.0);
        const double distance = ScreenDistanceAt(segment, mapping, pointer, clamped);
        if (distance < best.distancePx) {
            best = CurveScreenApproach{distance, clamped, segment.Evaluate(clamped)};
            bestSpanWidth = spanWidth;
        }
    };

    // 下限側: 区間の凸包を画面へ写し、そこまでの距離を測る。
    // 視点の前(w > 0)にある凸集合は、投影で凸集合へ写り、凸包の像は像の凸包になる。
    // したがって写した凸包までの距離は、その区間の曲線までの画面距離を超えない。
    std::vector<HullSpan> open;
    const auto later = [](const HullSpan& l, const HullSpan& r) {
        return l.lowerBoundPx > r.lowerBoundPx;
    };
    const auto push = [&](double t0, double t1, int depth) {
        const CurveHull hull = HullOf(segment, t0, t1);
        const auto count = static_cast<std::size_t>(hull.count);
        std::array<ScreenPoint, 4> screen{};
        std::array<double, 4> w{};
        bool anyInFront = false;
        for (std::size_t index = 0; index < count; ++index) {
            w[index] = ClipW(mapping, hull.points[index]);
            // ScreenMapping::Project と同じ境目。これ以下の点は画面へ写らない。
            anyInFront = anyInFront || w[index] > 1.0e-12;
        }
        if (!anyInFront) {
            // 凸包ごと視点の後ろ。w は1次式なので、区間の曲線もどこも写らない。
            return;
        }
        bool bounded = true;
        for (std::size_t index = 0; index < count; ++index) {
            const auto projected = mapping.Project(hull.points[index]);
            if (!projected.has_value()) {
                bounded = false;
                break;
            }
            screen[index] = *projected;
        }
        const double width = t1 - t0;
        consider(t0, width);
        consider(t1, width);
        // 凸包の一部がカメラの後ろにかかると下限を出せない。0 と見なして割り続ける。
        double lowerBound = 0.0;
        if (bounded) {
            // 両端を結ぶ画面上の弦の最寄りを、奥行きで3D上の割合へ戻して試す。
            // 直線では弦が曲線そのものなので、これが正確な答えになる。
            const auto last = static_cast<std::size_t>(hull.count - 1);
            const double f = ApproachToScreenSegment(pointer, screen[0], screen[last]).fraction;
            const double s = f * w[0] / ((1.0 - f) * w[last] + f * w[0]);
            consider(t0 + width * s, width);
            lowerBound = DistanceToScreenHull(screen, hull.count, pointer);
        }
        if (lowerBound > maximumDistancePx) {
            return;   // この区間のどこも範囲の外
        }
        open.push_back(HullSpan{t0, t1, lowerBound, depth});
        std::push_heap(open.begin(), open.end(), later);
    };

    for (const auto& [t0, t1] : InitialHullSpans(segment)) {
        push(t0, t1, 0);
    }
    // 下限の小さい区間から割る。いちばん小さい下限が最良の実距離と許容差以内になれば、
    // それより近い点はどの区間にも残っていない。
    int splits = 0;
    while (!open.empty()) {
        std::pop_heap(open.begin(), open.end(), later);
        const HullSpan span = open.back();
        open.pop_back();
        if (span.lowerBoundPx + kCurveScreenApproachTolerancePx >= best.distancePx) {
            break;
        }
        if (span.depth >= kHullMaximumDepth || splits >= kHullMaximumSplits) {
            continue;
        }
        ++splits;
        const double middle = 0.5 * (span.startParameter + span.endParameter);
        push(span.startParameter, middle, span.depth + 1);
        push(middle, span.endParameter, span.depth + 1);
    }
    if (!std::isfinite(best.distancePx) || best.distancePx > maximumDistancePx) {
        return std::nullopt;
    }

    // 仕上げ。見つけた点のまわり(最後に詰めた区間の幅)で、さらに近い点があれば採る。
    // 近くなったときだけ置き換えるので、上の探索で決まった距離の保証は崩れない。
    // 距離の差がごく小さくても曲線に沿った位置は大きく動くことがあるので、位置を揃えるために行う。
    constexpr double kInverseGolden = 0.6180339887498949;
    double low = std::max(0.0, best.parameter - bestSpanWidth);
    double high = std::min(1.0, best.parameter + bestSpanWidth);
    double left = high - kInverseGolden * (high - low);
    double right = low + kInverseGolden * (high - low);
    double leftDistance = ScreenDistanceAt(segment, mapping, pointer, left);
    double rightDistance = ScreenDistanceAt(segment, mapping, pointer, right);
    for (int iteration = 0; iteration < 100 && high - low > 1.0e-16; ++iteration) {
        if (leftDistance <= rightDistance) {
            high = right;
            right = left;
            rightDistance = leftDistance;
            left = high - kInverseGolden * (high - low);
            leftDistance = ScreenDistanceAt(segment, mapping, pointer, left);
        } else {
            low = left;
            left = right;
            leftDistance = rightDistance;
            right = low + kInverseGolden * (high - low);
            rightDistance = ScreenDistanceAt(segment, mapping, pointer, right);
        }
    }
    consider(left, bestSpanWidth);
    consider(right, bestSpanWidth);
    return best;
}

bool CurveLiesInPlane(const CurveSegment& segment, const Vector3& planeOrigin,
    const Vector3& planeNormal, double toleranceMm)
{
    const double normalLength = planeNormal.Length();
    if (!(normalLength > 0.0)) {
        return false;
    }
    const Vector3 normal = planeNormal * (1.0 / normalLength);
    const auto withinPlane = [&](const Vector3& point) {
        return std::abs(Dot(point - planeOrigin, normal)) <= toleranceMm;
    };
    switch (segment.Kind()) {
    case CurveKind::Line:
        return withinPlane(segment.StartPoint()) && withinPlane(segment.EndPoint());
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        // 円周上の点の平面からの距離は、中心のずれ + 半径 × sin(傾き) × cos(角度)。
        // 最大は両者の和なので、別々ではなく和で比べる(円弧では安全側の判定になる)。
        const double axisLength = segment.Normal().Length();
        if (!(axisLength > 0.0)) {
            return false;
        }
        const double tilt = Cross(segment.Normal() * (1.0 / axisLength), normal).Length();
        const double centerOffset = std::abs(Dot(segment.Center() - planeOrigin, normal));
        return centerOffset + segment.Radius() * tilt <= toleranceMm;
    }
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline:
        return std::all_of(segment.ControlPoints().begin(), segment.ControlPoints().end(),
            withinPlane);
    }
    return false;
}

std::optional<Vector3> ExtensionPoint(const CurveSegment& segment, const Vector3& point,
    double maximumExtensionMm)
{
    switch (segment.Kind()) {
    case CurveKind::Line: {
        const Vector3 start = segment.StartPoint();
        const Vector3 end = segment.EndPoint();
        const Vector3 direction = Normalized(end - start);
        if (!(direction.Length() > 0.0)) {
            return std::nullopt;
        }
        const double along = Dot(point - start, direction);
        const double length = (end - start).Length();
        if (along >= 0.0 && along <= length) {
            return std::nullopt;   // 線の内側。延長線ではない
        }
        const double overshoot = along < 0.0 ? -along : along - length;
        if (overshoot > maximumExtensionMm) {
            return std::nullopt;
        }
        return start + direction * along;
    }
    case CurveKind::CircularArc: {
        // 同じ中心・半径・平面のまま、掃引の外側へ回した位置。
        const Vector3 center = segment.Center();
        const Vector3 normal = segment.Normal();
        const Vector3 reference = segment.ReferenceDirection();
        const Vector3 planar = (point - center) - normal * Dot(point - center, normal);
        if (!(planar.Length() > 0.0)) {
            return std::nullopt;
        }
        const Vector3 unit = Normalized(planar);
        const double angle = std::atan2(Dot(unit, Cross(normal, reference)),
            Dot(unit, reference));
        const double start = segment.StartAngleRad();
        const double sweep = segment.SweepAngleRad();
        const double t = (angle - start) / sweep;
        if (t >= 0.0 && t <= 1.0) {
            return std::nullopt;   // 円弧の内側
        }
        const double overshootRad = t < 0.0 ? -t * std::abs(sweep)
                                            : (t - 1.0) * std::abs(sweep);
        if (overshootRad * segment.Radius() > maximumExtensionMm) {
            return std::nullopt;
        }
        return center + unit * segment.Radius();
    }
    default:
        return std::nullopt;   // 円と自由曲線は延長線を出さない
    }
}

std::vector<Vector3> PerpendicularFeet(const CurveSegment& segment, const Vector3& point,
    const GeometryTolerance& tolerance)
{
    std::vector<Vector3> feet;
    const double sampling = std::max(segment.TotalLength(tolerance.modelLinearMm) / 200.0,
        tolerance.modelLinearMm * 10.0);
    const std::vector<CurvePoint> sampled = SampleCurve(segment, sampling);
    // 接線と (点 - 曲線上の点) の内積が0になるところ。符号が変わる区間を詰める。
    const auto value = [&](double t) {
        const Vector3 position = segment.Evaluate(t);
        return Dot(segment.FirstDerivative(t), point - position);
    };
    for (std::size_t index = 1; index < sampled.size(); ++index) {
        const double t0 = sampled[index - 1].parameter;
        const double t1 = sampled[index].parameter;
        double v0 = value(t0);
        double v1 = value(t1);
        if (v0 == 0.0) {
            feet.push_back(segment.Evaluate(t0));
            continue;
        }
        if (!((v0 < 0.0) != (v1 < 0.0))) {
            continue;
        }
        double low = t0;
        double high = t1;
        for (int iteration = 0; iteration < 80; ++iteration) {
            const double middle = 0.5 * (low + high);
            const double v = value(middle);
            if ((v < 0.0) != (v0 < 0.0)) {
                high = middle;
                v1 = v;
            } else {
                low = middle;
                v0 = v;
            }
        }
        feet.push_back(segment.Evaluate(0.5 * (low + high)));
    }
    return feet;
}

std::vector<Vector3> TangentPoints(const CurveSegment& segment, const Vector3& point,
    const GeometryTolerance& tolerance)
{
    std::vector<Vector3> points;
    const double sampling = std::max(segment.TotalLength(tolerance.modelLinearMm) / 200.0,
        tolerance.modelLinearMm * 10.0);
    const std::vector<CurvePoint> sampled = SampleCurve(segment, sampling);
    // 接線が (点 - 曲線上の点) と平行になるところ。外積の大きさが0。
    const auto value = [&](double t) {
        const Vector3 position = segment.Evaluate(t);
        return Cross(segment.FirstDerivative(t), point - position).Length();
    };
    for (std::size_t index = 1; index + 1 < sampled.size(); ++index) {
        const double previous = value(sampled[index - 1].parameter);
        const double current = value(sampled[index].parameter);
        const double next = value(sampled[index + 1].parameter);
        if (!(current <= previous && current <= next)) {
            continue;
        }
        double low = sampled[index - 1].parameter;
        double high = sampled[index + 1].parameter;
        for (int iteration = 0; iteration < 60; ++iteration) {
            const double third = (high - low) / 3.0;
            const double a = low + third;
            const double b = high - third;
            if (value(a) < value(b)) {
                high = b;
            } else {
                low = a;
            }
        }
        const double t = 0.5 * (low + high);
        if (value(t) <= tolerance.modelLinearMm * 1000.0) {
            points.push_back(segment.Evaluate(t));
        }
    }
    return points;
}

} // namespace kachakacha::v2::geometry
