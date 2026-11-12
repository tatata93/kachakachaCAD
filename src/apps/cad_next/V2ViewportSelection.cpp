#include "V2Viewport.h"
#include "V2DocumentTiming.h"
#include "V2ToolSelection.h"
#include "V2DrawingSelection.h"
#include "V2RenderSelection.h"
#include "V2TransformPreview.h"
#include "V2ReferencePlanePicking.h"
#include "kachakacha/domain/Feature.h"
#include <QPointF>
#include <QVariant>
#include <algorithm>
#include <utility>
#include <cmath>

void v2ui::AppendReferencePlaneCandidates(const std::vector<V2Viewport::WorkPlaneView>& planes,
    const kachakacha::v2::geometry::ScreenMapping& mapping,double width,double tolerance,
    const QPointF& pointer,std::vector<kachakacha::v2::app::PickCandidate>& candidates)
{
    using namespace kachakacha::v2;
    const double half=std::clamp(width*.3,5.0,5000.0);
    for(const auto& plane:planes) {
        if(plane.entityId.IsNil())continue;
        const geometry::Vector3 corners[]={plane.frame.PointAt(-half,-half),plane.frame.PointAt(half,-half),
            plane.frame.PointAt(half,half),plane.frame.PointAt(-half,half)};
        double best=tolerance+1;auto hit=plane.frame.origin;
        if(const auto p=mapping.Project(hit))best=std::hypot(p->x-pointer.x(),p->y-pointer.y());
        for(int i=0;i<4;++i) {
            const auto a=mapping.Project(corners[i]),b=mapping.Project(corners[(i+1)%4]);
            if(!a||!b)continue;
            const double dx=b->x-a->x,dy=b->y-a->y,length2=dx*dx+dy*dy;
            if(length2<1e-12)continue;
            const double t=std::clamp(((pointer.x()-a->x)*dx+(pointer.y()-a->y)*dy)/length2,0.0,1.0);
            const double distance=std::hypot(a->x+t*dx-pointer.x(),a->y+t*dy-pointer.y());
            if(distance<best){best=distance;hit=corners[i]+(corners[(i+1)%4]-corners[i])*t;}
        }
        if(best>tolerance)continue;
        app::PickCandidate pick;pick.entityId=plane.entityId;pick.kind=app::SelectionElementKind::WorkPlane;
        pick.hitPoint=hit;pick.distancePx=best;candidates.push_back(pick);
    }
}

void V2Viewport::SetSelection(kachakacha::v2::app::SelectionSet selection)
{
    V2DocumentTiming timing("viewport-selection");timing.Mark("begin");
    const bool treeInput=property("treePickInput").toBool();
    const bool treeToolInput=property("treeToolInput").toBool();
    const auto treeModifiers=Qt::KeyboardModifiers(property("treeInputModifiers").toInt());
    setProperty("treeInputModifiers",{});
    setProperty("treePickInput",false); // Consume before callbacks can re-enter selection.
    setProperty("treeToolInput",false);
    if(treeInput && !V2ToolSelection::Active(*this) && pickHandler_) {
        if(selection.entityIds.size()==1) {
            const auto id=selection.entityIds.front();
            const auto* entity=session_->GetDocument().FindEntity(id);
            const auto* feature=entity?session_->GetDocument().FindFeature(entity->createdBy):nullptr;
            const auto* point=feature&&feature->enabled
                ?std::get_if<kachakacha::v2::domain::CreatePointDefinition>(&feature->definition):nullptr;
            if(point && entity->kind==kachakacha::v2::domain::EntityKind::Point) {
                PickedPoint picked;picked.point=point->positionMm;
                auto handler=std::move(pickHandler_);pickHandler_=nullptr;setProperty("pointPickPrompt",{});
                status_="点を指定しました。";if(statusCallback_)statusCallback_(status_);
                handler(picked);RefreshCursorShape();update();return;
            }
        }
        status_="点の項目を選んでください。線の端点・交点・面上の位置は3Dビューで指定できます。";
        if(statusCallback_)statusCallback_(status_);update();return;
    }
    if(treeInput && !V2ToolSelection::Active(*this) && referencePickHandler_) {
        if(selection.entityIds.size()==1 && std::find(referencePickIds_.begin(),referencePickIds_.end(),
            selection.entityIds.front())!=referencePickIds_.end()) {
            const auto id=selection.entityIds.front();auto handler=std::move(referencePickHandler_);referencePickHandler_=nullptr;
            referencePickIds_.clear();ForgetPickCycle();handler(id);update();return;
        }
        status_="この入力で使える対象を選んでください。入力中の設定は保持しています。";
        if(statusCallback_)statusCallback_(status_);update();return;
    }
    if (elementPickHandler_ && !V2ToolSelection::Active(*this)) {
        for (const auto& ref : selection.ordered) {
            kachakacha::v2::app::PickCandidate pick;
            pick.entityId = ref.entityId; pick.kind = ref.kind;
            pick.pickedFaceIndex = ref.pickedFaceIndex; pick.hitPoint = ref.hitPoint;
            pick.segmentId = ref.segmentId.value_or(kachakacha::v2::base::SegmentId{}); pick.subshapeKey = ref.subshapeKey;
            if (AcceptElementPick(pick)) return;
        }
        for (const auto& id : selection.entityIds) {
            kachakacha::v2::app::PickCandidate pick; pick.entityId = id;
            if (AcceptElementPick(pick)) return;
        }
        if(treeInput) {
            status_="この項目全体は現在の入力には使えません。必要な面・辺などは3Dビューで選んでください。";
            if(statusCallback_)statusCallback_(status_);update();
        }
        return;
    }

    if(treeToolInput&&!V2ToolSelection::Active(*this)&&toolPickActive_&&selection.entityIds.size()==1) {
        kachakacha::v2::app::PickCandidate pick;pick.entityId=selection.entityIds.front();
        const auto requested=SelectionModeFor(treeModifiers);
        const auto mode=ModeForTogglePick(pick,ModeForToolPick(pick,requested));
        selection=kachakacha::v2::app::ApplySelection(selection_,pick,mode);
    }
    selection_ = std::move(selection);
    if(selection_.entityIds.empty())V2DrawingSelection::Of(*this).ResumeInput();
    V2RenderSelectionOf(*this).Update(selection_);
    timing.Mark("render-selection");
    v2ui::AppendTransformGhost(*this,*session_,selection_,hover_,cursorPanel_,cursorDelta_);
    timing.Mark("transform-ghost");
    ApplyVisibilityFilter();
    timing.Mark("visibility");
    if (selectionChangedCallback_) {
        selectionChangedCallback_();
    }
    timing.Mark("callback");
    update();
}

