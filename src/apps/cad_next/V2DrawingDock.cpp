#include "V2DrawingDock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <string_view>

namespace {

//! 作り方カードを 1 行に並べる枚数。380px の棚に収まる数。
constexpr int kMethodCardsPerRow = 3;

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

} // namespace

V2DrawingDock::V2DrawingDock(QWidget* parent)
    : QDockWidget(QStringLiteral("作図"), parent)
{
    setObjectName(QStringLiteral("drawingDock"));
    body_ = new QWidget(this);
    auto* layout = new QVBoxLayout(body_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    activeTool_ = new QLabel(body_);
    activeTool_->setObjectName(QStringLiteral("activeDrawingTool"));
    QFont activeFont = activeTool_->font();
    activeFont.setBold(true);
    activeFont.setPointSize(activeFont.pointSize() + 2);
    activeTool_->setFont(activeFont);
    activeTool_->setText(QStringLiteral("使用中: 選択"));
    layout->addWidget(activeTool_);

    inputModes_ = new QTabWidget(body_);
    inputModes_->setObjectName(QStringLiteral("drawingInputModes"));
    inputModes_->setDocumentMode(true);
    layout->addWidget(inputModes_, 1);

    auto* interactivePage = new QWidget(inputModes_);
    auto* interactiveLayout = new QVBoxLayout(interactivePage);
    interactiveLayout->setContentsMargins(4, 8, 4, 4);
    interactiveLayout->setSpacing(6);

    // 作り方カード(正本の methods)。作り方を先に選び、必要な欄だけを出す。
    methodTitle_ = new QLabel(QStringLiteral("作り方"), interactivePage);
    methodTitle_->setObjectName(QStringLiteral("drawingMethodTitle"));
    interactiveLayout->addWidget(methodTitle_);
    methodRow_ = new QWidget(interactivePage);
    // 横一列に並べると 380px の棚で 4 枚目(円弧の「始点接線・半径・中心角」)が切れて
    // 横スクロールが出る(PC 1.5 倍 2026-09-19)。3 枚ごとに折り返す升目にする。
    methodLayout_ = new QGridLayout(methodRow_);
    methodLayout_->setContentsMargins(0, 0, 0, 0);
    methodLayout_->setSpacing(4);
    interactiveLayout->addWidget(methodRow_);
    // 決める欄が無い道具のときに、代わりに出す一文。
    // 空の棚を出すと「壊れた」ようにしか見えない。作り方があれば、そのカードの一文。
    hint_ = new QLabel(interactivePage);
    hint_->setObjectName(QStringLiteral("drawingNextStep"));
    hint_->setWordWrap(true);
    interactiveLayout->addWidget(hint_);

    toolTitle_ = new QLabel(QStringLiteral("入力"), interactivePage);
    interactiveLayout->addWidget(toolTitle_);
    toolForm_ = new QFormLayout();
    toolForm_->setContentsMargins(0, 0, 0, 0);
    toolForm_->setSpacing(3);
    BuildArcRows(toolForm_);
    interactiveLayout->addLayout(toolForm_);

    auto* optionTitle = new QLabel(QStringLiteral("オプション"), interactivePage);
    optionTitle->setObjectName(QStringLiteral("drawingOptionTitle"));
    interactiveLayout->addWidget(optionTitle);
    construction_ = new QCheckBox(QStringLiteral("補助線として作図"), interactivePage);
    interactiveLayout->addWidget(construction_);
    keepPoints_ = new QCheckBox(QStringLiteral("指定した点を作図点として残す"), interactivePage);
    interactiveLayout->addWidget(keepPoints_);
    QObject::connect(construction_, &QCheckBox::toggled, this, [this] { EmitSettings(); });
    QObject::connect(keepPoints_, &QCheckBox::toggled, this, [this] { EmitSettings(); });

    interactiveLayout->addStretch(1);
    inputModes_->addTab(interactivePage, QStringLiteral("画面で作図"));

    auto* coordinatePage = new QWidget(inputModes_);
    auto* coordinateLayout = new QVBoxLayout(coordinatePage);
    coordinateLayout->setContentsMargins(4, 8, 4, 4);
    coordinateLayout->setSpacing(6);
    auto* wireTitle = new QLabel(QStringLiteral("座標と寸法を入力"), coordinatePage);
    coordinateLayout->addWidget(wireTitle);
    wireForm_ = new QFormLayout();
    wireForm_->setContentsMargins(0, 0, 0, 0);
    wireForm_->setSpacing(3);
    BuildDirectWireRows(wireForm_);
    coordinateLayout->addLayout(wireForm_);

    createWire_ = new QPushButton(QStringLiteral("この座標で線を作る"), coordinatePage);
    QObject::connect(createWire_, &QPushButton::clicked, this, [this] { PressCreateWire(); });
    coordinateLayout->addWidget(createWire_);
    message_ = new QLabel(coordinatePage);
    message_->setWordWrap(true);
    coordinateLayout->addWidget(message_);
    coordinateLayout->addStretch(1);
    inputModes_->addTab(coordinatePage, QStringLiteral("座標で作成"));
    // 棚の中身は巻物にする。欄が多い棚の最小幅で右の棚全体が広がり、
    // 画面(作図の場所)が狭くなって入力列が画面の外へ寄っていた。
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body_);
    setWidget(scroll);
    ApplyArcVisibility();
    ApplyDirectWireVisibility();
    ApplyToolRows();
}

void V2DrawingDock::SetTool(kachakacha::v2::modeling::DrawingTool tool)
{
    const bool changed = tool_ != tool;
    tool_ = tool;
    activeTool_->setText(QStringLiteral("使用中: %1").arg(Text(
        kachakacha::v2::modeling::DrawingToolNameJa(tool_))));
    if (changed) {
        inputModes_->setCurrentIndex(0);
        message_->clear();
    }
    ApplyToolRows();
}

QString V2DrawingDock::HintText() const
{
    return hint_ == nullptr ? QString() : hint_->text();
}

QString V2DrawingDock::ActiveToolText() const
{
    return activeTool_ == nullptr ? QString() : activeTool_->text();
}

int V2DrawingDock::InputModeIndex() const
{
    return inputModes_ == nullptr ? -1 : inputModes_->currentIndex();
}

void V2DrawingDock::SetInputModeIndex(int index)
{
    if (inputModes_ != nullptr && index >= 0 && index < inputModes_->count()) {
        inputModes_->setCurrentIndex(index);
    }
}

//! いまの道具で意味のある欄だけを出す。決め方は core(app/DrawingShelfRows)。
void V2DrawingDock::ApplyToolRows()
{
    const auto rows = kachakacha::v2::app::DrawingShelfRowsFor(tool_);
    // 見出しに道具の名前を入れる。「作図」だけでは、どの道具の欄か読めない。
    setWindowTitle(QString::fromStdString(
        kachakacha::v2::app::DrawingShelfTitleJa(tool_)));
    RebuildMethodCards();
    if (rows.arc) {
        // 半径と中心角は、さらに作り方で決まる。二重に決めない。
        ApplyArcVisibility();
    } else {
        toolForm_->setRowVisible(arcRadius_, false);
        toolForm_->setRowVisible(arcSweep_, false);
    }
    construction_->setVisible(rows.construction);
    keepPoints_->setVisible(rows.keepPoints);
    toolTitle_->setVisible(rows.arc);
    // 一文はいつも出す。作り方があればそのカードの一文、無ければ道具の使い方。
    if (methodIndex_ >= 0 && methodIndex_ < static_cast<int>(methodCards_.size())) {
        hint_->setText(Text(methodCards_[static_cast<std::size_t>(methodIndex_)].hintJa));
    } else {
        hint_->setText(Text(kachakacha::v2::app::DrawingToolHintJa(tool_)));
    }
    hint_->setVisible(true);
}

//! いまの道具のカードを並べ直す。何を並べるかは core(app/DrawingMethodCards)。
void V2DrawingDock::RebuildMethodCards()
{
    for (QToolButton* button : methodButtons_) {
        methodLayout_->removeWidget(button);
        delete button;
    }
    methodButtons_.clear();
    methodCards_ = kachakacha::v2::app::DrawingMethodCardsFor(tool_);
    ToolSettings now;
    now.arcMode = arcMode_;
    methodIndex_ = kachakacha::v2::app::CurrentDrawingMethodIndex(tool_, now);
    for (std::size_t index = 0; index < methodCards_.size(); ++index) {
        const auto& card = methodCards_[index];
        auto* button = new QToolButton(methodRow_);
        button->setObjectName(QStringLiteral("drawingMethodCard"));
        button->setText(Text(card.labelJa));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(Text(card.Blocked() ? card.blockedReasonJa : card.hintJa));
        button->setEnabled(!card.Blocked());
        button->setChecked(static_cast<int>(index) == methodIndex_);
        const int at = static_cast<int>(index);
        QObject::connect(button, &QToolButton::clicked, this, [this, at] { ChooseMethod(at); });
        methodLayout_->addWidget(button, at / kMethodCardsPerRow, at % kMethodCardsPerRow);
        button->show();   // 親が見えたあとに作った子は show() まで見えない(HP-DM)
        methodButtons_.push_back(button);
    }
    methodRow_->setVisible(!methodCards_.empty());
    methodTitle_->setVisible(!methodCards_.empty());
}

//! カードを押した。円弧なら作り方(ArcMode)が変わる。押せないカードは理由を言う。
void V2DrawingDock::ChooseMethod(int index)
{
    if (index < 0 || index >= static_cast<int>(methodCards_.size())) {
        return;
    }
    const auto& card = methodCards_[static_cast<std::size_t>(index)];
    if (card.Blocked()) {
        if (blockedMethodHandler_) {
            blockedMethodHandler_(Text(card.blockedReasonJa));
        }
        return;
    }
    methodIndex_ = index;
    for (std::size_t at = 0; at < methodButtons_.size(); ++at) {
        methodButtons_[at]->setChecked(static_cast<int>(at) == index);
    }
    hint_->setText(Text(card.hintJa));
    // カードが欄を名指ししていれば、カーソル横の入力列でその欄を選んでおく
    // (円の「直径指定」→ 直径)。押しても何も起きないカードを作らない。
    if (cursorFieldHandler_) {
        cursorFieldHandler_(Text(card.cursorFieldId));
    }
    if (card.arcMode.has_value() && *card.arcMode != arcMode_) {
        arcMode_ = *card.arcMode;
        ApplyArcVisibility();
        EmitSettings();
    }
}

QToolButton* V2DrawingDock::MethodButton(const QString& labelJa) const
{
    for (QToolButton* button : methodButtons_) {
        if (button->text() == labelJa) {
            return button;
        }
    }
    return nullptr;
}

std::vector<QString> V2DrawingDock::MethodLabels() const
{
    std::vector<QString> labels;
    for (QToolButton* button : methodButtons_) {
        labels.push_back(button->text());
    }
    return labels;
}

QString V2DrawingDock::CurrentMethodLabel() const
{
    if (methodIndex_ < 0 || methodIndex_ >= static_cast<int>(methodButtons_.size())) {
        return QString();
    }
    return methodButtons_[static_cast<std::size_t>(methodIndex_)]->text();
}

bool V2DrawingDock::MethodEnabled(const QString& labelJa) const
{
    QToolButton* button = MethodButton(labelJa);
    return button != nullptr && button->isVisible() && button->isEnabled();
}

QString V2DrawingDock::MethodTip(const QString& labelJa) const
{
    QToolButton* button = MethodButton(labelJa);
    return button == nullptr ? QString() : button->toolTip();
}

bool V2DrawingDock::ClickMethod(const QString& labelJa)
{
    QToolButton* button = MethodButton(labelJa);
    if (button == nullptr || !button->isVisible()) {
        return false;
    }
    if (!button->isEnabled()) {
        // 押せない理由を言う(押したことにはしない)。
        for (std::size_t index = 0; index < methodButtons_.size(); ++index) {
            if (methodButtons_[index] == button) {
                ChooseMethod(static_cast<int>(index));
            }
        }
        return false;
    }
    button->click();
    return true;
}

void V2DrawingDock::SetBlockedMethodHandler(std::function<void(const QString&)> handler)
{
    blockedMethodHandler_ = std::move(handler);
}

void V2DrawingDock::SetCursorFieldHandler(std::function<void(const QString&)> handler)
{
    cursorFieldHandler_ = std::move(handler);
}

void V2DrawingDock::BuildArcRows(QFormLayout* form)
{
    // 作り方(3点 / 始点・終点・半径 / 始点接線)はカードで決める。ここは値の欄だけ。
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
    if (!kachakacha::v2::app::DrawingShelfRowsFor(tool_).arc) {
        return;   // 円弧の道具でないなら、この区画はそもそも出ていない。
    }
    const ArcMode mode = arcMode_;
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
    settings.arcMode = arcMode_;
    settings.radiusMm = arcRadius_->value();
    settings.sweepAngleRad = arcSweep_->value() * kPi / 180.0;
    settings.construction = construction_->isChecked();
    settings.keepPoints = keepPoints_->isChecked();
    return settings;
}

void V2DrawingDock::SetSettings(const ToolSettings& settings)
{
    loading_ = true;
    arcMode_ = settings.arcMode;
    arcRadius_->setValue(settings.radiusMm);
    arcSweep_->setValue(settings.sweepAngleRad * 180.0 / kPi);
    construction_->setChecked(settings.construction);
    keepPoints_->setChecked(settings.keepPoints);
    loading_ = false;
    RebuildMethodCards();   // 円弧の作り方が変わったなら、押されたカードも変わる
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
