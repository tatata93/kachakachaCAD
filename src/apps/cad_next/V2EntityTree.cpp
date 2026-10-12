#include "V2EntityTree.h"

#include <QAbstractItemView>
#include <QTreeWidget>
#include <QWidget>
#include <QDropEvent>
#include <QTreeWidgetItem>
#include <QScrollBar>
#include <QTreeWidgetItemIterator>
#include <QVariant>
#include <QString>

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
        scroll_=0; return;
    }
    for(QTreeWidgetItemIterator it(this); *it; ++it) {
        const auto key=KeyOf(*it);
        if(!key.isEmpty()&&HasExpansionState(*it))expanded_[key]=(*it)->isExpanded();
    }
    scroll_=verticalScrollBar()->value();
}
void V2EntityTree::EndRebuild() {
    for(QTreeWidgetItemIterator it(this); *it; ++it) {
        const auto key=KeyOf(*it);const auto found=expanded_.find(key);
        (*it)->setExpanded(found==expanded_.end() || found->second);
    }
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
