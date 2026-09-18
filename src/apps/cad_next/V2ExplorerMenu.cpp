//! Model Explorer(左の一覧)の右クリック(正本 3 HTML 2026-09-18、指示書 model_explorer)。
//!
//!   名前変更 / 表示・非表示 / 選択に正対 / グループへ移動 / 複製 / 削除 / プロパティ
//!
//! 並べるのは台帳のコマンドだけ(押せるかどうかの判断も文言も台帳が持つ)。
//! 「グループへ移動」だけは、引きずって落とすのと同じ道(DropTreeItemsOnto)を通す。
//! 消せないときの理由は core(RemoveFeatureCommand)が人の言葉で出す。ここでは言い直さない。

#include "V2MainWindow.h"

#include "V2EditDock.h"
#include "V2EntityTree.h"

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

#include <algorithm>
#include <string>
#include <vector>

namespace {

//! 台帳の QAction を献立に足す。無い命令は足さない(「押せるが何も起きない」を作らない)。
void AddCatalogAction(V2MainWindow& window, QMenu& menu, const char* id)
{
    if (QAction* action = window.ActionFor(id); action != nullptr) {
        menu.addAction(action);
    }
}

} // namespace

void V2MainWindow::ShowExplorerMenu(const QPoint& at)
{
    if (entityTree_ == nullptr) {
        return;
    }
    // 右クリックした行がまだ選ばれていなければ、その行を選ぶ(選択の同期は普段の道)。
    QTreeWidgetItem* under = entityTree_->itemAt(at);
    if (under != nullptr && !under->isSelected() && (under->flags() & Qt::ItemIsSelectable)) {
        entityTree_->setCurrentItem(under);
    }
    QMenu menu(this);
    BuildExplorerMenu(menu);
    if (!menu.isEmpty()) {
        menu.exec(entityTree_->mapToGlobal(at));
    }
}

//! 献立を組むだけ。出すのは呼び出し側なので、試験は exec を通さずに並びを確かめられる。
void V2MainWindow::BuildExplorerMenu(QMenu& menu)
{
    AddCatalogAction(*this, menu, "entity.rename");
    AddCatalogAction(*this, menu, "group.rename");
    menu.addSeparator();
    AddCatalogAction(*this, menu, "view.hide_selected");
    AddCatalogAction(*this, menu, "view.show_all");
    AddCatalogAction(*this, menu, "view.align_selection");
    menu.addSeparator();
    BuildMoveToGroupMenu(menu);
    AddCatalogAction(*this, menu, "group.create");
    AddCatalogAction(*this, menu, "group.dissolve");
    menu.addSeparator();
    AddCatalogAction(*this, menu, "wire.copy");
    AddCatalogAction(*this, menu, "edit.delete");
    menu.addSeparator();
    AddCatalogAction(*this, menu, "measure.open");
    QAction* properties = menu.addAction(QStringLiteral("プロパティ"));
    properties->setStatusTip(QStringLiteral("選んだものの直せる値を右の欄に出します。"));
    QObject::connect(properties, &QAction::triggered, this, [this] { ShowProperties(); });
}

//! 「グループへ移動」の小献立。文書のグループを並べ、選ぶと落としたのと同じ道を通す。
//! グループが1つも無ければ、押せない項目として理由を出す。
void V2MainWindow::BuildMoveToGroupMenu(QMenu& menu)
{
    QMenu* into = menu.addMenu(QStringLiteral("グループへ移動"));
    const auto& snapshot = session_->GetDocument().Snapshot();
    std::vector<QTreeWidgetItem*> moved;
    for (QTreeWidgetItem* item : entityTree_->selectedItems()) {
        if (item != nullptr && (item->flags() & Qt::ItemIsDragEnabled)) {
            moved.push_back(item);
        }
    }
    if (moved.empty()) {
        QAction* none = into->addAction(QStringLiteral("(移せるものを選んでいません)"));
        none->setEnabled(false);
        return;
    }
    for (const auto& [item, groupId] : groupItems_) {
        const kachakacha::v2::document::Group* group = nullptr;
        for (const auto& candidate : snapshot.groups) {
            if (candidate.id == groupId) {
                group = &candidate;
            }
        }
        if (group == nullptr) {
            continue;
        }
        QAction* action = into->addAction(QString::fromStdString(group->displayName));
        QTreeWidgetItem* target = item;
        action->setEnabled(std::find(moved.begin(), moved.end(), target) == moved.end());
        QObject::connect(action, &QAction::triggered, this,
            [this, moved, target] { DropTreeItemsOnto(moved, target); });
    }
    if (snapshot.groups.empty()) {
        QAction* none = into->addAction(QStringLiteral("(グループがありません。先に「グループ化」)"));
        none->setEnabled(false);
    }
    into->addSeparator();
    QAction* out = into->addAction(QStringLiteral("グループの外へ"));
    QObject::connect(out, &QAction::triggered, this,
        [this, moved] { DropTreeItemsOnto(moved, nullptr); });
}

//! プロパティ: 選んだものの直せる値は右の「直す」欄に出る。欄を前に出すだけ。
void V2MainWindow::ShowProperties()
{
    RefreshEditDock();
    if (editDock_ != nullptr) {
        editDock_->show();
        editDock_->raise();
    }
}

std::vector<QString> V2MainWindow::ExplorerMenuLabels()
{
    QMenu menu(this);
    BuildExplorerMenu(menu);
    std::vector<QString> labels;
    for (QAction* action : menu.actions()) {
        if (!action->isSeparator()) {
            labels.push_back(action->text());
        }
    }
    return labels;
}
