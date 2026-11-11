#include "V2HistoryShapePlan.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <utility>

namespace v2ui {
namespace {
using namespace kachakacha::v2;
using NativeMap=std::map<std::string,modeling::KernelShapeHandle>;
using Steps=std::map<base::EntityId,app::ShapeRebuildStep>;

bool SameStep(const app::ShapeRebuildStep& a,const app::ShapeRebuildStep& b)
{
    return a.featureId==b.featureId && a.outputEntityId==b.outputEntityId
        && a.kind==b.kind && a.outputOrdinal==b.outputOrdinal;
}

// Serialize the common definitions through the existing complete format. This
// deliberately compares more than native geometry needs: unknown new fields
// invalidate reuse instead of being accidentally omitted from a hand-made key.
std::string CommonDefinitions(const document::DocumentSnapshot& source,
    const std::set<base::FeatureId>& features,const std::set<base::EntityId>& entities)
{
    io::DocumentFile file;
    file.snapshot.id=source.id;
    file.snapshot.settings.tolerance=source.settings.tolerance;
    file.snapshot.referenceDimensions=source.referenceDimensions;
    for (auto feature:source.features) if (features.contains(feature.id)) {
        feature.revision=0;
        feature.displayName.clear();
        feature.derivedGroupId.reset();
        file.snapshot.features.push_back(std::move(feature));
    }
    for (auto entity:source.entities) if (entities.contains(entity.id)) {
        entity.revision=0;
        entity.displayName.clear();
        entity.groupId.reset();
        entity.visibility=domain::Visibility::Visible;
        file.snapshot.entities.push_back(std::move(entity));
    }
    for (auto id:source.evaluationOrder)
        if (features.contains(id)) file.snapshot.evaluationOrder.push_back(id);
    return io::WriteDocumentJson(file);
}

bool ValidMaps(const document::DocumentSnapshot& snapshot,
    const std::vector<app::ShapeRebuildStep>& steps,const NativeMap& parts,const NativeMap& guides)
{
    std::set<std::string> expectedParts,expectedGuides;
    for (const auto& step:steps) {
        const auto entity=std::find_if(snapshot.entities.begin(),snapshot.entities.end(),
            [&](const auto& e){return e.id==step.outputEntityId;});
        if (entity==snapshot.entities.end()) return false;
        if (entity->kind==domain::EntityKind::Part) expectedParts.insert(entity->id.ToString());
        else if (entity->kind==domain::EntityKind::GuideSurface) expectedGuides.insert(entity->id.ToString());
        else if (step.kind!=app::ShapeRebuildKind::FabricationModel) return false;
    }
    const auto matches=[](const NativeMap& map,const std::set<std::string>& expected) {
        if (map.size()!=expected.size()) return false;
        for (const auto& [id,handle]:map)
            if (!expected.contains(id)||!kernel::HasShape(handle)) return false;
        return true;
    };
    return matches(parts,expectedParts)&&matches(guides,expectedGuides);
}
}

std::optional<PartHistoryPlan> PlanPartHistory(const document::DocumentSnapshot& before,
    const document::DocumentSnapshot& after,const NativeMap& parts,const NativeMap& guides)
{
    if (before.id!=after.id) return {};
    std::set<base::FeatureId> oldFeatures,commonFeatures;
    std::set<base::EntityId> oldEntities,commonEntities;
    for (const auto& f:before.features) oldFeatures.insert(f.id);
    for (const auto& f:after.features) if (oldFeatures.contains(f.id)) commonFeatures.insert(f.id);
    for (const auto& e:before.entities) oldEntities.insert(e.id);
    for (const auto& e:after.entities) if (oldEntities.contains(e.id)) commonEntities.insert(e.id);
    if (CommonDefinitions(before,commonFeatures,commonEntities)
        !=CommonDefinitions(after,commonFeatures,commonEntities)) return {};

    const auto oldPlan=app::PlanShapeRebuild(before),newPlan=app::PlanShapeRebuild(after);
    if (!ValidMaps(before,oldPlan,parts,guides)) return {};
    Steps oldSteps,newSteps;
    for (const auto& step:oldPlan) if (!oldSteps.emplace(step.outputEntityId,step).second) return {};
    for (const auto& step:newPlan) if (!newSteps.emplace(step.outputEntityId,step).second) return {};
    PartHistoryPlan result;
    const auto independentOutput=[](const auto& snapshot,base::EntityId id) {
        return std::any_of(snapshot.entities.begin(),snapshot.entities.end(),[&](const auto& e){
            return e.id==id && (e.kind==domain::EntityKind::Part||e.kind==domain::EntityKind::GuideSurface);});
    };
    for (const auto& step:oldPlan) {
        const auto next=newSteps.find(step.outputEntityId);
        if (next!=newSteps.end()) {
            if (!SameStep(step,next->second)) return {};
        } else {
            if (!independentOutput(before,step.outputEntityId)) return {};
            result.removed.push_back(step.outputEntityId.ToString());
        }
    }
    for (const auto& step:newPlan) if (!oldSteps.contains(step.outputEntityId)) {
        if (!independentOutput(after,step.outputEntityId)) return {};
        result.added.push_back(step);
    }
    return result;
}

void RemoveHistoryParts(const PartHistoryPlan& plan,NativeMap& parts,NativeMap& guides)
{
    std::set<std::uint64_t> removed,retained;
    for (const auto& id:plan.removed) for(auto* map:{&parts,&guides}) {
        const auto found=map->find(id);
        if (found!=map->end()) {removed.insert(found->second.value);map->erase(found);}
    }
    // Shared native handles can represent unchanged committed outputs. Release
    // each orphan once, never a handle still owned by either document map.
    for (const auto* map:std::array<const NativeMap*,2>{&parts,&guides})
        for (const auto& [id,handle]:*map) retained.insert(handle.value);
    for (auto handle:removed) if (!retained.contains(handle)) kernel::ReleaseShape({handle});
}
}
