#include "V2DrawingDock.h"

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

#include <cstddef>
#include <string>
#include <string_view>

namespace {

using kachakacha::v2::app::DirectWireIsPlanar;
using kachakacha::v2::app::DirectWireKind;
using kachakacha::v2::app::DirectWireKinds;
using kachakacha::v2::app::DirectWirePointCount;
using kachakacha::v2::app::DirectWireRequest;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ArcMode;
using kachakacha::v2::modeling::ToolSettings;

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

//! 円弧の作り方の並び(V1 と同じ)。
constexpr std::array<ArcMode, 3> kArcModes{ArcMode::ThreePoints, ArcMode::EndpointsAndRadius,
    ArcMode::StartTangent};

} // namespace

V2DrawingDock::V2DrawingDock(QWidget* parent)
    : QDockWidget(QStringLiteral("作図"), parent)
{
    setObjectName(QStringLiteral("drawingDock"));
    body_ = new QWidget(this);
    auto* layout = new QVBoxLayout(body_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto* toolTitle = new QLabel(QStringLiteral("道具の決め方"), body_);
    layout->addWidget(toolTitle);
    toolForm_ = new QFormLayout();
    toolForm_->setContentsMargins(0, 0, 0, 0);
    toolForm_->setSpacing(3);
    BuildArcRows(toolForm_);
    layout->addLayout(toolForm_);

    construction_ = new QCheckBox(QStringLiteral("補助線として作図"), body_);
    layout->addWidget(construction_);
    keepPoints_ = new QCheckBox(QStringLiteral("指定した点を作図点として残す"), body_);
    layout->addWidget(keepPoints_);
    QObject::connect(construction_, &QCheckBox::toggled, this, [this] { EmitSettings(); });
    QObject::connect(keepPoints_, &QCheckBox::toggled, this, [this] { EmitSettings(); });

    auto* wireTitle = new QLabel(QStringLiteral("数値で線を作る"), body_);
    layout->addWidget(wireTitle);
    wireForm_ = new QFormLayout();
    wireForm_->setContentsMargins(0, 0, 0, 0);
    wireForm_->setSpacing(3);
    BuildDirectWireRows(wireForm_);
    layout->addLayout(wireForm_);

    createWire_ = new QPushButton(QStringLiteral("線を作る"), body_);
    QObject::connect(createWire_, &QPushButton::clicked, this, [this] { PressCreateWire(); });
    layout->addWidget(createWire_);
    message_ = new QLabel(body_);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    layout->addStretch(1);
    // 棚の中身は巻物にする。欄が多い棚の最小幅で右の棚全体が広がり、
    // 画面(作図の場所)が狭くなって入力列が画面の外へ寄っていた。
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body_);
    setWidget(scroll);
    ApplyArcVisibility();
    ApplyDirectWireVisibility();
}

void V2DrawingDock::BuildArcRows(QFormLayout* form)
{
    arcMode_ = new QComboBox(body_);
    arcMode_->addItem(QStringLiteral("3点(始点・通過点・終点)"));
    arcMode_->addItem(QStringLiteral("両端 + 半径"));
    arcMode_->addItem(QStringLiteral("始点 + 接線方向・半径・中心角"));
    form->addRow(QStringLiteral("円弧の作り方"), arcMode_);
    arcRadius_ = new QDoubleSpinBox(body_);
    arcRadius_->setRange(0.001, 100000.0);
    arcRadius_->setDecimals(3);
    arcRadius_->setSingleStep(1.0);
    arcRadius_->setSuffix(QStringLiteral(" mm"));
    arcRadius_->setValue(10.0);
    form->addRow(QStringLiteral("半径"), arcRadius_);
    arcSweep_ = new QDoubleSpinBox(body_);
    arcSweep_->setRange(-360.0, 360.0);
    arcSweep_->setDecimals(3);
    arcSweep_->setSingleStep(5.0);
    arcSweep_->setSuffix(QStringLiteral(" 度"));
    arcSweep_->setValue(90.0);
    form->addRow(QStringLiteral("中心角"), arcSweep_);
    QObject::connect(arcMode_, &QComboBox::currentIndexChanged, this, [this] {
        ApplyArcVisibility();
        EmitSettings();
    });
    QObject::connect(arcRadius_, &QDoubleSpinBox::valueChanged, this,
        [this] { EmitSettings(); });
    QObject::connect(arcSweep_, &QDoubleSpinBox::valueChanged, this,
        [this] { EmitSettings(); });
}

void V2DrawingDock::BuildDirectWireRows(QFormLayout* form)
{
    wireKind_ = new QComboBox(body_);
    for (const DirectWireKind kind : DirectWireKinds()) {
        wireKind_->addItem(Text(kachakacha::v2::app::DirectWireKindNameJa(kind)));
    }
    form->addRow(QStringLiteral("種類"), wireKind_);
    for (std::size_t index = 0; index < wirePoints_.size(); ++index) {
        wirePoints_[index] = AddVectorRow(form,
            QStringLiteral("点%1 mm").arg(static_cast<int>(index) + 1));
    }
    wireRadius_ = new QDoubleSpinBox(body_);
    // 0 も入れられる。断るのは core(UI-D002)で、欄が黙って 0.001 に寄せない。
    wireRadius_->setRange(0.0, 100000.0);
    wireRadius_->setDecimals(3);
    wireRadius_->setSingleStep(1.0);
    wireRadius_->setSuffix(QStringLiteral(" mm"));
    wireRadius_->setValue(10.0);
    form->addRow(QStringLiteral("半径"), wireRadius_);
    wireName_ = new QLineEdit(body_);
    wireName_->setPlaceholderText(QStringLiteral("空なら「数値の線」"));
    form->addRow(QStringLiteral("名前"), wireName_);
    wireConstruction_ = new QCheckBox(QStringLiteral("補助線にする"), body_);
    form->addRow(wireConstruction_);
    QObject::connect(wireKind_, &QComboBox::currentIndexChanged, this,
        [this] { ApplyDirectWireVisibility(); });
}

std::array<QDoubleSpinBox*, 3> V2DrawingDock::AddVectorRow(QFormLayout* form,
    const QString& label)
{
    auto* row = new QWidget(body_);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    std::array<QDoubleSpinBox*, 3> fields{};
    for (std::size_t index = 0; index < 3; ++index) {
        auto* field = new QDoubleSpinBox(row);
        field->setRange(-100000.0, 100000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        rowLayout->addWidget(field);
        fields[index] = field;
    }
    form->addRow(label, row);
    return fields;
}

void V2DrawingDock::ApplyArcVisibility()
{
    const int index = arcMode_->currentIndex();
    const ArcMode mode = index >= 0 && index < static_cast<int>(kArcModes.size())
        ? kArcModes[static_cast<std::size_t>(index)]
        : ArcMode::ThreePoints;
    // 3点のときは半径も中心角も使わない。出したままにすると効くのか分からない。
    toolForm_->setRowVisible(arcRadius_, mode != ArcMode::ThreePoints);
    toolForm_->setRowVisible(arcSweep_, mode == ArcMode::StartTangent);
}

void V2DrawingDock::ApplyDirectWireVisibility()
{
    const DirectWireRequest request = DirectWire();
    const int needed = DirectWirePointCount(request.kind);
    const bool planar = DirectWireIsPlanar(request.kind);
    for (std::size_t index = 0; index < wirePoints_.size(); ++index) {
        wireForm_->setRowVisible(wirePoints_[index][0]->parentWidget(),
            static_cast<int>(index) < needed);
        // 平面上は (u, v) の2つ。3つ目(z)は 3D のときだけ。
        wirePoints_[index][2]->setVisible(!planar);
    }
    wireForm_->setRowVisible(wireRadius_, request.kind == DirectWireKind::PlanarCircle);
}

void V2DrawingDock::EmitSettings()
{
    if (!loading_ && settingsHandler_) {
        settingsHandler_(Settings());
    }
}

ToolSettings V2DrawingDock::Settings() const
{
    ToolSettings settings;
    const int index = arcMode_->currentIndex();
    if (index >= 0 && index < static_cast<int>(kArcModes.size())) {
        settings.arcMode = kArcModes[static_cast<std::size_t>(index)];
    }
    settings.radiusMm = arcRadius_->value();
    settings.sweepAngleRad = arcSweep_->value() * kPi / 180.0;
    settings.construction = construction_->isChecked();
    settings.keepPoints = keepPoints_->isChecked();
    return settings;
}

void V2DrawingDock::SetSettings(const ToolSettings& settings)
{
    loading_ = true;
    for (std::size_t index = 0; index < kArcModes.size(); ++index) {
        if (kArcModes[index] == settings.arcMode) {
            arcMode_->setCurrentIndex(static_cast<int>(index));
        }
    }
    arcRadius_->setValue(settings.radiusMm);
    arcSweep_->setValue(settings.sweepAngleRad * 180.0 / kPi);
    construction_->setChecked(settings.construction);
    keepPoints_->setChecked(settings.keepPoints);
    loading_ = false;
    ApplyArcVisibility();
    EmitSettings();
}

void V2DrawingDock::SetSettingsHandler(std::function<void(const ToolSettings&)> handler)
{
    settingsHandler_ = std::move(handler);
}

DirectWireRequest V2DrawingDock::DirectWire() const
{
    DirectWireRequest request;
    const int index = wireKind_->currentIndex();
    const auto& kinds = DirectWireKinds();
    if (index >= 0 && index < static_cast<int>(kinds.size())) {
        request.kind = kinds[static_cast<std::size_t>(index)];
    }
    const bool planar = DirectWireIsPlanar(request.kind);
    for (const auto& row : wirePoints_) {
        request.points.push_back(Vector3{row[0]->value(), row[1]->value(),
            planar ? 0.0 : row[2]->value()});
    }
    request.radiusMm = wireRadius_->value();
    request.construction = wireConstruction_->isChecked();
    return request;
}

QString V2DrawingDock::DirectWireName() const
{
    return wireName_->text().trimmed();
}

void V2DrawingDock::SetDirectWire(const DirectWireRequest& request, const QString& name)
{
    const auto& kinds = DirectWireKinds();
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        if (kinds[index] == request.kind) {
            wireKind_->setCurrentIndex(static_cast<int>(index));
        }
    }
    for (std::size_t index = 0; index < wirePoints_.size(); ++index) {
        const Vector3 point = index < request.points.size() ? request.points[index] : Vector3{};
        wirePoints_[index][0]->setValue(point.x);
        wirePoints_[index][1]->setValue(point.y);
        wirePoints_[index][2]->setValue(point.z);
    }
    wireRadius_->setValue(request.radiusMm);
    wireConstruction_->setChecked(request.construction);
    wireName_->setText(name);
    ApplyDirectWireVisibility();
}

void V2DrawingDock::SetCreateWireHandler(std::function<void()> handler)
{
    createWireHandler_ = std::move(handler);
}

void V2DrawingDock::PressCreateWire()
{
    if (createWireHandler_) {
        createWireHandler_();
    }
}

void V2DrawingDock::SetDirectWireKindIndex(int index)
{
    wireKind_->setCurrentIndex(index);
    ApplyDirectWireVisibility();
}

void V2DrawingDock::ShowMessage(const QString& text)
{
    message_->setText(text);
}
