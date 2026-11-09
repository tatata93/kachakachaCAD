#include "V2GeneratedWirePicking.h"
#include "V2GeneratedCurves.h"

void V2PickWholeSmoothCurves(const kachakacha::v2::document::Document& document,
    std::vector<kachakacha::v2::app::PickCandidate>& candidates) {
    using namespace kachakacha::v2;
    for(auto& candidate:candidates) {
        if(candidate.kind!=app::SelectionElementKind::Edge||candidate.segmentId.IsNil())continue;
        const auto* entity=document.FindEntity(candidate.entityId);
        if(!entity||entity->kind!=domain::EntityKind::Wire)continue;
        const auto* feature=document.FindFeature(entity->createdBy);
        const auto* wire=feature?std::get_if<domain::CreateWireDefinition>(&feature->definition):nullptr;
        if(!wire||wire->segments.size()<2||!V2IsSmoothCurvedWire(wire->segments))continue;
        candidate.kind=app::SelectionElementKind::Object;
        candidate.segmentId={};candidate.subshapeKey.reset();candidate.curveParameter.reset();
    }
}
