#include "V2PlanarCurves.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <cmath>
namespace V2PlanarCurves {
using geometry::Vector3;
std::optional<geometry::PlanarFrame> SourceFrame(const fabrication::PatternPanel& panel,
    const app::FabricationSource& source,double tolerance)
{
    std::vector<Vector3> points;
    for(const auto& curve:*source.flatBoundary)
        for(const auto& sample:geometry::SampleCurve(curve,tolerance))points.push_back(sample.position);
    if(points.size()!=panel.outline.size()||points.size()<3)return {};
    const auto first=panel.outline.front();
    for(std::size_t a=1;a<points.size();++a)for(std::size_t b=a+1;b<points.size();++b) {
        const double u=panel.outline[a].u-first.u,v=panel.outline[a].v-first.v;
        const double x=panel.outline[b].u-first.u,y=panel.outline[b].v-first.v;
        const double det=u*y-v*x;if(std::abs(det)<1e-8)continue;
        geometry::PlanarFrame frame;
        frame.uDirection=((points[a]-points[0])*y-(points[b]-points[0])*v)*(1/det);
        frame.vDirection=((points[b]-points[0])*u-(points[a]-points[0])*x)*(1/det);
        if(std::abs(frame.uDirection.Length()-1)>1e-6||std::abs(frame.vDirection.Length()-1)>1e-6
            ||std::abs(geometry::Dot(frame.uDirection,frame.vDirection))>1e-6)return {};
        frame.normal=geometry::Cross(frame.uDirection,frame.vDirection);
        frame.origin=points[0]-frame.uDirection*first.u-frame.vDirection*first.v;
        return frame;
    }
    return {};
}
Vector3 Rotate(Vector3 p,Vector3 axis,double angle)
{
    return p*std::cos(angle)+geometry::Cross(axis,p)*std::sin(angle)
        +axis*(geometry::Dot(axis,p)*(1-std::cos(angle)));
}
Vector3 PoseVector(Vector3 p,const geometry::PlanarFrame& frame,double progress)
{
    const Vector3 z{0,0,1};auto axis=geometry::Cross(z,frame.normal);
    const double angle=std::acos(std::clamp(geometry::Dot(z,frame.normal),-1.0,1.0));
    axis=axis.Length()>1e-9?geometry::Normalized(axis):Vector3{1,0,0};
    const auto x=Rotate({1,0,0},axis,angle);
    const double twist=std::atan2(geometry::Dot(geometry::Cross(x,frame.uDirection),frame.normal),
        geometry::Dot(x,frame.uDirection));
    return Rotate(Rotate(p,z,twist*progress),axis,angle*progress);
}
std::vector<geometry::CurveSegment> PoseCurves(const std::vector<geometry::CurveSegment>& curves,
    const geometry::PlanarFrame& frame,double progress)
{
    const Vector3 z{0,0,1};auto axis=geometry::Cross(z,frame.normal);
    const double angle=std::acos(std::clamp(geometry::Dot(z,frame.normal),-1.0,1.0));
    axis=axis.Length()>1e-9?geometry::Normalized(axis):Vector3{1,0,0};
    const auto x=Rotate({1,0,0},axis,angle);
    const double twist=std::atan2(geometry::Dot(geometry::Cross(x,frame.uDirection),frame.normal),geometry::Dot(x,frame.uDirection));
    std::vector<geometry::CurveSegment> result;
    for(const auto& curve:curves) {
        auto next=geometry::RotateCurve(geometry::TranslateCurve(curve,frame.origin*(-1)),{},axis,-angle);if(!next.HasValue())return {};
        next=geometry::RotateCurve(next.Value(),{},z,twist*(progress-1));if(!next.HasValue())return {};
        next=geometry::RotateCurve(next.Value(),{},axis,angle*progress);if(!next.HasValue())return {};
        result.push_back(geometry::TranslateCurve(next.Value(),frame.origin*progress));
    }
    return result;
}
bool SameLoop(const std::vector<geometry::CurveSegment>& curves,const std::vector<geometry::Point2>& loop,
    const geometry::PlanarFrame& frame,double tolerance)
{
    std::vector<Vector3> points;for(const auto& curve:curves)
        for(const auto& p:geometry::SampleCurve(curve,tolerance))points.push_back(p.position);
    if(points.size()!=loop.size())return false;
    for(std::size_t i=0;i<points.size();++i)
        if(geometry::Distance(points[i],frame.origin+frame.uDirection*loop[i].u+frame.vDirection*loop[i].v)>1e-6)return false;
    return true;
}
base::Result<std::vector<exporters::PatternCurve>> Place(const fabrication::PatternPanel& panel,
    const fabrication::PatternPlacement& placement,const app::FabricationSource& source,
    const app::FabricationMarkings& markings,const geometry::PlanarFrame& frame,double tolerance)
{
    using Out=base::Result<std::vector<exporters::PatternCurve>>;
    std::vector<exporters::PatternCurve> exact;
    for(const auto& path:Paths(panel,source,markings,frame,tolerance))
        for(const auto& curve:PoseCurves(path.curves,frame,0)){
            const auto rotated=geometry::RotateCurve(curve,{},{0,0,1},placement.rotationRad);
            if(!rotated.HasValue())return Out::Failure(rotated.Diagnostics());
            exact.push_back({path.layer,geometry::TranslateCurve(rotated.Value(),
                {placement.translationMm.u,placement.translationMm.v,0}),path.mountain,path.label});
        }
    return Out::Success(std::move(exact));
}
std::vector<Path> Paths(const fabrication::PatternPanel& panel,
    const app::FabricationSource& source,const app::FabricationMarkings& markings,
    const geometry::PlanarFrame& frame,double tolerance)
{
    std::vector<Path> result;
    const auto append=[&](const auto& points,bool closed,exporters::PatternLine layer,
        const std::string& label,bool mountain,const auto& candidates){
        std::vector<geometry::CurveSegment> curves;
        for(const auto& candidate:candidates)if(SameLoop(candidate,points,frame,tolerance)){curves=candidate;break;}
        if(curves.empty()){
            auto loop=points;if(closed&&!loop.empty())loop.push_back(loop.front());
            for(std::size_t i=1;i<loop.size();++i){
                const auto world=[&](const auto& p){return frame.origin+frame.uDirection*p.u+frame.vDirection*p.v;};
                const auto line=geometry::CurveSegment::MakeLine(world(loop[i-1]),world(loop[i]));
                if(line.HasValue())curves.push_back(line.Value());
            }
        }
        result.push_back({layer,std::move(curves),mountain,label});
    };
    const std::vector<std::vector<geometry::CurveSegment>> outer={*source.flatBoundary};
    append(panel.outline,true,exporters::PatternLine::Outline,panel.panelId,false,outer);
    auto openings=source.flatOpenings;openings.insert(openings.end(),markings.openings.begin(),markings.openings.end());
    for(const auto& loop:panel.openings)append(loop,true,exporters::PatternLine::Opening,panel.panelId,false,openings);
    for(const auto& fold:panel.folds)append(fold.path,false,exporters::PatternLine::Fold,fold.foldId,
        fold.sense==fabrication::FoldSense::Mountain,markings.folds);
    for(const auto& cut:panel.reliefCuts)append(cut,false,exporters::PatternLine::Cut,panel.panelId,false,markings.reliefCuts);
    return result;
}
}
