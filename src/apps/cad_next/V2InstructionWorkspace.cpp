#include <QPointer>
#include <QUuid>
#include <QString>
#include <QWidget>
#include "V2InstructionMode.h"
#include "V2InstructionSceneBrowser.h"
#include "V2MainWindow.h"
#include "V2ToolIcons.h"
#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

void V2InstructionMode::BuildWorkspaceActions(QHBoxLayout* row)
{
    const auto add=[this,row](const char* text,const char* name,const char* command){
        auto* button=new QPushButton(QString::fromUtf8(text),this);button->setObjectName(name);
        row->addWidget(button);connect(button,&QPushButton::clicked,this,[this,command]{Command(command);});
    };
    add("追加","instructionAddPage","instructions.new_page");
    add("複製","instructionDuplicatePage","instructions.duplicate");
    add("ページ削除","instructionDeletePage","instructions.delete_page");
}
void V2InstructionMode::RefreshWorkspaceActions()
{
    if(auto* controls=findChild<QWidget*>("instructionPaperViewControls"))controls->setVisible(layoutEditing_);
    if(auto* zoom=findChild<QSpinBox*>("instructionPaperZoom")){const QSignalBlocker blocked(zoom);zoom->setValue(int(sheetView_->Zoom()*100+.5));}
    const int count=layoutEditing_?int(sheets_.size()):int(scenes_.size());const int index=layoutEditing_?currentSheet_:current_;
    if(auto* button=findChild<QPushButton*>("instructionPageEarlier"))button->setEnabled(!choosing_&&index>0);
    if(auto* button=findChild<QPushButton*>("instructionPageLater"))button->setEnabled(!choosing_&&index+1<count);
    if(!workspaceActions_)return;
    while(auto* item=workspaceActions_->takeAt(0)){if(item->widget()){item->widget()->hide();item->widget()->deleteLater();}delete item;}
    const auto add=[this](const char* text,const char* id){
        auto* button=new QToolButton(this);button->setText(QString::fromUtf8(text));
        button->setToolTip(QString::fromUtf8(text));button->setIcon(V2ToolIcon(id,text));
        button->setIconSize({24,24});button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setObjectName(QStringLiteral("instructionAction_")+id);
        workspaceActions_->addWidget(button);button->show();
        connect(button,&QToolButton::clicked,this,[this,id]{Command(id);});
    };
    add("選択・移動","instructions.move");
    if(layoutEditing_){add("場面を配置","instructions.place_scene");add("写真を貼る","instructions.insert_image");add("用紙サイズ","instructions.paper");}
    else {add("現在モデル","instructions.capture");add("別モデル","instructions.model_file");add("矢印","instructions.arrow");}
    add("文章","instructions.text");add("画像出力","instructions.image");add("PDF出力","instructions.pdf");
    add("開く","instructions.open");add("保存","instructions.save");
}
void V2InstructionMode::SyncPlacementInput()
{
    if(choosing_)return;
    if(activeTool_=="text"){
        if(layoutEditing_){pendingItem_.text=sheetText_;sheetView_->SetTool("text",pendingItem_);}
        else view_->SetTool("text",sceneText_);
    }
}
void V2InstructionMode::NewDocument()
{
    if(!CheckSaved())return;
    CancelInput();view_->SetPage(nullptr);sheetView_->SetDocument(nullptr,nullptr);
    scenes_={InstructionPage{}};scenes_[0].title=QStringLiteral("場面 1");
    sheets_={InstructionSheet{}};sheets_[0].title=QStringLiteral("用紙 1");
    current_=currentSheet_=0;path_.clear();undo_.clear();redo_.clear();choosing_=false;
    sceneBrowser_->Invalidate();
    ShowPage(0);dirty_=false;before_=Snapshot();
}
void V2InstructionMode::RefreshSceneBrowser()
{
    sceneBrowser_->Refresh(&scenes_,current_,layoutEditing_,choosing_);
}
void V2InstructionMode::ChooseBrowserScene(int index,bool place)
{
    if(choosing_||index<0||index>=int(scenes_.size()))return;
    if(!place&&!layoutEditing_){if(current_!=index)ShowPage(index);return;}
    if(!place&&layoutEditing_&&activeTool_!="place_scene"){
        if(!CommitSelectedText())return;current_=index;RefreshSceneBrowser();return;
    }
    if(layoutEditing_&&activeTool_=="place_scene"&&pendingItem_.sceneId==scenes_[index].id)return;
    CancelInput();current_=index;Command("instructions.place_scene");
}
void V2InstructionMode::SyncBrowserVisibility(bool active)
{
    if(active==browserActive_)return;
    if(auto* dock=window_.findChild<QDockWidget*>("entityDock")){
        if(active){entityDockVisible_=!dock->isHidden();dock->hide();}
        else dock->setVisible(entityDockVisible_);
    }
    browserActive_=active;
}
void V2InstructionMode::DeletePage(bool sceneFromBrowser)
{
    if(choosing_||!ReadyToSwitch())return;
    CancelInput();
    const bool deleteSheet=layoutEditing_&&!sceneFromBrowser;
    const QString name=deleteSheet?sheets_[currentSheet_].title:scenes_[current_].title;
    int references=0;
    if(!deleteSheet)for(const auto& sheet:sheets_)for(const auto& item:sheet.items)
        if(item.kind==InstructionItemKind::Scene&&item.sceneId==scenes_[current_].id)++references;
    const QString message=QStringLiteral("「%1」を削除しますか？%2\n削除後はCtrl+Zで戻せます。")
        .arg(name,references?QStringLiteral("\nこの場面を使っている用紙上の図%1個も削除します。").arg(references):QString());
    if(QMessageBox::question(&window_,QStringLiteral("説明書のページ削除"),message,
        QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
    before_=Snapshot();view_->SetPage(nullptr);sheetView_->SetDocument(nullptr,nullptr);
    if(deleteSheet){
        sheets_.erase(sheets_.begin()+currentSheet_);
        if(sheets_.empty()){sheets_.push_back(InstructionSheet{});sheets_[0].title=QStringLiteral("用紙 1");}
        ShowSheet(std::min(currentSheet_,int(sheets_.size())-1));
    }else{
        const auto id=scenes_[current_].id;
        for(auto& sheet:sheets_)std::erase_if(sheet.items,[id](const auto& item){return item.kind==InstructionItemKind::Scene&&item.sceneId==id;});
        scenes_.erase(scenes_.begin()+current_);
        if(scenes_.empty()){scenes_.push_back(InstructionPage{});scenes_[0].title=QStringLiteral("場面 1");}
        current_=std::min(current_,int(scenes_.size())-1);
        if(layoutEditing_)ShowSheet(currentSheet_);else ShowPage(current_);
    }
    Remember();
}
bool V2InstructionMode::AcceptTextInput(const QString& text)
{
    if(!text.trimmed().isEmpty()&&text.size()<=100000)return true;
    if(hint_)hint_->setText(QStringLiteral("文章は1文字以上、100000文字以内で入力してください。"));return false;
}
void V2InstructionMode::AddSelectedTextControls(QWidget* panel,bool sheet)
{
    const int index=sheet?sheetView_->Selected():view_->SelectedMark();
    if(index<0)return;
    const bool text=sheet?sheets_[currentSheet_].items[index].kind==InstructionItemKind::Text:!scenes_[current_].marks[index].arrow;
    if(!text)return;
    auto* layout=static_cast<QVBoxLayout*>(panel->layout());
    layout->addWidget(new QLabel(QStringLiteral("選択した文章を編集（新しく配置しません）"),panel));
    AddFontControls(panel,sheet);
    auto* edit=new QPlainTextEdit(panel);edit->setMaximumHeight(100);edit->setObjectName("instructionSelectedText");
    edit->setPlainText(sheet?sheets_[currentSheet_].items[index].text:scenes_[current_].marks[index].text);layout->addWidget(edit);
    auto accepted=std::make_shared<QString>(edit->toPlainText());
    connect(edit,&QPlainTextEdit::textChanged,this,[this,edit,accepted]{
        if(edit->toPlainText().size()<=100000){*accepted=edit->toPlainText();return;}
        const QSignalBlocker blocked(edit);edit->setPlainText(*accepted);
        if(hint_)hint_->setText(QStringLiteral("100000文字を超える入力は適用しませんでした。文章を短くするか分けてください。"));
    });
    const auto sceneId=scenes_[current_].id,sheetId=sheets_[currentSheet_].id;
    const auto itemId=sheet?sheets_[currentSheet_].items[index].id:QUuid{};
    commitText_=[this,sheet,index,sceneId,sheetId,itemId,input=QPointer<QPlainTextEdit>(edit)]{
        if(!input)return true;
        QString* target=nullptr;
        if(sheet){for(auto& paper:sheets_)if(paper.id==sheetId)for(auto& item:paper.items)
            if(item.id==itemId&&item.kind==InstructionItemKind::Text)target=&item.text;}
        else for(auto& scene:scenes_)if(scene.id==sceneId&&index<int(scene.marks.size())&&!scene.marks[index].arrow)target=&scene.marks[index].text;
        if(!target||*target==input->toPlainText())return true;
        if(!AcceptTextInput(input->toPlainText()))return false;
        before_=Snapshot();*target=input->toPlainText();Remember();
        if(sheet)sheetView_->Refresh();else view_->Refresh();return true;
    };
    auto* apply=new QPushButton(QStringLiteral("文章を適用"),panel);apply->setObjectName("instructionApplySelectedText");layout->addWidget(apply);
    connect(apply,&QPushButton::clicked,this,[this]{CommitSelectedText();});
}
void V2InstructionMode::ShowSceneSelection(QWidget* panel)
{
    auto* layout=static_cast<QVBoxLayout*>(panel->layout());
    auto* marks=new QComboBox(panel);marks->setObjectName("instructionMarks");marks->addItem(QStringLiteral("矢印・文章を選択"));
    for(const auto& mark:scenes_[current_].marks)marks->addItem(mark.arrow?QStringLiteral("矢印"):mark.text.left(30));
    marks->setCurrentIndex(view_->SelectedMark()+1);layout->addWidget(marks);
    connect(marks,&QComboBox::currentIndexChanged,this,[this](int i){view_->SetSelectedMark(i-1);});
    AddSelectedTextControls(panel,false);
    auto* remove=new QPushButton(QStringLiteral("選択した部品・矢印・文章を削除"),panel);layout->addWidget(remove);
    connect(remove,&QPushButton::clicked,this,[this]{Command("instructions.remove");});
}
