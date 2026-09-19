//! Model Explorer(左の一覧)の人の道(HP-XP)。正本 3 HTML(2026-09-18)、指示書 model_explorer。
//!
//! 節の並び(原点が先頭)、1つずつの ◉ での出し隠し、3D ↔ 一覧の選択の同期、右クリックの献立。
//! 見えている行を実際に触る。文書の値は core の Command を通ってしか変えない。

#include "V2SelfTest.h"

#include "V2EntityTree.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"

#include <QApplication>
#include <QPointF>
#include <QString>
#include <QTreeWidgetItem>

#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Visibility;

//! 線を1本引いて、その物の番号を返す。取れなければ Nil。
[[nodiscard]] EntityId DrawLineAt(V2MainWindow& window, double offsetMm)
{
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    const auto first = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{-20.0, offsetMm, 0.0});
    const auto second = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, offsetMm, 0.0});
    if (!first.has_value() || !second.has_value()) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return EntityId{};
    }
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    EntityId made;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            made = entity.id;
        }
    }
    return made;
}

[[nodiscard]] Visibility VisibilityOf(V2MainWindow& window, const EntityId& id)
{
    const auto* entity = window.Session().GetDocument().FindEntity(id);
    return entity == nullptr ? Visibility::Hidden : entity->visibility;
}

//! HP-XP-01。節は正本の並びで、原点が先頭。文書の行が根。
[[nodiscard]] bool CaseExplorerSectionsInOrder(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const char* expected[8] = {"原点", "作業面", "グループ", "ワイヤー", "面", "立体", "近似", "生成物"};
    if (!Explain((std::string("節が8つ以上(実際 ") + std::to_string(window.GroupRowCount()) + ")").c_str(),
            window.GroupRowCount() >= 8)) {
        return false;
    }
    for (int row = 0; row < 8; ++row) {
        if (!Explain((std::string("節の並び ") + expected[row] + "(実際 "
                         + window.GroupRowText(row).toStdString() + ")").c_str(),
                window.GroupRowText(row) == QString::fromUtf8(expected[row]))) {
            return false;
        }
    }
    const auto rows = window.ExplorerRows();
    if (!Explain("節の見出しは選べない(ものではない)",
            !(rows[3]->flags() & Qt::ItemIsSelectable))) {
        return false;
    }
    return Explain("根は文書の行", window.EntityTree()->topLevelItemCount() == 1
        && window.EntityTree()->topLevelItem(0)->text(1) == QStringLiteral("文書"));
}

//! HP-XP-02。引いた線はワイヤーの節に出て、◉ を外すと文書の visibility が Hidden になる。
//! 取り消しで戻る(Command を通っている証拠)。
[[nodiscard]] bool CaseExplorerToggleVisibilityPerEntity(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId line = DrawLineAt(window, 0.0);
    if (!Explain("線が引ける", !line.IsNil())) {
        return false;
    }
    QTreeWidgetItem* item = window.ItemOfEntity(line);
    if (!Explain("線の行がある", item != nullptr)
        || !Explain("線の行はワイヤーの節の下",
            item->parent() != nullptr && item->parent()->text(0) == QStringLiteral("ワイヤー"))
        || !Explain("最初は ◉ が付いている", item->checkState(0) == Qt::Checked)) {
        return false;
    }
    item->setCheckState(0, Qt::Unchecked);
    QApplication::processEvents();
    if (!Explain("◉ を外すと文書で隠れる", VisibilityOf(window, line) == Visibility::Hidden)) {
        return false;
    }
    QTreeWidgetItem* rebuilt = window.ItemOfEntity(line);
    if (!Explain("作り直した行も ◉ が外れている",
            rebuilt != nullptr && rebuilt->checkState(0) == Qt::Unchecked)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("取り消すと出る", VisibilityOf(window, line) == Visibility::Visible)) {
        return false;
    }
    rebuilt = window.ItemOfEntity(line);
    return Explain("行の ◉ も戻る", rebuilt != nullptr && rebuilt->checkState(0) == Qt::Checked);
}

//! HP-XP-03。3D で押すと一覧の行が選ばれ、一覧で選ぶと 3D でも選ばれる。
[[nodiscard]] bool CaseExplorerSelectionSyncBothWays(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId a = DrawLineAt(window, 0.0);
    const EntityId b = DrawLineAt(window, 10.0);
    if (!Explain("線が2本引ける", !a.IsNil() && !b.IsNil())) {
        return false;
    }
    if (!Explain("3D で1本目を押せる", ClickOnCurveOf(window, a))) {
        return false;
    }
    QTreeWidgetItem* rowA = window.ItemOfEntity(a);
    QTreeWidgetItem* rowB = window.ItemOfEntity(b);
    if (!Explain("一覧で1本目の行が選ばれる", rowA != nullptr && rowA->isSelected())
        || !Explain("2本目の行は選ばれない", rowB != nullptr && !rowB->isSelected())) {
        return false;
    }
    window.EntityTree()->clearSelection();
    rowB->setSelected(true);
    QApplication::processEvents();
    const auto& picked = window.Viewport().Selection().entityIds;
    return Explain("一覧で2本目を選ぶと 3D も2本目だけ",
        picked.size() == 1 && picked.front() == b);
}

//! HP-XP-04。右クリックの献立は正本の順(名前変更/表示・非表示/正対/グループへ移動/複製/削除/プロパティ)。
//! 「グループへ移動」で実際にグループへ入る(落としたのと同じ道)。
[[nodiscard]] bool CaseExplorerContextMenuOrderAndMove(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId line = DrawLineAt(window, 0.0);
    if (!Explain("線が引ける", !line.IsNil()) || !Explain("3D で押せる", ClickOnCurveOf(window, line))) {
        return false;
    }
    const std::vector<QString> labels = window.ExplorerMenuLabels();
    const char* wanted[] = {"名前を変える", "選択を隠す", "選択に正対", "グループへ移動", "削除", "プロパティ"};
    std::size_t at = 0;
    for (const QString& label : labels) {
        if (at < 6 && label.contains(QString::fromUtf8(wanted[at]))) {
            ++at;
        }
    }
    if (!Explain((std::string("献立が正本の順(見つかった ") + std::to_string(at) + "/6)").c_str(), at == 6)) {
        return false;
    }
    // グループを作ってから、線を献立の道で入れる。
    window.Viewport().SetSelection({});
    window.RunCommand("group.create");
    if (!Explain("グループができる", !window.GroupItems().empty())) {
        return false;
    }
    if (!Explain("3D で線を押せる", ClickOnCurveOf(window, line))) {
        return false;
    }
    std::vector<QTreeWidgetItem*> moved{window.ItemOfEntity(line)};
    window.DropTreeItemsOnto(moved, window.GroupItems().front().first);
    const auto* entity = window.Session().GetDocument().FindEntity(line);
    return Explain("線がグループに入る", entity != nullptr && entity->groupId.has_value())
        && Explain("線の行がグループの行の下",
            window.ItemOfEntity(line) != nullptr
                && window.ItemOfEntity(line)->parent() == window.GroupItems().front().first);
}

} // namespace

std::vector<SelfTestCase> ExplorerCases()
{
    return {
        {"HP-XP-01 一覧の節は正本の順で原点が先頭", CaseExplorerSectionsInOrder},
        {"HP-XP-02 ◉ で1つずつ出し隠しでき、取り消しで戻る", CaseExplorerToggleVisibilityPerEntity},
        {"HP-XP-03 3D と一覧の選択が両方向に同期する", CaseExplorerSelectionSyncBothWays},
        {"HP-XP-04 右クリックの献立は正本の順で、グループへ移せる", CaseExplorerContextMenuOrderAndMove},
    };
}

} // namespace kachakacha::v2::selftest
