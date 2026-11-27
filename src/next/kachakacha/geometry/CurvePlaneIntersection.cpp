#include "kachakacha/geometry/CurveIntersection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kachakacha::v2::geometry {
namespace {
// Split a cubic at its derivative roots. Each remaining interval is monotone,
// so sign bracketing finds every crossing; extrema also retain touching roots.
std::vector<double> CubicRoots(const std::array<double,4>& c)
{
    const double scale = std::max({std::abs(c[0]),std::abs(c[1]),std::abs(c[2]),std::abs(c[3])});
    if (scale == 0) return {}; // Coplanar span: infinitely many roots.
    const double epsilon = scale * 64 * std::numeric_limits<double>::epsilon();
    std::vector<double> cuts{0,1};
    const auto cut = [&](double t) { if (t > 0 && t < 1) cuts.push_back(t); };
    const double a=3*c[3], b=2*c[2], d=c[1];
    if (std::abs(a) <= epsilon) {
        if (std::abs(b) > epsilon) cut(-d/b);
    } else {
        const double discriminant=b*b-4*a*d;
        if (discriminant >= 0) {
            const double q=-.5*(b+std::copysign(std::sqrt(discriminant),b));
            if (q != 0) { cut(q/a); cut(d/q); }
            else cut(-b/(2*a));
        }
    }
    std::sort(cuts.begin(),cuts.end());
    cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    const auto value=[&](long double t){ return ((static_cast<long double>(c[3])*t+c[2])*t+c[1])*t+c[0]; };
    std::vector<double> roots;
    std::vector<bool> crossingAtCut(cuts.size(),false);
    for (std::size_t i=1;i<cuts.size();++i) {
        double lo=cuts[i-1], hi=cuts[i];
        long double left=value(lo), right=value(hi);
        if (!((left<0 && right>0)||(left>0 && right<0))) continue;
        crossingAtCut[i-1]=crossingAtCut[i]=true;
        for (int n=0;n<64;++n) {
            const double mid=(lo+hi)*.5;
            if (mid==lo || mid==hi) break;
            const auto v=value(mid);
            if (v==0) { lo=hi=mid; break; }
            if ((v<0)==(left<0)) { lo=mid; left=v; } else hi=mid;
        }
        roots.push_back((lo+hi)*.5);
    }
    // Nearby crossings can straddle a very shallow extremum. Never collapse
    // those two finite roots into the extremum just because its height is tiny.
    for (std::size_t i=0;i<cuts.size();++i)
        if (!crossingAtCut[i] && std::abs(value(cuts[i]))<=epsilon) roots.push_back(cuts[i]);
    return roots;
}
}

std::vector<CurvePoint> IntersectCurveWithPlane(const CurveSegment& curve,
    const Vector3& origin, const Vector3& normal, const GeometryTolerance& tolerance)
{
    std::vector<CurvePoint> result;
    const double length=normal.Length();
    if (!origin.IsFinite() || !normal.IsFinite() || !(length>0)
        || !std::isfinite(length) || !(tolerance.modelLinearMm>0)) return result;
    const Vector3 unit=normal*(1.0/length);
    const double allowed=tolerance.modelLinearMm;
    if (CurveLiesInPlane(curve,origin,unit,allowed)) return result;
    const auto distance=[&](const Vector3& point){return Dot(point-origin,unit);};
    const auto add=[&](double t) {
        if (!std::isfinite(t) || t < -1e-13 || t > 1+1e-13) return;
        t=std::clamp(t,0.,1.);
        const auto point=curve.Evaluate(t);
        if (std::abs(distance(point)) <= allowed) result.push_back({t,point});
    };
    if (curve.Kind()==CurveKind::Line) {
        const double first=distance(curve.StartPoint()), last=distance(curve.EndPoint());
        if (first!=last) add(first/(first-last));
    } else if (curve.Kind()==CurveKind::Circle || curve.Kind()==CurveKind::CircularArc) {
        const double a=Dot(curve.ReferenceDirection(),unit)*curve.Radius();
        const double b=Dot(Cross(curve.Normal(),curve.ReferenceDirection()),unit)*curve.Radius();
        const double radius=std::hypot(a,b), offset=distance(curve.Center());
        const double arithmeticMargin=64*std::numeric_limits<double>::epsilon()*std::max(radius,std::abs(offset));
        if (radius>0 && std::abs(offset)<=radius+arithmeticMargin) {
            const double phase=std::atan2(b,a), angle=std::acos(std::clamp(-offset/radius,-1.,1.));
            const double start=curve.StartAngleRad(), sweep=curve.SweepAngleRad();
            for (double root:{phase-angle,phase+angle}) {
                // Keep the first occurrence of each geometric root in the finite
                // sweep, including reversed and multi-turn arcs, without extending it.
                const double turn=sweep>0 ? std::ceil((start-root)/(2*kPi)-1e-14)
                                          : std::floor((start-root)/(2*kPi)+1e-14);
                add((root+turn*2*kPi-start)/sweep);
            }
        }
    } else {
        const std::size_t spans=curve.Kind()==CurveKind::CubicBSpline ? curve.ControlPoints().size()-3 : 1;
        for (std::size_t span=0;span<spans;++span) {
            const double lo=static_cast<double>(span)/spans, hi=static_cast<double>(span+1)/spans;
            const double f0=distance(curve.Evaluate(lo)), f1=distance(curve.Evaluate(hi));
            const double d0=Dot(curve.FirstDerivative(lo),unit)*(hi-lo);
            const double d1=Dot(curve.FirstDerivative(hi),unit)*(hi-lo);
            const std::array<double,4> c{f0,d0,3*(f1-f0)-2*d0-d1,2*(f0-f1)+d0+d1};
            for (double t:CubicRoots(c)) add(lo+(hi-lo)*t);
        }
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.parameter<b.parameter;});
    result.erase(std::unique(result.begin(),result.end(),[](const auto& a,const auto& b){
        return std::abs(a.parameter-b.parameter)<=1e-12;
    }),result.end());
    return result;
}
} // namespace kachakacha::v2::geometry
