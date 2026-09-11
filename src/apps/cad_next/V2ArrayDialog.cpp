#include "V2ArrayDialog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

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

V2ArrayDialog::V2ArrayDialog(const V2ArrayChoice& initial, bool circular, QWidget* parent)
    : QDialog(parent)
    , circular_(circular)
{
    setWindowTitle(circular ? QStringLiteral("円に並べる") : QStringLiteral("直線に並べる"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    count_ = new QSpinBox(this);
    // 上限は core と同じ 200。打ち間違いで画面が固まらないようにする。
    count_->setRange(2, 200);
    count_->setValue(initial.count);
    count_->setToolTip(QStringLiteral("元のものを含めた数です。5 なら元 + 写し4つ。"));
    form->addRow(QStringLiteral("個数(元を含む)"), count_);

    if (circular) {
        angle_ = new QDoubleSpinBox(this);
        angle_->setRange(-360.0, 360.0);
        angle_->setDecimals(2);
        angle_->setSuffix(QStringLiteral(" °"));
        angle_->setValue(initial.totalAngleDeg);
        angle_->setToolTip(QStringLiteral(
            "端から端までの角です。360 なら一周で、最後の1つは元に重ねません。"));
        form->addRow(QStringLiteral("全体の角度"), angle_);
        centerX_ = MakeLength(this, initial.center.x);
        centerY_ = MakeLength(this, initial.center.y);
        centerZ_ = MakeLength(this, initial.center.z);
        form->addRow(QStringLiteral("中心 X"), centerX_);
        form->addRow(QStringLiteral("中心 Y"), centerY_);
        form->addRow(QStringLiteral("中心 Z"), centerZ_);
        layout->addWidget(new QLabel(QStringLiteral(
            "回す軸は、いまの作図面の法線です。作図面の上で並びます。"), this));
    } else {
        spanKind_ = new QComboBox(this);
        spanKind_->addItem(QStringLiteral("1つ分の間隔"));
        spanKind_->addItem(QStringLiteral("端から端まで"));
        spanKind_->setCurrentIndex(initial.spanIsTotal ? 1 : 0);
        spanKind_->setToolTip(QStringLiteral(
            "「端から端まで」にすると、長さを個数-1 で割った間隔になります。"
            "窓割りは端から端までで決まることが多いので、手で割らずに済みます。"));
        form->addRow(QStringLiteral("下の値の意味"), spanKind_);
        stepX_ = MakeLength(this, initial.step.x);
        stepY_ = MakeLength(this, initial.step.y);
        stepZ_ = MakeLength(this, initial.step.z);
        form->addRow(QStringLiteral("X"), stepX_);
        form->addRow(QStringLiteral("Y"), stepY_);
        form->addRow(QStringLiteral("Z"), stepZ_);
    }

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

V2ArrayChoice V2ArrayDialog::Choice() const
{
    V2ArrayChoice choice;
    choice.count = count_->value();
    if (circular_) {
        choice.totalAngleDeg = angle_->value();
        choice.center = kachakacha::v2::geometry::Vector3{centerX_->value(),
            centerY_->value(), centerZ_->value()};
        return choice;
    }
    choice.spanIsTotal = spanKind_->currentIndex() == 1;
    choice.step = kachakacha::v2::geometry::Vector3{stepX_->value(), stepY_->value(),
        stepZ_->value()};
    return choice;
}
