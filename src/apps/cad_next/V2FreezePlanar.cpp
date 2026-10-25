#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2PlanarCurves.h"
using namespace kachakacha::v2;
using namespace V2PlanarCurves;
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <algorithm>
#include <cmath>

bool V2MainWindow::FreezePlanarPanels(const domain::CreateFabricationModelDefinition& definition,
    const app::FabricationEvaluation& evaluated,int& wires,int& surfaces,int& parts)
{
    using fabrication::FreezeOutput;
    const auto sources=FabricationSourcesFor(definition.parts,definition.splitSolidFaces);
    const auto selected=SelectedPartNumbers();
    const auto markings=FabricationMarkingsFor(definition);
    const auto tolerance=session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    std::size_t first=0;for(const auto& face:evaluated.bandFaces)first+=face.mesh.BandCount();
    const double progress=std::clamp(definition.masterPercent/100.0,0.0,1.0);
    for(std::size_t index=first;index<evaluated.panels.size();++index) {
        if(!selected.empty()&&std::find(selected.begin(),selected.end(),index)==selected.end())continue;
        if(index>=evaluated.panelOrigins.size())return false;
        const auto& origin=evaluated.panelOrigins[index];const auto& panel=evaluated.panels[index];
        const auto source=std::find_if(sources.begin(),sources.end(),[&](const auto& item){
            return item.entityId==origin.entityId&&item.faceIndex==origin.faceIndex&&item.flatBoundary;});
        if(source==sources.end())return false;
        const auto frame=SourceFrame(panel,*source,tolerance);if(!frame)return false;
        modeling::GuideTable table;table.method=modeling::GuideSurfaceMethod::PlanarBoundary;
        std::vector<base::EntityId> boundaries;
        for(const auto& path:Paths(panel,*source,markings,*frame,tolerance)) {
            const auto curves=PoseCurves(path.curves,*frame,progress);
            const auto wire=AddPlainWire(curves,path.label.c_str());if(wire.IsNil())return false;++wires;
            if(path.layer==exporters::PatternLine::Outline||path.layer==exporters::PatternLine::Opening)boundaries.push_back(wire);
        }
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
