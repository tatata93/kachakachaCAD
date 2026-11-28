#include "V2SelectionMeasurement.h"

#include "kachakacha/app/ProfileCurveBounds.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Measurement.h"

#include <algorithm>
#include <array>
#include <set>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace v2ui {
namespace {
using namespace kachakacha::v2;
using geometry::CurveKind;
using geometry::CurveSegment;
using geometry::Vector3;

struct SelectedMeasureItem {
    std::optional<Vector3> point;
    std::vector<const CurveSegment*> curves;
};

struct MeasureSelectionTarget {
    base::EntityId entity;
    std::optional<app::SelectionRef> vertex;
    bool wholeWire = false;
    std::set<base::SegmentId> segments;
};

bool CollectTarget(std::vector<MeasureSelectionTarget>& items, const app::SelectionRef& ref)
{
    using Kind = app::SelectionElementKind;
    if (ref.kind != Kind::Object && ref.kind != Kind::Edge && ref.kind != Kind::Vertex) return false;
    auto existing = std::find_if(items.begin(),items.end(),[&](const auto& item) {
        return ref.kind != Kind::Vertex && !item.vertex && item.entity == ref.entityId;
    });
    if (existing == items.end()) {
        if (items.size() == 2) return false;
        items.push_back({ref.entityId, std::nullopt, false, {}});
        existing = std::prev(items.end());
    }
    if (ref.kind == Kind::Vertex) existing->vertex = ref;
    else if (ref.segmentId && ref.kind == Kind::Edge) existing->segments.insert(*ref.segmentId);
    else existing->wholeWire = true;
    return true;
}

SelectedMeasureItem Resolve(const MeasureSelectionTarget& target, const document::Document& document,
    const modeling::SnapScene& scene)
{
    const auto* entity = document.FindEntity(target.entity);
    if (!entity) return {};
    if (entity->kind == domain::EntityKind::Point) {
        const auto point = std::find_if(scene.points.begin(), scene.points.end(),
            [&](const auto& item) { return item.entityId == target.entity; });
        return point == scene.points.end() ? SelectedMeasureItem{} : SelectedMeasureItem{point->position, {}};
    }
    SelectedMeasureItem value;
    for (const auto& curve : scene.curves) {
        if (curve.entityId != target.entity) continue;
        if (target.vertex) {
            const auto& ref = *target.vertex;
            if (ref.segmentId == curve.segmentId && ref.curveParameter
                && std::isfinite(*ref.curveParameter) && *ref.curveParameter >= 0.0 && *ref.curveParameter <= 1.0) {
                return {curve.segment.Evaluate(*ref.curveParameter), {}};
            }
        } else if (entity->kind == domain::EntityKind::Wire
            && (target.wholeWire || target.segments.contains(curve.segmentId))) {
            value.curves.push_back(&curve.segment);
        }
    }
    return value;
}

void AddComponents(app::MeasureOverlay& overlay, const Vector3& first, const Vector3& last)
{
    const std::array<Vector3, 4> corners{first, {last.x, first.y, first.z},
        {last.x, last.y, first.z}, last};
    const auto delta = last - first;
    for (int axis = 0; axis < 3; ++axis) {
        const auto length = axis == 0 ? delta.x : axis == 1 ? delta.y : delta.z;
        if (std::abs(length) > 1e-10)
            overlay.components.push_back({corners[axis], corners[axis + 1], axis, length});
    }
}

double TotalLength(const SelectedMeasureItem& item, double tolerance)
{
    double total = 0.0;
    for (const auto* curve : item.curves) total += geometry::MeasureCurveLength(*curve, tolerance);
    return total;
}

void MeasureOne(app::MeasureOverlay& overlay, const SelectedMeasureItem& item, double tolerance)
{
    if (item.curves.empty()) return;
    if (item.curves.size() > 1) {
        overlay.rows.push_back({"ワイヤー全長", app::FormatMillimetersJa(TotalLength(item, tolerance))});
        overlay.rows.push_back({"区間数", std::to_string(item.curves.size())});
        return;
    }
    const auto& curve = *item.curves.front();
    app::MeasureRequest request;
    request.curves.push_back(curve);
    request.toleranceMm = tolerance;
    const auto rows = app::BuildMeasureRows(request);
    const auto isUseful = [](const app::MeasureRow& row) {
        return row.labelJa == "種類" || row.labelJa == "長さ" || row.labelJa == "半径" || row.labelJa == "直径";
    };
    for (auto row : rows) {
        if (!isUseful(row)) continue;
        if (row.labelJa == "長さ" && curve.Kind() == CurveKind::CircularArc) row.labelJa = "弧長";
        if (row.labelJa == "長さ" && curve.Kind() == CurveKind::Circle) row.labelJa = "円周";
        overlay.rows.push_back(std::move(row));
    }
    if (curve.Kind() == CurveKind::Line) {
        AddComponents(overlay, curve.StartPoint(), curve.EndPoint());
        if (overlay.components.size() < 2) overlay.components.clear();
    } else if (geometry::MeasureCurveRadius(curve)) {
        overlay.lines.push_back({curve.Center(), curve.Evaluate(0.5)});
    }
}

struct WireApproach {
    geometry::ClosestApproach points;
    const CurveSegment* first = nullptr;
    const CurveSegment* second = nullptr;
    bool exact = true;
};

WireApproach ClosestWires(const SelectedMeasureItem& first, const SelectedMeasureItem& second)
{
    const auto boundsOf = [](const SelectedMeasureItem& item) {
        std::vector<geometry::Bounds3> bounds;
        bounds.reserve(item.curves.size());
        for (const auto* segment : item.curves) bounds.push_back(app::detail::ProfileCurveBounds(*segment));
        return bounds;
    };
    const auto firstBounds = boundsOf(first);
    const auto secondBounds = boundsOf(second);
    const auto lowerBound = [](const geometry::Bounds3& a, const geometry::Bounds3& b) {
        if (a.empty || b.empty) return 0.0;
        const auto x = std::max({0.0, a.minimum.x - b.maximum.x, b.minimum.x - a.maximum.x});
        const auto y = std::max({0.0, a.minimum.y - b.maximum.y, b.minimum.y - a.maximum.y});
        const auto z = std::max({0.0, a.minimum.z - b.maximum.z, b.minimum.z - a.maximum.z});
        return std::hypot(x, y, z);
    };
    WireApproach best;
    best.points.distanceMm = std::numeric_limits<double>::infinity();
    bool exact = true;
    for (std::size_t i = 0; i < first.curves.size(); ++i) {
        const auto* a = first.curves[i];
        for (std::size_t j = 0; j < second.curves.size(); ++j) {
            const auto* b = second.curves[j];
            if (lowerBound(firstBounds[i], secondBounds[j]) > best.points.distanceMm) continue;
            geometry::ClosestApproach between;
            if (a->Kind() == CurveKind::Line && b->Kind() == CurveKind::Line) {
                const auto line = geometry::ClosestApproachBetween({a->StartPoint(), a->EndPoint()},
                    {b->StartPoint(), b->EndPoint()});
                if (!line.valid) continue;
                between = {line.firstPoint, line.secondPoint, line.distanceMm,
                    line.firstParameter, line.secondParameter};
            } else {
                exact = false;
                between = geometry::MeasureCurveToCurve(*a, *b);
                const auto reverse = geometry::MeasureCurveToCurve(*b, *a);
                if (reverse.distanceMm < between.distanceMm)
                    between = {reverse.secondPoint, reverse.firstPoint, reverse.distanceMm,
                        reverse.secondParameter, reverse.firstParameter};
            }
            if (between.distanceMm < best.points.distanceMm)
                best = {between, a, b};
        }
    }
    best.exact = exact;
    return best;
}

void MeasurePair(app::MeasureOverlay& overlay, const SelectedMeasureItem& first,
    const SelectedMeasureItem& second, double tolerance)
{
    if (first.point && second.point) {
        const auto distance = geometry::MeasureTwoPoints(*first.point, *second.point);
        if (!distance.HasValue()) return;
        const auto& value = distance.Value();
        overlay.rows = {{"距離", app::FormatMillimetersJa(value.distanceMm)},
            {"ΔX", app::FormatMillimetersJa(value.deltaXMm)}, {"ΔY", app::FormatMillimetersJa(value.deltaYMm)},
            {"ΔZ", app::FormatMillimetersJa(value.deltaZMm)}};
        overlay.points = {*first.point, *second.point};
        overlay.lines.push_back(overlay.points);
        AddComponents(overlay, *first.point, *second.point);
        return;
    }
    if (first.curves.empty() || second.curves.empty()) return;
    overlay.rows.push_back({"1本目の長さ", app::FormatMillimetersJa(TotalLength(first, tolerance))});
    overlay.rows.push_back({"2本目の長さ", app::FormatMillimetersJa(TotalLength(second, tolerance))});
    const auto nearest = ClosestWires(first, second);
    if (!nearest.first || !nearest.second) return;
    overlay.rows.push_back({nearest.exact ? "最短距離" : "最短距離（近似）",
        app::FormatMillimetersJa(nearest.points.distanceMm)});
    overlay.points = {nearest.points.firstPoint, nearest.points.secondPoint};
    overlay.lines.push_back({nearest.points.firstPoint, nearest.points.secondPoint});
    const auto angle = geometry::MeasureTangentAngle(*nearest.first, nearest.points.firstParameter,
        *nearest.second, nearest.points.secondParameter);
    if (angle.HasValue()) {
        const bool twoLines = first.curves.size() == 1 && second.curves.size() == 1
            && nearest.first->Kind() == CurveKind::Line && nearest.second->Kind() == CurveKind::Line;
        overlay.rows.push_back({twoLines ? "なす角" : "最短点での接線角",
            app::FormatDegreesJa(angle.Value().acute.Value())});
    }
}
} // namespace

kachakacha::v2::app::MeasureOverlay BuildSelectionMeasure(const kachakacha::v2::app::SelectionSet& selection,
    const kachakacha::v2::document::Document& document, const kachakacha::v2::modeling::SnapScene& scene)
{
    using namespace kachakacha::v2;
    std::vector<MeasureSelectionTarget> targets;
    if (selection.ordered.empty()) {
        for (const auto& id : selection.entityIds)
            if (!CollectTarget(targets, app::SelectionRef{id})) return {};
    } else {
        for (const auto& ref : selection.ordered)
            if (!CollectTarget(targets, ref)) return {};
    }
    if (targets.empty()) return {};
    const double tolerance = document.Snapshot().settings.tolerance.interactiveJoinMm;
    app::MeasureOverlay overlay;
    overlay.automatic = true;
    const auto first = Resolve(targets[0], document, scene);
    if (targets.size() == 1) {
        MeasureOne(overlay, first, tolerance);
    } else {
        const auto second = Resolve(targets[1], document, scene);
        MeasurePair(overlay, first, second, tolerance);
    }
    return overlay;
}
} // namespace v2ui
