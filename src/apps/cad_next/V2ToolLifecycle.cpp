#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ThickenPreviewState.h"
#include "V2OperationPanelHost.h"
#include "V2SurfaceEditTool.h"
#include "V2SolidTool.h"
#include "V2EdgeFinishTool.h"
#include "V2ShellSplitTool.h"
#include "V2HoverEditTool.h"
#include "V2GptSurfaceTool.h"
#include "V2GptFabricationTool.h"
#include "V2LoopFacesTool.h"
#include <QString>

void V2MainWindow::EndArmedTools()
{
    ClearPendingCommand();
    viewport_->CancelPointPick();
    viewport_->CancelReferencePick();
    const auto selection=viewport_->Selection();
    if(operationHost_)operationHost_->ClearTemporaryPage();
    if(extrudeShelfShown_||viewport_->ExtrudeHandleShown())EndExtrudePreview();
    if (partDock_ != nullptr) partDock_->EndTool();
    if (fabricationDock_ != nullptr) fabricationDock_->EndTool();
    if (surfaceShelfShown_) { EndSurfacePreview(); }
    if (approxShelfShown_) { EndApprox(); }
    if (booleanShelfShown_) { EndBoolean(); }
    if (thicken_->shelfShown) { EndThicken(); }
    if (surfaceEdit_ != nullptr && surfaceEdit_->Active()) { surfaceEdit_->End(); }
    if (solidTool_ != nullptr && solidTool_->Active()) { solidTool_->End(); }
    if (edgeFinishTool_ != nullptr && edgeFinishTool_->Active()) { edgeFinishTool_->End(); }
    if (shellSplitTool_ != nullptr && shellSplitTool_->Active()) { shellSplitTool_->End(); }
    if (hoverEdit_ != nullptr) { hoverEdit_->Clear(); }
    if (gptFabrication_ != nullptr) { gptFabrication_->End(); }
    if (gptSurface_ != nullptr) { gptSurface_->End(); }
    if (loopFaces_ != nullptr) { loopFaces_->End(); }
    if (viewport_ != nullptr) {
        // 道具が消し忘れた札・下見・拾いの構えを念のため全部消す。
        viewport_->CancelReferencePick();
        viewport_->HideToolPreview();
        viewport_->HideToolRoleLabels();
        viewport_->SetRoleColors({});   // 境界辺などの紫の色分けも残さない(オーナー報告 2026-09-28)
        viewport_->SetToolPickActive(false);
        viewport_->SetToolPickToggle(false);
    }
    viewport_->SetSelection(selection);
    ShowToolFooter(QString());
}

void V2MainWindow::EndOwnedToolsBut(const void* keep)
{
    ClearPendingCommand();
    viewport_->CancelPointPick();
    viewport_->CancelReferencePick();
    const auto selection=viewport_->Selection();
    if(operationHost_)operationHost_->ClearTemporaryPage();
    if(extrudeShelfShown_||viewport_->ExtrudeHandleShown())EndExtrudePreview();
    if(surfaceShelfShown_)EndSurfacePreview();
    if(approxShelfShown_)EndApprox();
    if(booleanShelfShown_)EndBoolean();
    if(thicken_->shelfShown)EndThicken();
    if(surfaceEdit_&&surfaceEdit_.get()!=keep&&surfaceEdit_->Active())surfaceEdit_->End();
    if (gptFabrication_ != nullptr && gptFabrication_.get() != keep) { gptFabrication_->End(); }
    if (gptSurface_ != nullptr && gptSurface_.get() != keep) { gptSurface_->End(); }
    if (solidTool_ != nullptr && solidTool_.get() != keep && solidTool_->Active()) {
        solidTool_->End();
    }
    if (edgeFinishTool_ != nullptr && edgeFinishTool_.get() != keep && edgeFinishTool_->Active()) {
        edgeFinishTool_->End();
    }
    if (shellSplitTool_ != nullptr && shellSplitTool_.get() != keep && shellSplitTool_->Active()) {
        shellSplitTool_->End();
    }
    if (loopFaces_ != nullptr && loopFaces_.get() != keep
        && (loopFaces_->Active() || loopFaces_->HasRecent())) {
        loopFaces_->End();
    }
    viewport_->SetSelection(selection);
}
