#include "V2EntityTree.h"
#include <QPoint>

#include <QAbstractItemView>
#include <QTreeWidget>
#include <QWidget>
#include <QDropEvent>
#include <QTreeWidgetItem>
#include <QScrollBar>
#include <QTreeWidgetItemIterator>
#include <QVariant>
#include <QString>
#include <QTimer>
#include <QSignalBlocker>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QRect>
#include <QObject>
#include <QPersistentModelIndex>
#include <utility>

namespace {
bool HasExpansionState(const QTreeWidgetItem* item) {
    const auto key=V2EntityTree::KeyOf(item);
    return item->childCount()>0||key.contains("/section:")||key.startsWith("group:")||key=="document";
}
}

V2EntityTree::V2EntityTree(QWidget* parent) : QTreeWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    // 木の中だけで動かす。外から落ちてくるものは受けない。
    setDragDropMode(QAbstractItemView::InternalMove);
    setExpandsOnDoubleClick(false);setAutoExpandDelay(650);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    QObject::connect(this,&QTreeWidget::itemChanged,this,[this](auto* item,int column){
        if(column!=0||!item)return;const auto key=KeyOf(item);if(key.isEmpty())return;
        changes_.insert_or_assign(key,Change{item->text(0),item->checkState(0)});
        if(changeQueued_)return;changeQueued_=true;QTimer::singleShot(0,this,[this]{FlushChanges();});
    });
}

void V2EntityTree::SetDropHandler(
    std::function<void(const std::vector<QTreeWidgetItem*>&, QTreeWidgetItem*)> handler)
{
    dropHandler_ = std::move(handler);
}

void V2EntityTree::DropOnto(const std::vector<QTreeWidgetItem*>& moved,
    QTreeWidgetItem* onto)
{
    if (dropHandler_) {
        dropHandler_(moved, onto);
    }
}

void V2EntityTree::dropEvent(QDropEvent* event)
{
    // Qt に行を動かさせない。動かしても、次に文書から作り直したときに戻る。
    // 「動いたように見えたのに戻る」は、いちばん分かりにくい失敗である。
    std::vector<QTreeWidgetItem*> moved;
    const auto chosen = selectedItems();
    moved.reserve(static_cast<std::size_t>(chosen.size()));
    for (QTreeWidgetItem* item : chosen) {
        moved.push_back(item);
    }
    QTreeWidgetItem* onto = itemAt(event->position().toPoint());
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    DropOnto(moved, onto);
}

void V2EntityTree::Key(QTreeWidgetItem* item, const QString& key) {
    item->setData(0, Qt::UserRole + 23, key);
}
QString V2EntityTree::KeyOf(const QTreeWidgetItem* item) {
    return item ? item->data(0, Qt::UserRole + 23).toString() : QString();
}
void V2EntityTree::BeginRebuild(const QString& documentId) {
    if(documentId_ != documentId) {
        expanded_.clear(); beforeFilter_.clear(); filter_.clear(); documentId_=documentId;
        currentKey_.clear();selectedKeys_.clear();rows_.clear();changes_.clear();scroll_=0; return;
    }
    for(QTreeWidgetItemIterator it(this); *it; ++it) {
        const auto key=KeyOf(*it);
        if(!key.isEmpty()&&HasExpansionState(*it))expanded_[key]=(*it)->isExpanded();
    }
    scroll_=verticalScrollBar()->value();
    currentKey_=KeyOf(currentItem());selectedKeys_.clear();
    for(auto* item:selectedItems())selectedKeys_.insert(KeyOf(item));rows_.clear();
}
void V2EntityTree::EndRebuild() {
    for(QTreeWidgetItemIterator it(this); *it; ++it) {
        const auto key=KeyOf(*it);const auto found=expanded_.find(key);
        (*it)->setExpanded(found==expanded_.end() || found->second);
        rows_.insert_or_assign(key,QPersistentModelIndex(indexFromItem(*it)));
        if(selectedKeys_.contains(key))(*it)->setSelected(true);
    }
    if(auto* item=FindKey(currentKey_))setCurrentItem(item,0,QItemSelectionModel::NoUpdate);
    verticalScrollBar()->setValue(scroll_);
}
bool V2EntityTree::BeginFilter(const QString& term) {
    if(term==filter_)return false;
    if(filter_.isEmpty()&&!term.isEmpty()) {
        beforeFilter_.clear();
        for(QTreeWidgetItemIterator it(this); *it; ++it)
            if(!KeyOf(*it).isEmpty()&&HasExpansionState(*it))beforeFilter_[KeyOf(*it)]=(*it)->isExpanded();
    }
    if(term.isEmpty()) {
        for(QTreeWidgetItemIterator it(this); *it; ++it) {
            const auto found=beforeFilter_.find(KeyOf(*it));
            if(found!=beforeFilter_.end())(*it)->setExpanded(found->second);
        }
        beforeFilter_.clear();
    }
    filter_=term;return !term.isEmpty();
}
bool V2EntityTree::AncestorsExpanded(const QTreeWidgetItem* item) {
    for(auto* parent=item->parent();parent;parent=parent->parent())
        if(!parent->isExpanded())return false;
    return true;
}
QTreeWidgetItem* V2EntityTree::FindKey(const QString& key) const {
    const auto found=rows_.find(key);return found==rows_.end()||!found->second.isValid()?nullptr:itemFromIndex(found->second);
}
void V2EntityTree::SetChangeHandler(std::function<void(QTreeWidgetItem*)> handler){changed_=std::move(handler);}
void V2EntityTree::FlushChanges(){
    changeQueued_=false;const auto pending=std::exchange(changes_,{});
    for(const auto& [key,change]:pending)if(auto* item=FindKey(key)){
        {QSignalBlocker block(this);item->setText(0,change.text);item->setCheckState(0,change.check);}
        if(changed_)changed_(item);
    }
}
bool V2EntityTree::CheckHit(QTreeWidgetItem* row,const QPoint& point) const{
    if(!row||!(row->flags()&Qt::ItemIsUserCheckable))return false;
    QStyleOptionViewItem option;initViewItemOption(&option);option.rect=visualItemRect(row);
    option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=row->checkState(0);
    return style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,this).contains(point);
}
void V2EntityTree::mousePressEvent(QMouseEvent* event){
    if(event->button()==Qt::LeftButton)if(auto* item=itemAt(event->position().toPoint());event->button()==Qt::LeftButton&&CheckHit(item,event->position().toPoint())){
        checkKey_=KeyOf(item);checkAtPress_=item->checkState(0);event->accept();return;
    }
    auto* row=itemAt(event->position().toPoint());
    const auto key=KeyOf(row);
    const bool folder=row&&key.startsWith("group:")&&!key.contains("/section:");
    const bool plain=event->button()==Qt::LeftButton&&event->modifiers()==Qt::NoModifier;
    const bool label=row&&event->position().x()>=visualItemRect(row).left();
    const bool expanded=row&&row->isExpanded();
    QTreeWidget::mousePressEvent(event);
    if(folder&&plain&&label)if(auto* current=FindKey(key))current->setExpanded(!expanded);
}
void V2EntityTree::mouseMoveEvent(QMouseEvent* event){
    if(!checkKey_.isEmpty()){event->accept();return;}
    QTreeWidget::mouseMoveEvent(event);
}
void V2EntityTree::mouseReleaseEvent(QMouseEvent* event){
    if(!checkKey_.isEmpty()){
        const auto key=std::exchange(checkKey_,{});
        if(event->button()==Qt::LeftButton)if(auto* row=FindKey(key);CheckHit(row,event->position().toPoint())&&row->checkState(0)==checkAtPress_)
            row->setCheckState(0,checkAtPress_==Qt::Checked?Qt::Unchecked:Qt::Checked);
        event->accept();return;
    }
    QTreeWidget::mouseReleaseEvent(event);
}
void V2EntityTree::mouseDoubleClickEvent(QMouseEvent* event){
    if(event->button()==Qt::LeftButton&&CheckHit(itemAt(event->position().toPoint()),event->position().toPoint())){
        mousePressEvent(event);return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}
