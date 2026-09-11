#include "kachakacha/app/Selection.h"

#include "kachakacha/app/PlaneFocus.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::app {
namespace {

using geometry::ScreenPoint;
using geometry::Vector3;

//! 点と線分の距離。画面の上での話なので px。
[[nodiscard]] double DistanceToSegmentPx(const ScreenPoint& point, const ScreenPoint& start,
    const ScreenPoint& end)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (!(lengthSquared > 0.0)) {
        const double ex = point.x - start.x;
        const double ey = point.y - start.y;
        return std::sqrt(ex * ex + ey * ey);
    }
    double t = ((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = start.x + t * dx;
    const double cy = start.y + t * dy;
    const double ex = point.x - cx;
    const double ey = point.y - cy;
    return std::sqrt(ex * ex + ey * ey);
}

//! 1本の曲線と点の距離。画面へ落としてから測る。
//! 落とせない点(カメラの後ろ)はまたぐ弦ごと捨てる。
[[nodiscard]] std::optional<double> CurveDistancePx(const geometry::CurveSegment& segment,
    const geometry::ScreenMapping& mapping, const ScreenPoint& pointer,
    double toleranceMm)
{
    const auto samples = geometry::SampleCurve(segment, toleranceMm);
    std::optional<double> best;
    std::optional<ScreenPoint> previous;
    for (const auto& sample : samples) {
        const auto screen = mapping.Project(sample.position);
        if (!screen.has_value()) {
            previous.reset();
            continue;
        }
        if (previous.has_value()) {
            const double distance = DistanceToSegmentPx(pointer, *previous, *screen);
            if (!best.has_value() || distance < *best) {
                best = distance;
            }
        }
        previous = screen;
    }
    if (!best.has_value() && samples.size() == 1) {
        const auto screen = mapping.Project(samples.front().position);
        if (screen.has_value()) {
            best = DistanceToSegmentPx(pointer, *screen, *screen);
        }
    }
    return best;
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
        const auto distance = CurveDistancePx(curve.segment, mapping, pointer,
            tolerance.interactiveJoinMm);
        if (!distance.has_value() || *distance > tolerance.displayPickPx) {
            continue;
        }
        // 同じ距離のときは先に入っているものを残す。毎回同じ結果になる。
        if (best.has_value() && !(*distance < best->distancePx)) {
            continue;
        }
        PickCandidate candidate;
        candidate.entityId = curve.entityId;
        candidate.segmentId = curve.segmentId;
        candidate.distancePx = *distance;
        best = candidate;
    }
    return best;
}

SelectionSet ApplySelection(const SelectionSet& current,
    const std::optional<PickCandidate>& picked, SelectionMode mode)
{
    if (!picked.has_value()) {
        // 何も無いところを押した。素で押したときだけ空にする。
        return mode == SelectionMode::Replace ? SelectionSet{} : current;
    }
    const base::EntityId id = picked->entityId;
    const bool already = IsSelected(current, id);
    SelectionSet next;
    switch (mode) {
    case SelectionMode::Replace:
        next.entityIds.push_back(id);
        return next;
    case SelectionMode::Add:
        next = current;
        if (!already) {
            next.entityIds.push_back(id);
        }
        return next;
    case SelectionMode::Toggle:
        next = current;
        if (already) {
            next.entityIds.erase(
                std::remove(next.entityIds.begin(), next.entityIds.end(), id),
                next.entityIds.end());
        } else {
            next.entityIds.push_back(id);
        }
        return next;
    case SelectionMode::Subtract:
        next = current;
        next.entityIds.erase(
            std::remove(next.entityIds.begin(), next.entityIds.end(), id),
            next.entityIds.end());
        return next;
    }
    return current;
}

bool IsSelected(const SelectionSet& selection, base::EntityId entityId)
{
    return std::find(selection.entityIds.begin(), selection.entityIds.end(), entityId)
        != selection.entityIds.end();
}

int SelectedCountOfKind(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot, domain::EntityKind kind)
{
    int count = 0;
    for (const base::EntityId id : selection.entityIds) {
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
            selection.entityIds.push_back(entity.id);
        }
    }
    return selection;
}

SelectionSet PruneSelection(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot)
{
    SelectionSet next;
    for (const base::EntityId id : selection.entityIds) {
        for (const auto& entity : snapshot.entities) {
            if (entity.id == id) {
                next.entityIds.push_back(id);
                break;
            }
        }
    }
    return next;
}

std::vector<geometry::CurveSegment> SelectedCurves(const SelectionSet& selection,
    const modeling::SnapScene& scene)
{
    std::vector<geometry::CurveSegment> curves;
    for (const base::EntityId id : selection.entityIds) {
        for (const auto& curve : scene.curves) {
            if (curve.entityId == id) {
                curves.push_back(curve.segment);
            }
        }
    }
    return curves;
}

} // namespace kachakacha::v2::app
