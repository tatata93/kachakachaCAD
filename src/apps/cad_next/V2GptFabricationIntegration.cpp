#include "V2MainWindow.h"
#include "V2GptFabricationTool.h"
#include "V2FabricationDock.h"
#include "V2SurfaceEditTool.h"
#include "kachakacha/app/GroupTree.h"
using namespace kachakacha::v2;
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
    const app::FabricationEvaluation& evaluation,int& wires)
{
    if (freezeOutput_ != fabrication::FreezeOutput::WiresOnly) {
        ReportDiagnostics({base::MakeError("GPT-F006", "GPT版の部品への厚み付き固定は未対応です。", "「輪郭を線にする」または型紙のSVG/DXF出力を使用してください。")});
        return false;
    }
    for (std::size_t index=0;index<evaluation.gptPanels.size();++index) {
        const auto& panel=evaluation.gptPanels[index];
        const double progress=definition.bandProgress.size()==evaluation.gptPanels.size()
            ? definition.bandProgress[index] : definition.masterPercent/100.;
        std::vector<std::vector<geometry::Point2>> loops{panel.pattern.outline};
        loops.insert(loops.end(),panel.pattern.openings.begin(),panel.pattern.openings.end());
        for (const auto& loop:loops) {
            const auto points=fabrication::GptPanelLoop(panel,loop,progress);
            if (AddPlainWire(PolylineOf(points),panel.pattern.panelId.c_str()).IsNil()) { return false; }
            ++wires;
        }
    }
    return true;
}
