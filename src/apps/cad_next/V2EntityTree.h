#pragma once

//! 左のモデルツリー。引きずって移せる木(オーナー指示 2026-09-14 §9)。
//!
//! Qt に行を動かさせない。木は文書から毎回作り直すので、木だけ動かしても
//! 次の作り直しで元へ戻る。**落ちた先を窓へ知らせて、文書のほうを変える。**
//! こうすると、Undo/Redo も保存も、ふつうの操作と同じ道に乗る。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include <QTreeWidget>

#include <functional>
#include <vector>

class QDropEvent;
class QTreeWidgetItem;

class V2EntityTree final : public QTreeWidget {
public:
    explicit V2EntityTree(QWidget* parent);

    //! 引きずったものが落ちたときに呼ぶもの。
    //! 第1引数は動かしたもの、第2引数は落ちた先の行(空なら最上位)。
    void SetDropHandler(
        std::function<void(const std::vector<QTreeWidgetItem*>&, QTreeWidgetItem*)> handler);

    //! 試験から呼ぶ。本物の引きずりを使わずに同じ道を通す。
    void DropOnto(const std::vector<QTreeWidgetItem*>& moved, QTreeWidgetItem* onto);

protected:
    void dropEvent(QDropEvent* event) override;

private:
    std::function<void(const std::vector<QTreeWidgetItem*>&, QTreeWidgetItem*)> dropHandler_;
};
