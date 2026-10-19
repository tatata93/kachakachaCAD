#include <QString>
#include "V2InstructionMode.h"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <utility>

bool V2InstructionMode::CommitSelectedText() {
    if(sheetView_&&!sheetView_->CommitTextEdit())return false;
    const auto commit=commitText_;return !commit||commit();
}
bool V2InstructionMode::ReadyToSwitch() {
    if(CommitSelectedText())return true;
    const QSignalBlocker pageBlocked(pages_),workspaceBlocked(workspace_);
    pages_->setCurrentIndex(layoutEditing_?currentSheet_:current_);workspace_->setCurrentIndex(layoutEditing_?1:0);
    return false;
}
void V2InstructionMode::FocusSelectedText() {
    const bool needsSettings=activeTool_!="move";
    if(!CommitSelectedText())return;activeTool_="move";
    if(layoutEditing_){if(needsSettings)ShowSettings();sheetView_->SetTool("move");sheetView_->BeginTextEdit();return;}
    ShowSettings();
    if(auto* edit=settings_?settings_->findChild<QPlainTextEdit*>("instructionSelectedText"):nullptr){edit->setFocus();edit->selectAll();}
}
void V2InstructionMode::BuildViewControls(QHBoxLayout* row) {
    for(int direction:{-1,1}) {
        auto* button=new QPushButton(direction<0?QStringLiteral("前へ"):QStringLiteral("後へ"),this);
        button->setObjectName(direction<0?"instructionPageEarlier":"instructionPageLater");
        button->setToolTip(QStringLiteral("現在のシーン・用紙の順序を変更。3D参照は保持します。"));
        row->addWidget(button);connect(button,&QPushButton::clicked,this,[this,direction]{MoveCurrentPage(direction);});
    }
    auto* controls=new QWidget(this);controls->setObjectName("instructionPaperViewControls");
    auto* layout=new QHBoxLayout(controls);layout->setContentsMargins(0,0,0,0);layout->setSpacing(3);
    auto* fit=new QPushButton(QStringLiteral("全体"),controls);fit->setObjectName("instructionPaperFit");
    fit->setToolTip(QStringLiteral("用紙全体を表示。ホイールで拡大、中ボタンで移動できます。"));layout->addWidget(fit);
    auto* zoom=new QSpinBox(controls);zoom->setObjectName("instructionPaperZoom");zoom->setRange(25,800);zoom->setSuffix(" %");zoom->setValue(100);zoom->setKeyboardTracking(false);layout->addWidget(zoom);
    row->addWidget(controls);connect(fit,&QPushButton::clicked,this,[this]{sheetView_->FitView();});
    connect(zoom,&QSpinBox::valueChanged,this,[this](int value){sheetView_->SetZoom(value/100.0);});
    sheetView_->viewChanged=[this,zoom]{const QSignalBlocker blocked(zoom);zoom->setValue(int(sheetView_->Zoom()*100+.5));};
}
void V2InstructionMode::MoveCurrentPage(int direction) {
    if(choosing_||!CommitSelectedText())return;
    const int index=layoutEditing_?currentSheet_:current_;
    const int count=layoutEditing_?int(sheets_.size()):int(scenes_.size());
    const int next=index+direction;if(next<0||next>=count)return;
    CancelInput();before_=Snapshot();commitText_={};view_->SetPage(nullptr);sheetView_->SetDocument(nullptr,nullptr);
    if(layoutEditing_){std::swap(sheets_[index],sheets_[next]);ShowSheet(next);}
    else {std::swap(scenes_[index],scenes_[next]);ShowPage(next);}
    Remember();
}
void V2InstructionMode::AddSheetLayoutControls(QWidget* panel) {
    auto* layout=static_cast<QVBoxLayout*>(panel->layout());
    auto* aspect=new QCheckBox(QStringLiteral("縦横比を保持（ドラッグ・数値入力）"),panel);
    aspect->setObjectName("instructionKeepAspect");aspect->setChecked(sheetView_->KeepAspect());layout->addWidget(aspect);
    connect(aspect,&QCheckBox::toggled,this,[this](bool value){sheetView_->SetKeepAspect(value);});
    auto* row=new QHBoxLayout;layout->addLayout(row);
    for(const auto& action:{std::pair{"back","最背面"},std::pair{"front","最前面"},std::pair{"duplicate","選択物を複製"}}) {
        auto* button=new QPushButton(QString::fromUtf8(action.second),panel);
        button->setObjectName(QStringLiteral("instructionItem_")+action.first);row->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,id=QString::fromLatin1(action.first)]{sheetView_->AdjustItem(id);});
    }
    auto* alignRow=new QHBoxLayout;layout->addLayout(alignRow);auto* align=new QComboBox(panel);align->setObjectName("instructionItemAlign");
    for(const auto& action:{std::pair{"left","用紙の左端"},std::pair{"center_x","用紙の左右中央"},std::pair{"right","用紙の右端"},
        std::pair{"top","用紙の上端"},std::pair{"center_y","用紙の上下中央"},std::pair{"bottom","用紙の下端"}})
        align->addItem(QString::fromUtf8(action.second),QString::fromLatin1(action.first));
    alignRow->addWidget(align);auto* apply=new QPushButton(QStringLiteral("そろえる"),panel);apply->setObjectName("instructionAlignApply");alignRow->addWidget(apply);
    connect(apply,&QPushButton::clicked,this,[this,align]{sheetView_->AdjustItem(align->currentData().toString());});
    auto* help=new QLabel(QStringLiteral("矢印キーで1 mm、Shift＋矢印で10 mm移動。図の順序は最前面・最背面で変更できます。"),panel);help->setWordWrap(true);layout->addWidget(help);
}
