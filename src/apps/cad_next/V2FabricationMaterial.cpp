#include "V2FabricationMaterial.h"
#include "V2GeneratedCurves.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>

using namespace kachakacha::v2;
std::vector<std::vector<geometry::Vector3>> V2MaterialLoops(
    const app::FabricationEvaluation::MaterialPanel& panel,const std::vector<std::vector<geometry::Vector3>>& rails)
{
    const auto at=panel.bandIndex*2;if(at+1>=rails.size())return {};
    std::vector<std::vector<geometry::Vector3>> loops;
    for(const auto& uv:panel.loopsUv)loops.push_back(fabrication::MapBandMaterialLoop(uv,panel.axis,
        panel.low,panel.high,panel.longitudinal,{rails[at],rails[at+1]}));
    return loops;
}
std::vector<geometry::CurveSegment> V2MaterialCurves(std::vector<geometry::Vector3> points,bool closed,double modelToleranceMm)
{
    std::vector<geometry::Vector3> cleaned;
    for(auto p:points)if(cleaned.empty()||geometry::Distance(cleaned.back(),p)>modelToleranceMm*1.001)cleaned.push_back(p);
    if(closed&&cleaned.size()>1&&geometry::Distance(cleaned.front(),cleaned.back())<=modelToleranceMm*1.001)cleaned.pop_back();
    points=std::move(cleaned);if(points.size()<(closed?3:2))return {};
    if(closed)points.push_back(points.front());
    std::vector<geometry::CurveSegment> curves;
    for(std::size_t i=1;i<points.size();++i){const auto line=geometry::CurveSegment::MakeLine(points[i-1],points[i]);if(line.HasValue())curves.push_back(line.Value());}
    return V2CompactGeneratedCurves(curves);
}
base::Result<modeling::GuideSurfaceResult> V2BuildMaterialSurface(
    const std::vector<std::vector<geometry::Vector3>>& rails,
    const std::vector<std::vector<geometry::Vector3>>& loops,
    const geometry::GeometryTolerance& tolerance,double boundaryTolerance,geometry::Vector3 expectedNormal)
{
    using Out=base::Result<modeling::GuideSurfaceResult>;
    if(rails.size()!=2||loops.empty())return Out::Failure(base::MakeError("FAB-O001","近似の材料境界がありません。",{}));
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::RuledSections;request.keepSectionOrder=true;
    for(int row=0;row<2;++row){modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;chain.index=row+1;
        // Keep both section parameterizations identical. Independent curve fitting
        // or arc-length resampling here would shear the cutouts between the rails.
        for(std::size_t i=1;i<rails[row].size();++i){const auto line=geometry::CurveSegment::MakeLine(rails[row][i-1],rails[row][i]);if(line.HasValue())chain.segments.push_back(line.Value());}
        request.chains.push_back(std::move(chain));}
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,tolerance);if(!analysis.HasValue())return Out::Failure(analysis.Diagnostics());
    auto support=kernel::BuildGuideSurface(request,analysis.Value(),tolerance);if(!support.HasValue())return support;
    if(expectedNormal.LengthSquared()>1e-18){
        const auto frame=kernel::OutputSurfaceFrame(support.Value().handle,rails[0][rails[0].size()/2]);
        if(!frame.HasValue()){kernel::ReleaseShape(support.Value().handle);return Out::Failure(frame.Diagnostics());}
        if(geometry::Dot(frame.Value().normal,expectedNormal)<0){
            kernel::ReleaseShape(support.Value().handle);
            for(auto& chain:request.chains){std::reverse(chain.segments.begin(),chain.segments.end());for(auto& c:chain.segments){const auto reversedCurve=geometry::ReverseCurve(c);
                if(!reversedCurve.HasValue())return Out::Failure(reversedCurve.Diagnostics());c=reversedCurve.Value();}}
            const auto reversed=modeling::AnalyzeGuideSurfaceRequest(request,tolerance);if(!reversed.HasValue())return Out::Failure(reversed.Diagnostics());
            support=kernel::BuildGuideSurface(request,reversed.Value(),tolerance);if(!support.HasValue())return support;
        }
    }
    struct Supports {std::vector<modeling::KernelShapeHandle> values;~Supports(){for(auto h:values)kernel::ReleaseShape(h);}} supports;
    bool twisted=false;
    for(std::size_t i=1;i<rails[0].size();++i){const auto a=rails[0][i-1],b=rails[1][i-1],c=rails[0][i],d=rails[1][i];
        const auto n=geometry::Cross(b-a,c-a);
        if(n.Length()>1e-15&&std::abs(geometry::Dot(d-a,n))/n.Length()>tolerance.modelLinearMm*.01)twisted=true;
    }
    if(!twisted)supports.values.push_back(support.Value().handle);
    else {
        // A bilinear warped quad is not the two triangles used by the core.
        // Keep exact planar B-Rep patches for those cells, rather than moving
        // the cut path to a different supporting surface.
        kernel::ReleaseShape(support.Value().handle);
        const auto middle=(rails[0].size()-1)/2;
        const bool reverse=expectedNormal.LengthSquared()>1e-18&&geometry::Dot(expectedNormal,
            geometry::Cross(rails[1][middle]-rails[0][middle],rails[0][middle+1]-rails[0][middle]))<0;
        for(std::size_t i=1;i<rails[0].size();++i)for(int side:{0,1}){
            std::vector<geometry::Vector3> triangle=side?std::vector{rails[1][i-1],rails[1][i],rails[0][i]}:std::vector{rails[0][i-1],rails[1][i-1],rails[0][i]};
            if(reverse)std::reverse(triangle.begin(),triangle.end());
            modeling::GuideSurfaceRequest face;face.method=modeling::GuideSurfaceMethod::PlanarBoundary;
            modeling::GuideChain boundary;boundary.role=modeling::ChainRole::OuterBoundary;boundary.closed=true;boundary.index=1;boundary.segments=V2MaterialCurves(triangle,true,tolerance.modelLinearMm);face.chains.push_back(boundary);
            const auto checked=modeling::AnalyzeGuideSurfaceRequest(face,tolerance);if(!checked.HasValue())return Out::Failure(checked.Diagnostics());
            const auto made=kernel::BuildGuideSurface(face,checked.Value(),tolerance);if(!made.HasValue())return Out::Failure(made.Diagnostics());
            supports.values.push_back(made.Value().handle);
        }
    }
    std::vector<std::vector<geometry::CurveSegment>> boundaries;
    for(const auto& loop:loops)boundaries.push_back(V2MaterialCurves(loop,true,tolerance.modelLinearMm));
    double maximum=0,rms=0;
    auto exact=tolerance;exact.interactiveJoinMm=exact.modelLinearMm;
    const auto trimmed=supports.values.size()==1
        ?kernel::TrimFabricationFace(supports.values.front(),boundaries,exact)
        :kernel::TrimSurfaceRegion(supports.values,boundaries,exact,boundaryTolerance,&maximum,&rms);
    if(!trimmed.HasValue())return Out::Failure(trimmed.Diagnostics());
    if(maximum>tolerance.modelLinearMm*5){kernel::ReleaseShape(trimmed.Value());
        return Out::Failure(base::MakeError("FAB-O002","生成面の境界が型紙の境界と一致しません。",
            "追加のずれ "+std::to_string(maximum)+" mm。分割方向か枚数を変えてください。"));}

    auto made=support.Value();made.handle=trimmed.Value();made.maximumDeviationMm=maximum;made.rmsDeviationMm=rms;
    const auto samples=kernel::FaceSamplesOf(made.handle,0);if(samples.HasValue())made.samples=samples.Value().samples;
    return Out::Success(std::move(made),trimmed.Diagnostics());
}

// Scan the material in its parameter chart. Split every patch at the same
// diagonal as the core strip, so previews need neither native Booleans nor
// a different approximation of the holes.
modeling::ShapeMesh V2MaterialMesh(const app::FabricationEvaluation::MaterialPanel& panel,
    const std::vector<std::vector<geometry::Vector3>>& rails)
{
    using P=geometry::Point2;modeling::ShapeMesh mesh;
    const auto at=panel.bandIndex*2;if(at+1>=rails.size())return mesh;
    auto loops=panel.loopsUv;const bool swap=panel.axis==fabrication::BandSplitAxis::V;
    for(auto& loop:loops)for(auto& p:loop)if(swap)std::swap(p.u,p.v);
    std::vector<double> breaks=panel.longitudinal;
    for(const auto& loop:loops)for(auto p:loop)breaks.push_back(p.v);
    std::sort(breaks.begin(),breaks.end());breaks.erase(std::unique(breaks.begin(),breaks.end(),[](auto a,auto b){return std::abs(a-b)<1e-12;}),breaks.end());
    for(std::size_t i=1;i<breaks.size();++i){
        const double lo=breaks[i-1],hi=breaks[i],mid=(lo+hi)*.5;if(hi-lo<1e-12)continue;
        struct Crossing{double mid,lo,hi;};std::vector<Crossing> crossings;
        for(const auto& loop:loops)for(std::size_t j=0;j<loop.size();++j){const auto a=loop[j],b=loop[(j+1)%loop.size()];
            if((a.v<mid&&b.v>mid)||(b.v<mid&&a.v>mid)){
                const auto x=[&](double y){return a.u+(b.u-a.u)*(y-a.v)/(b.v-a.v);};
                crossings.push_back({x(mid),x(lo),x(hi)});
            }
        }
        std::sort(crossings.begin(),crossings.end(),[](auto a,auto b){return a.mid<b.mid;});
        const auto end=std::upper_bound(panel.longitudinal.begin(),panel.longitudinal.end(),mid);
        const auto column=std::min(panel.longitudinal.size()-2,static_cast<std::size_t>(end-panel.longitudinal.begin()-1));
        const double s0=panel.longitudinal[column],span=panel.longitudinal[column+1]-s0;
        const auto diagonal=[&](P p){return (p.u-panel.low)/(panel.high-panel.low)+(p.v-s0)/span-1;};
        for(std::size_t k=1;k<crossings.size();k+=2){
            const std::vector<P> quad{{crossings[k-1].lo,lo},{crossings[k].lo,lo},{crossings[k].hi,hi},{crossings[k-1].hi,hi}};
            for(int side:{-1,1}){
                std::vector<P> polygon;
                for(std::size_t j=0;j<quad.size();++j){const auto a=quad[j],b=quad[(j+1)%quad.size()];const double fa=side*diagonal(a),fb=side*diagonal(b);
                    if(fa<=1e-12)polygon.push_back(a);
                    if((fa<0&&fb>0)||(fa>0&&fb<0)){const double t=fa/(fa-fb);polygon.push_back({a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t});}
                }
                if(polygon.size()<3)continue;if(swap)for(auto& p:polygon)std::swap(p.u,p.v);
                const auto world=fabrication::MapBandMaterialLoop(polygon,panel.axis,panel.low,panel.high,panel.longitudinal,{rails[at],rails[at+1]});
                for(std::size_t j=2;j<world.size();++j){
                    if(geometry::Cross(world[j-1]-world[0],world[j]-world[0]).LengthSquared()<1e-20)continue;
                    modeling::MeshTriangle triangle;triangle.points={world[0],world[j-1],world[j]};mesh.triangles.push_back(triangle);
                }
            }
        }
    }
    modeling::RefreshNormals(mesh);modeling::RefreshBounds(mesh);mesh.faceCount=1;return mesh;
}

base::Result<base::EntityId> V2StoreMaterialGuide(document::Document& document,base::IdGenerator& ids,
    modeling::KernelShapeHandle shape,const std::string& label)
{
    using Out=base::Result<base::EntityId>;
    const auto bytes=kernel::CaptureOutputShape(shape);if(!bytes.HasValue())return Out::Failure(bytes.Diagnostics());
    domain::FreezeDerivedDefinition frozen;frozen.frozenBrep=bytes.Value();
    domain::Feature feature;feature.id=ids.NextTyped<base::IdKind::Feature>();
    feature.type=domain::FeatureType::FreezeDerived;feature.definition=frozen;feature.displayName=label+" 近似面";
    domain::Entity entity;entity.id=ids.NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;
    entity.kind=domain::EntityKind::GuideSurface;entity.displayName=feature.displayName;
    entity.editPolicy=domain::EditPolicy::Frozen;entity.groupId=document.Snapshot().settings.activeGroupId;
    feature.outputs.push_back({"material",entity.id,entity.kind});
    const auto added=document.Run(document::AddFeatureCommand(feature,{entity},"窓を保った部材面"));
    if(!added.committed)return Out::Failure(added.diagnostics);return Out::Success(entity.id);
}
V2NamedMaterialCurves V2FixedConnectionCurves(const domain::CreateFabricationModelDefinition& definition,
    const app::FabricationEvaluation& evaluated,const V2NamedMaterialCurves& originals,const std::string& stateName)
{
    V2NamedMaterialCurves result;if(!evaluated.bandMesh)return result;
    const auto state=app::ResolveFoldState(definition,*evaluated.bandMesh);
    const auto folded=fabrication::FoldBandMesh(*evaluated.bandMesh,state.masterProgress,state.creaseFactors,state.unfoldBaseRail);
    std::size_t source=0;
    for(const auto& wire:app::AdaptConnectionWires(*evaluated.bandMesh,folded,originals,evaluated.maximumDeviationMm+.35)){
        while(source<originals.size()&&originals[source].second.empty())++source;
        std::vector<geometry::CurveSegment> curves;
        if(wire.snappedPoints==0&&source<originals.size())curves=originals[source].second;
        else for(std::size_t i=1;i<wire.points.size();++i){const auto line=geometry::CurveSegment::MakeLine(wire.points[i-1],wire.points[i]);if(line.HasValue())curves.push_back(line.Value());}
        result.emplace_back(wire.name+" ("+stateName+")",std::move(curves));++source;
    }
    return result;
}
