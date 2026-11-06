#include "V2MainWindow.h"
#include "V2DocumentTiming.h"
#include "V2OffsetTool.h"
#include "V2ToolSelection.h"
#include "V2GptFabricationTool.h"
#include "V2GptSurfaceTool.h"
#include "V2LoopFacesTool.h"
#include "V2SurfaceEditTool.h"
#include "V2SolidTool.h"
#include "V2EdgeFinishTool.h"
#include "V2ShellSplitTool.h"
#include "V2SurfaceAnalysisTool.h"
#include <QElapsedTimer>
#include <iostream>

//! 3D か左の一覧で選択が変わるたびに、関わる棚を持ち直す(WireViewportCallbacks の続き)。
void V2MainWindow::HandleSelectionChanged()
{
    QElapsedTimer timer;timer.start();
    V2DocumentTiming timing("selection");timing.Mark("begin");
    const auto tick=[&](const char* name){timing.Mark(name);if(qEnvironmentVariableIsSet("KACHACAD_OUTPUT_PERF_OUTPUT"))std::cout<<"selection "<<name<<" ms="<<timer.restart()<<std::endl;};
    // 3D 画面で選んだものを、左の一覧でも光らせる(V1 と同じ。逆も同じ)。
    HighlightTreeForSelection();tick("tree");
    if(V2ToolSelection::Active(*viewport_)){V2ToolSelection::Refresh(*viewport_);RefreshCommandVisibility();return;}
    if (gptFabrication_ != nullptr) { gptFabrication_->HandleSelectionChanged(); }
    if (gptSurface_ != nullptr) { gptSurface_->HandleSelectionChanged(); }
    if (loopFaces_ != nullptr) { loopFaces_->HandleSelectionChanged(); }
    // 構えている命令があれば、そろったかを見る。
    RefreshPendingCommand(false);
    // 面作成中の素のクリックは、**いまの欄**へ入る(もう一度押すと外れる)。
    // 「選んでから右棚の追加ボタンを押す」を基本操作にしない。
    RefreshSurfaceForSelectionChange();
    // 近似中の素のクリックは対象へ入る(面と立体だけ)。押すたびに候補を作り直す。
    RefreshApproxForSelectionChange();
    // 足す引く中の素のクリックは土台 → 相手の順に入る(押し直すと外れる)。
    RefreshBooleanForSelectionChange();
    // 厚み中の素のクリックは面の欄へ入る(押し直すと外れる)。
    RefreshThickenForSelectionChange();
    // 面の編集中の素のクリックは縁・面・線の欄へ入る(押し直すと外れる)。
    if (surfaceEdit_ != nullptr) {
        surfaceEdit_->HandleSelectionChanged();
    }
    // 立体を作る最中の素のクリックは輪郭・軸・経路・相手の欄へ(種類で決まる。押し直すと外れる)。
    if (solidTool_ != nullptr) {
        solidTool_->HandleSelectionChanged();
    }
    // 辺の丸め・面取りの最中の素のクリックは、部品と一番近い辺へ(押し直すと外れる)。
    if (edgeFinishTool_ != nullptr) {
        edgeFinishTool_->HandleSelectionChanged();
    }
    // シェル・分割の最中の素のクリックは、部品(シェルなら一番近い面)へ(押し直すと外れる)。
    if (shellSplitTool_ != nullptr) {
        shellSplitTool_->HandleSelectionChanged();
    }
    // 製作モードで部材を押したら「対象部材」欄をその番号にする(F-05/06/07)。
    RefreshFabricationPartPickForSelectionChange();
    // 選んだ近似モデルと対象部材を、3D の下見で色分けする。
    RefreshFoldEmphasis();tick("tools-fold");
    // 下見を出している最中なら、写しと下見を選択に合わせる(§9)。
    RefreshExtrudeForSelectionChange();
    RefreshExportCounts();tick("export-counts");
    RefreshProcessContextFromSelection();tick("process-context");
    RefreshMeasurements();tick("measurement");
    RefreshEditDock();tick("numeric-edit");
    RefreshCornerDock();tick("corner");
    V2OffsetTool::Refresh(*this);tick("offset");
    RefreshFabricationDock();tick("fabrication");
    const auto commandFacts=BuildFactsForCommands();
    RefreshPartDock(&commandFacts);tick("command-facts-part");
    // 作業平面の棚は「いま何を選んでいるか」で作れるかが変わる。
    RefreshWorkPlaneDock();tick("workplane");
    // 面の解析は選んだ面を塗る(選んでいなければ全部の面)。
    if (surfaceAnalysis_ != nullptr) {
        surfaceAnalysis_->Refresh();
    }
    // 選択が変われば押せるものも変わる。押せる形を選択に付いてこさせる。
    RefreshCommandVisibility(&commandFacts);tick("commands");
}

