#include "V2MainWindow.h"

#include <QAction>
#include <QComboBox>
#include <QToolBar>

#include "kachakacha/app/UiMode.h"

#include <array>

namespace {

using kachakacha::v2::app::AllUiModes;
using kachakacha::v2::app::CommandIdsForMode;

} // namespace

void V2MainWindow::BuildModeToolActions()
{
    // 並べる命令はモードの台帳(app/UiMode)から取る。ここに別の一覧を持つと、
    // 台帳へ足した命令が2段目に出ないまま残る。作図の道具(kToolBindings)は
    // 道具として既に並んでいるので除く。
    toolPalette_->addSeparator();
    for (const kachakacha::v2::app::UiMode mode : AllUiModes()) {
        for (const std::string_view id : CommandIdsForMode(mode)) {
            if (IsToolBoundCommand(id)) {
                continue;
            }
            QAction* action = ActionFor(id);
            if (action == nullptr) {
                continue;
            }
            toolPalette_->addAction(action);
            modeToolActions_.emplace_back(id, action);
        }
    }
}

void V2MainWindow::RefreshActiveGroupCombo()
{
    if (groupCombo_ == nullptr) {
        return;
    }
    const auto& snapshot = session_->GetDocument().Snapshot();
    refreshingGroupCombo_ = true;
    groupCombo_->clear();
    groupComboIds_.clear();
    groupCombo_->addItem(QStringLiteral("(まとまりなし)"));
    groupComboIds_.push_back(std::nullopt);
    int current = snapshot.settings.activeGroupId.has_value() ? -1 : 0;
    for (const auto& group : snapshot.groups) {
        if (snapshot.settings.activeGroupId == group.id) {
            current = static_cast<int>(groupComboIds_.size());
        }
        groupCombo_->addItem(QString::fromStdString(group.displayName));
        groupComboIds_.push_back(group.id);
    }
    groupCombo_->setCurrentIndex(current);
    refreshingGroupCombo_ = false;
}

void V2MainWindow::ActivateGroupByComboIndex(int index)
{
    if (refreshingGroupCombo_ || index < 0
        || index >= static_cast<int>(groupComboIds_.size())) {
        return;
    }
    if (SetActiveGroup(groupComboIds_[static_cast<std::size_t>(index)])) {
        SetStatus(index == 0 ? QStringLiteral("作業中のまとまりを外しました。")
                             : QStringLiteral("作業中のまとまりを %1 にしました。")
                                   .arg(groupCombo_->itemText(index)));
    }
}

int V2MainWindow::GroupComboCount() const
{
    return groupCombo_ == nullptr ? 0 : groupCombo_->count();
}

QString V2MainWindow::GroupComboText(int index) const
{
    if (groupCombo_ == nullptr || index < 0 || index >= groupCombo_->count()) {
        return QString();
    }
    return groupCombo_->itemText(index);
}

int V2MainWindow::GroupComboCurrent() const
{
    return groupCombo_ == nullptr ? -1 : groupCombo_->currentIndex();
}

void V2MainWindow::SelectGroupCombo(int index)
{
    if (groupCombo_ != nullptr) {
        groupCombo_->setCurrentIndex(index);
    }
}

bool V2MainWindow::ModeToolVisible(std::string_view id) const
{
    for (const auto& entry : modeToolActions_) {
        if (entry.first == id) {
            return entry.second->isVisible();
        }
    }
    return false;
}
