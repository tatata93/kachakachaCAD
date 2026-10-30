#include "kachakacha/kernel/OcctMassProperties.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <BRepAdaptor_Curve2d.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <gp_Pnt2d.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Vec.hxx>
#include <gp_Vec2d.hxx>
#include <NCollection_Array1.hxx>
#include <array>
#include <vector>
#include <optional>

namespace kachakacha::v2::kernel::detail {
namespace {
bool Accurate(double error,const GProp_GProps& p) {
    return std::isfinite(error)&&error>=0&&error<=1e-8&&std::isfinite(p.Mass());
}
TopoDS_Shape DividedCopy(const TopoDS_Shape& shape) {
    BRepBuilderAPI_Copy copy(shape,true,false);
    ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
    split.SetSurfaceCriterion(GeomAbs_C3);split.SetBoundaryCriterion(GeomAbs_C3);
    split.SetPCurveCriterion(GeomAbs_C3);split.SetTolerance(1e-7);split.Perform();
    return split.Result();
}
struct Quadrature {
    std::vector<double> nodes,weights;
    explicit Quadrature(int n) :nodes(n),weights(n) {
        const double pi=std::acos(-1.0);
        for(int i=0;i<(n+1)/2;++i) {
            double x=std::cos(pi*(i+.75)/(n+.5)),derivative=0;
            for(int iteration=0;iteration<32;++iteration) {
                double p=1,previous=0;
                for(int k=1;k<=n;++k){const double old=p;p=((2*k-1)*x*p-(k-1)*previous)/k;previous=old;}
                derivative=n*(x*p-previous)/(x*x-1);
                const double step=p/derivative;x-=step;if(std::abs(step)<1e-15)break;
            }
            nodes[i]=-x;nodes[n-1-i]=x;
            weights[i]=weights[n-1-i]=2/((1-x*x)*derivative*derivative);
        }
    }
};
using Integral=std::array<double,4>;
Integral PointFlux(BRepAdaptor_Surface& surface,const gp_Pnt& origin,double u,double v,bool centroid) {
    gp_Pnt p;gp_Vec du,dv;surface.D1(u,v,p,du,dv);
    const gp_Vec r(origin,p);const double flux=r.Dot(du.Crossed(dv));
    return {flux/3,centroid?r.X()*flux/4:0,centroid?r.Y()*flux/4:0,centroid?r.Z()*flux/4:0};
}
Integral Flux(BRepAdaptor_Surface& surface,const gp_Pnt& origin,double u0,double u1,
    double v0,double v1,const Quadrature& rule,bool centroid) {
    Integral sum{};const double hu=(u1-u0)*.5,hv=(v1-v0)*.5;
    for(std::size_t i=0;i<rule.nodes.size();++i)for(std::size_t j=0;j<rule.nodes.size();++j) {
        const auto value=PointFlux(surface,origin,(u0+u1)*.5+hu*rule.nodes[i],(v0+v1)*.5+hv*rule.nodes[j],centroid);
        const double weight=hu*hv*rule.weights[i]*rule.weights[j];
        for(int k=0;k<(centroid?4:1);++k)sum[k]+=value[k]*weight;
    }
    return sum;
}
Integral Integrate(BRepAdaptor_Surface& surface,const gp_Pnt& origin,double u0,double u1,
    double v0,double v1,bool centroid,int depth,Integral& error) {
    static const Quadrature coarse(8),fine(12);
    const auto a=Flux(surface,origin,u0,u1,v0,v1,coarse,centroid);
    const auto b=Flux(surface,origin,u0,u1,v0,v1,fine,centroid);
    double difference=0;
    for(int k=0;k<(centroid?4:1);++k)difference=std::max(difference,std::abs(a[k]-b[k])/std::max(1.0,std::abs(b[k])));
    if(difference<1e-10||depth==8){
        for(int k=0;k<(centroid?4:1);++k)error[k]+=std::abs(a[k]-b[k]);
        return b;
    }
    Integral sum{};const double um=(u0+u1)*.5,vm=(v0+v1)*.5;
    for(int u=0;u<2;++u)for(int v=0;v<2;++v){
        const auto cell=Integrate(surface,origin,u?um:u0,u?u1:um,v?vm:v0,v?v1:vm,centroid,depth+1,error);
        for(int k=0;k<4;++k)sum[k]+=cell[k];
    }
    return sum;
}
bool RectangularEdge(const BRepAdaptor_Curve2d& curve,double u0,double u1,double v0,double v1) {
    const double tu=std::max(1e-12,(u1-u0)*1e-10),tv=std::max(1e-12,(v1-v0)*1e-10);
    const auto first=curve.Value(curve.FirstParameter()),last=curve.Value(curve.LastParameter());
    const bool vertical=std::abs(first.X()-last.X())<=tu&&
        (std::abs(first.X()-u0)<=tu||std::abs(first.X()-u1)<=tu);
    const bool horizontal=std::abs(first.Y()-last.Y())<=tv&&
        (std::abs(first.Y()-v0)<=tv||std::abs(first.Y()-v1)<=tv);
    if(!vertical&&!horizontal)return false;
    if(curve.GetType()==GeomAbs_Line)return true;
    // STEP may encode a straight UV trim as a rational spline. Prove that its
    // whole control hull lies on a rectangle edge; endpoint sampling is not enough.
    const auto contained=[&](const auto& spline) {
        for(int i=1;i<=spline->NbPoles();++i) {
            if(!(spline->Weight(i)>0))return false;
            const auto p=spline->Pole(i);
            if(vertical&&(std::abs(p.X()-first.X())>tu||p.Y()<v0-tv||p.Y()>v1+tv))return false;
            if(horizontal&&(std::abs(p.Y()-first.Y())>tv||p.X()<u0-tu||p.X()>u1+tu))return false;
        }
        return true;
    };
    if(curve.GetType()==GeomAbs_BSplineCurve) {
        return contained(curve.BSpline());
    }
    if(curve.GetType()==GeomAbs_BezierCurve) {
        return contained(curve.Bezier());
    }
    return false;
}
bool Rectangle(const TopoDS_Face& face,double u0,double u1,double v0,double v1) {
    int wires=0;for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next())++wires;
    if(wires!=1)return false;
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge))continue;
        if(!RectangularEdge(BRepAdaptor_Curve2d(edge,face),u0,u1,v0,v1))return false;
    }
    return true;
}
struct Estimate {Integral value{},error{};};
template<class Function>
Estimate LineRule(const Function& function,double first,double last,const Quadrature& rule,bool centroid) {
    Estimate result;const double half=(last-first)*.5;
    for(std::size_t i=0;i<rule.nodes.size();++i) {
        const auto sample=function((first+last)*.5+half*rule.nodes[i]);
        const double weight=half*rule.weights[i];
        for(int k=0;k<(centroid?4:1);++k) {
            result.value[k]+=sample.value[k]*weight;
            result.error[k]+=sample.error[k]*std::abs(weight);
        }
    }
    return result;
}
template<class Function>
Estimate LineIntegral(const Function& function,double first,double last,bool centroid,int depth=0) {
    static const Quadrature coarse(8),fine(12);
    const auto a=LineRule(function,first,last,coarse,centroid);
    auto b=LineRule(function,first,last,fine,centroid);double difference=0;
    for(int k=0;k<(centroid?4:1);++k)
        difference=std::max(difference,std::abs(a.value[k]-b.value[k])/std::max(1.,std::abs(b.value[k])));
    if(difference<1e-10||depth==8) {
        for(int k=0;k<(centroid?4:1);++k)b.error[k]+=std::abs(a.value[k]-b.value[k]);
        return b;
    }
    const double mid=(first+last)*.5;
    auto left=LineIntegral(function,first,mid,centroid,depth+1);
    const auto right=LineIntegral(function,mid,last,centroid,depth+1);
    for(int k=0;k<4;++k){left.value[k]+=right.value[k];left.error[k]+=right.error[k];}
    return left;
}
// Green's theorem integrates the actual UV wires (including concave outlines
// and clockwise holes), not their bounding rectangle. The primitive is split
// at native U spans; nested quadrature errors are propagated to the final gate.
Estimate TrimmedFlux(TopoDS_Face face,BRepAdaptor_Surface& surface,const gp_Pnt& origin,bool centroid) {
    face.Orientation(TopAbs_FORWARD);
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    const int count=surface.NbUIntervals(GeomAbs_CN),vCount=surface.NbVIntervals(GeomAbs_CN);
    if(count<=0||vCount<=0||count>50000/vCount)throw Standard_Failure("Too many trim integration intervals");
    NCollection_Array1<double> us(1,count+1),vs(1,vCount+1);
    surface.UIntervals(us,GeomAbs_CN);surface.VIntervals(vs,GeomAbs_CN);
    const auto primitive=[&](double u,double v) {
        Estimate result;const double first=std::min(u0,u),last=std::max(u0,u);
        std::vector<double> cuts{first};
        for(int i=1;i<=count+1;++i)if(us(i)>first&&us(i)<last)cuts.push_back(us(i));
        cuts.push_back(last);
        for(std::size_t i=1;i<cuts.size();++i) {
            const auto part=LineIntegral([&](double x){return Estimate{PointFlux(surface,origin,x,v,centroid),{}};},cuts[i-1],cuts[i],centroid);
            for(int k=0;k<(centroid?4:1);++k){result.value[k]+=(u<u0?-1:1)*part.value[k];result.error[k]+=part.error[k];}
        }
        return result;
    };
    Estimate result;
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());
        if(edge.Orientation()==TopAbs_INTERNAL||edge.Orientation()==TopAbs_EXTERNAL)continue;
        BRepAdaptor_Curve2d curve(edge,face);
        const int intervals=curve.NbIntervals(GeomAbs_CN);
        if(intervals<=0||intervals>50000)throw Standard_Failure("Too many trim boundary intervals");
        NCollection_Array1<double> ts(1,intervals+1);curve.Intervals(ts,GeomAbs_CN);
        std::vector<double> cuts;
        for(int i=1;i<=intervals+1;++i)cuts.push_back(ts(i));
        const occ::handle<Geom2d_Curve> bounded=new Geom2d_TrimmedCurve(curve.Curve(),ts(1),ts(intervals+1));
        const auto cross=[&](const NCollection_Array1<double>& knots,bool vertical) {
            for(int i=2;i<knots.Length();++i) {
                const occ::handle<Geom2d_Curve> line=new Geom2d_Line(
                    vertical?gp_Pnt2d(knots(i),0):gp_Pnt2d(0,knots(i)),vertical?gp_Dir2d(0,1):gp_Dir2d(1,0));
                Geom2dAPI_InterCurveCurve hit(bounded,line,1e-12);
                if(!hit.Intersector().IsDone())throw Standard_Failure("Trim integration span intersection failed");
                for(int j=1;j<=hit.NbPoints();++j) {
                    const double t=hit.Intersector().Point(j).ParamOnFirst();
                    if(t>ts(1)&&t<ts(intervals+1))cuts.push_back(t);
                }
            }
        };
        // A small feature between quadrature nodes must not disappear simply
        // because both rules miss the same narrow surface knot interval.
        cross(us,true);cross(vs,false);
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        if(cuts.size()>50000)throw Standard_Failure("Too many trim integration crossings");
        for(std::size_t i=1;i<cuts.size();++i) {
            const auto part=LineIntegral([&](double t){
                gp_Pnt2d p;gp_Vec2d derivative;curve.D1(t,p,derivative);
                auto value=primitive(p.X(),p.Y());
                for(int k=0;k<(centroid?4:1);++k){value.value[k]*=derivative.Y();value.error[k]*=std::abs(derivative.Y());}
                return value;
            },cuts[i-1],cuts[i],centroid);
            for(int k=0;k<(centroid?4:1);++k) {
                result.value[k]+=(edge.Orientation()==TopAbs_REVERSED?-1:1)*part.value[k];
                result.error[k]+=part.error[k];
            }
        }
    }
    return result;
}
class IntegratedProperties final:public GProp_GProps {
public:
    IntegratedProperties(const Integral& value,const gp_Pnt& origin,bool centroid) {
        dim=value[0];loc=origin;g=centroid&&std::abs(dim)>1e-20?
            gp_Pnt(value[1]/dim,value[2]/dim,value[3]/dim):gp_Pnt(0,0,0);
        inertia=gp_Mat(0,0,0,0,0,0,0,0,0);
    }
};
// Offset surfaces hide the spline spans from OCCT's GK integration. Integrate
// the exact native surface per continuity interval, without replacing geometry
// by a mesh or an approximated surface. Planar trims retain their exact area
// and centroid; other trims integrate their native oriented UV wires.
std::optional<GProp_GProps> OffsetVolume(const TopoDS_Shape& shape,bool centroid) {
    bool offset=false;
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
        BRepAdaptor_Surface s(TopoDS::Face(e.Current()));
        offset=offset||s.GetType()==GeomAbs_OffsetSurface||s.NbUIntervals(GeomAbs_CN)>2||s.NbVIntervals(GeomAbs_CN)>2;
    }
    if(!offset)return std::nullopt;
    gp_Pnt origin;
    for(TopExp_Explorer e(shape,TopAbs_VERTEX);e.More();e.Next()) {origin=BRep_Tool::Pnt(TopoDS::Vertex(e.Current()));break;}
    // Validate every domain before accumulating anything.
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
        const auto face=TopoDS::Face(e.Current());
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(!std::isfinite(u0+u1+v0+v1))return std::nullopt;
        const BRepAdaptor_Surface surface(face,true);
        // Keep the established periodic-solid integration route. A seam can
        // have two pcurves on one edge; this new path handles open panel domains.
        if(!Rectangle(face,u0,u1,v0,v1)&&(surface.IsUClosed()||surface.IsVClosed()))return std::nullopt;
    }
    Integral sum{},error{};
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
        const auto face=TopoDS::Face(e.Current());BRepAdaptor_Surface s(face,true);
        const double sign=face.Orientation()==TopAbs_REVERSED?-1:1;
        if(s.GetType()==GeomAbs_Plane) {
            GProp_GProps area;
            const double relative=BRepGProp::SurfaceProperties(face,area,1e-10);
            if(!Accurate(relative,area))throw Standard_Failure("Planar trim area integration failed");
            const gp_Vec r(origin,area.CentreOfMass());
            const double flux=sign*area.Mass()*r.Dot(gp_Vec(s.Plane().Axis().Direction()));
            sum[0]+=flux/3;
            if(centroid){sum[1]+=r.X()*flux/4;sum[2]+=r.Y()*flux/4;sum[3]+=r.Z()*flux/4;}
            error[0]+=std::abs(flux/3)*relative;
            if(centroid){error[1]+=std::abs(r.X()*flux/4)*relative;error[2]+=std::abs(r.Y()*flux/4)*relative;error[3]+=std::abs(r.Z()*flux/4)*relative;}
            continue;
        }
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(!Rectangle(face,u0,u1,v0,v1)) {
            const auto trimmed=TrimmedFlux(face,s,origin,centroid);
            for(int k=0;k<(centroid?4:1);++k){sum[k]+=sign*trimmed.value[k];error[k]+=trimmed.error[k];}
            continue;
        }
        const int nu=s.NbUIntervals(GeomAbs_CN),nv=s.NbVIntervals(GeomAbs_CN);
        if(nu<=0||nv<=0||nu>50000/nv)throw Standard_Failure("Too many surface integration intervals");
        NCollection_Array1<double> us(1,nu+1),vs(1,nv+1);s.UIntervals(us,GeomAbs_CN);s.VIntervals(vs,GeomAbs_CN);
        for(int u=1;u<=nu;++u)for(int v=1;v<=nv;++v) {
            const auto value=Integrate(s,origin,us(u),us(u+1),vs(v),vs(v+1),centroid,0,error);
            for(int k=0;k<4;++k)sum[k]+=sign*value[k];
        }
    }
    for(int k=0;k<(centroid?4:1);++k)
        if(!std::isfinite(sum[k])||!std::isfinite(error[k])||error[k]>std::max(1e-10,std::abs(sum[k])*1e-8))
            throw Standard_Failure("Offset volume integration did not reach relative accuracy 1e-8");
    return IntegratedProperties(sum,origin,centroid);
}
}
void VolumeProperties(const TopoDS_Shape& shape,GProp_GProps& properties,bool centroid) {
    if(const auto offset=OffsetVolume(shape,centroid)){properties=*offset;return;}
    GProp_GProps measured;
    bool composite=false;
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More()&&!composite;e.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(e.Current()));
        composite=surface.NbUIntervals(GeomAbs_C3)>1||surface.NbVIntervals(GeomAbs_C3)>1;
    }
    if(composite) {
        // Avoid first integrating blindly across known spline continuity breaks.
        // This is the same exact analysis subdivision as the fallback below.
        const auto divided=DividedCopy(shape);
        if(!divided.IsNull()&&BRepCheck_Analyzer(divided).IsValid()) {
            const double error=BRepGProp::VolumePropertiesGK(divided,measured,1e-10,false,true,centroid,false);
            if(Accurate(error,measured)){properties=measured;return;}
            measured=GProp_GProps();
        }
    }
    double error=BRepGProp::VolumePropertiesGK(shape,measured,1e-10,false,true,centroid,false);
    if(!Accurate(error,measured)) {
        GProp_GProps adaptive;
        const double adaptiveError=BRepGProp::VolumeProperties(shape,adaptive,1e-10);
        if(Accurate(adaptiveError,adaptive)&&std::isfinite(measured.Mass())
            &&std::abs(adaptive.Mass()-measured.Mass())<=std::max(1e-10,std::abs(adaptive.Mass())*1e-8)) {
            properties=adaptive;return;
        }
        // IsUseSpan only handles BSpline surfaces, not a revolution whose
        // meridian is a spline. Split an analysis copy at C3 breaks so those
        // integration intervals are respected too. Stored geometry is untouched.
        const auto divided=DividedCopy(shape);
        if(divided.IsNull()||!BRepCheck_Analyzer(divided).IsValid()) {
            std::ostringstream message;message<<std::setprecision(17)
                <<"Volume integration subdivision is invalid; source_valid="<<BRepCheck_Analyzer(shape).IsValid()
                <<" GK="<<measured.Mass()<<" error="<<error
                <<" adaptive="<<adaptive.Mass()<<" error="<<adaptiveError;
            throw Standard_Failure(message.str().c_str());
        }
        measured=GProp_GProps();
        error=BRepGProp::VolumePropertiesGK(divided,measured,1e-10,false,true,centroid,false);
    }
    if(!Accurate(error,measured))
        throw Standard_Failure("Volume integration did not reach relative accuracy 1e-8");
    properties=measured;
}
}
#endif
