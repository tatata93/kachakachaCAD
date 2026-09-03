#pragma once

#include <QDropEvent>
#include <QTreeWidget>

#include <functional>

//! モデルツリー(エクスプローラ風)。ドラッグ&ドロップはQtの内部移動を使わず、
//! onMoveRequested でプロジェクト側(部材グループの所属)を変更してツリーを作り直す。
//! moc を使わない方針のため通知は std::function で行う。
class ModelTreeWidget final : public QTreeWidget {
public:
    explicit ModelTreeWidget(QWidget* parent = nullptr)
        : QTreeWidget(parent)
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
    }

    //! ドロップ時に呼ばれる: (ドラッグ中の選択アイテム, ドロップ先アイテム(無ければnullptr))。
    //! 戻り値は「処理した」印(現状は表示のみに使用)。
    std::function<bool(const QList<QTreeWidgetItem*>&, QTreeWidgetItem*)> onMoveRequested;

protected:
    void dropEvent(QDropEvent* event) override
    {
        // Qt既定のアイテム移動はさせない(モデル側で所属を変えてツリーを再構築する)。
        if (onMoveRequested) {
            onMoveRequested(selectedItems(), itemAt(event->position().toPoint()));
        }
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
    }
};
