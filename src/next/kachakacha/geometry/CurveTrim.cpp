#include "kachakacha/geometry/CurveTrim.h"

#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr double kTwoPi = 6.283185307179586;
//! 区切りどうし・端との「同じ場所」の幅(t)。
constexpr double kSameParameter = 1.0e-9;

[[nodiscard]] bool IsCircle(const CurveSegment& curve)
{
    return curve.Kind() == CurveKind::Circle;
}

//! 円の t を 0〜1 に畳む。
[[nodiscard]] double Wrap01(double t)
{
    double wrapped = t - std::floor(t);
    if (wrapped >= 1.0) {
        wrapped -= 1.0;
    }
    return wrapped;
}

//! 円の t0 から t1 まで進む区間の円弧(t1 ≤ t0 なら 0 をまたぐ)。
[[nodiscard]] Result<CurveSegment> CircleArcBetween(const CurveSegment& circle, double t0,
    double t1)
{
    double fraction = Wrap01(t1 - t0);
    if (fraction <= kSameParameter) {
        fraction = 1.0;   // 同じ場所へ戻る = 一周
    }
    const double sign = circle.SweepAngleRad() >= 0.0 ? 1.0 : -1.0;
    return CurveSegment::MakeCircularArc(circle.Center(), circle.Normal(),
        circle.ReferenceDirection(), circle.Radius(),
        circle.StartAngleRad() + circle.SweepAngleRad() * Wrap01(t0), sign * kTwoPi * fraction);
}

} // namespace

std::vector<CutPoint> CutPointsOn(const CurveSegment& target,
    const std::vector<CurveSegment>& boundaries, const GeometryTolerance& tolerance)
{
    std::vector<CutPoint> cuts;
    const bool closed = IsCircle(target);
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        for (const CurveIntersection& hit : IntersectCurves(target, boundaries[index], tolerance)) {
            double t = hit.firstParameter;
            if (closed) {
                t = Wrap01(t);
            } else if (t <= kSameParameter || t >= 1.0 - kSameParameter) {
                continue;   // 端で触れているだけ(隣の線との継ぎ目)。区切りにしない
            }
            cuts.push_back(CutPoint{t, index, target.Evaluate(std::clamp(t, 0.0, 1.0))});
        }
    }
    std::sort(cuts.begin(), cuts.end(),
        [](const CutPoint& a, const CutPoint& b) { return a.parameter < b.parameter; });
    std::vector<CutPoint> unique;
    for (const CutPoint& cut : cuts) {
        if (unique.empty() || cut.parameter - unique.back().parameter > kSameParameter) {
            unique.push_back(cut);
        }
    }
    // 円は 0 と 1 が同じ場所。両端に同じ区切りが残っていれば片方を落とす。
    if (closed && unique.size() >= 2
        && (1.0 - unique.back().parameter) + unique.front().parameter <= kSameParameter) {
        unique.pop_back();
    }
    return unique;
}

Result<TrimSpan> TrimSpanAround(const CurveSegment& target, const std::vector<CutPoint>& cuts,
    double t, const GeometryTolerance& tolerance)
{
    (void)tolerance;
    using Out = Result<TrimSpan>;
    if (!IsFinite(t) || t < -kSameParameter || t > 1.0 + kSameParameter) {
        return Out::Failure(MakeError("GEO-E022", "押した場所が線の上にありません。",
            "線の上の、消したい側を押してください。"));
    }
    TrimSpan span;
    span.closed = IsCircle(target);
    if (cuts.empty()) {
        span.whole = true;
        return Out::Success(span);
    }
    if (span.closed) {
        if (cuts.size() < 2) {
            return Out::Failure(MakeError(kTrimOneCutOnCircle,
                "円と交わる線が 1 か所しかないので、消す区間が決まりません。",
                "円を横切る線をもう 1 本引くか、円ごと消すなら選んで削除してください。"));
        }
        const double wrapped = Wrap01(t);
        // wrapped を挟む隣り合う区切り(周期)。
        std::size_t index = 0;
        while (index < cuts.size() && cuts[index].parameter <= wrapped) {
            ++index;
        }
        // index = wrapped より大きい最初の区切り。無ければ先頭(0 をまたぐ)。
        const std::size_t highIndex = index < cuts.size() ? index : 0;
        const std::size_t lowIndex = index == 0 ? cuts.size() - 1 : index - 1;
        span.low = cuts[lowIndex].parameter;
        span.high = cuts[highIndex].parameter;
        span.lowCut = cuts[lowIndex];
        span.highCut = cuts[highIndex];
        return Out::Success(span);
    }
    const double clamped = std::clamp(t, 0.0, 1.0);
    span.low = 0.0;
    span.high = 1.0;
    for (const CutPoint& cut : cuts) {
        if (cut.parameter <= clamped) {
            span.low = cut.parameter;
            span.lowCut = cut;
        } else {
            span.high = cut.parameter;
            span.highCut = cut;
            break;
        }
    }
    return Out::Success(span);
}

Result<TrimRemainder> RemoveSpan(const CurveSegment& target, const TrimSpan& span)
{
    using Out = Result<TrimRemainder>;
    TrimRemainder remainder;
    if (span.whole) {
        return Out::Success(remainder);
    }
    if (span.closed) {
        const auto arc = CircleArcBetween(target, span.high, span.low);
        if (!arc.HasValue()) {
            return Out::Failure(arc.Diagnostics());
        }
        remainder.after.push_back(arc.Value());
        return Out::Success(remainder);
    }
    if (span.low > kSameParameter) {
        const auto split = target.Split(span.low);
        if (!split.HasValue()) {
            return Out::Failure(split.Diagnostics());
        }
        remainder.before.push_back(*split.Value().first);
    }
    if (span.high < 1.0 - kSameParameter) {
        const auto split = target.Split(span.high);
        if (!split.HasValue()) {
            return Out::Failure(split.Diagnostics());
        }
        remainder.after.push_back(*split.Value().second);
    }
    return Out::Success(remainder);
}

std::vector<Vector3> SampleSpan(const CurveSegment& target, const TrimSpan& span, int count)
{
    std::vector<Vector3> points;
    const int steps = std::max(count, 2);
    if (span.whole) {
        for (int k = 0; k <= steps; ++k) {
            points.push_back(target.Evaluate(static_cast<double>(k) / steps));
        }
        return points;
    }
    if (span.closed) {
        double fraction = Wrap01(span.high - span.low);
        if (fraction <= kSameParameter) {
            fraction = 1.0;
        }
        for (int k = 0; k <= steps; ++k) {
            points.push_back(target.Evaluate(Wrap01(span.low + fraction * k / steps)));
        }
        return points;
    }
    for (int k = 0; k <= steps; ++k) {
        points.push_back(target.Evaluate(span.low + (span.high - span.low) * k / steps));
    }
    return points;
}

double SpanLengthMm(const CurveSegment& target, const TrimSpan& span,
    const GeometryTolerance& tolerance)
{
    const double step = std::max(tolerance.modelLinearMm, 1.0e-6);
    if (span.whole) {
        return target.TotalLength(step);
    }
    if (span.closed) {
        double fraction = Wrap01(span.high - span.low);
        if (fraction <= kSameParameter) {
            fraction = 1.0;
        }
        return target.TotalLength(step) * fraction;
    }
    return target.ArcLength(span.low, span.high, step);
}

Result<ExtendResult> ExtendToNearestBoundary(const CurveSegment& target, int endpointIndex,
    const std::vector<CurveSegment>& boundaries, const GeometryTolerance& tolerance)
{
    using Out = Result<ExtendResult>;
    if (target.IsClosed(tolerance.modelLinearMm)) {
        return Out::Failure(MakeError(kExtendClosed, "閉じた線は延ばせません。",
            "円や閉じた形には端がありません。"));
    }
    const double step = std::max(tolerance.modelLinearMm, 1.0e-6);
    const Vector3 tip = endpointIndex == 0 ? target.StartPoint() : target.EndPoint();
    // 一度だけ十分に伸ばし、伸びた部分と境目の交点のうち、いちばん近いものを採る。
    double reach = std::max(target.TotalLength(step) * 10.0, 1.0);
    for (const CurveSegment& boundary : boundaries) {
        reach = std::max(reach, boundary.ClosestPoint(tip).distance * 4.0);
    }
    reach = std::min(reach, 1.0e5);
    if (target.Kind() == CurveKind::CircularArc) {
        // 円弧は一周まで。
        const double rest = (kTwoPi - std::abs(target.SweepAngleRad())) * target.Radius();
        reach = std::min(reach, std::max(rest, 0.0));
        if (!(reach > step)) {
            return Out::Failure(MakeError(kExtendClosed, "この円弧はもう一周しています。", {}));
        }
    }
    const auto extended = ExtendCurve(target, endpointIndex, reach);
    if (!extended.HasValue()) {
        return Out::Failure(extended.Diagnostics());
    }
    const double tipParameter = extended.Value().ClosestPoint(tip).parameter;
    std::optional<ExtendResult> best;
    for (std::size_t index = 0; index < boundaries.size(); ++index) {
        for (const CurveIntersection& hit :
            IntersectCurves(extended.Value(), boundaries[index], tolerance)) {
            const bool forward = endpointIndex == 0 ? hit.firstParameter < tipParameter
                                                    : hit.firstParameter > tipParameter;
            if (!forward) {
                continue;
            }
            const double added = extended.Value().ArcLength(std::min(tipParameter, hit.firstParameter),
                std::max(tipParameter, hit.firstParameter), step);
            if (added <= std::max(tolerance.interactiveJoinMm, step)) {
                continue;   // 端そのもの
            }
            if (!best.has_value() || added < best->addedMm) {
                const auto grown = ExtendCurve(target, endpointIndex, added);
                if (!grown.HasValue()) {
                    continue;
                }
                best = ExtendResult{grown.Value(), added, index,
                    extended.Value().Evaluate(hit.firstParameter)};
            }
        }
    }
    if (!best.has_value()) {
        return Out::Failure(MakeError("GEO-E001", "延ばしても相手に届きません。",
            "その端の先に、交わる線がありません。"));
    }
    return Out::Success(*best);
}

std::optional<CutPoint> NearestCut(const CurveSegment& target, const std::vector<CutPoint>& cuts,
    double t)
{
    if (cuts.empty() || !IsFinite(t)) {
        return std::nullopt;
    }
    const Vector3 pressed = target.Evaluate(std::clamp(t, 0.0, 1.0));
    std::optional<CutPoint> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (const CutPoint& cut : cuts) {
        const double distance = Distance(cut.point, pressed);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = cut;
        }
    }
    return best;
}

Result<SplitPieces> SplitAtCut(const CurveSegment& target, const CutPoint& cut)
{
    using Out = Result<SplitPieces>;
    if (IsCircle(target)) {
        const auto seam = CircleArcBetween(target, cut.parameter, cut.parameter);
        if (!seam.HasValue()) {
            return Out::Failure(seam.Diagnostics());
        }
        return Out::Success(SplitPieces{seam.Value(), std::nullopt});
    }
    const auto split = target.Split(cut.parameter);
    if (!split.HasValue()) {
        return Out::Failure(split.Diagnostics());
    }
    return Out::Success(SplitPieces{*split.Value().first, *split.Value().second});
}

} // namespace kachakacha::v2::geometry
