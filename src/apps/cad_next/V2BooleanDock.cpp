#include "V2BooleanDock.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace {

using kachakacha::v2::app::BooleanSlot;

[[nodiscard]] bool ClickIfVisible(QPushButton* button)
{
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;   // 見えていない・押せないものは押せない。
    }
    button->click();
    return true;
}

} // namespace

V2BooleanDock::V2BooleanDock(QWidget* parent)
    : QDockWidget(QStringLiteral("足す・引く"), parent)
{
    setObjectName(QStringLiteral("booleanDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 1. 操作。押して切り替える。押された形が、いまの操作。
    layout->addWidget(new QLabel(QStringLiteral("1. 操作"), body));
    auto* operations = new QHBoxLayout();
    add_ = new QPushButton(QStringLiteral("足す"), body);
    cut_ = new QPushButton(QStringLiteral("引く"), body);
    add_->setCheckable(true);
    cut_->setCheckable(true);
    QObject::connect(add_, &QPushButton::clicked, this, [this] {
        if (!loading_ && operationHandler_) {
            operationHandler_(false);
        }
    });
    QObject::connect(cut_, &QPushButton::clicked, this, [this] {
        if (!loading_ && operationHandler_) {
            operationHandler_(true);
        }
    });
    operations->addWidget(add_);
    operations->addWidget(cut_);
    layout->addLayout(operations);

    BuildRows(layout);

    // 3. 状態。
    layout->addWidget(new QLabel(QStringLiteral("3. 状態"), body));
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

void V2BooleanDock::BuildRows(QVBoxLayout* layout)
{
    // 2. 入力。土台 / 相手。欄ごとに「ここへ選ぶ」と「解除」。
    // 3D の次のクリックがどの欄へ入るかは、押された形の「ここへ選ぶ」でいつも見えている。
    layout->addWidget(new QLabel(QStringLiteral("2. 入力"), widget()));
    const auto row = [this, layout](const QString& name, QLabel** value, QPushButton** arm,
                         QPushButton** clear, BooleanSlot slot) {
        auto* line = new QHBoxLayout();
        line->addWidget(new QLabel(name, widget()));
        *value = new QLabel(widget());
        (*value)->setWordWrap(true);
        line->addWidget(*value, 1);
        *arm = new QPushButton(QStringLiteral("ここへ選ぶ"), widget());
        (*arm)->setCheckable(true);
        QObject::connect(*arm, &QPushButton::clicked, this, [this, slot] {
            if (!loading_ && activateHandler_) {
                activateHandler_(slot);
            }
        });
        line->addWidget(*arm);
        *clear = new QPushButton(QStringLiteral("解除"), widget());
        QObject::connect(*clear, &QPushButton::clicked, this, [this, slot] {
            if (!loading_ && clearHandler_) {
                clearHandler_(slot);
            }
        });
        line->addWidget(*clear);
        layout->addLayout(line);
    };
    row(QStringLiteral("土台"), &targetValue_, &armTarget_, &clearTarget_, BooleanSlot::Target);
    row(QStringLiteral("相手"), &toolValue_, &armTool_, &clearTool_, BooleanSlot::Tool);
}

void V2BooleanDock::ShowInput(const kachakacha::v2::app::BooleanInputState& state,
    const QString& targetNameJa, const QString& toolNameJa,
    const std::vector<QString>& statusLinesJa, bool canConfirm)
{
    loading_ = true;
    add_->setChecked(!state.cut);
    cut_->setChecked(state.cut);
    targetValue_->setText(state.target.IsNil() ? QStringLiteral("(選んでいません)")
                                                : targetNameJa);
    toolValue_->setText(state.tool.IsNil() ? QStringLiteral("(選んでいません)") : toolNameJa);
    const BooleanSlot next = kachakacha::v2::app::NextBooleanSlot(state);
    armTarget_->setChecked(next == BooleanSlot::Target);
    armTool_->setChecked(next == BooleanSlot::Tool);
    clearTarget_->setEnabled(!state.target.IsNil());
    clearTool_->setEnabled(!state.tool.IsNil());
    QString status;
    for (const QString& line : statusLinesJa) {
        if (!status.isEmpty()) {
            status += QStringLiteral("\n");
        }
        status += line;
    }
    status_->setText(status);
    confirm_->setEnabled(canConfirm);
    loading_ = false;
}

void V2BooleanDock::SetOperationHandler(std::function<void(bool)> handler)
{
    operationHandler_ = std::move(handler);
}

void V2BooleanDock::SetActivateHandler(std::function<void(BooleanSlot)> handler)
{
    activateHandler_ = std::move(handler);
}

void V2BooleanDock::SetClearHandler(std::function<void(BooleanSlot)> handler)
{
    clearHandler_ = std::move(handler);
}

void V2BooleanDock::SetActionHandlers(std::function<void()> confirm,
    std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

QPushButton* V2BooleanDock::ArmFor(BooleanSlot slot) const
{
    return slot == BooleanSlot::Target ? armTarget_ : armTool_;
}

QPushButton* V2BooleanDock::ClearFor(BooleanSlot slot) const
{
    return slot == BooleanSlot::Target ? clearTarget_ : clearTool_;
}

bool V2BooleanDock::ClickOperation(bool cut)
{
    return ClickIfVisible(cut ? cut_ : add_);
}

bool V2BooleanDock::ClickActivate(BooleanSlot slot)
{
    return ClickIfVisible(ArmFor(slot));
}

bool V2BooleanDock::ClickClear(BooleanSlot slot)
{
    return ClickIfVisible(ClearFor(slot));
}

bool V2BooleanDock::ClickConfirm()
{
    return ClickIfVisible(confirm_);
}

BooleanSlot V2BooleanDock::ActiveSlotShown() const
{
    return armTool_->isChecked() ? BooleanSlot::Tool : BooleanSlot::Target;
}

QString V2BooleanDock::TargetTextJa() const
{
    return targetValue_->text();
}

QString V2BooleanDock::ToolTextJa() const
{
    return toolValue_->text();
}

bool V2BooleanDock::CutShown() const
{
    return cut_->isChecked();
}

QString V2BooleanDock::StatusTextJa() const
{
    return status_->text();
}
