#pragma once

#include "kachakacha/document/Document.h"
#include "kachakacha/modeling/SnapEngine.h"
#include <vector>
#include <variant>

namespace v2ui {
// Display visibility must not remove an input from a saved feature's definition.
// Preserve source segment IDs, and never insert hidden inputs into the pick scene.
inline std::vector<kachakacha::v2::modeling::SnapCurve> SourceWireCurves(
    const kachakacha::v2::document::Document& document,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::base::EntityId& id) {
    using namespace kachakacha::v2;
    std::vector<modeling::SnapCurve> curves;
    for(const auto& curve:scene.curves)if(curve.entityId==id)curves.push_back(curve);
    if(!curves.empty())return curves;
    const auto* entity=document.FindEntity(id);
    const auto* feature=entity&&entity->kind==domain::EntityKind::Wire?document.FindFeature(entity->createdBy):nullptr;
    if(!feature||!feature->enabled)return curves;
    const auto* wire=std::get_if<domain::CreateWireDefinition>(&feature->definition);
    if(!wire)return curves;
    for(std::size_t i=0;i<wire->segments.size();++i)
        curves.push_back({id,i<wire->segmentIds.size()?wire->segmentIds[i]:base::SegmentId{},
            wire->segments[i],wire->construction,entity->datum});
    return curves;
}
}
