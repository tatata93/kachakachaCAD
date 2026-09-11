//! 左の一覧(V1 のモデルツリー)を作り直すところ。
//!
//! 一覧は「いま文書に何があるか」を読むための窓であり、選ぶための入口でもある。
//! V2MainWindow.cpp が 1500 行の上限に届いたので、一覧まわりだけをここへ移した。
//! 動きは変えていない。

#include "V2MainWindow.h"

#include "kachakacha/app/EntityNaming.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/domain/Entity.h"

#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>

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
    // まとまりごとに束ねる。いま作業中のまとまりは名前の後ろに印を付ける。
    std::map<std::string, QTreeWidgetItem*> byGroup;
    const auto groupItem = [&](const std::optional<kachakacha::v2::base::GroupId>& id)
        -> QTreeWidgetItem* {
        std::string name = "(まとまりなし)";
        bool active = false;
        if (id.has_value()) {
            for (const auto& group : snapshot.groups) {
                if (group.id == *id) {
                    name = group.displayName;
                }
            }
            active = snapshot.settings.activeGroupId.has_value()
                && *snapshot.settings.activeGroupId == *id;
        }
        const std::string key = name + (active ? " ←作業中" : "");
        const auto found = byGroup.find(key);
        if (found != byGroup.end()) {
            return found->second;
        }
        auto* made = new QTreeWidgetItem(entityTree_);
        made->setText(0, QString::fromStdString(key));
        made->setText(1, QStringLiteral("まとまり"));
        byGroup.emplace(key, made);
        return made;
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

