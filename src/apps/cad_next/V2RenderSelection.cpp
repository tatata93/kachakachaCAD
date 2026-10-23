#include "V2RenderSelection.h"
#include <QObject>
#include <QString>

using namespace kachakacha::v2;
V2RenderSelection::V2RenderSelection(QObject* parent):QObject(parent) {
    setObjectName(QStringLiteral("renderSelectionIndex"));
}
void V2RenderSelection::Update(const app::SelectionSet& selection) {
    entities_.clear();wholeCurves_.clear();segments_.clear();++builds_;
    profileSeeds_=app::SelectedProfileSeeds(selection);
    for(const auto& ref:selection.ordered) {
        entities_.insert(ref.entityId);
        if(ref.kind==app::SelectionElementKind::Object
            ||(ref.kind==app::SelectionElementKind::Edge&&!ref.segmentId))wholeCurves_.insert(ref.entityId);
        else if(ref.kind==app::SelectionElementKind::Edge)segments_[ref.entityId].insert(*ref.segmentId);
    }
    // A Face (or any other ordered kind) blocks the compatibility Object fallback.
    for(const auto id:selection.entityIds)if(entities_.insert(id).second)wholeCurves_.insert(id);
}
bool V2RenderSelection::Entity(base::EntityId id) const {return entities_.contains(id);}
bool V2RenderSelection::Curve(base::EntityId id,base::SegmentId segment) const {
    if(wholeCurves_.contains(id))return true;
    const auto found=segments_.find(id);return found!=segments_.end()&&found->second.contains(segment);
}
V2RenderSelection& V2RenderSelectionOf(const QObject& owner) {
    auto* found=owner.findChild<QObject*>(QStringLiteral("renderSelectionIndex"),Qt::FindDirectChildrenOnly);
    return found?*static_cast<V2RenderSelection*>(found):*new V2RenderSelection(const_cast<QObject*>(&owner));
}
