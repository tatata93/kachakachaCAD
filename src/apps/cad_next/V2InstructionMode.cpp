#include <QAbstractSpinBox>
#include <QUuid>
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
    else {mode->CancelInput();if(mode->settings_)window.OperationHost().ClearTemporaryPage();}
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
    else if(id=="view.fit_all"){mode->CancelInput();mode->before_=mode->Snapshot();if(mode->layoutEditing_)mode->sheetView_->Refresh();else mode->view_->Fit();mode->Remember();}
    else if(mode->hint_)mode->hint_->setText(QStringLiteral("説明書の3D配置を編集しています。元モデルの編集は作図・部品モードに戻ってください。"));return true;
}
V2InstructionMode::V2InstructionMode(V2MainWindow& window):QWidget(window.centralWidget()),window_(window){
    setObjectName("instructionWorkspace");auto* layout=new QVBoxLayout(this);auto* row=new QHBoxLayout;layout->addLayout(row);
    workspace_=new QComboBox(this);workspace_->setObjectName("instructionEditorMode");
    workspace_->addItems({QStringLiteral("画像エディター"),QStringLiteral("用紙編集")});row->addWidget(workspace_);
    pages_=new QComboBox(this);pages_->setObjectName("instructionPages");row->addWidget(pages_);
    title_=new QLineEdit(this);title_->setPlaceholderText(QStringLiteral("場面・用紙の見出し"));row->addWidget(title_,1);
    view_=new V2InstructionScene(this);layout->addWidget(view_,1);
    sheetView_=new V2InstructionSheetView(this);layout->addWidget(sheetView_,1);sheetView_->hide();
    static_cast<QVBoxLayout*>(window.centralWidget()->layout())->insertWidget(0,this,1);qApp->installEventFilter(this);
    connect(workspace_,&QComboBox::currentIndexChanged,this,[this](int i){if(!loading_)SwitchWorkspace(i==1);});
    connect(pages_,&QComboBox::currentIndexChanged,this,[this](int i){if(!loading_){if(layoutEditing_)ShowSheet(i);else ShowPage(i);}});
    connect(title_,&QLineEdit::textEdited,this,[this]{if(loading_)return;before_=Snapshot();
        if(layoutEditing_)sheets_[currentSheet_].title=title_->text();else scenes_[current_].title=title_->text();
        pages_->setItemText(pages_->currentIndex(),title_->text());Remember();});
    view_->beginChange=[this]{before_=Snapshot();};view_->endChange=[this]{if(!choosing_)Remember();};
    sheetView_->beginChange=[this]{before_=Snapshot();};sheetView_->endChange=[this]{Remember();};
    sheetView_->selectionChanged=[this]{if(activeTool_=="move")ShowSettings();};
    sheetView_->refused=[this](const QString& text){if(hint_)hint_->setText(text);};
    view_->refused=[this](const QString& text){if(hint_)hint_->setText(text);};
    sheetView_->openScene=[this](QUuid id){for(std::size_t i=0;i<scenes_.size();++i)if(scenes_[i].id==id){ShowPage(int(i));break;}};
    NewPage(false);sheets_.push_back(InstructionSheet{});sheets_[0].title=QStringLiteral("用紙 1");dirty_=false;undo_.clear();
}
void V2InstructionMode::NewPage(bool copy){
    if(layoutEditing_){NewSheet(copy);return;}
    if(scenes_.size()>=200){if(hint_)hint_->setText(QStringLiteral("場面は200個までです。別の説明書に分けてください。"));return;}
    CancelInput();before_=Snapshot();InstructionPage page;if(copy&&!scenes_.empty())page=scenes_[current_];
    page.id=QUuid::createUuid();for(auto& part:page.parts)part.id=QUuid::createUuid();
    page.title=QStringLiteral("場面 %1").arg(scenes_.size()+1);scenes_.push_back(std::move(page));ShowPage(int(scenes_.size())-1);Remember();
}
void V2InstructionMode::ShowPage(int index){
    if(index<0||index>=int(scenes_.size()))return;CancelInput();choosing_=false;current_=index;layoutEditing_=false;loading_=true;
    workspace_->setCurrentIndex(0);pages_->clear();for(const auto& page:scenes_)pages_->addItem(page.title);
    pages_->setCurrentIndex(index);title_->setText(scenes_[index].title);loading_=false;
    sheetView_->hide();view_->show();view_->SetPage(&scenes_[index]);view_->SetTool("move");activeTool_="move";
    if(isVisible())ShowSettings();
}
void V2InstructionMode::SwitchWorkspace(bool layout){
    if(scenes_.empty())return;if(layout){if(sheets_.empty()){sheets_.push_back(InstructionSheet{});sheets_[0].title=QStringLiteral("用紙 1");}
        ShowSheet(std::clamp(currentSheet_,0,int(sheets_.size())-1));}
    else ShowPage(std::clamp(current_,0,int(scenes_.size())-1));
}
void V2InstructionMode::NewSheet(bool copy){
    if(sheets_.size()>=200){if(hint_)hint_->setText(QStringLiteral("用紙は200枚までです。"));return;}
    CancelInput();before_=Snapshot();InstructionSheet sheet;if(copy&&!sheets_.empty())sheet=sheets_[currentSheet_];
    sheet.id=QUuid::createUuid();for(auto& item:sheet.items)item.id=QUuid::createUuid();
    sheet.title=QStringLiteral("用紙 %1").arg(sheets_.size()+1);sheets_.push_back(std::move(sheet));ShowSheet(int(sheets_.size())-1);Remember();
}
void V2InstructionMode::ShowSheet(int index){
    if(index<0||index>=int(sheets_.size()))return;CancelInput();choosing_=false;currentSheet_=index;layoutEditing_=true;loading_=true;
    workspace_->setCurrentIndex(1);pages_->clear();for(const auto& sheet:sheets_)pages_->addItem(sheet.title);
    pages_->setCurrentIndex(index);title_->setText(sheets_[index].title);loading_=false;
    view_->hide();sheetView_->show();sheetView_->SetDocument(&sheets_[index],&scenes_);sheetView_->SetTool("move");activeTool_="move";
    if(isVisible())ShowSettings();
}
void V2InstructionMode::ShowSettings(){
    const bool previous=loading_;loading_=true;
    if(settings_){settings_->blockSignals(true);for(auto* child:settings_->findChildren<QObject*>())child->blockSignals(true);}
    if(layoutEditing_)ShowSheetSettings();else ShowSceneSettings();
    loading_=previous;
}
void V2InstructionMode::Command(std::string_view id){
    if(id=="instructions.scene_editor"||id=="instructions.sheet_editor"){SwitchWorkspace(id=="instructions.sheet_editor");return;}
    if(id=="instructions.capture"){SwitchWorkspace(false);Capture();return;}
    if(id=="instructions.model_file"){SwitchWorkspace(false);FileAction(id);return;}
    if(choosing_){if(hint_)hint_->setText(QStringLiteral("部品を選んで「この場面に配置」または「取消」を押してください。"));return;}
    if(id=="instructions.open"||id=="instructions.save"||id=="instructions.pdf"){FileAction(id);return;}
    if(id=="instructions.new_page"){NewPage(false);return;}if(id=="instructions.duplicate"){NewPage(true);return;}
    if(id=="instructions.insert_image"){if(!layoutEditing_)SwitchWorkspace(true);InsertImage();return;}
    if(id=="instructions.place_scene"||id=="instructions.paper")if(!layoutEditing_)SwitchWorkspace(true);
    if(id=="instructions.arrow"&&layoutEditing_){if(hint_)hint_->setText(QStringLiteral("矢印は画像エディターで場面に配置してください。"));return;}
    if(id=="instructions.remove"){
        if(layoutEditing_)sheetView_->CancelInput();else view_->CancelInput();
        before_=Snapshot();if(layoutEditing_){const int item=sheetView_->Selected();if(item>=0)sheets_[currentSheet_].items.erase(sheets_[currentSheet_].items.begin()+item);sheetView_->SetSelected(-1);sheetView_->Refresh();}
        else{const int part=view_->Selected(),mark=view_->SelectedMark();if(part>=0)scenes_[current_].parts.erase(scenes_[current_].parts.begin()+part);
            else if(mark>=0)scenes_[current_].marks.erase(scenes_[current_].marks.begin()+mark);view_->SetSelected(-1);view_->Refresh();}
        Remember();ShowSettings();return;
    }
    activeTool_=QString::fromUtf8(id.data()+13,int(id.size()-13));
    if(id=="instructions.image"||id=="instructions.paper"){if(layoutEditing_)sheetView_->SetTool("move");else view_->SetTool("move");}
    ShowSettings();
    if(id=="instructions.image"||id=="instructions.paper")return;
    if(layoutEditing_){sheetView_->SetTool(activeTool_,pendingItem_);return;}
    if(id=="instructions.arrow"){view_->SetTool("arrow");hint_->setText(QStringLiteral("始点から先端へドラッグして離します。2クリックでも配置できます。選択・移動で両端の印を調整。"));return;}
    if(id=="instructions.text"){auto* input=settings_->findChild<QLineEdit*>("instructionText");view_->SetTool("text",input?input->text():QString());return;}
    view_->SetTool("move");
}
void V2InstructionMode::Remember(){if(loading_||before_==Snapshot())return;undo_.push_back(before_);if(undo_.size()>30)undo_.erase(undo_.begin());redo_.clear();dirty_=true;before_=Snapshot();}
void V2InstructionMode::CancelInput(){view_->CancelInput();sheetView_->CancelInput();}
void V2InstructionMode::Undo(bool redo){if(choosing_)return;CancelInput();auto& from=redo?redo_:undo_;auto& to=redo?undo_:redo_;if(from.empty())return;to.push_back(Snapshot());const auto saved=from.back();from.pop_back();Restore(saved);dirty_=true;}
bool V2InstructionMode::eventFilter(QObject* object,QEvent* event){
    if(object==&window_&&event->type()==QEvent::Close&&!CheckSaved()){static_cast<QCloseEvent*>(event)->ignore();return true;}
    if(!isVisible()||event->type()!=QEvent::KeyPress)return false;auto* widget=qobject_cast<QWidget*>(object);if(!widget||widget->window()!=window()||qobject_cast<QLineEdit*>(object)||qobject_cast<QAbstractSpinBox*>(object))return false;
    const auto* key=static_cast<QKeyEvent*>(event);if((key->modifiers()&Qt::ControlModifier)&&(key->key()==Qt::Key_Z||key->key()==Qt::Key_Y)){Undo(key->key()==Qt::Key_Y||(key->modifiers()&Qt::ShiftModifier));return true;}
    if(key->key()==Qt::Key_Escape){if(choosing_)ShowPage(current_);else if(layoutEditing_)sheetView_->CancelInput();else view_->CancelInput();return true;}
    if(key->key()==Qt::Key_Delete){Command("instructions.remove");return true;}return false;
}
bool V2InstructionMode::CheckSaved(){
    if(!dirty_)return true;const auto answer=QMessageBox::question(&window_,QStringLiteral("説明書の保存"),QStringLiteral("説明書の変更を保存しますか？"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel);
    if(answer==QMessageBox::Cancel)return false;if(answer==QMessageBox::Discard)return true;FileAction("instructions.save");return !dirty_;
}
