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

//! 矩形選択と認める最小の移動量(logical px)。
//! GrabToMove の 4px とは別の門である。片方を直したつもりで両方が動くと、
//! 「掴んだ」と「囲んだ」の境目が勝手に入れ替わる。
constexpr double kBoxSelectMinimumDragPx = 5.0;

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

//! その選択参照が、この線分を覆っているか。
//!
//! 物体を選んだ参照は全線分を覆い、線分を選んだ参照はその線分だけを覆う。
//! 点・面・制御点・作業平面の参照は線を覆わない。
[[nodiscard]] bool RefCoversCurve(const SelectionRef& ref, base::EntityId entityId,
    base::SegmentId segmentId)
{
    if (ref.entityId != entityId) {
        return false;
    }
    if (ref.kind != SelectionElementKind::Object && ref.kind != SelectionElementKind::Edge) {
        return false;
    }
    return !(ref.kind == SelectionElementKind::Edge && ref.segmentId.has_value()
        && *ref.segmentId != segmentId);
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

[[nodiscard]] std::vector<PickCandidate> CollectCurvePicks(
    const modeling::SnapScene& scene, const geometry::ScreenMapping& mapping,
    const ScreenPoint& pointer, const geometry::GeometryTolerance& tolerance,
    const PickFocus& focus)
{
    std::vector<PickCandidate> candidates;
    for (const auto& curve : scene.curves) {
        if (!PickableOffPlaneCurve(focus.drawing, focus.dimOffPlane,
                CurveLiesOnPlane(curve.segment, focus.plane))) {
            continue;
        }
        const auto approach = ApproachToCurvePx(curve.segment, mapping, pointer,
            tolerance.interactiveJoinMm);
        if (!approach.has_value() || approach->distancePx > tolerance.displayPickPx) {
            continue;
        }
        PickCandidate candidate;
        candidate.entityId = curve.entityId;
        candidate.segmentId = curve.segmentId;
        candidate.kind = SelectionElementKind::Edge;
        candidate.curveParameter = approach->parameter;
        candidate.hitPoint = approach->point;
        candidate.distancePx = approach->distancePx;
        candidates.push_back(candidate);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& first,
                                                        const auto& second) {
        return first.distancePx < second.distancePx;
    });
    return candidates;
}

[[nodiscard]] std::vector<PickCandidate> CollectPointPicks(
    const modeling::SnapScene& scene, const geometry::ScreenMapping& mapping,
    const ScreenPoint& pointer, const geometry::GeometryTolerance& tolerance)
{
    std::vector<PickCandidate> candidates;
    for (const auto& point : scene.points) {
        const auto screen = mapping.Project(point.position);
        if (!screen.has_value()) {
            continue;
        }
        const double distance = std::hypot(screen->x - pointer.x, screen->y - pointer.y);
        if (distance > tolerance.displayPickPx) {
            continue;
        }
        PickCandidate candidate;
        candidate.entityId = point.entityId;
        candidate.kind = SelectionElementKind::Vertex;
        candidate.hitPoint = point.position;
        candidate.distancePx = distance;
        candidates.push_back(candidate);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& first,
                                                        const auto& second) {
        return first.distancePx < second.distancePx;
    });
    return candidates;
}

//! 矩形選択の途中集計。物体1つ分。
struct BoxEntityState {
    base::EntityId entityId;
    BoxReach reach;
};

//! その物体の集計欄。無ければ場面の順で足す。
//! 返した参照は、次に足すまでのあいだだけ使う。
[[nodiscard]] BoxEntityState& BoxStateFor(std::vector<BoxEntityState>& states,
    base::EntityId entityId)
{
    for (auto& state : states) {
        if (state.entityId == entityId) {
            return state;
        }
    }
    states.push_back(BoxEntityState{entityId, BoxReach{}});
    return states.back();
}

} // namespace

bool ScreenBox::Contains(const geometry::ScreenPoint& point) const noexcept
{
    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

double BoxSelectionMinimumDragPx() noexcept
{
    return kBoxSelectMinimumDragPx;
}

bool BoxSelectionIsMeaningful(const geometry::ScreenPoint& start,
    const geometry::ScreenPoint& end) noexcept
{
    return std::hypot(end.x - start.x, end.y - start.y) >= kBoxSelectMinimumDragPx;
}

BoxSelection MakeBoxSelection(const geometry::ScreenPoint& start,
    const geometry::ScreenPoint& end) noexcept
{
    BoxSelection made;
    made.box.minX = std::min(start.x, end.x);
    made.box.maxX = std::max(start.x, end.x);
    made.box.minY = std::min(start.y, end.y);
    made.box.maxY = std::max(start.y, end.y);
    // 取り方は横向きだけで決める(§4.2)。上下どちらへ引いても意味は変わらない。
    made.kind = end.x >= start.x ? BoxSelectionKind::Contained : BoxSelectionKind::Crossing;
    return made;
}

bool BoxTouchesSegment(const ScreenBox& box, const geometry::ScreenPoint& start,
    const geometry::ScreenPoint& end) noexcept
{
    if (box.Contains(start) || box.Contains(end)) {
        return true;
    }
    // Liang–Barsky。矩形の中に残る区間があれば触れている。
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    double enter = 0.0;
    double leave = 1.0;
    // denominator が厳密に0のときだけ「枠と平行」である。近似の0を許容差で丸めると、
    // わずかに傾いた線が平行扱いになって、触れているのに落ちる。
    // 傾きが小さいだけなら t が大きくなり、下の判定がそのまま外へ弾く。
    const auto clip = [&enter, &leave](double denominator, double numerator) {
        if (denominator == 0.0) {
            return numerator >= 0.0;
        }
        const double t = numerator / denominator;
        if (denominator < 0.0) {
            if (t > leave) {
                return false;
            }
            if (t > enter) {
                enter = t;
            }
            return true;
        }
        if (t < enter) {
            return false;
        }
        if (t < leave) {
            leave = t;
        }
        return true;
    };
    return clip(-dx, start.x - box.minX) && clip(dx, box.maxX - start.x)
        && clip(-dy, start.y - box.minY) && clip(dy, box.maxY - start.y);
}

bool BoxReach::Taken(BoxSelectionKind kind) const noexcept
{
    if (!projectable) {
        // 画面に1点も出ていないものは、囲んでも触れてもいない。
        return false;
    }
    return kind == BoxSelectionKind::Crossing ? touched : fullyInside;
}

void AccumulateBoxReach(BoxReach& reach, const ScreenBox& box,
    const geometry::ScreenMapping& mapping, const std::vector<geometry::Vector3>& polyline)
{
    std::optional<geometry::ScreenPoint> previous;
    for (const geometry::Vector3& world : polyline) {
        const auto screen = mapping.Project(world);
        if (!screen.has_value()) {
            // 画面へ写せない点がある。「完全に入った」とは言えない。
            reach.fullyInside = false;
            previous.reset();
            continue;
        }
        reach.projectable = true;
        const bool inside = box.Contains(*screen);
        if (!inside) {
            reach.fullyInside = false;
        }
        // 折れ線が枠を横切るだけでも触れている。細い矩形を線が跨ぐとき、
        // 点だけを見ていると取りこぼす。
        const bool crosses = previous.has_value()
            && BoxTouchesSegment(box, *previous, *screen);
        if (inside || crosses) {
            reach.touched = true;
            if (!reach.hitPoint.has_value()) {
                reach.hitPoint = world;
            }
        }
        previous = *screen;
    }
}

void AccumulateBoxCurve(BoxReach& reach, const ScreenBox& box,
    const geometry::ScreenMapping& mapping, const geometry::CurveSegment& segment,
    const geometry::GeometryTolerance& tolerance)
{
    const auto samples = geometry::SampleCurve(segment, tolerance.interactiveJoinMm);
    std::vector<geometry::Vector3> positions;
    positions.reserve(samples.size());
    for (const auto& sample : samples) {
        positions.push_back(sample.position);
    }
    AccumulateBoxReach(reach, box, mapping, positions);
}

void AccumulateBoxPoint(BoxReach& reach, const ScreenBox& box,
    const geometry::ScreenMapping& mapping, const geometry::Vector3& point)
{
    AccumulateBoxReach(reach, box, mapping, std::vector<geometry::Vector3>{point});
}

std::vector<PickCandidate> CollectBoxPickCandidates(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const BoxSelection& request,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus)
{
    std::vector<BoxEntityState> states;
    for (const auto& curve : scene.curves) {
        // 薄くして掴めなくしている線は、矩形でも掴まない。
        // 判断はクリックと同じ一箇所(app/PlaneFocus)から出す。
        if (!PickableOffPlaneCurve(focus.drawing, focus.dimOffPlane,
                CurveLiesOnPlane(curve.segment, focus.plane))) {
            continue;
        }
        AccumulateBoxCurve(BoxStateFor(states, curve.entityId).reach, request.box, mapping,
            curve.segment, tolerance);
    }
    for (const auto& point : scene.points) {
        AccumulateBoxPoint(BoxStateFor(states, point.entityId).reach, request.box, mapping,
            point.position);
    }
    std::vector<PickCandidate> candidates;
    for (const auto& state : states) {
        if (!state.reach.Taken(request.kind)) {
            continue;
        }
        PickCandidate candidate;
        candidate.entityId = state.entityId;
        // 矩形で選ぶのは対象そのもの。線分IDは付けない。
        candidate.kind = SelectionElementKind::Object;
        if (state.reach.hitPoint.has_value()) {
            candidate.hitPoint = *state.reach.hitPoint;
        }
        candidates.push_back(candidate);
    }
    return candidates;
}

std::vector<PickCandidate> CollectPickCandidates(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus)
{
    std::vector<PickCandidate> candidates = CollectPointPicks(scene, mapping, pointer,
        tolerance);
    auto curves = CollectCurvePicks(scene, mapping, pointer, tolerance, focus);
    candidates.insert(candidates.end(), curves.begin(), curves.end());
    return candidates;
}

std::optional<PickCandidate> PickCurve(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus)
{
    const auto candidates = CollectCurvePicks(scene, mapping, pointer, tolerance, focus);
    return candidates.empty() ? std::nullopt
                              : std::optional<PickCandidate>{candidates.front()};
}

std::optional<PickCandidate> PickPoint(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance)
{
    const auto candidates = CollectPointPicks(scene, mapping, pointer, tolerance);
    return candidates.empty() ? std::nullopt
                              : std::optional<PickCandidate>{candidates.front()};
}

std::optional<PickCandidate> PickEntity(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus)
{
    const auto candidates = CollectPickCandidates(scene, mapping, pointer, tolerance, focus);
    return candidates.empty() ? std::nullopt
                              : std::optional<PickCandidate>{candidates.front()};
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

SelectionSet ApplyBoxSelection(const SelectionSet& current,
    const std::vector<PickCandidate>& candidates, SelectionMode mode)
{
    // 同じ物体を線と塗った形の両方から拾うことがある。先に1つへまとめる。
    // まとめないと、Replace で件数が物の数と合わず、Toggle は2回当たって元へ戻る。
    std::vector<PickCandidate> unique;
    unique.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const bool seen = std::any_of(unique.begin(), unique.end(), [&](const auto& kept) {
            return kept.entityId == candidate.entityId;
        });
        if (!seen) {
            unique.push_back(candidate);
        }
    }
    if (mode == SelectionMode::Replace) {
        // 矩形は「これで選び直す」。1件ずつ Replace を重ねると最後の1件しか残らない。
        std::vector<SelectionRef> refs;
        refs.reserve(unique.size());
        for (const auto& candidate : unique) {
            refs.push_back(RefFromCandidate(candidate));
        }
        return SelectionFromRefs(std::move(refs));
    }
    std::vector<SelectionRef> refs = EffectiveRefs(current);
    for (const auto& candidate : unique) {
        // 照合は物体単位で行う。矩形で選ぶのは対象そのものだからである。
        // 部分要素ごとに見ると、線分を1つ選んでいるワイヤーを Ctrl+矩形で囲ったときに
        // 「物体」と「線分」が二重に入り、選択件数が物の数と合わなくなる。
        const bool already = std::any_of(refs.begin(), refs.end(), [&](const auto& ref) {
            return ref.entityId == candidate.entityId;
        });
        if (mode == SelectionMode::Subtract || (mode == SelectionMode::Toggle && already)) {
            refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const auto& ref) {
                return ref.entityId == candidate.entityId;
            }), refs.end());
            continue;
        }
        if (!already) {
            refs.push_back(RefFromCandidate(candidate));
        }
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

bool IsCurveSelected(const SelectionSet& selection, base::EntityId entityId,
    base::SegmentId segmentId)
{
    const auto refs = EffectiveRefs(selection);
    return std::any_of(refs.begin(), refs.end(), [&](const auto& ref) {
        return RefCoversCurve(ref, entityId, segmentId);
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
            if (!RefCoversCurve(ref, curve.entityId, curve.segmentId)) {
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
