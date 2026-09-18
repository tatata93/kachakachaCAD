//! 左の一覧(V1 のモデルツリー)を作り直すところ。
//!
//! 一覧は「いま文書に何があるか」を読むための窓であり、選ぶための入口でもある。
//! V2MainWindow.cpp が 1500 行の上限に届いたので、一覧まわりだけをここへ移した。
//! 動きは変えていない。

#include "V2MainWindow.h"

#include "V2EntityTree.h"
#include "V2TreeIcons.h"

#include "kachakacha/app/EntityNaming.h"
#include "kachakacha/app/NameFilter.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/domain/Entity.h"

#include <QLabel>
#include <QLineEdit>
#include <QFont>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

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
        // 節(原点・作業面・…)の見出しは、空でも「もの」ではない。
        return item->text(1) == QStringLiteral("節") || item->text(1) == QStringLiteral("文書")
            ? 0
            : 1;
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
