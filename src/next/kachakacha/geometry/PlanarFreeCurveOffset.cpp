#include "kachakacha/geometry/PlanarOffsetInternal.h"
#include "kachakacha/geometry/CurveFit.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace kachakacha::v2::geometry::detail {
namespace {
using One = base::Result<CurveSegment>;
struct Jet { Vector3 p, d1, d2, d3; };
struct Polynomial {
    std::vector<std::array<Vector3,4>> spans;
    double first = 0, last = 1;
    Polynomial(const CurveSegment& curve, Vector3 origin, double a = 0, double b = 1) : first(a), last(b) {
        if (curve.Kind() == CurveKind::CubicBezier) {
            const auto& p = curve.ControlPoints(); spans.push_back({p[0],p[1],p[2],p[3]});
        } else spans = UniformBSplineBezierSpans(curve.ControlPoints());
        for (auto& span : spans) for (auto& point : span) point = point-origin;
    }
    Jet At(double t) const {
        const double source = std::clamp(first+(last-first)*t,0.,1.);
        const double scaled = source*spans.size();
        const auto index = std::min(static_cast<std::size_t>(scaled),spans.size()-1);
        const double u = scaled-index, v = 1-u, scale = spans.size()*(last-first);
        const auto& p = spans[index];
        return {p[0]*(v*v*v)+p[1]*(3*v*v*u)+p[2]*(3*v*u*u)+p[3]*(u*u*u),
            ((p[1]-p[0])*(3*v*v)+(p[2]-p[1])*(6*v*u)+(p[3]-p[2])*(3*u*u))*scale,
            ((p[2]-p[1]*2+p[0])*(6*v)+(p[3]-p[2]*2+p[1])*(6*u))*(scale*scale),
            (p[3]-p[2]*3+p[1]*3-p[0])*(6*scale*scale*scale)};
    }
};
struct Offset {
    Polynomial source;
    Vector3 normal;
    double distance;
    Jet At(double t) const {
        const auto p = source.At(t);
        const auto w = Cross(normal,p.d1), derivative = Cross(normal,p.d2);
        const double speed = w.Length();
        if (!(speed > 0)) return {{std::numeric_limits<double>::quiet_NaN(),0,0},{},{},{}};
        return {p.p+w*(distance/speed),
            p.d1+(derivative*(1/speed)-w*(Dot(w,derivative)/(speed*speed*speed)))*distance,{}, {}};
    }
};
using Measurement = OffsetStatistics;
One Failed(const char* text) {
    return One::Failure(base::MakeError("GEO-E004",text,
        "距離を小さくするか、加工平面と元の曲線を確認してください。元の線は変更していません。"));
}
bool RegularSamples(const Offset& offset) {
    for (std::size_t i=0;i<offset.source.spans.size()*4+1;++i) {
        const double t = double(i)/(offset.source.spans.size()*4);
        const auto q = offset.At(t), p = offset.source.At(t);
        if (!q.p.IsFinite() || !q.d1.IsFinite() || !(Dot(q.d1,p.d1)>0)) return false;
    }
    return true;
}
// The existing core spline uses uniform cubic spans. Clamped end derivatives
// and a tridiagonal interpolation solve keep smooth joins while allowing many
// poles in linear time when the ideal offset has curvature jumps at input knots.
base::Result<CurveSegment> Interpolate(const Offset& offset, std::size_t spans) {
    std::vector<Vector3> rhs(spans+1);
    std::vector<double> diagonal(spans+1,4.);
    diagonal.front()=2;diagonal.back()=2;
    const auto first=offset.At(0),last=offset.At(1);
    rhs.front()=first.p*3+first.d1*(1./spans);
    rhs.back()=last.p*3-last.d1*(1./spans);
    for(std::size_t i=1;i<spans;++i)rhs[i]=offset.At(double(i)/spans).p*6;
    for(std::size_t i=1;i<=spans;++i){
        const double factor=1/diagonal[i-1];diagonal[i]-=factor;rhs[i]=rhs[i]-rhs[i-1]*factor;
    }
    std::vector<Vector3> control(spans+3);
    control[spans+1]=rhs[spans]*(1/diagonal[spans]);
    for(std::size_t i=spans;i-->0;)control[i+1]=(rhs[i]-control[i+2])*(1/diagonal[i]);
    control[0]=control[2]-first.d1*(2./spans);
    control[spans+2]=control[spans]+last.d1*(2./spans);
    return CurveSegment::MakeCubicBSpline(std::move(control));
}
// On a knot-free interval, p'' is affine and p''' is constant. The unit-normal
// derivative bounds and the linear-interpolation remainder bound the entire
// positional error, including locations not used by the fit or RMS samples.
bool CheckInterval(const Offset& offset, const Polynomial& fit, double a, double b,
    double tolerance, Measurement& measured) {
    struct Interval { double a,b; int depth; };
    std::vector<Interval> pending{{a,b,0}};
    std::size_t inspected=0;
    while (!pending.empty()) {
        if(++inspected>262144 || measured.samples>1048576)return false;
        const auto part = pending.back(); pending.pop_back();
        const double h = part.b-part.a, t = (part.a+part.b)*.5;
        const auto p = offset.source.At(t), q = offset.At(t);
        const double p2 = std::max(offset.source.At(part.a).d2.Length(),offset.source.At(part.b).d2.Length());
        const double minimumSpeed = Cross(offset.normal,p.d1).Length()-h*p2*.5;
        bool regular = minimumSpeed>0;
        double bound = std::numeric_limits<double>::infinity();
        const double left = Distance(offset.At(part.a).p,fit.At(part.a).p);
        const double right = Distance(offset.At(part.b).p,fit.At(part.b).p);
        const double middle = Distance(q.p,fit.At(t).p);
        if (!q.p.IsFinite() || !q.d1.IsFinite() || !(Dot(q.d1,p.d1)>0)) return false;
        if (regular) {
            const double p1 = p.d1.Length()+h*p2*.5;
            const double q1 = p1+std::abs(offset.distance)*p2/minimumSpeed;
            const double q2 = p2+std::abs(offset.distance)*(p.d3.Length()/minimumSpeed+3*p2*p2/(minimumSpeed*minimumSpeed));
            regular = Dot(q.d1,p.d1)-h*.5*(q2*p1+q1*p2)>0;
            const double f2 = std::max(fit.At(part.a).d2.Length(),fit.At(part.b).d2.Length());
            bound = std::max(left,right)+(q2+f2)*h*h/8;
        }
        if (regular && bound<=tolerance) {
            measured.bound = std::max(measured.bound,bound);
            measured.squares += left*left+right*right+middle*middle; measured.samples += 3;
        } else {
            if (part.depth>=24 || middle>tolerance || measured.samples>1048576) return false;
            pending.push_back({t,part.b,part.depth+1}); pending.push_back({part.a,t,part.depth+1});
        }
    }
    return true;
}
bool Certify(const Offset& offset, const CurveSegment& curve, double tolerance, Measurement& measured) {
    const Polynomial fit(curve,{});
    std::vector<double> knots{0,1};
    for (std::size_t i=1;i<fit.spans.size();++i) knots.push_back(double(i)/fit.spans.size());
    const auto& input = offset.source;
    for (std::size_t i=1;i<input.spans.size();++i) {
        const double t = (double(i)/input.spans.size()-input.first)/(input.last-input.first);
        if (t>0 && t<1) knots.push_back(t);
    }
    std::sort(knots.begin(),knots.end());
    knots.erase(std::unique(knots.begin(),knots.end()),knots.end());
    for (std::size_t i=1;i<knots.size();++i)
        if (!CheckInterval(offset,fit,knots[i-1],knots[i],tolerance,measured)) return false;
    return true;
}
}

base::Diagnostic OffsetApproximationDiagnostic(const OffsetStatistics& measured, const GeometryTolerance& tolerance) {
    std::ostringstream text; text<<std::setprecision(7)<<"自由曲線の近似：偏差上限 "<<measured.bound
        <<" mm / RMS（標本） "<<std::sqrt(measured.squares/std::max<std::size_t>(1,measured.samples))
        <<" mm（許容 "<<tolerance.modelLinearMm<<" mm）";
    return base::MakeInformation("GEO-I001",text.str());
}
One OffsetFreeCurve(const CurveSegment& curve, Vector3 normal, double distance,
    const GeometryTolerance& tolerance, double first, double last, OffsetStatistics* statistics) {
    normal = Normalized(normal);
    if (!std::isfinite(distance) || normal==Vector3{} || !(first>=0 && last<=1 && first<last)
        || !(tolerance.modelLinearMm>0)) return Failed("オフセット距離・平面・曲線の範囲を確認してください。");
    if (!CurveLiesInPlane(curve,curve.StartPoint(),normal,tolerance.modelLinearMm))
        return One::Failure(base::MakeError("GEO-E005","曲線が加工平面上にありません。","加工平面を確認してください。"));
    if (distance==0 && first==0 && last==1) return One::Success(curve);
    const auto origin = curve.StartPoint();
    const Offset offset{Polynomial(curve,origin,first,last),normal,distance};
    if (!RegularSamples(offset)) return Failed("この距離では曲線がつぶれるか、向きが反転します。");
    double magnitude=origin.Length();
    for(const auto& point:curve.ControlPoints())magnitude=std::max(magnitude,point.Length());
    const double rounding = std::numeric_limits<double>::epsilon()*128*(1+magnitude+std::abs(distance));
    const double budget = tolerance.modelLinearMm-rounding;
    if (!(budget>0)) return Failed("この座標と許容差では曲線の精度を確認できません。");
    std::size_t spans=std::clamp<std::size_t>(offset.source.spans.size()*4,8,256);
    for(;;){
        const auto made=Interpolate(offset,spans);
        if(!made.HasValue())return One::Failure(made.Diagnostics());
        Measurement measured;
        if(Certify(offset,made.Value(),budget,measured)){
            measured.bound+=rounding;if(statistics)*statistics=measured;
            return One::Success(TranslateCurve(made.Value(),origin),
                {OffsetApproximationDiagnostic(measured,tolerance)});
        }
        if(spans==4096)break;
        spans=std::min<std::size_t>(4096,spans*2);
    }
    return Failed("この距離では、曲線のオフセットを許容差内で確認できません。");
}
}
