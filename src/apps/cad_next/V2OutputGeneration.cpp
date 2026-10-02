#include <QDialog>
#include "V2OutputTool.h"
#include "V2MainWindow.h"
#include <QComboBox>
#include <QLabel>
#include <set>
using namespace kachakacha::v2;
namespace {
std::vector<base::EntityId> NewVisibleGeometry(const document::DocumentSnapshot& before,const document::DocumentSnapshot& after) {
    std::set<base::EntityId> previous;for(const auto& entity:before.entities)previous.insert(entity.id);
    std::vector<base::EntityId> made;
    for(const auto& entity:after.entities)if(!previous.count(entity.id)&&entity.visibility==domain::Visibility::Visible
        && (entity.kind==domain::EntityKind::Wire||entity.kind==domain::EntityKind::GuideSurface||entity.kind==domain::EntityKind::Part))made.push_back(entity.id);
    return made;
}
}
void V2OutputTool::Generate(V2MainWindow& window,std::string_view command) {
    if(!window.ValidateGenerationParts())return;
    for(auto* child:window.findChildren<QDialog*>())if(child->objectName()=="selectionOutput")child->close();
    auto* dialog=new V2OutputTool(window,window.fabricationDock_->GenerationDestination()==0?3:2);
    const auto selection=window.viewport_->Selection();auto& document=window.session_->GetDocument();const auto before=document.Snapshot();
    bool ok=false;
    {
        document::Document::Transaction temporary(document,"生成プレビュー");
        if(command=="fabrication.freeze_flat")window.FreezeFlatOutline();
        else if(command=="fabrication.freeze_target")window.FreezeTargetShape();
        else if(command=="fabrication.freeze_wires")window.FreezeContourWires();
        else window.FreezeFabricationState();
        const auto made=NewVisibleGeometry(before,document.Snapshot());ok=!document.CompoundSpoiled()&&!made.empty()&&dialog->Capture(made);
    }
    window.AdoptCurrentDocument();window.RebuildKernelShapes();window.RefreshShapeViews();window.viewport_->SetSelection(selection);
    dialog->revision_=document.Revision();dialog->documentId_=document.Snapshot().id;dialog->generated_=true;dialog->FillTargets();
    if(!ok)dialog->SetProblem(QStringLiteral("指定した部材の形状を生成できません。元の文書は変更していません。"));
    else {geometry::OutputPlacement placement;placement.keepPosition=false;dialog->SetPlacement(placement);}
    dialog->show();
}
bool V2OutputTool::CaptureModels(const std::vector<base::EntityId>& ids) {
    const auto selection=window_.viewport_->Selection();const auto output=window_.freezeOutput_;const auto parts=window_.fabricationDock_->PartNumbersText();
    auto& document=window_.session_->GetDocument();const auto before=document.Snapshot();bool ok=false;
    {
        document::Document::Transaction temporary(document,"出力モデルを評価");
        std::vector<base::EntityId> models,plain;
        for(const auto& id:ids){const auto* entity=document.FindEntity(id);if(entity!=nullptr && entity->kind==domain::EntityKind::FabricationModel)models.push_back(id);else plain.push_back(id);}
        window_.viewport_->SetSelection(app::SelectionSet{models});window_.freezeOutput_=fabrication::FreezeOutput::WiresAndSurfaces;
        if(window_.ValidateGenerationParts())window_.FreezeFabricationState();
        const auto made=NewVisibleGeometry(before,document.Snapshot());
        for(const auto& id:made){const auto* entity=document.FindEntity(id);if(entity!=nullptr && entity->kind==domain::EntityKind::GuideSurface)plain.push_back(id);}
        ok=!document.CompoundSpoiled()&&plain.size()>ids.size()-models.size()&&Capture(plain);
    }
    window_.freezeOutput_=output;window_.fabricationDock_->SetPartNumbersText(parts);window_.AdoptCurrentDocument();window_.RebuildKernelShapes();window_.RefreshShapeViews();window_.viewport_->SetSelection(selection);
    if(!ok)SetProblem(QStringLiteral("選択した近似モデルの現在形状を評価できません。生成する対象部材を確認してください。"));return ok;
}
