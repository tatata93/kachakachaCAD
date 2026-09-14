//! 整理用のまとまり(フォルダ)の操作(オーナー指示 2026-09-14 §7〜13)。
//!
//! まとまりは **幾何ではない。** 入れても出しても、依存も参照も所有も変わらない。
//! まとまりを解いても中身は消えない。
//!
//! 引きずって落としたときも、Qt に行を動かさせず **文書のほうを変える。**
//! 木は文書から毎回作り直すので、木だけ動かしても次の作り直しで元へ戻る。
//! 「動いたように見えたのに戻る」は、いちばん分かりにくい失敗である。

#include "V2MainWindow.h"

#include "V2EntityTree.h"
#include "V2Viewport.h"

#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"

#include <QString>
#include <QTreeWidgetItem>

#include <optional>
#include <string>
#include <vector>

namespace {

using kachakacha::v2::base::GroupId;

//! 木の行から「まとまり」を引く。まとまりの行でなければ値を返さない。
[[nodiscard]] std::optional<GroupId> GroupOfItem(
    const std::vector<std::pair<QTreeWidgetItem*, GroupId>>& items,
    const QTreeWidgetItem* item)
{
    for (const auto& entry : items) {
        if (entry.first == item) {
            return entry.second;
        }
    }
    return std::nullopt;
}

} // namespace

bool V2MainWindow::IsGroupCommand(std::string_view id)
{
    return id == "group.create" || id == "group.dissolve" || id == "group.rename";
}

void V2MainWindow::RunGroupCommand(std::string_view id)
{
    if (id == "group.create") {
        CreateGroupFromSelection();
        return;
    }
    if (id == "group.dissolve") {
        DissolveSelectedGroup();
        return;
    }
    RenameSelectedGroup();
}

//! 選んだものをまとめる。何も選んでいなければ空のまとまりを作る。
//!
//! 「複数選んで右クリック → グループ化」で作れること(§9)。
//! 独自の分かりにくい管理ダイアログを必須にしない。
void V2MainWindow::CreateGroupFromSelection()
{
    using kachakacha::v2::document::AddGroupCommand;
    using kachakacha::v2::document::MoveEntitiesToGroupCommand;

    kachakacha::v2::document::Group group;
    group.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Group>();
    // いま選んでいるまとまりの下へ入れる。入れ子はここで作れる。
    group.parentId = SelectedGroupId();
    group.displayName = NextGroupName();
    const auto id = group.id;
    // まとまりを作るのと、中身を入れるのは、人から見れば1つの操作である。
    // 別々に入れると、1回の取り消しで中身だけ戻り、空のまとまりが残る。
    const auto chosen = viewport_->Selection().entityIds;
    bool ok = true;
    {
        kachakacha::v2::document::Document::Transaction transaction(
            session_->GetDocument(), "まとまりにする");
        const auto added = session_->GetDocument().Run(AddGroupCommand(std::move(group)));
        if (!added.committed) {
            ReportDiagnostics(added.diagnostics);
            ok = false;
        }
        if (ok && !chosen.empty()) {
            const auto moved = session_->GetDocument().Run(
                MoveEntitiesToGroupCommand(chosen, id));
            if (!moved.committed) {
                ReportDiagnostics(moved.diagnostics);
                ok = false;
            }
        }
        if (ok) {
            ok = transaction.Commit();
        }
        // Commit していなければ、まとまりも中身の移動もまとめて無かったことになる。
    }
    AdoptCurrentDocument();
    if (!ok) {
        return;
    }
    SetStatus(chosen.empty()
            ? QStringLiteral("まとまりを作りました。名前は F2 で変えられます。")
            : QStringLiteral("%1個をまとまりにしました。名前は F2 で変えられます。")
                  .arg(static_cast<int>(chosen.size())));
}

//! まとまりを解く。**中身は消さない。** 親のまとまりへ戻す(§11)。
void V2MainWindow::DissolveSelectedGroup()
{
    const auto group = SelectedGroupId();
    if (!group.has_value()) {
        SetStatus(QStringLiteral("まとまりを解く: 左の一覧でまとまりの行を選んでください。"));
        return;
    }
    const auto removed = session_->GetDocument().Run(
        kachakacha::v2::document::RemoveGroupCommand(*group));
    if (!removed.committed) {
        ReportDiagnostics(removed.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("まとまりを解きました。中身は残っています。"));
}

void V2MainWindow::RenameSelectedGroup()
{
    const auto group = SelectedGroupId();
    if (!group.has_value()) {
        SetStatus(QStringLiteral("名前を変える: 左の一覧でまとまりの行を選んでください。"));
        return;
    }
    // 行をその場で書き換えるのが本筋。ここは献立から呼ばれたときの入口である。
    for (const auto& entry : groupItems_) {
        if (entry.second == *group && entityTree_ != nullptr) {
            entityTree_->editItem(entry.first, 0);
            return;
        }
    }
}

//! いま左の一覧で選んでいるまとまり。選んでいなければ値を返さない。
std::optional<kachakacha::v2::base::GroupId> V2MainWindow::SelectedGroupId() const
{
    if (entityTree_ == nullptr) {
        return std::nullopt;
    }
    for (QTreeWidgetItem* item : entityTree_->selectedItems()) {
        if (const auto group = GroupOfItem(groupItems_, item); group.has_value()) {
            return group;
        }
    }
    return std::nullopt;
}

//! 次に作るまとまりの名前。同じ名前が並ばないように番号を送る。
std::string V2MainWindow::NextGroupName() const
{
    const auto& snapshot = session_->GetDocument().Snapshot();
    for (int number = 1; number < 1000; ++number) {
        const std::string candidate = "まとまり" + std::to_string(number);
        bool taken = false;
        for (const auto& group : snapshot.groups) {
            if (group.displayName == candidate) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            return candidate;
        }
    }
    return "まとまり";
}

//! 木の行が書き換わった。まとまりの行なら名前か出し隠し。まとまりでなければ偽。
bool V2MainWindow::RenameOrToggleGroupFromItem(QTreeWidgetItem* item)
{
    const auto group = GroupOfItem(groupItems_, item);
    if (!group.has_value()) {
        return false;
    }
    const auto& snapshot = session_->GetDocument().Snapshot();
    const kachakacha::v2::document::Group* current = nullptr;
    for (const auto& candidate : snapshot.groups) {
        if (candidate.id == *group) {
            current = &candidate;
            break;
        }
    }
    if (current == nullptr) {
        return true;
    }
    const bool wantVisible = item->checkState(0) == Qt::Checked;
    if (wantVisible != current->visible) {
        const auto done = session_->GetDocument().Run(
            kachakacha::v2::document::SetGroupVisibilityCommand(*group, wantVisible));
        if (!done.committed) {
            ReportDiagnostics(done.diagnostics);
        }
        AdoptCurrentDocument();
        SetStatus(wantVisible ? QStringLiteral("まとまりを出しました。")
                              : QStringLiteral("まとまりを隠しました。"
                                               "中の1つずつの出し隠しは変えていません。"));
        return true;
    }
    // 「←作業中」の印は名前ではない。外してから比べる。
    std::string typed = item->text(0).toStdString();
    const std::string marker = " ←作業中";
    if (typed.size() >= marker.size()
        && typed.compare(typed.size() - marker.size(), marker.size(), marker) == 0) {
        typed.erase(typed.size() - marker.size());
    }
    if (typed == current->displayName) {
        return true;   // 何も変わっていない。
    }
    const auto done = session_->GetDocument().Run(
        kachakacha::v2::document::RenameGroupCommand(*group, typed));
    if (!done.committed) {
        ReportDiagnostics(done.diagnostics);
    }
    AdoptCurrentDocument();
    return true;
}

//! 引きずって落とした。落ちた先のまとまりへ入れる。最上位へ落としたら外へ出す。
void V2MainWindow::DropTreeItemsOnto(const std::vector<QTreeWidgetItem*>& moved,
    QTreeWidgetItem* onto)
{
    using kachakacha::v2::document::MoveEntitiesToGroupCommand;
    using kachakacha::v2::document::SetGroupParentCommand;

    // 落ちた先。まとまりの行でなければ、その行が入っているまとまりへ入れる。
    std::optional<GroupId> destination = GroupOfItem(groupItems_, onto);
    if (!destination.has_value() && onto != nullptr) {
        for (const auto& entry : entityItems_) {
            if (entry.first != onto) {
                continue;
            }
            const auto* entity = session_->GetDocument().FindEntity(entry.second);
            if (entity != nullptr) {
                destination = entity->groupId;
            }
            break;
        }
    }
    std::vector<kachakacha::v2::base::EntityId> entities;
    std::vector<GroupId> groups;
    for (QTreeWidgetItem* item : moved) {
        if (const auto group = GroupOfItem(groupItems_, item); group.has_value()) {
            groups.push_back(*group);
            continue;
        }
        for (const auto& entry : entityItems_) {
            if (entry.first == item) {
                entities.push_back(entry.second);
                break;
            }
        }
    }
    if (entities.empty() && groups.empty()) {
        return;
    }
    // 一度に引きずった分は、人から見れば1つの操作である。
    // 1つでも断られたら全部やめる。半分だけ移った状態を作らない。
    // 輪になる移動が1つ混じっていたときに、ほかだけ移ってしまうと、
    // どこまで移ったのかが利用者に分からなくなる。
    bool ok = true;
    {
        kachakacha::v2::document::Document::Transaction transaction(
            session_->GetDocument(), "まとまりへ移す");
        for (const GroupId& group : groups) {
            const auto done = session_->GetDocument().Run(
                SetGroupParentCommand(group, destination));
            if (!done.committed) {
                ReportDiagnostics(done.diagnostics);
                ok = false;
                break;
            }
        }
        if (ok && !entities.empty()) {
            const auto done = session_->GetDocument().Run(
                MoveEntitiesToGroupCommand(entities, destination));
            if (!done.committed) {
                ReportDiagnostics(done.diagnostics);
                ok = false;
            }
        }
        if (ok) {
            ok = transaction.Commit();
        }
        // ここを抜けるときに、Commit していなければ始める前へ戻る。
    }
    AdoptCurrentDocument();
    if (!ok) {
        return;
    }
    SetStatus(destination.has_value()
            ? QStringLiteral("まとまりへ移しました。中身と参照は変えていません。")
            : QStringLiteral("まとまりの外へ出しました。中身と参照は変えていません。"));
}

QTreeWidgetItem* V2MainWindow::ItemOfEntity(const kachakacha::v2::base::EntityId& id) const
{
    for (const auto& entry : entityItems_) {
        if (entry.second == id) {
            return entry.first;
        }
    }
    return nullptr;
}
