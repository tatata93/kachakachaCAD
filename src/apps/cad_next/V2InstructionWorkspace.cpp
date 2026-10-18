#include <QString>
#include <QWidget>
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2ToolIcons.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

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
    ShowPage(0);dirty_=false;before_=Snapshot();
}
void V2InstructionMode::DeletePage()
{
    CancelInput();
    const QString name=layoutEditing_?sheets_[currentSheet_].title:scenes_[current_].title;
    int references=0;
    if(!layoutEditing_)for(const auto& sheet:sheets_)for(const auto& item:sheet.items)
        if(item.kind==InstructionItemKind::Scene&&item.sceneId==scenes_[current_].id)++references;
    const QString message=QStringLiteral("「%1」を削除しますか？%2\n削除後はCtrl+Zで戻せます。")
        .arg(name,references?QStringLiteral("\nこの場面を使っている用紙上の図%1個も削除します。").arg(references):QString());
    if(QMessageBox::question(&window_,QStringLiteral("説明書のページ削除"),message,
        QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
    before_=Snapshot();view_->SetPage(nullptr);sheetView_->SetDocument(nullptr,nullptr);
    if(layoutEditing_){
        sheets_.erase(sheets_.begin()+currentSheet_);
        if(sheets_.empty()){sheets_.push_back(InstructionSheet{});sheets_[0].title=QStringLiteral("用紙 1");}
        ShowSheet(std::min(currentSheet_,int(sheets_.size())-1));
    }else{
        const auto id=scenes_[current_].id;
        for(auto& sheet:sheets_)std::erase_if(sheet.items,[id](const auto& item){return item.kind==InstructionItemKind::Scene&&item.sceneId==id;});
        scenes_.erase(scenes_.begin()+current_);
        if(scenes_.empty()){scenes_.push_back(InstructionPage{});scenes_[0].title=QStringLiteral("場面 1");}
        ShowPage(std::min(current_,int(scenes_.size())-1));
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
    auto* apply=new QPushButton(QStringLiteral("文章を適用"),panel);apply->setObjectName("instructionApplySelectedText");layout->addWidget(apply);
    connect(apply,&QPushButton::clicked,this,[this,sheet,index,edit]{
        if(!AcceptTextInput(edit->toPlainText()))return;
        before_=Snapshot();if(sheet)sheets_[currentSheet_].items[index].text=edit->toPlainText();else scenes_[current_].marks[index].text=edit->toPlainText();
        Remember();if(sheet)sheetView_->Refresh();else view_->Refresh();
    });
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
