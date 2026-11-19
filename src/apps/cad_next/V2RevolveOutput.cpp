#include "V2RevolveOutput.h"
#include "V2MainWindow.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

using namespace kachakacha::v2;
namespace {
using Curves=std::vector<geometry::CurveSegment>;
using Groups=v2ui::RevolveGroups;
bool Same(const geometry::CurveSegment& a,const geometry::CurveSegment& b,double tolerance)
{
    using geometry::CurveKind;
    if(a.Kind()!=b.Kind())return false;
    const auto pointsClose=[&](auto p,auto q){return (p-q).Length()<=tolerance;};
    if(a.Kind()==CurveKind::Circle)return pointsClose(a.Center(),b.Center())
        && std::abs(a.Radius()-b.Radius())<=tolerance
        && geometry::Cross(a.Normal(),b.Normal()).Length()*a.Radius()<=tolerance;
    if(a.Kind()==CurveKind::CircularArc)return pointsClose(a.Center(),b.Center())
        && std::abs(a.Radius()-b.Radius())<=tolerance
        && std::abs(std::abs(a.SweepAngleRad())-std::abs(b.SweepAngleRad()))*a.Radius()<=tolerance
        && pointsClose(a.Evaluate(.5),b.Evaluate(.5))
        && ((pointsClose(a.StartPoint(),b.StartPoint())&&pointsClose(a.EndPoint(),b.EndPoint()))
            ||(pointsClose(a.StartPoint(),b.EndPoint())&&pointsClose(a.EndPoint(),b.StartPoint())));
    const auto& x=a.ControlPoints();const auto& y=b.ControlPoints();
    if(x.size()!=y.size())return false;
    bool forward=true,reverse=true;
    for(std::size_t i=0;i<x.size();++i){forward&=pointsClose(x[i],y[i]);reverse&=pointsClose(x[i],y[y.size()-1-i]);}
    return forward||reverse;
}
void UniqueGroup(Groups& groups,Curves candidate,Curves& known,double tolerance)
{
    if(candidate.empty()||std::all_of(candidate.begin(),candidate.end(),[&](const auto& curve) {
        return std::any_of(known.begin(),known.end(),[&](const auto& other){return Same(curve,other,tolerance);});
    }))return;
    known.insert(known.end(),candidate.begin(),candidate.end());groups.push_back(std::move(candidate));
}
base::Result<Groups> FromTable(const modeling::GuideTable& table,Curves& known,
    const geometry::GeometryTolerance& tolerance)
{
    using Out=base::Result<Groups>;
    const auto request=modeling::ToGuideSurfaceRequest(table,tolerance);
    if(!request.HasValue())return Out::Failure(request.Diagnostics());
    const auto axis=geometry::Normalized(table.revolveAxisDirection),origin=table.revolveAxisPoint;
    const double full=2*std::numbers::pi,angle=table.revolveAngleRad;
    const bool closed=std::abs(std::abs(angle)-full)<=tolerance.modelAngularRad;
    Groups groups;
    for(const auto& chain:request.Value().chains) {
        if(chain.role!=modeling::ChainRole::Section||chain.segments.empty())continue;
        Curves opposite;
        for(const auto& curve:chain.segments) {
            const auto rotated=geometry::RotateCurve(curve,origin,axis,closed?std::copysign(std::numbers::pi,angle):angle);
            if(!rotated.HasValue())return Out::Failure(rotated.Diagnostics());
            opposite.push_back(rotated.Value());
        }
        UniqueGroup(groups,std::move(opposite),known,tolerance.modelLinearMm);
        // Only the ends of a logical profile draw tracks, never its tessellation or inner joins.
        if((chain.segments.front().StartPoint()-chain.segments.back().EndPoint()).Length()<=tolerance.modelLinearMm)continue;
        for(const auto point:{chain.segments.front().StartPoint(),chain.segments.back().EndPoint()}) {
            const auto center=origin+axis*geometry::Dot(point-origin,axis),radial=point-center;
            if(radial.Length()<=tolerance.modelLinearMm)continue;
            const auto track=closed?geometry::CurveSegment::MakeCircle(center,axis,radial,radial.Length())
                :geometry::CurveSegment::MakeCircularArc(center,axis,radial,radial.Length(),0,angle);
            if(!track.HasValue())return Out::Failure(track.Diagnostics());
            UniqueGroup(groups,{track.Value()},known,tolerance.modelLinearMm);
        }
    }
    return Out::Success(std::move(groups));
}
}

base::Result<v2ui::RevolveGroups> v2ui::RevolveWires(const V2SurfaceSnapshot& snapshot,
    const modeling::SnapScene& scene,const document::DocumentSnapshot& document)
{
    using Out=base::Result<Groups>;
    const auto& tolerance=document.settings.tolerance;
    std::set<base::EntityId> wires;
    for(const auto& entity:document.entities)if(entity.kind==domain::EntityKind::Wire)wires.insert(entity.id);
    Curves known;
    // A face edge is not an editable WireEntity. It must not suppress a requested wire copy.
    for(const auto& curve:scene.curves)if(wires.contains(curve.entityId))known.push_back(curve.segment);
    std::vector<modeling::GuideTable> tables{snapshot.table};
    for(const auto& [table,built]:snapshot.batch)tables.push_back(table);
    for(const auto& table:tables)for(const auto& row:table.rows)
        known.insert(known.end(),row.segments.begin(),row.segments.end());
    Groups result;
    for(const auto& table:tables) {
        const auto groups=FromTable(table,known,tolerance);
        if(!groups.HasValue())return Out::Failure(groups.Diagnostics());
        result.insert(result.end(),groups.Value().begin(),groups.Value().end());
    }
    return Out::Success(std::move(result));
}

base::Result<std::vector<base::EntityId>> v2ui::CommitRevolveOutput(V2MainWindow& window,
    const V2SurfaceSnapshot& snapshot,RevolveOutput mode,const AdoptRevolve& adopt)
{
    using Out=base::Result<std::vector<base::EntityId>>;
    auto& document=window.Session().GetDocument();
    const auto groups=RevolveWires(snapshot,window.Session().Scene(),document.Snapshot());
    if(!groups.HasValue())return Out::Failure(groups.Diagnostics());
    if(groups.Value().empty()&&mode==RevolveOutput::Wires)
        return Out::Failure(base::MakeError("UI-R004","新しく残す輪郭がありません。元と同じ線は増やしません。"));
    document::Document::Transaction transaction(document,"回転面の生成物を作る");
    std::vector<base::EntityId> ids;
    for(const auto& group:groups.Value()) {
        const auto id=window.AddPlainWire(group,"回転の輪郭",false);
        if(id.IsNil())return Out::Failure(base::MakeError("UI-R004","輪郭を作れなかったため、今回の生成を取り消しました。"));
        ids.push_back(id);
    }
    if(mode==RevolveOutput::Both) {
        const auto id=adopt(snapshot.table,snapshot.built);
        if(id.IsNil())return Out::Failure(base::MakeError("UI-R004","面を作れなかったため、今回の生成を取り消しました。"));
        ids.push_back(id);
        for(const auto& [table,built]:snapshot.batch) {
            const auto next=adopt(table,built);
            if(next.IsNil())return Out::Failure(base::MakeError("UI-R004","面を作れなかったため、今回の生成を取り消しました。"));
            ids.push_back(next);
        }
    }
    if(!transaction.Commit())return Out::Failure(base::MakeError("UI-R004","生成物を文書へ確定できませんでした。"));
    return Out::Success(std::move(ids));
}
