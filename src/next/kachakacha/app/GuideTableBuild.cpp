#include "kachakacha/app/GuideTableBuild.h"

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <string>
#include <utility>

namespace kachakacha::v2::app {

using base::Diagnostic;
using base::EntityId;
using base::MakeError;
using base::Result;
using domain::CreateGuideSurfaceDefinition;
using domain::EntityKind;
using modeling::ChainRole;
using modeling::GuideSurfaceMethod;
using modeling::GuideTable;
using modeling::GuideTableSelection;

std::optional<GuideTableSelection> GuideSelectionOf(const document::Document& document,
    const modeling::SnapScene& scene, const EntityId& wireId)
{
    const auto* entity = document.FindEntity(wireId);
    if (entity == nullptr || entity->kind != EntityKind::Wire) {
        return std::nullopt;
    }
    GuideTableSelection chosen;
    chosen.sourceWireId = wireId;
    chosen.label = entity->displayName;
    for (const auto& curve : scene.curves) {
        if (curve.entityId == wireId) {
            chosen.segments.push_back(curve.segment);
        }
    }
    if (chosen.segments.empty()) {
        return std::nullopt;
    }
    return chosen;
}

GuideTableDraft AutoSectionTable(const document::Document& document,
    const modeling::SnapScene& scene, const std::vector<EntityId>& wireIds)
{
    GuideTableDraft made;
    // 作り方は断面の数で決まる。2本なら渡すだけ(ルールド)、3本以上なら
    // なめらかに通す(ロフト)。2本にロフトは使えないし、
    // 3本をルールドで渡すと真ん中の断面が捨てられる。
    made.table.method = GuideSurfaceMethod::RuledSections;
    for (const auto& id : wireIds) {
        const auto chosen = GuideSelectionOf(document, scene, id);
        if (!chosen.has_value()) {
            continue;
        }
        const auto added = modeling::AddSelectionAsNewRow(made.table, ChainRole::Section,
            *chosen);
        if (!added.HasValue()) {
            made.diagnostics = added.Diagnostics();
            return made;
        }
        made.table = added.Value();
        ++made.sections;
    }
    if (made.sections >= 3) {
        const auto switched = modeling::SetGuideTableMethod(made.table,
            GuideSurfaceMethod::LoftSections);
        if (!switched.HasValue()) {
            made.diagnostics = switched.Diagnostics();
            return made;
        }
        made.table = switched.Value();
    }
    return made;
}

namespace {

[[nodiscard]] Result<GuideTable> AddDefinitionRow(const document::Document& document,
    const modeling::SnapScene& scene, GuideTable table, ChainRole role,
    const domain::WireChainRef& chain, std::size_t rowNumber)
{
    const std::string where = std::to_string(rowNumber) + "行目";
    if (chain.segments.empty()) {
        return Result<GuideTable>::Failure(MakeError("GEO-R001",
            "保存した作り方に、元の線が無い行があります。", where));
    }
    if (role == ChainRole::SourceSurface) {
        const auto* entity = document.FindEntity(chain.segments.front().entityId);
        if (entity == nullptr || entity->kind != EntityKind::GuideSurface) {
            return Result<GuideTable>::Failure(MakeError("GEO-R002",
                "保存した作り方が指す元の面が、文書にありません。", where));
        }
        return modeling::AddSourceSurfaceRow(table, entity->id, entity->displayName);
    }
    const auto& tolerance = document.Snapshot().settings.tolerance;
    bool first = true;
    for (const auto& ref : chain.segments) {
        const auto chosen = GuideSelectionOf(document, scene, ref.entityId);
        if (!chosen.has_value()) {
            return Result<GuideTable>::Failure(MakeError("GEO-R002",
                "保存した作り方が指す線が、文書にありません。",
                where + ": " + ref.entityId.ToString()));
        }
        const auto added = first
            ? modeling::AddSelectionAsNewRow(table, role, *chosen)
            : AppendSelectionToRow(table, table.rows.size() - 1, *chosen, tolerance);
        if (!added.HasValue()) {
            return added;
        }
        table = added.Value();
        first = false;
    }
    const bool reversed = !chain.reversed.empty() && chain.reversed.front();
    if (reversed) {
        return modeling::ReverseRow(table, table.rows.size() - 1);
    }
    return Result<GuideTable>::Success(std::move(table));
}

} // namespace

Result<GuideTable> GuideTableFromDefinition(const document::Document& document,
    const modeling::SnapScene& scene, const CreateGuideSurfaceDefinition& definition)
{
    if (definition.roles.size() != definition.chains.size()) {
        return Result<GuideTable>::Failure(MakeError("GEO-R001",
            "保存した作り方の役割と線の数が合いません。",
            std::to_string(definition.roles.size()) + " 役割、"
                + std::to_string(definition.chains.size()) + " 鎖"));
    }
    GuideTable table;
    table.method = static_cast<GuideSurfaceMethod>(definition.method);
    table.offsetDistanceMm = definition.offsetDistanceMm;
    for (std::size_t index = 0; index < definition.chains.size(); ++index) {
        const auto role = static_cast<ChainRole>(definition.roles[index]);
        auto added = AddDefinitionRow(document, scene, std::move(table), role,
            definition.chains[index], index + 1);
        if (!added.HasValue()) {
            return added;
        }
        table = added.Value();
    }
    return Result<GuideTable>::Success(std::move(table));
}

CreateGuideSurfaceDefinition DefinitionFromGuideTable(const GuideTable& table)
{
    CreateGuideSurfaceDefinition definition;
    definition.method = static_cast<int>(table.method);
    definition.offsetDistanceMm = table.offsetDistanceMm;
    for (const auto& row : table.rows) {
        definition.roles.push_back(static_cast<int>(row.role));
        domain::WireChainRef chain;
        // 逆向きの行は、元の並びで覚える。作り直しは「並べてから逆にする」ので、
        // 逆にした並びを覚えると、作り直したときに二重に逆になる。
        std::vector<EntityId> ids = row.sourceWireIds;
        if (row.reversed) {
            std::reverse(ids.begin(), ids.end());
        }
        for (const auto& wireId : ids) {
            domain::SegmentRef ref;
            ref.entityId = wireId;
            chain.segments.push_back(ref);
            chain.reversed.push_back(row.reversed);
        }
        definition.chains.push_back(std::move(chain));
    }
    return definition;
}

std::vector<EntityId> GuideTableInputIds(const GuideTable& table)
{
    std::vector<EntityId> ids;
    for (const auto& row : table.rows) {
        ids.insert(ids.end(), row.sourceWireIds.begin(), row.sourceWireIds.end());
    }
    return ids;
}

Result<GuideTable> AppendSelectionToRow(const GuideTable& table, std::size_t rowIndex,
    const GuideTableSelection& selection, const geometry::GeometryTolerance& tolerance)
{
    const auto direct = modeling::AddSelectionToRow(table, rowIndex, selection, tolerance);
    if (direct.HasValue()) {
        return direct;
    }
    // 端につながらないときは、選んだ線を逆向きにしてもう一度だけ試す。
    // 人が線を引いた向きは、表の向きと合っているとは限らない。
    GuideTableSelection flipped;
    flipped.sourceWireId = selection.sourceWireId;
    flipped.label = selection.label;
    for (auto item = selection.segments.rbegin(); item != selection.segments.rend(); ++item) {
        const auto reversed = geometry::ReverseCurve(*item);
        if (!reversed.HasValue()) {
            return direct;
        }
        flipped.segments.push_back(reversed.Value());
    }
    const auto retried = modeling::AddSelectionToRow(table, rowIndex, flipped, tolerance);
    return retried.HasValue() ? retried : direct;
}

std::string_view GuideSurfaceMethodLabelJa(GuideSurfaceMethod method) noexcept
{
    switch (method) {
    case GuideSurfaceMethod::PlanarBoundary: return "平面(閉じた外形と穴)";
    case GuideSurfaceMethod::RuledSections:  return "ルールド(断面2つを直線で渡す)";
    case GuideSurfaceMethod::LoftSections:   return "ロフト(断面3つ以上をなめらかに通す)";
    case GuideSurfaceMethod::GuidedLoft:     return "案内付きロフト(外形U 2本と断面)";
    case GuideSurfaceMethod::GordonNetwork:  return "曲線網(外形U と外形V を通す)";
    case GuideSurfaceMethod::BoundaryFill:   return "境界埋め(閉じた3〜4辺)";
    case GuideSurfaceMethod::OffsetGuide:    return "離した面(元の面と距離)";
    }
    return "不明";
}

const std::vector<GuideSurfaceMethod>& GuideSurfaceMethods()
{
    static const std::vector<GuideSurfaceMethod> methods{
        GuideSurfaceMethod::PlanarBoundary, GuideSurfaceMethod::RuledSections,
        GuideSurfaceMethod::LoftSections, GuideSurfaceMethod::GuidedLoft,
        GuideSurfaceMethod::GordonNetwork, GuideSurfaceMethod::BoundaryFill,
        GuideSurfaceMethod::OffsetGuide};
    return methods;
}

} // namespace kachakacha::v2::app
