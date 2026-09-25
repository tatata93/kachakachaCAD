#include "V2GptFabricationTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctGptFabrication.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/app/ExplorerModel.h"
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QWidget>
#include <QObject>
#include <QString>
#include <QStringList>
#include <algorithm>
using namespace kachakacha::v2;

V2GptFabricationTool::V2GptFabricationTool(V2MainWindow& window):window_(window)
{
    dock_=new QDockWidget(QStringLiteral("製作近似 GPT版"),&window);
    dock_->setObjectName(QStringLiteral("gptFabricationDock"));
    auto* widget=new QWidget(dock_); dock_->setWidget(widget);
    auto* layout=new QVBoxLayout(widget); Controls(layout); Actions(layout);
}
void V2GptFabricationTool::Controls(QVBoxLayout* layout)
{
    sources_=new QLabel(QStringLiteral("面を選んで追加してください。"),dock_->widget());
    sources_->setWordWrap(true); layout->addWidget(sources_);
    const auto number=[&](const char* name,const QString& label,double value,double minimum,double maximum) {
        auto* box=new QDoubleSpinBox(dock_->widget()); box->setObjectName(QString::fromLatin1(name));
        box->setDecimals(4); box->setRange(minimum,maximum); box->setValue(value);
        box->setPrefix(label); box->setSuffix(QStringLiteral(" mm")); layout->addWidget(box);
        QObject::connect(box,&QDoubleSpinBox::valueChanged,dock_,[this](double){Invalidate();}); return box;
    };
    tolerance_=number("gptFabricationTolerance",QStringLiteral("許容偏差 "),.25,.001,100);
    width_=number("gptFabricationWidth",QStringLiteral("最小幅 "),.5,.01,1000);
    thickness_=number("gptFabricationThickness",QStringLiteral("板厚 "),.2,.001,100);
    count_=new QSpinBox(dock_->widget()); count_->setObjectName(QStringLiteral("gptFabricationCount"));
    count_->setRange(1,64); count_->setValue(12); count_->setPrefix(QStringLiteral("最大部材数 ")); layout->addWidget(count_);
    direction_=new QComboBox(dock_->widget()); direction_->setObjectName(QStringLiteral("gptFabricationDirection"));
    direction_->addItems({QStringLiteral("曲げ方向を自動比較"),QStringLiteral("方向1"),QStringLiteral("方向2")});
    layout->addWidget(direction_);
    QObject::connect(count_,&QSpinBox::valueChanged,dock_,[this](int){Invalidate();});
    QObject::connect(direction_,&QComboBox::currentIndexChanged,dock_,[this](int){Invalidate();});
    panels_=new QTreeWidget(dock_->widget()); panels_->setObjectName(QStringLiteral("gptFabricationPanels"));
    panels_->setHeaderLabels({QStringLiteral("部材"),QStringLiteral("最大偏差 mm")}); layout->addWidget(panels_);
    assembly_=new QDoubleSpinBox(dock_->widget()); assembly_->setObjectName(QStringLiteral("gptFabricationAssembly"));
    assembly_->setRange(0,100); assembly_->setValue(100); assembly_->setPrefix(QStringLiteral("組立 "));
    assembly_->setSuffix(QStringLiteral(" %")); layout->addWidget(assembly_);
    QObject::connect(assembly_,&QDoubleSpinBox::valueChanged,dock_,[this](double value){definition_.masterPercent=value;ShowPreview();});
    status_=new QLabel(dock_->widget()); status_->setObjectName(QStringLiteral("gptFabricationStatus"));
    status_->setWordWrap(true); status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_);
}
void V2GptFabricationTool::Actions(QVBoxLayout* layout)
{
    auto* actions=new QGridLayout; layout->addLayout(actions);
    const auto button=[&](const char* name,const QString& label,int row,int col,auto handler) {
        auto* made=new QPushButton(label,dock_->widget()); made->setObjectName(QString::fromLatin1(name));
        QObject::connect(made,&QPushButton::clicked,dock_,handler); actions->addWidget(made,row,col); return made;
    };
    button("gptFabricationAdd",QStringLiteral("選択した面を追加"),0,0,[this]{Add(window_.viewport_->Selection().entityIds);});
    button("gptFabricationClear",QStringLiteral("対象を空にする"),0,1,[this]{definition_.parts.clear();Invalidate();sources_->setText(QStringLiteral("対象なし"));});
    button("gptFabricationPreview",QStringLiteral("近似をプレビュー"),1,0,[this]{Preview();});
    button("gptFabricationCancel",QStringLiteral("取消"),1,1,[this]{End();});
    confirm_=button("gptFabricationConfirm",QStringLiteral("この近似を確定"),2,0,[this]{Confirm();});
    pattern_=button("gptFabricationPattern",QStringLiteral("確定して型紙へ"),2,1,[this]{Confirm(true);});
    confirm_->setEnabled(false); pattern_->setEnabled(false);
}
void V2GptFabricationTool::Begin()
{
    if (active_) { return; }
    window_.SetMode(app::UiMode::Fabrication);
    active_=true; definition_={}; definition_.method=2; definition_.minimumPartWidthMm=.5;
    const auto selected=window_.viewport_->Selection().entityIds;
    window_.viewport_->SetToolPickActive(true);
    window_.viewport_->SetToolPickToggle(true); window_.RefreshRightShelves();
    Invalidate(); Add(selected);
    window_.ShowToolFooter(QStringLiteral("製作近似 GPT版 / 面を選ぶ → 下見 → 確定 / Escで取消"));
}
void V2GptFabricationTool::End()
{
    if (!active_) { return; }
    active_=false; preview_.reset(); window_.viewport_->HideToolPreview();
    window_.viewport_->HideToolRoleLabels(); window_.viewport_->SetToolPickActive(false);
    window_.viewport_->SetToolPickToggle(false); window_.ShowToolFooter(QString()); window_.RefreshRightShelves();
}
void V2GptFabricationTool::Add(const std::vector<base::EntityId>& ids)
{
    for (const auto& id:ids) {
        const auto* entity=window_.session_->GetDocument().FindEntity(id);
        if (!entity || entity->kind!=domain::EntityKind::GuideSurface) {
            status_->setText(QStringLiteral("形状ガイドの面を選んでください。線や立体は追加していません。")); return;
        }
    }
    for (const auto& id:ids) {
        if (std::find(definition_.parts.begin(),definition_.parts.end(),id)==definition_.parts.end()) { definition_.parts.push_back(id); }
    }
    Invalidate(); QString text=QStringLiteral("対象: ");
    for (const auto& id:definition_.parts) {
        const auto* entity=window_.session_->GetDocument().FindEntity(id);
        if (entity) { text+=QString::fromStdString(entity->displayName)+QStringLiteral(" / "); }
    }
    sources_->setText(text);
}
void V2GptFabricationTool::HandleSelectionChanged()
{
    if (!active_) { return; }
    const auto picked=window_.viewport_->TakeLastToolPick();
    if (picked.has_value()) { Add({*picked}); }
}
void V2GptFabricationTool::Invalidate()
{
    preview_.reset(); if (!active_) { return; }
    confirm_->setEnabled(false); pattern_->setEnabled(false); panels_->clear(); window_.viewport_->HideToolPreview();
    status_->setText(QStringLiteral("条件を指定してプレビューしてください。元の面は変更しません。"));
}
base::Result<app::FabricationEvaluation> V2GptFabricationTool::Evaluate(
    const domain::CreateFabricationModelDefinition& definition) const
{
    std::vector<kernel::GptFabricationInput> inputs;
    for (const auto& id:definition.parts) {
        const auto found=window_.guideShapes_.find(id.ToString());
        if (found==window_.guideShapes_.end()) {
            return base::Result<app::FabricationEvaluation>::Failure(base::MakeError("GPT-F007","元の面が見つかりません。","面を再生成して選び直してください。"));
        }
        inputs.push_back({id,found->second});
    }
    return kernel::BuildGptFabrication(definition,inputs);
}
void V2GptFabricationTool::Preview()
{
    Invalidate(); definition_.targetMaxDeviation={"",tolerance_->value(),geometry::QuantityKind::Length};
    definition_.materialThickness={"",thickness_->value(),geometry::QuantityKind::Length};
    definition_.maximumPartCount=count_->value(); definition_.minimumPartWidthMm=width_->value();
    definition_.splitAxis=direction_->currentIndex()==0 ? 2 : direction_->currentIndex()-1;
    definition_.masterPercent=assembly_->value();
    const auto made=Evaluate(definition_);
    if (!made.HasValue()) { status_->setText(QString::fromStdString(made.FirstMessageJa())); return; }
    preview_=made.Value(); revision_=window_.session_->GetDocument().Snapshot().revision;
    for (const auto& panel:preview_->gptPanels) {
        auto* row=new QTreeWidgetItem(panels_); row->setText(0,QString::fromStdString(panel.pattern.panelId));
        row->setText(1,QString::number(panel.maximumMm,'g',6));
    }
    status_->setText(QString::fromStdString(preview_->summaryJa)+QStringLiteral("\n柱面への近似です。標本偏差は全域の上界ではありません。"));
    confirm_->setEnabled(preview_->reachedTolerance); pattern_->setEnabled(preview_->reachedTolerance); ShowPreview();
}
void V2GptFabricationTool::ShowPreview()
{
    if (!active_ || !preview_) { return; }
    modeling::ShapeMesh mesh; std::vector<std::vector<geometry::Vector3>> faces;
    for (const auto& panel:preview_->gptPanels) {
        const auto part=fabrication::GptPanelMesh(panel,definition_.masterPercent/100.0);
        mesh.triangles.insert(mesh.triangles.end(),part.triangles.begin(),part.triangles.end());
        mesh.edges.insert(mesh.edges.end(),part.edges.begin(),part.edges.end());
    }
    for (const auto& triangle:mesh.triangles) { faces.push_back({triangle.points[0],triangle.points[1],triangle.points[2]}); }
    window_.viewport_->ShowToolPreview(mesh.edges); window_.viewport_->SetToolPreviewFaces(std::move(faces),&mesh);
}
void V2GptFabricationTool::Confirm(bool pattern)
{
    if (!preview_ || !preview_->reachedTolerance) { Preview(); return; }
    if (revision_!=window_.session_->GetDocument().Snapshot().revision) { Invalidate(); status_->setText(QStringLiteral("元の文書が変わりました。再度プレビューしてください。")); return; }
    domain::Feature feature; feature.id=window_.ids_->NextTyped<base::IdKind::Feature>();
    feature.type=domain::FeatureType::CreateFabricationModel;
    feature.displayName=app::UniqueDisplayName(window_.session_->GetDocument().Snapshot(), domain::EntityKind::FabricationModel, "近似モデル（GPT版）");
    feature.definition=definition_; feature.inputEntityIds=definition_.parts;
    domain::Entity entity; entity.id=window_.ids_->NextTyped<base::IdKind::Entity>();
    entity.kind=domain::EntityKind::FabricationModel; entity.displayName=feature.displayName; entity.createdBy=feature.id;
    feature.outputs.push_back({"fabrication",entity.id,entity.kind});
    const auto added=window_.session_->GetDocument().Run(document::AddFeatureCommand(feature,{entity},"GPT近似を作る"));
    if (!added.committed) { window_.ReportDiagnostics(added.diagnostics); return; }
    const auto summary=preview_->summaryJa;
    for (std::size_t i=0;i<preview_->panels.size();++i) {
        const auto name=feature.displayName+" 部材"+std::to_string(i+1);
        preview_->panels[i].panelId=name; preview_->gptPanels[i].pattern.panelId=name;
    }
    window_.fabricationModels_[entity.id.ToString()]=*preview_;
    End(); window_.AdoptCurrentDocument(); window_.RefreshFabricationView(); window_.RefreshShapeViews();
    window_.viewport_->SetSelection(app::SelectionSet{{entity.id}}); window_.SetStatus(QString::fromStdString(summary));
    if (pattern) { window_.RunCommand("fabrication.create_pattern"); }
}
bool V2GptFabricationTool::Rebuild(const domain::Feature& feature,base::EntityId output)
{
    const auto* definition=std::get_if<domain::CreateFabricationModelDefinition>(&feature.definition);
    if (!definition || definition->method!=2) { return false; }
    const auto made=Evaluate(*definition);
    if (!made.HasValue() || !made.Value().reachedTolerance) { return false; }
    auto result=made.Value();
    for (std::size_t i=0;i<result.panels.size();++i) {
        const auto name=feature.displayName+" 部材"+std::to_string(i+1);
        result.panels[i].panelId=name; result.gptPanels[i].pattern.panelId=name;
    }
    window_.fabricationModels_[output.ToString()]=std::move(result); return true;
}
bool V2GptFabricationTool::HandleKey(int key)
{
    if (!active_) { return false; }
    if (key==Qt::Key_Escape) { End(); return true; }
    if (key==Qt::Key_Return || key==Qt::Key_Enter) { Confirm(); return true; }
    return false;
}
