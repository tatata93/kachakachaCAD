#include <QSet>
#include <QUuid>
#include <QWidget>
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVariant>
#include <algorithm>
#include <map>
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/modeling/ShapeMesh.h"
bool V2InstructionMode::Collect(V2MainWindow& model){
    InstructionPage candidate;candidate.title=QStringLiteral("使用部品を選択");std::size_t count=0;
    for(const auto& shape:model.Viewport().ShapeViews()){
        if(!model.Viewport().EntityShown(shape.entityId)||shape.mesh.triangles.empty())continue;
        count+=shape.mesh.triangles.size();if(count>500000||candidate.parts.size()>=512){
            if(hint_)hint_->setText(QStringLiteral("このモデルは説明書の読込上限（候補512要素・表示三角形50万）を超えます。必要部分を別KCDへ出力してください。"));return false;}
        auto asset=std::make_shared<InstructionAsset>();asset->mesh=shape.mesh;
        const auto* entity=model.Session().GetDocument().FindEntity(shape.entityId);asset->name=entity?QString::fromStdString(entity->displayName):QStringLiteral("部品");
        candidate.parts.push_back({QUuid::createUuid(),asset});
    }
    const bool hasFaces=!candidate.parts.empty();
    std::map<std::string,std::shared_ptr<InstructionAsset>> wires;
    std::size_t edgePoints=0;
    for(const auto& curve:model.Session().Scene().curves){
        if(curve.construction||!model.Viewport().EntityShown(curve.entityId))continue;
        const auto key=curve.entityId.ToString();auto& asset=wires[key];
        if(!asset){asset=std::make_shared<InstructionAsset>();const auto* entity=model.Session().GetDocument().FindEntity(curve.entityId);asset->name=entity?QString::fromStdString(entity->displayName):QStringLiteral("ワイヤー");}
        std::vector<kachakacha::v2::geometry::Vector3> points;
        for(const auto& p:kachakacha::v2::geometry::SampleCurve(curve.segment,.01)){points.push_back(p.position);}
        edgePoints+=points.size();if(edgePoints>2000000){if(hint_)hint_->setText(QStringLiteral("ワイヤーの表示点数が多すぎます。モデルを分けてください。"));return false;}
        if(points.size()>=2)asset->mesh.edges.push_back(std::move(points));
    }
    if(wires.size()>128){
        auto combined=std::make_shared<InstructionAsset>();combined->name=QStringLiteral("ワイヤー（%1本）").arg(wires.size());
        for(const auto& [id,asset]:wires){(void)id;combined->mesh.edges.insert(combined->mesh.edges.end(),asset->mesh.edges.begin(),asset->mesh.edges.end());}
        wires.clear();wires.emplace("combined",std::move(combined));
    }
    for(auto& [id,asset]:wires){
        (void)id;if(asset->mesh.edges.empty())continue;
        if(candidate.parts.size()>=512){if(hint_)hint_->setText(QStringLiteral("取り込み候補は512個までです。必要な要素だけ表示してください。"));return false;}
        kachakacha::v2::modeling::RefreshBounds(asset->mesh);candidate.parts.push_back({QUuid::createUuid(),asset,{}, {},!hasFaces});
    }
    if(candidate.parts.empty()){if(hint_)hint_->setText(QStringLiteral("モデルに表示可能な部品・面がありません。"));return false;}
    source_=std::move(candidate);sourceChecks_.clear();for(const auto& part:source_.parts)sourceChecks_.push_back(part.visible);sourceRow_=-1;choosing_=true;view_->SetPage(&source_);view_->SetTool("pick");view_->Fit();ShowModelPicker();return true;
}
void V2InstructionMode::Capture(){Collect(window_);}
bool V2InstructionMode::ReadModel(const QString& path){
    const auto theme=window_.Theme();V2MainWindow model;window_.ApplyTheme(theme);model.SetPathChooser([](bool){return QString();});
    if(!model.OpenDocumentFile(path)){if(hint_)hint_->setText(QStringLiteral("モデルを読み込めません。対応するKCD/KCD2ファイルを選んでください。"));return false;}
    return Collect(model);
}
void V2InstructionMode::ShowModelPicker(){
    auto* panel=new QWidget;settings_=panel;auto* layout=new QVBoxLayout(panel);
    hint_=new QLabel(QStringLiteral("使用する部品にチェックを入れ、3Dで確認して「この場面に配置」。一覧の行を押すと対応部品を強調します。"),panel);hint_->setWordWrap(true);layout->addWidget(hint_);
    auto* list=new QListWidget(panel);partList_=list;list->setObjectName("instructionModelParts");layout->addWidget(list,1);
    for(std::size_t i=0;i<source_.parts.size();++i){auto* item=new QListWidgetItem(source_.parts[i].asset->name,list);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(sourceChecks_[i]?Qt::Checked:Qt::Unchecked);}
    list->setCurrentRow(sourceRow_);
    connect(list,&QListWidget::itemChanged,this,[this,list](QListWidgetItem* item){const int row=list->row(item);if(row>=0&&row<int(sourceChecks_.size())){sourceChecks_[row]=item->checkState()==Qt::Checked;source_.parts[row].visible=sourceChecks_[row];view_->Refresh();}});
    connect(list,&QListWidget::currentRowChanged,this,[this](int row){sourceRow_=row;view_->SetSelected(row);});

    auto* row=new QHBoxLayout;layout->addLayout(row);
    for(const bool checked:{true,false}){auto* button=new QPushButton(checked?QStringLiteral("全て使う"):QStringLiteral("全て外す"),panel);row->addWidget(button);
        connect(button,&QPushButton::clicked,this,[list,checked]{for(int i=0;i<list->count();++i)list->item(i)->setCheckState(checked?Qt::Checked:Qt::Unchecked);});}
    auto* preview=new QPushButton(QStringLiteral("チェックした部品だけを3D表示"),panel);layout->addWidget(preview);
    connect(preview,&QPushButton::clicked,this,[this,list]{for(int i=0;i<list->count();++i)source_.parts[i].visible=list->item(i)->checkState()==Qt::Checked;view_->Refresh();});
    auto* all=new QPushButton(QStringLiteral("モデル全体を3D表示"),panel);layout->addWidget(all);
    connect(all,&QPushButton::clicked,this,[this]{for(auto& p:source_.parts)p.visible=true;view_->Refresh();});
    auto* accept=new QPushButton(QStringLiteral("この場面に配置"),panel);accept->setObjectName("instructionAcceptParts");layout->addWidget(accept);connect(accept,&QPushButton::clicked,this,[this]{AcceptParts();});
    auto* cancel=new QPushButton(QStringLiteral("取消"),panel);layout->addWidget(cancel);connect(cancel,&QPushButton::clicked,this,[this]{ShowPage(current_);});
    window_.OperationHost().ShowTemporaryPage(panel,QStringLiteral("説明書 — 使用する部品を選択"));
}
void V2InstructionMode::AcceptParts(){
    if(!choosing_||!partList_)return;std::vector<InstructionPart> chosen;
    for(int i=0;i<partList_->count();++i)if(partList_->item(i)->checkState()==Qt::Checked){auto part=source_.parts[i];part.visible=true;part.id=QUuid::createUuid();chosen.push_back(std::move(part));}
    if(chosen.empty()){hint_->setText(QStringLiteral("使用する部品を1つ以上選んでください。"));return;}
    if(scenes_[current_].parts.size()+chosen.size()>128){hint_->setText(QStringLiteral("1場面に配置できる部品は128個までです。"));return;}
    QSet<QUuid> seen;std::size_t triangles=0;
    const auto count=[&](const InstructionPart& p){if(!seen.contains(p.asset->id)){seen.insert(p.asset->id);triangles+=p.asset->mesh.triangles.size();}};
    for(const auto& page:scenes_)for(const auto& p:page.parts)count(p);for(const auto& p:chosen)count(p);
    if(triangles>500000){hint_->setText(QStringLiteral("説明書全体の表示形状が50万三角形を超えます。説明書を分けてください。"));return;}
    before_=Snapshot();auto& page=scenes_[current_];page.parts.insert(page.parts.end(),chosen.begin(),chosen.end());
    ShowPage(current_);view_->SetSelected(int(page.parts.size()-chosen.size()));ShowSettings();view_->Fit();Remember();
}
