#include "V2GridDock.h"

#include "kachakacha/geometry/Expression.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace {

//! 副点の並び(V1 と同じ): 主点のみ / 1/2 / 1/3 / 1/4。
constexpr int kSubdivisions[4] = {0, 2, 3, 4};

} // namespace

V2GridDock::V2GridDock(QWidget* parent)
    : QDockWidget(QStringLiteral("グリッド"), parent)
{
    setObjectName(QStringLiteral("gridDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(3);

    visible_ = new QCheckBox(QStringLiteral("グリッドを表示"), body);
    visible_->setChecked(true);
    form->addRow(visible_);

    spacing_ = new QLineEdit(QStringLiteral("10"), body);
    spacing_->setToolTip(QStringLiteral("数値または式(例: 25.4/8)。mm。"));
    form->addRow(QStringLiteral("主点間隔"), spacing_);
    spacingValue_ = new QLabel(QStringLiteral("= 10 mm"), body);
    form->addRow(QStringLiteral(""), spacingValue_);

    subdivision_ = new QComboBox(body);
    subdivision_->addItem(QStringLiteral("主点のみ"));
    subdivision_->addItem(QStringLiteral("1/2"));
    subdivision_->addItem(QStringLiteral("1/3"));
    subdivision_->addItem(QStringLiteral("1/4"));
    form->addRow(QStringLiteral("副点"), subdivision_);

    originU_ = new QDoubleSpinBox(body);
    originU_->setRange(-100000.0, 100000.0);
    originU_->setDecimals(3);
    originU_->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("基準 X(u)"), originU_);
    originV_ = new QDoubleSpinBox(body);
    originV_->setRange(-100000.0, 100000.0);
    originV_->setDecimals(3);
    originV_->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("基準 Y(v)"), originV_);

    auto* originButtons = new QWidget(body);
    auto* originLayout = new QHBoxLayout(originButtons);
    originLayout->setContentsMargins(0, 0, 0, 0);
    resetOrigin_ = new QPushButton(QStringLiteral("基準を 0,0 に戻す"), originButtons);
    pickOrigin_ = new QPushButton(QStringLiteral("原点を画面で指す"), originButtons);
    originLayout->addWidget(resetOrigin_);
    originLayout->addWidget(pickOrigin_);
    form->addRow(originButtons);

    allModes_ = new QCheckBox(QStringLiteral("作図モード以外でも表示"), body);
    allModes_->setChecked(true);
    form->addRow(allModes_);
    dimOffPlane_ = new QCheckBox(QStringLiteral("作図面以外の線を常に薄く"), body);
    dimOffPlane_->setChecked(true);
    form->addRow(dimOffPlane_);

    majorColor_ = new QPushButton(QStringLiteral("主点色"), body);
    minorColor_ = new QPushButton(QStringLiteral("副点色"), body);
    backgroundColor_ = new QPushButton(QStringLiteral("背景色"), body);
    form->addRow(QStringLiteral("主点色"), majorColor_);
    form->addRow(QStringLiteral("副点色"), minorColor_);
    form->addRow(QStringLiteral("背景色"), backgroundColor_);
    layout->addLayout(form);

    message_ = new QLabel(body);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    layout->addStretch(1);
    // 棚の中身は巻物にする。欄が多い棚の最小幅で右の棚全体が広がり、
    // 画面(作図の場所)が狭くなって入力列が画面の外へ寄っていた。
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);

    QObject::connect(visible_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(spacing_, &QLineEdit::textChanged, this,
        [this](const QString& text) { (void)ApplySpacingExpression(text); });
    QObject::connect(subdivision_, &QComboBox::currentIndexChanged, this, [this] { Emit(); });
    QObject::connect(originU_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(originV_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(resetOrigin_, &QPushButton::clicked, this, [this] {
        loading_ = true;
        originU_->setValue(0.0);
        originV_->setValue(0.0);
        loading_ = false;
        Emit();
    });
    QObject::connect(pickOrigin_, &QPushButton::clicked, this, [this] {
        if (pickOriginHandler_) {
            pickOriginHandler_();
        }
    });
    QObject::connect(allModes_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(dimOffPlane_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(majorColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(majorColor_, major_, QStringLiteral("主点色")); });
    QObject::connect(minorColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(minorColor_, minor_, QStringLiteral("副点色")); });
    QObject::connect(backgroundColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(backgroundColor_, background_, QStringLiteral("背景色")); });
}

bool V2GridDock::ApplySpacingExpression(const QString& expression)
{
    // 式は core が読む。読めなければ、その場で理由を出して当てない(黙って前の値にしない)。
    const auto evaluated = kachakacha::v2::geometry::EvaluateExpression(
        expression.toStdString(), kachakacha::v2::geometry::QuantityKind::Length);
    if (!evaluated.HasValue()) {
        message_->setText(QStringLiteral("主点間隔: %1")
                .arg(QString::fromStdString(evaluated.Diagnostics().front().summaryJa)));
        return false;
    }
    if (!(evaluated.Value().value > 0.0)) {
        message_->setText(QStringLiteral("主点間隔: 0 より大きい数にしてください。"));
        return false;
    }
    spacingMm_ = evaluated.Value().value;
    spacingValue_->setText(QStringLiteral("= %1 mm").arg(spacingMm_, 0, 'f', 3));
    message_->setText(QString());
    Emit();
    return true;
}

void V2GridDock::Apply()
{
    if (applyHandler_) {
        applyHandler_(Choice());
    }
}

void V2GridDock::Emit()
{
    if (!loading_ && applyHandler_) {
        applyHandler_(Choice());
    }
}

V2GridChoice V2GridDock::Choice() const
{
    V2GridChoice choice;
    choice.grid.visible = visible_->isChecked();
    choice.grid.majorSpacingMm = spacingMm_;
    const int index = subdivision_->currentIndex();
    choice.grid.subdivision = index >= 0 && index < 4 ? kSubdivisions[index] : 0;
    choice.grid.originUmm = originU_->value();
    choice.grid.originVmm = originV_->value();
    choice.spacingExpression = spacing_->text();
    choice.showInAllModes = allModes_->isChecked();
    choice.dimOffPlaneLines = dimOffPlane_->isChecked();
    choice.majorColor = major_;
    choice.minorColor = minor_;
    choice.backgroundColor = background_;
    return choice;
}

void V2GridDock::SetChoice(const V2GridChoice& choice)
{
    loading_ = true;
    visible_->setChecked(choice.grid.visible);
    spacingMm_ = choice.grid.majorSpacingMm;
    spacing_->setText(choice.spacingExpression.isEmpty()
            ? QString::number(choice.grid.majorSpacingMm)
            : choice.spacingExpression);
    spacingValue_->setText(QStringLiteral("= %1 mm").arg(spacingMm_, 0, 'f', 3));
    for (int index = 0; index < 4; ++index) {
        if (kSubdivisions[index] == choice.grid.subdivision) {
            subdivision_->setCurrentIndex(index);
        }
    }
    originU_->setValue(choice.grid.originUmm);
    originV_->setValue(choice.grid.originVmm);
    allModes_->setChecked(choice.showInAllModes);
    dimOffPlane_->setChecked(choice.dimOffPlaneLines);
    major_ = choice.majorColor;
    minor_ = choice.minorColor;
    background_ = choice.backgroundColor;
    PaintButton(majorColor_, major_);
    PaintButton(minorColor_, minor_);
    PaintButton(backgroundColor_, background_);
    loading_ = false;
}

void V2GridDock::SetApplyHandler(std::function<void(const V2GridChoice&)> handler)
{
    applyHandler_ = std::move(handler);
}

void V2GridDock::SetPickOriginHandler(std::function<void()> handler)
{
    pickOriginHandler_ = std::move(handler);
}

void V2GridDock::SetColorChooser(
    std::function<QColor(const QColor& initial, const QString& title)> chooser)
{
    colorChooser_ = std::move(chooser);
}

QString V2GridDock::MessageText() const
{
    return message_->text();
}

void V2GridDock::ChooseColor(QPushButton* button, QColor& color, const QString& title)
{
    const QColor chosen = colorChooser_ ? colorChooser_(color, title)
                                       : QColorDialog::getColor(color, this, title);
    if (!chosen.isValid()) {
        return;   // やめた。
    }
    color = chosen;
    PaintButton(button, color);
    Emit();
}

void V2GridDock::PaintButton(QPushButton* button, const QColor& color)
{
    if (!color.isValid()) {
        return;
    }
    // ボタンそのものを色にする。文字は明るさで白黒を選ぶ。
    const bool dark = (color.red() + color.green() + color.blue()) < 3 * 128;
    button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
            .arg(color.name(), dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000")));
}
