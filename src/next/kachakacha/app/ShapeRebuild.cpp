#include "kachakacha/app/ShapeRebuild.h"

#include "kachakacha/domain/Feature.h"

#include <algorithm>

namespace kachakacha::v2::app {
namespace {

using domain::FeatureType;

//! その作り方が実形状を作るか。作るなら種類を返す。
[[nodiscard]] std::optional<ShapeRebuildKind> KindOf(FeatureType type) noexcept
{
    switch (type) {
    case FeatureType::Extrude:
        return ShapeRebuildKind::Extrude;
    case FeatureType::CreatePartFromWireCage:
        return ShapeRebuildKind::WireCage;
    case FeatureType::Boolean:
        return ShapeRebuildKind::Boolean;
    case FeatureType::CreateGuideSurface:
        return ShapeRebuildKind::GuideSurface;
    default:
        return std::nullopt;
    }
}

//! その作り方が出したもの。1つも出していなければ空の id。
[[nodiscard]] base::EntityId OutputOf(const domain::Feature& feature)
{
    return feature.outputs.empty() ? base::EntityId{} : feature.outputs.front().entityId;
}

} // namespace

std::string_view ShapeRebuildKindNameJa(ShapeRebuildKind kind) noexcept
{
    switch (kind) {
    case ShapeRebuildKind::Extrude:      return "押し出し";
    case ShapeRebuildKind::WireCage:     return "かごから部品";
    case ShapeRebuildKind::Boolean:      return "足す・引く";
    case ShapeRebuildKind::GuideSurface: return "形状ガイド";
    }
    return "不明";
}

std::vector<ShapeRebuildStep> PlanShapeRebuild(const document::DocumentSnapshot& snapshot)
{
    std::vector<ShapeRebuildStep> steps;
    // 評価順に従う。並びを自分で決め直さない。
    // 決め直すと、足し算が材料より先に来て、材料がまだ無いと言われる。
    for (const base::FeatureId& id : snapshot.evaluationOrder) {
        const auto found = std::find_if(snapshot.features.begin(), snapshot.features.end(),
            [&](const domain::Feature& feature) { return feature.id == id; });
        if (found == snapshot.features.end() || !found->enabled) {
            continue;
        }
        const auto kind = KindOf(found->type);
        if (!kind.has_value()) {
            continue;
        }
        const base::EntityId output = OutputOf(*found);
        if (output.IsNil()) {
            continue;
        }
        steps.push_back(ShapeRebuildStep{*kind, found->id, output, found->displayName});
    }
    return steps;
}

bool NeedsShapeRebuild(const document::DocumentSnapshot& snapshot)
{
    return !PlanShapeRebuild(snapshot).empty();
}

} // namespace kachakacha::v2::app
