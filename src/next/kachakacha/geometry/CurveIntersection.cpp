#include "kachakacha/geometry/CurveIntersection.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>

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
