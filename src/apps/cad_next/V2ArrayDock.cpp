#include "V2ArrayDock.h"
#include "V2PanelFrame.h"

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
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <utility>

namespace {

//! mm の欄。範囲は模型の大きさに合わせる(実物 1/87 で数メートル)。
[[nodiscard]] QDoubleSpinBox* MakeLength(QWidget* parent, double value)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(-100000.0, 100000.0);
    box->setDecimals(3);
    box->setSuffix(QStringLiteral(" mm"));
    box->setValue(value);
    return box;
}

} // namespace

V2ArrayDock::V2ArrayDock(QWidget* parent)
    : QDockWidget(QStringLiteral("配列"), parent)
{
    setObjectName(QStringLiteral("arrayDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("1. 作り方")));
    auto* methodRow = new QWidget(body);
    auto* methodLayout = new QHBoxLayout(methodRow);
    methodLayout->setContentsMargins(0, 0, 0, 0);
    methodLinear_ = new QToolButton(methodRow);
    methodLinear_->setObjectName(QStringLiteral("arrayMethodLinear"));
    methodLinear_->setText(QStringLiteral("直線"));
    methodLinear_->setCheckable(true);
    methodLinear_->setChecked(true);
    methodCircular_ = new QToolButton(methodRow);
    methodCircular_->setObjectName(QStringLiteral("arrayMethodCircular"));
    methodCircular_->setText(QStringLiteral("円形"));
    methodCircular_->setCheckable(true);
    methodLayout->addWidget(methodLinear_);
    methodLayout->addWidget(methodCircular_);
    layout->addWidget(methodRow);
    QObject::connect(methodLinear_, &QToolButton::clicked, this, [this] {
        circular_ = false;
        ApplyMethodVisibility();
    });
    QObject::connect(methodCircular_, &QToolButton::clicked, this, [this] {
        circular_ = true;
        ApplyMethodVisibility();
    });

    hint_ = new QLabel(
        QStringLiteral("並べる数と、直線なら間隔と向き、円形なら中心と全体角度を決めてください。"),
        body);
    hint_->setWordWrap(true);
    layout->addWidget(hint_);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("2. 設定")));
    auto* form = new QFormLayout();
    layout->addLayout(form);
    count_ = new QSpinBox(body);
    // 上限は core(ArrayPlan)と同じ 200。打ち間違いで画面が固まらないようにする。
    count_->setRange(2, 200);
    count_->setValue(5);
    count_->setToolTip(QStringLiteral("元のものを含めた数です。5 なら元 + 写し4つ。"));
    form->addRow(QStringLiteral("個数(元を含む)"), count_);

    linearRow_ = new QWidget(body);
    auto* linearForm = new QFormLayout(linearRow_);
    linearForm->setContentsMargins(0, 0, 0, 0);
    interval_ = MakeLength(linearRow_, 20.0);
    linearForm->addRow(QStringLiteral("間隔"), interval_);
    direction_ = new QComboBox(linearRow_);
    direction_->addItem(QStringLiteral("作業平面の X"));
    direction_->addItem(QStringLiteral("作業平面の Y"));
    linearForm->addRow(QStringLiteral("方向"), direction_);
    layout->addWidget(linearRow_);

    circularRow_ = new QWidget(body);
    auto* circularForm = new QFormLayout(circularRow_);
    circularForm->setContentsMargins(0, 0, 0, 0);
    centerU_ = MakeLength(circularRow_, 0.0);
    circularForm->addRow(QStringLiteral("中心 u"), centerU_);
    centerV_ = MakeLength(circularRow_, 0.0);
    circularForm->addRow(QStringLiteral("中心 v"), centerV_);
    totalAngle_ = new QDoubleSpinBox(circularRow_);
    totalAngle_->setRange(-360.0, 360.0);
    totalAngle_->setDecimals(2);
    totalAngle_->setSuffix(QStringLiteral(" °"));
    totalAngle_->setValue(360.0);
    totalAngle_->setToolTip(QStringLiteral(
        "端から端までの角です。360 なら一周で、最後の1つは元に重ねません。"));
    circularForm->addRow(QStringLiteral("全体の角度"), totalAngle_);
    layout->addWidget(circularRow_);

    layout->addStretch(1);
    auto* buttons = new QWidget(body);
    auto* buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    cancel_ = new QPushButton(QStringLiteral("キャンセル"), buttons);
    confirm_ = new QPushButton(QStringLiteral("確定"), buttons);
    MarkCancelConfirm(cancel_, confirm_);
    buttonsLayout->addWidget(cancel_);
    buttonsLayout->addWidget(confirm_);
    layout->addWidget(buttons);
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

    setWidget(body);
    ApplyMethodVisibility();
}

void V2ArrayDock::ApplyMethodVisibility()
{
    methodLinear_->setChecked(!circular_);
    methodCircular_->setChecked(circular_);
    linearRow_->setVisible(!circular_);
    circularRow_->setVisible(circular_);
}

void V2ArrayDock::ShowChoice(const V2ArrayChoice& choice, bool circular)
{
    loading_ = true;
    circular_ = circular;
    count_->setValue(choice.count);
    if (circular) {
        centerU_->setValue(choice.center.x);
        centerV_->setValue(choice.center.y);
        totalAngle_->setValue(choice.totalAngleDeg);
    } else {
        // 世界座標の step から、いちばん動いている軸を「間隔・方向」に戻す。
        // 作業平面の X/Y どちらへ寄っているかで選び直す(呼ぶ側は u/v をそのまま渡す)。
        const bool alongY = std::abs(choice.step.y) > std::abs(choice.step.x);
        interval_->setValue(alongY ? choice.step.y : choice.step.x);
        direction_->setCurrentIndex(alongY ? 1 : 0);
    }
    loading_ = false;
    ApplyMethodVisibility();
}

V2ArrayChoice V2ArrayDock::Choice() const
{
    V2ArrayChoice choice;
    choice.count = count_->value();
    choice.circular = circular_;
    if (circular_) {
        choice.totalAngleDeg = totalAngle_->value();
        choice.center = kachakacha::v2::geometry::Vector3{centerU_->value(), centerV_->value(),
            0.0};
        return choice;
    }
    choice.spanIsTotal = false;
    // u/v 平面上の値をそのまま x/y に仮置きする。世界座標へは呼ぶ側が変換する
    // (V2ArrayDock は作業平面を知らない)。
    if (direction_->currentIndex() == 1) {
        choice.step = kachakacha::v2::geometry::Vector3{0.0, interval_->value(), 0.0};
    } else {
        choice.step = kachakacha::v2::geometry::Vector3{interval_->value(), 0.0, 0.0};
    }
    return choice;
}

int V2ArrayDock::DirectionIndex() const
{
    return direction_ == nullptr ? 0 : direction_->currentIndex();
}

void V2ArrayDock::SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

bool V2ArrayDock::ClickConfirm()
{
    if (confirm_ == nullptr || !confirm_->isVisible()) {
        return false;
    }
    confirm_->click();
    return true;
}

bool V2ArrayDock::ClickCancel()
{
    if (cancel_ == nullptr || !cancel_->isVisible()) {
        return false;
    }
    cancel_->click();
    return true;
}

bool V2ArrayDock::ClickMethod(bool circular)
{
    QToolButton* button = circular ? methodCircular_ : methodLinear_;
    if (button == nullptr || !button->isVisible()) {
        return false;
    }
    button->click();
    return true;
}

void V2ArrayDock::SetCountTyped(int value)
{
    if (count_ != nullptr) {
        count_->setValue(value);
    }
}

int V2ArrayDock::Count() const
{
    return count_ == nullptr ? 0 : count_->value();
}
