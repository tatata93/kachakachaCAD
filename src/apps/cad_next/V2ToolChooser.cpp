#include "V2OperationPanelHost.h"
#include "V2ToolIcons.h"
#include <QSize>
#include "kachakacha/app/CommandCatalog.h"
#include <QAction>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QKeySequence>
#include <QVariant>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <QSizePolicy>
#include <algorithm>
using namespace kachakacha::v2::app;
namespace {
QString Text(std::string_view s){return QString::fromUtf8(s.data(),static_cast<int>(s.size()));}
}
void V2OperationPanelHost::SetActionLookup(std::function<QAction*(std::string_view)> lookup) {
    actionLookup_=std::move(lookup);const auto mode=chooserMode_.value_or(UiMode::Drawing);
    chooserMode_.reset();SetToolMode(mode);
}
void V2OperationPanelHost::SetToolMode(UiMode mode) {
    if(chooserMode_==mode)return;chooserMode_=mode;
    if(chooser_){chooser_->setObjectName(QStringLiteral("retiredToolChooser"));for(auto* child:chooser_->findChildren<QWidget*>())child->setObjectName(QStringLiteral("retired"));pages_->removeWidget(chooser_);chooser_->hide();chooser_->deleteLater();}
    searchEntries_.clear();
    auto* container=new QWidget(pages_);container->setObjectName(QStringLiteral("toolChooserPage"));
    auto* outer=new QVBoxLayout(container);outer->setContentsMargins(0,0,0,0);
    search_=new QLineEdit(container);search_->setObjectName(QStringLiteral("toolSearch"));search_->setPlaceholderText(QStringLiteral("道具を検索（Ctrl+K）"));
    search_->setClearButtonEnabled(true);outer->addWidget(search_);
    allModes_=new QCheckBox(QStringLiteral("全モードから探す"),container);allModes_->setObjectName(QStringLiteral("toolSearchAllModes"));outer->addWidget(allModes_);
    searchCount_=new QLabel(container);outer->addWidget(searchCount_);
    auto* scroll=new QScrollArea(container);scroll->setObjectName(QStringLiteral("idleToolChooser"));scroll->setWidgetResizable(true);outer->addWidget(scroll,1);
    auto* body=new QWidget(scroll);auto* layout=new QVBoxLayout(body);scroll->setWidget(body);
    for(auto toolMode:AllUiModes())for(const auto& category:RibbonCategoriesFor(toolMode)){
        auto* group=new QGroupBox(Text(UiModeNameJa(toolMode))+QStringLiteral(" / ")+Text(category.labelJa),body);
        group->setProperty("commonTools",category.key=="find_view");
        group->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
        auto* grid=new QGridLayout(group);grid->setAlignment(Qt::AlignTop);int index=0;
        for(bool extra:{false,true})for(const auto& tool:category.tools){
            if(tool.extra!=extra)continue;
            auto* button=new QPushButton(Text(tool.labelJa),group);button->setObjectName(QStringLiteral("toolChoice"));button->setMinimumHeight(36);
            button->setIcon(V2ToolIcon(tool.commandId,tool.labelJa,tool.surfaceMethod.value_or(-1)));
            button->setIconSize(QSize(24,24));button->setAccessibleName(Text(tool.labelJa));
            button->setProperty("commandId",Text(tool.commandId));
            const auto* command=FindCommand(tool.commandId);const QString guide=command?Text(command->operationGuideJa):Text(tool.blockedReasonJa);
            auto* action=actionLookup_?actionLookup_(tool.commandId):nullptr;
            const auto refresh=[button,action,tool,guide]{
                // Hidden menu actions report disabled in Qt; preserve their logical availability.
                const bool available=!action||(action->property("toolAvailable").isValid()
                    ?action->property("toolAvailable").toBool():action->isEnabled());
                button->setEnabled(!tool.Blocked()&&available);
                QString help=tool.Blocked()?Text(tool.blockedReasonJa):(action?action->toolTip():guide);
                if(action&&!action->shortcut().isEmpty())help+=QStringLiteral("\n")+action->shortcut().toString();
                button->setToolTip(help);};
            refresh();if(action)connect(action,&QAction::changed,button,refresh);
            connect(button,&QPushButton::clicked,this,[this,toolMode,tool]{if(toolHandler_)toolHandler_(toolMode,tool);});
            grid->addWidget(button,index/2,index%2);++index;
            searchEntries_.push_back({toolMode,group,button,Text(tool.labelJa)+QStringLiteral(" ")+Text(category.labelJa)+QStringLiteral(" ")+guide+QStringLiteral(" ")+Text(tool.commandId)});
        }
        layout->addWidget(group);
    }
    layout->addStretch();chooser_=container;pages_->addWidget(chooser_);
    connect(search_,&QLineEdit::textChanged,this,[this]{FilterTools();});
    connect(allModes_,&QCheckBox::toggled,this,[this]{FilterTools();});
    connect(search_,&QLineEdit::returnPressed,this,[this]{
        for(const auto& entry:searchEntries_)if(entry.button->isVisible()&&entry.button->isEnabled()) {entry.button->click();return;}});
    FilterTools();if(current_==Shelf::None)pages_->setCurrentWidget(chooser_);
}
void V2OperationPanelHost::FilterTools() {
    const auto query=search_->text().trimmed();int count=0;std::map<QGroupBox*,int> positions;
    for(const auto& entry:searchEntries_){entry.group->hide();entry.group->layout()->removeWidget(entry.button);}
    for(const auto& entry:searchEntries_){
        const bool match=((allModes_->isChecked()&&!entry.group->property("commonTools").toBool())||entry.mode==chooserMode_)&&(query.isEmpty()||entry.text.contains(query,Qt::CaseInsensitive));
        entry.button->setVisible(match);if(match){entry.group->show();const int at=positions[entry.group]++;static_cast<QGridLayout*>(entry.group->layout())->addWidget(entry.button,at/2,at%2);++count;}
    }
    searchCount_->setText(count?QStringLiteral("%1件 — 上下の道具は同じ操作です").arg(count):QStringLiteral("該当する道具がありません。別の名前か全モードで探してください。"));
}
void V2OperationPanelHost::FocusToolSearch() {
    SetShelves({});allModes_->setChecked(true);search_->setFocus();search_->selectAll();
}
