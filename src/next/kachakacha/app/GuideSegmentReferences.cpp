#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::app {
namespace {
using geometry::CurveSegment;

std::optional<CurveSegment> Portion(const CurveSegment& source, double a, double b)
{
    if (!std::isfinite(a) || !std::isfinite(b) || a < 0 || b > 1 || a >= b) return {};
    auto curve = source;
    if (b < 1.0) {
        const auto split = curve.Split(b);
        if (!split.HasValue()) return {};
        curve = *split.Value().first;
    }
    if (a > 0.0) {
        const auto split = curve.Split(a / b);
        if (!split.HasValue()) return {};
        curve = *split.Value().second;
    }
    return curve;
}

bool Matches(const CurveSegment& a, const CurveSegment& b, double eps)
{
    for (int i = 0; i <= 16; ++i)
        if (geometry::Distance(a.Evaluate(i / 16.0), b.Evaluate(i / 16.0)) > eps) return false;
    return true;
}

std::optional<CurveSegment> StoredSegment(const document::Document& document,
    const domain::SegmentRef& ref)
{
    const auto* entity = document.FindEntity(ref.entityId);
    const auto* feature = entity ? document.FindFeature(entity->createdBy) : nullptr;
    const auto* wire = feature ? std::get_if<domain::CreateWireDefinition>(&feature->definition) : nullptr;
    if (!wire) return {};
    const auto at = std::find(wire->segmentIds.begin(), wire->segmentIds.end(), ref.segmentId);
    if (at == wire->segmentIds.end()) return {};
    const auto index = static_cast<std::size_t>(at - wire->segmentIds.begin());
    return index < wire->segments.size() ? std::optional<CurveSegment>(wire->segments[index]) : std::nullopt;
}

bool ReferenceCurve(const CurveSegment& wanted, const modeling::SnapCurve& source,
    domain::WireChainRef& chain, double eps)
{
    for (bool reverse : {false, true}) {
        const auto flipped = geometry::ReverseCurve(wanted);
        if (reverse && !flipped.HasValue()) continue;
        const auto& oriented = reverse ? flipped.Value() : wanted;
        double a = 0.0, b = 1.0;
        if (!Matches(oriented, source.segment, eps)) {
            const auto start = source.segment.ClosestPoint(oriented.StartPoint());
            const auto end = source.segment.ClosestPoint(oriented.EndPoint());
            if (start.distance > eps || end.distance > eps) continue;
            a = start.parameter;
            b = end.parameter;
            // The seam of a closed curve is both 0 and 1.
            if (b < a && geometry::Distance(source.segment.EndPoint(), oriented.EndPoint()) <= eps) b = 1;
            const auto portion = Portion(source.segment, a, b);
            if (!portion || !Matches(oriented, *portion, eps)) continue;
        }
        chain.segments.push_back({source.entityId, source.segmentId, a, b});
        chain.reversed.push_back(reverse);
        return true;
    }
    return false;
}
} // namespace

base::Result<domain::CreateGuideSurfaceDefinition> DefinitionFromGuideTable(
    const modeling::GuideTable& table, const modeling::SnapScene& scene, double toleranceMm)
{
    using Out = base::Result<domain::CreateGuideSurfaceDefinition>;
    auto definition = DefinitionFromGuideTable(table);
    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        const auto& row = table.rows[i];
        if (row.role == modeling::ChainRole::SourceSurface) continue;
        domain::WireChainRef chain;
        chain.rowReversed = row.reversed;
        for (const auto& segment : row.segments) {
            bool found = false;
            for (const auto& source : scene.curves) {
                if (source.segmentId.IsNil() || std::find(row.sourceWireIds.begin(),
                        row.sourceWireIds.end(), source.entityId) == row.sourceWireIds.end()) continue;
                if (ReferenceCurve(segment, source, chain, toleranceMm)) { found = true; break; }
            }
            if (!found) return Out::Failure(base::MakeError("GEO-R002",
                "面の境界区間を元の線へ対応付けられません。線を選び直してください。"));
        }
        definition.chains[i] = std::move(chain);
    }
    return Out::Success(std::move(definition));
}

base::Result<modeling::GuideTableSelection> ResolveGuideSegmentChain(
    const document::Document& document, const modeling::SnapScene& scene,
    const domain::WireChainRef& chain)
{
    using Out = base::Result<modeling::GuideTableSelection>;
    modeling::GuideTableSelection selected;
    for (std::size_t i = 0; i < chain.segments.size(); ++i) {
        const auto& ref = chain.segments[i];
        const auto source = std::find_if(scene.curves.begin(), scene.curves.end(), [&](const auto& c) {
            return c.entityId == ref.entityId && c.segmentId == ref.segmentId;
        });
        const auto stored = source == scene.curves.end() ? StoredSegment(document, ref)
            : std::optional<CurveSegment>(source->segment);
        if (!stored) return Out::Failure(base::MakeError("GEO-R002",
            "面が参照する元の線の区間が見つかりません。"));
        auto curve = Portion(*stored, ref.startParameter, ref.endParameter);
        if (!curve) return Out::Failure(base::MakeError("GEO-R001", "保存した境界の区間が不正です。"));
        if (i < chain.reversed.size() && chain.reversed[i]) {
            const auto reversed = geometry::ReverseCurve(*curve);
            if (!reversed.HasValue()) return Out::Failure(reversed.Diagnostics());
            curve = reversed.Value();
        }
        selected.segments.push_back(*curve);
    }
    if (!chain.segments.empty()) {
        selected.sourceWireId = chain.segments.front().entityId;
        const auto* entity = document.FindEntity(selected.sourceWireId);
        if (entity) selected.label = entity->displayName;
    }
    return Out::Success(std::move(selected));
}
} // namespace kachakacha::v2::app
