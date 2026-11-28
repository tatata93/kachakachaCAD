#include "V2Viewport.h"
#include "V2DocumentTiming.h"
#include "V2PointPicking.h"
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

namespace {
kachakacha::v2::app::PickCandidate PickOf(const kachakacha::v2::app::SelectionRef& ref)
{
    using namespace kachakacha::v2;
    app::PickCandidate pick;
    pick.entityId = ref.entityId;
    pick.kind = ref.kind;
    pick.segmentId = ref.segmentId.value_or(base::SegmentId{});
    pick.subshapeKey = ref.subshapeKey;
    pick.curveParameter = ref.curveParameter;
    pick.hitPoint = ref.hitPoint;
    pick.distancePx = ref.screenDistancePx;
    pick.pickedFaceIndex = ref.pickedFaceIndex;
    pick.profileSeed = ref.profileSeed;
    return pick;
}

std::optional<V2Viewport::PickedPoint> ResolveTreePoint(const V2Viewport& viewport,
    const kachakacha::v2::app::DrawingSession& session,
    const kachakacha::v2::app::SelectionSet& selection)
{
    using namespace kachakacha::v2;
    if (selection.entityIds.size() != 1) return std::nullopt;
    const auto id = selection.entityIds.front();
    if (selection.ordered.size() == 1 && selection.ordered.front().entityId == id
        && selection.ordered.front().kind == app::SelectionElementKind::Vertex
        && viewport.EntityShown(id)) {
        auto pick = PickOf(selection.ordered.front());
        const auto& curves = session.Scene().curves;
        const auto curve = std::find_if(curves.begin(), curves.end(), [&](const auto& item) {
            return item.entityId == id && item.segmentId == pick.segmentId;
        });
        if (curve != curves.end() && pick.curveParameter
            && *pick.curveParameter >= 0 && *pick.curveParameter <= 1) {
            pick.hitPoint = curve->segment.Evaluate(*pick.curveParameter);
            pick.kind = app::SelectionElementKind::Edge;
            return V2Viewport::PickedPoint{pick.hitPoint, pick};
        }
    }
    const auto& document = session.GetDocument();
    const auto* entity = document.FindEntity(id);
    const auto* feature = entity ? document.FindFeature(entity->createdBy) : nullptr;
    const auto* point = feature && feature->enabled
        ? std::get_if<domain::CreatePointDefinition>(&feature->definition) : nullptr;
    if (point && entity->kind == domain::EntityKind::Point)
        return V2Viewport::PickedPoint{point->positionMm, std::nullopt};
    return std::nullopt;
}
}

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

bool V2Viewport::AppendOrdinaryPointCandidate(const QPointF& position)
{
    using namespace kachakacha::v2;
    if (session_->CurrentTool() != modeling::DrawingTool::Select
        || toolPickActive_ || ReferencePickPending() || PointInputActive() || V2ToolSelection::Active(*this)) return false;
    if (!std::any_of(cycle_.candidates.begin(), cycle_.candidates.end(), [this](const auto& item) {
            const auto* entity = session_->GetDocument().FindEntity(item.entityId);
            return entity && entity->kind == domain::EntityKind::Wire;
        })) return false;
    std::optional<app::PickCandidate> onCurve;
    (void)v2ui::ReferencePointAt(*this, session_->Scene(),
        session_->GetDocument().Snapshot().settings.tolerance, position, false, &onCurve, false);
    if (!onCurve || !onCurve->curveParameter) return false;
    const auto* entity = session_->GetDocument().FindEntity(onCurve->entityId);
    if (!entity || entity->kind != domain::EntityKind::Wire) return false;
    onCurve->kind = app::SelectionElementKind::Vertex;
    const auto duplicate = std::any_of(cycle_.candidates.begin(), cycle_.candidates.end(),
        [&](const auto& item) {
            return item.kind == onCurve->kind && item.entityId == onCurve->entityId
                && item.segmentId == onCurve->segmentId && item.curveParameter == onCurve->curveParameter;
        });
    if (duplicate) return false;
    cycle_.candidates.push_back(*onCurve);
    return true;
}

void V2Viewport::SetSelection(kachakacha::v2::app::SelectionSet selection)
{
    using namespace kachakacha::v2;
    V2DocumentTiming timing("viewport-selection");timing.Mark("begin");
    const bool treeInput=property("treePickInput").toBool();
    const bool treeToolInput=property("treeToolInput").toBool();
    const auto treeModifiers=Qt::KeyboardModifiers(property("treeInputModifiers").toInt());
    setProperty("treeInputModifiers",{});
    setProperty("treePickInput",false); // Consume before callbacks can re-enter selection.
    setProperty("treeToolInput",false);
    if(treeInput && !V2ToolSelection::Active(*this) && (pickHandler_ || directionPick_)) {
        const auto picked = ResolveTreePoint(*this, *session_, selection);
        if (directionPick_) {
            AcceptListedDirectionPoint(picked ? std::optional{picked->point} : std::nullopt);
            return;
        }
        if (picked) {
            auto handler=std::move(pickHandler_);pickHandler_=nullptr;setProperty("pointPickPrompt",{});
            ForgetPickCycle();
            status_="点を指定しました。";if(statusCallback_)statusCallback_(status_);
            handler(*picked);RefreshCursorShape();update();return;
        }
        status_="作図点を選ぶか、左一覧の「部分を指定」から線上の点を指定してください。";
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
            if (AcceptElementPick(PickOf(ref))) return;
        }
        for (const auto& id : selection.entityIds) {
            if (treeInput && std::any_of(selection.ordered.begin(),selection.ordered.end(),[id](const auto& ref) {
                    return ref.entityId==id && ref.kind!=app::SelectionElementKind::Object;
                })) continue;
            kachakacha::v2::app::PickCandidate pick; pick.entityId = id;
            if (AcceptElementPick(pick)) return;
        }
        if(treeInput) {
            status_="一覧の右クリックで面・辺を指定するか、3Dビューで位置を指定してください。";
            if(statusCallback_)statusCallback_(status_);update();
        }
        return;
    }

    if(treeToolInput&&!V2ToolSelection::Active(*this)&&toolPickActive_) {
        const auto requested=SelectionModeFor(treeModifiers);
        if(selection.entityIds.size()==1) {
            app::PickCandidate pick;
            pick.entityId=selection.entityIds.front();
            if (selection.ordered.size()==1 && selection.ordered.front().entityId==pick.entityId)
                pick=PickOf(selection.ordered.front());
            if (pick.kind!=app::SelectionElementKind::Object && toolCandidateFilter_
                && !toolCandidateFilter_(pick)) {
                status_="この部分は現在の入力に使用できません。";
                if (statusCallback_) statusCallback_(status_);
                update();
                return;
            }
            const auto mode=ModeForTogglePick(pick,ModeForToolPick(pick,requested));
            selection=kachakacha::v2::app::ApplySelection(selection_,pick,mode);
        } else if(requested==kachakacha::v2::app::SelectionMode::Toggle) {
            auto combined=selection_;
            for(const auto& id:selection.entityIds) {
                kachakacha::v2::app::PickCandidate pick;pick.entityId=id;
                combined=kachakacha::v2::app::ApplySelection(combined,pick,requested);
            }
            selection=std::move(combined);
        }
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

