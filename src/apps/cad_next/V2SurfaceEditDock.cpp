#include "V2SurfaceEditDock.h"

#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <string>

namespace {

using kachakacha::v2::app::MirrorPlaneChoice;
using kachakacha::v2::app::SurfaceEditOperation;
using kachakacha::v2::modeling::SurfaceContinuity;

[[nodiscard]] QString Text(std::string_view text)
{
    return QString::fromUtf8(std::string(text).c_str());
}

[[nodiscard]] bool ClickIfVisible(QPushButton* button)
{
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;   // 見えていない・押せないものは押せない。
    }
    button->click();
    return true;
}

void FillContinuity(QComboBox* combo)
{
    combo->addItem(QStringLiteral("G0(位置だけ)"));
    combo->addItem(QStringLiteral("G1(接して滑らか)"));
    combo->addItem(QStringLiteral("G2(曲がり方までそろう)"));
}

} // namespace

V2SurfaceEditDock::V2SurfaceEditDock(QWidget* parent)
    : QDockWidget(QStringLiteral("面の編集"), parent)
{
    setObjectName(QStringLiteral("surfaceEditDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    BuildOperationCards(layout);

    // 2. 入力。欄の名前と入れたものを 1 行ずつ。3D で押すと入り、押し直すと外れる。
    layout->addWidget(new QLabel(QStringLiteral("2. 入力(3D で押すと入り、押し直すと外れます)"), body));
    entries_ = new QTreeWidget(body);
    entries_->setColumnCount(2);
    entries_->setHeaderLabels(QStringList{QStringLiteral("欄"), QStringLiteral("入れたもの")});
    entries_->setMaximumHeight(150);
    layout->addWidget(entries_);
    auto* listActions = new QHBoxLayout();
    remove_ = new QPushButton(QStringLiteral("選んだ行を外す"), body);
    clear_ = new QPushButton(QStringLiteral("すべて解除"), body);
    listActions->addWidget(remove_);
    listActions->addWidget(clear_);
    listActions->addStretch(1);
    layout->addLayout(listActions);
    QObject::connect(remove_, &QPushButton::clicked, this, [this] {
        const int row = entries_->indexOfTopLevelItem(entries_->currentItem());
        if (loading_ || !removeHandler_ || row < 0
            || row >= static_cast<int>(shownEntries_.size())) {
            return;
        }
        removeHandler_(shownEntries_[static_cast<std::size_t>(row)].id);
    });
    QObject::connect(clear_, &QPushButton::clicked, this, [this] {
        if (!loading_ && clearHandler_) {
            clearHandler_();
        }
    });

    BuildOptions(layout);

    // 4. 状態。
    layout->addWidget(new QLabel(QStringLiteral("4. 状態"), body));
    status_ = new QLabel(body);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch(1);

    auto* actions = new QHBoxLayout();
    cancel_ = new QPushButton(QStringLiteral("キャンセル Esc"), body);
    confirm_ = new QPushButton(QStringLiteral("確定 Enter"), body);
    QObject::connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelHandler_) {
            cancelHandler_();
        }
    });
    QObject::connect(confirm_, &QPushButton::clicked, this, [this] {
        if (confirmHandler_) {
            confirmHandler_();
        }
    });
    actions->addWidget(cancel_);
    actions->addStretch(1);
    actions->addWidget(confirm_);
    layout->addLayout(actions);
}

void V2SurfaceEditDock::BuildOperationCards(QVBoxLayout* layout)
{
    // 1. 作り方。押された形がいまの作り方。
    layout->addWidget(new QLabel(QStringLiteral("1. 作り方"), widget()));
    const auto& operations = kachakacha::v2::app::SurfaceEditOperations();
    for (std::size_t index = 0; index < operations.size() && index < cards_.size(); ++index) {
        const SurfaceEditOperation operation = operations[index];
        auto* card = new QPushButton(Text(kachakacha::v2::app::SurfaceEditLabelJa(operation))
                + QStringLiteral("  ─  ") + Text(kachakacha::v2::app::SurfaceEditHintJa(operation)),
            widget());
        card->setCheckable(true);
        QObject::connect(card, &QPushButton::clicked, this, [this, operation] {
            if (!loading_ && operationHandler_) {
                operationHandler_(operation);
            }
        });
        cards_[index] = card;
        layout->addWidget(card);
    }
}

void V2SurfaceEditDock::BuildOptions(QVBoxLayout* layout)
{
    // 3. 設定。作り方で使うものだけ出す。
    layout->addWidget(new QLabel(QStringLiteral("3. 設定"), widget()));
    auto* form = new QFormLayout();
    continuityALabel_ = new QLabel(QStringLiteral("滑らかさ"), widget());
    continuityA_ = new QComboBox(widget());
    FillContinuity(continuityA_);
    form->addRow(continuityALabel_, continuityA_);
    continuityBLabel_ = new QLabel(QStringLiteral("縁 B の滑らかさ"), widget());
    continuityB_ = new QComboBox(widget());
    FillContinuity(continuityB_);
    form->addRow(continuityBLabel_, continuityB_);
    toleranceLabel_ = new QLabel(QStringLiteral("許容"), widget());
    tolerance_ = new QDoubleSpinBox(widget());
    tolerance_->setRange(0.0001, 10.0);
    tolerance_->setDecimals(4);
    tolerance_->setSingleStep(0.005);
    tolerance_->setSuffix(QStringLiteral(" mm"));
    form->addRow(toleranceLabel_, tolerance_);
    tensionLabel_ = new QLabel(QStringLiteral("張りの強さ"), widget());
    tension_ = new QDoubleSpinBox(widget());
    tension_->setRange(0.1, 5.0);
    tension_->setDecimals(2);
    tension_->setSingleStep(0.1);
    form->addRow(tensionLabel_, tension_);
    isoLabel_ = new QLabel(QStringLiteral("向きと本数"), widget());
    auto* isoRow = new QHBoxLayout();
    isoDirection_ = new QComboBox(widget());
    isoDirection_->addItem(QStringLiteral("U 方向の線"));
    isoDirection_->addItem(QStringLiteral("V 方向の線"));
    isoDirection_->addItem(QStringLiteral("両方"));
    isoCount_ = new QSpinBox(widget());
    isoCount_->setRange(1, 50);
    isoCount_->setSuffix(QStringLiteral(" 本"));
    isoRow->addWidget(isoDirection_);
    isoRow->addWidget(isoCount_);
    form->addRow(isoLabel_, isoRow);
    mirrorLabel_ = new QLabel(QStringLiteral("対称面"), widget());
    mirrorPlane_ = new QComboBox(widget());
    for (const MirrorPlaneChoice choice : {MirrorPlaneChoice::CenterXZ, MirrorPlaneChoice::CenterYZ,
             MirrorPlaneChoice::CenterXY, MirrorPlaneChoice::WorkPlane}) {
        mirrorPlane_->addItem(Text(kachakacha::v2::app::MirrorPlaneLabelJa(choice)));
    }
    form->addRow(mirrorLabel_, mirrorPlane_);
    layout->addLayout(form);
    for (QComboBox* combo : {continuityA_, continuityB_, isoDirection_, mirrorPlane_}) {
        QObject::connect(combo, &QComboBox::currentIndexChanged, this, [this](int) { EmitOptions(); });
    }
    for (QDoubleSpinBox* spin : {tolerance_, tension_}) {
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { EmitOptions(); });
    }
    QObject::connect(isoCount_, &QSpinBox::valueChanged, this, [this](int) { EmitOptions(); });
}

void V2SurfaceEditDock::EmitOptions()
{
    if (loading_ || !optionsHandler_) {
        return;
    }
    auto next = shown_;
    next.continuityA = static_cast<SurfaceContinuity>(std::max(0, continuityA_->currentIndex()));
    next.continuityB = static_cast<SurfaceContinuity>(std::max(0, continuityB_->currentIndex()));
    next.toleranceMm = tolerance_->value();
    next.tension = tension_->value();
    next.isoDirection = std::max(0, isoDirection_->currentIndex());
    next.isoCount = isoCount_->value();
    next.mirrorPlane = static_cast<MirrorPlaneChoice>(std::max(0, mirrorPlane_->currentIndex()));
    optionsHandler_(next);
}

void V2SurfaceEditDock::ShowInput(const kachakacha::v2::app::SurfaceEditInputState& state,
    const std::vector<Entry>& entries, const std::vector<QString>& statusLinesJa, bool canConfirm)
{
    loading_ = true;
    shown_ = state;
    shownEntries_ = entries;
    const auto& operations = kachakacha::v2::app::SurfaceEditOperations();
    for (std::size_t index = 0; index < operations.size() && index < cards_.size(); ++index) {
        cards_[index]->setChecked(operations[index] == state.operation);
    }
    entries_->clear();
    for (const Entry& entry : entries) {
        auto* item = new QTreeWidgetItem(entries_);
        item->setText(0, entry.slotJa);
        item->setText(1, entry.nameJa);
    }
    remove_->setEnabled(!entries.empty());
    clear_->setEnabled(!entries.empty());
    const auto plan = kachakacha::v2::app::SurfaceEditSlotsFor(state.operation);
    continuityALabel_->setText(state.operation == SurfaceEditOperation::Bridge
            ? QStringLiteral("縁 A の滑らかさ") : QStringLiteral("滑らかさ"));
    continuityALabel_->setVisible(plan.continuityA);
    continuityA_->setVisible(plan.continuityA);
    continuityBLabel_->setVisible(plan.continuityB);
    continuityB_->setVisible(plan.continuityB);
    continuityA_->setCurrentIndex(static_cast<int>(state.continuityA));
    continuityB_->setCurrentIndex(static_cast<int>(state.continuityB));
    const bool refit = state.operation == SurfaceEditOperation::Refit;
    toleranceLabel_->setVisible(refit);
    tolerance_->setVisible(refit);
    tolerance_->setValue(state.toleranceMm);
    const bool bridge = state.operation == SurfaceEditOperation::Bridge;
    tensionLabel_->setVisible(bridge);
    tension_->setVisible(bridge);
    tension_->setValue(state.tension);
    const bool iso = state.operation == SurfaceEditOperation::IsoCurve;
    isoLabel_->setVisible(iso);
    isoDirection_->setVisible(iso);
    isoCount_->setVisible(iso);
    isoDirection_->setCurrentIndex(state.isoDirection);
    isoCount_->setValue(state.isoCount);
    const bool mirror = state.operation == SurfaceEditOperation::Mirror;
    mirrorLabel_->setVisible(mirror);
    mirrorPlane_->setVisible(mirror);
    mirrorPlane_->setCurrentIndex(static_cast<int>(state.mirrorPlane));
    QString text;
    for (const QString& line : statusLinesJa) {
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + line;
    }
    status_->setText(text);
    confirm_->setEnabled(canConfirm);
    loading_ = false;
}

void V2SurfaceEditDock::SetOperationHandler(std::function<void(SurfaceEditOperation)> handler)
{
    operationHandler_ = std::move(handler);
}

void V2SurfaceEditDock::SetRemoveHandler(
    std::function<void(const kachakacha::v2::base::EntityId&)> handler)
{
    removeHandler_ = std::move(handler);
}

void V2SurfaceEditDock::SetClearHandler(std::function<void()> handler)
{
    clearHandler_ = std::move(handler);
}

void V2SurfaceEditDock::SetOptionsHandler(
    std::function<void(const kachakacha::v2::app::SurfaceEditInputState&)> handler)
{
    optionsHandler_ = std::move(handler);
}

void V2SurfaceEditDock::SetActionHandlers(std::function<void()> confirm,
    std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

bool V2SurfaceEditDock::ClickOperation(SurfaceEditOperation operation)
{
    const auto& operations = kachakacha::v2::app::SurfaceEditOperations();
    for (std::size_t index = 0; index < operations.size() && index < cards_.size(); ++index) {
        if (operations[index] == operation) {
            return ClickIfVisible(cards_[index]);
        }
    }
    return false;
}

bool V2SurfaceEditDock::ClickRemoveRow(int row)
{
    if (row < 0 || row >= entries_->topLevelItemCount()) {
        return false;
    }
    entries_->setCurrentItem(entries_->topLevelItem(row));
    return ClickIfVisible(remove_);
}

bool V2SurfaceEditDock::ClickClear()
{
    return ClickIfVisible(clear_);
}

bool V2SurfaceEditDock::ClickConfirm()
{
    return ClickIfVisible(confirm_);
}

bool V2SurfaceEditDock::ClickCancel()
{
    return ClickIfVisible(cancel_);
}

bool V2SurfaceEditDock::ChooseContinuity(bool second, SurfaceContinuity value)
{
    QComboBox* combo = second ? continuityB_ : continuityA_;
    if (!combo->isVisible()) {
        return false;
    }
    combo->setCurrentIndex(static_cast<int>(value));
    return true;
}

bool V2SurfaceEditDock::TypeTolerance(double millimetres)
{
    if (!tolerance_->isVisible()) {
        return false;
    }
    tolerance_->setValue(millimetres);
    return true;
}

bool V2SurfaceEditDock::TypeTension(double value)
{
    if (!tension_->isVisible()) {
        return false;
    }
    tension_->setValue(value);
    return true;
}

bool V2SurfaceEditDock::ChooseIso(int direction, int count)
{
    if (!isoDirection_->isVisible()) {
        return false;
    }
    isoDirection_->setCurrentIndex(direction);
    isoCount_->setValue(count);
    return true;
}

bool V2SurfaceEditDock::ChooseMirrorPlane(MirrorPlaneChoice choice)
{
    if (!mirrorPlane_->isVisible()) {
        return false;
    }
    mirrorPlane_->setCurrentIndex(static_cast<int>(choice));
    return true;
}

std::vector<QString> V2SurfaceEditDock::EntryTexts() const
{
    std::vector<QString> texts;
    for (const Entry& entry : shownEntries_) {
        texts.push_back(entry.slotJa + QStringLiteral(" ") + entry.nameJa);
    }
    return texts;
}

QString V2SurfaceEditDock::StatusTextJa() const
{
    return status_->text();
}

bool V2SurfaceEditDock::ContinuityRowShown(bool second) const
{
    return second ? continuityB_->isVisible() : continuityA_->isVisible();
}

bool V2SurfaceEditDock::ConfirmEnabled() const
{
    return confirm_->isEnabled();
}
