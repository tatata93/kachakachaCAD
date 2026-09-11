#include "kachakacha/document/Commands.h"

#include <algorithm>
#include <set>

namespace kachakacha::v2::document {

using base::MakeError;
using base::MakeWarning;

namespace {

constexpr const char* kNotFound = "DOC-C001";
//! その種類には効かない操作。
constexpr const char* kNotAllowed = "DOC-C005";
constexpr const char* kStillUsed = "DOC-C002";
constexpr const char* kDuplicate = "DOC-C003";
constexpr const char* kEmptyName = "DOC-C004";
constexpr const char* kOriginPlane = "DOC-C009";

//! 原点の基準平面を作った操作か。消す・改名するのを断るために見る。
[[nodiscard]] bool IsOriginPlaneFeature(const Feature& feature)
{
    const auto* plane = std::get_if<domain::CreateWorkPlaneDefinition>(&feature.definition);
    return plane != nullptr && plane->isOriginPlane;
}

[[nodiscard]] Entity* FindMutable(DocumentSnapshot& snapshot, EntityId id)
{
    for (Entity& entity : snapshot.entities) {
        if (entity.id == id) {
            return &entity;
        }
    }
    return nullptr;
}

[[nodiscard]] Feature* FindMutableFeature(DocumentSnapshot& snapshot, FeatureId id)
{
    for (Feature& feature : snapshot.features) {
        if (feature.id == id) {
            return &feature;
        }
    }
    return nullptr;
}

} // namespace

// ---- AddFeature ----

AddFeatureCommand::AddFeatureCommand(Feature feature, std::vector<Entity> outputs,
    std::string label)
    : feature_(std::move(feature))
    , outputs_(std::move(outputs))
    , label_(std::move(label))
{
}

std::vector<Diagnostic> AddFeatureCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    if (FindMutableFeature(candidate, feature_.id) != nullptr) {
        diagnostics.push_back(MakeError(kDuplicate,
            "同じIDの操作履歴が既にあります。", feature_.displayName));
        return diagnostics;
    }
    for (const Entity& entity : outputs_) {
        if (FindMutable(candidate, entity.id) != nullptr) {
            diagnostics.push_back(MakeError(kDuplicate,
                "同じIDのオブジェクトが既にあります。", entity.displayName));
            return diagnostics;
        }
    }
    // 入力が実在するかは Validate が見るが、先に分かるならここで断る。
    for (const EntityId& input : feature_.inputEntityIds) {
        if (FindMutable(candidate, input) == nullptr) {
            diagnostics.push_back(MakeError(kNotFound,
                "入力に指定されたものが見つかりません。", input.ToString()));
            return diagnostics;
        }
    }
    candidate.features.push_back(feature_);
    for (Entity entity : outputs_) {
        entity.createdBy = feature_.id;
        if (!entity.groupId.has_value()) {
            // 派生物は Feature が指す派生グループへ入れ、作業中グループを使わない
            // (architecture-and-data.md §11)。混ぜると、作業中グループを切り替えた
            // だけで部品の居場所が変わってしまう。
            if (feature_.derivedGroupId.has_value()) {
                entity.groupId = feature_.derivedGroupId;
            } else if (candidate.settings.activeGroupId.has_value()) {
                entity.groupId = candidate.settings.activeGroupId;
            }
        }
        candidate.entities.push_back(std::move(entity));
    }
    return diagnostics;
}

// ---- RemoveFeature ----

RemoveFeatureCommand::RemoveFeatureCommand(FeatureId featureId, RemovePolicy policy,
    std::string label)
    : featureId_(featureId), policy_(policy), label_(std::move(label))
{
}

std::vector<Diagnostic> RemoveFeatureCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    const Feature* target = nullptr;
    for (const Feature& feature : candidate.features) {
        if (feature.id == featureId_) {
            target = &feature;
            break;
        }
    }
    if (target == nullptr) {
        diagnostics.push_back(MakeError(kNotFound,
            "消そうとした操作が見つかりません。", featureId_.ToString()));
        return diagnostics;
    }
    if (IsOriginPlaneFeature(*target)) {
        diagnostics.push_back(MakeError(kOriginPlane,
            "原点の基準平面は消せません。",
            target->displayName + " は原点の平面です。隠すことはできます。"));
        return diagnostics;
    }

    // この操作の出力を入力にしているFeature(下流)を集める。
    std::set<std::string> removeFeatures{featureId_.ToString()};
    std::set<std::string> removeEntities;
    std::vector<EntityId> pending;
    for (const domain::FeatureOutput& output : target->outputs) {
        removeEntities.insert(output.entityId.ToString());
        pending.push_back(output.entityId);
    }
    std::vector<std::string> downstreamNames;
    while (!pending.empty()) {
        const EntityId current = pending.back();
        pending.pop_back();
        for (const Feature& feature : candidate.features) {
            const bool uses = std::any_of(feature.inputEntityIds.begin(),
                feature.inputEntityIds.end(),
                [&current](const EntityId& id) { return id == current; });
            if (!uses || !removeFeatures.insert(feature.id.ToString()).second) {
                continue;
            }
            downstreamNames.push_back(feature.displayName);
            for (const domain::FeatureOutput& output : feature.outputs) {
                if (removeEntities.insert(output.entityId.ToString()).second) {
                    pending.push_back(output.entityId);
                }
            }
        }
    }

    if (!downstreamNames.empty() && policy_ == RemovePolicy::RefuseIfUsed) {
        std::string names;
        for (std::size_t index = 0; index < downstreamNames.size(); ++index) {
            if (index > 0) {
                names += "、";
            }
            names += downstreamNames[index];
        }
        diagnostics.push_back(MakeError(kStillUsed,
            "これを使っているものがあるので消せません。",
            "先に " + names + " を消すか、まとめて消すを選んでください。"));
        return diagnostics;
    }

    if (!downstreamNames.empty()) {
        diagnostics.push_back(MakeWarning("DOC-C005",
            "つながっているものも一緒に消しました。",
            std::to_string(downstreamNames.size()) + " 件"));
    }

    candidate.features.erase(
        std::remove_if(candidate.features.begin(), candidate.features.end(),
            [&removeFeatures](const Feature& feature) {
                return removeFeatures.count(feature.id.ToString()) > 0;
            }),
        candidate.features.end());
    candidate.entities.erase(
        std::remove_if(candidate.entities.begin(), candidate.entities.end(),
            [&removeEntities](const Entity& entity) {
                return removeEntities.count(entity.id.ToString()) > 0;
            }),
        candidate.entities.end());
    return diagnostics;
}

// ---- Rename ----

RenameEntityCommand::RenameEntityCommand(EntityId entityId, std::string newName)
    : entityId_(entityId), newName_(std::move(newName))
{
}

std::vector<Diagnostic> RenameEntityCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    Entity* entity = FindMutable(candidate, entityId_);
    if (entity == nullptr) {
        diagnostics.push_back(MakeError(kNotFound,
            "名前を変えるものが見つかりません。", entityId_.ToString()));
        return diagnostics;
    }
    if (newName_.empty()) {
        diagnostics.push_back(MakeError(kEmptyName,
            "名前を空にはできません。", {}));
        return diagnostics;
    }
    for (const Feature& feature : candidate.features) {
        if (feature.id == entity->createdBy && IsOriginPlaneFeature(feature)) {
            diagnostics.push_back(MakeError(kOriginPlane,
                "原点の基準平面の名前は変えられません。", entity->displayName));
            return diagnostics;
        }
    }
    // 同名は許す(名前は表示用。参照はIDで行う)。
    entity->displayName = newName_;
    ++entity->revision;
    return diagnostics;
}

// ---- Visibility ----

SetVisibilityCommand::SetVisibilityCommand(std::vector<EntityId> entityIds,
    domain::Visibility visibility)
    : entityIds_(std::move(entityIds)), visibility_(visibility)
{
}

std::vector<Diagnostic> SetVisibilityCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    for (const EntityId& id : entityIds_) {
        Entity* entity = FindMutable(candidate, id);
        if (entity == nullptr) {
            diagnostics.push_back(MakeError(kNotFound,
                "表示を変えるものが見つかりません。", id.ToString()));
            return diagnostics;
        }
        entity->visibility = visibility_;
        ++entity->revision;
    }
    return diagnostics;
}

// ---- 補助線と基準線 ----

SetConstructionCommand::SetConstructionCommand(std::vector<EntityId> entityIds,
    bool construction)
    : entityIds_(std::move(entityIds)), construction_(construction)
{
}

std::vector<Diagnostic> SetConstructionCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    for (const EntityId& id : entityIds_) {
        Entity* entity = FindMutable(candidate, id);
        if (entity == nullptr) {
            diagnostics.push_back(MakeError(kNotFound,
                "補助線にするものが見つかりません。", id.ToString()));
            return diagnostics;
        }
        // 部品を補助線にはできない。線に対してだけ意味がある。
        if (entity->kind != domain::EntityKind::Wire
            && entity->kind != domain::EntityKind::Point) {
            diagnostics.push_back(MakeError(kNotAllowed,
                "この種類は補助線にできません。",
                std::string(domain::EntityKindNameJa(entity->kind))
                    + " は補助線になりません。"));
            return diagnostics;
        }
        entity->construction = construction_;
        ++entity->revision;
    }
    return diagnostics;
}

SetManufacturingCommand::SetManufacturingCommand(std::vector<EntityId> entityIds,
    domain::ManufacturingProperties properties)
    : entityIds_(std::move(entityIds)), properties_(std::move(properties))
{
}

std::vector<Diagnostic> SetManufacturingCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    if (entityIds_.empty()) {
        diagnostics.push_back(MakeError(kNotFound, "入力に指定されたものが見つかりません。",
            "材料を付けるものを選んでください。"));
        return diagnostics;
    }
    if (properties_.layerCount < 1) {
        diagnostics.push_back(MakeError("DOC-C010", "この種類には材料を付けられません。",
            "積層の枚数は 1 以上にしてください。"));
        return diagnostics;
    }
    for (const EntityId& id : entityIds_) {
        domain::Entity* entity = FindMutable(candidate, id);
        if (entity == nullptr) {
            diagnostics.push_back(MakeError(kNotFound,
                "入力に指定されたものが見つかりません。", id.ToString()));
            return diagnostics;
        }
        const bool allowed = entity->kind == domain::EntityKind::Part
            || entity->kind == domain::EntityKind::GuideSurface
            || entity->kind == domain::EntityKind::FabricationModel;
        if (!allowed) {
            diagnostics.push_back(MakeError("DOC-C010", "この種類には材料を付けられません。",
                std::string(domain::EntityKindNameJa(entity->kind)) + " には材料も積層もありません。"));
            return diagnostics;
        }
        entity->manufacturing = properties_;
        ++entity->revision;
    }
    return diagnostics;
}

SetDatumCommand::SetDatumCommand(std::vector<EntityId> entityIds, bool datum)
    : entityIds_(std::move(entityIds)), datum_(datum)
{
}

std::vector<Diagnostic> SetDatumCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    for (const EntityId& id : entityIds_) {
        Entity* entity = FindMutable(candidate, id);
        if (entity == nullptr) {
            diagnostics.push_back(MakeError(kNotFound,
                "基準線にするものが見つかりません。", id.ToString()));
            return diagnostics;
        }
        if (entity->kind != domain::EntityKind::Wire
            && entity->kind != domain::EntityKind::WorkPlane) {
            diagnostics.push_back(MakeError(kNotAllowed,
                "この種類は基準にできません。",
                std::string(domain::EntityKindNameJa(entity->kind))
                    + " は基準線になりません。"));
            return diagnostics;
        }
        entity->datum = datum_;
        ++entity->revision;
    }
    return diagnostics;
}

// ---- 残した参照寸法 ----

AddReferenceDimensionCommand::AddReferenceDimensionCommand(ReferenceDimension dimension)
    : dimension_(std::move(dimension))
{
}

std::vector<Diagnostic> AddReferenceDimensionCommand::Apply(
    DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    if (dimension_.id.IsNil()) {
        diagnostics.push_back(MakeError(kEmptyName, "寸法のIDがありません。", {}));
        return diagnostics;
    }
    for (const ReferenceDimension& existing : candidate.referenceDimensions) {
        if (existing.id == dimension_.id) {
            diagnostics.push_back(MakeError(kDuplicate, "同じIDの寸法があります。",
                dimension_.id.ToString()));
            return diagnostics;
        }
    }
    if (dimension_.targets.empty()) {
        diagnostics.push_back(MakeError(kEmptyName, "何を測ったのかが入っていません。",
            "測った相手が必要です。"));
        return diagnostics;
    }
    for (const EntityId& target : dimension_.targets) {
        const bool found = std::any_of(candidate.entities.begin(), candidate.entities.end(),
            [&](const Entity& entity) { return entity.id == target; });
        if (!found) {
            diagnostics.push_back(MakeError(kNotFound, "測った相手が見つかりません。",
                target.ToString()));
            return diagnostics;
        }
    }
    candidate.referenceDimensions.push_back(dimension_);
    return diagnostics;
}

RemoveReferenceDimensionCommand::RemoveReferenceDimensionCommand(base::DimensionId id)
    : id_(id)
{
}

std::vector<Diagnostic> RemoveReferenceDimensionCommand::Apply(
    DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    const auto found = std::find_if(candidate.referenceDimensions.begin(),
        candidate.referenceDimensions.end(),
        [&](const ReferenceDimension& dimension) { return dimension.id == id_; });
    if (found == candidate.referenceDimensions.end()) {
        diagnostics.push_back(MakeError(kNotFound, "その寸法はありません。",
            id_.ToString()));
        return diagnostics;
    }
    candidate.referenceDimensions.erase(found);
    return diagnostics;
}

// ---- Enabled ----

SetFeatureEnabledCommand::SetFeatureEnabledCommand(FeatureId featureId, bool enabled)
    : featureId_(featureId), enabled_(enabled)
{
}

std::vector<Diagnostic> SetFeatureEnabledCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    Feature* feature = FindMutableFeature(candidate, featureId_);
    if (feature == nullptr) {
        diagnostics.push_back(MakeError(kNotFound,
            "対象の操作が見つかりません。", featureId_.ToString()));
        return diagnostics;
    }
    feature->enabled = enabled_;
    ++feature->revision;
    return diagnostics;
}

// ---- UpdateDefinition ----

UpdateFeatureDefinitionCommand::UpdateFeatureDefinitionCommand(FeatureId featureId,
    domain::FeatureDefinition definition, std::vector<EntityId> inputEntityIds,
    std::string label)
    : featureId_(featureId)
    , definition_(std::move(definition))
    , inputEntityIds_(std::move(inputEntityIds))
    , label_(std::move(label))
{
}

std::vector<Diagnostic> UpdateFeatureDefinitionCommand::Apply(
    DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    Feature* feature = FindMutableFeature(candidate, featureId_);
    if (feature == nullptr) {
        diagnostics.push_back(MakeError(kNotFound,
            "変更する操作が見つかりません。", featureId_.ToString()));
        return diagnostics;
    }
    for (const EntityId& input : inputEntityIds_) {
        if (FindMutable(candidate, input) == nullptr) {
            diagnostics.push_back(MakeError(kNotFound,
                "入力に指定されたものが見つかりません。", input.ToString()));
            return diagnostics;
        }
    }
    // 出力のIDは変えない。再計算しても同じものを指し続ける(DOC-011)。
    feature->definition = definition_;
    feature->inputEntityIds = inputEntityIds_;
    ++feature->revision;
    return diagnostics;
}

// ---- ActiveGroup ----

SetActiveGroupCommand::SetActiveGroupCommand(std::optional<GroupId> groupId)
    : groupId_(groupId)
{
}

std::vector<Diagnostic> SetActiveGroupCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    if (groupId_.has_value()) {
        const bool exists = std::any_of(candidate.groups.begin(), candidate.groups.end(),
            [this](const Group& group) { return group.id == *groupId_; });
        if (!exists) {
            diagnostics.push_back(MakeError(kNotFound,
                "入力に指定されたものが見つかりません。", groupId_->ToString()));
            return diagnostics;
        }
    }
    candidate.settings.activeGroupId = groupId_;
    return diagnostics;
}

// ---- Group ----

AddGroupCommand::AddGroupCommand(Group group) : group_(std::move(group)) {}

std::vector<Diagnostic> AddGroupCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    for (const Group& group : candidate.groups) {
        if (group.id == group_.id) {
            diagnostics.push_back(MakeError(kDuplicate,
                "同じIDのまとまりが既にあります。", group_.displayName));
            return diagnostics;
        }
    }
    if (group_.parentId.has_value()) {
        const bool parentExists = std::any_of(candidate.groups.begin(),
            candidate.groups.end(), [this](const Group& group) {
                return group.id == *group_.parentId;
            });
        if (!parentExists) {
            diagnostics.push_back(MakeError(kNotFound,
                "親のまとまりが見つかりません。", group_.parentId->ToString()));
            return diagnostics;
        }
    }
    candidate.groups.push_back(group_);
    return diagnostics;
}

MoveEntitiesToGroupCommand::MoveEntitiesToGroupCommand(std::vector<EntityId> entityIds,
    std::optional<GroupId> groupId)
    : entityIds_(std::move(entityIds)), groupId_(groupId)
{
}

std::vector<Diagnostic> MoveEntitiesToGroupCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    if (groupId_.has_value()) {
        const bool exists = std::any_of(candidate.groups.begin(), candidate.groups.end(),
            [this](const Group& group) { return group.id == *groupId_; });
        if (!exists) {
            diagnostics.push_back(MakeError(kNotFound,
                "移し先のまとまりが見つかりません。", groupId_->ToString()));
            return diagnostics;
        }
    }
    for (const EntityId& id : entityIds_) {
        Entity* entity = FindMutable(candidate, id);
        if (entity == nullptr) {
            diagnostics.push_back(MakeError(kNotFound,
                "移すものが見つかりません。", id.ToString()));
            return diagnostics;
        }
        entity->groupId = groupId_;
        ++entity->revision;
    }
    return diagnostics;
}

RemoveGroupCommand::RemoveGroupCommand(GroupId groupId) : groupId_(groupId) {}

std::vector<Diagnostic> RemoveGroupCommand::Apply(DocumentSnapshot& candidate) const
{
    std::vector<Diagnostic> diagnostics;
    std::optional<GroupId> parent;
    bool found = false;
    for (const Group& group : candidate.groups) {
        if (group.id == groupId_) {
            parent = group.parentId;
            found = true;
            break;
        }
    }
    if (!found) {
        diagnostics.push_back(MakeError(kNotFound,
            "消すまとまりが見つかりません。", groupId_.ToString()));
        return diagnostics;
    }
    // 中身は消さない。親のまとまりへ移す。
    for (Entity& entity : candidate.entities) {
        if (entity.groupId.has_value() && *entity.groupId == groupId_) {
            entity.groupId = parent;
            ++entity.revision;
        }
    }
    for (Group& group : candidate.groups) {
        if (group.parentId.has_value() && *group.parentId == groupId_) {
            group.parentId = parent;
        }
    }
    candidate.groups.erase(
        std::remove_if(candidate.groups.begin(), candidate.groups.end(),
            [this](const Group& group) { return group.id == groupId_; }),
        candidate.groups.end());
    if (candidate.settings.activeGroupId.has_value()
        && *candidate.settings.activeGroupId == groupId_) {
        candidate.settings.activeGroupId = parent;
    }
    return diagnostics;
}

} // namespace kachakacha::v2::document
