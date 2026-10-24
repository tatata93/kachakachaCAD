#include "kachakacha/kernel/OcctProjectionConnections.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <gp_Pnt2d.hxx>
#include <algorithm>
#include <optional>
#include <cmath>
namespace kachakacha::v2::kernel::detail {
namespace {
using geometry::CurveSegment;
using geometry::CurveKind;
using geometry::Vector3;
struct End {std::size_t run;bool last;Vector3 point;};
bool SourcesConnect(const ProjectionRunOrigin& a,const ProjectionRunOrigin& b) {
    if(!a.source||!b.source)return false;if(a.source==b.source)return true;
    for(const auto& p:{a.source->StartPoint(),a.source->EndPoint()})
        for(const auto& q:{b.source->StartPoint(),b.source->EndPoint()})if(geometry::Distance(p,q)<=1e-6)return true;
    return false;
}
std::optional<CurveSegment> MoveEnd(const CurveSegment& curve,bool last,Vector3 point) {
    if(curve.Kind()==CurveKind::Line) {
        const auto moved=CurveSegment::MakeLine(last?curve.StartPoint():point,last?point:curve.EndPoint());
        if(moved.HasValue())return moved.Value();
    } else if(curve.Kind()==CurveKind::CubicBezier) {
        auto points=curve.ControlPoints();const auto delta=point-(last?curve.EndPoint():curve.StartPoint());
        if(last){points[3]=points[3]+delta;points[2]=points[2]+delta;}else{points[0]=points[0]+delta;points[1]=points[1]+delta;}
        const auto moved=CurveSegment::MakeCubicBezier(points);if(moved.HasValue())return moved.Value();
    } else if(curve.Kind()==CurveKind::CubicBSpline&&curve.ControlPoints().size()>=6) {
        auto points=curve.ControlPoints();const auto delta=point-(last?curve.EndPoint():curve.StartPoint());
        for(std::size_t i=0;i<3;++i)points[last?points.size()-1-i:i]=points[last?points.size()-1-i:i]+delta;
        const auto moved=CurveSegment::MakeCubicBSpline(points);if(moved.HasValue())return moved.Value();
    }
    return {};
}
bool Inside(const TopoDS_Face& face,const occ::handle<Geom_Surface>& surface,double u,double v) {
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    if(surface->IsUPeriodic())u+=std::round(((u0+u1)*.5-u)/surface->UPeriod())*surface->UPeriod();
    if(surface->IsVPeriodic())v+=std::round(((v0+v1)*.5-v)/surface->VPeriod())*surface->VPeriod();
    const BRepClass_FaceClassifier classifier(face,gp_Pnt2d(u,v),1e-7);
    return classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON;
}
}
void ConnectSingleFaceProjection(WireProjectionResult& result,const TopoDS_Face& face,
    const std::vector<ProjectionRunOrigin>& origins,double tolerance) {
    if(origins.size()!=result.wires.size())return;
    const double budget=std::min(tolerance*.25,tolerance-result.maxCurveDeviationMm);
    if(budget<=1e-7)return;std::vector<End> ends;
    for(std::size_t i=0;i<result.wires.size();++i){const auto& run=result.wires[i];
        ends.push_back({i,false,run.front().StartPoint()});ends.push_back({i,true,run.back().EndPoint()});}
    std::vector<std::vector<std::size_t>> candidates(ends.size());
    for(std::size_t i=0;i<ends.size();++i)for(std::size_t j=i+1;j<ends.size();++j) {
        if(ends[i].run==ends[j].run||!SourcesConnect(origins[ends[i].run],origins[ends[j].run]))continue;
        const double distance=geometry::Distance(ends[i].point,ends[j].point);
        if(distance>1e-6&&distance<=budget*2){candidates[i].push_back(j);candidates[j].push_back(i);}
    }
    double maximumMove=0;const auto surface=BRep_Tool::Surface(face);
    for(std::size_t i=0;i<ends.size();++i) {
        if(candidates[i].size()!=1)continue;const auto j=candidates[i].front();if(j<=i||candidates[j].size()!=1)continue;
        GeomAPI_ProjectPointOnSurf query(ToPoint((ends[i].point+ends[j].point)*.5),surface);if(query.NbPoints()==0)continue;
        double u,v;query.LowerDistanceParameters(u,v);if(!Inside(face,surface,u,v))continue;
        const auto point=FromPoint(query.NearestPoint());const double shift=std::max(geometry::Distance(point,ends[i].point),geometry::Distance(point,ends[j].point));
        if(shift>budget)continue;
        auto& first=ends[i].last?result.wires[ends[i].run].back():result.wires[ends[i].run].front();
        auto& second=ends[j].last?result.wires[ends[j].run].back():result.wires[ends[j].run].front();
        const auto a=MoveEnd(first,ends[i].last,point),b=MoveEnd(second,ends[j].last,point);if(!a||!b)continue;
        first=*a;second=*b;maximumMove=std::max(maximumMove,shift);
    }
    // Moving two adjacent Bezier controls by delta changes the entire curve by
    // at most |delta| (Bernstein convexity), including the preserved end tangent.
    result.maxCurveDeviationMm+=maximumMove;
}
}
#endif
