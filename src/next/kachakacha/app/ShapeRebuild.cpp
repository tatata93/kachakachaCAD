#include "kachakacha/app/ShapeRebuild.h"

#include "kachakacha/domain/Feature.h"

#include <algorithm>
#include <variant>

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
    case FeatureType::ThickenSurface:
        return ShapeRebuildKind::ThickenSurface;
    case FeatureType::EditSurface:
        return ShapeRebuildKind::EditSurface;
    case FeatureType::CreateFabricationModel:
        return ShapeRebuildKind::FabricationModel;
    case FeatureType::TransformPart:
        return ShapeRebuildKind::TransformPart;
    case FeatureType::CreateSolid:
        return ShapeRebuildKind::Solid;
    default:
        return std::nullopt;
    }
}

//! その作り方が出したもの。1つも出していなければ空の id。
[[nodiscard]] base::EntityId OutputOf(const domain::Feature& feature)
{
    return feature.outputs.empty() ? base::EntityId{} : feature.outputs.front().entityId;
}

[[nodiscard]] bool SameValue(const geometry::EvaluatedValue& a, const geometry::EvaluatedValue& b)
{
    return a.value == b.value && a.kind == b.kind && a.expression == b.expression;
}

//! 押し出しの定義がまったく同じか(同じ 1 回の押し出しから出来た部品どうし)。
[[nodiscard]] bool SameExtrude(const domain::Feature& a, const domain::Feature& b)
{
    const auto* left = std::get_if<domain::ExtrudeDefinition>(&a.definition);
    const auto* right = std::get_if<domain::ExtrudeDefinition>(&b.definition);
    return left != nullptr && right != nullptr && left->profiles == right->profiles
        && left->targets == right->targets && left->extentMode == right->extentMode
        && left->booleanMode == right->booleanMode && SameValue(left->distance, right->distance)
        && left->direction.x == right->direction.x && left->direction.y == right->direction.y
        && left->direction.z == right->direction.z;
}

//! 作った順で、定義の同じ押し出しがいくつ前にあるか。切ってあるものも数える
//! (作り直すと、切ってあっても同じ並びで立体が出来る)。
[[nodiscard]] std::size_t ExtrudeOrdinal(const document::DocumentSnapshot& snapshot,
    const domain::Feature& feature)
{
    std::size_t ordinal = 0;
    for (const domain::Feature& other : snapshot.features) {
        if (other.id == feature.id) {
            break;
        }
        if (other.type == FeatureType::Extrude && SameExtrude(other, feature)) {
            ++ordinal;
        }
    }
    return ordinal;
}

} // namespace

std::string_view ShapeRebuildKindNameJa(ShapeRebuildKind kind) noexcept
{
    switch (kind) {
    case ShapeRebuildKind::Extrude:      return "押し出し";
    case ShapeRebuildKind::WireCage:     return "かごから部品";
    case ShapeRebuildKind::Boolean:      return "足す・引く";
    case ShapeRebuildKind::GuideSurface: return "形状ガイド";
    case ShapeRebuildKind::ThickenSurface:   return "面に厚み";
    case ShapeRebuildKind::FabricationModel: return "近似モデル";
    case ShapeRebuildKind::EditSurface:      return "面の編集";
    case ShapeRebuildKind::TransformPart:    return "部品の配置";
    case ShapeRebuildKind::Solid:            return "立体の作成";
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
        ShapeRebuildStep step{*kind, found->id, output, found->displayName};
        if (*kind == ShapeRebuildKind::Extrude) {
            step.outputOrdinal = ExtrudeOrdinal(snapshot, *found);
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

bool NeedsShapeRebuild(const document::DocumentSnapshot& snapshot)
{
    return !PlanShapeRebuild(snapshot).empty();
}

} // namespace kachakacha::v2::app
