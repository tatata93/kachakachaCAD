//! 整理用のまとまり(フォルダ)の実用試験(オーナー指示 2026-09-14 §7〜13、GR-01〜10)。
//!
//! ここで見るのは「機能が存在するか」ではなく **人が使えるか** である。
//! 複数選んで右クリックでまとめられるか。引きずって移せるか。
//! まとまりを解いたときに中身が消えないか。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "V2SelfTest.h"

#include "V2EntityTree.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/Selection.h"

#include <QPointF>
#include <QString>
#include <QTreeWidgetItem>

#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/modeling/ToolController.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::GroupId;
using kachakacha::v2::domain::EntityKind;

//! 線を1本引いて、その物の番号を返す。取れなければ Nil。
[[nodiscard]] EntityId DrawOneLine(V2MainWindow& window, double offsetMm)
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

//! いまある最後のまとまり。
[[nodiscard]] bool LastGroup(V2MainWindow& window, GroupId& out)
{
    const auto& groups = window.Session().GetDocument().Snapshot().groups;
    if (groups.empty()) {
        return false;
    }
    out = groups.back().id;
    return true;
}

//! そのまとまりの行。無ければ空。
[[nodiscard]] QTreeWidgetItem* ItemOfGroup(V2MainWindow& window, const GroupId& id)
{
    for (const auto& entry : window.GroupItems()) {
        if (entry.second == id) {
            return entry.first;
        }
    }
    return nullptr;
}

} // namespace

//! GR-01 / GR-02。複数選んで「グループ化」でまとめられる。
[[nodiscard]] bool CaseGroupFromSelection(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawOneLine(window, 0.0);
    const EntityId second = DrawOneLine(window, 10.0);
    if (!Explain("線が2本引ける", !first.IsNil() && !second.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet both;
    both.entityIds.push_back(first);
    both.entityIds.push_back(second);
    window.Viewport().SetSelection(both);
    window.RunCommand("group.create");
    GroupId group;
    if (!Explain("まとまりができる", LastGroup(window, group))) {
        return false;
    }
    const auto inside = kachakacha::v2::app::EntitiesUnderGroup(
        window.Session().GetDocument().Snapshot(), group);
    if (!Explain((std::string("選んだ2つが入る(実際 ")
                     + std::to_string(inside.size()) + ")").c_str(),
            inside.size() == 2)) {
        return false;
    }
    return Explain("木にまとまりの行が出る", ItemOfGroup(window, group) != nullptr);
}

//! GR-03 / GR-04。引きずって別のまとまりへ移す。入れ子になる。
[[nodiscard]] bool CaseGroupDragAndDrop(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId wire = DrawOneLine(window, 0.0);
    if (!Explain("線が引ける", !wire.IsNil())) {
        return false;
    }
    // 空のまとまりを2つ作る。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("group.create");
    GroupId outer;
    if (!Explain("1つ目ができる", LastGroup(window, outer))) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("group.create");
    GroupId inner;
    if (!Explain("2つ目ができる", LastGroup(window, inner))) {
        return false;
    }
    if (!Explain("2つは別のもの", !(outer == inner))) {
        return false;
    }
    // 2つ目を1つ目の上へ落とす。入れ子になる。
    QTreeWidgetItem* innerItem = ItemOfGroup(window, inner);
    QTreeWidgetItem* outerItem = ItemOfGroup(window, outer);
    if (!Explain("どちらの行もある", innerItem != nullptr && outerItem != nullptr)) {
        return false;
    }
    window.DropTreeItemsOnto({innerItem}, outerItem);
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    const auto depth = kachakacha::v2::app::GroupDepth(snapshot, inner);
    if (!Explain((std::string("入れ子になる(深さ ") + std::to_string(depth) + ")").c_str(),
            depth == 1)) {
        return false;
    }
    // 線を内側のまとまりへ落とす。
    // 行は名前で探さず、窓が持っている対応表から引く。名前は変わりうる。
    QTreeWidgetItem* wireItem = window.ItemOfEntity(wire);
    if (!Explain("線の行がある", wireItem != nullptr)) {
        return false;
    }
    window.DropTreeItemsOnto({wireItem}, ItemOfGroup(window, inner));
    const auto inside = kachakacha::v2::app::EntitiesUnderGroup(
        window.Session().GetDocument().Snapshot(), inner);
    if (!Explain("線が内側へ入る", inside.size() == 1)) {
        return false;
    }
    // 外側から見ても、入れ子の中身まで数える。
    const auto all = kachakacha::v2::app::EntitiesUnderGroup(
        window.Session().GetDocument().Snapshot(), outer);
    return Explain("外側からも数えられる", all.size() == 1);
}

//! GR-06。まとまりを隠すと中身が画面から消える。中身の設定は書き換えない。
[[nodiscard]] bool CaseGroupVisibility(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId wire = DrawOneLine(window, 0.0);
    if (!Explain("線が引ける", !wire.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(wire);
    window.Viewport().SetSelection(one);
    window.RunCommand("group.create");
    GroupId group;
    if (!Explain("まとまりができる", LastGroup(window, group))) {
        return false;
    }
    const int before = static_cast<int>(window.Session().Scene().curves.size());
    QTreeWidgetItem* item = ItemOfGroup(window, group);
    if (!Explain("まとまりの行がある", item != nullptr)) {
        return false;
    }
    // itemChanged が文書を更新して木を作り直す。以後 item は無効なので再利用しない。
    item->setCheckState(0, Qt::Unchecked);
    if (!Explain((std::string("隠すと画面から消える(前 ") + std::to_string(before)
                     + " → 後 " + std::to_string(static_cast<int>(window.Session().Scene().curves.size())) + ")")
                     .c_str(),
            static_cast<int>(window.Session().Scene().curves.size()) < before)) {
        return false;
    }
    // 中身の設定は書き換えていない。
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.id == wire) {
            if (!Explain("中身の出し隠しは触らない",
                    entity.visibility == kachakacha::v2::domain::Visibility::Visible)) {
                return false;
            }
        }
    }
    QTreeWidgetItem* again = ItemOfGroup(window, group);
    if (again != nullptr) {
        // 上と同じく、この呼び出しだけで表示状態の更新まで完了する。
        again->setCheckState(0, Qt::Checked);
    }
    return Explain("出し直すと戻る", static_cast<int>(window.Session().Scene().curves.size()) == before);
}

//! GR-07 / GR-08。まとまりを解いても中身は消えない。取り消しで戻る。
[[nodiscard]] bool CaseGroupDissolveKeepsChildren(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId wire = DrawOneLine(window, 0.0);
    if (!Explain("線が引ける", !wire.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(wire);
    window.Viewport().SetSelection(one);
    window.RunCommand("group.create");
    GroupId group;
    if (!Explain("まとまりができる", LastGroup(window, group))) {
        return false;
    }
    const std::size_t entitiesBefore =
        window.Session().GetDocument().Snapshot().entities.size();
    // 木でまとまりの行を選んでから解く。
    QTreeWidgetItem* item = ItemOfGroup(window, group);
    if (!Explain("まとまりの行がある", item != nullptr)) {
        return false;
    }
    window.EntityTree()->clearSelection();
    item->setSelected(true);
    window.RunCommand("group.dissolve");
    const auto& after = window.Session().GetDocument().Snapshot();
    if (!Explain((std::string("まとまりが消える(残り ")
                     + std::to_string(after.groups.size()) + ")").c_str(),
            after.groups.empty())) {
        return false;
    }
    if (!Explain("中身は消えない", after.entities.size() == entitiesBefore)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("取り消すとまとまりが戻る",
        !window.Session().GetDocument().Snapshot().groups.empty());
}

//! GR-09。保存して開き直してもまとまりの階層が残る。
[[nodiscard]] bool CaseGroupSurvivesSaveAndOpen(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId wire = DrawOneLine(window, 0.0);
    if (!Explain("線が引ける", !wire.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(wire);
    window.Viewport().SetSelection(one);
    window.RunCommand("group.create");
    GroupId inner;
    if (!Explain("まとまりができる", LastGroup(window, inner))) {
        return false;
    }
    // 入れ子にする。
    window.EntityTree()->clearSelection();
    if (QTreeWidgetItem* item = ItemOfGroup(window, inner); item != nullptr) {
        item->setSelected(true);
    }
    window.RunCommand("group.create");
    GroupId outer;
    if (!Explain("入れ子の親ができる", LastGroup(window, outer))) {
        return false;
    }
    const std::size_t groupsBefore =
        window.Session().GetDocument().Snapshot().groups.size();
    if (!Explain("まとまりが2つある", groupsBefore == 2)) {
        return false;
    }
    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_group.kcd2")))) {
        return false;
    }
    const auto& after = window.Session().GetDocument().Snapshot();
    if (!Explain((std::string("まとまりの数が残る(実際 ")
                     + std::to_string(after.groups.size()) + ")").c_str(),
            after.groups.size() == groupsBefore)) {
        return false;
    }
    bool nested = false;
    for (const auto& group : after.groups) {
        if (group.parentId.has_value()) {
            nested = true;
        }
    }
    return Explain("入れ子も残る", nested);
}

//! Q1-Q5 B3。まとまりを作るのと中身を入れるのは1つの操作である。
//! 1回の取り消しで、空のまとまりが残ってはいけない。
[[nodiscard]] bool CaseGroupCreateIsOneUndoStep(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawOneLine(window, 0.0);
    const EntityId second = DrawOneLine(window, 10.0);
    if (!Explain("線が2本引ける", !first.IsNil() && !second.IsNil())) {
        return false;
    }
    const std::size_t groupsBefore =
        window.Session().GetDocument().Snapshot().groups.size();
    kachakacha::v2::app::SelectionSet both;
    both.entityIds.push_back(first);
    both.entityIds.push_back(second);
    window.Viewport().SetSelection(both);
    window.RunCommand("group.create");

    GroupId group;
    if (!Explain("まとまりができる", LastGroup(window, group))) {
        return false;
    }
    if (!Explain("中身が2つ入る",
            kachakacha::v2::app::EntitiesUnderGroup(
                window.Session().GetDocument().Snapshot(), group)
                    .size()
                == 2)) {
        return false;
    }

    window.RunCommand("edit.undo");
    const auto& afterUndo = window.Session().GetDocument().Snapshot();
    if (!Explain((std::string("1回の取り消しでまとまりごと戻る(まとまり ")
                     + std::to_string(afterUndo.groups.size()) + " 個)").c_str(),
            afterUndo.groups.size() == groupsBefore)) {
        return false;
    }
    // 中身だけ戻って空のまとまりが残る、が起きていないこと。
    for (const auto& entity : afterUndo.entities) {
        if (!Explain("線がどのまとまりにも入っていない", !entity.groupId.has_value())) {
            return false;
        }
    }

    window.RunCommand("edit.redo");
    GroupId again;
    if (!Explain("1回のやり直しでまとまりが戻る", LastGroup(window, again))) {
        return false;
    }
    return Explain("中身も一緒に戻る",
        kachakacha::v2::app::EntitiesUnderGroup(
            window.Session().GetDocument().Snapshot(), again)
                .size()
            == 2);
}

//! Q1-Q5 B3。複数を一度に引きずったら、1回の取り消しで全部戻る。
[[nodiscard]] bool CaseGroupDropIsOneUndoStep(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawOneLine(window, 0.0);
    const EntityId second = DrawOneLine(window, 10.0);
    if (!Explain("線が2本引ける", !first.IsNil() && !second.IsNil())) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("group.create");
    GroupId destination;
    if (!Explain("行き先のまとまりができる", LastGroup(window, destination))) {
        return false;
    }
    QTreeWidgetItem* onto = ItemOfGroup(window, destination);
    QTreeWidgetItem* firstItem = window.ItemOfEntity(first);
    QTreeWidgetItem* secondItem = window.ItemOfEntity(second);
    if (!Explain("木に行がそろう",
            onto != nullptr && firstItem != nullptr && secondItem != nullptr)) {
        return false;
    }
    window.DropTreeItemsOnto({firstItem, secondItem}, onto);
    if (!Explain("2つとも移る",
            kachakacha::v2::app::EntitiesUnderGroup(
                window.Session().GetDocument().Snapshot(), destination)
                    .size()
                == 2)) {
        return false;
    }

    window.RunCommand("edit.undo");
    if (!Explain("1回の取り消しで2つとも戻る",
            kachakacha::v2::app::EntitiesUnderGroup(
                window.Session().GetDocument().Snapshot(), destination)
                .empty())) {
        return false;
    }
    window.RunCommand("edit.redo");
    return Explain("1回のやり直しで2つとも戻る",
        kachakacha::v2::app::EntitiesUnderGroup(
            window.Session().GetDocument().Snapshot(), destination)
                .size()
            == 2);
}

//! Q1-Q5 B3。輪になる移動が1つ混じっていたら、ほかも移さない。
//! 半分だけ移った状態にすると、どこまで移ったのかが分からなくなる。
[[nodiscard]] bool CaseGroupDropRollsBackWhenOneIsRefused(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId wire = DrawOneLine(window, 0.0);
    if (!Explain("線が引ける", !wire.IsNil())) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("group.create");
    GroupId outer;
    if (!Explain("親ができる", LastGroup(window, outer))) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("group.create");
    GroupId inner;
    if (!Explain("2つ目ができる", LastGroup(window, inner))) {
        return false;
    }
    // 2つ目を1つ目の下へ引きずって入れ子にする。
    QTreeWidgetItem* outerItem = ItemOfGroup(window, outer);
    QTreeWidgetItem* innerFirst = ItemOfGroup(window, inner);
    if (!Explain("2つの行がある", outerItem != nullptr && innerFirst != nullptr)) {
        return false;
    }
    window.DropTreeItemsOnto({innerFirst}, outerItem);
    bool nested = false;
    for (const auto& group : window.Session().GetDocument().Snapshot().groups) {
        if (group.id == inner && group.parentId.has_value() && *group.parentId == outer) {
            nested = true;
        }
    }
    if (!Explain("入れ子になっている", nested)) {
        return false;
    }

    const std::uint64_t revision = window.Session().GetDocument().Revision();
    // 親を子の下へ落とす。輪になるので断られる。
    // 一緒に線も引きずる。断られたら線も移らないこと。
    QTreeWidgetItem* innerItem = ItemOfGroup(window, inner);
    QTreeWidgetItem* wireItem = window.ItemOfEntity(wire);
    outerItem = ItemOfGroup(window, outer);
    (void)innerFirst;
    if (!Explain("行がそろう",
            innerItem != nullptr && wireItem != nullptr && outerItem != nullptr)) {
        return false;
    }
    window.DropTreeItemsOnto({outerItem, wireItem}, innerItem);

    if (!Explain((std::string("断られたら文書は変わらない(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    const auto* entity = window.Session().GetDocument().FindEntity(wire);
    if (!Explain("線も移っていない", entity != nullptr && !entity->groupId.has_value())) {
        return false;
    }
    for (const auto& group : window.Session().GetDocument().Snapshot().groups) {
        if (group.id == outer) {
            if (!Explain("親は親のまま", !group.parentId.has_value())) {
                return false;
            }
        }
    }
    return Explain("半分だけ移った状態にならない", true);
}

std::vector<SelfTestCase> GroupCases()
{
    return {
        {"複数選んでまとまりにできる", CaseGroupFromSelection},
        {"引きずってまとまりへ移せる", CaseGroupDragAndDrop},
        {"まとまりごと隠しても中身の設定は変わらない", CaseGroupVisibility},
        {"まとまりを解いても中身は消えない", CaseGroupDissolveKeepsChildren},
        {"まとまりの階層が保存して開き直しても残る", CaseGroupSurvivesSaveAndOpen},
        {"まとまりを作るのは1回の取り消しで戻る", CaseGroupCreateIsOneUndoStep},
        {"一度に引きずった分は1回の取り消しで戻る", CaseGroupDropIsOneUndoStep},
        {"1つでも断られたら引きずった分は全部戻る",
            CaseGroupDropRollsBackWhenOneIsRefused},
    };
}

} // namespace kachakacha::v2::selftest
