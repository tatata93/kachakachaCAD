#include "kachakacha/geometry/CurveRange.h"
#include "kachakacha/geometry/CurveFit.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kachakacha::v2::geometry {
namespace {
using Out=base::Result<std::vector<CurveSegment>>;
base::Result<CurveSegment> RestrictSingle(CurveSegment piece,double a,double b)
{
    if(b<1) {const auto split=piece.Split(b);if(!split.HasValue())return base::Result<CurveSegment>::Failure(split.Diagnostics());piece=*split.Value().first;}
    if(a>0) {const auto split=piece.Split(a/b);if(!split.HasValue())return base::Result<CurveSegment>::Failure(split.Diagnostics());piece=*split.Value().second;}
    return base::Result<CurveSegment>::Success(std::move(piece));
}
}
Out ExtractCurveRange(const CurveSegment& curve,double first,double last)
{
    if(!std::isfinite(first)||!std::isfinite(last)||first<0||last>1||!(first<last))
        return Out::Failure(base::MakeError("GEO-C012","取り出す曲線の範囲が不正です。","0以上1以下の、長さがある区間を指定してください。"));
    if(first==0&&last==1)return Out::Success({curve});
    if(curve.Kind()!=CurveKind::CubicBSpline) {
        const auto piece=RestrictSingle(curve,first,last);
        return piece.HasValue()?Out::Success({piece.Value()}):Out::Failure(piece.Diagnostics());
    }
    const auto spans=UniformBSplineBezierSpans(curve.ControlPoints());
    const auto knotPosition=[&](double parameter) {
        const double value=parameter*static_cast<double>(spans.size()),integer=std::round(value);
        return std::abs(value-integer)<=16*std::numeric_limits<double>::epsilon()*std::max(1.,std::abs(value))?integer:value;
    };
    const double a=knotPosition(first),b=knotPosition(last);
    const auto start=std::min(static_cast<std::size_t>(std::floor(a)),spans.size()-1);
    const auto finish=std::min(static_cast<std::size_t>(std::ceil(b))-1,spans.size()-1);
    const double localStart=a-start,localEnd=b-finish;
    const auto endPiece=[&](std::size_t index,double low,double high) {
        const auto& p=spans[index];return RestrictSingle(CurveSegment::MakeCubicBezier({p[0],p[1],p[2],p[3]}).Value(),low,high);
    };
    if(start==finish) {
        const auto piece=endPiece(start,localStart,localEnd);
        return piece.HasValue()?Out::Success({piece.Value()}):Out::Failure(piece.Diagnostics());
    }
    std::vector<CurveSegment> result;
    if(localStart>0) {const auto p=endPiece(start,localStart,1);if(!p.HasValue())return Out::Failure(p.Diagnostics());result.push_back(p.Value());}
    const auto begin=start+(localStart>0?1:0),end=finish+(localEnd==1?1:0);
    if(begin<end) {
        const auto& p=curve.ControlPoints();
        const auto middle=CurveSegment::MakeCubicBSpline(std::vector<Vector3>(p.begin()+begin,p.begin()+end+3));
        if(!middle.HasValue())return Out::Failure(middle.Diagnostics());result.push_back(middle.Value());
    }
    if(localEnd<1) {const auto p=endPiece(finish,0,localEnd);if(!p.HasValue())return Out::Failure(p.Diagnostics());result.push_back(p.Value());}
    return Out::Success(std::move(result));
}
}
