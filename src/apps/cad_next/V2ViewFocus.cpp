#include "V2Viewport.h"
#include <QPushButton>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <algorithm>
using namespace kachakacha::v2;
bool V2Viewport::IsolationActive() const {
    return isolationActive_&&isolationDocument_==session_->GetDocument().Snapshot().id;
}
bool V2Viewport::EntityShown(base::EntityId id) const {
    if(id.IsNil())return true; // Tool previews are not document entities yet.
    if(IsolationActive()&&std::find(isolationExcluded_.begin(),isolationExcluded_.end(),id)!=isolationExcluded_.end())return false;
    return !display_.selectionOnly||app::IsSelected(selection_,id);
}
void V2Viewport::IsolateSelection() {
    if(selection_.entityIds.empty())return;
    if(!isolationActive_)isolationPreviousSelectionOnly_=display_.selectionOnly;
    isolationExcluded_.clear();isolationDocument_=session_->GetDocument().Snapshot().id;isolationActive_=true;
    for(const auto& e:session_->GetDocument().Snapshot().entities)
        if(!app::IsSelected(selection_,e.id))isolationExcluded_.push_back(e.id);
    display_.selectionOnly=false;
    auto* button=findChild<QPushButton*>(QStringLiteral("exitIsolation"));
    if(!button){button=new QPushButton(QStringLiteral("局所表示中 — 解除"),this);button->setObjectName(QStringLiteral("exitIsolation"));
        connect(button,&QPushButton::clicked,this,[this]{ClearIsolation();});}
    button->move(8,80);button->resize(180,30);button->show();button->raise();
    ApplyVisibilityFilter();cycle_={};hoveredEntityId_={};hoveredSegmentId_={};update();
}
void V2Viewport::ClearIsolation() {
    if(isolationActive_)display_.selectionOnly=isolationPreviousSelectionOnly_;
    isolationActive_=false;isolationExcluded_.clear();
    if(auto* button=findChild<QPushButton*>(QStringLiteral("exitIsolation")))button->hide();
    ApplyVisibilityFilter();cycle_={};update();
}

void V2Viewport::ApplyVisibilityFilter() {
    if(!IsolationActive()&&!display_.selectionOnly){session_->SetSnapEntityFilter({});return;}
    QPointer<V2Viewport> alive(this);
    session_->SetSnapEntityFilter([alive](base::EntityId id){return !alive||alive->EntityShown(id);});
    DiscardHoverState();
}
