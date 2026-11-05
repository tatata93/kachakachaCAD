#include "kachakacha/geometry/PlanarWireOffset.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireConnect.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace kachakacha::v2::geometry {
namespace {
using Out=base::Result<std::vector<CurveSegment>>;
Out Invalid(const char* detail) {
    return Out::Failure(base::MakeError("GEO-E004",
        "この距離では閉じた輪郭をオフセットできません。",detail));
}
std::vector<Vector3> LineCircle(const CurveSegment& line,const CurveSegment& circle,
    const GeometryTolerance& tolerance) {
    const auto direction=Normalized(line.EndPoint()-line.StartPoint());
    const auto relative=line.StartPoint()-circle.Center();
    const double projection=-Dot(relative,direction);
    double height2=circle.Radius()*circle.Radius()-Dot(relative,relative)+projection*projection;
    if(height2 < -tolerance.modelLinearMm*std::max(circle.Radius(),tolerance.modelLinearMm))return {};
    height2=std::max(0.,height2);
    const auto center=line.StartPoint()+direction*projection;
    const auto delta=direction*std::sqrt(height2);
    return {center+delta,center-delta};
}
std::vector<Vector3> CircleCircle(const CurveSegment& a,const CurveSegment& b,
    Vector3 normal,const GeometryTolerance& tolerance) {
    const auto separation=b.Center()-a.Center();const double length=Distance(a.Center(),b.Center());
    if(length<=tolerance.modelLinearMm)return {};
    const double along=(a.Radius()*a.Radius()-b.Radius()*b.Radius()+length*length)/(2*length);
    double height2=a.Radius()*a.Radius()-along*along;
    if(height2 < -tolerance.modelLinearMm*std::max(a.Radius(),tolerance.modelLinearMm))return {};
    const auto direction=separation*(1./length);
    const auto center=a.Center()+direction*along;
    const auto delta=Cross(normal,direction)*std::sqrt(std::max(0.,height2));
    return {center+delta,center-delta};
}
std::optional<Vector3> Joint(const CurveSegment& a,const CurveSegment& b,Vector3 near,
    Vector3 normal,const GeometryTolerance& tolerance) {
    if(Distance(a.EndPoint(),b.StartPoint())<=tolerance.modelLinearMm)
        return (a.EndPoint()+b.StartPoint())*.5;
    const bool lineA=a.Kind()==CurveKind::Line,lineB=b.Kind()==CurveKind::Line;
    if(lineA&&lineB){
        const auto met=MeetLines(a,b,tolerance.modelLinearMm);
        if(met.HasValue())return met.Value().first.EndPoint();
        return std::nullopt;
    }
    const auto candidates=lineA?LineCircle(a,b,tolerance):lineB?LineCircle(b,a,tolerance):
        CircleCircle(a,b,normal,tolerance);
    if(candidates.empty())return std::nullopt;
    return *std::min_element(candidates.begin(),candidates.end(),[&](const auto& p,const auto& q){
        return Distance(p,near)<Distance(q,near);
    });
}
base::Result<CurveSegment> TrimToJoints(const CurveSegment& curve,Vector3 start,Vector3 end,
    const GeometryTolerance& tolerance) {
    using One=base::Result<CurveSegment>;
    if(curve.Kind()==CurveKind::Line){
        if(Dot(end-start,curve.EndPoint()-curve.StartPoint())>
            tolerance.modelLinearMm*Distance(curve.StartPoint(),curve.EndPoint()))
            return CurveSegment::MakeLine(start,end);
    }else{
        const auto y=Cross(curve.Normal(),curve.ReferenceDirection());
        const auto angle=[&](Vector3 p,double reference){
            const auto radial=p-curve.Center();
            return reference+std::remainder(std::atan2(Dot(radial,y),Dot(radial,curve.ReferenceDirection()))-
                reference,2*kPi);
        };
        const double first=angle(start,curve.StartAngleRad());
        const double last=angle(end,curve.StartAngleRad()+curve.SweepAngleRad());
        const double sweep=last-first;
        if(sweep*curve.SweepAngleRad()>0&&std::abs(sweep)<2*kPi-tolerance.modelAngularRad)
            return CurveSegment::MakeCircularArc(curve.Center(),curve.Normal(),curve.ReferenceDirection(),
                curve.Radius(),first,sweep);
    }
    return One::Failure(base::MakeError("GEO-E004","オフセットで辺がつぶれるか、向きが反転します。",
        "距離を小さくしてください。元の線は変更していません。"));
}
Out Independently(const std::vector<CurveSegment>& curves,Vector3 normal,double distance) {
    std::vector<CurveSegment> result;result.reserve(curves.size());
    for(const auto& curve:curves){
        const auto moved=OffsetCurveInPlane(curve,normal,distance);
        if(!moved.HasValue())return Out::Failure(moved.Diagnostics());
        result.push_back(moved.Value());
    }
    return Out::Success(std::move(result));
}
}

Out OffsetCurvesInPlane(const std::vector<CurveSegment>& curves,Vector3 planeNormal,
    double distanceMm,const GeometryTolerance& tolerance) {
    if(curves.size()<2)return Independently(curves,planeNormal,distanceMm);
    const auto joined=JoinCurves(curves,tolerance.interactiveJoinMm);
    if(!joined.HasValue()||Distance(joined.Value().front().StartPoint(),joined.Value().back().EndPoint())>
        tolerance.modelLinearMm)return Independently(curves,planeNormal,distanceMm);
    const auto& ordered=joined.Value();const auto normal=Normalized(planeNormal);
    if(!std::isfinite(distanceMm)||normal==Vector3{})return Invalid("距離と加工平面を確認してください。");
    for(const auto& curve:ordered){
        if(!CurveLiesInPlane(curve,ordered.front().StartPoint(),normal,tolerance.modelLinearMm))
            return Out::Failure(base::MakeError("GEO-E005","選んだ輪郭が加工平面上にありません。",
                "加工平面を確認してください。"));
        if(curve.Kind()!=CurveKind::Line&&curve.Kind()!=CurveKind::CircularArc)
            return Independently(curves,planeNormal,distanceMm);
    }
    const auto originalCrossings=FindSelfIntersections(ordered,true,tolerance);
    if(!originalCrossings.HasValue())return Out::Failure(originalCrossings.Diagnostics());
    if(!originalCrossings.Value().empty())return Invalid("元の輪郭が自身と交わっています。");
    std::vector<CurveSegment> shifted;shifted.reserve(ordered.size());
    for(const auto& curve:ordered){
        // The standalone arc API changes radius; a traversed contour offsets left.
        const double amount=curve.Kind()==CurveKind::CircularArc?
            -std::copysign(1.,curve.SweepAngleRad())*distanceMm:distanceMm;
        const auto moved=OffsetCurveInPlane(curve,normal,amount);
        if(!moved.HasValue())return Out::Failure(moved.Diagnostics());
        shifted.push_back(moved.Value());
    }
    std::vector<Vector3> joints;const auto count=shifted.size();joints.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const auto next=(i+1)%count;
        const auto point=Joint(shifted[i],shifted[next],
            (ordered[i].EndPoint()+ordered[next].StartPoint())*.5,normal,tolerance);
        if(!point)return Invalid("隣り合う辺を同じ位置でつなげません。距離を小さくしてください。");
        joints.push_back(*point);
    }
    std::vector<CurveSegment> result;result.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const auto trimmed=TrimToJoints(shifted[i],joints[(i+count-1)%count],joints[i],tolerance);
        if(!trimmed.HasValue())return Out::Failure(trimmed.Diagnostics());
        result.push_back(trimmed.Value());
    }
    for(std::size_t i=0;i<count;++i)
        if(Distance(result[i].EndPoint(),result[(i+1)%count].StartPoint())>tolerance.modelLinearMm)
            return Invalid("曲線の接続位置が許容差を超えます。元の輪郭は変更していません。");
    const auto crossings=FindSelfIntersections(result,true,tolerance);
    if(!crossings.HasValue())return Out::Failure(crossings.Diagnostics());
    if(!crossings.Value().empty())return Invalid("オフセットした輪郭が自身と交わります。距離を小さくしてください。");
    return Out::Success(std::move(result));
}
}
