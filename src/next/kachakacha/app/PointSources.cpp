#include "kachakacha/app/PointSources.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <cmath>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;
using geometry::CurveKind;

} // namespace

std::string PointSourceKindNameJa(PointSourceKind kind)
{
    switch (kind) {
    case PointSourceKind::MeasuredPoint:    return "測った点";
    case PointSourceKind::MeasuredMidpoint: return "測った2点の中点";
    case PointSourceKind::CurveCenter:      return "中心";
    case PointSourceKind::CurveStart:       return "始点";
    case PointSourceKind::CurveEnd:         return "終点";
    case PointSourceKind::CurveMidpoint:    return "中点";
    case PointSourceKind::ControlPoint:     return "制御点";
    case PointSourceKind::ClosestApproach:  return "最も近づく場所";
    }
    return "不明";
}

std::vector<PointCandidate> PointCandidatesOfCurve(const CurveSegment& curve,
    std::optional<EntityId> sourceEntityId)
{
    std::vector<PointCandidate> candidates;
    const auto add = [&](PointSourceKind kind, const Vector3& position,
                         std::size_t index = 0) {
        PointCandidate candidate;
        candidate.kind = kind;
        candidate.positionMm = position;
        candidate.labelJa = PointSourceKindNameJa(kind);
        candidate.sourceEntityId = sourceEntityId;
        candidate.index = index;
        candidates.push_back(std::move(candidate));
    };

    // 円は始点と終点が同じ位置なので、終点は挙げない。
    const bool closed = curve.Kind() == CurveKind::Circle;
    add(PointSourceKind::CurveStart, curve.StartPoint());
    if (!closed) {
        add(PointSourceKind::CurveEnd, curve.EndPoint());
    }
    add(PointSourceKind::CurveMidpoint, curve.Evaluate(0.5));

    if (curve.Kind() == CurveKind::CircularArc || curve.Kind() == CurveKind::Circle) {
        add(PointSourceKind::CurveCenter, curve.Center());
    }
    if (curve.Kind() == CurveKind::CubicBezier || curve.Kind() == CurveKind::CubicBSpline) {
        const std::vector<Vector3>& points = curve.ControlPoints();
        for (std::size_t index = 0; index < points.size(); ++index) {
            PointCandidate candidate;
            candidate.kind = PointSourceKind::ControlPoint;
            candidate.positionMm = points[index];
            candidate.labelJa = "制御点" + std::to_string(index + 1);
            candidate.sourceEntityId = sourceEntityId;
            candidate.index = index;
            candidates.push_back(std::move(candidate));
        }
    }
    return candidates;
}

std::vector<PointCandidate> PointCandidatesOfDistance(
    const geometry::DistanceMeasurement& measurement)
{
    std::vector<PointCandidate> candidates;
    const auto add = [&](PointSourceKind kind, const Vector3& position) {
        PointCandidate candidate;
        candidate.kind = kind;
        candidate.positionMm = position;
        candidate.labelJa = PointSourceKindNameJa(kind);
        candidates.push_back(std::move(candidate));
    };
    add(PointSourceKind::MeasuredPoint, measurement.firstPoint);
    add(PointSourceKind::MeasuredPoint, measurement.secondPoint);
    add(PointSourceKind::MeasuredMidpoint,
        (measurement.firstPoint + measurement.secondPoint) * 0.5);
    return candidates;
}

std::vector<PointCandidate> PointCandidatesOfApproach(
    const geometry::ClosestApproach& approach)
{
    std::vector<PointCandidate> candidates;
    const auto add = [&](const Vector3& position) {
        PointCandidate candidate;
        candidate.kind = PointSourceKind::ClosestApproach;
        candidate.positionMm = position;
        candidate.labelJa = PointSourceKindNameJa(PointSourceKind::ClosestApproach);
        candidates.push_back(std::move(candidate));
    };
    add(approach.firstPoint);
    add(approach.secondPoint);
    add((approach.firstPoint + approach.secondPoint) * 0.5);
    return candidates;
}

Result<domain::CreatePointDefinition> MakePointDefinition(const PointCandidate& candidate)
{
    if (!candidate.positionMm.IsFinite()) {
        return Result<domain::CreatePointDefinition>::Failure(MakeError("MEA-M001",
            "測る点の座標に数値でない値が入っています。",
            candidate.labelJa + " から点を作れません。"));
    }
    domain::CreatePointDefinition definition;
    definition.positionMm = candidate.positionMm;
    // 式は持たせない。測った位置から作った点は、式ではなく座標が正本である。
    // 見かけの式を作ると、あとで再計算したときに測定元と食い違う。
    return Result<domain::CreatePointDefinition>::Success(std::move(definition));
}

std::string PointDisplayNameJa(const PointCandidate& candidate,
    std::string_view sourceNameJa)
{
    if (sourceNameJa.empty()) {
        return candidate.labelJa;
    }
    return std::string(sourceNameJa) + "の" + candidate.labelJa;
}

} // namespace kachakacha::v2::app
