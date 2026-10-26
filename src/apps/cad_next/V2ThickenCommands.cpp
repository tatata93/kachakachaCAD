//! 「厚み」を道具から始める(指示書 matrix P-10)。
//!
//! 厚みを押す → 面待ち → 3D で形状ガイドの面を押すと入る(押し直すと外れる) →
//! 作り方(外側/中央/内側/平面まで)を選ぶ → **実際に厚みを付けて** 出来上がりの稜線を
//! 下見 → Enter で確定。確定は下見に使った形をそのまま文書へ入れる
//! (V2BooleanCommands.cpp と同じ役目分け)。
//!
//! 面は 3D の札(SURFACE)と右の棚の欄に出て、「選び直す」で外せ、
//! 3D で押し直しても外れる。何が欄に入るかは core(app/ThickenInputState)が決める。

#include "V2MainWindow.h"
#include "V2ThickenPreviewState.h"
#include "V2GeometryJob.h"
#include "V2Calculation.h"
#include "V2SolidOutputPreview.h"

#include "V2ThickenDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"

#include <QString>
#include <QCheckBox>
#include <QScopeGuard>
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/document/Commands.h"
#include <QScopedValueRollback>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::fabrication::ThicknessPlacement;

namespace {
void ReleaseThickenPreview(V2ThickenPreviewState& state)
{
    for(const auto handle:state.built)kachakacha::v2::kernel::ReleaseShape(handle);
    state.built.clear();state.builtMeshes.clear();state.builtWires.clear();
}
auto BoundaryWiresFor(const V2ThickenPreviewState& state,
    kachakacha::v2::modeling::KernelShapeHandle handle,double tolerance)
{
    using namespace kachakacha::v2;
    using Result=base::Result<std::vector<std::vector<geometry::CurveSegment>>>;
    const auto found=state.builtWires.find(handle.value);
    if(found==state.builtWires.end())return kernel::OutputBoundaryWires(handle,tolerance);
    if(found->second.empty())return Result::Failure(base::MakeWarning("EXP-W002",
        "厚みの境界ワイヤーを作れません。","ソリッドのみを選んでください。"));
    return Result::Success(found->second);
}
void AddThickenMeasurements(kachakacha::v2::app::ThickenPreviewOutcome& outcome,
    const kachakacha::v2::kernel::ThickenedSolid& built,bool first)
{
    outcome.volumeMm3+=built.volumeMm3;
    outcome.thicknessMm=std::max(outcome.thicknessMm,built.thicknessMm);
    outcome.corrected=outcome.corrected||built.corrected;
    outcome.normalThicknessMinMm=first?built.normalThicknessMinMm:std::min(outcome.normalThicknessMinMm,built.normalThicknessMinMm);
    outcome.normalThicknessMaxMm=std::max(outcome.normalThicknessMaxMm,built.normalThicknessMaxMm);
    outcome.maxOffsetDeviationMm=std::max(outcome.maxOffsetDeviationMm,built.maxOffsetDeviationMm);
    outcome.rmsOffsetDeviationMm=std::max(outcome.rmsOffsetDeviationMm,built.rmsOffsetDeviationMm);
}
}

//! 「厚み」を押す。構えていなければ構え、構えていれば何もしない
//! (足す・引くと違い、操作の切り替えが無いので2度目に意味が無い)。
void V2MainWindow::RunThickenTool(bool toPlane)
{
    if (thicken_->shelfShown) {
        // 構えている間の2度目は確定(回転体・足す引くと同じ文法)。
        ConfirmThicken();
        return;
    }
    EndArmedTools();
    // 選んであった形状ガイドの面は、そのまま欄へ入れる(選んでから押す道も残す)。
    thicken_->input = kachakacha::v2::app::ThickenInputState{};
    thicken_->input.toPlane = toPlane;
    thicken_->input.placement = thicknessPlacement_;
    thicken_->input.thicknessMm = kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(), kachakacha::v2::app::ParameterId::ThickenThicknessMm);
    const auto& document = session_->GetDocument();
    for (const EntityId& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::GuideSurface) {
            thicken_->input = kachakacha::v2::app::WithThickenPick(thicken_->input, id);   // 何枚でも
        }
        if(toPlane && entity && entity->kind==EntityKind::WorkPlane)thicken_->input.targetPlane=id;
    }
    thicken_->shelfShown = true;
    RefreshRightShelves();
    viewport_->SetToolPickActive(true);
    viewport_->SetToolPickToggle(true);
    MirrorThickenToSelection();
    BindThickenInputs();
    RefreshThickenAll();
    SetStatus(QStringLiteral("厚み: 3D で形状ガイドの面を押してください(何枚でも。押し直すと外れます)。"
                             "作り方を選んで、Enter で確定、Esc でやめます。"));
}

//! 3D の選択の印を、面の欄に合わせる。欄が正本。
void V2MainWindow::MirrorThickenToSelection()
{
    thicken_->mirror = thicken_->input.surfaces;
    kachakacha::v2::app::SelectionSet mirrored;
    for (const EntityId& id : thicken_->mirror) {
        mirrored.entityIds.push_back(id);
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = id;
        mirrored.ordered.push_back(ref);
    }
    thicken_->mirroring = true;
    viewport_->SetSelection(std::move(mirrored));
    thicken_->mirroring = false;
}

//! 3D の選択が変わった。形状ガイドの面が足されれば欄へ、外れれば欄からも外す(何枚でも)。
void V2MainWindow::RefreshThickenForSelectionChange()
{
    if (!thicken_->shelfShown || thicken_->mirroring || viewport_ == nullptr) {
        return;
    }
    const auto& now = viewport_->Selection().entityIds;
    // 押した当人が分かるなら、入っていれば外し、入っていなければ足す。
    if (const auto pick = viewport_->TakeLastToolPick(); pick.has_value()) {
        const auto* entity = session_->GetDocument().FindEntity(*pick);
        if (entity != nullptr && entity->kind == EntityKind::GuideSurface) {
            thicken_->input = kachakacha::v2::app::WithThickenPick(thicken_->input, *pick);
            MirrorThickenToSelection();
            RefreshThickenAll();
            return;
        }
    }
    bool changed = false;
    for (const EntityId& id : thicken_->mirror) {
        if (std::find(now.begin(), now.end(), id) == now.end()) {
            thicken_->input = kachakacha::v2::app::WithoutThickenSurface(thicken_->input, id);
            changed = true;
        }
    }
    const auto& document = session_->GetDocument();
    for (const EntityId& id : now) {
        if (std::find(thicken_->mirror.begin(), thicken_->mirror.end(), id) != thicken_->mirror.end()) {
            continue;   // すでに映しているもの。
        }
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::GuideSurface) {
            thicken_->input = kachakacha::v2::app::WithThickenPick(thicken_->input, id);
            changed = true;
        }
    }
    MirrorThickenToSelection();   // 受けなかったもの(部品・線など)を選択に残さない。
    if (changed) {
        RefreshThickenAll();
    }
}

//! 面と作り方がそろっていれば **実際に厚みを付けて** 稜線を下見に出す。文書へは書かない。
void V2MainWindow::RefreshThickenPreview()
{
    const auto input=thicken_->input;
    thicken_->outcome = kachakacha::v2::app::ThickenPreviewOutcome{};
    ReleaseThickenPreview(*thicken_);
    thicken_->builtEdges.clear();
    thicken_->builtThickness.clear();
    thicken_->builtMesh = {};
    viewport_->HideToolPreview();
    if (!kachakacha::v2::app::ThickenReadyToBuild(thicken_->input)) {
        return;
    }
    thicken_->outcome.evaluated = true;
    const auto tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto revision = session_->GetDocument().Revision();
    std::optional<kachakacha::v2::modeling::WorkPlaneFrame> frame;
    if (thicken_->input.toPlane) {
        frame = WorkPlaneFrameOf(thicken_->input.targetPlane);
        if (!frame.has_value() && thicken_->input.targetFaceBrep.empty() && !guideShapes_.contains(thicken_->input.targetPlane.ToString())) {
            thicken_->outcome.refusalJa = "相手の作業平面の形がまだありません";
            return;
        }
    }
    // 面ごとに実際に厚みを付ける。1 枚でも付けられなければ、どれも作らない(半分だけ
    // 作れたことにしない)。
    RefreshThickenDock();
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines;
    for (const EntityId& id : input.surfaces) {
        const auto found = guideShapes_.find(id.ToString());
        const auto* entity = session_->GetDocument().FindEntity(id);
        const std::string name = entity != nullptr ? entity->displayName : std::string("面");
        if (found == guideShapes_.end()) {
            thicken_->outcome.refusalJa = name + ": 面の形がまだありません";
            ReleaseThickenPreview(*thicken_);
            return;
        }
        kachakacha::v2::modeling::KernelShapeHandle stop;
        const bool snapshot = !input.targetFaceBrep.empty();
        if (snapshot) {
            const auto restored = kachakacha::v2::kernel::RestoreOutputShape(input.targetFaceBrep);
            if (!restored.HasValue()) { thicken_->outcome.refusalJa=restored.FirstSummaryJa(); return; }
            stop=restored.Value();
        } else if (const auto end=guideShapes_.find(input.targetPlane.ToString());end!=guideShapes_.end()) stop=end->second;
        kachakacha::v2::modeling::ShapeMesh preparedMesh;
        std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> preparedWires;
        const auto built = input.toPlane ? V2Calculation::Call(this,"終端の面まで計算しています…",
            [](const auto&... args){return kachakacha::v2::kernel::ThickenSurfaceToFace(args...);},
            found->second,stop,frame,input.targetOffsetMm,tolerance)
            : V2GeometryJob::Thicken(this,found->second,input.thicknessMm,
                input.placement,frame,tolerance,true,input.correctOpposite,&preparedMesh,false,&preparedWires);
        if(snapshot)kachakacha::v2::kernel::ReleaseShape(stop);
        if(thicken_->refreshPending||!thicken_->shelfShown||input.surfaces!=thicken_->input.surfaces
            ||input.thicknessMm!=thicken_->input.thicknessMm||input.placement!=thicken_->input.placement
            ||input.correctOpposite!=thicken_->input.correctOpposite
            ||input.toPlane!=thicken_->input.toPlane||input.targetPlane!=thicken_->input.targetPlane
            ||input.targetOffsetMm!=thicken_->input.targetOffsetMm||input.targetFaceBrep!=thicken_->input.targetFaceBrep) {
            if(built.HasValue())kachakacha::v2::kernel::ReleaseShape(built.Value().handle);
            ReleaseThickenPreview(*thicken_);return;
        }
        if(session_->GetDocument().Revision()!=revision) {
            if(built.HasValue())kachakacha::v2::kernel::ReleaseShape(built.Value().handle);
            ReleaseThickenPreview(*thicken_);
            thicken_->outcome.refusalJa="文書が変更されたため、古い計算結果を採用しませんでした。";
            return;
        }
        if (!built.HasValue()) {
            thicken_->outcome.refusalJa = (thicken_->input.surfaces.size() > 1 ? name + ": " : std::string())
                + built.FirstSummaryJa();
            if(!built.Diagnostics().empty()&&!built.Diagnostics().front().detailsJa.empty())
                thicken_->outcome.refusalJa += "\n" + built.Diagnostics().front().detailsJa;
            ReleaseThickenPreview(*thicken_);
            thicken_->builtEdges.clear();
            thicken_->builtThickness.clear();
            return;
        }
        for(const auto& note:built.Diagnostics())thicken_->outcome.boundaryNotesJa.push_back(note.summaryJa);
        AddThickenMeasurements(thicken_->outcome,built.Value(),thicken_->built.empty());
        thicken_->built.push_back(built.Value().handle);
        thicken_->builtEdges.push_back(built.Value().edges);
        thicken_->builtThickness.push_back(built.Value().thicknessMm);
        if(!input.toPlane)thicken_->builtWires.emplace(built.Value().handle.value,std::move(preparedWires));
        // 出来上がりの稜線。画面に出すのは、確定で文書へ入るその形。
        auto mesh = preparedMesh.Empty()?kachakacha::v2::kernel::BuildShapeMesh(built.Value().handle)
            :kachakacha::v2::base::Result<kachakacha::v2::modeling::ShapeMesh>::Success(std::move(preparedMesh));
        if (mesh.HasValue()) {
            lines.insert(lines.end(), mesh.Value().edges.begin(), mesh.Value().edges.end());
            thicken_->builtMesh.triangles.insert(thicken_->builtMesh.triangles.end(),
                mesh.Value().triangles.begin(), mesh.Value().triangles.end());
            thicken_->builtMeshes.emplace(built.Value().handle.value,std::move(mesh.Value()));
        }
    }
    thicken_->outcome.available = true;
    thicken_->outcome.count = thicken_->built.size();
    if (!lines.empty()) {
        thicken_->builtMesh.edges = std::move(lines);
        ShowSolidOutputPreview(*viewport_, thicken_->builtMesh, thickenDock_->OutputMode());
    }
}

//! 棚・札・一番下の一行を、いまの入力に合わせる。
void V2MainWindow::RefreshThickenDock()
{
    if (thickenDock_ == nullptr) {
        return;
    }
    const auto& document = session_->GetDocument();
    const auto nameOf = [&document](const EntityId& id) {
        const auto* entity = id.IsNil() ? nullptr : document.FindEntity(id);
        return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                                  : std::string("名前のないもの");
    };
    const bool previewShown = !viewport_->ToolPreview().empty();
    const bool calculating=thicken_->refreshing&&kachakacha::v2::app::ThickenReadyToBuild(thicken_->input)
        &&!thicken_->outcome.available&&thicken_->outcome.refusalJa.empty();
    auto shownOutcome=thicken_->outcome;
    if(calculating)shownOutcome.evaluated=false;
    std::vector<QString> lines;
    for (const std::string& line : kachakacha::v2::app::ThickenStatusLinesJa(thicken_->input,
             shownOutcome, previewShown)) {
        lines.push_back(QString::fromStdString(line));
    }
    if(calculating)lines.front()=QStringLiteral("計算中です。3D表示と設定は操作できます。設定を変えると計算し直します。");
    const bool needsWires=kachakacha::v2::app::IncludesWires(thickenDock_->OutputMode());
    const bool boundariesReady=!needsWires || (!thicken_->builtEdges.empty()&&std::all_of(thicken_->builtEdges.begin(),thicken_->builtEdges.end(),
        [](const auto& curves){return !curves.empty();}));
    if(thicken_->outcome.available&&!boundariesReady)lines.push_back(QStringLiteral("境界ワイヤーを作れません。ソリッドのみを選ぶか、面を分けてください。"));
    if(thicken_->outcome.available)lines.push_back(thickenDock_->OutputMode()==kachakacha::v2::app::SolidOutputMode::WiresOnly
        ? QStringLiteral("境界ワイヤーを生成します。ソリッドは保存しません。")
        : thickenDock_->OutputMode()==kachakacha::v2::app::SolidOutputMode::Both
        ? QStringLiteral("ソリッドと独立した境界ワイヤーを生成します。") : QStringLiteral("ソリッドのみを生成します。"));
    // 面の名前を並べる(何枚でも)。
    std::string names;
    for (const EntityId& id : thicken_->input.surfaces) {
        names += (names.empty() ? "" : " / ") + nameOf(id);
    }
    if (thicken_->input.surfaces.size() > 1) {
        names += "(" + std::to_string(thicken_->input.surfaces.size()) + " 枚)";
    }
    BindThickenInputs();
    thickenDock_->ShowInput(thicken_->input, QString::fromStdString(names), lines,
        thicken_->outcome.available&&boundariesReady&&!thicken_->refreshPending&&!V2GeometryJob::Active()
            &&!thicken_->built.empty()&&thicken_->built.size()==thicken_->input.surfaces.size());
    ShowToolFooter(thicken_->shelfShown
            ? QString::fromStdString(kachakacha::v2::app::ThickenFooterLine(thicken_->input,
                  names, nameOf(thicken_->input.targetPlane), thicken_->outcome, previewShown))
            : QString());
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    for (std::size_t index = 0; index < thicken_->input.surfaces.size(); ++index) {
        labels.push_back({thicken_->input.surfaces[index], thicken_->input.surfaces.size() == 1
                ? std::string("SURFACE")
                : "SURFACE " + std::to_string(index + 1)});
    }
    ShowRoleLabels(labels);
}

void V2MainWindow::RefreshThickenAll()
{
    if(thicken_->refreshing) {
        thicken_->refreshPending=true;
        V2GeometryJob::CancelPreview(this);
        RefreshThickenDock();return;
    }
    QScopedValueRollback<bool> refreshing(thicken_->refreshing,true);
    do {
        thicken_->refreshPending=false;
        // Show selected faces and new values before starting the calculation.
        RefreshThickenDock();
        RefreshThickenPreview();
    } while(thicken_->refreshPending&&thicken_->shelfShown);
    if(thicken_->shelfShown)RefreshThickenDock();
}

//! 「選び直す」。面の欄を空にし、次のクリックを待つ。
void V2MainWindow::ReselectThicken()
{
    thicken_->input.surfaces.clear();
    MirrorThickenToSelection();
    RefreshThickenAll();
}

//! 作り方のカード(外側・中央・内側)。「平面まで」は解いて、指定の作り方にする。
void V2MainWindow::ChooseThickenPlacement(ThicknessPlacement value)
{
    if(thicken_->input.placement==value&&!thicken_->input.toPlane)return;
    thicken_->input.placement = value;
    thicken_->input.toPlane = false;
    thicknessPlacement_ = value;   // 部品の棚の組み合わせ欄とも値をそろえる(RefreshPartDock)。
    RefreshThickenAll();
}

//! 「平面まで」のカード。相手の作業平面は、既定でいちばん上のものにしておく。
void V2MainWindow::ChooseThickenToPlane()
{
    if(!thicken_->shelfShown){RunThickenTool(true);return;}
    thicken_->input.toPlane = true;
    RefreshThickenAll();
}

//! 相手の作業平面を選んだ。
void V2MainWindow::ChooseThickenTarget(const EntityId& planeId)
{
    viewport_->CancelReferencePick();
    thicken_->input.targetPlane = planeId;
    thicken_->input.targetFaceBrep.clear();
    RefreshThickenAll();
}

//! 厚み(mm)の欄を打った。数の棚(板厚)とも値をそろえる。
void V2MainWindow::ApplyThickenThicknessMm(double value)
{
    if(thicken_->input.thicknessMm==value)return;
    thicken_->input.thicknessMm = value;
    (void)parameterDock_->Apply(kachakacha::v2::app::ParameterId::ThickenThicknessMm,
        QString::number(value, 'f', 3));
    RefreshThickenAll();
}

//! やめる。文書は始める前とまったく同じ。
void V2MainWindow::EndThicken(bool keepTool)
{
    V2GeometryJob::CancelPreview(this);
    thicken_->shelfShown = keepTool;
    if (keepTool) { thicken_->input.surfaces.clear(); thicken_->input.targetPlane = {}; thicken_->input.targetFaceBrep.clear(); }
    ReleaseThickenPreview(*thicken_);
    thicken_->builtEdges.clear();
    thicken_->builtThickness.clear();
    thicken_->builtMesh = {};
    thicken_->outcome = kachakacha::v2::app::ThickenPreviewOutcome{};
    thicken_->mirror.clear();
    if (viewport_ != nullptr) {
        viewport_->HideToolPreview();
        viewport_->HideToolRoleLabels();
        viewport_->SetToolPickActive(keepTool);
        viewport_->SetToolPickToggle(keepTool);
        // 欄の印を選択に残さない。残すと、次に構えたときに勝手に欄へ入る(HP-TH-02)。
        thicken_->mirroring = true;
        viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
        thicken_->mirroring = false;
    }
    ShowToolFooter(QString());
    if (keepTool) RefreshThickenDock();
    RefreshRightShelves();
}

//! 確定。**下見に使った形をそのまま**文書へ入れる。何枚でも 1 回の取り消しで戻る。
void V2MainWindow::ConfirmThicken()
{
    if(thicken_->refreshing||V2GeometryJob::Active())return;
    if (thicken_->built.empty() || thicken_->built.size() != thicken_->input.surfaces.size()) {
        SetStatus(QStringLiteral(
            "厚み: まだ作れません。面と作り方(平面までなら相手も)を入れてください。"));
        RefreshThickenDock();
        return;
    }
    const bool toPlane = thicken_->input.toPlane;
    const char* label = toPlane ? "面を平面まで" : "面に厚み";
    const auto outcome = thicken_->outcome;
    const auto surfaces = thicken_->input.surfaces;
    const auto built = thicken_->built;
    const auto edges = thicken_->builtEdges;
    const auto thickness = thicken_->builtThickness;
    const auto outputMode = thickenDock_->OutputMode();
    const auto* choice=findChild<QCheckBox*>(QStringLiteral("thickenIndependent"));
    const bool independent=choice&&choice->isChecked();
    std::vector<std::string> independentParts;
    std::vector<std::uint64_t> transferredMeshes;
    auto cleanup=qScopeGuard([&]{for(const auto& id:independentParts){partShapes_.erase(id);partEdges_.erase(id);}
        for(const auto handle:transferredMeshes)shapeMeshes_.erase(handle);});
    kachakacha::v2::document::Document::Transaction transaction(session_->GetDocument(), label);
    for (std::size_t index = 0; index < surfaces.size(); ++index) {
        const auto reuseMesh=[&]{
            const auto mesh=thicken_->builtMeshes.find(built[index].value);
            if(mesh!=thicken_->builtMeshes.end()&&!mesh->second.Empty()){
                shapeMeshes_[mesh->first]=std::move(mesh->second);transferredMeshes.push_back(mesh->first);
                thicken_->builtMeshes.erase(mesh);
            }
        };
        kachakacha::v2::domain::ThickenSurfaceDefinition definition;
        definition.surface = surfaces[index];
        definition.correctOpposite = !toPlane && thicken_->input.correctOpposite;
        definition.thickness.value = thickness[index];
        definition.thickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
        if (toPlane) {
            definition.targetPlane = thicken_->input.targetPlane;
            definition.targetOffsetMm = thicken_->input.targetOffsetMm;
            definition.targetFaceBrep = thicken_->input.targetFaceBrep;
        } else {
            definition.placement = static_cast<int>(thicken_->input.placement);
        }
        if(kachakacha::v2::app::IncludesWires(outputMode)) {
            const auto wires=BoundaryWiresFor(*thicken_,built[index],
                session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm);
            if(!wires.HasValue()){ReportDiagnostics(wires.Diagnostics());return;}
            for(const auto& wire:wires.Value())if(AddPlainWire(wire,"厚みの境界").IsNil())return;
        }
        if(kachakacha::v2::app::IncludesSolid(outputMode)&&independent) {
            using namespace kachakacha::v2;
            const auto brep=kernel::CaptureOutputShape(built[index]);
            if(!brep.HasValue()){ReportDiagnostics(brep.Diagnostics());return;}
            domain::Feature feature;feature.id=ids_->NextTyped<base::IdKind::Feature>();feature.type=domain::FeatureType::FreezeDerived;
            domain::FreezeDerivedDefinition frozen;frozen.frozenBrep=brep.Value();feature.definition=std::move(frozen);feature.displayName=std::string(label)+"（独立）";
            domain::Entity entity;entity.id=ids_->NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;
            entity.kind=domain::EntityKind::Part;entity.editPolicy=domain::EditPolicy::Frozen;
            entity.groupId=session_->GetDocument().Snapshot().settings.activeGroupId;
            entity.displayName=app::UniqueDisplayName(session_->GetDocument().Snapshot(),entity.kind,feature.displayName);
            feature.outputs.push_back({"part",entity.id,entity.kind});
            const auto added=session_->GetDocument().Run(document::AddFeatureCommand(feature,{entity},label));
            if(!added.committed){ReportDiagnostics(added.diagnostics);return;}
            reuseMesh();
            const auto key=entity.id.ToString();partShapes_[key]=built[index];partEdges_[key]=edges[index];independentParts.push_back(key);
        } else if(kachakacha::v2::app::IncludesSolid(outputMode)) {
            std::vector<EntityId> inputs{surfaces[index]};
            if(toPlane)inputs.push_back(thicken_->input.targetPlane);
            reuseMesh();
            const auto madeId = AddPartFeature(kachakacha::v2::domain::FeatureType::ThickenSurface,
                std::move(definition), built[index], edges[index], label, inputs);
            if (madeId.IsNil()) return;
        }
    }
    if (!transaction.Commit()) {
        SetStatus(QStringLiteral("厚み: 途中で失敗したので、何も作っていません。"));
        AdoptCurrentDocument();
        return;
    }
    cleanup.dismiss();
    // Committed parts now own these handles; cancelling only owns previews.
    if(kachakacha::v2::app::IncludesSolid(outputMode))thicken_->built.clear();
    EndThicken();
    AdoptCurrentDocument();
    if(outputMode==kachakacha::v2::app::SolidOutputMode::WiresOnly) {
        SetStatus(QStringLiteral("厚み: 出来上がりの境界ワイヤーを生成しました。"));return;
    }
    SetStatus(surfaces.size() > 1
            ? QStringLiteral("%1: %2 枚の面から部品を %2 個作りました(体積の合計 %3 mm3)。")
                  .arg(QString::fromUtf8(label))
                  .arg(static_cast<int>(surfaces.size()))
                  .arg(outcome.volumeMm3, 0, 'f', 3)
            : QStringLiteral("%1: 厚み %2 mm、体積 %3 mm3 の部品を作りました。")
                  .arg(QString::fromUtf8(label))
                  .arg(outcome.thicknessMm, 0, 'f', 3)
                  .arg(outcome.volumeMm3, 0, 'f', 3));
    if(outcome.corrected)SetStatus(QStringLiteral("厚み: 反対面を補正しました。法線厚み（対応点・標本）%1〜%2 mm、位置ずれ最大 %3 mm / RMS（最大部材）%4 mm。")
        .arg(outcome.normalThicknessMinMm,0,'f',3).arg(outcome.normalThicknessMaxMm,0,'f',3)
        .arg(outcome.maxOffsetDeviationMm,0,'f',3).arg(outcome.rmsOffsetDeviationMm,0,'f',3));
}

bool V2MainWindow::ThickenShelfShown() const noexcept { return thicken_->shelfShown; }

const kachakacha::v2::app::ThickenInputState& V2MainWindow::ThickenInput() const
{
    return thicken_->input;
}
