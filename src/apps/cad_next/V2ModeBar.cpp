#include "V2MainWindow.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QObject>
#include <QSizePolicy>
#include <QString>
#include <QToolBar>

#include "kachakacha/app/UiMode.h"

#include <array>

namespace {

using kachakacha::v2::app::AllUiModes;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::app::UiModeNameJa;

} // namespace

void V2MainWindow::BuildModeBar()
{
    modeBar_ = addToolBar(QStringLiteral("モード"));
    modeBar_->setObjectName(QStringLiteral("modeBar"));
    modeBar_->setMovable(false);
    modeBar_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const UiMode mode : AllUiModes()) {
        QAction* action = modeBar_->addAction(
            QString::fromUtf8(std::string(UiModeNameJa(mode)).c_str()));
        action->setCheckable(true);
        action->setChecked(mode == mode_);
        modeActions_.emplace_back(mode, action);
        QObject::connect(action, &QAction::triggered, this,
            [this, mode] { SetMode(mode); });
    }
    // モード → 正対 → 選択 → 測定 → 作図面 → まとまり → 吸着。
    modeBar_->addSeparator();
    for (const std::string_view id : {"view.align_workplane", "selection.activate",
             "measure.open"}) {
        if (QAction* action = ActionFor(id); action != nullptr) {
            modeBar_->addAction(action);
        }
    }
    modeBar_->addSeparator();
    modeBar_->addWidget(new QLabel(QStringLiteral(" 作図面 "), modeBar_));
    planeCombo_ = new QComboBox(modeBar_);
    planeCombo_->setToolTip(QStringLiteral("作業中の作図面。選ぶと切り替わります。"));
    planeCombo_->setMinimumContentsLength(10);
    planeCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    planeCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    planeCombo_->setMaximumWidth(170);
    modeBar_->addWidget(planeCombo_);
    QObject::connect(planeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (refreshingPlaneCombo_ || index < 0
            || index >= static_cast<int>(planeComboIds_.size())) {
            return;
        }
        ActivateWorkPlaneById(planeComboIds_[static_cast<std::size_t>(index)]);
    });
    modeBar_->addWidget(new QLabel(QStringLiteral(" グループ "), modeBar_));
    groupCombo_ = new QComboBox(modeBar_);
    groupCombo_->setToolTip(QStringLiteral("これから作るものを入れる作業中のグループ。"));
    groupCombo_->setMinimumContentsLength(10);
    groupCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    groupCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    groupCombo_->setMaximumWidth(170);
    modeBar_->addWidget(groupCombo_);
    QObject::connect(groupCombo_, &QComboBox::currentIndexChanged, this,
        [this](int index) { ActivateGroupByComboIndex(index); });
    if (QAction* snap = ActionFor("snap.toggle"); snap != nullptr) {
        snap->setCheckable(true);
        snap->setChecked(snapEnabled_);
        modeBar_->addAction(snap);
    }
    addToolBarBreak();
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
    groupCombo_->addItem(QStringLiteral("(グループなし)"));
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
        SetStatus(index == 0 ? QStringLiteral("作業中のグループを外しました。")
                             : QStringLiteral("作業中のグループを %1 にしました。")
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
