#include "V2MainWindow.h"
#include "V2EntityTree.h"
#include "V2Viewport.h"
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/document/Commands.h"
#include <QSignalBlocker>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QVariant>
#include <QString>
#include <QObject>
#include <QWidget>
#include <QTreeWidget>
#include <set>
#include <map>
#include <functional>
using namespace kachakacha::v2;
namespace {
bool Category(const QTreeWidgetItem* item){return item&&V2EntityTree::KeyOf(item).contains("/section:");}
bool GroupCategory(const QTreeWidgetItem* item){return Category(item)&&V2EntityTree::KeyOf(item).endsWith("/section:"+QString::number(int(app::ExplorerSection::Groups)));}
struct Targets {std::set<base::EntityId> entities;std::set<base::GroupId> groups;};
void Collect(QTreeWidgetItem* item,Targets& targets){
    if(!item)return;const auto key=V2EntityTree::KeyOf(item);
    if(key.startsWith("group:"))if(auto id=base::GroupId::Parse(key.mid(6).toStdString())){targets.groups.insert(*id);return;}
    if(key.startsWith("entity:"))if(auto id=base::EntityId::Parse(key.mid(7).toStdString())){targets.entities.insert(*id);return;}
    for(int i=0;i<item->childCount();++i)Collect(item->child(i),targets);
}
}
bool V2MainWindow::ApplyExplorerVisibility(const std::vector<QTreeWidgetItem*>& rows,bool visible){
    Targets targets;for(auto* row:rows)Collect(row,targets);
    auto& document=session_->GetDocument();const auto& snapshot=document.Snapshot();
    std::vector<base::EntityId> entities;std::vector<base::GroupId> groups;
    for(const auto id:targets.entities)if(const auto* e=document.FindEntity(id);e&&((e->visibility==domain::Visibility::Visible)!=visible))entities.push_back(id);
    for(const auto id:targets.groups)for(const auto& g:snapshot.groups)if(g.id==id&&g.visible!=visible)groups.push_back(id);
    if(targets.entities.empty()&&targets.groups.empty())return false;
    if(entities.empty()&&groups.empty())return true;
    document::Document::Transaction transaction(document,visible?"まとめて表示":"まとめて非表示");
    for(const auto id:groups){const auto result=document.Run(document::SetGroupVisibilityCommand(id,visible));if(!result.committed){ReportDiagnostics(result.diagnostics);return true;}}
    if(!entities.empty()){const auto result=document.Run(document::SetVisibilityCommand(entities,visible?domain::Visibility::Visible:domain::Visibility::Hidden));if(!result.committed){ReportDiagnostics(result.diagnostics);return true;}}
    if(transaction.Commit()){RefreshExplorerVisibility();SetStatus(visible?QStringLiteral("選んだ項目を表示しました。グループ内の個別設定は維持します。"):
        QStringLiteral("選んだ項目を隠しました。グループ内の個別設定は維持します。"));}
    return true;
}
bool V2MainWindow::ToggleExplorerCategory(QTreeWidgetItem* item){
    return Category(item)&&ApplyExplorerVisibility({item},item->checkState(0)!=Qt::Unchecked);
}
void V2MainWindow::RefreshExplorerCategoryChecks(){
    if(!entityTree_)return;QSignalBlocker block(entityTree_);
    if(auto* child=findChild<QPushButton*>("explorerChildGroup"))child->setEnabled(SelectedGroupId().has_value());
    std::map<QTreeWidgetItem*,std::pair<int,int>> counts;
    for(const auto& [row,id]:entityItems_)if(const auto* entity=session_->GetDocument().FindEntity(id)){
        row->setCheckState(0,entity->visibility==domain::Visibility::Visible?Qt::Checked:Qt::Unchecked);
        for(auto* parent=row->parent();parent;parent=parent->parent())if(Category(parent)&&!GroupCategory(parent)){
            auto& count=counts[parent];++count.first;count.second+=entity->visibility==domain::Visibility::Visible;
        }
    }
    for(const auto& [row,id]:groupItems_)for(const auto& group:session_->GetDocument().Snapshot().groups)if(group.id==id){
        row->setCheckState(0,group.visible?Qt::Checked:Qt::Unchecked);
        if(auto* parent=row->parent();GroupCategory(parent)){auto& count=counts[parent];++count.first;count.second+=group.visible;}
    }
    for(const auto& [row,count]:counts){row->setFlags(row->flags()|Qt::ItemIsUserCheckable);
        row->setCheckState(0,count.second==0?Qt::Unchecked:count.second==count.first?Qt::Checked:Qt::PartiallyChecked);
        row->setToolTip(0,QStringLiteral("チェックでこの分類をまとめて表示・非表示。個別に混在しているときは中間の印になります。"));}
}
void V2MainWindow::RefreshExplorerVisibility(){
    QElapsedTimer timer;timer.start();
    // Visibility changes do not replace tree rows, recalculate geometry, or notify
    // target-picking tools as though the user had picked the same objects again.
    RefreshPartEdges();RefreshWorkPlaneViews();RefreshShapeViews();RefreshExplorerCategoryChecks();
    RefreshExportCounts();RefreshCommandVisibility();viewport_->update();
    setProperty("explorerVisibilityMs",timer.nsecsElapsed()/1e6);
}
void V2MainWindow::CreateSiblingGroup(){
    const auto selected=SelectedGroupId();
    document::Group group;group.id=ids_->NextTyped<base::IdKind::Group>();group.displayName=NextGroupName();
    if(selected)for(const auto& current:session_->GetDocument().Snapshot().groups)if(current.id==*selected)group.parentId=current.parentId;
    if(!selected)if(const auto* entity=EntityForItem(entityTree_->currentItem()))
        if(const auto* current=session_->GetDocument().FindEntity(*entity))group.parentId=current->groupId;
    const auto id=group.id;const auto result=session_->GetDocument().Run(document::AddGroupCommand(std::move(group)));
    if(!result.committed){ReportDiagnostics(result.diagnostics);return;}AdoptCurrentDocument();
    if(auto* row=entityTree_->FindKey("group:"+QString::fromStdString(id.ToString())))entityTree_->setCurrentItem(row);
    SetStatus(QStringLiteral("同じ階層にグループを作りました。F2で名前を変更できます。"));
}
void V2MainWindow::BuildExplorerGroupButtons(QVBoxLayout* layout){
    auto* row=new QHBoxLayout;auto* sibling=new QPushButton(QStringLiteral("新規グループ"),entityTree_);
    sibling->setObjectName("explorerSiblingGroup");sibling->setToolTip(QStringLiteral("同じ階層に空のグループを作成します。既存の項目は移動しません。"));
    auto* child=new QPushButton(QStringLiteral("子グループ"),entityTree_);child->setObjectName("explorerChildGroup");
    child->setToolTip(QStringLiteral("選択したグループの中にグループを作成します。"));child->setEnabled(false);
    QObject::connect(sibling,&QPushButton::clicked,this,[this]{CreateSiblingGroup();});
    QObject::connect(child,&QPushButton::clicked,this,[this]{if(SelectedGroupId())CreateGroupFromSelection();});
    QObject::connect(entityTree_,&QTreeWidget::itemSelectionChanged,child,[this,child]{child->setEnabled(SelectedGroupId().has_value());});
    row->addWidget(sibling);row->addWidget(child);layout->addLayout(row);
}
