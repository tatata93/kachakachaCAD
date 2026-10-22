#include "V2NumericSpinBox.h"
#include "V2GridDock.h"

#include "kachakacha/geometry/Expression.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QVariant>

#include <string>

namespace {

//! 副点の並び(V1 と同じ): 主点のみ / 1/2 / 1/3 / 1/4。
constexpr int kSubdivisions[4] = {0, 2, 3, 4};
QWidget* PairRow(QWidget* parent, QWidget* first, QWidget* second)
{
    auto* row=new QWidget(parent);auto* layout=new QHBoxLayout(row);
    layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
    layout->addWidget(first);layout->addWidget(second);return row;
}

} // namespace

V2GridDock::V2GridDock(QWidget* parent)
    : QDockWidget(QStringLiteral("グリッド"), parent)
{
    setObjectName(QStringLiteral("gridDock"));
    setFeatures(QDockWidget::NoDockWidgetFeatures);
    setAllowedAreas(Qt::RightDockWidgetArea);
    setMinimumWidth(280);
    setMinimumHeight(235);
    setMaximumHeight(285);
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(3);

    visible_ = new QCheckBox(QStringLiteral("グリッドを表示"), body);
    visible_->setObjectName(QStringLiteral("gridVisibility"));
    visible_->setToolTip(QStringLiteral("表示だけを切り替えます。グリッドへのスナップはスナップボタンで切り替えます。"));
    visible_->setChecked(true);
    form->addRow(visible_);

    spacing_ = new QLineEdit(QStringLiteral("10"), body);
    spacing_->setObjectName(QStringLiteral("gridSpacing"));
    spacing_->setToolTip(QStringLiteral("数値または式(例: 25.4/8)。mm。"));
    spacingValue_ = new QLabel(QStringLiteral("= 10 mm"), body);
    form->addRow(QStringLiteral("主点間隔"), PairRow(body,spacing_,spacingValue_));

    subdivision_ = new QComboBox(body);
    subdivision_->setObjectName(QStringLiteral("gridSubdivision"));
    subdivision_->addItem(QStringLiteral("主点のみ"));
    subdivision_->addItem(QStringLiteral("1/2"));
    subdivision_->addItem(QStringLiteral("1/3"));
    subdivision_->addItem(QStringLiteral("1/4"));
    form->addRow(QStringLiteral("副点"), subdivision_);

    originU_ = new V2NumericSpinBox(body);
    originU_->setObjectName(QStringLiteral("gridOriginU"));
    originU_->setRange(-100000.0, 100000.0);
    originU_->setDecimals(3);
    originU_->setSuffix(QStringLiteral(" mm"));
    originU_->setToolTip(QStringLiteral("グリッド基準 X(u)"));
    originV_ = new V2NumericSpinBox(body);
    originV_->setObjectName(QStringLiteral("gridOriginV"));
    originV_->setRange(-100000.0, 100000.0);
    originV_->setDecimals(3);
    originV_->setSuffix(QStringLiteral(" mm"));
    originV_->setToolTip(QStringLiteral("グリッド基準 Y(v)"));
    form->addRow(QStringLiteral("基準 X / Y"), PairRow(body,originU_,originV_));

    auto* originButtons = new QWidget(body);
    auto* originLayout = new QHBoxLayout(originButtons);
    originLayout->setContentsMargins(0, 0, 0, 0);
    resetOrigin_ = new QPushButton(QStringLiteral("基準を 0,0 に戻す"), originButtons);
    pickOrigin_ = new QPushButton(QStringLiteral("原点を画面で指す"), originButtons);
    originLayout->addWidget(resetOrigin_);
    originLayout->addWidget(pickOrigin_);
    form->addRow(originButtons);

    layout->addLayout(form);
    auto* detailsButton = new QToolButton(body);
    detailsButton->setObjectName(QStringLiteral("gridDetails"));
    detailsButton->setText(QStringLiteral("詳細設定（表示・色）"));
    detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailsButton->setArrowType(Qt::RightArrow);
    detailsButton->setCheckable(true);
    layout->addWidget(detailsButton);
    auto* details = new QWidget(body);
    details->setObjectName(QStringLiteral("gridDetailsBody"));
    details->hide();
    form = new QFormLayout(details);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(3);
    layout->addWidget(details);
    QObject::connect(detailsButton, &QToolButton::toggled, details,
        [details, detailsButton](bool on) {
            details->setVisible(on);
            detailsButton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });

    BuildDisplayFields(form, body);

    message_ = new QLabel(body);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    layout->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);
    ConnectSignals();
}

void V2GridDock::BuildDisplayFields(QFormLayout* form, QWidget* body)
{
    allModes_ = new QCheckBox(QStringLiteral("作図モード以外でも表示"), body);
    allModes_->setObjectName(QStringLiteral("gridAllModes"));
    allModes_->setChecked(true);
    form->addRow(allModes_);
    dimOffPlane_ = new QCheckBox(QStringLiteral("新規作図中だけ作図面以外の線を薄く"), body);
    dimOffPlane_->setChecked(true);
    form->addRow(dimOffPlane_);
    workPlaneVisible_ = new QCheckBox(QStringLiteral("作図面を出す"), body);
    workPlaneVisible_->setChecked(true);
    workPlaneVisible_->setToolTip(QStringLiteral(
        "文書にある作図面を、薄い四角と破線の枠で出します。どこに描いているかが読めます。"));
    form->addRow(workPlaneVisible_);
    shapesVisible_ = new QCheckBox(QStringLiteral("立体・面を出す"), body);
    shapesVisible_->setChecked(true);
    shapesVisible_->setToolTip(QStringLiteral(
        "押し出しや形状ガイドで作った形を塗って出します。外すと線だけになります。"));
    form->addRow(shapesVisible_);
    surfaceGrid_=new QComboBox(body);surfaceGrid_->setObjectName("surfaceGridMode");
    surfaceGrid_->addItems({QStringLiteral("表示しない"),QStringLiteral("選択した面だけ"),QStringLiteral("すべての面")});
    surfaceGrid_->setCurrentIndex(1);
    surfaceGrid_->setToolTip(QStringLiteral("形状確認用のU/V線です。面の分割や編集用ワイヤーではありません。"));
    form->addRow(QStringLiteral("面の形状確認線"),surfaceGrid_);

    majorColor_ = new QPushButton(QStringLiteral("主点色"), body);
    minorColor_ = new QPushButton(QStringLiteral("副点色"), body);
    backgroundColor_ = new QPushButton(QStringLiteral("背景色"), body);
    form->addRow(QStringLiteral("主点色"), majorColor_);
    form->addRow(QStringLiteral("副点色"), minorColor_);
    form->addRow(QStringLiteral("背景色"), backgroundColor_);

}

void V2GridDock::ConnectSignals()
{
    QObject::connect(visible_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(spacing_, &QLineEdit::editingFinished, this,
        [this] { if(!loading_)(void)ApplySpacingExpression(spacing_->text()); });
    QObject::connect(subdivision_, &QComboBox::currentIndexChanged, this, [this] { Emit(); });
    QObject::connect(originU_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if(!loading_)originU_->setProperty("exactGridOrigin",value); Emit();
    });
    QObject::connect(originV_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if(!loading_)originV_->setProperty("exactGridOrigin",value); Emit();
    });
    QObject::connect(resetOrigin_, &QPushButton::clicked, this, [this] {
        loading_ = true;
        originU_->setValue(0.0);
        originV_->setValue(0.0);
        originU_->setProperty("exactGridOrigin",0.0);
        originV_->setProperty("exactGridOrigin",0.0);
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
    QObject::connect(workPlaneVisible_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(surfaceGrid_, &QComboBox::currentIndexChanged, this, [this] { Emit(); });
    QObject::connect(shapesVisible_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(majorColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(majorColor_, major_, QStringLiteral("主点色")); });
    QObject::connect(minorColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(minorColor_, minor_, QStringLiteral("副点色")); });
    QObject::connect(backgroundColor_, &QPushButton::clicked, this,
        [this] { ChooseColor(backgroundColor_, background_, QStringLiteral("背景色")); });
}

bool V2GridDock::ApplySpacingExpression(const QString& expression)
{
    if (spacing_->text() != expression) spacing_->setText(expression);
    // 式は core が読む。読めなければ、その場で理由を出して当てない(黙って前の値にしない)。
    const auto evaluated = kachakacha::v2::geometry::EvaluateExpression(
        expression.toStdString(), kachakacha::v2::geometry::QuantityKind::Length);
    if (!evaluated.HasValue()) {
        message_->setText(QStringLiteral("主点間隔: %1")
                .arg(QString::fromStdString(evaluated.FirstSummaryJa())));
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
    choice.grid.originUmm = originU_->property("exactGridOrigin").toDouble();
    choice.grid.originVmm = originV_->property("exactGridOrigin").toDouble();
    choice.spacingExpression = spacing_->text();
    choice.showInAllModes = allModes_->isChecked();
    choice.dimOffPlaneLines = dimOffPlane_->isChecked();
    choice.workPlaneVisible = workPlaneVisible_->isChecked();
    choice.shapesVisible = shapesVisible_->isChecked();
    choice.surfaceGridMode=surfaceGrid_->currentIndex();
    choice.majorColor = major_;
    choice.minorColor = minor_;
    choice.backgroundColor = background_;
    return choice;
}

void V2GridDock::SetChoice(const V2GridChoice& choice)
{
    loading_ = true;
    visible_->setChecked(choice.grid.visible);
    const bool sameSpacing = spacingMm_ == choice.grid.majorSpacingMm;
    spacingMm_ = choice.grid.majorSpacingMm;
    // View-only sync must preserve the expression and caret, including unfinished input.
    if (!sameSpacing) spacing_->setText(choice.spacingExpression.isEmpty()
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
    originU_->setProperty("exactGridOrigin",choice.grid.originUmm);
    originV_->setProperty("exactGridOrigin",choice.grid.originVmm);
    allModes_->setChecked(choice.showInAllModes);
    dimOffPlane_->setChecked(choice.dimOffPlaneLines);
    workPlaneVisible_->setChecked(choice.workPlaneVisible);
    shapesVisible_->setChecked(choice.shapesVisible);
    surfaceGrid_->setCurrentIndex(choice.surfaceGridMode);
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
