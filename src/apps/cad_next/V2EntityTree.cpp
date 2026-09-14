#include "V2EntityTree.h"

#include <QAbstractItemView>
#include <QTreeWidget>
#include <QWidget>
#include <QDropEvent>
#include <QTreeWidgetItem>

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
