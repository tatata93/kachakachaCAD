#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::geometry {

namespace {

//! 弦 [t0,t1] の中点が曲線からどれだけ離れているか。
[[nodiscard]] double ChordError(const CurveSegment& segment, double t0, double t1)
{
    const Vector3 a = segment.Evaluate(t0);
    const Vector3 b = segment.Evaluate(t1);
    const Vector3 middle = segment.Evaluate(0.5 * (t0 + t1));
    const Vector3 chordMiddle = (a + b) * 0.5;
    return (middle - chordMiddle).Length();
}

void Subdivide(const CurveSegment& segment, double t0, double t1, double toleranceMm,
    int depth, int maximumPoints, std::vector<CurvePoint>& out)
{
    if (static_cast<int>(out.size()) >= maximumPoints || depth >= 16) {
        return;
    }
    if (ChordError(segment, t0, t1) <= toleranceMm) {
        return;
    }
    const double middle = 0.5 * (t0 + t1);
    Subdivide(segment, t0, middle, toleranceMm, depth + 1, maximumPoints, out);
    out.push_back(CurvePoint{middle, segment.Evaluate(middle)});
    Subdivide(segment, middle, t1, toleranceMm, depth + 1, maximumPoints, out);
}

[[nodiscard]] double SegmentPointDistance(const Vector3& point, const Vector3& a,
    const Vector3& b)
{
    const Vector3 direction = b - a;
    const double lengthSquared = direction.LengthSquared();
    if (lengthSquared <= 0.0) {
        return (point - a).Length();
    }
    double t = Dot(point - a, direction) / lengthSquared;
    t = std::clamp(t, 0.0, 1.0);
    return (point - (a + direction * t)).Length();
}

//! 2次元の線分どうしが交わるか。端点で触れるだけの場合も交差として扱う。
[[nodiscard]] bool SegmentsCross(const Point2& a0, const Point2& a1, const Point2& b0,
    const Point2& b1, double toleranceMm)
{
    const double d1u = a1.u - a0.u;
    const double d1v = a1.v - a0.v;
    const double d2u = b1.u - b0.u;
    const double d2v = b1.v - b0.v;
    const double denominator = d1u * d2v - d1v * d2u;
    const double du = b0.u - a0.u;
    const double dv = b0.v - a0.v;
    if (std::abs(denominator) < 1.0e-18) {
        // 平行。重なっているかどうかは端点の距離で見る。
        const auto near = [&](const Point2& p, const Point2& q0, const Point2& q1) {
            return SegmentPointDistance({p.u, p.v, 0.0}, {q0.u, q0.v, 0.0},
                       {q1.u, q1.v, 0.0})
                <= toleranceMm;
        };
        return near(a0, b0, b1) || near(a1, b0, b1) || near(b0, a0, a1) || near(b1, a0, a1);
    }
    const double t = (du * d2v - dv * d2u) / denominator;
    const double s = (du * d1v - dv * d1u) / denominator;
    return t >= 0.0 && t <= 1.0 && s >= 0.0 && s <= 1.0;
}

} // namespace

std::vector<CurvePoint> SampleCurve(const CurveSegment& segment, double toleranceMm,
    int maximumPoints)
{
    const double tolerance = toleranceMm > 0.0 ? toleranceMm : 1.0e-6;
    std::vector<CurvePoint> points;
    points.push_back(CurvePoint{0.0, segment.Evaluate(0.0)});
    // 円と閉曲線は、いきなり両端だけを見ると弦の誤差が0になって分割されない。
    // 先に4等分してから、必要なところをさらに割る。
    const double seeds[]{0.25, 0.5, 0.75};
    double previous = 0.0;
    for (const double seed : seeds) {
        Subdivide(segment, previous, seed, tolerance, 0, maximumPoints, points);
        points.push_back(CurvePoint{seed, segment.Evaluate(seed)});
        previous = seed;
    }
    Subdivide(segment, previous, 1.0, tolerance, 0, maximumPoints, points);
    points.push_back(CurvePoint{1.0, segment.Evaluate(1.0)});
    std::sort(points.begin(), points.end(),
        [](const CurvePoint& l, const CurvePoint& r) { return l.parameter < r.parameter; });
    return points;
}

std::vector<Vector3> SampleChain(const std::vector<CurveSegment>& segments,
    double toleranceMm)
{
    std::vector<Vector3> points;
    for (const CurveSegment& segment : segments) {
        const std::vector<CurvePoint> sampled = SampleCurve(segment, toleranceMm);
        for (const CurvePoint& point : sampled) {
            if (!points.empty()
                && (points.back() - point.position).Length() <= toleranceMm * 0.5) {
                continue;
            }
            points.push_back(point.position);
        }
    }
    return points;
}

void RemoveClosingDuplicate(std::vector<Vector3>& points, double toleranceMm)
{
    while (points.size() >= 2
        && (points.back() - points.front()).Length() <= std::max(toleranceMm, 1.0e-12)) {
        points.pop_back();
    }
}

std::vector<double> NormalizedArcLength(const std::vector<Vector3>& points)
{
    std::vector<double> parameters(points.size(), 0.0);
    if (points.size() < 2) {
        return parameters;
    }
    double total = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        total += (points[index] - points[index - 1]).Length();
        parameters[index] = total;
    }
    if (!(total > 0.0)) {
        return parameters;
    }
    for (double& value : parameters) {
        value /= total;
    }
    return parameters;
}

Vector3 PointAtNormalizedArcLength(const std::vector<Vector3>& points,
    const std::vector<double>& parameters, double t)
{
    if (points.empty()) {
        return {};
    }
    if (points.size() == 1 || parameters.size() != points.size()) {
        return points.front();
    }
    const double clamped = std::clamp(t, 0.0, 1.0);
    for (std::size_t index = 1; index < parameters.size(); ++index) {
        if (clamped <= parameters[index]) {
            const double span = parameters[index] - parameters[index - 1];
            const double local = span > 0.0 ? (clamped - parameters[index - 1]) / span : 0.0;
            return points[index - 1] + (points[index] - points[index - 1]) * local;
        }
    }
    return points.back();
}

Vector3 Centroid(const std::vector<Vector3>& points)
{
    if (points.empty()) {
        return {};
    }
    Vector3 sum{};
    for (const Vector3& point : points) {
        sum = sum + point;
    }
    return sum * (1.0 / static_cast<double>(points.size()));
}

PlaneFit FitPlane(const std::vector<Vector3>& points)
{
    PlaneFit fit;
    if (points.size() < 3) {
        return fit;
    }
    fit.origin = Centroid(points);
    // 共分散行列の最小固有ベクトルが法線。3x3なので、べき乗法の逆版ではなく
    // 「各座標軸を法線候補にした行列式」から素直に作る。
    double xx = 0.0, xy = 0.0, xz = 0.0, yy = 0.0, yz = 0.0, zz = 0.0;
    for (const Vector3& point : points) {
        const Vector3 d = point - fit.origin;
        xx += d.x * d.x;
        xy += d.x * d.y;
        xz += d.x * d.z;
        yy += d.y * d.y;
        yz += d.y * d.z;
        zz += d.z * d.z;
    }
    const double detX = yy * zz - yz * yz;
    const double detY = xx * zz - xz * xz;
    const double detZ = xx * yy - xy * xy;
    const double best = std::max({detX, detY, detZ});
    if (!(best > 0.0)) {
        return fit; // 全部同じ点、または一直線
    }
    Vector3 normal;
    if (best == detX) {
        normal = {detX, xz * yz - xy * zz, xy * yz - xz * yy};
    } else if (best == detY) {
        normal = {xz * yz - xy * zz, detY, xy * xz - yz * xx};
    } else {
        normal = {xy * yz - xz * yy, xy * xz - yz * xx, detZ};
    }
    const double length = normal.Length();
    if (!(length > 0.0)) {
        return fit;
    }
    fit.normal = normal * (1.0 / length);
    for (const Vector3& point : points) {
        fit.maximumDeviationMm =
            std::max(fit.maximumDeviationMm, std::abs(Dot(point - fit.origin, fit.normal)));
    }
    fit.valid = true;
    return fit;
}

PlanarFrame MakeFrame(const PlaneFit& fit)
{
    PlanarFrame frame;
    frame.origin = fit.origin;
    frame.normal = fit.normal;
    // 法線と最も平行でない座標軸を選んでから直交化する。
    const Vector3 axes[]{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    const Vector3* chosen = &axes[0];
    double smallest = std::abs(Dot(fit.normal, axes[0]));
    for (int index = 1; index < 3; ++index) {
        const double value = std::abs(Dot(fit.normal, axes[index]));
        if (value < smallest) {
            smallest = value;
            chosen = &axes[index];
        }
    }
    Vector3 u = *chosen - fit.normal * Dot(*chosen, fit.normal);
    const double length = u.Length();
    if (length > 0.0) {
        u = u * (1.0 / length);
    }
    frame.uDirection = u;
    frame.vDirection = Cross(fit.normal, u);
    return frame;
}

std::vector<Point2> ProjectToFrame(const std::vector<Vector3>& points,
    const PlanarFrame& frame)
{
    std::vector<Point2> projected;
    projected.reserve(points.size());
    for (const Vector3& point : points) {
        const Vector3 d = point - frame.origin;
        projected.push_back(Point2{Dot(d, frame.uDirection), Dot(d, frame.vDirection)});
    }
    return projected;
}

double SignedArea(const std::vector<Point2>& loop)
{
    if (loop.size() < 3) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t index = 0; index < loop.size(); ++index) {
        const Point2& a = loop[index];
        const Point2& b = loop[(index + 1) % loop.size()];
        sum += a.u * b.v - b.u * a.v;
    }
    return 0.5 * sum;
}

bool ContainsPoint(const std::vector<Point2>& loop, const Point2& point)
{
    if (loop.size() < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t index = 0, previous = loop.size() - 1; index < loop.size();
        previous = index++) {
        const Point2& a = loop[index];
        const Point2& b = loop[previous];
        const bool straddles = (a.v > point.v) != (b.v > point.v);
        if (!straddles) {
            continue;
        }
        const double denominator = b.v - a.v;
        if (denominator == 0.0) {
            continue;
        }
        const double crossU = a.u + (point.v - a.v) / denominator * (b.u - a.u);
        if (point.u < crossU) {
            inside = !inside;
        }
    }
    return inside;
}

bool HasSelfIntersection(const std::vector<Point2>& loop, double toleranceMm)
{
    const std::size_t count = loop.size();
    if (count < 4) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Point2& a0 = loop[i];
        const Point2& a1 = loop[(i + 1) % count];
        for (std::size_t j = i + 1; j < count; ++j) {
            // 隣どうし、および最初と最後は端点を共有しているので飛ばす。
            if (j == i || j == (i + 1) % count || (j + 1) % count == i) {
                continue;
            }
            const Point2& b0 = loop[j];
            const Point2& b1 = loop[(j + 1) % count];
            if (SegmentsCross(a0, a1, b0, b1, toleranceMm)) {
                return true;
            }
        }
    }
    return false;
}

bool LoopsIntersect(const std::vector<Point2>& first, const std::vector<Point2>& second,
    double toleranceMm)
{
    if (first.size() < 2 || second.size() < 2) {
        return false;
    }
    for (std::size_t i = 0; i < first.size(); ++i) {
        const Point2& a0 = first[i];
        const Point2& a1 = first[(i + 1) % first.size()];
        for (std::size_t j = 0; j < second.size(); ++j) {
            const Point2& b0 = second[j];
            const Point2& b1 = second[(j + 1) % second.size()];
            if (SegmentsCross(a0, a1, b0, b1, toleranceMm)) {
                return true;
            }
        }
    }
    return false;
}

namespace {

//! 線分どうしの最短距離と、それぞれの線分上の位置(0..1)。
//! 平行・退化を別扱いにしないと、交差しているのに大きな距離を返してしまう。
struct SegmentApproach {
    double distance = 0.0;
    double firstLocal = 0.0;
    double secondLocal = 0.0;
};

[[nodiscard]] SegmentApproach ClosestBetweenSegments(const Vector3& p0, const Vector3& p1,
    const Vector3& q0, const Vector3& q1)
{
    const Vector3 u = p1 - p0;
    const Vector3 v = q1 - q0;
    const Vector3 w = p0 - q0;
    const double a = Dot(u, u);
    const double b = Dot(u, v);
    const double c = Dot(v, v);
    const double d = Dot(u, w);
    const double e = Dot(v, w);
    const double denominator = a * c - b * b;

    double s = 0.0;
    double t = 0.0;
    if (denominator > 1.0e-18) {
        s = std::clamp((b * e - c * d) / denominator, 0.0, 1.0);
    } else if (a > 0.0) {
        s = std::clamp(-d / a, 0.0, 1.0);
    }
    if (c > 0.0) {
        t = std::clamp((b * s + e) / c, 0.0, 1.0);
        // t を丸めたぶん、s をもう一度合わせる。片側だけ丸めると端で誤差が出る。
        if (a > 0.0) {
            s = std::clamp((b * t - d) / a, 0.0, 1.0);
        }
    }
    SegmentApproach approach;
    approach.firstLocal = s;
    approach.secondLocal = t;
    approach.distance = ((p0 + u * s) - (q0 + v * t)).Length();
    return approach;
}

} // namespace

PolylineApproach ClosestApproachBetween(const std::vector<Vector3>& first,
    const std::vector<Vector3>& second)
{
    PolylineApproach best;
    if (first.empty() || second.empty()) {
        return best;
    }
    const std::vector<double> firstParameters = NormalizedArcLength(first);
    const std::vector<double> secondParameters = NormalizedArcLength(second);
    const std::size_t firstEdges = first.size() > 1 ? first.size() - 1 : 1;
    const std::size_t secondEdges = second.size() > 1 ? second.size() - 1 : 1;
    for (std::size_t i = 0; i < firstEdges; ++i) {
        const Vector3& p0 = first[i];
        const Vector3& p1 = first.size() > 1 ? first[i + 1] : first[i];
        for (std::size_t j = 0; j < secondEdges; ++j) {
            const Vector3& q0 = second[j];
            const Vector3& q1 = second.size() > 1 ? second[j + 1] : second[j];
            const SegmentApproach approach = ClosestBetweenSegments(p0, p1, q0, q1);
            if (best.valid && approach.distance >= best.distanceMm) {
                continue;
            }
            best.valid = true;
            best.distanceMm = approach.distance;
            best.firstPoint = p0 + (p1 - p0) * approach.firstLocal;
            best.secondPoint = q0 + (q1 - q0) * approach.secondLocal;
            const double f0 = firstParameters[i];
            const double f1 = first.size() > 1 ? firstParameters[i + 1] : f0;
            const double s0 = secondParameters[j];
            const double s1 = second.size() > 1 ? secondParameters[j + 1] : s0;
            best.firstParameter = f0 + (f1 - f0) * approach.firstLocal;
            best.secondParameter = s0 + (s1 - s0) * approach.secondLocal;
        }
    }
    return best;
}

double MinimumDistanceBetween(const std::vector<Vector3>& first,
    const std::vector<Vector3>& second)
{
    const PolylineApproach approach = ClosestApproachBetween(first, second);
    return approach.valid ? approach.distanceMm : 0.0;
}

double MaximumDeviationTo(const std::vector<Vector3>& probe,
    const std::vector<Vector3>& reference)
{
    if (probe.empty() || reference.empty()) {
        return 0.0;
    }
    double largest = 0.0;
    for (const Vector3& point : probe) {
        double smallest = (point - reference.front()).Length();
        for (std::size_t index = 1; index < reference.size(); ++index) {
            smallest = std::min(smallest,
                SegmentPointDistance(point, reference[index - 1], reference[index]));
        }
        largest = std::max(largest, smallest);
    }
    return largest;
}

} // namespace kachakacha::v2::geometry
