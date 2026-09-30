#include "V2MainWindow.h"
#include "V2GptFabricationTool.h"
#include "V2FabricationDock.h"
#include "V2SurfaceEditTool.h"
#include "kachakacha/app/GroupTree.h"
#include <algorithm>
#include <cmath>
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctExtrude.h"
using namespace kachakacha::v2;
namespace {
bool FollowsPanel(const modeling::KernelShapeHandle& shape,const fabrication::GptApproxPanel& panel,
    double progress,const geometry::GeometryTolerance& tolerance)
{
    const auto mesh=fabrication::GptPanelMesh(panel,progress);
    const std::size_t step=std::max<std::size_t>(1,mesh.triangles.size()/64);
    for(std::size_t index=0;index<mesh.triangles.size();index+=step) {
        const auto point=mesh.triangles[index].Center();
        const auto nearest=kernel::DistanceToShapeSurface(shape,point);
        if(!nearest.HasValue() || nearest.Value()>tolerance.modelLinearMm*10) { return false; }
    }
    return !mesh.triangles.empty();
}
}

void V2MainWindow::BeginGptFabrication()
{
    const auto selected=viewport_->Selection();
    ClearPendingCommand();
    if (surfaceShelfShown_) { EndSurfacePreview(); }
    if (approxShelfShown_) { EndApprox(); }
    if (booleanShelfShown_) { EndBoolean(); }
    if (thickenShelfShown_) { EndThicken(); }
    if (surfaceEdit_ && surfaceEdit_->Active()) { surfaceEdit_->End(); }
    SelectTool(modeling::DrawingTool::Select); EndOwnedToolsBut(gptFabrication_.get());
    viewport_->SetSelection(selected); gptFabrication_->Begin();
}
void V2MainWindow::AppendGptFabricationViews(std::vector<V2Viewport::ShapeView>& shapes) const
{
    if (fabricationDock_ && !fabricationDock_->ShowApprox()) { return; }
    const auto& snapshot=session_->GetDocument().Snapshot();
    for (const auto& entity:snapshot.entities) {
        if (entity.kind!=domain::EntityKind::FabricationModel || !app::EntityEffectivelyVisible(snapshot,entity)) { continue; }
        const auto found=fabricationModels_.find(entity.id.ToString());
        if (found==fabricationModels_.end() || found->second.gptPanels.empty()) { continue; }
        const auto* feature=session_->GetDocument().FindFeature(entity.createdBy);
        const auto* definition=feature ? std::get_if<domain::CreateFabricationModelDefinition>(&feature->definition) : nullptr;
        if (!definition) { continue; }
        for (std::size_t index=0;index<found->second.gptPanels.size();++index) {
            const auto& panel=found->second.gptPanels[index];
            const double progress=definition->bandProgress.size()==found->second.gptPanels.size()
                ? definition->bandProgress[index] : definition->masterPercent/100.;
            V2Viewport::ShapeView view; view.entityId=entity.id; view.surface=true;
            view.mesh=fabrication::GptPanelMesh(panel,progress); shapes.push_back(std::move(view));
        }
    }
}

bool V2MainWindow::FreezeGptContours(const domain::CreateFabricationModelDefinition& definition,
    const app::FabricationEvaluation& evaluation,int& wires,int* surfaces)
{
    if (freezeOutput_ != fabrication::FreezeOutput::WiresOnly && freezeOutput_ != fabrication::FreezeOutput::WiresAndSurfaces) {
        ReportDiagnostics({base::MakeError("GPT-F006", "GPT版の部品への厚み付き固定は未対応です。", "「輪郭を線にする」または型紙のSVG/DXF出力を使用してください。")});
        return false;
    }
    const auto selected=SelectedPartNumbers();
    for (std::size_t index=0;index<evaluation.gptPanels.size();++index) {
        if(!selected.empty() && std::find(selected.begin(),selected.end(),index)==selected.end()) { continue; }
        const auto& panel=evaluation.gptPanels[index];
        const bool makeSurface=freezeOutput_==fabrication::FreezeOutput::WiresAndSurfaces;
        const double progress=definition.bandProgress.size()==evaluation.gptPanels.size()
            ? definition.bandProgress[index] : definition.masterPercent/100.;
        std::vector<std::vector<geometry::Point2>> loops{panel.pattern.outline};
        loops.insert(loops.end(),panel.pattern.openings.begin(),panel.pattern.openings.end());
        for (const auto& loop:loops) {
            const auto points=fabrication::GptPanelLoop(panel,loop,progress);
            const auto boundary=AddPlainWire(PolylineOf(points),panel.pattern.panelId.c_str());
            if (boundary.IsNil()) { return false; }
            ++wires;
        }
        if(makeSurface) {
            std::vector<base::EntityId> constraints;
            for(const auto& patch:fabrication::GptPanelPatches(panel,progress)) {
                std::vector<geometry::Vector3> points(patch.begin(),patch.end()); points.push_back(points.front());
                const auto support=AddPlainWire(PolylineOf(points),(panel.pattern.panelId+" 面の区間境界").c_str());
                if(support.IsNil()) { return false; }
                const auto hidden=session_->GetDocument().Run(document::SetVisibilityCommand({support},domain::Visibility::Hidden));
                if(!hidden.committed) { ReportDiagnostics(hidden.diagnostics); return false; }
                constraints.push_back(support);
            }
            if(!GenerateApproxSurface(constraints,panel.pattern.panelId+" 近似面")) { return false; }
            const auto surfaceId=session_->GetDocument().Snapshot().entities.back().id;
            if(!FollowsPanel(guideShapes_.at(surfaceId.ToString()),panel,progress,session_->GetDocument().Snapshot().settings.tolerance)) {
                ReportDiagnostics({base::MakeError("GPT-F007","生成面が部材の曲げ形状と一致しません。","入力形状を確認してください。")}); return false;
            }
            if(surfaces) { ++*surfaces; }
        }
    }
    return true;
}
