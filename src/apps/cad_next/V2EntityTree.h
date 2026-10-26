#pragma once

//! 左のモデルツリー。引きずって移せる木(オーナー指示 2026-09-14 §9)。
//!
//! Qt に行を動かさせない。木は文書から毎回作り直すので、木だけ動かしても
//! 次の作り直しで元へ戻る。**落ちた先を窓へ知らせて、文書のほうを変える。**
//! こうすると、Undo/Redo も保存も、ふつうの操作と同じ道に乗る。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include <QTreeWidget>
#include <QString>
#include <map>
#include <QPersistentModelIndex>
#include <set>

#include <functional>
#include <vector>

class QDropEvent;
class QTreeWidgetItem;
class QMouseEvent;

class V2EntityTree final : public QTreeWidget {
public:
    explicit V2EntityTree(QWidget* parent);

    //! 引きずったものが落ちたときに呼ぶもの。
    //! 第1引数は動かしたもの、第2引数は落ちた先の行(空なら最上位)。
    void SetDropHandler(
        std::function<void(const std::vector<QTreeWidgetItem*>&, QTreeWidgetItem*)> handler);

    //! 試験から呼ぶ。本物の引きずりを使わずに同じ道を通す。
    void DropOnto(const std::vector<QTreeWidgetItem*>& moved, QTreeWidgetItem* onto);

    // View state uses document/UUID/category keys, independent of labels and visibility.
    static void Key(QTreeWidgetItem* item, const QString& key);
    static QString KeyOf(const QTreeWidgetItem* item);
    void BeginRebuild(const QString& documentId);
    void EndRebuild();
    void RegisterAppendedRow(QTreeWidgetItem* item);
    bool BeginFilter(const QString& term);
    static bool AncestorsExpanded(const QTreeWidgetItem* item);
    QTreeWidgetItem* FindKey(const QString& key) const;
    void SetChangeHandler(std::function<void(QTreeWidgetItem*)> handler);

protected:
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    std::function<void(const std::vector<QTreeWidgetItem*>&, QTreeWidgetItem*)> dropHandler_;
    std::map<QString, bool> expanded_, beforeFilter_;
    QString documentId_, filter_;
    int scroll_ = 0;
    QString currentKey_;
    std::set<QString> selectedKeys_;
    std::map<QString,QPersistentModelIndex> rows_;
    struct Change {QString text;Qt::CheckState check;};
    std::map<QString,Change> changes_;
    std::function<void(QTreeWidgetItem*)> changed_;
    bool changeQueued_=false;
    QString checkKey_;
    Qt::CheckState checkAtPress_=Qt::Unchecked;
    bool CheckHit(QTreeWidgetItem* row,const QPoint& point) const;
    void FlushChanges();
};
