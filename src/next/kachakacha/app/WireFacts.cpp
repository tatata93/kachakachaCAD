//! 線の事実の行。見出しは WireFacts.h。

#include "kachakacha/app/WireFacts.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <ios>
#include <limits>
#include <sstream>

namespace kachakacha::v2::app {
namespace {

using geometry::CurveKind;
using geometry::CurveSegment;
using geometry::Vector3;
using modeling::GuideTableSelection;

[[nodiscard]] std::string Mm(double value)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << value;
    return out.str();
}

[[nodiscard]] Vector3 ChainStart(const std::vector<CurveSegment>& chain)
{
    return chain.front().StartPoint();
}
[[nodiscard]] Vector3 ChainEnd(const std::vector<CurveSegment>& chain)
{
    return chain.back().EndPoint();
}

//! 面にする(LoopFaces)と同じ、端点をまとめる許容差。
[[nodiscard]] double JoinMmOf(const geometry::GeometryTolerance& tolerance)
{
    return std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm * 100.0);
}

[[nodiscard]] double ChainLength(const std::vector<CurveSegment>& chain, double tolerance)
{
    double length = 0.0;
    for (const CurveSegment& segment : chain) {
        length += segment.TotalLength(tolerance);
    }
    return length;
}

//! 座標軸に直交する平面(x = c / y = c / z = c)に載るか。載る軸(0/1/2)と値。
struct AxisPlane {
    int axis = -1;
    double value = 0.0;
    double deviationMm = 0.0;
};

[[nodiscard]] double Component(const Vector3& p, int axis)
{
    return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}

//! 載る軸の平面を全部(直線は 2 つの軸平面に載ることがある)。
[[nodiscard]] std::vector<AxisPlane> AxisPlanesOf(const std::vector<Vector3>& points, double tolMm)
{
    std::vector<AxisPlane> planes;
    for (int axis = 0; axis < 3; ++axis) {
        double low = std::numeric_limits<double>::infinity();
        double high = -low;
        for (const Vector3& p : points) {
            low = std::min(low, Component(p, axis));
            high = std::max(high, Component(p, axis));
        }
        if (high - low <= tolMm) {
            AxisPlane plane;
            plane.axis = axis;
            plane.value = 0.5 * (low + high);
            plane.deviationMm = high - low;
            planes.push_back(plane);
        }
    }
    return planes;
}

[[nodiscard]] std::string AxisPlaneNameJa(const AxisPlane& plane, double tolMm)
{
    static constexpr const char* kOnOrigin[3] = {"側面 YZ", "正面 XZ", "上面 XY"};
    static constexpr const char* kParallel[3] = {"YZ に平行(x = ", "XZ に平行(y = ", "XY に平行(z = "};
    return std::abs(plane.value) <= tolMm
        ? std::string(kOnOrigin[plane.axis])
        : std::string(kParallel[plane.axis]) + Mm(plane.value) + " mm)";
}

[[nodiscard]] WirePlaneFact PlaneFactOf(const std::vector<CurveSegment>& chain,
    const geometry::GeometryTolerance& tolerance)
{
    WirePlaneFact fact;
    const double tolMm = JoinMmOf(tolerance);
    const std::vector<Vector3> points = geometry::SampleChain(chain, tolMm * 0.1);
    if (points.size() < 2) {
        fact.nameJa = "なし";
        return fact;
    }
    const std::vector<AxisPlane> axes = AxisPlanesOf(points, tolMm);
    if (!axes.empty()) {
        Vector3 normal{};
        (axes.front().axis == 0 ? normal.x : axes.front().axis == 1 ? normal.y : normal.z) = 1.0;
        fact.normal = normal;
        for (const AxisPlane& plane : axes) {
            fact.deviationMm = std::max(fact.deviationMm, plane.deviationMm);
            fact.nameJa += (fact.nameJa.empty() ? "" : "・") + AxisPlaneNameJa(plane, tolMm);
        }
        return fact;
    }
    const geometry::PlaneFit fit = geometry::FitPlane(points);
    if (!fit.valid) {
        // 一直線(直線)で、軸の平面のどれにも載っていない: 3D に斜めの直線。
        fact.nameJa = "なし(斜めの直線)";
        return fact;
    }
    fact.deviationMm = fit.maximumDeviationMm;
    if (fit.maximumDeviationMm <= tolMm) {
        fact.normal = fit.normal;
        fact.nameJa = "傾いた平面";
        return fact;
    }
    fact.nameJa = "なし(3D の線、平面から " + Mm(fit.maximumDeviationMm) + " mm)";
    return fact;
}

//! 端 p が wires[other] の途中に乗っているか(端と端は除く)。乗っていればその距離。
[[nodiscard]] std::optional<double> OnMiddleOf(const GuideTableSelection& other, const Vector3& p,
    double joinMm)
{
    const auto& chain = other.segments;
    const bool closed = (ChainStart(chain) - ChainEnd(chain)).Length() <= joinMm;
    if (!closed && ((p - ChainStart(chain)).Length() <= joinMm
                       || (p - ChainEnd(chain)).Length() <= joinMm)) {
        return std::nullopt;
    }
    for (const CurveSegment& segment : chain) {
        const auto closest = segment.ClosestPoint(p);
        if (closest.distance <= joinMm) {
            return closest.distance;
        }
    }
    return std::nullopt;
}

[[nodiscard]] WireEndFact EndFactOf(const std::vector<GuideTableSelection>& wires,
    std::size_t target, const Vector3& p, double joinMm, double nearMm, bool movable)
{
    WireEndFact fact;
    fact.movable = movable;
    // 1. 端と端。いちばん近い相手の端。
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t other = 0; other < wires.size(); ++other) {
        if (other == target || wires[other].segments.empty()) {
            continue;
        }
        const auto& chain = wires[other].segments;
        const bool closed = (ChainStart(chain) - ChainEnd(chain)).Length() <= joinMm;
        if (closed) {
            continue;   // 円や閉じた輪郭に「端」は無い
        }
        for (const bool atEnd : {false, true}) {
            const Vector3 q = atEnd ? ChainEnd(chain) : ChainStart(chain);
            const double d = (p - q).Length();
            if (d < best) {
                best = d;
                fact.other = other;
                fact.otherAtEnd = atEnd;
                fact.distanceMm = d;
                fact.target = q;
            }
        }
    }
    if (best <= joinMm) {
        fact.relation = WireEndRelation::Joined;
        return fact;
    }
    // 2. 途中に乗っている(T 字)。端と端が届いていないときだけ。
    for (std::size_t other = 0; other < wires.size(); ++other) {
        if (other == target || wires[other].segments.empty()) {
            continue;
        }
        if (const auto d = OnMiddleOf(wires[other], p, joinMm); d.has_value()) {
            fact.relation = WireEndRelation::OnMiddle;
            fact.other = other;
            fact.otherAtEnd = false;
            fact.distanceMm = *d;
            fact.target = p;
            return fact;
        }
    }
    // 3. 近いが届いていない(ずれ)。
    if (best <= nearMm) {
        fact.relation = WireEndRelation::Near;
        return fact;
    }
    fact.relation = WireEndRelation::Free;
    fact.distanceMm = std::isfinite(best) ? best : 0.0;
    return fact;
}

} // namespace

WireFacts DescribeWire(const std::vector<GuideTableSelection>& wires, std::size_t target,
    const geometry::GeometryTolerance& tolerance)
{
    WireFacts facts;
    if (target >= wires.size() || wires[target].segments.empty()) {
        facts.plane.nameJa = "なし";
        return facts;
    }
    const auto& chain = wires[target].segments;
    const double joinMm = JoinMmOf(tolerance);
    facts.plane = PlaneFactOf(chain, tolerance);
    facts.lengthMm = ChainLength(chain, tolerance.modelLinearMm);
    facts.closed = (ChainStart(chain) - ChainEnd(chain)).Length() <= joinMm;
    if (facts.closed) {
        return facts;
    }
    // ずれと見なす上限は、面にする(LoopFaces の near-miss)と同じ。
    double longest = 0.0;
    for (const GuideTableSelection& wire : wires) {
        if (!wire.segments.empty()) {
            longest = std::max(longest, ChainLength(wire.segments, tolerance.modelLinearMm));
        }
    }
    const double nearMm = std::max(joinMm * 10.0, longest * 0.25);
    const bool movable = std::all_of(chain.begin(), chain.end(),
        [](const CurveSegment& segment) { return segment.Kind() == CurveKind::Line; });
    facts.start = EndFactOf(wires, target, ChainStart(chain), joinMm, nearMm, movable);
    facts.end = EndFactOf(wires, target, ChainEnd(chain), joinMm, nearMm, movable);
    return facts;
}

std::string WireEndTextJa(const std::vector<GuideTableSelection>& wires, const WireEndFact& fact)
{
    const std::string other = fact.other < wires.size() ? wires[fact.other].label : std::string("?");
    switch (fact.relation) {
    case WireEndRelation::Joined:
        return other + " の" + (fact.otherAtEnd ? "終点" : "始点") + "(" + Mm(fact.distanceMm) + " mm)";
    case WireEndRelation::OnMiddle:
        return other + " の途中(T 字, " + Mm(fact.distanceMm) + " mm)";
    case WireEndRelation::Near:
        return other + " の" + (fact.otherAtEnd ? "終点" : "始点") + "まで " + Mm(fact.distanceMm)
            + " mm 離れています";
    case WireEndRelation::Free:
        break;
    }
    return "なし(何にも触れていません)";
}

std::string WireFactsTextJa(const std::vector<GuideTableSelection>& wires, const WireFacts& facts)
{
    std::string text = "載る面: " + facts.plane.nameJa + "   長さ " + Mm(facts.lengthMm) + " mm";
    if (facts.closed) {
        return text + "\n閉じた線(端はありません)";
    }
    return text + "\n始点 → " + WireEndTextJa(wires, facts.start) + "\n終点 → "
        + WireEndTextJa(wires, facts.end);
}

} // namespace kachakacha::v2::app
