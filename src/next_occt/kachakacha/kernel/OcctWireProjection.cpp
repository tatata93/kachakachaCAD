#include "kachakacha/kernel/OcctWireProjection.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <optional>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctBoundaryCurves.h"
#include "kachakacha/kernel/OcctWireWrap.h"
#include "kachakacha/kernel/OcctProjectionVisibility.h"
#include "kachakacha/kernel/OcctProjectionConnections.h"
#include "kachakacha/geometry/CurveFit.h"
#include "kachakacha/geometry/WireEdit.h"
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepOffsetAPI_NormalProjection.hxx>
#include <BRepProj_Projection.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <Standard_Failure.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#endif
namespace kachakacha::v2::kernel {
using geometry::Vector3;
using geometry::CurveSegment;
using Out = base::Result<WireProjectionResult>;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
void Require(bool ok,const char* text) { if(!ok)throw std::runtime_error(text); }
std::vector<TopoDS_Face> NativeFaces(const std::vector<modeling::KernelShapeHandle>& handles) {
    std::vector<TopoDS_Face> faces;
    for(const auto& handle:handles) {
        TopoDS_Shape shape;Require(LookupShape(handle,shape),"投影先の形状がありません。選び直してください。");
        for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
            const auto f=TopoDS::Face(e.Current());
            // OCCT projection may update flags, p-curves or tolerances on a target.
            // A deep geometric copy protects the caller's shared native part/guide.
            BRepBuilderAPI_Copy copy(f,true,false);Require(copy.IsDone(),"投影先の面を複製できません。");
            faces.push_back(TopoDS::Face(copy.Shape()));
        }
    }
    Require(!faces.empty(),"投影先の面を選んでください。");return faces;
}
geometry::ClosestPointResult SourceMatch(const Vector3& point,const CurveSegment& curve,
    const WireProjectionInput& input,const WireProjectionOptions& options) {
    if(options.mode!=WireProjectionMode::Direction)return curve.ClosestPoint(point);
    const auto d=geometry::Normalized(input.direction);
    const auto perpendicular=[&](Vector3 v){return v-d*geometry::Dot(v,d);};
    double t=0;
    if(curve.Kind()==geometry::CurveKind::Line) {
        const auto edge=perpendicular(curve.EndPoint()-curve.StartPoint());
        if(edge.LengthSquared()>1e-20)t=std::clamp(geometry::Dot(perpendicular(point-curve.StartPoint()),edge)/edge.LengthSquared(),0.0,1.0);
    } else {
        const auto score=[&](double at){return perpendicular(curve.Evaluate(at)-point).LengthSquared();};
        double best=score(0);constexpr int count=128;
        for(int i=1;i<=count;++i){const double value=score(i/double(count));if(value<best){best=value;t=i/double(count);}}
        double a=std::max(0.0,t-1.0/count),b=std::min(1.0,t+1.0/count);
        for(int i=0;i<48;++i){const double l=a+(b-a)/3,r=b-(b-a)/3;if(score(l)<score(r))b=r;else a=l;}
        if(score((a+b)/2)<best)t=(a+b)/2;
    }
    const auto p=curve.Evaluate(t);return {t,p,geometry::Distance(p,point)};
}
bool Allowed(const TopoDS_Edge& edge,const WireProjectionInput& source,
    const WireProjectionOptions& options) {
    double a,b;const auto curve=BRep_Tool::Curve(edge,a,b);if(curve.IsNull())return false;
    for(int i=0;i<=4;++i) {
        const auto point=FromPoint(curve->Value(a+(b-a)*i/4));
        double best=1e100;Vector3 delta;
        for(const auto& input:source.curves) {
            const auto p=SourceMatch(point,input,source,options);if(p.distance<best){best=p.distance;delta=point-p.point;}
        }
        if(best>options.maximumDistanceMm+options.toleranceMm)return false;
        const auto direction=geometry::Normalized(source.direction);
        if(options.mode==WireProjectionMode::Direction
            &&(delta-direction*geometry::Dot(delta,direction)).Length()>options.toleranceMm*2)return false;
        if(options.mode==WireProjectionMode::Direction&&options.hits==WireProjectionHits::NearestPositive
            &&geometry::Dot(delta,source.direction)<-options.joinToleranceMm)return false;
    }
    return true;
}
void AddEdge(WireProjectionResult& result,const TopoDS_Edge& edge,const WireProjectionInput& source,
    const WireProjectionOptions& options) {
    if(BRep_Tool::Degenerated(edge)||!Allowed(edge,source,options))return;
    const auto converted=detail::BoundaryCurves(edge,options.toleranceMm);
    Require(converted.HasValue(),converted.FirstMessageJa().c_str());
    result.maxCurveDeviationMm=std::max(result.maxCurveDeviationMm,converted.Value().maxDeviationBoundMm);
    auto curves=converted.Value().curves;
    if(!curves.empty()&&source.curves.size()==1&&!source.curves.front().IsClosed(options.toleranceMm)) {
        const auto a=SourceMatch(curves.front().StartPoint(),source.curves.front(),source,options);
        const auto b=SourceMatch(curves.back().EndPoint(),source.curves.front(),source,options);
        if(a.parameter>b.parameter) {
            std::reverse(curves.begin(),curves.end());for(auto& c:curves){const auto r=geometry::ReverseCurve(c);Require(r.HasValue(),"投影曲線の向きを揃えられません。");c=r.Value();}
        }
    }
    if(!curves.empty())result.wires.push_back(std::move(curves));
}
void KeepNearest(WireProjectionResult& result,const WireProjectionInput& input,const WireProjectionOptions& options) {
    const auto match=[&](Vector3 point){return SourceMatch(point,input.curves.front(),input,options);};
    const auto span=[&](const auto& run){double a=match(run.front().StartPoint()).parameter,b=match(run.back().EndPoint()).parameter;return std::pair{std::min(a,b),std::max(a,b)};};
    const auto score=[&](const auto& run){double total=0;int count=0;for(const auto& c:run)for(int i=0;i<=4;++i){total+=match(c.Evaluate(i/4.0)).distance;++count;}return total/count;};
    auto& runs=result.wires;
    for(std::size_t i=0;i<runs.size();++i)for(std::size_t j=i+1;j<runs.size();) {
        const auto a=span(runs[i]),b=span(runs[j]);
        if(std::abs(a.first-b.first)<1e-5&&std::abs(a.second-b.second)<1e-5) {
            if(score(runs[j])+options.toleranceMm<score(runs[i]))runs[i]=std::move(runs[j]);runs.erase(runs.begin()+j);
        } else ++j;
    }
}
void Join(WireProjectionResult& result,double tolerance);
void RestorePlanarForm(WireProjectionResult& result,const CurveSegment& source,
    const WireProjectionInput& input,const std::vector<TopoDS_Face>& faces,const WireProjectionOptions& options) {
    if(faces.size()!=1||result.wires.empty())return;
    constexpr double precision=1e-10;
    const GeomLib_IsPlanarSurface flat(BRep_Tool::Surface(faces.front()),precision);if(!flat.IsPlanar())return;
    const auto plane=flat.Plan();const auto normal=plane.Axis().Direction();const Vector3 n{normal.X(),normal.Y(),normal.Z()};
    const auto direction=options.mode==WireProjectionMode::Closest?n:geometry::Normalized(input.direction);
    const double denominator=geometry::Dot(n,direction);if(std::abs(denominator)<precision)return;
    const auto origin=FromPoint(plane.Location());
    const auto project=[&](Vector3 p){return p+direction*(geometry::Dot(origin-p,n)/denominator);};
    std::optional<CurveSegment> exact;
    if(source.Kind()==geometry::CurveKind::Circle||source.Kind()==geometry::CurveKind::CircularArc) {
        if(geometry::Cross(source.Normal(),n).Length()>precision)return; // Oblique projection is an ellipse, not a circle.
        exact=geometry::TranslateCurve(source,project(source.Center())-source.Center());
    } else if(source.Kind()==geometry::CurveKind::Line) {
        const auto line=CurveSegment::MakeLine(project(source.StartPoint()),project(source.EndPoint()));if(line.HasValue())exact=line.Value();
    } else if(!source.IsClosed(precision)) {
        auto points=source.ControlPoints();for(auto& p:points)p=project(p);
        const auto curve=source.Kind()==geometry::CurveKind::CubicBezier?CurveSegment::MakeCubicBezier(points):CurveSegment::MakeCubicBSpline(points);
        if(curve.HasValue())exact=curve.Value();
    }
    if(!exact)return;auto joined=result;Join(joined,std::min(options.joinToleranceMm,1e-7));if(joined.wires.size()!=1)return;
    const auto& run=joined.wires.front();
    const bool complete=source.Kind()==geometry::CurveKind::Circle?joined.openEnds==0:
        geometry::Distance(run.front().StartPoint(),exact->StartPoint())<1e-7&&geometry::Distance(run.back().EndPoint(),exact->EndPoint())<1e-7;
    if(complete){result.wires={{*exact}};result.maxCurveDeviationMm=precision;result.openEnds=source.Kind()==geometry::CurveKind::Circle?0:2;}
}
void ProjectOne(WireProjectionResult& result,const std::vector<TopoDS_Face>& faces,
    const WireProjectionInput& input,const WireProjectionOptions& options,
    std::vector<detail::ProjectionRunOrigin>& origins,const detail::ProjectionRunOrigin* inherited=nullptr) {
    // Separate edges also support an unordered selection of disconnected source wires.
    for(const auto& segment:input.curves) {
        const auto origin=inherited?*inherited:detail::ProjectionRunOrigin{&segment};
        WireProjectionResult pieces;WireProjectionInput one{{segment},input.direction};
        const auto d=geometry::Normalized(input.direction),delta=segment.EndPoint()-segment.StartPoint();
        if(options.mode==WireProjectionMode::Direction&&segment.Kind()==geometry::CurveKind::Line
            &&(delta-d*geometry::Dot(delta,d)).LengthSquared()<1e-20)continue;
        const auto edge=ToEdge(segment);Require(edge.HasValue(),edge.FirstMessageJa().c_str());
        if(options.mode==WireProjectionMode::Closest) {
            TopoDS_Compound target;BRep_Builder builder;builder.MakeCompound(target);
            for(const auto& face:faces)builder.Add(target,face);
            BRepOffsetAPI_NormalProjection projection(target);projection.Add(edge.Value());
            projection.SetParams(options.toleranceMm*0.25,options.toleranceMm*options.toleranceMm*0.0625,GeomAbs_C1,8,64);
            projection.SetMaxDistance(options.maximumDistanceMm);projection.SetLimit(true);projection.Compute3d(true);projection.Build();
            Require(projection.IsDone(),"最近点投影を計算できません。方向投影も試してください。");
            for(TopExp_Explorer e(projection.Projection(),TopAbs_EDGE);e.More();e.Next())AddEdge(pieces,TopoDS::Edge(e.Current()),one,options);
        } else {
            Require(input.direction.IsFinite()&&input.direction.Length()>1e-12,"投影方向が零です。");
            for(const auto& face:faces) {
                const BRepAdaptor_Surface surface(face);
                // A ray contained in a plane has infinitely many intersections, not a projected curve.
                if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().Dot(gp_Dir(ToVector(input.direction))))<1e-10)continue;
                BRepProj_Projection projection(edge.Value(),face,gp_Dir(ToVector(input.direction)));
                if(!projection.IsDone())continue;
                for(;projection.More();projection.Next())for(BRepTools_WireExplorer e(projection.Current());e.More();e.Next())AddEdge(pieces,e.Current(),one,options);
            }
        }
        if((options.mode==WireProjectionMode::Closest||options.hits!=WireProjectionHits::All)&&segment.IsClosed(1e-9)) {
            // Complementary arcs of a periodic source can share BOTH endpoints.
            // They are not duplicate hits. Join a complete single contour first;
            // resolve multiple/partial layers using exact nonperiodic subarcs.
            auto joined=pieces;Join(joined,options.joinToleranceMm);
            if(joined.wires.size()==1&&joined.openEnds==0)pieces=std::move(joined);
            else if(pieces.wires.size()>1) {
                if(segment.Kind()==geometry::CurveKind::CubicBSpline) {
                    WireProjectionInput spans;spans.direction=input.direction;
                    for(const auto& control:geometry::UniformBSplineBezierSpans(segment.ControlPoints())) {
                        const auto bezier=CurveSegment::MakeCubicBezier({control.begin(),control.end()});
                        Require(bezier.HasValue(),"閉スプラインの投影区間を分けられません。");spans.curves.push_back(bezier.Value());
                    }
                    Require(!spans.curves.empty(),"閉スプラインの制御点が足りません。");ProjectOne(result,faces,spans,options,origins,&origin);continue;
                }
                const auto half=segment.Split(.5);Require(half.HasValue(),"閉曲線の投影区間を分けられません。");
                const auto first=half.Value().first->Split(.5),second=half.Value().second->Split(.5);
                Require(first.HasValue()&&second.HasValue(),"閉曲線の投影区間を分けられません。");
                ProjectOne(result,faces,{{*first.Value().first,*first.Value().second,*second.Value().first,*second.Value().second},input.direction},options,origins,&origin);
                continue;
            }
        } else if(options.mode==WireProjectionMode::Direction&&options.hits!=WireProjectionHits::All)
            detail::SelectVisibleProjection(pieces,[&](Vector3 point){return SourceMatch(point,segment,one,options);},options);
        else if(options.mode==WireProjectionMode::Closest||options.hits!=WireProjectionHits::All)KeepNearest(pieces,one,options);
        RestorePlanarForm(pieces,segment,one,faces,options);
        result.maxCurveDeviationMm=std::max(result.maxCurveDeviationMm,pieces.maxCurveDeviationMm);
        result.wires.insert(result.wires.end(),pieces.wires.begin(),pieces.wires.end());
        origins.insert(origins.end(),pieces.wires.size(),origin);
    }
}
bool SameRun(const std::vector<CurveSegment>& a,const std::vector<CurveSegment>& b,double tolerance) {
    if(a.empty()||b.empty())return false;
    const auto middle=a[a.size()/2].Evaluate(0.5);
    double distance=1e100;for(const auto& c:b)distance=std::min(distance,c.ClosestPoint(middle).distance);
    return distance<=tolerance && ((geometry::Distance(a.front().StartPoint(),b.front().StartPoint())<=tolerance
        &&geometry::Distance(a.back().EndPoint(),b.back().EndPoint())<=tolerance)
        ||(geometry::Distance(a.front().StartPoint(),b.back().EndPoint())<=tolerance
        &&geometry::Distance(a.back().EndPoint(),b.front().StartPoint())<=tolerance));
}
void Reverse(std::vector<CurveSegment>& curves) {
    std::reverse(curves.begin(),curves.end());for(auto& c:curves){const auto reversed=geometry::ReverseCurve(c);Require(reversed.HasValue(),"曲線の向きを反転できません。");c=reversed.Value();}
}
int EndpointCount(const std::vector<std::vector<CurveSegment>>& runs,Vector3 point,double tolerance) {
    int count=0;for(const auto& run:runs) {
        if(geometry::Distance(point,run.front().StartPoint())<=tolerance)++count;
        if(geometry::Distance(point,run.back().EndPoint())<=tolerance)++count;
    }
    return count;
}
void Join(WireProjectionResult& result,double tolerance) {
    // Generated native edge ends must coincide. The interactive pick radius can
    // exceed an entire small lamp; it must not collapse its arcs or close a gap.
    tolerance=std::min(tolerance,1e-6);
    result.openEnds=0;
    auto& runs=result.wires;
    for(std::size_t i=0;i<runs.size();++i)for(std::size_t j=i+1;j<runs.size();) {
        if(SameRun(runs[i],runs[j],tolerance))runs.erase(runs.begin()+j);else ++j;
    }
    bool changed=true;
    while(changed) {changed=false;
        for(std::size_t i=0;i<runs.size()&&!changed;++i) {
            if(geometry::Distance(runs[i].front().StartPoint(),runs[i].back().EndPoint())<=tolerance)continue;
            for(std::size_t j=i+1;j<runs.size();++j) {
                const auto a=runs[i].front().StartPoint(),b=runs[i].back().EndPoint();
                const auto c=runs[j].front().StartPoint(),d=runs[j].back().EndPoint();
                if(geometry::Distance(b,c)<=tolerance&&EndpointCount(runs,b,tolerance)==2){}
                else if(geometry::Distance(b,d)<=tolerance&&EndpointCount(runs,b,tolerance)==2)Reverse(runs[j]);
                else if(geometry::Distance(a,d)<=tolerance&&EndpointCount(runs,a,tolerance)==2)std::swap(runs[i],runs[j]);
                else if(geometry::Distance(a,c)<=tolerance&&EndpointCount(runs,a,tolerance)==2){Reverse(runs[i]);}
                else continue;
                runs[i].insert(runs[i].end(),runs[j].begin(),runs[j].end());runs.erase(runs.begin()+j);changed=true;break;
            }
        }
    }
    for(auto& run:runs){detail::SimplifyProjectionRun(run);if(geometry::Distance(run.front().StartPoint(),run.back().EndPoint())>tolerance)result.openEnds+=2;}
}
double SurfaceDistance(const Vector3& point,const std::vector<TopoDS_Face>& faces,double tolerance) {
    double best=1e100;
    for(const auto& face:faces) {
        const auto surface=BRep_Tool::Surface(face);GeomAPI_ProjectPointOnSurf query(ToPoint(point),surface);
        if(query.NbPoints()==0)continue;
        double u,v;query.LowerDistanceParameters(u,v);
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(surface->IsUPeriodic())u+=std::round(((u0+u1)*.5-u)/surface->UPeriod())*surface->UPeriod();
        if(surface->IsVPeriodic())v+=std::round(((v0+v1)*.5-v)/surface->VPeriod())*surface->VPeriod();
        BRepClass_FaceClassifier classifier(face,gp_Pnt2d(u,v),tolerance);
        if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)best=std::min(best,query.LowerDistance());
    }
    return best;
}
}
#endif
Out ProjectNativeWires(const std::vector<modeling::KernelShapeHandle>& targets,
    const std::vector<WireProjectionInput>& inputs,const WireProjectionOptions& options) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        Require(std::isfinite(options.toleranceMm)&&options.toleranceMm>=1e-7&&options.toleranceMm<=1,
            "許容偏差は0.0000001〜1 mmで指定してください。");
        Require(std::isfinite(options.joinToleranceMm)&&options.joinToleranceMm>=0
            &&std::isfinite(options.maximumDistanceMm)&&options.maximumDistanceMm>0,"接続許容差と最大距離を確認してください。");
        Require(!inputs.empty(),"投影するワイヤーを選んでください。");
        const auto faces=NativeFaces(targets);WireProjectionResult result;
        if(options.mode==WireProjectionMode::Wrap) {
            if(faces.size()!=1)return Out::Failure(base::MakeError(kWrapNotDevelopable,
                "等長巻き付けは平面・円柱・円錐を1面ずつ指定してください。","複数面には方向投影／最近点投影を使い、端点を接続します。"));
            const auto wrapped=detail::WrapNativeWires(faces.front(),inputs,options);
            if(!wrapped.HasValue())return wrapped;result=wrapped.Value();
        } else {
            std::vector<detail::ProjectionRunOrigin> origins;
            for(const auto& input:inputs)ProjectOne(result,faces,input,options,origins);
            if(faces.size()==1)detail::ConnectSingleFaceProjection(result,faces.front(),origins,options.toleranceMm);
        }
        Require(!result.wires.empty(),"指定した向き・距離では選んだ面に当たりません。方向反転や投影先を確認してください。");
        Join(result,options.joinToleranceMm);
        for(const auto& run:result.wires)for(const auto& c:run)for(int k=0;k<=16;++k) {
            const double d=SurfaceDistance(c.Evaluate(k/16.0),faces,options.toleranceMm*2);
            Require(d<=options.toleranceMm*3,"投影結果が選択面の範囲から外れています。基準点・面・許容偏差を確認してください。");
            result.maxSampledSurfaceDeviationMm=std::max(result.maxSampledSurfaceDeviationMm,d);
        }
        return Out::Success(std::move(result));
    } catch(const Standard_Failure& error) {return Out::Failure(base::MakeError(kWireProjectionFailed,"曲面への投影に失敗しました。",error.what()));}
    catch(const std::exception& error) {return Out::Failure(base::MakeError(kWireProjectionFailed,"曲面への投影に失敗しました。",error.what()));}
#else
    (void)targets;(void)inputs;(void)options;
    return Out::Failure(base::MakeError(kWireProjectionFailed,"投影にはネイティブ幾何カーネルが必要です。",{}));
#endif
}
}
