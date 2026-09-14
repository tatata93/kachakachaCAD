//! 左の一覧(V1 のモデルツリー)を作り直すところ。
//!
//! 一覧は「いま文書に何があるか」を読むための窓であり、選ぶための入口でもある。
//! V2MainWindow.cpp が 1500 行の上限に届いたので、一覧まわりだけをここへ移した。
//! 動きは変えていない。

#include "V2MainWindow.h"

#include "V2EntityTree.h"

#include "kachakacha/app/EntityNaming.h"
#include "kachakacha/app/NameFilter.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/domain/Entity.h"

#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

void V2MainWindow::RefreshEntityList()
{
    if (!entityTree_) {
        return;
    }
    // 書き換えの便りを止めてから作り直す。止めないと、作り直しの途中で
    // 「名前が変わった」と誤って伝わり、名前が入れ替わる。
    const bool blocked = entityTree_->blockSignals(true);
    entityTree_->clear();
    entityItems_.clear();
    const auto& snapshot = session_->GetDocument().Snapshot();
    // まとまりを **入れ子のまま** 出す。名前で束ねると、
    // 同じ名前の別のまとまりが1つに見えてしまう(オーナー指示 §9・§10)。
    std::map<std::string, QTreeWidgetItem*> byGroupId;
    BuildGroupItems(byGroupId);
    QTreeWidgetItem* looseItem = nullptr;
    const auto groupItem = [&](const std::optional<kachakacha::v2::base::GroupId>& id)
        -> QTreeWidgetItem* {
        if (id.has_value()) {
            const auto found = byGroupId.find(id->ToString());
            if (found != byGroupId.end() && found->second != nullptr) {
                return found->second;
            }
        }
        if (looseItem == nullptr) {
            looseItem = new QTreeWidgetItem(entityTree_);
            looseItem->setText(0, QStringLiteral("(まとまりなし)"));
            looseItem->setText(1, QStringLiteral("まとまり"));
            looseItem->setFlags((looseItem->flags() | Qt::ItemIsDropEnabled)
                & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
        }
        return looseItem;
    };
    entityItems_.clear();
    // 原点の基準平面と3軸は、最上部の「原点」に固定して出す(V1 と同じ)。
    // 消せず、名前も変えられず、まとまりへも入らない。
    axisItems_.fill(nullptr);
    auto* originRoot = new QTreeWidgetItem(entityTree_);
    originRoot->setText(0, QStringLiteral("原点"));
    originRoot->setText(1, QStringLiteral("原点"));
    originRoot->setToolTip(0, QStringLiteral(
        "初期の基準平面(top_XY / front_XZ / side_YZ)と軸。削除やまとまりへの移動はできません"));
    for (const auto& entity : snapshot.entities) {
        if (!kachakacha::v2::app::IsOriginPlane(snapshot, entity.id)) {
            continue;
        }
        auto* item = new QTreeWidgetItem(originRoot);
        item->setText(0, QString::fromUtf8(entity.displayName.c_str()));
        item->setText(1, QStringLiteral("作業平面"));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
        entityItems_.emplace_back(item, entity.id);
    }
    const char* axisNames[3] = {"X軸", "Y軸", "Z軸"};
    for (int axis = 0; axis < 3; ++axis) {
        auto* item = new QTreeWidgetItem(originRoot);
        item->setText(0, QString::fromUtf8(axisNames[axis]));
        item->setText(1, QStringLiteral("軸"));
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable
            & ~Qt::ItemIsDragEnabled);
        item->setCheckState(0, viewport_ != nullptr && viewport_->AxisVisible(axis)
                ? Qt::Checked
                : Qt::Unchecked);
        axisItems_[static_cast<std::size_t>(axis)] = item;
    }
    for (const auto& entity : snapshot.entities) {
        if (kachakacha::v2::app::IsOriginPlane(snapshot, entity.id)) {
            continue;
        }
        auto* item = new QTreeWidgetItem(groupItem(entity.groupId));
        const QString name = entity.displayName.empty()
            ? QStringLiteral("(名前なし)")
            : QString::fromUtf8(entity.displayName.c_str());
        item->setText(0, name);
        // F2 で名前を書き換えられるようにする。まとまりの行は変えられない。
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        entityItems_.emplace_back(item, entity.id);
        item->setText(1, QString::fromUtf8(
            std::string(kachakacha::v2::domain::EntityKindNameJa(entity.kind)).c_str()));
    }
    entityTree_->expandAll();
    for (int column = 0; column < entityTree_->columnCount(); ++column) {
        entityTree_->resizeColumnToContents(column);
    }
    if (groupLabel_ != nullptr) {
        groupLabel_->setText(ActiveGroupText());
    }
    RefreshActiveGroupCombo();
    entityTree_->blockSignals(blocked);
    RefreshWorkPlaneViews();
    // 作り直したら絞り込みをかけ直す。かけ直さないと、線を1本引いただけで
    // 絞り込みが外れたように見える。
    ApplyEntityTreeFilter();
}

//! 文書の作図面を、画面に出す形へ写す。
//! V1 と同じく **すべての作図面** を出す。作業中の1枚だけだと、
//! 「どこに描けるのか」が分からず、平面を選ぶ手がかりが画面に無くなる。
void V2MainWindow::RefreshWorkPlaneViews()
{
    if (viewport_ == nullptr) {
        return;
    }
    std::vector<V2Viewport::WorkPlaneView> planes;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind != kachakacha::v2::domain::EntityKind::WorkPlane
            || entity.visibility != kachakacha::v2::domain::Visibility::Visible) {
            continue;
        }
        const auto frame = WorkPlaneFrameOf(entity.id);
        if (!frame.has_value()) {
            continue;
        }
        V2Viewport::WorkPlaneView view;
        view.entityId = entity.id;
        view.frame = *frame;
        view.label = QString::fromUtf8(entity.displayName.c_str());
        view.active = entity.id == activeWorkPlaneId_;
        planes.push_back(std::move(view));
    }
    viewport_->SetWorkPlaneViews(std::move(planes));
}


namespace {

//! その行と、その下のものを絞り込む(V1 の ApplyModelTreeFilter と同じ木のたどり方)。
//!
//! 自分か祖先が当たれば、下のものごと残す。下のどれかが当たれば、祖先も残す。
//! こうしないと、まとまりの中の1本を探したときに、まとまりごと消えてしまう。
//! 残すかどうかの判断は core(app/NameFilter)にある。
bool ApplyFilterToItem(QTreeWidgetItem* item, const std::string& term, bool ancestorMatches)
{
    const bool selfMatches = kachakacha::v2::app::NameMatchesFilter(
        item->text(0).toStdString(), item->text(1).toStdString(), term);
    bool descendantMatches = false;
    for (int index = 0; index < item->childCount(); ++index) {
        if (ApplyFilterToItem(item->child(index), term, ancestorMatches || selfMatches)) {
            descendantMatches = true;
        }
    }
    const bool empty = kachakacha::v2::app::TrimmedFilterTerm(term).empty();
    const bool visible = empty || ancestorMatches || selfMatches || descendantMatches;
    item->setHidden(!visible);
    if (!empty && visible && item->childCount() > 0) {
        item->setExpanded(true);
    }
    return selfMatches || descendantMatches;
}

//! 見えている行を数える(絞り込みで隠れたものと、まとまりの見出しを除く)。
int CountVisibleLeaves(const QTreeWidgetItem* item)
{
    if (item->isHidden()) {
        return 0;
    }
    if (item->childCount() == 0) {
        return 1;
    }
    int count = 0;
    for (int index = 0; index < item->childCount(); ++index) {
        count += CountVisibleLeaves(item->child(index));
    }
    return count;
}

} // namespace

void V2MainWindow::ApplyEntityTreeFilter()
{
    if (entityTree_ == nullptr || entityFilter_ == nullptr) {
        return;
    }
    const std::string term = entityFilter_->text().toStdString();
    for (int index = 0; index < entityTree_->topLevelItemCount(); ++index) {
        (void)ApplyFilterToItem(entityTree_->topLevelItem(index), term, false);
    }
}

void V2MainWindow::SetEntityFilterText(const QString& text)
{
    if (entityFilter_ == nullptr) {
        return;
    }
    entityFilter_->setText(text);
    // 欄の便りが届かない場でも同じ結果になるようにする(試験のため)。
    ApplyEntityTreeFilter();
}

int V2MainWindow::VisibleEntityRowCount() const
{
    if (entityTree_ == nullptr) {
        return 0;
    }
    int count = 0;
    for (int index = 0; index < entityTree_->topLevelItemCount(); ++index) {
        count += CountVisibleLeaves(entityTree_->topLevelItem(index));
    }
    return count;
}

//! まとまりの行を、入れ子のまま作る。
//!
//! 親から順に作る。親がまだ無ければその場で親を作りにいく。
//! 先に場所を取ってから親を作るのは、壊れた文書に輪があっても止まるためである。
void V2MainWindow::BuildGroupItems(std::map<std::string, QTreeWidgetItem*>& byGroupId)
{
    const auto& snapshot = session_->GetDocument().Snapshot();
    groupItems_.clear();
    std::function<QTreeWidgetItem*(const kachakacha::v2::base::GroupId&)> make;
    make = [&](const kachakacha::v2::base::GroupId& id) -> QTreeWidgetItem* {
        const std::string key = id.ToString();
        if (const auto found = byGroupId.find(key); found != byGroupId.end()) {
            return found->second;
        }
        const kachakacha::v2::document::Group* group = nullptr;
        for (const auto& candidate : snapshot.groups) {
            if (candidate.id == id) {
                group = &candidate;
                break;
            }
        }
        if (group == nullptr) {
            return nullptr;
        }
        byGroupId.emplace(key, nullptr);
        QTreeWidgetItem* parent = group->parentId.has_value() ? make(*group->parentId)
                                                              : nullptr;
        auto* made = parent != nullptr ? new QTreeWidgetItem(parent)
                                       : new QTreeWidgetItem(entityTree_);
        const bool active = snapshot.settings.activeGroupId.has_value()
            && *snapshot.settings.activeGroupId == id;
        made->setText(0, QString::fromStdString(
            group->displayName + (active ? " ←作業中" : "")));
        made->setText(1, QStringLiteral("まとまり"));
        // まとまりは名前を変えられる。引きずって別のまとまりへ移せる。
        made->setFlags(made->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled | Qt::ItemIsUserCheckable);
        made->setCheckState(0, group->visible ? Qt::Checked : Qt::Unchecked);
        byGroupId[key] = made;
        groupItems_.emplace_back(made, id);
        return made;
    };
    // まとまりが空でも木に出す。出さないと、作った直後に何も見えない。
    for (const auto& group : snapshot.groups) {
        (void)make(group.id);
    }
}
