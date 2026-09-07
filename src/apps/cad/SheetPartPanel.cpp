#include "SheetPartPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

using kachakacha::model::PlateThicknessDirection;

SheetPartPanel::SheetPartPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sheetPartPanel"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(5);

    auto* group = new QGroupBox(QStringLiteral("選択中の面部品"));
    group->setProperty("manualAnchor", QStringLiteral("sheetPart"));
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 5, 8, 8);
    layout->setSpacing(5);

    selectionLabel_ = new QLabel(QStringLiteral("面部品を選択してください"));
    selectionLabel_->setObjectName(QStringLiteral("sheetPartSelectionLabel"));
    selectionLabel_->setWordWrap(true);
    selectionLabel_->setStyleSheet(QStringLiteral("font-weight: 600; color: #26323a;"));
    layout->addWidget(selectionLabel_);

    stateLabel_ = new QLabel(QStringLiteral(
        "形状を作った後、ここで材料と厚みを設定すると製作用の部品になります。"));
    stateLabel_->setObjectName(QStringLiteral("sheetPartStateLabel"));
    stateLabel_->setWordWrap(true);
    stateLabel_->setStyleSheet(QStringLiteral("color: #4f5b63;"));
    layout->addWidget(stateLabel_);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    material_ = new QComboBox;
    material_->setObjectName(QStringLiteral("sheetPartMaterial"));
    material_->addItem(QStringLiteral("プラ板"), QStringLiteral("styrene"));
    material_->addItem(QStringLiteral("紙・厚紙"), QStringLiteral("paper"));
    material_->addItem(QStringLiteral("真鍮板"), QStringLiteral("brass"));
    material_->addItem(QStringLiteral("その他"), QStringLiteral("other"));
    startThickness_ = new QDoubleSpinBox;
    startThickness_->setObjectName(QStringLiteral("sheetPartThickness"));
    startThickness_->setRange(0.001, 1000.0);
    startThickness_->setDecimals(3);
    startThickness_->setSingleStep(0.1);
    startThickness_->setValue(0.5);
    startThickness_->setSuffix(QStringLiteral(" mm"));
    variableThickness_ = new QCheckBox(QStringLiteral("終端まで厚みを変える"));
    variableThickness_->setObjectName(QStringLiteral("sheetPartVariableThickness"));
    endThickness_ = new QDoubleSpinBox;
    endThickness_->setObjectName(QStringLiteral("sheetPartEndThickness"));
    endThickness_->setRange(0.001, 1000.0);
    endThickness_->setDecimals(3);
    endThickness_->setSingleStep(0.1);
    endThickness_->setValue(0.5);
    endThickness_->setSuffix(QStringLiteral(" mm"));
    endThickness_->setEnabled(false);
    direction_ = new QComboBox;
    direction_->setObjectName(QStringLiteral("sheetPartDirection"));
    direction_->addItem(
        QStringLiteral("+側（法線矢印側）"), static_cast<int>(PlateThicknessDirection::Positive));
    direction_->addItem(
        QStringLiteral("中央（両側へ半分）"), static_cast<int>(PlateThicknessDirection::Centered));
    direction_->addItem(
        QStringLiteral("-側（矢印と反対）"), static_cast<int>(PlateThicknessDirection::Negative));
    form->addRow(QStringLiteral("材料"), material_);
    form->addRow(QStringLiteral("厚み"), startThickness_);
    form->addRow(variableThickness_);
    form->addRow(QStringLiteral("終端の厚み"), endThickness_);
    form->addRow(QStringLiteral("厚みの向き"), direction_);
    layout->addLayout(form);

    connect(variableThickness_, &QCheckBox::toggled, endThickness_, &QWidget::setEnabled);
    connect(startThickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!variableThickness_->isChecked()) {
            endThickness_->setValue(value);
        }
    });

    applyButton_ = new QPushButton(QStringLiteral("この面に製作条件を設定"));
    applyButton_->setObjectName(QStringLiteral("primaryButton"));
    applyButton_->setToolTip(QStringLiteral(
        "選択中の面部品へ材料・厚み・向きを設定します。元の形状は保持されます"));
    connect(applyButton_, &QPushButton::clicked, this, [this] {
        if (onApply) onApply();
    });
    layout->addWidget(applyButton_);

    createFromWiresButton_ = new QPushButton(QStringLiteral("選択線から面部品を一度に作る"));
    createFromWiresButton_->setObjectName(QStringLiteral("sheetPartCreateFromWiresButton"));
    createFromWiresButton_->setToolTip(QStringLiteral(
        "閉じた輪郭、複数断面、外形ガイドの選択から形状と製作条件をまとめて作ります"));
    connect(createFromWiresButton_, &QPushButton::clicked, this, [this] {
        if (onCreateFromWires) onCreateFromWires();
    });
    layout->addWidget(createFromWiresButton_);

    outer->addWidget(group);
    ShowNoSelection(false);
}

void SheetPartPanel::ShowNoSelection(bool canCreateFromWires)
{
    selectionLabel_->setText(QStringLiteral("面部品を選択してください"));
    stateLabel_->setText(QStringLiteral(
        "形状を作った後、材料と厚みを設定すると製作用の部品になります。"));
    applyButton_->setText(QStringLiteral("面部品を選択"));
    SetEditorEnabled(false);
    createFromWiresButton_->setEnabled(canCreateFromWires);
}

void SheetPartPanel::ShowSurface(const QString& name, int manufacturingVariantCount)
{
    selectionLabel_->setText(name);
    createFromWiresButton_->setEnabled(false);
    if (manufacturingVariantCount == 0) {
        stateLabel_->setText(QStringLiteral(
            "形状のみです。材料と厚みを設定すると、製作・展開できる面部品になります。"));
        applyButton_->setText(QStringLiteral("この面に製作条件を設定"));
        SetEditorEnabled(true);
        return;
    }
    stateLabel_->setText(QStringLiteral(
        "この形状には製作条件が%1件あります。変更する部品をモデル一覧で選択してください。")
            .arg(manufacturingVariantCount));
    applyButton_->setText(QStringLiteral("製作条件の部品を選択"));
    SetEditorEnabled(false);
}

void SheetPartPanel::ShowPlate(
    const QString& name,
    double startThicknessMillimeters,
    double endThicknessMillimeters,
    PlateThicknessDirection direction,
    const QString& materialCode)
{
    selectionLabel_->setText(name);
    stateLabel_->setText(QStringLiteral(
        "製作条件あり。形状を保ったまま、材料・厚み・向きを変更できます。"));
    applyButton_->setText(QStringLiteral("製作条件を更新"));
    SetEditorEnabled(true);
    createFromWiresButton_->setEnabled(false);
    SetValues(
        startThicknessMillimeters, endThicknessMillimeters,
        std::abs(startThicknessMillimeters - endThicknessMillimeters) > 1.0e-9,
        direction, materialCode);
}

double SheetPartPanel::StartThicknessMillimeters() const
{
    return startThickness_->value();
}

double SheetPartPanel::EndThicknessMillimeters() const
{
    return variableThickness_->isChecked() ? endThickness_->value() : startThickness_->value();
}

bool SheetPartPanel::UsesVariableThickness() const
{
    return variableThickness_->isChecked();
}

PlateThicknessDirection SheetPartPanel::Direction() const
{
    return static_cast<PlateThicknessDirection>(direction_->currentData().toInt());
}

QString SheetPartPanel::MaterialCode() const
{
    return material_->currentData().toString();
}

void SheetPartPanel::SetManufacturingValuesForTest(
    double startThicknessMillimeters,
    double endThicknessMillimeters,
    bool variableThickness,
    PlateThicknessDirection direction,
    const QString& materialCode)
{
    SetValues(
        startThicknessMillimeters, endThicknessMillimeters,
        variableThickness, direction, materialCode);
}

void SheetPartPanel::SetEditorEnabled(bool enabled)
{
    material_->setEnabled(enabled);
    startThickness_->setEnabled(enabled);
    variableThickness_->setEnabled(enabled);
    endThickness_->setEnabled(enabled && variableThickness_->isChecked());
    direction_->setEnabled(enabled);
    applyButton_->setEnabled(enabled);
}

void SheetPartPanel::SetValues(
    double startThicknessMillimeters,
    double endThicknessMillimeters,
    bool variableThickness,
    PlateThicknessDirection direction,
    const QString& materialCode)
{
    const QSignalBlocker startBlocker(startThickness_);
    const QSignalBlocker variableBlocker(variableThickness_);
    const QSignalBlocker endBlocker(endThickness_);
    startThickness_->setValue(startThicknessMillimeters);
    variableThickness_->setChecked(variableThickness);
    endThickness_->setValue(endThicknessMillimeters);
    endThickness_->setEnabled(variableThickness && startThickness_->isEnabled());
    const int directionIndex = direction_->findData(static_cast<int>(direction));
    direction_->setCurrentIndex(directionIndex >= 0 ? directionIndex : 0);
    const int materialIndex = material_->findData(materialCode);
    material_->setCurrentIndex(materialIndex >= 0 ? materialIndex : material_->count() - 1);
}
