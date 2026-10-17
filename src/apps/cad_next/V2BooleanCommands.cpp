#include <QStringList>
#include "V2FaceActions.h"
#include "V2ImageTool.h"
#include "V2CurvedEmbossTool.h"
#include "V2ProjectionTool.h"
//! 「足す・引く」を道具から始める(引継ぎ 2026-09-17 の 4)。
//!
//! 足す/引くを押す → 土台待ち → 3D で部品を押すと土台に入り、自動で相手待ちへ →
//! 相手を押す → **実際に足し引きして** 出来上がりの稜線を下見 → Enter で確定。
//! 確定は下見に使った形をそのまま文書へ入れる。土台と相手を隠すのも含めて
//! 1つのまとまり(1回で戻せる)。
//!
//! 土台・相手は 3D の札(TARGET / TOOL)と右の棚の欄に別々に出て、
//! 「ここへ選ぶ」で選び直せ、「解除」で空にでき、3D で押し直すと外れる。
//! 何がどの欄に入るかは core(app/BooleanInputState)が決める。

#include "V2MainWindow.h"
#include "V2GeometryJob.h"
#include "V2OutputTool.h"
#include "V2GptSurfaceTool.h"
#include "V2GptFabricationTool.h"
#include "V2EdgeFinishTool.h"
#include "V2LoopFacesTool.h"
#include "V2Ribbon.h"
#include "V2ShellSplitTool.h"
#include "V2SolidTool.h"
#include "V2SurfaceAnalysisTool.h"
#include "V2SurfaceEditTool.h"

#include "V2BooleanDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/BooleanInputState.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/ToolController.h"

#include "V2CornerDock.h"

#include <QString>
#include <QVariant>
#include <QComboBox>

#include <algorithm>
#include <string>
#include <vector>

using kachakacha::v2::app::BooleanSlot;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

namespace {

[[nodiscard]] std::vector<EntityId> Missing(const std::vector<EntityId>& left,
    const std::vector<EntityId>& right)
{
    std::vector<EntityId> out;
    for (const EntityId& id : left) {
        if (std::find(right.begin(), right.end(), id) == right.end()) {
            out.push_back(id);
        }
    }
    return out;
}
bool AcceptTrimEntity(kachakacha::v2::app::BooleanKind mode,EntityKind kind) {
    return kind==EntityKind::Part || (mode==kachakacha::v2::app::BooleanKind::SplitOverlap
        && (kind==EntityKind::GuideSurface||kind==EntityKind::Wire||kind==EntityKind::Point||kind==EntityKind::WorkPlane));
}

bool OpenTemporaryTool(V2MainWindow& window,std::string_view id)
{
    if (id == "wire.project_surface" || id == "wire.wrap_project") { V2ProjectionTool::Open(window,id=="wire.wrap_project"); return true; }
    if (id == "part.curved_emboss") { V2CurvedEmbossTool::Open(window); return true; } if (id == "image.place") { V2ImageTool::Open(window); return true; }
    if (V2FaceActions::Run(window,id)) return true;
    if (id == "export.selection" || id == "export.kcd" || id == "output.copy" || id == "export.stl" || id == "export.step") { V2OutputTool::Open(window,id=="export.stl"?0:id=="export.step"?1:id=="output.copy"?3:2); return true; }
    return false;
}

QString ConfigureCornerTool(V2CornerDock& dock, std::string_view id)
{
    dock.SetPolylineMode(id == "wire.corner_chamfer" || id == "wire.corner_fillet");
    dock.SetFillet(id == "wire.fillet" || id == "wire.corner_fillet");
    return dock.Choice().polyline
        ? QStringLiteral("加工するワイヤーを選んでください。辺を押すとそのワイヤー全体の角を下見します。Enterで確定、Escで取消。")
        : QStringLiteral("加工する辺を順に選んでください。2辺では残す側を押してください。Enterで確定、Escで取消。");
}

} // namespace

//! 自分の棚を持つ道具(立体を作る・辺の丸め面取り・シェル分割・面にする)は 1 つだけ構える。
//! 別の道具を押したら、構えていた道具はやめる(棚が前の道具のまま残り、押しが両方へ入るため)。


//! 選択済みの線を保持して、GPT版の独立した面生成を構える。
void V2MainWindow::BeginGptSurface()
{
    const auto selected = viewport_->Selection();
    ClearPendingCommand();
    if (surfaceShelfShown_) { EndSurfacePreview(); }
    if (approxShelfShown_) { EndApprox(); }
    if (booleanShelfShown_) { EndBoolean(); }
    if (ThickenShelfShown()) { EndThicken(); }
    if (surfaceEdit_ != nullptr && surfaceEdit_->Active()) { surfaceEdit_->End(); }
    SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    EndOwnedToolsBut(gptSurface_.get());
    viewport_->SetSelection(selected);
    gptSurface_->Begin();
}

//! 道具の棚を構えてから相手を選ぶ命令。引き受けたらArmCommandは通さない。
bool V2MainWindow::BeginToolFirstCommand(std::string_view id)
{
    if (IsArrayCommand(id)) { RunArrayCommand(id); return true; }
    if (OpenTemporaryTool(*this,id)) return true;
    if (id == "part.overlap_inspect") { EndArmedTools(); ShowOverlapBrowser(); return true; }
    if (id == "part.extrude" && !extrudeShelfShown_) {
        EndArmedTools();
        SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        // Continue into the regular pending-input / preview path.
    }
    if (id == "surface.gpt_create") { BeginGptSurface(); return true; }
    if (id == "surface.create" && !surfaceShelfShown_) {
        ClearPendingCommand();
        RunGuideCommand(id);
        return true;
    }
    // 近似も道具から始める。何も選んでいなくても棚が出て、3D で対象を押せる。
    // 構えている間の2度目は確定。
    if (id == "fabrication.create" || id == "fabrication.preview_update") {
        ClearPendingCommand();
        RunFabricationCommand(id);
        return true;
    }
    if (id == "part.boolean_add" || id == "part.boolean_cut" || id == "part.boolean_intersect"
        || id == "part.contact_wire" || id == "part.trim_overlap" || id == "part.split_overlap") {
        ClearPendingCommand(); RunBooleanTool(BooleanKindForCommand(id));
        return true;
    }
    // 厚みも未選択から構え、選択済みなら欄へ取り込む。
    if (id == "part.thicken" || id == "part.thicken_to_plane") {
        ClearPendingCommand();
        if(id=="part.thicken")RunThickenTool();
        else ChooseThickenToPlane();
        return true;
    }
    // 空の選択でも専用の棚を構え、選択の追加・解除ごとに輪を読み直す。
    if (id == "surface.from_lines") {
        ClearPendingCommand();
        EndOwnedToolsBut(loopFaces_.get());
        const auto selection = viewport_->Selection();
        SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        viewport_->SetSelection(selection);
        loopFaces_->Start();
        if (ribbon_ != nullptr) ribbon_->RevealCommand(id);
        return true;
    }
    // 立体も未選択から構え、3Dで選んだ線を入力へ追加する。
    if (solidTool_ != nullptr && V2SolidTool::Handles(id)) {
        ClearPendingCommand();
        EndOwnedToolsBut(solidTool_.get());
        solidTool_->Begin(id);
        return true;
    }
    // 辺の丸め・面取り(P-12)も道具から始める。3D で部品の辺の近くを押す。
    if (edgeFinishTool_ != nullptr && V2EdgeFinishTool::Handles(id)) {
        ClearPendingCommand();
        EndOwnedToolsBut(edgeFinishTool_.get());
        edgeFinishTool_->Begin(id);
        return true;
    }
    // シェル・分割(P-13)も道具から始める。3D で部品(シェルなら開けたい面)を押す。
    if (shellSplitTool_ != nullptr && V2ShellSplitTool::Handles(id)) {
        ClearPendingCommand();
        EndOwnedToolsBut(shellSplitTool_.get());
        shellSplitTool_->Begin(id);
        return true;
    }
    // 面の編集も道具から始める。何も選んでいなくても棚が出て、3D で面・縁・線を押せる。
    if (surfaceEdit_ != nullptr && surfaceEdit_->Handles(id)) {
        ClearPendingCommand();
        EndOwnedToolsBut(surfaceEdit_.get());
        surfaceEdit_->Begin(id);
        RefreshRightShelves();
        return true;
    }
    // 面の解析は、いつでも開ける(面を作る・面の編集の下見を塗るので、その道具は止めない)。
    if (surfaceAnalysis_ != nullptr && surfaceAnalysis_->Handles(id)) {
        surfaceAnalysis_->Run(id);
        return true;
    }
    // 回転体。面を作るの道具を 作り方 = 回転体 で構える(断面 → 軸 → 下見 → Enter)。
    if (id == "guide.revolve") {
        ClearPendingCommand(); RunRevolveTool();
        return true;
    }
    // C面取り / R丸め。線が2本そろっていなければ、面取りの道具を持って拾うのを待つ。
    // 両方の入口で設定と下見を先に表示する。
    if (id == "wire.chamfer" || id == "wire.fillet"
        || id == "wire.corner_chamfer" || id == "wire.corner_fillet") {
        ClearPendingCommand();
        const QString prompt = ConfigureCornerTool(*cornerDock_, id);
        SelectTool(kachakacha::v2::modeling::DrawingTool::ChamferOrFilletPair);
        SetStatus(prompt);
        return true;
    }
    return false;
}

//! 作り方からカーネルの演算へ。開き直し(V2RebuildCommands)も同じものを使う。
kachakacha::v2::kernel::BooleanOperation V2MainWindow::KernelBooleanOperation(
    kachakacha::v2::app::BooleanKind kind)
{
    using kachakacha::v2::app::BooleanKind;
    using kachakacha::v2::kernel::BooleanOperation;
    return kind == BooleanKind::Cut ? BooleanOperation::Difference
        : kind == BooleanKind::Intersect ? BooleanOperation::Intersection
                                         : BooleanOperation::Union;
}

//! 命令の名前から作り方(足す / 引く / 交差)。
kachakacha::v2::app::BooleanKind V2MainWindow::BooleanKindForCommand(std::string_view id)
{
    using kachakacha::v2::app::BooleanKind;
    if (id == "part.contact_wire") return BooleanKind::ContactWire;
    if (id == "part.trim_overlap") return BooleanKind::TrimOverlap;
    if (id == "part.split_overlap") return BooleanKind::SplitOverlap;
    return id == "part.boolean_cut" ? BooleanKind::Cut
        : id == "part.boolean_intersect" ? BooleanKind::Intersect
                                         : BooleanKind::Add;
}

//! 「足す」「引く」「交差」を押した。構えていなければ構え、構えていれば作り方を切り替える。
void V2MainWindow::RunBooleanTool(kachakacha::v2::app::BooleanKind kind)
{
    if (booleanShelfShown_) {
        ChooseBooleanOperation(kind);
        return;
    }
    SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    // 選んであった部品は、選んだ順に土台・相手へ入れる(選んでから押す道も残す)。
    booleanInput_ = kachakacha::v2::app::BooleanInputState{};
    booleanInput_.kind = kind;
    for(const auto name:{"trimTargetScope","trimToolScope"})if(auto* scope=findChild<QComboBox*>(name)) {
        const bool blocked=scope->blockSignals(true);scope->setCurrentIndex(0);scope->blockSignals(blocked);
    }
    const auto& document = session_->GetDocument();
    for (const EntityId& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && AcceptTrimEntity(kind,entity->kind)) {
            booleanInput_ = kachakacha::v2::app::WithBooleanPick(booleanInput_, id);
        }
    }
    for(const auto& ref:viewport_->Selection().ordered)if(ref.pickedFaceIndex)
        booleanDock_->setProperty(("trimFace_"+ref.entityId.ToString()).c_str(),static_cast<int>(*ref.pickedFaceIndex));
    booleanShelfShown_ = true;
    RefreshRightShelves();
    viewport_->SetToolPickActive(true);
    viewport_->SetToolPickToggle(true);
    MirrorBooleanToSelection();
    RefreshBooleanAll();
    SetStatus(QStringLiteral("%1: 部品を2つ以上(土台 1 つと相手 1 個以上。相手は何個でも)、3D で順に押してください。"
                             "押し直すと外れます。Enter で確定、Esc でやめます。\n%2")
            .arg(QString::fromUtf8(
                     std::string(kachakacha::v2::app::BooleanOperationLabelJa(kind)).c_str()),
                QString::fromStdString(kachakacha::v2::app::BooleanHintJa(booleanInput_))));
}

//! 3D の選択の印を、土台・相手に合わせる。欄が正本。
void V2MainWindow::MirrorBooleanToSelection()
{
    booleanMirror_ = kachakacha::v2::app::BooleanEntries(booleanInput_);
    kachakacha::v2::app::SelectionSet mirrored;
    mirrored.entityIds = booleanMirror_;
    for (const EntityId& id : booleanMirror_) {
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = id;
        const auto face=booleanDock_->property(("trimFace_"+id.ToString()).c_str());
        if(face.isValid()) {ref.kind=kachakacha::v2::app::SelectionElementKind::Face;ref.pickedFaceIndex=face.toUInt();}
        mirrored.ordered.push_back(ref);
    }
    booleanMirroring_ = true;
    viewport_->SetSelection(std::move(mirrored));
    booleanMirroring_ = false;
}

//! 3D の選択が変わった。足された部品は次の欄へ、外れた部品は欄からも外す。
void V2MainWindow::RefreshBooleanForSelectionChange()
{
    if (!booleanShelfShown_ || booleanMirroring_ || viewport_ == nullptr) {
        return;
    }
    const auto& now = viewport_->Selection().entityIds;
    const auto added = Missing(now, booleanMirror_);
    const auto removed = Missing(booleanMirror_, now);
    const auto& document = session_->GetDocument();
    bool changed = false;
    for(const auto& ref:viewport_->Selection().ordered)if(ref.pickedFaceIndex) {
        const auto key="trimFace_"+ref.entityId.ToString();const auto value=static_cast<int>(*ref.pickedFaceIndex);
        changed=changed||booleanDock_->property(key.c_str())!=QVariant(value);
        booleanDock_->setProperty(key.c_str(),value);
    }
    for (const EntityId& id : added) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && AcceptTrimEntity(booleanInput_.kind,entity->kind)) {
            booleanInput_ = kachakacha::v2::app::WithBooleanPick(booleanInput_, id);
            changed = true;
        }
    }
    if (!removed.empty()) {
        booleanInput_ = kachakacha::v2::app::WithoutBooleanEntries(booleanInput_, removed);
        changed = true;
    }
    MirrorBooleanToSelection();   // 受けなかったもの(線など)を選択に残さない。
    if (changed) {
        RefreshBooleanAll();
    }
}

//! 土台と相手がそろっていれば **実際に足し引きして** 稜線を下見に出す。文書へは書かない。
void V2MainWindow::RefreshBooleanPreview()
{
    booleanOutcome_ = kachakacha::v2::app::BooleanPreviewOutcome{};
    if(booleanBuilt_)kachakacha::v2::kernel::ReleaseShape(*booleanBuilt_);
    booleanBuilt_.reset();
    viewport_->HideToolPreview();
    if (!kachakacha::v2::app::BooleanReady(booleanInput_)) {
        return;
    }
    booleanOutcome_.evaluated = true;
    const auto base = partShapes_.find(booleanInput_.target.ToString());
    if (base == partShapes_.end()) {
        booleanOutcome_.refusalJa = "選んだ部品の立体がまだありません";
        return;
    }
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    // 相手を順に足す・引く(何個でも)。1 つでもできなければ、何も作らない。
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> tools;
    for (const EntityId& tool : booleanInput_.tools) {
        const auto other = partShapes_.find(tool.ToString());
        if (other == partShapes_.end()) {
            booleanOutcome_.refusalJa = "選んだ部品の立体がまだありません";
            return;
        }
        tools.push_back(other->second);
    }
    const auto revision=session_->GetDocument().Revision();
    const auto built = V2GeometryJob::Boolean(this,
        KernelBooleanOperation(booleanInput_.kind), base->second, tools, tolerance);
    if(session_->GetDocument().Revision()!=revision) {
        if(built.HasValue())kachakacha::v2::kernel::ReleaseShape(built.Value().handle);
        booleanOutcome_.refusalJa="文書が変更されたため、古い計算結果を採用しませんでした。";
        return;
    }
    if (!built.HasValue()) {
        booleanOutcome_.refusalJa = built.FirstSummaryJa();
        if(!built.Diagnostics().empty()&&!built.Diagnostics().front().detailsJa.empty())
            booleanOutcome_.refusalJa += "\n" + built.Diagnostics().front().detailsJa;
        return;
    }
    const auto current = built.Value().handle;
    booleanOutcome_.available = true;
    booleanOutcome_.volumeMm3 = built.Value().volumeMm3;
    booleanOutcome_.previousVolumeMm3 = built.Value().previousVolumeMm3;
    booleanBuilt_ = current;
    // 出来上がりの稜線。画面に出すのは、確定で文書へ入るその形。
    const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(current);
    if (mesh.HasValue()) {
        viewport_->ShowToolPreview(mesh.Value().edges);
    }
}

//! 棚・札・一番下の一行を、いまの入力に合わせる。
void V2MainWindow::RefreshBooleanDock()
{
    if (booleanDock_ == nullptr) {
        return;
    }
    const auto& document = session_->GetDocument();
    const auto nameOf = [&document](const EntityId& id) {
        const auto* entity = id.IsNil() ? nullptr : document.FindEntity(id);
        return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                                 : std::string("名前のないもの");
    };
    const bool previewShown = !viewport_->ToolPreview().empty();
    std::vector<QString> lines;
    for (const std::string& line : kachakacha::v2::app::BooleanStatusLinesJa(booleanInput_,
             booleanOutcome_, previewShown)) {
        lines.push_back(QString::fromStdString(line));
    }
    // 相手の名前を並べる(何個でも)。
    std::string tools;
    QStringList toolNames;
    for (const EntityId& id : booleanInput_.tools) {
        toolNames.push_back(QString::fromStdString(nameOf(id)));
        tools += (tools.empty() ? "" : " / ") + nameOf(id);
    }
    booleanDock_->ShowInput(booleanInput_, QString::fromStdString(nameOf(booleanInput_.target)),
        QString::fromStdString(tools), lines, booleanBuilt_.has_value());
    booleanDock_->SetToolItems(toolNames);
    booleanDock_->removeTool = [this](int row) {
        if (row < 0 || row >= static_cast<int>(booleanInput_.tools.size())) return;
        booleanInput_ = kachakacha::v2::app::WithoutBooleanEntries(booleanInput_, {booleanInput_.tools[row]});
        MirrorBooleanToSelection(); RefreshBooleanAll();
    };
    ShowToolFooter(booleanShelfShown_
            ? QString::fromStdString(kachakacha::v2::app::BooleanFooterLine(booleanInput_,
                  nameOf(booleanInput_.target), tools, booleanOutcome_, previewShown))
            : QString());
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    if (!booleanInput_.target.IsNil()) {
        labels.push_back({booleanInput_.target,
            std::string(kachakacha::v2::app::BooleanSlotKey(BooleanSlot::Target))});
    }
    for (std::size_t index = 0; index < booleanInput_.tools.size(); ++index) {
        const std::string key(kachakacha::v2::app::BooleanSlotKey(BooleanSlot::Tool));
        labels.push_back({booleanInput_.tools[index],
            booleanInput_.tools.size() == 1 ? key : key + " " + std::to_string(index + 1)});
    }
    ShowRoleLabels(labels);
}

void V2MainWindow::RefreshBooleanAll()
{
    if (RefreshContactPreview()) return;
    RefreshBooleanPreview();
    RefreshBooleanDock();
}

//! 「ここへ選ぶ」。次の 3D クリックがその欄へ入る。
void V2MainWindow::ActivateBooleanSlot(BooleanSlot slot)
{
    booleanInput_ = kachakacha::v2::app::WithActiveBooleanSlot(booleanInput_, slot);
    RefreshBooleanDock();
    SetStatus(QStringLiteral("%1: 次のクリックは「%2」へ入ります。")
            .arg(QString::fromUtf8(std::string(kachakacha::v2::app::BooleanOperationLabelJa(
                                                   booleanInput_.kind))
                                       .c_str()),
                QString::fromUtf8(
                    std::string(kachakacha::v2::app::BooleanSlotNameJa(slot)).c_str())));
}

//! 「解除」。欄を空にし、次のクリックをその欄へ。
void V2MainWindow::ClearBooleanSlot(BooleanSlot slot)
{
    booleanInput_ = kachakacha::v2::app::WithBooleanSlotCleared(booleanInput_, slot);
    MirrorBooleanToSelection();
    RefreshBooleanAll();
}

//! 足す ⇄ 引く ⇄ 交差。入力はそのまま、下見だけ作り直す。
void V2MainWindow::ChooseBooleanOperation(kachakacha::v2::app::BooleanKind kind)
{
    if(booleanInput_.kind==kind)return;
    booleanInput_.kind = kind;
    RefreshBooleanAll();
    SetStatus(QStringLiteral("%1: 部品を2つ以上(土台 1 つと相手 1 個以上。相手は何個でも)、3D で順に押してください。\n%2")
            .arg(QString::fromUtf8(
                     std::string(kachakacha::v2::app::BooleanOperationLabelJa(kind)).c_str()),
                QString::fromStdString(kachakacha::v2::app::BooleanHintJa(booleanInput_))));
}

//! やめる。文書は始める前とまったく同じ。
void V2MainWindow::EndBoolean(bool keepTool)
{
    contactBase_.reset();
    contactBuilt_.reset();
    booleanShelfShown_ = keepTool;
    if(booleanBuilt_)kachakacha::v2::kernel::ReleaseShape(*booleanBuilt_);
    booleanBuilt_.reset();
    booleanOutcome_ = kachakacha::v2::app::BooleanPreviewOutcome{};
    booleanMirror_.clear();
    if (viewport_ != nullptr) {
        viewport_->HideToolPreview();
        viewport_->HideToolRoleLabels();
        viewport_->SetToolPickActive(keepTool);
        viewport_->SetToolPickToggle(keepTool);
    }
    ShowToolFooter(QString());
    RefreshRightShelves();
}

//! 確定。**下見に使った形をそのまま**文書へ入れる。土台と相手を隠すのも含めて1回で戻せる。
void V2MainWindow::ConfirmBoolean()
{
    if (ConfirmContact()) return;
    if (!booleanBuilt_.has_value()) {
        SetStatus(QStringLiteral("%1: まだ作れません。土台と相手を入れてください。")
                .arg(QString::fromUtf8(std::string(kachakacha::v2::app::BooleanOperationLabelJa(
                                                       booleanInput_.kind))
                                           .c_str())));
        RefreshBooleanDock();
        return;
    }
    const std::string labelText(kachakacha::v2::app::BooleanOperationLabelJa(booleanInput_.kind));
    const char* label = labelText.c_str();
    kachakacha::v2::domain::BooleanDefinition definition;
    definition.mode = kachakacha::v2::app::BooleanModeOf(booleanInput_.kind);   // 0 足す 1 引く 2 交差
    definition.targets.push_back(booleanInput_.target);
    definition.tools = booleanInput_.tools;   // 相手は何個でも(保存の形は前から並び)
    std::vector<EntityId> used{booleanInput_.target};
    used.insert(used.end(), booleanInput_.tools.begin(), booleanInput_.tools.end());
    const auto outcome = booleanOutcome_;
    const auto handle = *booleanBuilt_;
    kachakacha::v2::document::Document::Transaction transaction(session_->GetDocument(), label);
    // 出来た形の辺は、いまは持たない。持てるようになるまで、元の辺を使い回さない。
    const auto madeId = AddPartFeature(kachakacha::v2::domain::FeatureType::Boolean,
        std::move(definition), handle, {}, label, used);
    if (madeId.IsNil()) {
        return;   // まとまりは戻る(Transaction が捨てる)。
    }
    // 使い切った2つは隠す。消すと、作り方をたどれなくなる。
    const auto hidden = session_->GetDocument().Run(kachakacha::v2::document::SetVisibilityCommand(
        used, kachakacha::v2::domain::Visibility::Hidden));
    if (!hidden.committed) {
        ReportDiagnostics(hidden.diagnostics);
        return;
    }
    if (!transaction.Commit()) {
        return;
    }
    booleanBuilt_.reset(); // Ownership moved to the committed part.
    EndBoolean();
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("%1: 体積が %2 mm3 から %3 mm3 になりました。")
            .arg(QString::fromUtf8(label))
            .arg(outcome.previousVolumeMm3, 0, 'f', 4)
            .arg(outcome.volumeMm3, 0, 'f', 4));
}
