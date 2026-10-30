// Preserve an existing body skin when a selected outline cuts a region from it.
// A concave perimeter is not four semantic sides of a Coons patch: refilling it
// can reproduce every boundary point while pulling the middle into a dent.
#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include <algorithm>
#include <vector>

using namespace kachakacha::v2;

bool V2LoopFacesTool::PreservesSource(std::size_t face) const
{
    return face<networkTables_.size()&&std::any_of(networkTables_[face].rows.begin(),networkTables_[face].rows.end(),
        [](const auto& row){return row.role==modeling::ChainRole::SourceSurface;});
}

void V2LoopFacesTool::AttachSourceSupports(modeling::GuideTable& table,bool opening)
{
    if(table.method!=modeling::GuideSurfaceMethod::BoundaryFill)return;
    const auto tolerance=ToleranceNow();
    std::vector<std::vector<geometry::CurveSegment>> loops(1);
    std::vector<base::EntityId> boundaryIds;
    for(const auto& row:table.rows)if(row.role==modeling::ChainRole::BoundarySide) {
        loops.front().insert(loops.front().end(),row.segments.begin(),row.segments.end());
        for(const auto id:row.sourceWireIds)
            if(std::find(boundaryIds.begin(),boundaryIds.end(),id)==boundaryIds.end())boundaryIds.push_back(id);
    }
    for(const auto& row:table.rows)if(row.role==modeling::ChainRole::HoleBoundary)loops.push_back(row.segments);
    const auto& document=window_.session_->GetDocument();
    const auto snapshot=document.Snapshot();
    struct Candidate {base::EntityId id;modeling::KernelShapeHandle handle;bool visible;std::size_t shared;};
    std::vector<Candidate> candidates;
    for(const auto* shapes:{&window_.guideShapes_,&window_.partShapes_})for(const auto& [key,handle]:*shapes) {
        const auto id=base::EntityId::Parse(key);if(!id)continue;
        const auto* entity=document.FindEntity(*id);
        if(!entity||(entity->kind!=domain::EntityKind::Part&&entity->kind!=domain::EntityKind::GuideSurface))continue;
        const auto* feature=document.FindFeature(entity->createdBy);
        std::size_t shared=0;
        if(feature)for(const auto wire:boundaryIds)
            shared+=std::find(feature->inputEntityIds.begin(),feature->inputEntityIds.end(),wire)!=feature->inputEntityIds.end();
        candidates.push_back({*id,handle,app::EntityEffectivelyVisible(snapshot,*entity),shared});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[opening](const auto& a,const auto& b){
        // For a plain perimeter, prefer its own original skin to unrelated
        // visible geometry. Existing projected-hole visibility policy stays intact.
        if(!opening&&a.shared!=b.shared)return a.shared>b.shared;
        if(a.visible!=b.visible)return a.visible>b.visible;
        return a.id.ToString()<b.id.ToString();
    });
    const auto append=[&](base::EntityId id){
        modeling::GuideTableRow source;source.role=modeling::ChainRole::SourceSurface;
        source.sourceWireIds={id};source.sourceLabels={document.FindEntity(id)->displayName};table.rows.push_back(std::move(source));
    };
    for(const auto& candidate:candidates) {
        // Do not flatten an ordinary curved perimeter onto a merely nearby,
        // unrelated plane. The existing projection allowance is appropriate
        // only for an opening or a skin referencing the selected source wires.
        const double allowance=opening||candidate.shared?tolerance.interactiveJoinMm:0;
        const auto cut=kernel::TrimSurfaceRegion({candidate.handle},loops,tolerance,allowance);
        if(!cut.HasValue())continue;
        kernel::ReleaseShape(cut.Value());append(candidate.id);return;
    }
    // An opening can span adjacent faces; retain the established minimal set
    // of native supports rather than introducing unrelated dependencies.
    if(!opening)return;
    for(const bool visible:{true,false}) {
        std::vector<modeling::KernelShapeHandle> handles;
        std::vector<base::EntityId> ids;
        for(const auto& candidate:candidates)if(!visible||candidate.visible) {
            handles.push_back(candidate.handle);ids.push_back(candidate.id);
        }
        if(handles.size()<2)continue;
        const auto cut=kernel::TrimSurfaceRegion(handles,loops,tolerance,tolerance.interactiveJoinMm);
        if(!cut.HasValue())continue;
        kernel::ReleaseShape(cut.Value());
        for(std::size_t at=0;at<handles.size()&&handles.size()>1;) {
            auto trial=handles;trial.erase(trial.begin()+at);
            const auto reduced=kernel::TrimSurfaceRegion(trial,loops,tolerance,tolerance.interactiveJoinMm);
            if(reduced.HasValue()) {kernel::ReleaseShape(reduced.Value());handles=std::move(trial);ids.erase(ids.begin()+at);}
            else ++at;
        }
        for(const auto id:ids)append(id);
        return;
    }
}
