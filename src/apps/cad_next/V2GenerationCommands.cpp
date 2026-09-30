#include <QString>
#include "V2MainWindow.h"
#include "V2GptSurfaceTool.h"
#include "kachakacha/app/GptSurface.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/io/AtomicFile.h"
#include <QFileInfo>
#include <algorithm>
#include <set>
using namespace kachakacha::v2;
namespace {
document::DocumentSnapshot GeneratedOnly(const document::DocumentSnapshot& before,
    const document::DocumentSnapshot& after,base::DocumentId id)
{
    document::DocumentSnapshot result; result.id=id; result.settings.tolerance=after.settings.tolerance;
    std::set<base::EntityId> previous;
    for(const auto& entity:before.entities) { previous.insert(entity.id); }
    std::set<base::FeatureId> features;
    for(auto entity:after.entities) {
        if(previous.count(entity.id)) { continue; }
        entity.groupId.reset(); entity.generatedFrom.reset();
        features.insert(entity.createdBy); result.entities.push_back(std::move(entity));
    }
    for(const auto& feature:after.features) { if(features.count(feature.id)) { result.features.push_back(feature); } }
    for(const auto& feature:after.evaluationOrder) { if(features.count(feature)) { result.evaluationOrder.push_back(feature); } }
    return result;
}
}
bool V2MainWindow::ValidateGenerationParts()
{
    const auto choice=ReadPartNumbers();
    if(choice.unreadable) { SetStatus(QString::fromStdString(choice.whyJa)); return false; }
    if(choice.blank) { return true; }
    std::vector<std::string> models;
    for(const auto& id:viewport_->Selection().entityIds) {
        if(fabricationModels_.count(id.ToString())) { models.push_back(id.ToString()); }
    }
    if(models.empty()) { models.push_back(CurrentFabricationModelId().ToString()); }
    for(const auto& id:models) {
        const auto found=fabricationModels_.find(id);
        if(found==fabricationModels_.end()) { SetStatus(QStringLiteral("生成する近似モデルを選んでください。")); return false; }
        for(const auto index:choice.numbers) {
            if(index>=found->second.panels.size()) {
                SetStatus(QStringLiteral("部材%1はありません。対象部材を直してください。何も生成していません。").arg(index+1)); return false;
            }
        }
    }
    return true;
}
void V2MainWindow::ExportFabricationGeneration(std::string_view id)
{
    if(!ValidateGenerationParts()) { return; }
    QString path=AskForPath(true);
    if(path.isEmpty()) { SetStatus(QStringLiteral("別ファイルへの生成を取り消しました。")); return; }
    if(!path.endsWith(QStringLiteral(".kcd2"),Qt::CaseInsensitive)) { path+=QStringLiteral(".kcd2"); }
    if(!documentPath_.isEmpty() && QFileInfo(path).absoluteFilePath().compare(QFileInfo(documentPath_).absoluteFilePath(),Qt::CaseInsensitive)==0) {
        SetStatus(QStringLiteral("現在の文書と別の保存先を指定してください。")); return;
    }
    auto& document=session_->GetDocument(); const auto before=document.Snapshot();
    const auto selected=viewport_->Selection(); bool written=false; QString error;
    {
        document::Document::Transaction transaction(document,"別ファイルへ生成");
        fabricationDock_->SetGenerationDestination(0); RunFreezeCommand(id); fabricationDock_->SetGenerationDestination(1);
        auto snapshot=GeneratedOnly(before,document.Snapshot(),ids_->NextTyped<base::IdKind::Document>());
        if(document.CompoundSpoiled() || snapshot.entities.empty()) { error=StatusText(); }
        else {
            io::DocumentFile file; file.snapshot=std::move(snapshot); file.metadata.title=QFileInfo(path).fileName().toStdString();
            const auto archive=io::SaveDocument(file);
            if(!archive.HasValue()) { error=QString::fromStdString(archive.FirstMessageJa()); }
            else {
                const auto saved=io::WriteFileAtomically(path.toStdString(),archive.Value());
                written=saved.HasValue(); if(!written) { error=QString::fromStdString(saved.FirstMessageJa()); }
            }
        }
        // No commit: only the independent file is retained, never temporary entities in the source.
    }
    AdoptCurrentDocument(); viewport_->SetSelection(selected);
    SetStatus(written ? QStringLiteral("生成した形を%1へ保存しました。元の文書は変更していません。").arg(path)
                      : QStringLiteral("別ファイルへ生成できませんでした。%1").arg(error));
}
void V2MainWindow::AutomaticallyGenerate(const std::vector<base::EntityId>& models)
{
    const int mode=fabricationDock_->AutomaticGeneration(); if(mode==0) { return; }
    const auto output=freezeOutput_; const auto parts=fabricationDock_->PartNumbersText();
    freezeOutput_=mode==1 ? fabrication::FreezeOutput::WiresOnly : fabrication::FreezeOutput::WiresAndSurfaces;
    viewport_->SetSelection(app::SelectionSet{models}); fabricationDock_->SetPartNumbersText(QStringLiteral("すべて"));
    RunFreezeCommand("fabrication.freeze_state");
    freezeOutput_=output; fabricationDock_->SetPartNumbersText(parts); RefreshFabricationDock();
    viewport_->SetSelection(app::SelectionSet{models});
}
bool V2MainWindow::GenerateApproxSurface(const std::vector<base::EntityId>& wires,const std::string& label)
{
    domain::Feature feature; feature.id=ids_->NextTyped<base::IdKind::Feature>();
    feature.type=domain::FeatureType::CreateGuideSurface; feature.displayName=label; feature.inputEntityIds=wires;
    domain::CreateGuideSurfaceDefinition definition; definition.gptBuilder=true;
    definition.method=wires.size()>1 ? app::kGptPanelPatchesMethod : app::kGptBoundaryMethod; definition.gptToleranceMm=0.01;
    for(const auto& wire:wires) {
        domain::WireChainRef chain; chain.segments.push_back({wire}); chain.reversed.push_back(false);
        definition.chains.push_back(chain); definition.roles.push_back(app::kGptBoundaryRole);
    }
    feature.definition=definition; domain::Entity entity; entity.id=ids_->NextTyped<base::IdKind::Entity>();
    entity.kind=domain::EntityKind::GuideSurface; entity.createdBy=feature.id; entity.displayName=label;
    feature.outputs.push_back({"surface",entity.id,entity.kind});
    AdoptCurrentDocument();
    if(!gptSurface_->Rebuild(feature,entity.id)) { return false; }
    const auto added=session_->GetDocument().Run(document::AddFeatureCommand(feature,{entity},"近似面を生成"));
    if(!added.committed) { ReportDiagnostics(added.diagnostics); return false; }
    return true;
}
