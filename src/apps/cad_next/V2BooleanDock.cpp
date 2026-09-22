#include "V2BooleanDock.h"
#include "V2PanelFrame.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

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

    // 1. 作り方(足す・引く)。押して切り替える。押された形が、いまの作り方(正本の methods)。
    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("1. 作り方")));
    auto* operations = new QHBoxLayout();
    add_ = new QPushButton(QStringLiteral("足す"), body);
    cut_ = new QPushButton(QStringLiteral("引く"), body);
    intersect_ = new QPushButton(QStringLiteral("交差"), body);
    intersect_->setToolTip(QStringLiteral("土台と相手に共通する部分だけを残します(相手が何個でも、全部に共通する部分)。"));
    using kachakacha::v2::app::BooleanKind;
    const std::pair<QPushButton*, BooleanKind> kinds[] = {
        {add_, BooleanKind::Add}, {cut_, BooleanKind::Cut}, {intersect_, BooleanKind::Intersect}};
    for (const auto& [button, kind] : kinds) {
        button->setCheckable(true);
        QObject::connect(button, &QPushButton::clicked, this, [this, kind = kind] {
            if (!loading_ && operationHandler_) {
                operationHandler_(kind);
            }
        });
        operations->addWidget(button);
    }
    layout->addLayout(operations);

    BuildRows(layout);

    // 3. 状態。
    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("3. 状態")));
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
    MarkCancelConfirm(cancel_, confirm_);
    layout->addLayout(actions);
}

void V2BooleanDock::BuildRows(QVBoxLayout* layout)
{
    // 2. 入力。土台 / 相手。欄ごとに「ここへ選ぶ」と「解除」。
    // 3D の次のクリックがどの欄へ入るかは、押された形の「ここへ選ぶ」でいつも見えている。
    layout->addWidget(MakePanelSectionTitle(widget(), QStringLiteral("2. 入力")));
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
    add_->setChecked(state.kind == kachakacha::v2::app::BooleanKind::Add);
    cut_->setChecked(state.kind == kachakacha::v2::app::BooleanKind::Cut);
    intersect_->setChecked(state.kind == kachakacha::v2::app::BooleanKind::Intersect);
    targetValue_->setText(state.target.IsNil() ? QStringLiteral("(選んでいません)")
                                                : targetNameJa);
    toolValue_->setText(state.tools.empty() ? QStringLiteral("(選んでいません)") : toolNameJa);
    const BooleanSlot next = kachakacha::v2::app::NextBooleanSlot(state);
    armTarget_->setChecked(next == BooleanSlot::Target);
    armTool_->setChecked(next == BooleanSlot::Tool);
    clearTarget_->setEnabled(!state.target.IsNil());
    clearTool_->setEnabled(!state.tools.empty());
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

void V2BooleanDock::SetOperationHandler(
    std::function<void(kachakacha::v2::app::BooleanKind)> handler)
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

bool V2BooleanDock::ClickOperation(kachakacha::v2::app::BooleanKind kind)
{
    using kachakacha::v2::app::BooleanKind;
    return ClickIfVisible(kind == BooleanKind::Cut ? cut_
            : kind == BooleanKind::Intersect      ? intersect_
                                                  : add_);
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

kachakacha::v2::app::BooleanKind V2BooleanDock::KindShown() const
{
    using kachakacha::v2::app::BooleanKind;
    return cut_->isChecked() ? BooleanKind::Cut
        : intersect_->isChecked() ? BooleanKind::Intersect
                                  : BooleanKind::Add;
}

QString V2BooleanDock::StatusTextJa() const
{
    return status_->text();
}
