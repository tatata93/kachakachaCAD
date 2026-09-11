#include "kachakacha/app/Selection.h"

#include "kachakacha/app/PlaneFocus.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::app {
namespace {

using geometry::ScreenPoint;
using geometry::Vector3;

struct ScreenSegmentApproach {
    double distancePx = 0.0;
    double fraction = 0.0;
};

//! 点と線分の距離。画面の上での話なので px。
[[nodiscard]] ScreenSegmentApproach ApproachToSegmentPx(const ScreenPoint& point,
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

struct CurveApproach {
    double distancePx = 0.0;
    double parameter = 0.0;
    Vector3 point{};
};

//! 1本の曲線と点の距離。画面へ落としてから測る。
//! 落とせない点(カメラの後ろ)はまたぐ弦ごと捨てる。
[[nodiscard]] std::optional<CurveApproach> ApproachToCurvePx(
    const geometry::CurveSegment& segment,
    const geometry::ScreenMapping& mapping, const ScreenPoint& pointer,
    double toleranceMm)
{
    const auto samples = geometry::SampleCurve(segment, toleranceMm);
    std::optional<CurveApproach> best;
    std::optional<ScreenPoint> previous;
    std::optional<geometry::CurvePoint> previousSample;
    for (const auto& sample : samples) {
        const auto screen = mapping.Project(sample.position);
        if (!screen.has_value()) {
            previous.reset();
            previousSample.reset();
            continue;
        }
        if (previous.has_value() && previousSample.has_value()) {
            const auto approach = ApproachToSegmentPx(pointer, *previous, *screen);
            if (!best.has_value() || approach.distancePx < best->distancePx) {
                const double t = approach.fraction;
                best = CurveApproach{approach.distancePx,
                    previousSample->parameter
                        + (sample.parameter - previousSample->parameter) * t,
                    previousSample->position + (sample.position - previousSample->position) * t};
            }
        }
        previous = screen;
        previousSample = sample;
    }
    if (!best.has_value() && samples.size() == 1) {
        const auto screen = mapping.Project(samples.front().position);
        if (screen.has_value()) {
            const auto approach = ApproachToSegmentPx(pointer, *screen, *screen);
            best = CurveApproach{approach.distancePx, samples.front().parameter,
                samples.front().position};
        }
    }
    return best;
}

[[nodiscard]] bool SameTarget(const SelectionRef& first, const SelectionRef& second)
{
    if (first.entityId != second.entityId || first.kind != second.kind
        || first.segmentId != second.segmentId) {
        return false;
    }
    if (first.subshapeKey.has_value() != second.subshapeKey.has_value()) {
        return false;
    }
    return !first.subshapeKey.has_value()
        || first.subshapeKey->ToString() == second.subshapeKey->ToString();
}

[[nodiscard]] std::vector<SelectionRef> EffectiveRefs(const SelectionSet& selection)
{
    std::vector<SelectionRef> refs = selection.ordered;
    for (const base::EntityId id : selection.entityIds) {
        const bool represented = std::any_of(refs.begin(), refs.end(), [id](const auto& ref) {
            return ref.entityId == id;
        });
        if (!represented) {
            SelectionRef ref;
            ref.entityId = id;
            refs.push_back(ref);
        }
    }
    return refs;
}

[[nodiscard]] SelectionSet SelectionFromRefs(std::vector<SelectionRef> refs)
{
    SelectionSet selection;
    selection.ordered = std::move(refs);
    for (const auto& ref : selection.ordered) {
        if (std::find(selection.entityIds.begin(), selection.entityIds.end(), ref.entityId)
            == selection.entityIds.end()) {
            selection.entityIds.push_back(ref.entityId);
        }
    }
    return selection;
}

[[nodiscard]] SelectionRef RefFromCandidate(const PickCandidate& candidate)
{
    SelectionRef ref;
    ref.entityId = candidate.entityId;
    ref.kind = candidate.kind;
    if (!candidate.segmentId.IsNil()) {
        ref.segmentId = candidate.segmentId;
    }
    ref.subshapeKey = candidate.subshapeKey;
    ref.curveParameter = candidate.curveParameter;
    ref.hitPoint = candidate.hitPoint;
    ref.screenDistancePx = candidate.distancePx;
    return ref;
}

[[nodiscard]] bool HasEntity(const document::DocumentSnapshot& snapshot, base::EntityId id)
{
    return std::any_of(snapshot.entities.begin(), snapshot.entities.end(),
        [id](const auto& entity) { return entity.id == id; });
}

} // namespace

std::optional<PickCandidate> PickCurve(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus)
{
    std::optional<PickCandidate> best;
    for (const auto& curve : scene.curves) {
        // 薄くしている線は拾わない。別の面の線を誤って掴むのが、
        // 面が何枚も浮いているこのCADでいちばん困る事故である。
        if (!PickableOffPlaneCurve(focus.drawing, focus.dimOffPlane,
                CurveLiesOnPlane(curve.segment, focus.plane))) {
            continue;
        }
        const auto approach = ApproachToCurvePx(curve.segment, mapping, pointer,
            tolerance.interactiveJoinMm);
        if (!approach.has_value() || approach->distancePx > tolerance.displayPickPx) {
            continue;
        }
        // 同じ距離のときは先に入っているものを残す。毎回同じ結果になる。
        if (best.has_value() && !(approach->distancePx < best->distancePx)) {
            continue;
        }
        PickCandidate candidate;
        candidate.entityId = curve.entityId;
        candidate.segmentId = curve.segmentId;
        candidate.kind = SelectionElementKind::Edge;
        candidate.curveParameter = approach->parameter;
        candidate.hitPoint = approach->point;
        candidate.distancePx = approach->distancePx;
        best = candidate;
    }
    return best;
}

std::optional<PickCandidate> PickPoint(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance)
{
    std::optional<PickCandidate> best;
    for (const auto& point : scene.points) {
        const auto screen = mapping.Project(point.position);
        if (!screen.has_value()) {
            continue;
        }
        const double dx = screen->x - pointer.x;
        const double dy = screen->y - pointer.y;
        const double distance = std::sqrt(dx * dx + dy * dy);
        if (distance > tolerance.displayPickPx) {
            continue;
        }
        // 同じ距離のときは先に入っているものを残す。毎回同じ結果になる。
        if (best.has_value() && !(distance < best->distancePx)) {
            continue;
        }
        PickCandidate candidate;
        candidate.entityId = point.entityId;
        candidate.kind = SelectionElementKind::Vertex;
        candidate.hitPoint = point.position;
        candidate.distancePx = distance;
        best = candidate;
    }
    return best;
}

std::optional<PickCandidate> PickEntity(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus)
{
    // 点を先に見る。点は線の上に載っていることが多いので、
    // 線を先に見ると点が永久に拾えない。
    if (auto point = PickPoint(scene, mapping, pointer, tolerance); point.has_value()) {
        return point;
    }
    return PickCurve(scene, mapping, pointer, tolerance, focus);
}

SelectionSet ApplySelection(const SelectionSet& current,
    const std::optional<PickCandidate>& picked, SelectionMode mode)
{
    if (!picked.has_value()) {
        // 何も無いところを押した。素で押したときだけ空にする。
        return mode == SelectionMode::Replace ? SelectionSet{}
                                              : SelectionFromRefs(EffectiveRefs(current));
    }
    const SelectionRef target = RefFromCandidate(*picked);
    std::vector<SelectionRef> refs = EffectiveRefs(current);
    const auto found = std::find_if(refs.begin(), refs.end(), [&](const auto& ref) {
        return SameTarget(ref, target);
    });
    switch (mode) {
    case SelectionMode::Replace:
        return SelectionFromRefs({target});
    case SelectionMode::Add:
        if (found == refs.end()) {
            refs.push_back(target);
        }
        return SelectionFromRefs(std::move(refs));
    case SelectionMode::Toggle:
        if (found != refs.end()) {
            refs.erase(found);
        } else {
            refs.push_back(target);
        }
        return SelectionFromRefs(std::move(refs));
    case SelectionMode::Subtract:
        if (found != refs.end()) {
            refs.erase(found);
        }
        return SelectionFromRefs(std::move(refs));
    }
    return SelectionFromRefs(std::move(refs));
}

bool IsSelected(const SelectionSet& selection, base::EntityId entityId)
{
    const auto refs = EffectiveRefs(selection);
    return std::any_of(refs.begin(), refs.end(), [entityId](const auto& ref) {
        return ref.entityId == entityId;
    });
}

bool IsSelected(const SelectionSet& selection, const SelectionRef& target)
{
    const auto refs = EffectiveRefs(selection);
    return std::any_of(refs.begin(), refs.end(), [&](const auto& ref) {
        return SameTarget(ref, target);
    });
}

std::size_t SelectionItemCount(const SelectionSet& selection)
{
    return EffectiveRefs(selection).size();
}

int SelectedCountOfKind(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot, domain::EntityKind kind)
{
    const SelectionSet normalized = SelectionFromRefs(EffectiveRefs(selection));
    int count = 0;
    for (const base::EntityId id : normalized.entityIds) {
        for (const auto& entity : snapshot.entities) {
            if (entity.id == id && entity.kind == kind) {
                ++count;
                break;
            }
        }
    }
    return count;
}

SelectionSet SelectAllOfKind(const document::DocumentSnapshot& snapshot,
    domain::EntityKind kind)
{
    SelectionSet selection;
    for (const auto& entity : snapshot.entities) {
        // 隠したものは選ばない。隠したのに次の操作へ巻き込まれると、
        // 画面に出ていないものが動いて、なぜ変わったのか分からなくなる。
        if (entity.kind == kind && entity.visibility == domain::Visibility::Visible) {
            SelectionRef ref;
            ref.entityId = entity.id;
            selection.ordered.push_back(ref);
        }
    }
    return SelectionFromRefs(std::move(selection.ordered));
}

SelectionSet PruneSelection(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot)
{
    std::vector<SelectionRef> kept;
    for (const auto& ref : EffectiveRefs(selection)) {
        if (HasEntity(snapshot, ref.entityId)) {
            kept.push_back(ref);
        }
    }
    return SelectionFromRefs(std::move(kept));
}

std::vector<geometry::CurveSegment> SelectedCurves(const SelectionSet& selection,
    const modeling::SnapScene& scene)
{
    std::vector<geometry::CurveSegment> curves;
    std::vector<std::pair<base::EntityId, base::SegmentId>> added;
    for (const auto& ref : EffectiveRefs(selection)) {
        for (const auto& curve : scene.curves) {
            if (curve.entityId != ref.entityId) {
                continue;
            }
            if (ref.kind == SelectionElementKind::Edge && ref.segmentId.has_value()
                && curve.segmentId != *ref.segmentId) {
                continue;
            }
            if (ref.kind != SelectionElementKind::Object
                && ref.kind != SelectionElementKind::Edge) {
                continue;
            }
            const auto key = std::make_pair(curve.entityId, curve.segmentId);
            if (std::find(added.begin(), added.end(), key) == added.end()) {
                added.push_back(key);
                curves.push_back(curve.segment);
            }
        }
    }
    return curves;
}

} // namespace kachakacha::v2::app
