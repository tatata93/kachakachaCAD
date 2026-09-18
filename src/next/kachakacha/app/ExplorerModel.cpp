#include "kachakacha/app/ExplorerModel.h"

#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/domain/Feature.h"

namespace kachakacha::v2::app {

std::string_view ExplorerSectionNameJa(ExplorerSection section) noexcept
{
    switch (section) {
    case ExplorerSection::Origin:        return "原点";
    case ExplorerSection::WorkPlanes:    return "作業面";
    case ExplorerSection::Groups:        return "グループ";
    case ExplorerSection::Wires:         return "ワイヤー";
    case ExplorerSection::Surfaces:      return "面";
    case ExplorerSection::Solids:        return "立体";
    case ExplorerSection::Approximation: return "近似";
    case ExplorerSection::Generated:     return "生成物";
    }
    return "";
}

const std::vector<ExplorerSection>& ExplorerSections()
{
    static const std::vector<ExplorerSection> sections{ExplorerSection::Origin,
        ExplorerSection::WorkPlanes, ExplorerSection::Groups, ExplorerSection::Wires,
        ExplorerSection::Surfaces, ExplorerSection::Solids, ExplorerSection::Approximation,
        ExplorerSection::Generated};
    return sections;
}

ExplorerSection SectionForEntity(const document::DocumentSnapshot& snapshot,
    const domain::Entity& entity)
{
    if (IsOriginPlane(snapshot, entity.id)) {
        return ExplorerSection::Origin;
    }
    if (entity.groupId.has_value()) {
        return ExplorerSection::Groups;
    }
    for (const auto& feature : snapshot.features) {
        if (feature.id == entity.createdBy
            && feature.type == domain::FeatureType::FreezeDerived) {
            return ExplorerSection::Generated;
        }
    }
    switch (entity.kind) {
    case domain::EntityKind::WorkPlane:        return ExplorerSection::WorkPlanes;
    case domain::EntityKind::Point:
    case domain::EntityKind::Wire:             return ExplorerSection::Wires;
    case domain::EntityKind::GuideSurface:     return ExplorerSection::Surfaces;
    case domain::EntityKind::Part:             return ExplorerSection::Solids;
    case domain::EntityKind::FabricationModel: return ExplorerSection::Approximation;
    case domain::EntityKind::Pattern:          return ExplorerSection::Generated;
    }
    return ExplorerSection::Wires;
}

std::string_view ExplorerKindNameJa(domain::EntityKind kind) noexcept
{
    switch (kind) {
    case domain::EntityKind::Point:            return "点";
    case domain::EntityKind::WorkPlane:        return "作業面";
    case domain::EntityKind::Wire:             return "ワイヤー";
    case domain::EntityKind::GuideSurface:     return "面";
    case domain::EntityKind::Part:             return "立体";
    case domain::EntityKind::FabricationModel: return "近似モデル";
    case domain::EntityKind::Pattern:          return "型紙";
    }
    return "不明";
}

std::string UniqueDisplayName(const document::DocumentSnapshot& snapshot,
    domain::EntityKind kind, const std::string& base)
{
    const auto taken = [&](const std::string& candidate) {
        for (const auto& entity : snapshot.entities) {
            if (entity.kind == kind && entity.displayName == candidate) {
                return true;
            }
        }
        return false;
    };
    if (!taken(base)) {
        return base;
    }
    // 2 から数える。「押し出し」の次は「押し出し 2」。1 は付けない(最初のものは素の名前)。
    for (int number = 2; number < 100000; ++number) {
        const std::string candidate = base + " " + std::to_string(number);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return base;
}

} // namespace kachakacha::v2::app
