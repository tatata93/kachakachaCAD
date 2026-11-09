#include "V2GeneratedWirePicking.h"
#include <QObject>
#include <algorithm>
#include <map>
#include <set>

namespace {
using namespace kachakacha::v2;
struct Run {std::vector<base::SegmentId> ids;bool curved=false;};
struct Wire {std::vector<base::SegmentId> all;std::vector<Run> runs;std::map<base::SegmentId,std::size_t> at;};
bool NeedsIndex(app::DrawingSession& session,base::EntityId id) {
    const auto* e=session.GetDocument().FindEntity(id);if(!e||e->kind!=domain::EntityKind::Wire)return false;
    const auto* f=session.GetDocument().FindFeature(e->createdBy);
    const auto* wire=f?std::get_if<domain::CreateWireDefinition>(&f->definition):nullptr;
    return !wire||wire->segments.size()>1;
}
bool Smooth(const geometry::CurveSegment& a,const geometry::CurveSegment& b,double tolerance) {
    const auto u=a.FirstDerivative(1),v=b.FirstDerivative(0);
    return a.Kind()!=geometry::CurveKind::Line&&b.Kind()!=geometry::CurveKind::Line
        &&geometry::Distance(a.EndPoint(),b.StartPoint())<=std::min(1e-7,tolerance)
        &&u.LengthSquared()>1e-24&&v.LengthSquared()>1e-24
        &&geometry::Dot(geometry::Normalized(u),geometry::Normalized(v))>1-1e-6;
}
Wire Index(const std::vector<const modeling::SnapCurve*>& curves,double tolerance) {
    Wire wire;
    for(std::size_t i=0;i<curves.size();++i) {
        const auto& c=*curves[i];wire.all.push_back(c.segmentId);
        if(i==0||!Smooth(curves[i-1]->segment,c.segment,tolerance))
            wire.runs.push_back({{},c.segment.Kind()!=geometry::CurveKind::Line});
        wire.runs.back().ids.push_back(c.segmentId);
    }
    if(wire.runs.size()>1&&Smooth(curves.back()->segment,curves.front()->segment,tolerance)) {
        auto last=std::move(wire.runs.back().ids);wire.runs.pop_back();
        last.insert(last.end(),wire.runs.front().ids.begin(),wire.runs.front().ids.end());
        wire.runs.front().ids=std::move(last);
    }
    for(std::size_t i=0;i<wire.runs.size();++i)for(const auto id:wire.runs[i].ids)wire.at[id]=i;
    return wire;
}
// Cache IDs, never scene pointers. Coordinate work occurs after a scene change,
// not once per rendered curve or once per mouse-move candidate.
class Cache final : public QObject {
public:
    Cache(QObject& owner,app::DrawingSession& session):QObject(&owner),session_(session) {
        setObjectName("smoothWirePickIndex");connection_=session.OnSceneChanged([this]{wires_.clear();locations_.clear();indexed_=false;});
    }
    const Wire& Get(base::EntityId id) {
        if(!indexed_) {
            const auto& curves=session_.Scene().curves;
            for(std::size_t i=0;i<curves.size();++i)locations_[curves[i].entityId].push_back(i);
            indexed_=true;
        }
        if(!wires_.contains(id))if(const auto at=locations_.find(id);at!=locations_.end()) {
            std::vector<const modeling::SnapCurve*> curves;
            for(const auto i:at->second)curves.push_back(&session_.Scene().curves[i]);
            wires_[id]=Index(curves,session_.GetDocument().Snapshot().settings.tolerance.modelLinearMm);
        }
        const auto found=wires_.find(id);static const Wire empty;
        return found==wires_.end()?empty:found->second;
    }
private:
    app::DrawingSession& session_;app::DrawingSession::SceneChangedConnection connection_;
    std::map<base::EntityId,Wire> wires_;std::map<base::EntityId,std::vector<std::size_t>> locations_;bool indexed_=false;
};
Cache& Of(const QObject& owner,app::DrawingSession& session) {
    auto* existing=owner.findChild<QObject*>("smoothWirePickIndex",Qt::FindDirectChildrenOnly);
    return existing?*static_cast<Cache*>(existing):*new Cache(const_cast<QObject&>(owner),session);
}
app::SelectionSet ExpandWhole(const app::SelectionSet& current,base::EntityId entity,
    const std::vector<base::SegmentId>& all) {
    auto refs=current.ordered;
    for(const auto id:current.entityIds)if(std::none_of(refs.begin(),refs.end(),[&](const auto& r){return r.entityId==id;})) {
        app::SelectionRef r;r.entityId=id;refs.push_back(r);
    }
    app::SelectionSet result;std::set<base::EntityId> seen;
    for(const auto& ref:refs) {
        if(seen.insert(ref.entityId).second)result.entityIds.push_back(ref.entityId);
        if(ref.entityId==entity&&ref.kind==app::SelectionElementKind::Object&&!ref.profileSeed) {
            for(const auto id:all){auto edge=ref;edge.kind=app::SelectionElementKind::Edge;edge.segmentId=id;result.ordered.push_back(edge);}
        } else result.ordered.push_back(ref);
    }
    return result;
}
}

void V2PickWholeSmoothCurves(const QObject& owner,app::DrawingSession& session,
    std::vector<app::PickCandidate>& candidates) {
    for(auto& candidate:candidates) {
        if(candidate.kind!=app::SelectionElementKind::Edge||candidate.segmentId.IsNil())continue;
        if(!NeedsIndex(session,candidate.entityId))continue;
        const auto& wire=Of(owner,session).Get(candidate.entityId);
        if(wire.runs.size()!=1||!wire.runs.front().curved||wire.all.size()<2)continue;
        candidate.kind=app::SelectionElementKind::Object;
        candidate.segmentId={};candidate.subshapeKey.reset();candidate.curveParameter.reset();
    }
}
std::vector<base::SegmentId> V2SmoothCurvePickSegments(const QObject& owner,app::DrawingSession& session,
    const app::PickCandidate& candidate) {
    if(candidate.kind!=app::SelectionElementKind::Edge||candidate.segmentId.IsNil())return {};
    if(!NeedsIndex(session,candidate.entityId))return {candidate.segmentId};
    const auto& wire=Of(owner,session).Get(candidate.entityId);const auto at=wire.at.find(candidate.segmentId);
    return at==wire.at.end()?std::vector<base::SegmentId>{}:wire.runs[at->second].ids;
}
app::SelectionSet V2ApplySmoothCurvePick(const QObject& owner,app::DrawingSession& session,
    const app::SelectionSet& current,const std::optional<app::PickCandidate>& picked,app::SelectionMode mode) {
    // A tool can leave a partial span reference before normal picking resumes.
    // Selecting its whole smooth curve must replace those old references too.
    if(picked&&picked->kind==app::SelectionElementKind::Object&&NeedsIndex(session,picked->entityId)) {
        const auto& wire=Of(owner,session).Get(picked->entityId);
        if(wire.runs.size()==1&&wire.runs.front().curved) {
            const bool all=std::all_of(wire.all.begin(),wire.all.end(),[&](auto id){return app::IsCurveSelected(current,picked->entityId,id);});
            auto clean=current;std::erase(clean.entityIds,picked->entityId);
            std::erase_if(clean.ordered,[&](const auto& ref){return ref.entityId==picked->entityId;});
            if(mode==app::SelectionMode::Subtract||(mode==app::SelectionMode::Toggle&&all))
                return app::ApplySelection(clean,std::nullopt,app::SelectionMode::Add);
            return app::ApplySelection(mode==app::SelectionMode::Replace?app::SelectionSet{}:clean,picked,app::SelectionMode::Add);
        }
    }
    const auto ids=picked?V2SmoothCurvePickSegments(owner,session,*picked):std::vector<base::SegmentId>{};
    if(ids.size()<2)return app::ApplySelection(current,picked,mode);
    const bool selected=std::all_of(ids.begin(),ids.end(),[&](auto id){return app::IsCurveSelected(current,picked->entityId,id);});
    if(mode==app::SelectionMode::Toggle)mode=selected?app::SelectionMode::Subtract:app::SelectionMode::Add;
    auto result=mode==app::SelectionMode::Replace?app::SelectionSet{}:current;
    if(mode==app::SelectionMode::Subtract)result=ExpandWhole(result,picked->entityId,Of(owner,session).Get(picked->entityId).all);
    for(const auto id:ids) {
        if(mode!=app::SelectionMode::Subtract&&app::IsCurveSelected(result,picked->entityId,id))continue;
        auto target=*picked;target.segmentId=id;
        if(id!=picked->segmentId){target.curveParameter.reset();target.subshapeKey.reset();}
        result=app::ApplySelection(result,target,mode==app::SelectionMode::Subtract?mode:app::SelectionMode::Add);
    }
    return result;
}
