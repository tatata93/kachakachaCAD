#include "V2MainWindow.h"

#include <QAction>
#include <QComboBox>
#include <QToolBar>

#include <array>

namespace {

constexpr std::array<std::string_view, 24> kModeToolIds{
    "workplane.create", "grid.edit",
    "guide.create", "guide.build", "part.extrude", "part.thicken",
    "part.thicken_to_plane", "part.from_wire_cage", "part.boolean_add",
    "part.boolean_cut", "derived.freeze",
    "fabrication.create", "fabrication.assign_role", "fabrication.preview_update",
    "fabrication.create_pattern", "fabrication.set_assembly", "fabrication.set_method",
    "fabrication.freeze_state", "fabrication.set_connection_scope",
    "export.validate", "export.stl", "export.step", "export.svg", "export.dxf",
};

} // namespace

void V2MainWindow::BuildModeToolActions()
{
    for (const std::string_view id : kModeToolIds) {
        QAction* action = ActionFor(id);
        if (action == nullptr) {
            continue;
        }
        toolPalette_->addAction(action);
        modeToolActions_.emplace_back(id, action);
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
