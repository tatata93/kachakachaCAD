#include "V2SolidDock.h"
#include "V2PanelFrame.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <utility>

namespace {

using kachakacha::v2::app::SolidSlot;
using kachakacha::v2::modeling::SolidMethod;

[[nodiscard]] bool ClickIfUsable(QPushButton* button)
{
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;   // 見えていない・押せないものは押せない。
    }
    button->click();
    return true;
}

[[nodiscard]] QString Text(std::string_view text)
{
    return QString::fromUtf8(std::string(text).c_str());
}

} // namespace

V2SolidDock::V2SolidDock(QWidget* parent)
    : QDockWidget(QStringLiteral("立体を作る"), parent)
{
    setObjectName(QStringLiteral("solidDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // いまの道具の名前(回転体 / ロフト立体 / スイープ)。棚の見出しは「立体を作る」なので、
    // どの作り方の最中かをここで見せる。
    toolName_ = new QLabel(body);
    toolName_->setObjectName(QStringLiteral("solidToolName"));
    QFont font = toolName_->font();
    font.setBold(true);
    toolName_->setFont(font);
    layout->addWidget(toolName_);

    BuildMethods(layout);
    BuildInputs(layout);
    BuildSettings(layout);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("4. 状態")));
    status_ = new QLabel(body);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch(1);

    layout->addLayout(MakeCancelConfirmRow(body, &cancel_, &confirm_));
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
}

//! 1. 作り方。3 枚のカード。字と押せるかは作り方ごとに core の表(SolidMethodCards)から。
void V2SolidDock::BuildMethods(QVBoxLayout* layout)
{
    layout->addWidget(MakePanelSectionTitle(widget(), QStringLiteral("1. 作り方")));
    auto* row = new QHBoxLayout();
    for (std::size_t index = 0; index < cards_.size(); ++index) {
        auto* card = new QPushButton(widget());
        card->setCheckable(true);
        const int number = static_cast<int>(index);
        QObject::connect(card, &QPushButton::clicked, this, [this, number] {
            if (loading_) {
                return;
            }
            if (method_ == SolidMethod::Revolve && revolveModeHandler_) {
                revolveModeHandler_(kachakacha::v2::app::RevolveModeOfCard(number));
                return;
            }
            // ロフト立体・スイープは押せるカードが 1 枚だけ。押された形を戻す。
            cards_[static_cast<std::size_t>(number)]->setChecked(number == 0);
        });
        row->addWidget(card);
        cards_[index] = card;
    }
    layout->addLayout(row);
    // 押せないカードの理由は、ツールチップだけでなく字でも出す(触らなくても読める)。
    methodNote_ = new QLabel(widget());
    methodNote_->setObjectName(QStringLiteral("solidMethodNote"));
    methodNote_->setWordWrap(true);
    layout->addWidget(methodNote_);
}

//! 2. 入力。輪郭(ロフト立体は断面)と、回転軸 / 経路。
void V2SolidDock::BuildInputs(QVBoxLayout* layout)
{
    layout->addWidget(MakePanelSectionTitle(widget(), QStringLiteral("2. 入力")));
    BuildSlotRow(layout, profile_, SolidSlot::Profiles);
    BuildSlotRow(layout, second_, SolidSlot::Axis);
}

void V2SolidDock::BuildSlotRow(QVBoxLayout* layout, SlotRow& row, SolidSlot slot)
{
    auto* line = new QHBoxLayout();
    row.name = new QLabel(widget());
    line->addWidget(row.name);
    row.value = new QLabel(widget());
    row.value->setWordWrap(true);
    line->addWidget(row.value, 1);
    row.arm = new QPushButton(QStringLiteral("ここへ選ぶ"), widget());
    row.arm->setCheckable(true);
    row.clear = new QPushButton(QStringLiteral("解除"), widget());
    // 2 段目の欄は作り方で中身(回転軸 / 経路)が替わる。押したときの欄は、そのとき読む。
    const bool second = &row == &second_;
    QObject::connect(row.arm, &QPushButton::clicked, this, [this, slot, second] {
        if (!loading_ && activateHandler_) {
            activateHandler_(second ? secondSlot_ : slot);
        }
    });
    QObject::connect(row.clear, &QPushButton::clicked, this, [this, slot, second] {
        if (!loading_ && clearHandler_) {
            clearHandler_(second ? secondSlot_ : slot);
        }
    });
    line->addWidget(row.arm);
    line->addWidget(row.clear);
    layout->addLayout(line);
}

//! 3. 設定。角度(回転体)・姿勢(スイープ)・操作と相手。
void V2SolidDock::BuildSettings(QVBoxLayout* layout)
{
    layout->addWidget(MakePanelSectionTitle(widget(), QStringLiteral("3. 設定")));
    auto* angleRow = new QHBoxLayout();
    angleLabel_ = new QLabel(QStringLiteral("角度"), widget());
    angleRow->addWidget(angleLabel_);
    angle_ = new QDoubleSpinBox(widget());
    angle_->setObjectName(QStringLiteral("solidAngle"));
    angle_->setRange(0.1, 360.0);
    angle_->setDecimals(3);
    angle_->setSuffix(QStringLiteral("°"));
    angleRow->addWidget(angle_, 1);
    layout->addLayout(angleRow);
    QObject::connect(angle_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && angleHandler_) {
            angleHandler_(value);
        }
    });
    // スイープの姿勢は「経路に追従」だけ(一定方向はまだ無い)。選べるふりをしない。
    poseLabel_ = new QLabel(QStringLiteral("姿勢: 経路に追従(一定方向はまだ選べません)"), widget());
    poseLabel_->setWordWrap(true);
    layout->addWidget(poseLabel_);

    auto* operations = new QHBoxLayout();
    operations->addWidget(new QLabel(QStringLiteral("操作"), widget()));
    for (std::size_t index = 0; index < booleans_.size(); ++index) {
        const int mode = static_cast<int>(index);
        auto* button = new QPushButton(
            Text(kachakacha::v2::app::SolidBooleanNameJa(mode)), widget());
        button->setCheckable(true);
        QObject::connect(button, &QPushButton::clicked, this, [this, mode] {
            if (!loading_ && booleanHandler_) {
                booleanHandler_(mode);
            }
        });
        operations->addWidget(button);
        booleans_[index] = button;
    }
    layout->addLayout(operations);
    BuildSlotRow(layout, target_, SolidSlot::Target);
}

const V2SolidDock::SlotRow* V2SolidDock::RowFor(SolidSlot slot) const
{
    switch (slot) {
    case SolidSlot::Profiles: return &profile_;
    case SolidSlot::Axis:
    case SolidSlot::Path:     return slot == secondSlot_ && method_ != SolidMethod::Loft ? &second_ : nullptr;
    case SolidSlot::Target:   return &target_;
    }
    return nullptr;
}

void V2SolidDock::ShowInput(const kachakacha::v2::app::SolidInputState& state,
    const QString& profileNamesJa, const QString& secondNamesJa, const QString& targetNameJa,
    const std::vector<QString>& statusLinesJa, bool canConfirm)
{
    loading_ = true;
    method_ = state.method;
    toolName_->setText(Text(kachakacha::v2::modeling::SolidMethodNameJa(state.method)));
    ShowMethods(state);
    ShowSlots(state, profileNamesJa, secondNamesJa, targetNameJa);
    const bool revolve = state.method == SolidMethod::Revolve;
    angleLabel_->setVisible(revolve);
    angle_->setVisible(revolve);
    // 全回転は 360° で固定(打てる形にしない)。角度指定・対称回転のときだけ打てる。
    const bool typed = revolve && state.revolveMode != kachakacha::v2::app::RevolveMode::Full;
    angle_->setEnabled(typed);
    angle_->setValue(typed ? state.angleDeg : 360.0);
    poseLabel_->setVisible(state.method == SolidMethod::Sweep);
    for (std::size_t index = 0; index < booleans_.size(); ++index) {
        booleans_[index]->setChecked(static_cast<int>(index) == state.booleanMode);
    }
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

void V2SolidDock::ShowMethods(const kachakacha::v2::app::SolidInputState& state)
{
    const auto& cards = kachakacha::v2::app::SolidMethodCards(state.method);
    const int checked = kachakacha::v2::app::SolidMethodCardIndex(state);
    QString note;
    for (std::size_t index = 0; index < cards_.size(); ++index) {
        QPushButton* card = cards_[index];
        card->setText(Text(cards[index].labelJa));
        card->setToolTip(Text(cards[index].tipJa));
        card->setEnabled(cards[index].available);
        card->setChecked(static_cast<int>(index) == checked);
        if (!cards[index].available) {
            note += (note.isEmpty() ? QString() : QStringLiteral("\n"))
                + Text(cards[index].labelJa) + QStringLiteral(": ") + Text(cards[index].tipJa);
        }
    }
    methodNote_->setText(note);
    methodNote_->setVisible(!note.isEmpty());
}

void V2SolidDock::ShowSlots(const kachakacha::v2::app::SolidInputState& state,
    const QString& profileNamesJa, const QString& secondNamesJa, const QString& targetNameJa)
{
    const SolidSlot next = kachakacha::v2::app::NextSolidSlot(state);
    const QString none = QStringLiteral("(選んでいません)");
    profile_.name->setText(Text(kachakacha::v2::app::SolidSlotNameJa(SolidSlot::Profiles, state.method)));
    profile_.value->setText(state.profiles.empty() ? none : profileNamesJa);
    profile_.arm->setChecked(next == SolidSlot::Profiles);
    profile_.clear->setEnabled(!state.profiles.empty());

    const bool hasSecond = state.method != SolidMethod::Loft;
    secondSlot_ = state.method == SolidMethod::Sweep ? SolidSlot::Path : SolidSlot::Axis;
    const bool secondFilled = secondSlot_ == SolidSlot::Axis ? !state.axis.IsNil() : !state.path.empty();
    for (QWidget* widget : {static_cast<QWidget*>(second_.name), static_cast<QWidget*>(second_.value),
             static_cast<QWidget*>(second_.arm), static_cast<QWidget*>(second_.clear)}) {
        widget->setVisible(hasSecond);
    }
    second_.name->setText(Text(kachakacha::v2::app::SolidSlotNameJa(secondSlot_, state.method)));
    second_.value->setText(secondFilled ? secondNamesJa : none);
    second_.arm->setChecked(hasSecond && next == secondSlot_);
    second_.clear->setEnabled(secondFilled);

    // 相手は足す・引くのときだけ。新しい部品のときは欄ごと隠す(要らない欄を置かない)。
    const bool hasTarget = state.booleanMode != 0;
    for (QWidget* widget : {static_cast<QWidget*>(target_.name), static_cast<QWidget*>(target_.value),
             static_cast<QWidget*>(target_.arm), static_cast<QWidget*>(target_.clear)}) {
        widget->setVisible(hasTarget);
    }
    target_.name->setText(Text(kachakacha::v2::app::SolidSlotNameJa(SolidSlot::Target, state.method)));
    target_.value->setText(state.target.IsNil() ? none : targetNameJa);
    target_.arm->setChecked(hasTarget && next == SolidSlot::Target);
    target_.clear->setEnabled(!state.target.IsNil());
}

void V2SolidDock::SetRevolveModeHandler(
    std::function<void(kachakacha::v2::app::RevolveMode)> handler)
{
    revolveModeHandler_ = std::move(handler);
}

void V2SolidDock::SetAngleHandler(std::function<void(double)> handler)
{
    angleHandler_ = std::move(handler);
}

void V2SolidDock::SetBooleanHandler(std::function<void(int)> handler)
{
    booleanHandler_ = std::move(handler);
}

void V2SolidDock::SetActivateHandler(std::function<void(SolidSlot)> handler)
{
    activateHandler_ = std::move(handler);
}

void V2SolidDock::SetClearHandler(std::function<void(SolidSlot)> handler)
{
    clearHandler_ = std::move(handler);
}

void V2SolidDock::SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

bool V2SolidDock::ClickMethodCard(int index)
{
    return index >= 0 && index < static_cast<int>(cards_.size())
        && ClickIfUsable(cards_[static_cast<std::size_t>(index)]);
}

bool V2SolidDock::ClickBoolean(int booleanMode)
{
    return booleanMode >= 0 && booleanMode < static_cast<int>(booleans_.size())
        && ClickIfUsable(booleans_[static_cast<std::size_t>(booleanMode)]);
}

bool V2SolidDock::ClickActivate(SolidSlot slot)
{
    const SlotRow* row = RowFor(slot);
    return row != nullptr && ClickIfUsable(row->arm);
}

bool V2SolidDock::ClickClear(SolidSlot slot)
{
    const SlotRow* row = RowFor(slot);
    return row != nullptr && ClickIfUsable(row->clear);
}

bool V2SolidDock::ClickConfirm()
{
    return ClickIfUsable(confirm_);
}

bool V2SolidDock::TypeAngle(double degrees)
{
    if (angle_ == nullptr || !angle_->isVisible() || !angle_->isEnabled()) {
        return false;
    }
    angle_->setValue(degrees);
    return true;
}

QString V2SolidDock::MethodCardTextJa(int index) const
{
    return index >= 0 && index < static_cast<int>(cards_.size())
        ? cards_[static_cast<std::size_t>(index)]->text()
        : QString();
}

bool V2SolidDock::MethodCardEnabled(int index) const
{
    return index >= 0 && index < static_cast<int>(cards_.size())
        && cards_[static_cast<std::size_t>(index)]->isEnabled();
}

bool V2SolidDock::MethodCardChecked(int index) const
{
    return index >= 0 && index < static_cast<int>(cards_.size())
        && cards_[static_cast<std::size_t>(index)]->isChecked();
}

QString V2SolidDock::MethodCardWhyJa(int index) const
{
    return index >= 0 && index < static_cast<int>(cards_.size())
        ? cards_[static_cast<std::size_t>(index)]->toolTip()
        : QString();
}

QString V2SolidDock::ToolNameJa() const
{
    return toolName_->text();
}

SolidSlot V2SolidDock::ActiveSlotShown() const
{
    // 欄を出しているか(隠していないか)で読む。棚が前に出ていなくても同じ答えにする。
    if (!second_.arm->isHidden() && second_.arm->isChecked()) {
        return secondSlot_;
    }
    if (!target_.arm->isHidden() && target_.arm->isChecked()) {
        return SolidSlot::Target;
    }
    return SolidSlot::Profiles;
}

QString V2SolidDock::SlotTextJa(SolidSlot slot) const
{
    const SlotRow* row = RowFor(slot);
    return row != nullptr && !row->value->isHidden() ? row->value->text() : QString();
}

bool V2SolidDock::SlotRowShown(SolidSlot slot) const
{
    const SlotRow* row = RowFor(slot);
    return row != nullptr && row->value->isVisible();
}

bool V2SolidDock::AngleEditable() const
{
    return angle_ != nullptr && angle_->isVisible() && angle_->isEnabled();
}

QString V2SolidDock::StatusTextJa() const
{
    return status_->text();
}

bool V2SolidDock::ConfirmEnabled() const
{
    return confirm_ != nullptr && confirm_->isEnabled();
}
