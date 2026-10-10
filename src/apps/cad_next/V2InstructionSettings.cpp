#include <QFont>
#include <QPointer>
#include <QQuaternion>
#include <QString>
#include <QWidget>
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QFontComboBox>
#include <QCheckBox>
#include <QVector3D>
void V2InstructionMode::ShowSceneSettings(){
    auto* panel=new QWidget;settings_=panel;auto* layout=new QVBoxLayout(panel);
    hint_=new QLabel(QStringLiteral("モデルを選ぶ → 使う部品を選ぶ → 3D配置。右ドラッグ:回転 / 中ドラッグ:平行移動 / ホイール:拡大。"),panel);hint_->setWordWrap(true);layout->addWidget(hint_);
    if(!scenes_[current_].legacyPng.isEmpty())hint_->setText(QStringLiteral("旧版の2D説明図を背景として保持しています。3D配置にはモデルを読み込んでください。"));
    if(activeTool_=="text"){
    AddFontControls(panel,false);
    auto* text=new QLineEdit(panel);text->setObjectName("instructionText");text->setPlaceholderText(QStringLiteral("番号・説明文（ツール選択後、3D上をクリック）"));layout->addWidget(text);
    const int mark=view_->SelectedMark();if(mark>=0&&!scenes_[current_].marks[mark].arrow)text->setText(scenes_[current_].marks[mark].text);
    connect(text,&QLineEdit::textEdited,this,[this,text]{view_->SetTool("text",text->text());});
    auto* applyText=new QPushButton(QStringLiteral("選択した文字を更新"),panel);layout->addWidget(applyText);
    connect(applyText,&QPushButton::clicked,this,[this,text]{const int i=view_->SelectedMark();if(i<0)return;before_=Snapshot();scenes_[current_].marks[i].text=text->text();Remember();view_->Refresh();});
    }
    if(activeTool_=="move"){
    auto* target=new QComboBox(panel);target->setObjectName("instructionPlacedPart");target->addItem(QStringLiteral("対象部品を選択"));
    for(std::size_t i=0;i<scenes_[current_].parts.size();++i)target->addItem(QString::number(i+1)+QStringLiteral("：")+scenes_[current_].parts[i].asset->name);
    target->setCurrentIndex(view_->Selected()+1);layout->addWidget(target);
    connect(target,&QComboBox::currentIndexChanged,this,[this](int index){view_->SetSelected(index-1);});
    view_->selectedChanged=[target=QPointer<QComboBox>(target)](int index){if(target){const QSignalBlocker blocker(target);target->setCurrentIndex(index+1);}};
    auto* form=new QFormLayout;layout->addLayout(form);std::vector<QDoubleSpinBox*> values;
    for(const auto* label:{"X移動 mm","Y移動 mm","Z移動 mm","X回転 °","Y回転 °","Z回転 °"}){
        auto* value=new QDoubleSpinBox(panel);value->setRange(-100000,100000);value->setDecimals(3);value->setKeyboardTracking(false);form->addRow(QString::fromUtf8(label),value);values.push_back(value);}
    auto* transform=new QPushButton(QStringLiteral("選択部品に移動・回転を加える"),panel);layout->addWidget(transform);
    connect(transform,&QPushButton::clicked,this,[this,values]{const int i=view_->Selected();if(i<0){hint_->setText(QStringLiteral("3D上で部品を選んでください。"));return;}
        before_=Snapshot();auto& part=scenes_[current_].parts[i];part.offset=part.offset+kachakacha::v2::geometry::Vector3{values[0]->value(),values[1]->value(),values[2]->value()};
        const auto c=(part.asset->mesh.minimum+part.asset->mesh.maximum)*.5;const QVector3D center(float(c.x),float(c.y),float(c.z));const auto previous=part.rotation.rotatedVector(center);
        part.rotation=QQuaternion::fromEulerAngles(float(values[3]->value()),float(values[4]->value()),float(values[5]->value()))*part.rotation;
        const auto correction=previous-part.rotation.rotatedVector(center);part.offset=part.offset+kachakacha::v2::geometry::Vector3{correction.x(),correction.y(),correction.z()};Remember();view_->Refresh();});
    }
    if(activeTool_=="image")AddImageExportControls(panel);
    auto* help=new QLabel(QStringLiteral("標準は白地・黒い輪郭線、隠れた線は消します。左ドラッグで部品・矢印を移動。矢印はドラッグで配置し、選択・移動で両端の印を調整。\n説明書 .kci は元モデルとは別に保存。"),panel);help->setWordWrap(true);layout->addWidget(help);layout->addStretch();
    window_.OperationHost().ShowTemporaryPage(panel,QStringLiteral("画像エディター — 3Dの場面"));
}
void V2InstructionMode::AddImageExportControls(QWidget* panel){
    auto* layout=static_cast<QVBoxLayout*>(panel->layout());auto* dimensions=new QHBoxLayout;layout->addLayout(dimensions);
    for(const auto* name:{"instructionImageWidth","instructionImageHeight"}){
        dimensions->addWidget(new QLabel(QString(name).endsWith("Width")?QStringLiteral("幅 px"):QStringLiteral("高さ px"),panel));
        auto* value=new QSpinBox(panel);value->setObjectName(name);value->setRange(64,4096);value->setValue(QString(name).endsWith("Width")?1600:1131);dimensions->addWidget(value);}
    auto* output=new QPushButton(QStringLiteral("形式と保存先を選んで画像出力"),panel);layout->addWidget(output);
    connect(output,&QPushButton::clicked,this,[this]{FileAction("instructions.image");});
}
void V2InstructionMode::AddFontControls(QWidget* panel,bool sheet){
    auto* layout=static_cast<QVBoxLayout*>(panel->layout());QFont initial=sheet?pendingItem_.font:textFont_;
    const int selected=sheet?sheetView_->Selected():view_->SelectedMark();
    if(sheet&&selected>=0&&sheets_[currentSheet_].items[selected].kind==InstructionItemKind::Text)initial=sheets_[currentSheet_].items[selected].font;
    if(!sheet&&selected>=0&&!scenes_[current_].marks[selected].arrow)initial=scenes_[current_].marks[selected].font;
    auto* family=new QFontComboBox(panel);family->setObjectName("instructionFont");family->setCurrentFont(initial);layout->addWidget(family);
    auto* row=new QHBoxLayout;layout->addLayout(row);row->addWidget(new QLabel(QStringLiteral("文字 pt"),panel));
    auto* size=new QDoubleSpinBox(panel);size->setObjectName("instructionFontSize");size->setRange(1,300);size->setDecimals(1);size->setKeyboardTracking(false);size->setValue(initial.pointSizeF());row->addWidget(size);
    auto* bold=new QCheckBox(QStringLiteral("太字"),panel);bold->setObjectName("instructionTextBold");bold->setChecked(initial.bold());row->addWidget(bold);
    auto* italic=new QCheckBox(QStringLiteral("斜体"),panel);italic->setChecked(initial.italic());row->addWidget(italic);
    const auto update=[this,sheet,family,size,bold,italic]{if(loading_)return;auto font=family->currentFont();font.setPointSizeF(size->value());font.setBold(bold->isChecked());font.setItalic(italic->isChecked());
        before_=Snapshot();if(sheet){pendingItem_.font=font;const int i=sheetView_->Selected();
            if(i>=0&&sheets_[currentSheet_].items[i].kind==InstructionItemKind::Text)sheets_[currentSheet_].items[i].font=font;
            sheetView_->SetTool(activeTool_,pendingItem_);sheetView_->Refresh();}
        else{textFont_=font;view_->SetTextFont(font);const int i=view_->SelectedMark();if(i>=0&&!scenes_[current_].marks[i].arrow)scenes_[current_].marks[i].font=font;view_->Refresh();}Remember();};
    connect(family,&QFontComboBox::currentFontChanged,this,[update]{update();});connect(size,&QDoubleSpinBox::editingFinished,this,update);
    connect(bold,&QCheckBox::toggled,this,[update]{update();});connect(italic,&QCheckBox::toggled,this,[update]{update();});
    if(sheet)pendingItem_.font=initial;else{textFont_=initial;view_->SetTextFont(initial);}
}
