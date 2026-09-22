#include "kachakacha/app/HoverEditPlan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Vector3;

namespace {

constexpr const char* kPickMissing = "UI-H001";

[[nodiscard]] std::string Millimeters(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.3f mm", value);
    return buffer;
}

[[nodiscard]] std::string Degrees(double fraction)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f°", fraction * 360.0);
    return buffer;
}

[[nodiscard]] std::string KindNameJa(const CurveSegment& curve)
{
    switch (curve.Kind()) {
    case geometry::CurveKind::Line:         return "直線";
    case geometry::CurveKind::CircularArc:  return "円弧";
    case geometry::CurveKind::Circle:       return "円";
    case geometry::CurveKind::CubicBezier:  return "ベジェ";
    case geometry::CurveKind::CubicBSpline: return "スプライン";
    }
    return "線";
}

//! 押した線分の番号。無ければ値なし。
[[nodiscard]] std::optional<std::size_t> SegmentIndexOf(const WireCurves& wire,
    const base::SegmentId& segmentId)
{
    for (std::size_t index = 0; index < wire.segmentIds.size(); ++index) {
        if (wire.segmentIds[index] == segmentId) {
            return index;
        }
    }
    return std::nullopt;
}

//! 境目 = 場面の全部の線から、押した線分そのものを除いたもの。
[[nodiscard]] std::vector<CurveSegment> BoundariesFor(const modeling::SnapScene& scene,
    const HoverEditPick& pick)
{
    std::vector<CurveSegment> boundaries;
    for (const modeling::SnapCurve& curve : scene.curves) {
        if (curve.entityId == pick.entityId && curve.segmentId == pick.segmentId) {
            continue;
        }
        boundaries.push_back(curve.segment);
    }
    return boundaries;
}

//! 押した線分より前の線分と後の線分に、残った部分を足して鎖にする。
//! 閉じた折れ線で前後の両方が残るなら、後 → 前 とつないで 1 本の開いた鎖にする。
[[nodiscard]] std::vector<std::vector<CurveSegment>> ChainsAround(const WireCurves& wire,
    std::size_t index, const std::vector<CurveSegment>& before,
    const std::vector<CurveSegment>& after)
{
    std::vector<CurveSegment> head(wire.segments.begin(),
        wire.segments.begin() + static_cast<std::ptrdiff_t>(index));
    head.insert(head.end(), before.begin(), before.end());
    std::vector<CurveSegment> tail = after;
    tail.insert(tail.end(), wire.segments.begin() + static_cast<std::ptrdiff_t>(index) + 1,
        wire.segments.end());
    std::vector<std::vector<CurveSegment>> chains;
    if (wire.closedLoop && wire.segments.size() > 1 && !head.empty() && !tail.empty()) {
        tail.insert(tail.end(), head.begin(), head.end());
        chains.push_back(std::move(tail));
        return chains;
    }
    if (!head.empty()) {
        chains.push_back(std::move(head));
    }
    if (!tail.empty()) {
        chains.push_back(std::move(tail));
    }
    return chains;
}

struct Located {
    WireCurves wire;
    std::size_t index = 0;
};

[[nodiscard]] Result<Located> Locate(const modeling::SnapScene& scene, const HoverEditPick& pick,
    const geometry::GeometryTolerance& tolerance)
{
    const auto wire = WireCurvesOf(scene, pick.entityId, tolerance);
    if (!wire.has_value()) {
        return Result<Located>::Failure(MakeError(kPickMissing, "押した線が場面にありません。",
            "線の上を押してください。"));
    }
    const auto index = SegmentIndexOf(*wire, pick.segmentId);
    if (!index.has_value()) {
        return Result<Located>::Failure(MakeError(kPickMissing, "押した線分がその線にありません。", {}));
    }
    return Result<Located>::Success(Located{*wire, *index});
}

[[nodiscard]] std::string ChainsWordJa(std::size_t count)
{
    if (count == 0) {
        return "線ごと消えます";
    }
    return std::to_string(count) + " 本になります";
}

} // namespace

std::optional<WireCurves> WireCurvesOf(const modeling::SnapScene& scene,
    const base::EntityId& entityId, const geometry::GeometryTolerance& tolerance)
{
    WireCurves wire;
    wire.entityId = entityId;
    for (const modeling::SnapCurve& curve : scene.curves) {
        if (!(curve.entityId == entityId)) {
            continue;
        }
        wire.segmentIds.push_back(curve.segmentId);
        wire.segments.push_back(curve.segment);
        wire.construction = curve.construction;
        wire.datum = curve.datum;
    }
    if (wire.segments.empty()) {
        return std::nullopt;
    }
    const double joinMm = std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm);
    wire.closedLoop = wire.segments.size() == 1
        ? wire.segments.front().IsClosed(joinMm)
        : geometry::Distance(wire.segments.back().EndPoint(), wire.segments.front().StartPoint())
            <= joinMm;
    return wire;
}

Result<HoverEditOutcome> PlanTrim(const modeling::SnapScene& scene, const HoverEditPick& pick,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<HoverEditOutcome>;
    const auto located = Locate(scene, pick, tolerance);
    if (!located.HasValue()) {
        return Out::Failure(located.Diagnostics());
    }
    const CurveSegment& target = located.Value().wire.segments[located.Value().index];
    const auto cuts = geometry::CutPointsOn(target, BoundariesFor(scene, pick), tolerance);
    const auto span = geometry::TrimSpanAround(target, cuts, pick.parameter, tolerance);
    if (!span.HasValue()) {
        return Out::Failure(span.Diagnostics());
    }
    const auto remainder = geometry::RemoveSpan(target, span.Value());
    if (!remainder.HasValue()) {
        return Out::Failure(remainder.Diagnostics());
    }
    HoverEditOutcome outcome;
    outcome.source = located.Value().wire;
    outcome.segmentIndex = located.Value().index;
    outcome.chains = ChainsAround(outcome.source, outcome.segmentIndex, remainder.Value().before,
        remainder.Value().after);
    outcome.previewLine = geometry::SampleSpan(target, span.Value());
    const double lengthMm = geometry::SpanLengthMm(target, span.Value(), tolerance);
    const std::string kind = KindNameJa(target);
    if (span.Value().whole) {
        outcome.summaryJa = "交わる線が無いので、" + kind + "ごと消えます(" + Millimeters(lengthMm) + ")。";
    } else if (span.Value().closed) {
        double fraction = span.Value().high - span.Value().low;
        fraction -= std::floor(fraction);
        outcome.summaryJa = "円の " + Degrees(fraction <= 1.0e-9 ? 1.0 : fraction)
            + " が消えて円弧になります。";
    } else {
        outcome.summaryJa = kind + "の " + Millimeters(lengthMm) + " が消えます。"
            + ChainsWordJa(outcome.chains.size()) + "。";
    }
    outcome.footerJa = "トリム: " + kind + " / CUT=" + Millimeters(lengthMm) + " / →"
        + ChainsWordJa(outcome.chains.size());
    return Out::Success(std::move(outcome));
}

Result<HoverEditOutcome> PlanExtend(const modeling::SnapScene& scene, const HoverEditPick& pick,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<HoverEditOutcome>;
    const auto located = Locate(scene, pick, tolerance);
    if (!located.HasValue()) {
        return Out::Failure(located.Diagnostics());
    }
    const WireCurves& wire = located.Value().wire;
    if (wire.closedLoop) {
        return Out::Failure(MakeError(geometry::kExtendClosed, "閉じた線は延ばせません。",
            "円や閉じた折れ線には端がありません。"));
    }
    // 伸ばせるのは線の両端だけ。押した点に近い方の端。
    const Vector3 start = wire.segments.front().StartPoint();
    const Vector3 end = wire.segments.back().EndPoint();
    const bool atStart = geometry::Distance(pick.point, start) < geometry::Distance(pick.point, end);
    const std::size_t index = atStart ? 0 : wire.segments.size() - 1;
    const int endpoint = atStart ? 0 : 1;
    HoverEditPick endPick = pick;
    endPick.segmentId = wire.segmentIds[index];
    const auto extended = geometry::ExtendToNearestBoundary(wire.segments[index], endpoint,
        BoundariesFor(scene, endPick), tolerance);
    if (!extended.HasValue()) {
        return Out::Failure(extended.Diagnostics());
    }
    HoverEditOutcome outcome;
    outcome.source = wire;
    outcome.segmentIndex = index;
    std::vector<CurveSegment> chain = wire.segments;
    chain[index] = extended.Value().extended;
    outcome.chains.push_back(std::move(chain));
    // 下見は伸びた部分だけ。
    const CurveSegment& grown = extended.Value().extended;
    const Vector3 tip = endpoint == 0 ? wire.segments[index].StartPoint()
                                      : wire.segments[index].EndPoint();
    const double tipParameter = grown.ClosestPoint(tip).parameter;
    const double from = endpoint == 0 ? 0.0 : tipParameter;
    const double to = endpoint == 0 ? tipParameter : 1.0;
    for (int k = 0; k <= 24; ++k) {
        outcome.previewLine.push_back(grown.Evaluate(from + (to - from) * k / 24.0));
    }
    outcome.summaryJa = KindNameJa(grown) + "の" + (atStart ? "始点" : "終点") + "が "
        + Millimeters(extended.Value().addedMm) + " 伸びて、相手の線に届きます。";
    outcome.footerJa = "延長: " + KindNameJa(grown) + " / ADD=" + Millimeters(extended.Value().addedMm);
    return Out::Success(std::move(outcome));
}

Result<HoverEditOutcome> PlanSplit(const modeling::SnapScene& scene, const HoverEditPick& pick,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<HoverEditOutcome>;
    const auto located = Locate(scene, pick, tolerance);
    if (!located.HasValue()) {
        return Out::Failure(located.Diagnostics());
    }
    const CurveSegment& target = located.Value().wire.segments[located.Value().index];
    const auto cuts = geometry::CutPointsOn(target, BoundariesFor(scene, pick), tolerance);
    const auto nearest = geometry::NearestCut(target, cuts, pick.parameter);
    if (!nearest.has_value()) {
        return Out::Failure(MakeError(geometry::kSplitNoCut, "この線と交わる線が無いので、分ける点がありません。",
            "交わる線を引くか、別の線を押してください。"));
    }
    const auto pieces = geometry::SplitAtCut(target, *nearest);
    if (!pieces.HasValue()) {
        return Out::Failure(pieces.Diagnostics());
    }
    HoverEditOutcome outcome;
    outcome.source = located.Value().wire;
    outcome.segmentIndex = located.Value().index;
    std::vector<CurveSegment> before{pieces.Value().first};
    std::vector<CurveSegment> after;
    if (pieces.Value().second.has_value()) {
        after.push_back(*pieces.Value().second);
    }
    if (target.Kind() == geometry::CurveKind::Circle) {
        // 円は継ぎ目を入れた 1 本の円弧になる(2 本には分かれない)。
        outcome.chains.push_back(before);
    } else {
        // 分割は 2 本に分ける。閉じた折れ線なら、その点で開いた 1 本(ChainsAround が前後をつなぐ)。
        std::vector<CurveSegment> head(outcome.source.segments.begin(),
            outcome.source.segments.begin() + static_cast<std::ptrdiff_t>(outcome.segmentIndex));
        head.insert(head.end(), before.begin(), before.end());
        std::vector<CurveSegment> tail = after;
        tail.insert(tail.end(),
            outcome.source.segments.begin() + static_cast<std::ptrdiff_t>(outcome.segmentIndex) + 1,
            outcome.source.segments.end());
        if (outcome.source.closedLoop && outcome.source.segments.size() > 1) {
            tail.insert(tail.end(), head.begin(), head.end());
            outcome.chains.push_back(std::move(tail));
        } else {
            outcome.chains.push_back(std::move(head));
            outcome.chains.push_back(std::move(tail));
        }
    }
    outcome.previewPoint = nearest->point;
    outcome.summaryJa = KindNameJa(target) + "を交点で " + ChainsWordJa(outcome.chains.size()) + "。";
    outcome.footerJa = "分割: " + KindNameJa(target) + " / AT=(" + Millimeters(nearest->point.x) + ", "
        + Millimeters(nearest->point.y) + ", " + Millimeters(nearest->point.z) + ") / →"
        + ChainsWordJa(outcome.chains.size());
    return Out::Success(std::move(outcome));
}

} // namespace kachakacha::v2::app
