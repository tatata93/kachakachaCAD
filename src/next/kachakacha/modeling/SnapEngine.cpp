#include "kachakacha/modeling/SnapEngine.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace kachakacha::v2::modeling {

using geometry::CurveIntersection;
using geometry::CurveKind;

namespace {

//! 吸着先の出どころだけを入れた候補。種類・位置・距離は Collector::Add が入れる。
[[nodiscard]] SnapCandidate From(const EntityId& entityId = {}, const SegmentId& segmentId = {},
    std::int64_t featureIndex = 0)
{
    SnapCandidate candidate;
    candidate.entityId = entityId;
    candidate.segmentId = segmentId;
    candidate.featureIndex = featureIndex;
    return candidate;
}

//! 曲線の ID の順。交点の2曲線を場面の並び順に依らず決めるために使う。
[[nodiscard]] bool CurveIdLess(const SnapCurve& first, const SnapCurve& second) noexcept
{
    if (first.entityId != second.entityId) {
        return first.entityId < second.entityId;
    }
    return first.segmentId < second.segmentId;
}

//! 候補を1つ足す道具。画面へ写せない点(カメラの後ろ)は捨てる。
class Collector {
public:
    Collector(const ScreenMapping& mapping, const ScreenPoint& pointer,
        const SnapSettings& settings, const GeometryTolerance& tolerance)
        : mapping_(mapping), pointer_(pointer), settings_(settings), tolerance_(tolerance)
    {
    }

    void Add(SnapKind kind, const Vector3& position, SnapCandidate candidate = {})
    {
        const auto projected = mapping_.Project(position);
        if (!projected.has_value()) {
            return;
        }
        candidate.kind = kind;
        candidate.position = position;
        candidate.screenDistancePx = geometry::ScreenDistance(*projected, pointer_);
        candidate.held = settings_.heldSnap.has_value()
            && SameSnapTarget(candidate, *settings_.heldSnap);
        // 半径は画面px。持ち越している吸着先だけ、手放すまでの余裕を足す。
        const double limit = tolerance_.snapPickPx
            + (candidate.held ? std::max(0.0, settings_.holdMarginPx) : 0.0);
        if (candidate.screenDistancePx > limit) {
            return;
        }
        // 同じ吸着先の候補は1つにする。位置が同じでも、吸着先が違えば別に残す。
        const auto known = std::find_if(candidates_.begin(), candidates_.end(),
            [&](const SnapCandidate& other) { return SameSnapTarget(other, candidate); });
        if (known == candidates_.end()) {
            candidates_.push_back(candidate);
        } else if (candidate.screenDistancePx < known->screenDistancePx) {
            *known = candidate;
        }
    }

    [[nodiscard]] std::vector<SnapCandidate> Take() { return std::move(candidates_); }

private:
    const ScreenMapping& mapping_;
    ScreenPoint pointer_;
    const SnapSettings& settings_;
    const GeometryTolerance& tolerance_;
    std::vector<SnapCandidate> candidates_;
};

//! いま使うグリッドの間隔。副点が画面で詰まりすぎるときは主点だけにする。
struct GridStep {
    double spacing = 0.0;
    std::int64_t perStep = 1;   //!< 格子の番号を副点の間隔で数えるための倍率
};

[[nodiscard]] std::optional<GridStep> CurrentGridStep(const SnapScene& scene,
    const ScreenMapping& mapping, const Vector3& onPlane, const SnapSettings& settings)
{
    if (!scene.grid.visible || !(scene.grid.majorSpacingMm > 0.0)) {
        return std::nullopt;
    }
    const int steps = scene.grid.subdivision > 1 ? scene.grid.subdivision : 1;
    const double minorSpacing = scene.grid.majorSpacingMm / static_cast<double>(steps);
    const bool showMinor = steps > 1
        && minorSpacing * mapping.PixelsPerMillimeterAt(onPlane)
            >= settings.minimumGridSpacingPx;
    GridStep step;
    step.spacing = showMinor ? minorSpacing : scene.grid.majorSpacingMm;
    step.perStep = showMinor ? 1 : steps;
    return step;
}

//! 線上の格子(GridOnCurve)。nearPoint(線上の、ポインタにいちばん近い点)のまわりで、
//! 線がグリッドの線を横切る点を足す。直線は解析的に、ほかの曲線は格子点が線に
//! 乗っているときだけ足す。同じ点は同じ番号(副点の間隔で数えた u, v)になる。
void AddGridOnCurve(Collector& collector, const SnapScene& scene, const SnapCurve& curve,
    const Vector3& nearPoint, const GridStep& step, const GeometryTolerance& tolerance)
{
    const SnapGrid& grid = scene.grid;
    const double minor = step.spacing / static_cast<double>(step.perStep);
    const auto uvOf = [&](const Vector3& point) {
        const Vector3 relative = point - grid.origin;
        return std::pair<double, double>{
            Dot(relative, grid.uDirection), Dot(relative, grid.vDirection)};
    };
    const auto add = [&](const Vector3& position) {
        const auto [u, v] = uvOf(position);
        SnapCandidate source = From(curve.entityId, curve.segmentId,
            static_cast<std::int64_t>(std::llround(u / minor)));
        source.latticeV = static_cast<std::int64_t>(std::llround(v / minor));
        collector.Add(SnapKind::GridOnCurve, position, source);
    };
    const auto [nu, nv] = uvOf(nearPoint);
    if (curve.segment.Kind() == CurveKind::Line) {
        const Vector3 a = curve.segment.StartPoint();
        const Vector3 b = curve.segment.EndPoint();
        const auto [ua, va] = uvOf(a);
        const auto [ub, vb] = uvOf(b);
        const auto crossings = [&](double from, double to, double at) {
            const double span = to - from;
            if (std::abs(span) <= tolerance.modelLinearMm) {
                return;   // グリッドの線と平行。重なっていても交わる点は反対向きで拾う
            }
            const double base = std::round(at / step.spacing);
            for (int k = -1; k <= 1; ++k) {
                const double t = ((base + k) * step.spacing - from) / span;
                if (t >= -1.0e-9 && t <= 1.0 + 1.0e-9) {
                    add(a + (b - a) * std::clamp(t, 0.0, 1.0));
                }
            }
        };
        crossings(ua, ub, nu);
        crossings(va, vb, nv);
        return;
    }
    for (int du = -1; du <= 1; ++du) {
        for (int dv = -1; dv <= 1; ++dv) {
            const Vector3 lattice = grid.origin
                + grid.uDirection * ((std::round(nu / step.spacing) + du) * step.spacing)
                + grid.vDirection * ((std::round(nv / step.spacing) + dv) * step.spacing);
            const Vector3 onCurve = curve.segment.ClosestPoint(lattice).point;
            if ((onCurve - lattice).Length() <= std::max(tolerance.modelLinearMm, 1.0e-6)) {
                add(lattice);
            }
        }
    }
}

} // namespace

std::string_view SnapKindLabelJa(SnapKind kind) noexcept
{
    switch (kind) {
    case SnapKind::Endpoint:           return "端点";
    case SnapKind::DrawingPoint:       return "作図点";
    case SnapKind::Intersection:       return "交点";
    case SnapKind::Midpoint:           return "中点";
    case SnapKind::Center:             return "中心";
    case SnapKind::Quadrant:           return "四半点";
    case SnapKind::GridOnCurve:        return "線上の格子";
    // 直線にも円にも出る。「曲線上」と出すと直線の上で首をかしげる(オーナー報告 2026-09-21)。
    case SnapKind::ClosestOnCurve:     return "線上";
    case SnapKind::Perpendicular:      return "垂足";
    case SnapKind::Tangent:            return "接点";
    case SnapKind::Extension:          return "延長線";
    case SnapKind::ProjectedOnPlane:   return "平面へ投影";
    case SnapKind::GridMajor:          return "主点";
    case SnapKind::GridMinor:          return "副点";
    case SnapKind::FreeOnPlane:        return "平面上";
    case SnapKind::ScreenIntersection: return "画面交差";
    }
    return "";
}

int SnapPriorityRank(SnapKind kind) noexcept
{
    switch (kind) {
    case SnapKind::Endpoint:
    case SnapKind::DrawingPoint:
        return 0;
    case SnapKind::Intersection:
        return 1;
    case SnapKind::Midpoint:
    case SnapKind::Center:
    case SnapKind::Quadrant:
        return 2;
    case SnapKind::GridOnCurve:
        // 最近点より上。最近点はポインタ直下で距離がほぼ 0 なので、同じ順位だと
        // 線の上のグリッドの点が選ばれない(オーナー報告 2026-09-21)。
        return 3;
    case SnapKind::ClosestOnCurve:
        return 4;
    case SnapKind::Perpendicular:
    case SnapKind::Tangent:
        return 5;
    case SnapKind::ProjectedOnPlane:
        return 6;   // 平面へ落とした「点」。§6.1 に順位が無い(SnapEngine.h)
    case SnapKind::GridMajor:
    case SnapKind::GridMinor:
        return 7;
    case SnapKind::Extension:
        // 延長線は「線」の案内で、グリッドは「点」である。点のほうを採る。
        // グリッドの上を狙っているのに、たまたま近くの線の延長と重なっただけで
        // 格子から外れた点が入っていた(オーナー報告 2026-09-21)。
        return 8;
    case SnapKind::FreeOnPlane:
        return 9;
    case SnapKind::ScreenIntersection:
        return 10;
    }
    return 10;
}

SnapTargetKey TargetKeyOf(const SnapCandidate& candidate) noexcept
{
    SnapTargetKey key;
    key.kind = candidate.kind;
    key.entityId = candidate.entityId;
    key.segmentId = candidate.segmentId;
    key.otherEntityId = candidate.otherEntityId;
    key.otherSegmentId = candidate.otherSegmentId;
    key.featureIndex = candidate.featureIndex;
    key.latticeV = candidate.latticeV;
    return key;
}

bool operator==(const SnapTargetKey& first, const SnapTargetKey& second) noexcept
{
    return first.kind == second.kind && first.entityId == second.entityId
        && first.segmentId == second.segmentId && first.otherEntityId == second.otherEntityId
        && first.otherSegmentId == second.otherSegmentId
        && first.featureIndex == second.featureIndex && first.latticeV == second.latticeV;
}

bool operator<(const SnapTargetKey& first, const SnapTargetKey& second) noexcept
{
    if (first.kind != second.kind) {
        return static_cast<int>(first.kind) < static_cast<int>(second.kind);
    }
    if (first.entityId != second.entityId) {
        return first.entityId < second.entityId;
    }
    if (first.segmentId != second.segmentId) {
        return first.segmentId < second.segmentId;
    }
    if (first.otherEntityId != second.otherEntityId) {
        return first.otherEntityId < second.otherEntityId;
    }
    if (first.otherSegmentId != second.otherSegmentId) {
        return first.otherSegmentId < second.otherSegmentId;
    }
    if (first.featureIndex != second.featureIndex) {
        return first.featureIndex < second.featureIndex;
    }
    return first.latticeV < second.latticeV;
}

bool SameSnapTarget(const SnapCandidate& first, const SnapCandidate& second) noexcept
{
    return TargetKeyOf(first) == TargetKeyOf(second);
}

std::vector<SnapCandidate> CollectSnapCandidates(const SnapScene& scene,
    const ScreenMapping& mapping, const ScreenPoint& pointer, const SnapSettings& settings,
    const GeometryTolerance& tolerance)
{
    if (settings.suppressed) {
        // 修飾キー中は一切吸着しない。トグルではないので状態も持たない。
        return {};
    }
    Collector collector(mapping, pointer, settings, tolerance);

    // --- 作図点 ---
    for (const SnapDrawingPoint& point : scene.points) {
        collector.Add(SnapKind::DrawingPoint, point.position, From(point.entityId));
    }

    // --- 曲線ごとの候補 ---
    for (const SnapCurve& curve : scene.curves) {
        collector.Add(SnapKind::Endpoint, curve.segment.StartPoint(),
            From(curve.entityId, curve.segmentId, 0));
        collector.Add(SnapKind::Endpoint, curve.segment.EndPoint(),
            From(curve.entityId, curve.segmentId, 1));
        collector.Add(SnapKind::Midpoint, curve.segment.Evaluate(0.5),
            From(curve.entityId, curve.segmentId));
        if (curve.segment.Kind() == CurveKind::Circle
            || curve.segment.Kind() == CurveKind::CircularArc) {
            collector.Add(SnapKind::Center, curve.segment.Center(),
                From(curve.entityId, curve.segmentId));
        }
        const auto quadrants = geometry::QuadrantPoints(curve.segment);
        for (std::size_t index = 0; index < quadrants.size(); ++index) {
            collector.Add(SnapKind::Quadrant, quadrants[index],
                From(curve.entityId, curve.segmentId, static_cast<std::int64_t>(index)));
        }
        // 曲線上の最近点。半径は画面pxなので、画面上でいちばん近く見える曲線上の点を使う。
        // 作業平面の中にある曲線では、画面位置を平面へ戻した点の解析的な最近点が
        // それと同じだけ近く写るときに限り、その正確な座標を使う。
        // 斜めや寝た視点では平面上で3Dに近い点が画面では遠く写るので、そのときは使わない。
        const double closestLimitPx = tolerance.snapPickPx + std::max(0.0, settings.holdMarginPx);
        if (const auto approach = geometry::ApproachToCurveOnScreen(curve.segment, mapping,
                pointer, closestLimitPx)) {
            Vector3 position = approach->point;
            const auto onWorkPlane = scene.workPlane.active
                    && geometry::CurveLiesInPlane(curve.segment, scene.workPlane.origin,
                        scene.workPlane.normal, tolerance.modelLinearMm)
                ? mapping.UnprojectOntoPlane(pointer, scene.workPlane.origin,
                      scene.workPlane.normal)
                : std::optional<Vector3>{};
            if (onWorkPlane.has_value()) {
                const Vector3 exact = curve.segment.ClosestPoint(*onWorkPlane).point;
                const auto exactScreen = mapping.Project(exact);
                if (exactScreen.has_value()
                    && geometry::ScreenDistance(*exactScreen, pointer)
                        <= approach->distancePx + 1.0e-6) {
                    position = exact;
                }
            }
            collector.Add(SnapKind::ClosestOnCurve, position,
                From(curve.entityId, curve.segmentId));
            // 線の上のグリッドの点。作業平面の中の線だけ(グリッドは作業平面に引く)。
            if (onWorkPlane.has_value()) {
                if (const auto step = CurrentGridStep(scene, mapping, *onWorkPlane, settings)) {
                    AddGridOnCurve(collector, scene, curve,
                        curve.segment.ClosestPoint(*onWorkPlane).point, *step, tolerance);
                }
            }
        }
        // 延長線上(V1の Extension)。基準点ではなく、画面の位置から決める。
        if (const auto onPlane = scene.workPlane.active
                ? mapping.UnprojectOntoPlane(pointer, scene.workPlane.origin,
                      scene.workPlane.normal)
                : std::optional<Vector3>{}) {
            if (const auto extension = geometry::ExtensionPoint(curve.segment, *onPlane,
                    settings.maximumExtensionMm)) {
                collector.Add(SnapKind::Extension, *extension,
                    From(curve.entityId, curve.segmentId));
            }
        }
        // 接点と垂足。直前に置いた点があるときだけ意味がある。
        if (settings.referencePoint.has_value()) {
            const auto feet = geometry::PerpendicularFeet(curve.segment,
                *settings.referencePoint, tolerance);
            for (std::size_t index = 0; index < feet.size(); ++index) {
                collector.Add(SnapKind::Perpendicular, feet[index],
                    From(curve.entityId, curve.segmentId, static_cast<std::int64_t>(index)));
            }
            const auto touches = geometry::TangentPoints(curve.segment,
                *settings.referencePoint, tolerance);
            for (std::size_t index = 0; index < touches.size(); ++index) {
                collector.Add(SnapKind::Tangent, touches[index],
                    From(curve.entityId, curve.segmentId, static_cast<std::int64_t>(index)));
            }
        }
    }

    // --- 交点 ---
    for (std::size_t a = 0; a < scene.curves.size(); ++a) {
        for (std::size_t b = a + 1; b < scene.curves.size(); ++b) {
            // 2曲線は ID の小さいほうを先にする。場面の並び順で持ち主や番号が入れ替わらない。
            const SnapCurve* first = &scene.curves[a];
            const SnapCurve* second = &scene.curves[b];
            if (CurveIdLess(*second, *first)) {
                std::swap(first, second);
            }
            auto crossings = geometry::IntersectCurvesOnScreen(first->segment,
                second->segment, mapping, tolerance.snapPickPx, tolerance);
            std::sort(crossings.begin(), crossings.end(),
                [](const CurveIntersection& l, const CurveIntersection& r) {
                    if (l.firstParameter != r.firstParameter) {
                        return l.firstParameter < r.firstParameter;
                    }
                    return l.secondParameter < r.secondParameter;
                });
            for (std::size_t index = 0; index < crossings.size(); ++index) {
                SnapCandidate source = From(first->entityId, first->segmentId,
                    static_cast<std::int64_t>(index));
                source.otherEntityId = second->entityId;
                source.otherSegmentId = second->segmentId;
                // 実3D交点と画面交差を分ける。後者は既定で選ばない(§4)。
                collector.Add(crossings[index].real ? SnapKind::Intersection
                                                    : SnapKind::ScreenIntersection,
                    crossings[index].position, source);
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
                    From(point.entityId));
            }
            for (const SnapCurve& curve : scene.curves) {
                collector.Add(SnapKind::ProjectedOnPlane, project(curve.segment.StartPoint()),
                    From(curve.entityId, curve.segmentId, 0));
                collector.Add(SnapKind::ProjectedOnPlane, project(curve.segment.EndPoint()),
                    From(curve.entityId, curve.segmentId, 1));
            }

            // グリッド。
            if (const auto step = CurrentGridStep(scene, mapping, *onPlane, settings)) {
                const Vector3 relative = *onPlane - scene.grid.origin;
                const double u = Dot(relative, scene.grid.uDirection);
                const double v = Dot(relative, scene.grid.vDirection);
                const double spacing = step->spacing;
                // 格子の番号は副点の間隔で数える。副点の表示が切り替わっても同じ点は同じ番号。
                const std::int64_t perStep = step->perStep;
                // 近くの格子点だけを見る。周囲1マスで足りる。
                for (int du = -1; du <= 1; ++du) {
                    for (int dv = -1; dv <= 1; ++dv) {
                        const double iu = std::round(u / spacing) + du;
                        const double iv = std::round(v / spacing) + dv;
                        const double gu = iu * spacing;
                        const double gv = iv * spacing;
                        const Vector3 position = scene.grid.origin
                            + scene.grid.uDirection * gu + scene.grid.vDirection * gv;
                        const bool major =
                            std::abs(std::remainder(gu, scene.grid.majorSpacingMm))
                                <= scene.grid.majorSpacingMm * 1.0e-9
                            && std::abs(std::remainder(gv, scene.grid.majorSpacingMm))
                                <= scene.grid.majorSpacingMm * 1.0e-9;
                        SnapCandidate lattice;
                        lattice.featureIndex = static_cast<std::int64_t>(iu) * perStep;
                        lattice.latticeV = static_cast<std::int64_t>(iv) * perStep;
                        collector.Add(major ? SnapKind::GridMajor : SnapKind::GridMinor,
                            position, lattice);
                    }
                }
            }
            // 最後の受け皿。平面上の自由な点。
            collector.Add(SnapKind::FreeOnPlane, *onPlane);
        }
    }

    std::vector<SnapCandidate> candidates = collector.Take();
    // 並びを決定的にする。順位 → 画面距離 → 吸着先の同一性。
    // 別の形の候補が同じ位置・同じ距離に並ぶことがある。場面の並び順で答えを変えない。
    std::sort(candidates.begin(), candidates.end(),
        [](const SnapCandidate& l, const SnapCandidate& r) {
            const int leftRank = SnapPriorityRank(l.kind);
            const int rightRank = SnapPriorityRank(r.kind);
            if (leftRank != rightRank) {
                return leftRank < rightRank;
            }
            if (l.screenDistancePx != r.screenDistancePx) {
                return l.screenDistancePx < r.screenDistancePx;
            }
            return TargetKeyOf(l) < TargetKeyOf(r);
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
    // 候補はすでに順位 → 画面距離で並んでいるので、最初の1つが順位どおりの答えである。
    const SnapCandidate* best = nullptr;
    const SnapCandidate* held = nullptr;
    for (const SnapCandidate& candidate : candidates) {
        if (candidate.kind == SnapKind::ScreenIntersection) {
            continue;
        }
        if (best == nullptr) {
            best = &candidate;
        }
        if (held == nullptr && candidate.held) {
            held = &candidate;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    if (held == nullptr || held == best) {
        return *best;
    }
    // 順位が上の候補が現れたら、すぐ乗り換える。
    if (SnapPriorityRank(best->kind) < SnapPriorityRank(held->kind)) {
        return *best;
    }
    // 同じ順位では、揺れの幅より明らかに近いときだけ乗り換える。
    // 境目を行き来するたびに入れ替わると、リングが点滅して狙いが定まらない。
    if (best->screenDistancePx + std::max(0.0, settings.holdMarginPx)
        < held->screenDistancePx) {
        return *best;
    }
    return *held;
}

std::optional<SnapCandidate> SnapHysteresis::Resolve(const SnapScene& scene,
    const ScreenMapping& mapping, const ScreenPoint& pointer, const SnapSettings& settings,
    const GeometryTolerance& tolerance)
{
    if (settings.suppressed) {
        // S を離したあとに古い候補へ戻らないよう、持ち越しも捨てる。
        held_.reset();
        return std::nullopt;
    }
    SnapSettings withHold = settings;
    withHold.heldSnap = held_;
    held_ = ChooseSnap(CollectSnapCandidates(scene, mapping, pointer, withHold, tolerance),
        withHold);
    return held_;
}

} // namespace kachakacha::v2::modeling
