#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <algorithm>
#include <cmath>

namespace {
using namespace kachakacha::v2;
using geometry::Vector3;
// Recover the rigid frame from the same boundary samples as the pattern. This
// also works when openings/markings changed the centroid used to fit its plane.
std::optional<geometry::PlanarFrame> SourceFrame(const fabrication::PatternPanel& panel,
    const app::FabricationSource& source)
{
    std::vector<Vector3> points;
    for(const auto& curve:*source.flatBoundary)
        for(const auto& sample:geometry::SampleCurve(curve,.01))points.push_back(sample.position);
    if(points.size()!=panel.outline.size()||points.size()<3)return {};
    const auto first=panel.outline.front();
    for(std::size_t a=1;a<points.size();++a)for(std::size_t b=a+1;b<points.size();++b) {
        const double u=panel.outline[a].u-first.u,v=panel.outline[a].v-first.v;
        const double x=panel.outline[b].u-first.u,y=panel.outline[b].v-first.v;
        const double det=u*y-v*x;if(std::abs(det)<1e-8)continue;
        geometry::PlanarFrame frame;
        frame.uDirection=((points[a]-points[0])*y-(points[b]-points[0])*v)*(1/det);
        frame.vDirection=((points[b]-points[0])*u-(points[a]-points[0])*x)*(1/det);
        if(std::abs(frame.uDirection.Length()-1)>1e-6||std::abs(frame.vDirection.Length()-1)>1e-6
            ||std::abs(geometry::Dot(frame.uDirection,frame.vDirection))>1e-6)return {};
        frame.normal=geometry::Cross(frame.uDirection,frame.vDirection);
        frame.origin=points[0]-frame.uDirection*first.u-frame.vDirection*first.v;
        return frame;
    }
    return {};
}
Vector3 Rotate(Vector3 p,Vector3 axis,double angle)
{
    return p*std::cos(angle)+geometry::Cross(axis,p)*std::sin(angle)
        +axis*(geometry::Dot(axis,p)*(1-std::cos(angle)));
}
Vector3 PoseVector(Vector3 p,const geometry::PlanarFrame& frame,double progress)
{
    const Vector3 z{0,0,1};auto axis=geometry::Cross(z,frame.normal);
    const double angle=std::acos(std::clamp(geometry::Dot(z,frame.normal),-1.0,1.0));
    axis=axis.Length()>1e-9?geometry::Normalized(axis):Vector3{1,0,0};
    const auto x=Rotate({1,0,0},axis,angle);
    const double twist=std::atan2(geometry::Dot(geometry::Cross(x,frame.uDirection),frame.normal),
        geometry::Dot(x,frame.uDirection));
    return Rotate(Rotate(p,z,twist*progress),axis,angle*progress);
}
std::vector<geometry::CurveSegment> PoseCurves(const std::vector<geometry::CurveSegment>& curves,
    const geometry::PlanarFrame& frame,double progress)
{
    const Vector3 z{0,0,1};auto axis=geometry::Cross(z,frame.normal);
    const double angle=std::acos(std::clamp(geometry::Dot(z,frame.normal),-1.0,1.0));
    axis=axis.Length()>1e-9?geometry::Normalized(axis):Vector3{1,0,0};
    const auto x=Rotate({1,0,0},axis,angle);
    const double twist=std::atan2(geometry::Dot(geometry::Cross(x,frame.uDirection),frame.normal),geometry::Dot(x,frame.uDirection));
    std::vector<geometry::CurveSegment> result;
    for(const auto& curve:curves) {
        auto next=geometry::RotateCurve(geometry::TranslateCurve(curve,frame.origin*(-1)),{},axis,-angle);if(!next.HasValue())return {};
        next=geometry::RotateCurve(next.Value(),{},z,twist*(progress-1));if(!next.HasValue())return {};
        next=geometry::RotateCurve(next.Value(),{},axis,angle*progress);if(!next.HasValue())return {};
        result.push_back(geometry::TranslateCurve(next.Value(),frame.origin*progress));
    }
    return result;
}
bool SameLoop(const std::vector<geometry::CurveSegment>& curves,const std::vector<geometry::Point2>& loop,
    const geometry::PlanarFrame& frame)
{
    std::vector<Vector3> points;for(const auto& curve:curves)
        for(const auto& p:geometry::SampleCurve(curve,.01))points.push_back(p.position);
    if(points.size()!=loop.size())return false;
    for(std::size_t i=0;i<points.size();++i)
        if(geometry::Distance(points[i],frame.origin+frame.uDirection*loop[i].u+frame.vDirection*loop[i].v)>1e-6)return false;
    return true;
}
}

bool V2MainWindow::FreezePlanarPanels(const domain::CreateFabricationModelDefinition& definition,
    const app::FabricationEvaluation& evaluated,int& wires,int& surfaces,int& parts)
{
    using fabrication::FreezeOutput;
    const auto sources=FabricationSourcesFor(definition.parts,definition.splitSolidFaces);
    const auto selected=SelectedPartNumbers();
    std::size_t first=0;for(const auto& face:evaluated.bandFaces)first+=face.mesh.BandCount();
    const double progress=std::clamp(definition.masterPercent/100.0,0.0,1.0);
    for(std::size_t index=first;index<evaluated.panels.size();++index) {
        if(!selected.empty()&&std::find(selected.begin(),selected.end(),index)==selected.end())continue;
        if(index>=evaluated.panelOrigins.size())return false;
        const auto& origin=evaluated.panelOrigins[index];const auto& panel=evaluated.panels[index];
        const auto source=std::find_if(sources.begin(),sources.end(),[&](const auto& item){
            return item.entityId==origin.entityId&&item.faceIndex==origin.faceIndex&&item.flatBoundary;});
        if(source==sources.end())return false;
        const auto frame=SourceFrame(panel,*source);if(!frame)return false;
        modeling::GuideTable table;table.method=modeling::GuideSurfaceMethod::PlanarBoundary;
        std::vector<base::EntityId> boundaries;
        const auto makeWire=[&](const std::vector<geometry::Point2>& loop,bool closed,const char* label){
            std::vector<Vector3> points;
            for(const auto& point:loop)points.push_back(PoseVector({point.u,point.v,0},*frame,progress)+frame->origin*progress);
            if(closed&&!points.empty()&&geometry::Distance(points.front(),points.back())>1e-8)points.push_back(points.front());
            const auto wire=AddPlainWire(PolylineOf(points),label);if(!wire.IsNil())++wires;return wire;
        };
        std::vector<std::vector<geometry::Point2>> loops{panel.outline};
        loops.insert(loops.end(),panel.openings.begin(),panel.openings.end());
        for(std::size_t loop=0;loop<loops.size();++loop) {
            const auto* exact=loop==0?&*source->flatBoundary:
                loop<=source->flatOpenings.size()?&source->flatOpenings[loop-1]:nullptr;
            base::EntityId wire;
            if(exact&&SameLoop(*exact,loops[loop],*frame)) {
                wire=AddPlainWire(PoseCurves(*exact,*frame,progress),loop?"生成した開口":"生成した外周");
                if(!wire.IsNil())++wires;
            } else wire=makeWire(loops[loop],true,loop?"生成した開口":"生成した外周");
            if(wire.IsNil())return false;boundaries.push_back(wire);
        }
        for(const auto& fold:panel.folds)if(makeWire(fold.path,false,"生成した折り線").IsNil())return false;
        for(const auto& cut:panel.reliefCuts)if(makeWire(cut,false,"生成した切れ目").IsNil())return false;
        if(freezeOutput_==FreezeOutput::WiresOnly)continue;
        AdoptCurrentDocument();
        for(std::size_t loop=0;loop<boundaries.size();++loop) {
            const auto input=app::GuideSelectionOf(session_->GetDocument(),session_->Scene(),boundaries[loop]);
            if(!input)return false;
            const auto added=modeling::AddSelectionAsNewRow(table,loop?modeling::ChainRole::HoleBoundary:modeling::ChainRole::OuterBoundary,*input);
            if(!added.HasValue())return false;table=added.Value();
        }
        auto built=BuildSurfaceFromTable(table,true);if(!built)return false;
        auto handles=guideShapes_;handles.insert(partShapes_.begin(),partShapes_.end());
        const auto native=handles.find(origin.entityId.ToString());if(native==handles.end())return false;
        const auto originalNormal=kernel::OutputSurfaceFrame(native->second,source->flatBoundary->front().StartPoint());
        const auto madeNormal=kernel::OutputSurfaceFrame(built->handle,frame->origin*progress);
        if(!originalNormal.HasValue()||!madeNormal.HasValue())return false;
        const double side=geometry::Dot(frame->normal,originalNormal.Value().normal)<0?-1:1;
        const auto expected=PoseVector({0,0,side},*frame,progress);
        if(geometry::Dot(expected,madeNormal.Value().normal)<0) {
            kernel::ReleaseShape(built->handle);
            for(std::size_t row=0;row<table.rows.size();++row) {
                const auto reverse=modeling::ReverseRow(table,row);if(!reverse.HasValue())return false;table=reverse.Value();
            }
            built=BuildSurfaceFromTable(table,true);if(!built)return false;
        }
        const auto guide=AdoptGuideSurface(table,*built,boundaries,panel.panelId+" 生成面");
        if(guide.IsNil())return false;++surfaces;
        if(freezeOutput_==FreezeOutput::PartsOnly||freezeOutput_==FreezeOutput::Both) {
            viewport_->SetSelection(app::SelectionSet{{guide}});const auto before=partShapes_.size();
            RunThickenSurface(definition.materialThickness.value);if(partShapes_.size()<=before)return false;++parts;
        }
    }
    return true;
}
