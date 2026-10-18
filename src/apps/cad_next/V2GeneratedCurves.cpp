#include "V2GeneratedCurves.h"
#include "kachakacha/geometry/CurveFit.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
using namespace kachakacha::v2::geometry;
constexpr double eps=1e-9;
constexpr double kPi=3.14159265358979323846;
std::optional<CurveSegment> Join(const CurveSegment& a,const CurveSegment& b) {
    if(Distance(a.EndPoint(),b.StartPoint())>eps)return {};
    if(a.Kind()==CurveKind::Line&&b.Kind()==CurveKind::Line) {
        const auto u=a.EndPoint()-a.StartPoint(),v=b.EndPoint()-b.StartPoint();
        if(Dot(u,v)<=0||Cross(u,v).Length()>eps*std::min(u.Length(),v.Length()))return {};
        const auto made=CurveSegment::MakeLine(a.StartPoint(),b.EndPoint());
        if(made.HasValue())return made.Value();
    }
    if(a.Kind()==CurveKind::CircularArc&&b.Kind()==CurveKind::CircularArc
        &&Distance(a.Center(),b.Center())<eps&&std::abs(a.Radius()-b.Radius())<eps
        &&Distance(a.Normal(),b.Normal())<eps&&Distance(a.ReferenceDirection(),b.ReferenceDirection())<eps
        &&a.SweepAngleRad()*b.SweepAngleRad()>0) {
        const double sweep=a.SweepAngleRad()+b.SweepAngleRad();
        if(std::abs(sweep)>2*kPi+eps)return {};
        const auto made=std::abs(std::abs(sweep)-2*kPi)<1e-12
            ?CurveSegment::MakeCircle(a.Center(),a.Normal()*(sweep>0?1:-1),Normalized(a.StartPoint()-a.Center()),a.Radius())
            :CurveSegment::MakeCircularArc(a.Center(),a.Normal(),a.ReferenceDirection(),a.Radius(),a.StartAngleRad(),sweep);
        if(made.HasValue())return made.Value();
    }
    return {};
}
// Bernstein control-point comparison bounds the whole span, not just samples.
std::optional<CurveSegment> Spline(const std::vector<CurveSegment>& curves,std::size_t first,std::size_t end) {
    const auto& p=curves[first].ControlPoints();
    std::vector<Vector3> q{p[0]*6-p[1]*7+p[2]*2,p[1]*2-p[2],p[2]*2-p[1]};
    for(std::size_t i=first;i<end;++i){const auto& c=curves[i].ControlPoints();q.push_back(c[3]*6+c[1]*2-c[2]*7);}
    const auto spans=UniformBSplineBezierSpans(q);
    if(spans.size()!=end-first)return {};
    for(std::size_t i=0;i<spans.size();++i)for(std::size_t j=0;j<4;++j)
        if(Distance(spans[i][j],curves[first+i].ControlPoints()[j])>eps)return {};
    const auto made=CurveSegment::MakeCubicBSpline(std::move(q));
    return made.HasValue()?std::optional<CurveSegment>{made.Value()}:std::nullopt;
}
}
std::vector<kachakacha::v2::geometry::CurveSegment> V2CompactGeneratedCurves(const std::vector<CurveSegment>& curves) {
    std::vector<CurveSegment> result;
    for(std::size_t i=0;i<curves.size();) {
        if(curves[i].Kind()==CurveKind::CubicBezier) {
            std::size_t end=i+1;
            while(end<curves.size()&&curves[end].Kind()==CurveKind::CubicBezier&&end-i<128)++end;
            std::size_t low=1,high=end-i;
            while(low<high){const auto count=(low+high+1)/2;if(Spline(curves,i,i+count))low=count;else high=count-1;}
            if(low>1)if(const auto spline=Spline(curves,i,i+low)){result.push_back(*spline);i+=low;continue;}
        }
        if(!result.empty())if(const auto joined=Join(result.back(),curves[i])){result.back()=*joined;++i;continue;}
        result.push_back(curves[i++]);
    }
    return result;
}
