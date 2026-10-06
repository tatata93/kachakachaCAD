#include "kachakacha/app/PlaneFocus.h"

#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <cmath>

namespace kachakacha::v2::app {

bool UsesDrawingPlaneFocus(modeling::DrawingTool tool) noexcept
{
    using modeling::DrawingTool;
    switch(tool){
    case DrawingTool::SetGridOrigin: case DrawingTool::Point:
    case DrawingTool::Line: case DrawingTool::Polyline: case DrawingTool::Rectangle:
    case DrawingTool::Circle: case DrawingTool::Arc: case DrawingTool::Bezier: case DrawingTool::Spline:
        return true;
    default: return false;
    }
}

base::Result<modeling::WorkPlaneFrame> EditingPlane(
    const std::vector<geometry::CurveSegment>& curves, const modeling::WorkPlaneFrame& preferred,
    bool automatic, double toleranceMm)
{
    using namespace geometry;
    using Out=base::Result<modeling::WorkPlaneFrame>;
    if(curves.empty())return Out::Failure(base::MakeError("GEO-E004","加工する線を選んでください。",{}));
    std::vector<Vector3> points;
    for(const auto& curve:curves)for(const auto& point:SampleCurve(curve,toleranceMm,256))points.push_back(point.position);
    auto plane=preferred;plane.origin=curves.front().StartPoint();
    if(automatic){
        const auto fit=FitPlane(points);
        if(fit.valid){
            plane.origin=fit.origin;plane.normal=fit.normal;
            // 符号だけは作図面に近い向きに揃える。直交する場合も決定的にする。
            const double alignment=Dot(plane.normal,preferred.normal);
            const double sign=std::abs(alignment)>1e-8?alignment:
                (std::abs(plane.normal.x)>1e-8?plane.normal.x:(std::abs(plane.normal.y)>1e-8?plane.normal.y:plane.normal.z));
            if(sign<0)plane.normal=plane.normal*-1;
        }
    }
    for(const auto& curve:curves)if(!CurveLiesInPlane(curve,plane.origin,plane.normal,toleranceMm))
        return Out::Failure(base::MakeError("GEO-E005","加工平面を決められません。",
            "加工平面の候補を変更してください。立体的な線は平面へ投影せず、処理を止めます。"));
    auto u=preferred.uAxis-plane.normal*Dot(preferred.uAxis,plane.normal);
    if(u.Length()<1e-8)u=preferred.vAxis-plane.normal*Dot(preferred.vAxis,plane.normal);
    plane.uAxis=Normalized(u);plane.vAxis=Normalized(Cross(plane.normal,plane.uAxis));
    return Out::Success(plane);
}

bool CurveLiesOnPlane(const geometry::CurveSegment& segment,
    const modeling::WorkPlaneFrame& plane, double toleranceMm)
{
    // 判定そのものは geometry に1つだけ置き、スナップと同じ答えを出す。
    return geometry::CurveLiesInPlane(segment, plane.origin, plane.normal, toleranceMm);
}

bool DimsOffPlaneCurve(bool drawing, bool enabled, bool selected, bool onPlane) noexcept
{
    return drawing && enabled && !selected && !onPlane;
}

bool PickableOffPlaneCurve(bool drawing, bool enabled, bool onPlane) noexcept
{
    // 薄くしているものは掴まない。それ以外は全部掴める。
    return !DimsOffPlaneCurve(drawing, enabled, false, onPlane);
}

} // namespace kachakacha::v2::app
