#include "V2FabricationDock.h"

#include <QCheckBox>
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

using kachakacha::v2::app::FabricationChoice;
using kachakacha::v2::app::FabricationMethod;
using kachakacha::v2::app::ParameterId;
using kachakacha::v2::fabrication::FreezeOutput;

namespace {

//! 分割軸の欄の並び: 自動 / U / V。値は 2 / 0 / 1。
constexpr int kSplitAxisValues[3] = {2, 0, 1};

[[nodiscard]] QDoubleSpinBox* MakeCount(QWidget* parent, double minimum, double maximum)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setRange(minimum, maximum);
    field->setDecimals(0);
    field->setSingleStep(1.0);
    return field;
}

[[nodiscard]] QDoubleSpinBox* MakeMm(QWidget* parent, double minimum, double maximum, double step)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setRange(minimum, maximum);
    field->setDecimals(3);
    field->setSingleStep(step);
    field->setSuffix(QStringLiteral(" mm"));
    return field;
}

[[nodiscard]] QPushButton* MakeRun(QWidget* parent, const QString& text, const char* command,
    V2FabricationDock* dock)
{
    auto* button = new QPushButton(text, parent);
    QObject::connect(button, &QPushButton::clicked, dock, [dock, command] { dock->PressRun(command); });
    return button;
}

} // namespace

V2FabricationDock::V2FabricationDock(QWidget* parent)
    : QDockWidget(QStringLiteral("製作"), parent)
{
    setObjectName(QStringLiteral("fabricationDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    model_ = new QLabel(QStringLiteral("近似モデル: (なし)"), body);
    model_->setWordWrap(true);
    layout->addWidget(model_);

    layout->addWidget(BuildOptionsForm(body));
    layout->addWidget(BuildRangeAndMaterial(body));

    auto* buttons = new QWidget(body);
    auto* buttonLayout = new QVBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("製作モデルを作る"), "fabrication.create", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("プレビュー更新"), "fabrication.preview_update", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("境界の役割(開口 / 折り線)"), "fabrication.assign_role", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("切れ目にする(開いた線)"), "fabrication.assign_relief_cut", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("接続スコープ"), "fabrication.set_connection_scope", this));
    layout->addWidget(buttons);

    layout->addWidget(BuildBendSection(body));
    auto* freezeButtons = new QWidget(body);
    auto* freezeLayout = new QVBoxLayout(freezeButtons);
    freezeLayout->setContentsMargins(0, 0, 0, 0);
    freezeLayout->setSpacing(2);
    freezeLayout->addWidget(MakeRun(freezeButtons, QStringLiteral("現在状態を固定"), "fabrication.freeze_state", this));
    freezeLayout->addWidget(MakeRun(freezeButtons, QStringLiteral("型紙を作る"), "fabrication.create_pattern", this));
    layout->addWidget(freezeButtons);

    message_ = new QLabel(body);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    layout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);
    Connect();
    RefreshMethodRows();
}

QWidget* V2FabricationDock::BuildOptionsForm(QWidget* body)
{
    auto* formWidget = new QWidget(body);
    form_ = new QFormLayout(formWidget);
    form_->setContentsMargins(0, 0, 0, 0);
    method_ = new QComboBox(formWidget);
    method_->addItem(QStringLiteral("V1方式(帯へ近似し直す)"));
    method_->addItem(QStringLiteral("V2方式(面を分類して展開)"));
    form_->addRow(QStringLiteral("方式"), method_);
    splitAxis_ = new QComboBox(formWidget);
    splitAxis_->addItem(QStringLiteral("自動(曲がっている方向を横切る)"));
    splitAxis_->addItem(QStringLiteral("U 方向で切る"));
    splitAxis_->addItem(QStringLiteral("V 方向で切る"));
    form_->addRow(QStringLiteral("分割軸"), splitAxis_);
    automatic_ = new QCheckBox(QStringLiteral("許すずれから自動で切る"), formWidget);
    automatic_->setChecked(true);
    form_->addRow(QStringLiteral("境界"), automatic_);
    manual_ = new QLineEdit(formWidget);
    manual_->setPlaceholderText(QStringLiteral("0.3, 0.6(分割軸の 0〜1)"));
    form_->addRow(QStringLiteral("手動境界"), manual_);
    maxParts_ = MakeCount(formWidget, 1.0, 200.0);
    maxParts_->setValue(12.0);
    form_->addRow(QStringLiteral("部材数の上限"), maxParts_);
    minWidth_ = MakeMm(formWidget, 0.0, 1000.0, 0.5);
    minWidth_->setValue(4.0);
    form_->addRow(QStringLiteral("部材の最小幅"), minWidth_);
    fidelity_ = MakeCount(formWidget, 1.0, 20.0);
    fidelity_->setValue(6.0);
    form_->addRow(QStringLiteral("再現度"), fidelity_);
    thickness_ = MakeMm(formWidget, 0.0, 100.0, 0.1);
    thickness_->setToolTip(QStringLiteral("数の棚の「板厚」と同じ値です。"));
    form_->addRow(QStringLiteral("板厚"), thickness_);
    deviation_ = MakeMm(formWidget, 0.0, 100.0, 0.05);
    deviation_->setToolTip(QStringLiteral("数の棚の「展開で許すずれ」と同じ値です。"));
    form_->addRow(QStringLiteral("許すずれ"), deviation_);
    return formWidget;

}

QWidget* V2FabricationDock::BuildRangeAndMaterial(QWidget* body)
{
    // 範囲(V1 の plate_range)と材料・積層(plate の材料、plate_laminate)。
    auto* widget = new QWidget(body);
    auto* form = new QFormLayout(widget);
    form->setContentsMargins(0, 0, 0, 0);
    const auto makeUnit = [widget](double value) {
        auto* field = new QDoubleSpinBox(widget);
        field->setRange(0.0, 1.0);
        field->setDecimals(3);
        field->setSingleStep(0.05);
        field->setValue(value);
        return field;
    };
    auto* uRow = new QWidget(widget);
    auto* uLayout = new QHBoxLayout(uRow);
    uLayout->setContentsMargins(0, 0, 0, 0);
    rangeUMin_ = makeUnit(0.0);
    rangeUMax_ = makeUnit(1.0);
    uLayout->addWidget(rangeUMin_);
    uLayout->addWidget(new QLabel(QStringLiteral("〜"), uRow));
    uLayout->addWidget(rangeUMax_);
    form->addRow(QStringLiteral("範囲 u"), uRow);
    auto* vRow = new QWidget(widget);
    auto* vLayout = new QHBoxLayout(vRow);
    vLayout->setContentsMargins(0, 0, 0, 0);
    rangeVMin_ = makeUnit(0.0);
    rangeVMax_ = makeUnit(1.0);
    vLayout->addWidget(rangeVMin_);
    vLayout->addWidget(new QLabel(QStringLiteral("〜"), vRow));
    vLayout->addWidget(rangeVMax_);
    form->addRow(QStringLiteral("範囲 v"), vRow);

    material_ = new QLineEdit(widget);
    material_->setPlaceholderText(QStringLiteral("プラ板 0.5 など"));
    form->addRow(QStringLiteral("材料"), material_);
    layers_ = MakeCount(widget, 1.0, 20.0);
    layers_->setValue(1.0);
    form->addRow(QStringLiteral("積層の枚数"), layers_);
    applyMaterial_ = new QPushButton(QStringLiteral("材料と積層を選んだものに当てる"), widget);
    form->addRow(applyMaterial_);
    return widget;
}

QWidget* V2FabricationDock::BuildBendSection(QWidget* body)
{
    auto* bendWidget = new QWidget(body);
    auto* bend = new QFormLayout(bendWidget);
    bend->setContentsMargins(0, 0, 0, 0);
    auto* assemblyRow = new QWidget(bendWidget);
    auto* assemblyLayout = new QHBoxLayout(assemblyRow);
    assemblyLayout->setContentsMargins(0, 0, 0, 0);
    assembly_ = new QDoubleSpinBox(assemblyRow);
    assembly_->setRange(0.0, 100.0);
    assembly_->setDecimals(1);
    assembly_->setSingleStep(5.0);
    assembly_->setSuffix(QStringLiteral(" %"));
    assembly_->setValue(100.0);
    applyAssembly_ = new QPushButton(QStringLiteral("当てる"), assemblyRow);
    assemblyLayout->addWidget(assembly_);
    assemblyLayout->addWidget(applyAssembly_);
    bend->addRow(QStringLiteral("組立率"), assemblyRow);
    freeze_ = new QComboBox(bendWidget);
    freeze_->addItem(QStringLiteral("ワイヤーのみ"));
    freeze_->addItem(QStringLiteral("部品のみ"));
    freeze_->addItem(QStringLiteral("両方"));
    bend->addRow(QStringLiteral("固定で作るもの"), freeze_);
    return bendWidget;
}

void V2FabricationDock::Connect()
{
    QObject::connect(method_, &QComboBox::currentIndexChanged, this, [this] {
        RefreshMethodRows();
        Emit();
    });
    QObject::connect(splitAxis_, &QComboBox::currentIndexChanged, this, [this] { Emit(); });
    QObject::connect(automatic_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(manual_, &QLineEdit::textChanged, this, [this] { Emit(); });
    QObject::connect(maxParts_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(minWidth_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(fidelity_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && parameterHandler_) {
            parameterHandler_(ParameterId::ExtrudeDistance, value);
        }
    });
    QObject::connect(deviation_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && parameterHandler_) {
            parameterHandler_(ParameterId::MaxDeviationMm, value);
        }
    });
    QObject::connect(rangeUMin_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(rangeUMax_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(rangeVMin_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(rangeVMax_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(applyMaterial_, &QPushButton::clicked, this, [this] { PressApplyMaterial(); });
    QObject::connect(applyAssembly_, &QPushButton::clicked, this, [this] { PressApplyAssembly(); });
    QObject::connect(freeze_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_ && freezeHandler_) {
            freezeHandler_(FreezeOutputChoice());
        }
    });
}

void V2FabricationDock::RefreshMethodRows()
{
    // 分割軸・境界・上限・最小幅は V1 方式(帯)だけが使う。V2 方式では隠す。
    const bool band = method_->currentIndex() == 0;
    form_->setRowVisible(splitAxis_, band);
    form_->setRowVisible(automatic_, band);
    form_->setRowVisible(manual_, band);
    form_->setRowVisible(maxParts_, band);
    form_->setRowVisible(minWidth_, band);
}

void V2FabricationDock::Emit()
{
    if (!loading_ && choiceChanged_) {
        choiceChanged_();
    }
}

FabricationChoice V2FabricationDock::Choice() const
{
    FabricationChoice choice;
    choice.method = method_->currentIndex() == 0 ? FabricationMethod::BandApproximation
                                                 : FabricationMethod::ClassifyFaces;
    const int axisIndex = splitAxis_->currentIndex();
    choice.splitAxis = axisIndex >= 0 && axisIndex < 3 ? kSplitAxisValues[axisIndex] : 2;
    choice.automaticBoundaries = automatic_->isChecked();
    choice.maximumPartCount = static_cast<int>(maxParts_->value());
    choice.minimumPartWidthMm = minWidth_->value();
    choice.fidelity = static_cast<int>(fidelity_->value());
    choice.rangeUMin = rangeUMin_->value();
    choice.rangeUMax = rangeUMax_->value();
    choice.rangeVMin = rangeVMin_->value();
    choice.rangeVMax = rangeVMax_->value();
    const auto parsed = kachakacha::v2::app::ParseBoundaryList(manual_->text().toStdString());
    if (parsed.HasValue()) {
        choice.manualBoundaries = parsed.Value();
    }
    return choice;
}

bool V2FabricationDock::ManualBoundariesReadable(QString* error) const
{
    const auto parsed = kachakacha::v2::app::ParseBoundaryList(manual_->text().toStdString());
    if (parsed.HasValue()) {
        return true;
    }
    if (error != nullptr) {
        *error = QString::fromStdString(parsed.Diagnostics().front().code + " "
            + parsed.Diagnostics().front().summaryJa + " " + parsed.Diagnostics().front().detailsJa);
    }
    return false;
}

void V2FabricationDock::SetChoice(const FabricationChoice& choice)
{
    loading_ = true;
    method_->setCurrentIndex(choice.method == FabricationMethod::BandApproximation ? 0 : 1);
    for (int index = 0; index < 3; ++index) {
        if (kSplitAxisValues[index] == choice.splitAxis) {
            splitAxis_->setCurrentIndex(index);
        }
    }
    automatic_->setChecked(choice.automaticBoundaries);
    manual_->setText(QString::fromStdString(
        kachakacha::v2::app::FormatBoundaryList(choice.manualBoundaries)));
    maxParts_->setValue(static_cast<double>(choice.maximumPartCount));
    minWidth_->setValue(choice.minimumPartWidthMm);
    fidelity_->setValue(static_cast<double>(choice.fidelity));
    rangeUMin_->setValue(choice.rangeUMin);
    rangeUMax_->setValue(choice.rangeUMax);
    rangeVMin_->setValue(choice.rangeVMin);
    rangeVMax_->setValue(choice.rangeVMax);
    loading_ = false;
    RefreshMethodRows();
}

void V2FabricationDock::SetMaterialHandler(
    std::function<void(const QString& material, int layers)> handler)
{
    materialHandler_ = std::move(handler);
}

void V2FabricationDock::SetMaterial(const QString& material, int layers)
{
    material_->setText(material);
    layers_->setValue(static_cast<double>(layers));
}

QString V2FabricationDock::MaterialName() const
{
    return material_->text();
}

int V2FabricationDock::LayerCount() const
{
    return static_cast<int>(layers_->value());
}

void V2FabricationDock::PressApplyMaterial()
{
    if (materialHandler_) {
        materialHandler_(material_->text(), LayerCount());
    }
}

void V2FabricationDock::SetRange(double uMin, double uMax, double vMin, double vMax)
{
    rangeUMin_->setValue(uMin);
    rangeUMax_->setValue(uMax);
    rangeVMin_->setValue(vMin);
    rangeVMax_->setValue(vMax);
}

void V2FabricationDock::SetChoiceChangedHandler(std::function<void()> handler)
{
    choiceChanged_ = std::move(handler);
}

void V2FabricationDock::SetParameterMm(ParameterId id, double value)
{
    loading_ = true;
    if (id == ParameterId::ExtrudeDistance) {
        thickness_->setValue(value);
    } else if (id == ParameterId::MaxDeviationMm) {
        deviation_->setValue(value);
    }
    loading_ = false;
}

void V2FabricationDock::SetParameterHandler(std::function<void(ParameterId, double)> handler)
{
    parameterHandler_ = std::move(handler);
}

void V2FabricationDock::SetAssemblyPercent(double percent)
{
    loading_ = true;
    assembly_->setValue(percent);
    loading_ = false;
}

double V2FabricationDock::AssemblyPercent() const
{
    return assembly_->value();
}

void V2FabricationDock::SetAssemblyHandler(std::function<void(double)> handler)
{
    assemblyHandler_ = std::move(handler);
}

void V2FabricationDock::PressApplyAssembly()
{
    if (assemblyHandler_) {
        assemblyHandler_(assembly_->value());
    }
}

FreezeOutput V2FabricationDock::FreezeOutputChoice() const
{
    switch (freeze_->currentIndex()) {
    case 1:  return FreezeOutput::PartsOnly;
    case 2:  return FreezeOutput::Both;
    default: return FreezeOutput::WiresOnly;
    }
}

void V2FabricationDock::SetFreezeOutput(kachakacha::v2::fabrication::FreezeOutput value)
{
    loading_ = true;
    freeze_->setCurrentIndex(value == FreezeOutput::PartsOnly ? 1
            : value == FreezeOutput::Both                     ? 2
                                                              : 0);
    loading_ = false;
}

void V2FabricationDock::SetFreezeOutputHandler(
    std::function<void(kachakacha::v2::fabrication::FreezeOutput)> handler)
{
    freezeHandler_ = std::move(handler);
}

void V2FabricationDock::SetRunHandler(std::function<void(const char*)> handler)
{
    runHandler_ = std::move(handler);
}

void V2FabricationDock::PressRun(const char* command)
{
    if (runHandler_) {
        runHandler_(command);
    }
}

void V2FabricationDock::SetMessage(const QString& text)
{
    message_->setText(text);
}

QString V2FabricationDock::MessageText() const
{
    return message_->text();
}

void V2FabricationDock::SetModelText(const QString& text)
{
    model_->setText(text);
}

void V2FabricationDock::SetManualBoundariesText(const QString& text)
{
    manual_->setText(text);
}

void V2FabricationDock::SetAutomaticBoundaries(bool automatic)
{
    automatic_->setChecked(automatic);
}

void V2FabricationDock::SetMaximumPartCount(int count)
{
    maxParts_->setValue(static_cast<double>(count));
}

void V2FabricationDock::SetSplitAxisIndex(int index)
{
    splitAxis_->setCurrentIndex(index);
}
