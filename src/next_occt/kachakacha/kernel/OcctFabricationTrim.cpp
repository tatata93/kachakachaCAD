#include "kachakacha/kernel/OcctFabricationTrim.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <gp_Dir2d.hxx>
#include <algorithm>
#include <array>
#include <map>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

namespace kachakacha::v2::kernel::detail {
namespace {
struct SliceChart {
    std::vector<occ::handle<Geom2d_Curve>> edges;
    bool exchange=false;
    double start=0,end=1;
    mutable std::mutex mutex;
    mutable std::map<double,std::optional<std::array<double,2>>> cache;

    std::optional<std::array<double,2>> Bounds(double fraction) const
    {
        const double x=start+(end-start)*std::clamp(fraction,0.0,1.0);
        std::lock_guard<std::mutex> lock(mutex);
        if(const auto found=cache.find(x);found!=cache.end())return found->second;
        const occ::handle<Geom2d_Line> line=new Geom2d_Line(exchange ? gp_Pnt2d(0,x) : gp_Pnt2d(x,0),
            exchange ? gp_Dir2d(1,0) : gp_Dir2d(0,1));
        std::vector<double> values;
        const auto add=[&](const gp_Pnt2d& p){values.push_back(exchange ? p.X() : p.Y());};
        for(const auto& edge:edges) {
            Geom2dAPI_InterCurveCurve intersections(edge,line,1e-9);
            for(int i=1;i<=intersections.NbPoints();++i)add(intersections.Point(i));
            for(int i=1;i<=intersections.NbSegments();++i) {
                occ::handle<Geom2d_Curve> a,b;intersections.Segment(i,a,b);
                if(!a.IsNull()) {add(a->Value(a->FirstParameter()));add(a->Value(a->LastParameter()));}
            }
        }
        std::sort(values.begin(),values.end());
        values.erase(std::unique(values.begin(),values.end(),[](double a,double b){return std::abs(a-b)<1e-7;}),values.end());
        std::optional<std::array<double,2>> result;
        if(values.size()==2 || (values.size()==1 && (fraction==0 || fraction==1)))
            result=std::array<double,2>{values.front(),values.back()};
        cache.emplace(x,result);return result;
    }
    gp_Pnt2d Point(double across,double along) const
    {
        const auto limits=Bounds(along);
        if(!limits)throw Standard_Failure("Non-monotone fabrication trim");
        const double x=start+(end-start)*std::clamp(along,0.0,1.0);
        const double y=(*limits)[0]+((*limits)[1]-(*limits)[0])*std::clamp(across,0.0,1.0);
        return exchange ? gp_Pnt2d(y,x) : gp_Pnt2d(x,y);
    }
};
bool ValidChart(const TopoDS_Face& face,const SliceChart& chart)
{
    for(int i=0;i<=256;++i) {
        const double t=i/256.0;
        if(!chart.Bounds(t))return false;
        for(double across:{0.0,.25,.5,.75,1.0}) {
            BRepClass_FaceClassifier classified(face,chart.Point(across,t),1e-6);
            if(classified.State()==TopAbs_OUT)return false;
        }
    }
    return true;
}
}
base::Result<fabrication::SurfaceEvaluator> TrimmedFabricationEvaluator(const TopoDS_Face& face)
{
    using Out=base::Result<fabrication::SurfaceEvaluator>;
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    const auto surface=BRep_Tool::Surface(face);
    for(bool exchange:{false,true}) {
        auto chart=std::make_shared<SliceChart>();chart->exchange=exchange;
        chart->start=exchange ? v0 : u0;chart->end=exchange ? v1 : u1;
        for(TopExp_Explorer edges(face,TopAbs_EDGE);edges.More();edges.Next()) {
            double first,last;
            const auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(edges.Current()),face,first,last);
            if(!curve.IsNull())chart->edges.push_back(new Geom2d_TrimmedCurve(curve,first,last));
        }
        if(chart->edges.empty() || !ValidChart(face,*chart))continue;
        fabrication::SurfaceEvaluator evaluate=[chart,surface](double u,double v) {
            try {
                const auto uv=chart->Point(u,v);
                const auto p=surface->Value(uv.X(),uv.Y());
                return geometry::Vector3{p.X(),p.Y(),p.Z()};
            } catch(const Standard_Failure&) {
                const double bad=std::numeric_limits<double>::quiet_NaN();
                return geometry::Vector3{bad,bad,bad};
            }
        };
        return Out::Success(std::move(evaluate));
    }
    return Out::Failure(base::MakeError("FAB-B006",
        "この面の外形は1本の帯の範囲へ対応できません。",
        "外形を横切る近似は作りません。分岐する区画や開口を境界で分けてください。"));
}
}
#endif
