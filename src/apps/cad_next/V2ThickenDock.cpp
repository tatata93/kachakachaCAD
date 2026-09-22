#include "V2ThickenDock.h"
#include "V2PanelFrame.h"

#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace {

using kachakacha::v2::fabrication::ThicknessPlacement;

[[nodiscard]] bool ClickIfVisible(QPushButton* button)
{
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;   // 見えていない・押せないものは押せない。
    }
    button->click();
    return true;
}

} // namespace

V2ThickenDock::V2ThickenDock(QWidget* parent)
    : QDockWidget(QStringLiteral("厚み"), parent)
{
    setObjectName(QStringLiteral("thickenDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 1. 作り方(正本の順: 見出し → 作り方 → 入力 → 設定 → 状態 → キャンセル・確定、C-10)。
    BuildPlacementCards(layout);

    // 2. 入力。面は何枚でも入る欄なので、「選び直す」だけでよい
    // (2つ以上の欄がある足す・引くと違い、次のクリックの行き先で迷う余地がない)。
    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("2. 入力")));
    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(new QLabel(QStringLiteral("面"), body));
    surfaceValue_ = new QLabel(body);
    surfaceValue_->setWordWrap(true);
    inputRow->addWidget(surfaceValue_, 1);
    reselect_ = new QPushButton(QStringLiteral("選び直す"), body);
    QObject::connect(reselect_, &QPushButton::clicked, this, [this] {
        if (!loading_ && reselectHandler_) {
            reselectHandler_();
        }
    });
    inputRow->addWidget(reselect_);
    layout->addLayout(inputRow);

    // 3. 厚み。「平面まで」の間は相手の作業平面の欄に差し替わる。
    auto* form = new QFormLayout();
    thicknessLabel_ = MakePanelSectionTitle(body, QStringLiteral("3. 厚み"));
    thickness_ = new QDoubleSpinBox(body);
    thickness_->setRange(0.0, 1000.0);
    thickness_->setDecimals(3);
    thickness_->setSuffix(QStringLiteral(" mm"));
    form->addRow(thicknessLabel_, thickness_);
    targetLabel_ = MakePanelSectionTitle(body, QStringLiteral("3. 相手の作業平面"));
    target_ = new QComboBox(body);
    form->addRow(targetLabel_, target_);
    layout->addLayout(form);
    QObject::connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && thicknessHandler_) {
            thicknessHandler_(value);
        }
    });
    QObject::connect(target_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading_ || !targetHandler_) {
            return;
        }
        if (index < 0 || index >= static_cast<int>(targets_.size())) {
            return;
        }
        targetHandler_(targets_[static_cast<std::size_t>(index)].entityId);
    });

    // 4. 状態。
    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("4. 状態")));
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

void V2ThickenDock::BuildPlacementCards(QVBoxLayout* layout)
{
    // 1. 作り方。外側・中央・内側・平面まで。押して切り替える(押された形がいまの作り方)。
    layout->addWidget(MakePanelSectionTitle(widget(), QStringLiteral("1. 作り方")));
    auto* cards = new QHBoxLayout();
    outsideCard_ = new QPushButton(QStringLiteral("外側"), widget());
    centeredCard_ = new QPushButton(QStringLiteral("中央"), widget());
    insideCard_ = new QPushButton(QStringLiteral("内側"), widget());
    toPlaneCard_ = new QPushButton(QStringLiteral("平面まで"), widget());
    for (QPushButton* card : {outsideCard_, centeredCard_, insideCard_, toPlaneCard_}) {
        card->setCheckable(true);
        cards->addWidget(card);
    }
    QObject::connect(outsideCard_, &QPushButton::clicked, this, [this] {
        if (!loading_ && placementHandler_) {
            placementHandler_(ThicknessPlacement::Outside);
        }
    });
    QObject::connect(centeredCard_, &QPushButton::clicked, this, [this] {
        if (!loading_ && placementHandler_) {
            placementHandler_(ThicknessPlacement::Centered);
        }
    });
    QObject::connect(insideCard_, &QPushButton::clicked, this, [this] {
        if (!loading_ && placementHandler_) {
            placementHandler_(ThicknessPlacement::Inside);
        }
    });
    QObject::connect(toPlaneCard_, &QPushButton::clicked, this, [this] {
        if (!loading_ && toPlaneHandler_) {
            toPlaneHandler_();
        }
    });
    layout->addLayout(cards);
}

void V2ThickenDock::ShowInput(const kachakacha::v2::app::ThickenInputState& state,
    const QString& surfaceNameJa, const std::vector<QString>& statusLinesJa, bool canConfirm)
{
    loading_ = true;
    surfaceValue_->setText(state.surfaces.empty() ? QStringLiteral("(選んでいません)")
                                                   : surfaceNameJa);
    outsideCard_->setChecked(!state.toPlane && state.placement == ThicknessPlacement::Outside);
    centeredCard_->setChecked(!state.toPlane && state.placement == ThicknessPlacement::Centered);
    insideCard_->setChecked(!state.toPlane && state.placement == ThicknessPlacement::Inside);
    toPlaneCard_->setChecked(state.toPlane);
    thickness_->setValue(state.thicknessMm);
    thicknessLabel_->setVisible(!state.toPlane);
    thickness_->setVisible(!state.toPlane);
    targetLabel_->setVisible(state.toPlane);
    target_->setVisible(state.toPlane);
    if (state.toPlane) {
        for (std::size_t index = 0; index < targets_.size(); ++index) {
            if (targets_[index].entityId == state.targetPlane) {
                target_->setCurrentIndex(static_cast<int>(index));
            }
        }
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

void V2ThickenDock::SetTargets(const std::vector<ExtrudeTargetChoice>& targets)
{
    bool same = targets.size() == targets_.size();
    for (std::size_t index = 0; same && index < targets.size(); ++index) {
        same = targets[index].entityId == targets_[index].entityId
            && targets[index].labelJa == targets_[index].labelJa;
    }
    if (same) {
        return;
    }
    const auto chosen = TargetEntityId();
    targets_ = targets;
    const bool blocked = target_->blockSignals(true);
    target_->clear();
    for (const auto& choice : targets_) {
        target_->addItem(choice.labelJa);
    }
    if (targets_.empty()) {
        target_->addItem(QStringLiteral("(作業平面がありません。先に作業面を作ってください)"));
    }
    if (chosen.has_value()) {
        for (std::size_t index = 0; index < targets_.size(); ++index) {
            if (targets_[index].entityId == *chosen) {
                target_->setCurrentIndex(static_cast<int>(index));
            }
        }
    }
    target_->blockSignals(blocked);
}

std::optional<kachakacha::v2::base::EntityId> V2ThickenDock::TargetEntityId() const
{
    const int index = target_ == nullptr ? -1 : target_->currentIndex();
    if (index < 0 || index >= static_cast<int>(targets_.size())) {
        return std::nullopt;
    }
    return targets_[static_cast<std::size_t>(index)].entityId;
}

void V2ThickenDock::SetReselectHandler(std::function<void()> handler)
{
    reselectHandler_ = std::move(handler);
}

void V2ThickenDock::SetPlacementHandler(std::function<void(ThicknessPlacement)> handler)
{
    placementHandler_ = std::move(handler);
}

void V2ThickenDock::SetToPlaneHandler(std::function<void()> handler)
{
    toPlaneHandler_ = std::move(handler);
}

void V2ThickenDock::SetTargetHandler(
    std::function<void(const kachakacha::v2::base::EntityId&)> handler)
{
    targetHandler_ = std::move(handler);
}

void V2ThickenDock::SetThicknessHandler(std::function<void(double)> handler)
{
    thicknessHandler_ = std::move(handler);
}

void V2ThickenDock::SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

bool V2ThickenDock::ClickReselect()
{
    return ClickIfVisible(reselect_);
}

bool V2ThickenDock::ClickPlacementCard(ThicknessPlacement value)
{
    switch (value) {
    case ThicknessPlacement::Outside:  return ClickIfVisible(outsideCard_);
    case ThicknessPlacement::Centered: return ClickIfVisible(centeredCard_);
    case ThicknessPlacement::Inside:   return ClickIfVisible(insideCard_);
    }
    return false;
}

bool V2ThickenDock::ClickToPlaneCard()
{
    return ClickIfVisible(toPlaneCard_);
}

bool V2ThickenDock::ClickConfirm()
{
    return ClickIfVisible(confirm_);
}

QString V2ThickenDock::SurfaceTextJa() const
{
    return surfaceValue_->text();
}

QString V2ThickenDock::StatusTextJa() const
{
    return status_->text();
}

bool V2ThickenDock::ThicknessRowShown() const
{
    return thickness_ != nullptr && thickness_->isVisible();
}

bool V2ThickenDock::TargetRowShown() const
{
    return target_ != nullptr && target_->isVisible();
}
