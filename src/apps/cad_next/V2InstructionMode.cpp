#include <QPointer>
#include <QSignalBlocker>
#include <QQuaternion>
#include <QWidget>
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QMessageBox>
#include <QJsonArray>
#include <QEvent>
#include <QObject>
#include <QVector3D>
#include <QString>
#include <algorithm>
using kachakacha::v2::app::UiMode;
namespace {
V2InstructionMode* Find(V2MainWindow& window){for(auto* child:window.centralWidget()->children())if(auto* mode=dynamic_cast<V2InstructionMode*>(child))return mode;return nullptr;}
}
void V2InstructionMode::Sync(V2MainWindow& window){
    auto* mode=Find(window);const bool active=window.Mode()==UiMode::Instructions;
    if(!mode&&active)mode=new V2InstructionMode(window);if(!mode)return;
    mode->setVisible(active);window.Viewport().setVisible(!active);
    if(active){if(mode->choosing_)mode->ShowModelPicker();else mode->ShowSettings();}
    else if(mode->settings_)window.OperationHost().ClearTemporaryPage();
}
void V2InstructionMode::SetPathChooser(V2MainWindow& window,std::function<QString(bool)> chooser){
    auto* mode=Find(window);if(!mode){mode=new V2InstructionMode(window);mode->hide();}mode->pathChooser_=std::move(chooser);
}
bool V2InstructionMode::Run(V2MainWindow& window,std::string_view id){
    if(id.starts_with("instructions.")){if(window.Mode()!=UiMode::Instructions)window.SetMode(UiMode::Instructions);Find(window)->Command(id);return true;}
    if(window.Mode()!=UiMode::Instructions)return false;auto* mode=Find(window);if(!mode)return false;
    if(id=="edit.undo")mode->Undo(false);else if(id=="edit.redo")mode->Undo(true);
    else if(id=="edit.delete")mode->Command("instructions.remove");
    else if(id=="file.save"||id=="file.save_as")mode->FileAction("instructions.save");
    else if(id=="file.open")mode->FileAction("instructions.open");
    else if(id=="selection.activate")mode->Command("instructions.move");
    else if(id=="tools.search"){window.OperationHost().SetShelves({});window.OperationHost().FocusToolSearch();}
    else if(id=="view.fit_all"){mode->before_=mode->Snapshot();mode->view_->Fit();mode->Remember();}
    else if(mode->hint_)mode->hint_->setText(QStringLiteral("説明書の3D配置を編集しています。元モデルの編集は作図・部品モードに戻ってください。"));return true;
}
V2InstructionMode::V2InstructionMode(V2MainWindow& window):QWidget(window.centralWidget()),window_(window){
    setObjectName("instructionWorkspace");auto* layout=new QVBoxLayout(this);auto* row=new QHBoxLayout;layout->addLayout(row);
    pages_=new QComboBox(this);pages_->setObjectName("instructionPages");row->addWidget(pages_);
    title_=new QLineEdit(this);title_->setPlaceholderText(QStringLiteral("手順の見出し"));row->addWidget(title_,1);
    view_=new V2InstructionScene(this);layout->addWidget(view_,1);
    static_cast<QVBoxLayout*>(window.centralWidget()->layout())->insertWidget(0,this,1);qApp->installEventFilter(this);
    connect(pages_,&QComboBox::currentIndexChanged,this,[this](int i){if(!loading_)ShowPage(i);});
    connect(title_,&QLineEdit::textEdited,this,[this]{if(loading_)return;before_=Snapshot();scenes_[current_].title=title_->text();pages_->setItemText(current_,title_->text());Remember();});
    view_->beginChange=[this]{before_=Snapshot();};view_->endChange=[this]{if(!choosing_)Remember();};
    NewPage(false);dirty_=false;undo_.clear();
}
void V2InstructionMode::NewPage(bool copy){
    if(scenes_.size()>=200){if(hint_)hint_->setText(QStringLiteral("説明書は200コマまでです。別の説明書に分けてください。"));return;}
    before_=Snapshot();InstructionPage page;if(copy&&!scenes_.empty())page=scenes_[current_];page.title=QStringLiteral("手順 %1").arg(scenes_.size()+1);
    scenes_.push_back(std::move(page));loading_=true;pages_->addItem(scenes_.back().title);loading_=false;ShowPage(int(scenes_.size())-1);Remember();
}
void V2InstructionMode::ShowPage(int index){
    if(index<0||index>=int(scenes_.size()))return;choosing_=false;current_=index;loading_=true;
    pages_->setCurrentIndex(index);title_->setText(scenes_[index].title);loading_=false;view_->SetPage(&scenes_[index]);view_->SetTool("move");activeTool_="move";
    if(isVisible())ShowSettings();
}
void V2InstructionMode::ShowSettings(){
    auto* panel=new QWidget;settings_=panel;auto* layout=new QVBoxLayout(panel);
    hint_=new QLabel(QStringLiteral("モデルを選ぶ → 使う部品を選ぶ → 3D配置。右ドラッグ:回転 / 中ドラッグ:平行移動 / ホイール:拡大。"),panel);hint_->setWordWrap(true);layout->addWidget(hint_);
    if(!scenes_[current_].legacyPng.isEmpty())hint_->setText(QStringLiteral("旧版の2D説明図を背景として保持しています。3D配置にはモデルを読み込んでください。"));
    if(activeTool_=="text"){
    auto* text=new QLineEdit(panel);text->setObjectName("instructionText");text->setPlaceholderText(QStringLiteral("番号・説明文（ツール選択後、3D上をクリック）"));layout->addWidget(text);
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
        auto* value=new QDoubleSpinBox(panel);value->setRange(-100000,100000);value->setDecimals(3);form->addRow(QString::fromUtf8(label),value);values.push_back(value);}
    auto* transform=new QPushButton(QStringLiteral("選択部品に移動・回転を加える"),panel);layout->addWidget(transform);
    connect(transform,&QPushButton::clicked,this,[this,values]{const int i=view_->Selected();if(i<0){hint_->setText(QStringLiteral("3D上で部品を選んでください。"));return;}
        before_=Snapshot();auto& part=scenes_[current_].parts[i];part.offset=part.offset+kachakacha::v2::geometry::Vector3{values[0]->value(),values[1]->value(),values[2]->value()};
        const auto c=(part.asset->mesh.minimum+part.asset->mesh.maximum)*.5;const QVector3D center(float(c.x),float(c.y),float(c.z));const auto previous=part.rotation.rotatedVector(center);
        part.rotation=QQuaternion::fromEulerAngles(float(values[3]->value()),float(values[4]->value()),float(values[5]->value()))*part.rotation;
        const auto correction=previous-part.rotation.rotatedVector(center);part.offset=part.offset+kachakacha::v2::geometry::Vector3{correction.x(),correction.y(),correction.z()};Remember();view_->Refresh();});
    }
    if(activeTool_=="image"){
    auto* dimensions=new QHBoxLayout;layout->addLayout(dimensions);
    for(const auto* name:{"instructionImageWidth","instructionImageHeight"}){dimensions->addWidget(new QLabel(QString(name).endsWith("Width")?QStringLiteral("幅 px"):QStringLiteral("高さ px"),panel));auto* value=new QSpinBox(panel);value->setObjectName(name);value->setRange(64,4096);value->setValue(QString(name).endsWith("Width")?1600:1131);dimensions->addWidget(value);}
    auto* output=new QPushButton(QStringLiteral("形式と保存先を選んで画像出力"),panel);layout->addWidget(output);
    connect(output,&QPushButton::clicked,this,[this]{FileAction("instructions.image");});
    }
    auto* help=new QLabel(QStringLiteral("標準は白地・黒い輪郭線、隠れた線は消します。左ドラッグで部品・矢印を移動。矢印は表面、空白では視点中心の配置面に置きます。\n説明書 .kci は元モデルとは別に保存。"),panel);help->setWordWrap(true);layout->addWidget(help);layout->addStretch();
    window_.OperationHost().ShowTemporaryPage(panel,QStringLiteral("組み立て説明書 — 3D配置"));
}
void V2InstructionMode::Command(std::string_view id){
    if(id=="instructions.capture"){Capture();return;}
    if(id=="instructions.model_file"){FileAction(id);return;}
    if(choosing_){if(hint_)hint_->setText(QStringLiteral("部品を選んで「このコマに配置」または「取消」を押してください。"));return;}
    if(id=="instructions.move"||id=="instructions.arrow"||id=="instructions.text"||id=="instructions.image"){
        activeTool_=QString::fromUtf8(id.data()+13,int(id.size()-13));ShowSettings();
    }else if(!settings_||!settings_->isVisible())ShowSettings();
    if(id=="instructions.image")return;
    if(id=="instructions.open"||id=="instructions.save"||id=="instructions.pdf" ){FileAction(id);return;}
    if(id=="instructions.new_page"){NewPage(false);return;}if(id=="instructions.duplicate"){NewPage(true);return;}
    if(id=="instructions.arrow"){view_->SetTool("arrow");hint_->setText(QStringLiteral("3D上で矢印の始点 → 終点をクリック。右ドラッグで視点変更、Escで中止。"));return;}
    if(id=="instructions.text"){auto* input=settings_->findChild<QLineEdit*>("instructionText");view_->SetTool("text",input?input->text():QString());hint_->setText(QStringLiteral("文字を置く位置を3D上でクリックしてください。"));return;}
    view_->SetTool("move");before_=Snapshot();if(id=="instructions.remove"){
        const int part=view_->Selected(),mark=view_->SelectedMark();if(part>=0)scenes_[current_].parts.erase(scenes_[current_].parts.begin()+part);
        else if(mark>=0)scenes_[current_].marks.erase(scenes_[current_].marks.begin()+mark);view_->SetSelected(-1);view_->Refresh();}Remember();
}
void V2InstructionMode::Remember(){if(loading_||before_==Snapshot())return;undo_.push_back(before_);if(undo_.size()>30)undo_.erase(undo_.begin());redo_.clear();dirty_=true;before_=Snapshot();}
void V2InstructionMode::Undo(bool redo){if(choosing_)return;auto& from=redo?redo_:undo_;auto& to=redo?undo_:redo_;if(from.empty())return;to.push_back(Snapshot());const auto saved=from.back();from.pop_back();Restore(saved);dirty_=true;}
bool V2InstructionMode::eventFilter(QObject* object,QEvent* event){
    if(object==&window_&&event->type()==QEvent::Close&&!CheckSaved()){static_cast<QCloseEvent*>(event)->ignore();return true;}
    if(!isVisible()||event->type()!=QEvent::KeyPress)return false;auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window()||qobject_cast<QLineEdit*>(object))return false;
    const auto* key=static_cast<QKeyEvent*>(event);if((key->modifiers()&Qt::ControlModifier)&&(key->key()==Qt::Key_Z||key->key()==Qt::Key_Y)){Undo(key->key()==Qt::Key_Y||(key->modifiers()&Qt::ShiftModifier));return true;}
    if(key->key()==Qt::Key_Escape){if(choosing_)ShowPage(current_);else view_->SetTool("move");return true;}
    if(key->key()==Qt::Key_Delete){Command("instructions.remove");return true;}return false;
}
bool V2InstructionMode::CheckSaved(){
    if(!dirty_)return true;const auto answer=QMessageBox::question(&window_,QStringLiteral("説明書の保存"),QStringLiteral("説明書の変更を保存しますか？"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel);
    if(answer==QMessageBox::Cancel)return false;if(answer==QMessageBox::Discard)return true;FileAction("instructions.save");return !dirty_;
}
