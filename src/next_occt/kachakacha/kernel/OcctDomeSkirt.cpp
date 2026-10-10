#include "kachakacha/kernel/OcctDomePatch.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <Geom_BSplineCurve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <NCollection_HArray1.hxx>
#include <functional>
#include <algorithm>
#include <cmath>
#include <vector>

namespace kachakacha::v2::kernel::detail {
namespace {
bool Monotone(const Geom_BSplineSurface& surface, const gp_XYZ& origin, const gp_XYZ& along)
{
    double u0,u1,v0,v1; surface.Bounds(u0,u1,v0,v1);
    for(double u:{u0,u1}) {
        double previous=-1e-8;
        for(int knot=1;knot<surface.NbVKnots();++knot)for(int j=0;j<=32;++j) {
            const double v=surface.VKnot(knot)+(surface.VKnot(knot+1)-surface.VKnot(knot))*j/32;
            const double value=(surface.Value(u,v).XYZ()-origin).Dot(along);
            if(value+1e-8<previous)return false;
            previous=value;
        }
    }
    return true;
}

occ::handle<Geom_BSplineCurve> InterpolateWidth(const std::vector<double>& params,
    const std::vector<double>& values, double firstDerivative, double lastDerivative)
{
    const int count=static_cast<int>(params.size());
    const occ::handle<NCollection_HArray1<gp_Pnt>> points=new NCollection_HArray1<gp_Pnt>(1,count);
    const occ::handle<NCollection_HArray1<double>> parameters=new NCollection_HArray1<double>(1,count);
    for(int i=0;i<count;++i) { points->SetValue(i+1,gp_Pnt(values[i],params[i],0));parameters->SetValue(i+1,params[i]); }
    // Include the parameter in Y so repeated widths remain distinct interpolation points.
    GeomAPI_Interpolate fit(points,parameters,false,1e-12);
    fit.Load(gp_Vec(firstDerivative,1,0),gp_Vec(lastDerivative,1,0),false);fit.Perform();
    return fit.IsDone() ? fit.Curve() : occ::handle<Geom_BSplineCurve>{};
}

occ::handle<Geom_BSplineCurve> FitSpan(double first,double last,
    const std::function<double(double)>& width)
{
    std::vector<double> parameters;
    for(int k=0;k<=4;++k)parameters.push_back(first+(last-first)*k/4);
    const double step=(last-first)*1e-5;
    const double firstDerivative=(-3*width(first)+4*width(first+step)-width(first+2*step))/(2*step);
    const double lastDerivative=(3*width(last)-4*width(last-step)+width(last-2*step))/(2*step);
    for(int iteration=0;iteration<16 && parameters.size()<512;++iteration) {
        std::vector<double> values;for(double v:parameters)values.push_back(width(v));
        const auto curve=InterpolateWidth(parameters,values,firstDerivative,lastDerivative);
        if(curve.IsNull())return {};
        std::vector<double> insert;
        for(std::size_t k=1;k<parameters.size();++k) {
            bool bad=false;
            for(int j=1;j<=7;++j) {
                const double v=parameters[k-1]+(parameters[k]-parameters[k-1])*j/8;
                bad=bad || std::abs(curve->Value(v).X()-width(v))>1e-7;
            }
            if(bad)insert.push_back((parameters[k-1]+parameters[k])*0.5);
        }
        if(insert.empty())return curve;
        parameters.insert(parameters.end(),insert.begin(),insert.end());
        std::sort(parameters.begin(),parameters.end());
    }
    return {};
}
}

occ::handle<Geom_BSplineCurve> DomeSkirtWidth(const Geom_BSplineSurface& base,
    const gp_XYZ& along,const gp_XYZ& across,bool& failed)
{
    failed=false;
    double u0,u1,v0,v1;base.Bounds(u0,u1,v0,v1);
    const auto origin=base.Value(u0,v0).XYZ();
    if(!Monotone(base,origin,along))return {};
    const auto width=[&](double v) {
        const double target=(base.Value(u0,v).XYZ()-origin).Dot(along);
        double low=v0,high=v1;
        for(int k=0;k<60;++k) {
            const double mid=(low+high)*0.5;
            if((base.Value(u1,mid).XYZ()-origin).Dot(along)<target)low=mid;else high=mid;
        }
        return (base.Value(u1,(low+high)*0.5).XYZ()-origin).Dot(across);
    };
    std::vector<double> breaks;
    for(int k=1;k<=base.NbVKnots();++k)breaks.push_back(base.VKnot(k));
    // Keep changes of derivative at real curve joins out of smooth interpolation spans.
    for(int k=2;k<base.NbVKnots();++k) {
        const double target=(base.Value(u1,base.VKnot(k)).XYZ()-origin).Dot(along);
        double low=v0,high=v1;
        for(int j=0;j<60;++j) {
            const double mid=(low+high)*0.5;
            if((base.Value(u0,mid).XYZ()-origin).Dot(along)<target)low=mid;else high=mid;
        }
        const double v=(low+high)*0.5;if(v>v0+1e-10 && v<v1-1e-10)breaks.push_back(v);
    }
    std::sort(breaks.begin(),breaks.end());
    breaks.erase(std::unique(breaks.begin(),breaks.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),breaks.end());
    GeomConvert_CompCurveToBSplineCurve concat;
    for(std::size_t k=1;k<breaks.size();++k) {
        const auto curve=FitSpan(breaks[k-1],breaks[k],width);
        if(curve.IsNull() || !concat.Add(curve,1e-9,true,false,3)) { failed=true;return {}; }
    }
    return concat.BSplineCurve();
}
}
#endif
