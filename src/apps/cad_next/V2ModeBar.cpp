#include "V2MainWindow.h"
#include "V2QuickDisplayControls.h"
#include "V2ToolbarRow.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QObject>
#include <QSizePolicy>
#include "V2Viewport.h"
#include <QString>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "kachakacha/app/UiMode.h"

#include <array>

namespace {

using kachakacha::v2::app::AllUiModes;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::app::UiModeNameJa;

void AddModeButton(V2ToolbarRow& row, QAction* action)
{
    auto* button = new QToolButton(&row);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setMinimumHeight(28);
    button->setFocusPolicy(Qt::StrongFocus);
    row.Add(button, action);
    button->show();
}

void AddModeChoice(V2ToolbarRow& row, QComboBox* combo, const QString& name)
{
    auto* field = new QWidget(&row);
    auto* layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* label = new QLabel(name, field);
    label->setBuddy(combo);
    combo->setAccessibleName(name);
    layout->addWidget(label);
    layout->addWidget(combo);
    auto* menu = new QMenu(name, field);
    menu->setObjectName(QStringLiteral("toolbarOverflowMenu"));
    menu->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, combo] {
        menu->clear();
        for (int i = 0; i < combo->count(); ++i) {
            auto* item = menu->addAction(combo->itemText(i));
            item->setCheckable(true);
            item->setChecked(i == combo->currentIndex());
            QObject::connect(item, &QAction::triggered, combo, [combo, i] { combo->setCurrentIndex(i); });
        }
    });
    row.Add(field, menu->menuAction());
    field->show();
}

} // namespace

void V2MainWindow::BuildModeBar()
{
    modeBar_ = addToolBar(QStringLiteral("モード"));
    modeBar_->setObjectName(QStringLiteral("modeBar"));
    modeBar_->setMovable(false);
    auto* row = new V2ToolbarRow(QStringLiteral("切替"), modeBar_);
    row->setObjectName(QStringLiteral("modeBarRow"));
    for (const UiMode mode : AllUiModes()) {
        auto* action = new QAction(QString::fromUtf8(std::string(UiModeNameJa(mode)).c_str()), modeBar_);
        action->setCheckable(true);
        action->setChecked(mode == mode_);
        modeActions_.emplace_back(mode, action);
        QObject::connect(action, &QAction::triggered, this,
            [this, mode] { SetMode(mode); });
        AddModeButton(*row, action);
    }
    // モード → 正対 → 選択 → 測定 → 作図面 → まとまり → 吸着。
    for (const std::string_view id : {"view.align_workplane", "selection.activate",
             "measure.open"}) {
        if (QAction* action = ActionFor(id); action != nullptr) {
            AddModeButton(*row, action);
        }
    }
    planeCombo_ = new QComboBox(row);
    planeCombo_->setToolTip(QStringLiteral("作業中の作図面。選ぶと切り替わります。"));
    planeCombo_->setMinimumContentsLength(10);
    planeCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    planeCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    planeCombo_->setMaximumWidth(170);
    AddModeChoice(*row, planeCombo_, QStringLiteral("作図面"));
    BuildDrawingScopeSwitch();
    AddModeChoice(*row, modeBar_->findChild<QComboBox*>(QStringLiteral("drawingScope")),
        QStringLiteral("作図範囲"));
    QObject::connect(planeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (refreshingPlaneCombo_ || index < 0
            || index >= static_cast<int>(planeComboIds_.size())) {
            return;
        }
        ActivateWorkPlaneById(planeComboIds_[static_cast<std::size_t>(index)]);
    });
    groupCombo_ = new QComboBox(row);
    groupCombo_->setToolTip(QStringLiteral("これから作るものを入れる作業中のグループ。"));
    groupCombo_->setMinimumContentsLength(10);
    groupCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    groupCombo_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    groupCombo_->setMaximumWidth(170);
    AddModeChoice(*row, groupCombo_, QStringLiteral("グループ"));
    QObject::connect(groupCombo_, &QComboBox::currentIndexChanged, this,
        [this](int index) { ActivateGroupByComboIndex(index); });
    modeBar_->addWidget(row);
    // Display toggles are used repeatedly while drawing.
    addToolBarBreak();
    auto* displayBar=addToolBar(QStringLiteral("表示切替"));
    displayBar->setObjectName(QStringLiteral("quickDisplayBar"));
    displayBar->setMovable(false);
    if (QAction* snap = ActionFor("snap.toggle"); snap != nullptr) {
        snap->setCheckable(true);
        snap->setChecked(snapEnabled_);
        displayBar->addAction(snap);
    }
    InstallQuickDisplayControls(*viewport_,*displayBar,*session_);
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

void V2MainWindow::BuildDrawingScopeSwitch()
{
    auto* scope = new QComboBox(modeBar_);
    scope->setObjectName(QStringLiteral("drawingScope"));
    scope->addItems({QStringLiteral("平面内"), QStringLiteral("3D作図")});
    scope->setToolTip(QStringLiteral("平面内: 作図面上だけに作図。3D作図: 奥行きのある点にも吸着。"
                                    "切替時は入力途中の点を取り消します。"));
    session_->SetDrawingPlaneOnly(true);
    QObject::connect(scope, &QComboBox::currentIndexChanged, this, [this](int index) {
        const bool hadInput = session_->HasPlacedPoints();
        session_->SetDrawingPlaneOnly(index == 0);
        viewport_->OnToolChanged();
        RefreshGuide();
        SetStatus((index == 0 ? QStringLiteral("平面内: 選択中の作図面上に描きます。")
                             : QStringLiteral("3D作図: 奥行きのある点にも描けます。"))
            + (hadInput ? QStringLiteral("入力途中の点を取り消しました。") : QString()));
    });
}
