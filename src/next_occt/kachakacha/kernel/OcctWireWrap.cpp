#include "kachakacha/kernel/OcctWireWrap.h"
#include <cmath>
#include <algorithm>
#include <functional>
#include <stdexcept>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctBoundaryCurves.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
namespace kachakacha::v2::kernel::detail {
using geometry::Vector3;
using geometry::CurveSegment;
namespace {
struct Mapper {
    BRepAdaptor_Surface surface;
    WireProjectionOptions options;
    double u0=0,v0=0;
    Vector3 u,v,n;
    Mapper(const TopoDS_Face& face,const WireProjectionOptions& choice):surface(face),options(choice) {
        u=geometry::Normalized(choice.sourceU);v=geometry::Normalized(choice.sourceV-u*geometry::Dot(choice.sourceV,u));
        n=geometry::Cross(u,v);
        if(n.Length()<0.9)throw std::runtime_error("元の作業平面の向きが決まりません。");
        GeomAPI_ProjectPointOnSurf pick(ToPoint(choice.targetPoint),BRep_Tool::Surface(face));
        if(pick.NbPoints()==0)throw std::runtime_error("貼付先の基準点がありません。");
        pick.LowerDistanceParameters(u0,v0);
    }
    Vector3 operator()(Vector3 p) const {
        const auto delta=p-options.sourceOrigin;
        if(std::abs(geometry::Dot(delta,n))>options.toleranceMm)throw std::runtime_error("等長巻き付けの元ワイヤーは、指定した作業平面上に置いてください。");
        const double x0=geometry::Dot(delta,u),y0=geometry::Dot(delta,v);
        const double c=std::cos(options.wrapRotationRad),s=std::sin(options.wrapRotationRad);
        const double x=c*x0-s*y0,y=s*x0+c*y0;
        if(surface.GetType()==GeomAbs_Plane)return FromPoint(surface.Value(u0+x,v0+y));
        if(surface.GetType()==GeomAbs_Cylinder)return FromPoint(surface.Value(u0+x/surface.Cylinder().Radius(),v0+y));
        const auto cone=surface.Cone();const double angle=cone.SemiAngle(),sa=std::sin(angle);
        // Unroll a cone to its sector. Its radial coordinate is the slant distance from the apex.
        const double l0=(cone.RefRadius()+v0*sa)/std::abs(sa);
        const double radial=l0+y,side=x;
        if(radial<=0)throw std::runtime_error("巻き付けが円錐の頂点を越えます。基準点を変えてください。");
        const double l=std::hypot(radial,side);
        const double du=std::atan2(side,radial)/std::abs(sa);
        return FromPoint(surface.Value(u0+du,(l*std::abs(sa)-cone.RefRadius())/sa));
    }
};
void Fit(const Mapper& map,const CurveSegment& source,double a,double b,int depth,
    std::vector<CurveSegment>& curves,double& error) {
    const auto value=[&](double t){return map(source.Evaluate(t));};
    const double h=std::min(1e-5,(b-a)*1e-3);
    const auto derivative=[&](double t){const double l=std::max(0.0,t-h),r=std::min(1.0,t+h);return(value(r)-value(l))*(1/(r-l));};
    const auto p=value(a),q=value(b);
    const auto curve=CurveSegment::MakeCubicBezier({p,p+derivative(a)*((b-a)/3),q-derivative(b)*((b-a)/3),q});
    if(!curve.HasValue())throw std::runtime_error("巻き付け曲線を生成できません。");
    double deviation=0;
    for(int k=1;k<32;++k)deviation=std::max(deviation,geometry::Distance(curve.Value().Evaluate(k/32.0),value(a+(b-a)*k/32)));
    if(deviation<=map.options.toleranceMm*0.2){curves.push_back(curve.Value());error=std::max(error,deviation);return;}
    if(depth>=18||curves.size()>4096)throw std::runtime_error("指定偏差内で巻き付けできません。許容偏差を見直してください。");
    Fit(map,source,a,(a+b)*0.5,depth+1,curves,error);Fit(map,source,(a+b)*0.5,b,depth+1,curves,error);
}
}
base::Result<WireProjectionResult> WrapNativeWires(const TopoDS_Face& face,
    const std::vector<WireProjectionInput>& inputs,const WireProjectionOptions& options) {
    BRepAdaptor_Surface surface(face);
    if(surface.GetType()!=GeomAbs_Plane&&surface.GetType()!=GeomAbs_Cylinder&&surface.GetType()!=GeomAbs_Cone)
        return base::Result<WireProjectionResult>::Failure(base::MakeError(kWrapNotDevelopable,
            "この複曲面は等長に巻き付けられません。","「図面から投影」か「3Dワイヤーを面に載せる」を選んでください。"));
    const Mapper mapper(face,options);WireProjectionResult result;
    for(const auto& input:inputs)for(const auto& source:input.curves) {
        std::vector<CurveSegment> curves;double error=0;Fit(mapper,source,0,1,0,curves,error);
        // Never merge across original input intervals. The additional error is
        // bounded against every fitted Bezier span; Fit's mapping estimate is
        // still sampled and is reported separately by the projection contract.
        error+=CompactBoundaryRun(curves,options.toleranceMm*.4);
        result.maxCurveDeviationMm=std::max(result.maxCurveDeviationMm,error);
        result.wires.push_back(std::move(curves));
    }
    return base::Result<WireProjectionResult>::Success(std::move(result));
}
}
#endif
