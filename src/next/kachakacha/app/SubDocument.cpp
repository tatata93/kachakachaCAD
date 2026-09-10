#include "kachakacha/app/SubDocument.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <variant>

namespace kachakacha::v2::app {

using base::EntityId;
using base::MakeError;
using base::Result;
using document::DocumentSnapshot;
using domain::Entity;
using domain::Feature;

namespace {

struct Collector {
    std::vector<EntityId> ids;

    void Add(const EntityId& id)
    {
        if (!id.IsNil()) {
            ids.push_back(id);
        }
    }
    void Add(const std::optional<EntityId>& id)
    {
        if (id.has_value()) {
            Add(*id);
        }
    }
    void Add(const std::vector<EntityId>& many)
    {
        for (const auto& id : many) {
            Add(id);
        }
    }
    void Add(const std::vector<domain::SegmentRef>& refs)
    {
        for (const auto& ref : refs) {
            Add(ref.entityId);
        }
    }
};

} // namespace

std::vector<EntityId> DefinitionEntityReferences(const domain::FeatureDefinition& definition)
{
    Collector out;
    std::visit(
        [&out](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, domain::CreatePointDefinition>) {
                out.Add(value.sourcePlaneId);
            } else if constexpr (std::is_same_v<T, domain::CreateWireDefinition>) {
                out.Add(value.sourcePlaneId);
            } else if constexpr (std::is_same_v<T, domain::TransformWireDefinition>) {
                out.Add(value.inputs);
            } else if constexpr (std::is_same_v<T, domain::FreezeDerivedDefinition>) {
                out.Add(value.sources);
            } else if constexpr (std::is_same_v<T, domain::CreateWorkPlaneDefinition>) {
                out.Add(value.inputs);
            } else if constexpr (std::is_same_v<T, domain::ProjectWireDefinition>) {
                out.Add(value.inputs);
                out.Add(value.targetPlaneId);
            } else if constexpr (std::is_same_v<T, domain::CreateGuideSurfaceDefinition>) {
                for (const auto& chain : value.chains) {
                    out.Add(chain.segments);
                }
            } else if constexpr (std::is_same_v<T, domain::ExtrudeDefinition>) {
                out.Add(value.profiles);
                out.Add(value.targets);
            } else if constexpr (std::is_same_v<T, domain::CreatePartFromWireCageDefinition>) {
                out.Add(value.wires);
            } else if constexpr (std::is_same_v<T, domain::BooleanDefinition>) {
                out.Add(value.targets);
                out.Add(value.tools);
            } else if constexpr (std::is_same_v<T, domain::ThickenSurfaceDefinition>) {
                out.Add(value.surface);
            } else if constexpr (std::is_same_v<T, domain::CreateFabricationModelDefinition>) {
                out.Add(value.parts);
                out.Add(value.openingWires);
                out.Add(value.foldWires);
                out.Add(value.connectionWires);
            } else if constexpr (std::is_same_v<T, domain::CreatePatternDefinition>) {
                out.Add(value.fabricationModels);
            }
        },
        definition);
    return out.ids;
}

Result<SubDocument> ExtractSubDocument(const DocumentSnapshot& source,
    const std::vector<EntityId>& selected)
{
    using Out = Result<SubDocument>;
    if (selected.empty()) {
        return Out::Failure(MakeError("EXP-S001", "選んだものがありません。",
            "別の文書にしたいものを、先に画面か一覧で選んでください。"));
    }
    std::map<std::string, const Entity*> entities;
    for (const Entity& entity : source.entities) {
        entities[entity.id.ToString()] = &entity;
    }
    std::map<std::string, const Feature*> features;
    for (const Feature& feature : source.features) {
        features[feature.id.ToString()] = &feature;
    }
    // 選んだものを作った Feature から始める。
    std::set<std::string> selectedIds;
    std::vector<const Feature*> pending;
    std::set<std::string> keptFeatures;
    int found = 0;
    for (const EntityId& id : selected) {
        const auto entity = entities.find(id.ToString());
        if (entity == entities.end()) {
            continue;
        }
        ++found;
        selectedIds.insert(id.ToString());
        const auto feature = features.find(entity->second->createdBy.ToString());
        if (feature != features.end() && keptFeatures.insert(feature->first).second) {
            pending.push_back(feature->second);
        }
    }
    if (found == 0) {
        return Out::Failure(MakeError("EXP-S002", "選んだものが文書にありません。",
            "選び直してから、もう一度どうぞ。"));
    }
    // 上流へたどる。inputEntityIds と作り方の中身の両方を見る。
    SubDocument made;
    made.selectedCount = found;
    std::set<std::string> keptEntities;
    while (!pending.empty()) {
        const Feature* feature = pending.back();
        pending.pop_back();
        std::vector<EntityId> inputs = feature->inputEntityIds;
        const auto referenced = DefinitionEntityReferences(feature->definition);
        inputs.insert(inputs.end(), referenced.begin(), referenced.end());
        for (const EntityId& input : inputs) {
            const auto entity = entities.find(input.ToString());
            if (entity == entities.end()) {
                continue;   // 壊れた参照は、元の文書のまま(ここで直さない)。
            }
            if (keptEntities.insert(input.ToString()).second
                && selectedIds.find(input.ToString()) == selectedIds.end()) {
                made.keptDependencyNames.push_back(entity->second->displayName);
            }
            const auto producer = features.find(entity->second->createdBy.ToString());
            if (producer != features.end() && keptFeatures.insert(producer->first).second) {
                pending.push_back(producer->second);
            }
        }
    }
    // 残った Feature の出力だけが Entity になる。並びは元の文書のまま(決定的)。
    made.snapshot.id = source.id;
    made.snapshot.settings = source.settings;
    std::set<std::string> keptGroups;
    for (const Feature& feature : source.features) {
        if (keptFeatures.find(feature.id.ToString()) == keptFeatures.end()) {
            continue;
        }
        made.snapshot.features.push_back(feature);
        if (feature.derivedGroupId.has_value()) {
            keptGroups.insert(feature.derivedGroupId->ToString());
        }
        for (const auto& output : feature.outputs) {
            keptEntities.insert(output.entityId.ToString());
        }
    }
    for (const Entity& entity : source.entities) {
        if (keptEntities.find(entity.id.ToString()) == keptEntities.end()) {
            continue;
        }
        made.snapshot.entities.push_back(entity);
        if (entity.groupId.has_value()) {
            keptGroups.insert(entity.groupId->ToString());
        }
    }
    // まとまりは、使っているものと、その親を残す。親が無いと入れ子が壊れる。
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& group : source.groups) {
            if (keptGroups.find(group.id.ToString()) != keptGroups.end()
                && group.parentId.has_value()
                && keptGroups.insert(group.parentId->ToString()).second) {
                grew = true;
            }
        }
    }
    for (const auto& group : source.groups) {
        if (keptGroups.find(group.id.ToString()) != keptGroups.end()) {
            made.snapshot.groups.push_back(group);
        }
    }
    for (const auto& dimension : source.referenceDimensions) {
        const bool allKept = std::all_of(dimension.targets.begin(), dimension.targets.end(),
            [&keptEntities](const EntityId& id) {
                return keptEntities.find(id.ToString()) != keptEntities.end();
            });
        if (allKept) {
            made.snapshot.referenceDimensions.push_back(dimension);
        }
    }
    for (const auto& id : source.evaluationOrder) {
        if (keptFeatures.find(id.ToString()) != keptFeatures.end()) {
            made.snapshot.evaluationOrder.push_back(id);
        }
    }
    const auto problems = document::Document::Validate(made.snapshot);
    for (const auto& problem : problems) {
        if (problem.IsError()) {
            return Out::Failure(problems);
        }
    }
    return Out::Success(std::move(made));
}

std::string KeptDependenciesSummaryJa(const SubDocument& made)
{
    if (made.keptDependencyNames.empty()) {
        return {};
    }
    std::string text = "選んでいないが参照されているので残したもの: ";
    for (std::size_t index = 0; index < made.keptDependencyNames.size(); ++index) {
        if (index != 0) {
            text += ", ";
        }
        text += made.keptDependencyNames[index];
    }
    return text;
}

std::optional<base::Diagnostic> KeptDependenciesNote(const SubDocument& made)
{
    if (made.keptDependencyNames.empty()) {
        return std::nullopt;
    }
    return base::MakeInformation("EXP-S003",
        "選んでいないが参照されているので残したものがあります。",
        KeptDependenciesSummaryJa(made));
}

} // namespace kachakacha::v2::app
