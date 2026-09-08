#include "kachakacha/modeling/SnapEngine.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::modeling {

using geometry::CurveIntersection;
using geometry::CurveKind;

namespace {

//! 候補を1つ足す道具。画面へ写せない点(カメラの後ろ)は捨てる。
class Collector {
public:
    Collector(const ScreenMapping& mapping, const ScreenPoint& pointer,
        const SnapSettings& settings)
        : mapping_(mapping), pointer_(pointer), settings_(settings)
    {
    }

    void Add(SnapKind kind, const Vector3& position, const EntityId& entityId = {},
        const SegmentId& segmentId = {}, const EntityId& otherEntityId = {},
        const SegmentId& otherSegmentId = {})
    {
        const auto projected = mapping_.Project(position);
        if (!projected.has_value()) {
            return;
        }
        const double distance = geometry::ScreenDistance(*projected, pointer_);
        const double limit = IsHighPrioritySnap(kind) ? settings_.highPriorityRadiusPx
                                                      : settings_.lowPriorityRadiusPx;
        if (distance > limit) {
            return;
        }
        SnapCandidate candidate;
        candidate.kind = kind;
        candidate.position = position;
        candidate.screenDistancePx = distance;
        candidate.entityId = entityId;
        candidate.segmentId = segmentId;
        candidate.otherEntityId = otherEntityId;
        candidate.otherSegmentId = otherSegmentId;
        // 同じ種類で同じ位置の候補は1つにする。
        const bool known = std::any_of(candidates_.begin(), candidates_.end(),
            [&](const SnapCandidate& other) {
                return other.kind == kind
                    && (other.position - position).Length() <= 1.0e-9;
            });
        if (!known) {
            candidates_.push_back(candidate);
        }
    }

    [[nodiscard]] std::vector<SnapCandidate> Take() { return std::move(candidates_); }

private:
    const ScreenMapping& mapping_;
    ScreenPoint pointer_;
    SnapSettings settings_;
    std::vector<SnapCandidate> candidates_;
};

} // namespace

std::string_view SnapKindLabelJa(SnapKind kind) noexcept
{
    switch (kind) {
    case SnapKind::Intersection:       return "交点";
    case SnapKind::Endpoint:           return "端点";
    case SnapKind::Center:             return "中心";
    case SnapKind::DrawingPoint:       return "作図点";
    case SnapKind::Tangent:            return "接点";
    case SnapKind::Perpendicular:      return "垂足";
    case SnapKind::Midpoint:           return "中点";
    case SnapKind::Quadrant:           return "四半点";
    case SnapKind::Extension:          return "延長線";
    case SnapKind::ProjectedOnPlane:   return "平面へ投影";
    case SnapKind::ClosestOnCurve:     return "曲線上";
    case SnapKind::GridMajor:          return "主点";
    case SnapKind::GridMinor:          return "副点";
    case SnapKind::FreeOnPlane:        return "平面上";
    case SnapKind::ScreenIntersection: return "画面交差";
    }
    return "";
}

bool IsHighPrioritySnap(SnapKind kind) noexcept
{
    // §6.1: 高優先の候補は6px、低優先は8px。
    // 交点・端点・中心・作図点までを高優先にする。
    switch (kind) {
    case SnapKind::Intersection:
    case SnapKind::Endpoint:
    case SnapKind::Center:
    case SnapKind::DrawingPoint:
        return true;
    default:
        return false;
    }
}

std::vector<SnapCandidate> CollectSnapCandidates(const SnapScene& scene,
    const ScreenMapping& mapping, const ScreenPoint& pointer, const SnapSettings& settings,
    const GeometryTolerance& tolerance)
{
    if (settings.suppressed) {
        // 修飾キー中は一切吸着しない。トグルではないので状態も持たない。
        return {};
    }
    Collector collector(mapping, pointer, settings);

    // --- 作図点 ---
    for (const SnapDrawingPoint& point : scene.points) {
        collector.Add(SnapKind::DrawingPoint, point.position, point.entityId);
    }

    // --- 曲線ごとの候補 ---
    for (const SnapCurve& curve : scene.curves) {
        collector.Add(SnapKind::Endpoint, curve.segment.StartPoint(), curve.entityId,
            curve.segmentId);
        collector.Add(SnapKind::Endpoint, curve.segment.EndPoint(), curve.entityId,
            curve.segmentId);
        collector.Add(SnapKind::Midpoint, curve.segment.Evaluate(0.5), curve.entityId,
            curve.segmentId);
        if (curve.segment.Kind() == CurveKind::Circle
            || curve.segment.Kind() == CurveKind::CircularArc) {
            collector.Add(SnapKind::Center, curve.segment.Center(), curve.entityId,
                curve.segmentId);
        }
        for (const Vector3& quadrant : geometry::QuadrantPoints(curve.segment)) {
            collector.Add(SnapKind::Quadrant, quadrant, curve.entityId, curve.segmentId);
        }
        // 曲線上の最近点。画面の位置から視線を伸ばし、いちばん近いところ。
        if (const auto ray = mapping.RayThrough(pointer)) {
            // 視線上の点として、曲線の点列で最も画面距離が近いところを選ぶ。
            const double sampling = std::max(
                curve.segment.TotalLength(tolerance.modelLinearMm) / 200.0,
                tolerance.modelLinearMm * 10.0);
            const auto sampled = geometry::SampleCurve(curve.segment, sampling);
            double best = -1.0;
            Vector3 bestPoint{};
            for (const auto& point : sampled) {
                const auto projected = mapping.Project(point.position);
                if (!projected.has_value()) {
                    continue;
                }
                const double distance = geometry::ScreenDistance(*projected, pointer);
                if (best < 0.0 || distance < best) {
                    best = distance;
                    bestPoint = point.position;
                }
            }
            if (best >= 0.0) {
                collector.Add(SnapKind::ClosestOnCurve, bestPoint, curve.entityId,
                    curve.segmentId);
            }
        }
        // 延長線上(V1の Extension)。基準点ではなく、画面の位置から決める。
        if (const auto onPlane = scene.workPlane.active
                ? mapping.UnprojectOntoPlane(pointer, scene.workPlane.origin,
                      scene.workPlane.normal)
                : std::optional<Vector3>{}) {
            if (const auto extension = geometry::ExtensionPoint(curve.segment, *onPlane,
                    settings.maximumExtensionMm)) {
                collector.Add(SnapKind::Extension, *extension, curve.entityId,
                    curve.segmentId);
            }
        }
        // 接点と垂足。直前に置いた点があるときだけ意味がある。
        if (settings.referencePoint.has_value()) {
            for (const Vector3& foot : geometry::PerpendicularFeet(curve.segment,
                     *settings.referencePoint, tolerance)) {
                collector.Add(SnapKind::Perpendicular, foot, curve.entityId,
                    curve.segmentId);
            }
            for (const Vector3& touch : geometry::TangentPoints(curve.segment,
                     *settings.referencePoint, tolerance)) {
                collector.Add(SnapKind::Tangent, touch, curve.entityId, curve.segmentId);
            }
        }
    }

    // --- 交点 ---
    for (std::size_t a = 0; a < scene.curves.size(); ++a) {
        for (std::size_t b = a + 1; b < scene.curves.size(); ++b) {
            const auto crossings = geometry::IntersectCurvesOnScreen(scene.curves[a].segment,
                scene.curves[b].segment, mapping, settings.lowPriorityRadiusPx, tolerance);
            for (const CurveIntersection& crossing : crossings) {
                // 実3D交点と画面交差を分ける。後者は既定で選ばない(§4)。
                collector.Add(crossing.real ? SnapKind::Intersection
                                            : SnapKind::ScreenIntersection,
                    crossing.position, scene.curves[a].entityId, scene.curves[a].segmentId,
                    scene.curves[b].entityId, scene.curves[b].segmentId);
            }
        }
    }

    // --- 作業平面まわり ---
    if (scene.workPlane.active) {
        const auto onPlane = mapping.UnprojectOntoPlane(pointer, scene.workPlane.origin,
            scene.workPlane.normal);
        if (onPlane.has_value()) {
            // 作図点や端点を作業平面へ法線投影した点(V1の ProjectedPoint)。
            const auto project = [&](const Vector3& source) {
                const double signedDistance =
                    Dot(source - scene.workPlane.origin, scene.workPlane.normal);
                return source - scene.workPlane.normal * signedDistance;
            };
            for (const SnapDrawingPoint& point : scene.points) {
                collector.Add(SnapKind::ProjectedOnPlane, project(point.position),
                    point.entityId);
            }
            for (const SnapCurve& curve : scene.curves) {
                collector.Add(SnapKind::ProjectedOnPlane, project(curve.segment.StartPoint()),
                    curve.entityId, curve.segmentId);
                collector.Add(SnapKind::ProjectedOnPlane, project(curve.segment.EndPoint()),
                    curve.entityId, curve.segmentId);
            }

            // グリッド。
            if (scene.grid.visible && scene.grid.majorSpacingMm > 0.0) {
                const Vector3 relative = *onPlane - scene.grid.origin;
                const double u = Dot(relative, scene.grid.uDirection);
                const double v = Dot(relative, scene.grid.vDirection);
                const int steps = scene.grid.subdivision > 1 ? scene.grid.subdivision : 1;
                const double minorSpacing = scene.grid.majorSpacingMm
                    / static_cast<double>(steps);
                const double pixelsPerMm = mapping.PixelsPerMillimeterAt(*onPlane);
                const bool showMinor = steps > 1
                    && minorSpacing * pixelsPerMm >= settings.minimumGridSpacingPx;
                const double spacing = showMinor ? minorSpacing : scene.grid.majorSpacingMm;
                // 近くの格子点だけを見る。周囲1マスで足りる。
                for (int du = -1; du <= 1; ++du) {
                    for (int dv = -1; dv <= 1; ++dv) {
                        const double gu = (std::round(u / spacing) + du) * spacing;
                        const double gv = (std::round(v / spacing) + dv) * spacing;
                        const Vector3 position = scene.grid.origin
                            + scene.grid.uDirection * gu + scene.grid.vDirection * gv;
                        const bool major =
                            std::abs(std::remainder(gu, scene.grid.majorSpacingMm))
                                <= scene.grid.majorSpacingMm * 1.0e-9
                            && std::abs(std::remainder(gv, scene.grid.majorSpacingMm))
                                <= scene.grid.majorSpacingMm * 1.0e-9;
                        collector.Add(major ? SnapKind::GridMajor : SnapKind::GridMinor,
                            position);
                    }
                }
            }
            // 最後の受け皿。平面上の自由な点。
            collector.Add(SnapKind::FreeOnPlane, *onPlane);
        }
    }

    std::vector<SnapCandidate> candidates = collector.Take();
    // 並びを決定的にする。優先順位 → 画面距離 → 位置。
    std::sort(candidates.begin(), candidates.end(),
        [](const SnapCandidate& l, const SnapCandidate& r) {
            if (l.kind != r.kind) {
                return static_cast<int>(l.kind) < static_cast<int>(r.kind);
            }
            if (l.screenDistancePx != r.screenDistancePx) {
                return l.screenDistancePx < r.screenDistancePx;
            }
            if (l.position.x != r.position.x) {
                return l.position.x < r.position.x;
            }
            if (l.position.y != r.position.y) {
                return l.position.y < r.position.y;
            }
            return l.position.z < r.position.z;
        });
    return candidates;
}

std::optional<SnapCandidate> ChooseSnap(const std::vector<SnapCandidate>& candidates,
    const SnapSettings& settings)
{
    if (settings.suppressed || candidates.empty()) {
        return std::nullopt;
    }
    // 画面交差は既定では選ばない(§4)。表示はするが吸着はしない。
    std::vector<const SnapCandidate*> usable;
    for (const SnapCandidate& candidate : candidates) {
        if (candidate.kind != SnapKind::ScreenIntersection) {
            usable.push_back(&candidate);
        }
    }
    if (usable.empty()) {
        return std::nullopt;
    }
    // 順位が上のものを採る。候補はすでに順位→画面距離で並んでいる。
    //
    // 「距離が大きく違うときは順位より画面距離を優先する」(§6.1)は、
    // 有効範囲の使い分けがその役目を果たしている。
    // 高優先の候補は6px、低優先は8pxまでしか候補にならないので、
    // 遠い高優先候補はそもそもここへ来ない。
    // これを「近いほうが勝つ」規則として別に足すと、
    // 常にポインタ直下にある平面上の自由点がすべてを奪ってしまう。
    return *usable.front();
}

} // namespace kachakacha::v2::modeling
