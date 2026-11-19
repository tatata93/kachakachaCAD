#include "V2DrawingPlaneProjection.h"
#include "V2ShapeLifetime.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctWireProjection.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace kachakacha::v2;
namespace {
base::Result<modeling::KernelShapeHandle> ProjectionTarget(const std::vector<geometry::CurveSegment>& curves,
    const modeling::WorkPlaneFrame& frame,const geometry::GeometryTolerance& tolerance)
{
    using Out=base::Result<modeling::KernelShapeHandle>;
    geometry::Bounds3 range;
    for(const auto& c:curves){
        geometry::Bounds3 hull;
        if(c.Kind()==geometry::CurveKind::Circle||c.Kind()==geometry::CurveKind::CircularArc){
            const geometry::Vector3 radius{c.Radius(),c.Radius(),c.Radius()};
            hull.Add(c.Center()-radius);hull.Add(c.Center()+radius);
        }else for(const auto& control:c.ControlPoints())hull.Add(control);
        for(int i=0;i<8;++i){
            const geometry::Vector3 p{i&1?hull.maximum.x:hull.minimum.x,i&2?hull.maximum.y:hull.minimum.y,i&4?hull.maximum.z:hull.minimum.z};
            range.Add({frame.CoordinateU(p),frame.CoordinateV(p),0});
        }
    }
    const double margin=std::max(1.,range.DiagonalLength()*.05);
    const auto p=[&](bool right,bool top){return frame.PointAt((right?range.maximum.x:range.minimum.x)+(right?margin:-margin),
        (top?range.maximum.y:range.minimum.y)+(top?margin:-margin));};
    const geometry::Vector3 corners[]={p(false,false),p(true,false),p(true,true),p(false,true)};
    modeling::GuideChain chain;chain.role=modeling::ChainRole::OuterBoundary;chain.closed=true;
    for(int i=0;i<4;++i){
        const auto line=geometry::CurveSegment::MakeLine(corners[i],corners[(i+1)%4]);
        if(!line.HasValue())return Out::Failure(line.Diagnostics());chain.segments.push_back(line.Value());
    }
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::PlanarBoundary;request.chains={chain};
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,tolerance);
    if(!analysis.HasValue())return Out::Failure(analysis.Diagnostics());
    const auto shape=kernel::BuildGuideSurface(request,analysis.Value(),tolerance);
    return shape.HasValue()?Out::Success(shape.Value().handle):Out::Failure(shape.Diagnostics());
}
}

base::Result<std::vector<geometry::CurveSegment>> V2ProjectDrawingCurves(const std::vector<geometry::CurveSegment>& curves,
    const modeling::WorkPlaneFrame& frame,const geometry::GeometryTolerance& tolerance,V2DrawingProjectionStats* stats)
{
    using Out=base::Result<std::vector<geometry::CurveSegment>>;
    if(stats)*stats={};
    const auto exact=geometry::ProjectCurvesOntoPlane(curves,{frame.origin,frame.normal},tolerance);
    if(exact.HasValue()||exact.FirstCode()!="GEO-E017")return exact;
    for(const auto& curve:curves){
        const auto one=geometry::ProjectCurveOntoPlane(curve,{frame.origin,frame.normal},tolerance);
        if(!one.HasValue()&&one.FirstCode()!="GEO-E017")return Out::Failure(one.Diagnostics());
    }
    const auto target=ProjectionTarget(curves,frame,tolerance);
    if(!target.HasValue())return Out::Failure(target.Diagnostics());
    const v2ui::ShapeListOwner owner({target.Value()});
    kernel::WireProjectionOptions options;options.mode=kernel::WireProjectionMode::Closest;
    options.toleranceMm=std::clamp(tolerance.modelLinearMm*.1,1e-7,.001);
    options.joinToleranceMm=tolerance.modelLinearMm;
    const auto projected=kernel::ProjectNativeWires({target.Value()},{{curves,frame.normal}},options);
    if(!projected.HasValue())return Out::Failure(projected.Diagnostics());
    std::vector<geometry::CurveSegment> result;
    for(const auto& wire:projected.Value().wires)result.insert(result.end(),wire.begin(),wire.end());
    V2DrawingProjectionStats measured;measured.maximumErrorMm=projected.Value().maxCurveDeviationMm;double squares=0;
    for(const auto& source:curves)for(int i=0;i<=32;++i){
            const auto wanted=geometry::ProjectPointOntoPlane(source.Evaluate(i/32.),{frame.origin,frame.normal});
            double error=std::numeric_limits<double>::infinity();
            for(const auto& curve:result)error=std::min(error,curve.ClosestPoint(wanted).distance);
            if(error>options.toleranceMm*3)return Out::Failure(base::MakeError(kernel::kWireProjectionFailed,
                "投影した線の一部が欠けているか、偏差が許容値を超えています。","元の線は変更していません。作図面と選択を確認してください。"));
            squares+=error*error;++measured.samples;
    }
    measured.rmsErrorMm=measured.samples?std::sqrt(squares/measured.samples):0;
    if(stats)*stats=measured;
    return Out::Success(std::move(result),projected.Diagnostics());
}
