#include "kachakacha/geometry/PlanarOffsetInternal.h"
#include "kachakacha/geometry/CurveFit.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <cmath>

namespace kachakacha::v2::geometry::detail {
namespace {
using Out = base::Result<std::vector<CurveSegment>>;
bool Free(const CurveSegment& c) { return c.Kind()==CurveKind::CubicBezier || c.Kind()==CurveKind::CubicBSpline; }
Out Invalid(const char* detail) {
    return Out::Failure(base::MakeError("GEO-E004","この距離では輪郭をオフセットできません。",detail));
}
struct Cut {
    double first=0,last=1;
    std::optional<Vector3> start,end;
    std::vector<CurveSegment> before,after;
};
Vector3 Tangent(const CurveSegment& curve, bool end) {
    if (Free(curve)) {
        if (curve.Kind()==CurveKind::CubicBezier) {
            const auto& p=curve.ControlPoints();return Normalized(end?p[3]-p[2]:p[1]-p[0]);
        }
        const auto spans=UniformBSplineBezierSpans(curve.ControlPoints());
        const auto& p=end?spans.back():spans.front();return Normalized(end?p[3]-p[2]:p[1]-p[0]);
    }
    return Normalized(curve.FirstDerivative(end?1:0));
}
bool OuterMiter(const CurveSegment& a, const CurveSegment& b, Cut& ca, Cut& cb,
    Vector3 normal, const GeometryTolerance& tolerance) {
    const auto da=Tangent(a,true),db=Tangent(b,false),between=b.StartPoint()-a.EndPoint();
    const double denominator=Dot(Cross(da,db),normal);
    if (std::abs(denominator)<=tolerance.modelAngularRad) return false;
    const double u=Dot(Cross(between,db),normal)/denominator;
    const double v=Dot(Cross(between,da),normal)/denominator;
    if (u < -tolerance.modelLinearMm || v > tolerance.modelLinearMm) return false;
    const auto joint=(a.EndPoint()+da*u+b.StartPoint()+db*v)*.5;
    if (!joint.IsFinite()) return false;
    if (a.Kind()==CurveKind::Line) ca.end=joint;
    else if (Distance(a.EndPoint(),joint)>tolerance.modelLinearMm)
        ca.after.push_back(CurveSegment::MakeLine(a.EndPoint(),joint).Value());
    if (b.Kind()==CurveKind::Line) cb.start=joint;
    else if (Distance(joint,b.StartPoint())>tolerance.modelLinearMm)
        cb.before.push_back(CurveSegment::MakeLine(joint,b.StartPoint()).Value());
    return true;
}
bool Connect(const std::vector<CurveSegment>& original, const std::vector<CurveSegment>& shifted,
    std::vector<Cut>& cuts, std::size_t i, std::size_t j, Vector3 normal, const GeometryTolerance& tolerance) {
    const auto& a=shifted[i];const auto& b=shifted[j];
    if (Distance(a.EndPoint(),b.StartPoint())<=tolerance.modelLinearMm) return true;
    const auto reference=(original[i].EndPoint()+original[j].StartPoint())*.5;
    if (!Free(a) && !Free(b)) {
        const auto joint=PrimitiveOffsetJoint(a,b,reference,normal,tolerance);
        if (!joint) return false;
        cuts[i].end=*joint;cuts[j].start=*joint;return true;
    }
    if (OuterMiter(a,b,cuts[i],cuts[j],normal,tolerance)) return true;
    if (i==j) return false;
    const auto found=IntersectCurves(a,b,tolerance);
    if (found.empty()) return false;
    const auto best=std::min_element(found.begin(),found.end(),[&](const auto& p,const auto& q) {
        return Distance(p.position,reference)<Distance(q.position,reference);
    });
    cuts[i].last=best->firstParameter;cuts[j].first=best->secondParameter;return true;
}
}

Out OffsetCurvedContour(const std::vector<CurveSegment>& ordered, Vector3 normal,
    double distance, const GeometryTolerance& tolerance) {
    if(!std::isfinite(distance)||normal==Vector3{}||ordered.empty())return Invalid("距離と加工平面を確認してください。");
    for(const auto& curve:ordered)if(!CurveLiesInPlane(curve,ordered.front().StartPoint(),normal,tolerance.modelLinearMm))
        return Out::Failure(base::MakeError("GEO-E005","選んだ輪郭が加工平面上にありません。","加工平面を確認してください。"));
    if (distance==0) return Out::Success(ordered);
    const bool closed=Distance(ordered.front().StartPoint(),ordered.back().EndPoint())<=tolerance.modelLinearMm;
    const auto sourceCrossings=FindSelfIntersections(ordered,closed,tolerance);
    if (!sourceCrossings.HasValue()) return Out::Failure(sourceCrossings.Diagnostics());
    std::vector<CurveSegment> shifted;std::vector<OffsetStatistics> statistics(ordered.size());
    for (std::size_t i=0;i<ordered.size();++i) {
        const auto& c=ordered[i];
        const double amount=c.Kind()==CurveKind::CircularArc?-std::copysign(1.,c.SweepAngleRad())*distance:distance;
        const auto made=Free(c)?OffsetFreeCurve(c,normal,distance,tolerance,0,1,&statistics[i])
            :OffsetCurveInPlane(c,normal,amount);
        if (!made.HasValue()) return Out::Failure(made.Diagnostics());
        shifted.push_back(made.Value());
    }
    std::vector<Cut> cuts(ordered.size());
    const std::size_t joints=closed?ordered.size():ordered.size()-1;
    for (std::size_t i=0;i<joints;++i)
        if (!Connect(ordered,shifted,cuts,i,(i+1)%ordered.size(),normal,tolerance))
            return Invalid("曲線の角を接続できません。距離を小さくするか、接続位置を確認してください。");
    std::vector<CurveSegment> result;OffsetStatistics total;
    for (std::size_t i=0;i<ordered.size();++i) {
        const auto& cut=cuts[i];
        if (!(cut.first<cut.last)) return Invalid("オフセットで辺がつぶれるか、向きが反転します。");
        const auto made=Free(ordered[i])
            ? ((cut.first==0 && cut.last==1)?base::Result<CurveSegment>::Success(shifted[i])
                :OffsetFreeCurve(ordered[i],normal,distance,tolerance,cut.first,cut.last,&statistics[i]))
            :TrimOffsetPrimitive(shifted[i],cut.start.value_or(shifted[i].Evaluate(cut.first)),
                cut.end.value_or(shifted[i].Evaluate(cut.last)),tolerance);
        if (!made.HasValue()) return Out::Failure(made.Diagnostics());
        result.insert(result.end(),cut.before.begin(),cut.before.end());result.push_back(made.Value());
        result.insert(result.end(),cut.after.begin(),cut.after.end());
        total.bound=std::max(total.bound,statistics[i].bound);
        total.squares+=statistics[i].squares;total.samples+=statistics[i].samples;
    }
    const std::size_t joins=closed?result.size():result.size()-1;
    for (std::size_t i=0;i<joins;++i)
        if (Distance(result[i].EndPoint(),result[(i+1)%result.size()].StartPoint())>tolerance.modelLinearMm)
            return Invalid("オフセット後の曲線が同じ位置でつながりません。元の線は変更していません。");
    const auto crossings=FindSelfIntersections(result,closed,tolerance);
    if (!crossings.HasValue()) return Out::Failure(crossings.Diagnostics());
    return Out::Success(std::move(result),{OffsetApproximationDiagnostic(total,tolerance)});
}
}
