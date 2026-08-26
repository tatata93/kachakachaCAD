#include "MainWindow.h"
#include "PlatePdfExport.h"

#include "kachakacha/io/PlateFlatPattern.h"
#include "kachakacha/io/PlanarExport.h"
#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/io/NumericExpression.h"
#include "kachakacha/model/Measurement.h"
#include "kachakacha/model/Sketch.h"
#include "kachakacha/model/WireOperations.h"
#include "kachakacha/occt/BodyExport.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDebug>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::AddPlateFlatPatternModel;
using kachakacha::io::BuildPlateAssemblyGuide;
using kachakacha::io::BuildPlateFlatPattern;
using kachakacha::io::AutomaticReliefStyle;
using kachakacha::io::PlateFlatPatternOptions;
using kachakacha::io::WireLiesOnWorkPlane;
using kachakacha::io::WritePlateFlatPatternDxf;
using kachakacha::io::WritePlateFlatPatternSvg;
using kachakacha::io::WritePlanarDxf;
using kachakacha::io::WritePlanarSvg;
using kachakacha::io::WriteProjectScript;
using kachakacha::qtio::CalculatePlatePdfLayout;
using kachakacha::qtio::PlatePdfOptions;
using kachakacha::qtio::WritePlateFlatPatternPdf;
using kachakacha::model::DimensionReference;
using kachakacha::model::DimensionReferenceKind;
using kachakacha::model::Project;
using kachakacha::model::PlateDevelopability;
using kachakacha::model::PlateSplitAxis;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::JigSide;
using kachakacha::model::Sketch;
using kachakacha::model::SurfaceKind;
using kachakacha::model::Wire;
using kachakacha::model::WireContinuity;
using kachakacha::model::WireKind;
using kachakacha::model::WireMetadata;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;
using kachakacha::model::ChamferIntersectingLines;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::JoinLineChain;
using kachakacha::model::MeasureDirectionToPlaneAngleDegrees;
using kachakacha::model::MeasureDirectionsAngle;
using kachakacha::model::MeasurePlaneToPlaneAngleDegrees;
using kachakacha::model::MeasurePointToWireDistance;
using kachakacha::model::MeasureSignedPointToPlaneDistance;
using kachakacha::model::MeasureThreePointAngle;
using kachakacha::model::MeasureWireCurvatureNormal;
using kachakacha::model::MeasureWireLength;
using kachakacha::model::MeasureWireRadius;
using kachakacha::model::MeasureWireTangent;
using kachakacha::model::MeasureWireToWireDistance;
using kachakacha::model::MeetLinesAtIntersection;
using kachakacha::model::ExtendWireToBoundary;
using kachakacha::model::OffsetPlanarWire;
using kachakacha::model::ReferenceDimension;
using kachakacha::model::ReferenceDimensionKind;
using kachakacha::model::RetainedLineEnd;
using kachakacha::model::TrimWireAtBoundaries;

namespace {

constexpr int kSelectionKindRole = Qt::UserRole;
constexpr int kSelectionIndexRole = Qt::UserRole + 1;
constexpr int kDimensionNameRole = Qt::UserRole + 2;
constexpr double kPi = 3.14159265358979323846;

bool IsAutomationInvocation()
{
    const QStringList arguments = QApplication::arguments();
    return arguments.contains(QStringLiteral("--self-test"))
        || arguments.contains(QStringLiteral("--snapshot"))
        || arguments.contains(QStringLiteral("--manual-state"))
        || arguments.contains(QStringLiteral("--export-first-body-stl"))
        || arguments.contains(QStringLiteral("--export-first-body-step"));
}

class ExpressionDoubleSpinBox final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    QValidator::State validate(QString& input, int& position) const override
    {
        Q_UNUSED(position);
        const QString expression = NormalizeExpression(input);
        if (expression.isEmpty()) {
            return QValidator::Intermediate;
        }
        static const QRegularExpression allowed(
            QStringLiteral("^[0-9eEpiPI+\\-*/().\\s]*$"));
        if (!allowed.match(expression).hasMatch()) {
            return QValidator::Invalid;
        }
        const std::optional<double> value = Evaluate(expression);
        if (!value.has_value()) {
            return QValidator::Intermediate;
        }
        return *value >= minimum() && *value <= maximum()
            ? QValidator::Acceptable
            : QValidator::Invalid;
    }

    double valueFromText(const QString& text) const override
    {
        const std::optional<double> result = Evaluate(NormalizeExpression(text));
        return result.has_value() ? *result : value();
    }

    void fixup(QString& input) const override
    {
        const std::optional<double> result = Evaluate(NormalizeExpression(input));
        if (result.has_value() && *result >= minimum() && *result <= maximum()) {
            input = prefix() + QDoubleSpinBox::textFromValue(*result) + suffix();
        }
    }

private:
    QString NormalizeExpression(QString text) const
    {
        if (!prefix().isEmpty() && text.startsWith(prefix())) {
            text.remove(0, prefix().size());
        }
        if (!suffix().isEmpty() && text.endsWith(suffix())) {
            text.chop(suffix().size());
        }
        text.replace(QChar(0x00d7), QLatin1Char('*'));
        text.replace(QChar(0x00f7), QLatin1Char('/'));
        text.replace(QChar(0x03c0), QStringLiteral("pi"));
        const QString decimalPoint = locale().decimalPoint();
        if (decimalPoint != QStringLiteral(".")) {
            text.replace(decimalPoint, QStringLiteral("."));
        }
        return text.trimmed();
    }

    static std::optional<double> Evaluate(const QString& expression)
    {
        const QByteArray utf8 = expression.toUtf8();
        return kachakacha::io::EvaluateNumericExpression(
            std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
    }
};

QDoubleSpinBox* MakeNumberField(double value = 0.0)
{
    auto* field = new ExpressionDoubleSpinBox;
    field->setRange(-1000000.0, 1000000.0);
    field->setDecimals(4);
    field->setSingleStep(0.5);
    field->setValue(value);
    field->setKeyboardTracking(false);
    field->setMinimumWidth(72);
    field->setToolTip(QStringLiteral("数値または計算式を入力できます。例: (180/2)*3"));
    return field;
}

QDoubleSpinBox* MakePositiveField(double value)
{
    QDoubleSpinBox* field = MakeNumberField(value);
    field->setRange(0.0001, 1000000.0);
    return field;
}

template <std::size_t Size>
QWidget* MakeCoordinateEditor(
    std::array<QDoubleSpinBox*, Size>& fields,
    const std::array<double, Size>& values,
    const std::array<QString, Size>& labels)
{
    auto* widget = new QWidget;
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    for (std::size_t index = 0; index < Size; ++index) {
        auto* label = new QLabel(labels[index]);
        label->setStyleSheet("color: #5c6670;");
        fields[index] = MakeNumberField(values[index]);
        layout->addWidget(label);
        layout->addWidget(fields[index], 1);
    }
    return widget;
}

QWidget* MakeVector3Editor(std::array<QDoubleSpinBox*, 3>& fields, Vector3 value = {})
{
    return MakeCoordinateEditor<3>(fields, {value.x, value.y, value.z}, {"X", "Y", "Z"});
}

QWidget* MakeVector2Editor(std::array<QDoubleSpinBox*, 2>& fields, Vector2 value = {})
{
    return MakeCoordinateEditor<2>(fields, {value.x, value.y}, {"U", "V"});
}

Vector3 ReadVector3(const std::array<QDoubleSpinBox*, 3>& fields)
{
    return {fields[0]->value(), fields[1]->value(), fields[2]->value()};
}

Vector2 ReadVector2(const std::array<QDoubleSpinBox*, 2>& fields)
{
    return {fields[0]->value(), fields[1]->value()};
}

Vector3 ReadTablePoint(const QTableWidget* table, int row)
{
    std::array<double, 3> values{};
    for (int column = 0; column < 3; ++column) {
        const auto* field = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, column));
        if (field == nullptr) {
            throw std::logic_error("Wire point editor is incomplete.");
        }
        values[column] = field->value();
    }
    return {values[0], values[1], values[2]};
}

QWidget* MakeFormPage(QFormLayout*& form)
{
    auto* page = new QWidget;
    form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setContentsMargins(0, 6, 0, 6);
    form->setVerticalSpacing(8);
    return page;
}

QString ToQString(const std::string& text)
{
    return QString::fromUtf8(text);
}

std::string ToName(const QString& text)
{
    return text.trimmed().toUtf8().toStdString();
}

void ValidateObjectName(const QString& name)
{
    if (name.trimmed().isEmpty()) {
        throw std::invalid_argument("名前を入力してください。");
    }
    static const QRegularExpression invalidCharacters(QStringLiteral("[\\s#]"));
    if (name.contains(invalidCharacters)) {
        throw std::invalid_argument("名前には空白と # を使用できません。");
    }
}

QString Number(double value)
{
    return QString::number(value, 'f', 3);
}

QString ReferenceDimensionKindText(ReferenceDimensionKind kind)
{
    switch (kind) {
    case ReferenceDimensionKind::PointDistance:
        return QStringLiteral("2点間");
    case ReferenceDimensionKind::WireLength:
        return QStringLiteral("ワイヤー全長");
    case ReferenceDimensionKind::WireRadius:
        return QStringLiteral("半径");
    case ReferenceDimensionKind::WireDistance:
        return QStringLiteral("ワイヤー間距離");
    case ReferenceDimensionKind::WireAngle:
        return QStringLiteral("接線角");
    case ReferenceDimensionKind::PointWireDistance:
        return QStringLiteral("点・ワイヤー間");
    case ReferenceDimensionKind::PointPlaneDistance:
        return QStringLiteral("点・平面間");
    case ReferenceDimensionKind::WirePlaneAngle:
        return QStringLiteral("ワイヤー・平面角");
    case ReferenceDimensionKind::PlaneAngle:
        return QStringLiteral("平面角");
    case ReferenceDimensionKind::PlaneDistance:
        return QStringLiteral("平面間距離");
    }
    return QStringLiteral("参照寸法");
}

bool IsAngleDimension(ReferenceDimensionKind kind)
{
    return kind == ReferenceDimensionKind::WireAngle
        || kind == ReferenceDimensionKind::WirePlaneAngle
        || kind == ReferenceDimensionKind::PlaneAngle;
}

QString ReferenceDimensionValueText(ReferenceDimensionKind kind, double value)
{
    if (kind == ReferenceDimensionKind::WireRadius) {
        return QStringLiteral("R %1 mm").arg(Number(value));
    }
    if (IsAngleDimension(kind)) {
        return QStringLiteral("%1°").arg(Number(value));
    }
    return QStringLiteral("%1 mm").arg(Number(value));
}

QString VectorText(Vector3 value)
{
    return QStringLiteral("X %1   Y %2   Z %3").arg(Number(value.x), Number(value.y), Number(value.z));
}

QString WireKindText(WireKind kind)
{
    switch (kind) {
    case WireKind::Line:
        return QStringLiteral("直線");
    case WireKind::Polyline:
        return QStringLiteral("ポリライン");
    case WireKind::CubicBezier:
        return QStringLiteral("3次ベジェ曲線");
    case WireKind::CubicBSpline:
        return QStringLiteral("3次B-spline");
    case WireKind::Circle:
        return QStringLiteral("円");
    case WireKind::CircularArc:
        return QStringLiteral("円弧");
    }
    return QStringLiteral("ワイヤー");
}

QString PolicyText(WirePlanePolicy policy)
{
    switch (policy) {
    case WirePlanePolicy::Free3D:
        return QStringLiteral("平面拘束なし");
    case WirePlanePolicy::ReferenceOnly:
        return QStringLiteral("平面を編集基準に使用");
    case WirePlanePolicy::LockedToPlane:
        return QStringLiteral("作業平面に固定");
    }
    return {};
}

bool WireLiesOnPlane(const Wire& wire, const WorkPlane& plane, double tolerance = 1.0e-7)
{
    const int samples = wire.Kind() == WireKind::Line ? 1 : 32;
    for (int sample = 0; sample <= samples; ++sample) {
        if (std::abs(plane.Project(wire.Evaluate(static_cast<double>(sample) / samples)).w) > tolerance) {
            return false;
        }
    }
    return true;
}

WireMetadata RetargetLineConstraints(
    const Project& project,
    WireMetadata metadata,
    const Wire& wire,
    bool updateLength)
{
    if (metadata.lineConstraints.Empty()) {
        return metadata;
    }
    if (wire.Kind() != WireKind::Line) {
        metadata.lineConstraints = {};
        return metadata;
    }
    if (updateLength && metadata.lineConstraints.lengthMillimeters.has_value()) {
        metadata.lineConstraints.lengthMillimeters = (wire.End() - wire.Start()).Length();
    }
    if (!metadata.lineConstraints.angleDegrees.has_value()) {
        return metadata;
    }
    if (!metadata.sourcePlaneName.has_value()) {
        throw std::invalid_argument("角度拘束の基準作業平面がありません。");
    }
    const auto plane = project.FindWorkPlane(*metadata.sourcePlaneName);
    if (!plane.has_value() || !WireLiesOnPlane(wire, *plane)) {
        throw std::invalid_argument("角度拘束された直線は基準作業平面の外へ移動できません。");
    }
    const auto start = plane->Project(wire.Start());
    const auto end = plane->Project(wire.End());
    metadata.lineConstraints.angleDegrees =
        std::atan2(end.v - start.v, end.u - start.u) * 180.0 / kPi;
    return metadata;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    BuildUi();
    BuildMenusAndToolbar();

    project_.AddWorkPlane("top_XY", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
    project_.AddWorkPlane("front_XZ", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}));
    project_.AddWorkPlane("side_YZ", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    RefreshModelViews(true);
    toolsTabs_->setCurrentIndex(0);
    SetViewportTool(ViewportTool::DrawLine);
    viewport_->SetIsometricView();

    resize(1380, 820);
    setMinimumSize(1040, 650);
    setWindowTitle(QStringLiteral("kachakachaCAD - 無題"));

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(60000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::WriteAutosave);
    autosaveTimer_->start();
    QTimer::singleShot(0, this, &MainWindow::OfferAutosaveRecovery);
}

void MainWindow::BuildUi()
{
    viewport_ = new CadViewport;
    setCentralWidget(viewport_);
    viewport_->SetSelectionChangedCallback([this](const std::vector<CadSelection>& selections) {
        UpdateSelections(selections, true);
    });
    viewport_->SetPointCreatedCallback([this](Vector3 point) {
        AddViewportPoint(point);
    });
    viewport_->SetLineCreatedCallback([this](Vector3 start, Vector3 end) {
        AddViewportLine(start, end);
    });
    viewport_->SetPolylineCreatedCallback([this](const std::vector<Vector3>& points) {
        AddViewportPolyline(points);
    });
    viewport_->SetRectangleCreatedCallback([this](const std::array<Vector3, 4>& corners) {
        AddViewportRectangle(corners);
    });
    viewport_->SetCircleCreatedCallback([this](Vector3 center, double radius) {
        AddViewportCircle(center, radius);
    });
    viewport_->SetArcCreatedCallback([this](Vector3 start, Vector3 through, Vector3 end) {
        AddViewportArc(start, through, end);
    });
    viewport_->SetArcWireCreatedCallback([this](const Wire& arc) {
        AddViewportArcWire(arc);
    });
    viewport_->SetBezierCreatedCallback([this](const std::array<Vector3, 4>& points) {
        AddViewportBezier(points);
    });
    viewport_->SetSplineCreatedCallback([this](const std::vector<Vector3>& throughPoints) {
        AddViewportSpline(throughPoints);
    });
    viewport_->SetWireControlPointMovedCallback([this](int wireIndex, const Wire& replacement) {
        ApplyViewportWireEdit(wireIndex, replacement);
    });
    viewport_->SetTranslationRequestedCallback([this](Vector3 delta, bool copy) {
        ApplyViewportTranslation(delta, copy);
    });
    viewport_->SetMirrorRequestedCallback([this](Vector3 linePoint, Vector3 lineDirection, Vector3 planeNormal) {
        ApplyViewportMirror(linePoint, lineDirection, planeNormal);
    });
    viewport_->SetRotationRequestedCallback([this](Vector3 axisPoint, Vector3 axisDirection, double angleRadians) {
        ApplyViewportRotation(axisPoint, axisDirection, angleRadians);
    });
    viewport_->SetSplitRequestedCallback([this](int wireIndex, double parameter) {
        ApplySplitWire(wireIndex, parameter);
    });
    viewport_->SetTrimRequestedCallback([this](int wireIndex, double parameter) {
        ApplyDirectLineTrim(wireIndex, parameter);
    });
    viewport_->SetExtendRequestedCallback([this](int wireIndex, double parameter) {
        ApplyDirectLineExtend(wireIndex, parameter);
    });
    viewport_->SetToolExitRequestedCallback([this] {
        SetViewportTool(ViewportTool::Select);
    });
    viewport_->SetCoincidenceRequestedCallback([this](WireEndpointPick anchor, WireEndpointPick follower) {
        ApplyEndpointCoincidence(anchor, follower);
    });
    viewport_->SetTangentRequestedCallback([this](WireEndpointPick anchor, WireEndpointPick follower) {
        ApplyEndpointTangency(anchor, follower);
    });
    viewport_->SetCurvatureRequestedCallback([this](WireEndpointPick anchor, WireEndpointPick follower) {
        ApplyEndpointCurvature(anchor, follower);
    });
    viewport_->SetMeasurementChangedCallback([this](const std::vector<MeasurementPick>& picks) {
        UpdateMeasurement(picks);
    });
    BuildDrawingActions();
    viewport_->SetDrawingStateChangedCallback([this](ViewportTool tool, std::size_t pointCount) {
        UpdateDrawingPanel(tool, pointCount);
    });
    viewport_->SetGridOriginChangedCallback([this](double u, double v) {
        const QSignalBlocker blockU(gridOrigin_[0]);
        const QSignalBlocker blockV(gridOrigin_[1]);
        gridOrigin_[0]->setValue(u);
        gridOrigin_[1]->setValue(v);
        statusBar()->showMessage(
            QStringLiteral("点グリッドの基準を X %1 mm / Y %2 mm に合わせました")
                .arg(u, 0, 'f', 3).arg(v, 0, 'f', 3),
            4000);
        SetViewportTool(ViewportTool::Select);
    });

    auto* modelDock = new QDockWidget(QStringLiteral("モデル"), this);
    modelDock->setObjectName("modelDock");
    modelDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* modelPanel = new QWidget;
    auto* modelLayout = new QVBoxLayout(modelPanel);
    modelLayout->setContentsMargins(6, 6, 6, 6);
    modelLayout->setSpacing(6);
    modelFilter_ = new QLineEdit;
    modelFilter_->setClearButtonEnabled(true);
    modelFilter_->setPlaceholderText(QStringLiteral("名前・種類で絞り込み"));
    modelFilter_->setToolTip(QStringLiteral("ワイヤー、板材などの種類名または部材名を入力して絞り込み"));
    modelTree_ = new QTreeWidget;
    modelTree_->setHeaderHidden(true);
    modelTree_->setAlternatingRowColors(true);
    modelTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    modelTree_->header()->setStretchLastSection(true);
    modelLayout->addWidget(modelFilter_);
    modelLayout->addWidget(modelTree_, 1);

    auto* beginnerGuide = new QGroupBox(QStringLiteral("操作ガイド"));
    beginnerGuide->setObjectName(QStringLiteral("beginnerGuide"));
    auto* beginnerGuideLayout = new QVBoxLayout(beginnerGuide);
    beginnerGuideLayout->setContentsMargins(9, 8, 9, 9);
    beginnerGuideLayout->setSpacing(5);
    beginnerGuideTitle_ = new QLabel(QStringLiteral("作業を選んでください"));
    beginnerGuideTitle_->setObjectName(QStringLiteral("beginnerGuideTitle"));
    beginnerGuideTitle_->setWordWrap(true);
    beginnerGuideNext_ = new QLabel(QStringLiteral("次に行う操作をここへ表示します"));
    beginnerGuideNext_->setObjectName(QStringLiteral("beginnerGuideNext"));
    beginnerGuideNext_->setWordWrap(true);
    beginnerGuideSteps_ = new QLabel;
    beginnerGuideSteps_->setObjectName(QStringLiteral("beginnerGuideSteps"));
    beginnerGuideSteps_->setWordWrap(true);
    beginnerGuideContext_ = new QLabel;
    beginnerGuideContext_->setObjectName(QStringLiteral("beginnerGuideContext"));
    beginnerGuideContext_->setWordWrap(true);
    beginnerGuideContext_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    beginnerGuideLayout->addWidget(beginnerGuideTitle_);
    beginnerGuideLayout->addWidget(beginnerGuideNext_);
    beginnerGuideLayout->addWidget(beginnerGuideSteps_);
    beginnerGuideLayout->addWidget(beginnerGuideContext_);

    auto* beginnerGuideButtons = new QHBoxLayout;
    beginnerGuideButtons->setContentsMargins(0, 2, 0, 0);
    beginnerGuideButtons->setSpacing(5);
    beginnerGuideNextButton_ = new QPushButton;
    beginnerGuideNextButton_->setObjectName(QStringLiteral("guideNextButton"));
    beginnerGuideNextButton_->setVisible(false);
    beginnerGuideManualButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogHelpButton), QStringLiteral("詳しい手順"));
    beginnerGuideManualButton_->setToolTip(QStringLiteral("現在の機能の画像付きマニュアルを開く"));
    beginnerGuideButtons->addWidget(beginnerGuideNextButton_, 1);
    beginnerGuideButtons->addWidget(beginnerGuideManualButton_);
    beginnerGuideLayout->addLayout(beginnerGuideButtons);
    modelLayout->addWidget(beginnerGuide);

    connect(beginnerGuideNextButton_, &QPushButton::clicked, this, [this] {
        if (toolsTabs_ == nullptr || beginnerGuideNextTab_ < 0) {
            return;
        }
        SetViewportTool(ViewportTool::Select);
        toolsTabs_->setCurrentIndex(beginnerGuideNextTab_);
    });
    connect(beginnerGuideManualButton_, &QPushButton::clicked, this, [this] {
        OpenManual(beginnerGuideManualAnchor_);
    });
    modelDock->setWidget(modelPanel);
    addDockWidget(Qt::LeftDockWidgetArea, modelDock);
    modelDock->setMinimumWidth(260);

    connect(modelFilter_, &QLineEdit::textChanged, this, [this] {
        ApplyModelTreeFilter();
    });

    connect(modelTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const QList<QTreeWidgetItem*> items = modelTree_->selectedItems();
        std::vector<CadSelection> selections;
        for (QTreeWidgetItem* item : items) {
            if (item->parent() == nullptr
                || !item->data(0, kSelectionKindRole).isValid()
                || !item->data(0, kSelectionIndexRole).isValid()) {
                continue;
            }
            selections.push_back({
                static_cast<CadSelectionKind>(item->data(0, kSelectionKindRole).toInt()),
                item->data(0, kSelectionIndexRole).toInt(),
            });
        }
        QTreeWidgetItem* current = modelTree_->currentItem();
        if (current != nullptr && current->parent() != nullptr
            && current->data(0, kSelectionKindRole).isValid()
            && current->data(0, kSelectionIndexRole).isValid()) {
            const CadSelection currentSelection = {
                static_cast<CadSelectionKind>(current->data(0, kSelectionKindRole).toInt()),
                current->data(0, kSelectionIndexRole).toInt(),
            };
            std::erase_if(selections, [&](const CadSelection& selection) {
                return selection.kind == currentSelection.kind && selection.index == currentSelection.index;
            });
            selections.push_back(currentSelection);
        }
        UpdateSelections(std::move(selections), false);
    });
    connect(modelTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (item == nullptr || item->parent() == nullptr) {
            return;
        }
        const CadSelectionKind kind = static_cast<CadSelectionKind>(item->data(0, kSelectionKindRole).toInt());
        const int index = item->data(0, kSelectionIndexRole).toInt();
        const bool visible = item->checkState(0) == Qt::Checked;
        bool currentVisibility = visible;
        if (kind == CadSelectionKind::WorkPlane && index >= 0 && index < static_cast<int>(project_.WorkPlanes().size())) {
            currentVisibility = project_.WorkPlanes()[index].visible;
        } else if (kind == CadSelectionKind::Point && index >= 0 && index < static_cast<int>(project_.Points().size())) {
            currentVisibility = project_.Points()[index].visible;
        } else if (kind == CadSelectionKind::Wire && index >= 0 && index < static_cast<int>(project_.Wires().size())) {
            currentVisibility = project_.Wires()[index].visible;
        } else if (kind == CadSelectionKind::Surface && index >= 0 && index < static_cast<int>(project_.Surfaces().size())) {
            currentVisibility = project_.Surfaces()[index].visible;
        } else if (kind == CadSelectionKind::Plate && index >= 0 && index < static_cast<int>(project_.Plates().size())) {
            currentVisibility = project_.Plates()[index].visible;
        } else if (kind == CadSelectionKind::Body && index >= 0 && index < static_cast<int>(project_.Bodies().size())) {
            currentVisibility = project_.Bodies()[index].visible;
        } else {
            return;
        }
        if (currentVisibility == visible) {
            return;
        }

        RecordUndo();
        if (kind == CadSelectionKind::WorkPlane) {
            project_.SetWorkPlaneVisible(project_.WorkPlanes()[index].name, visible);
        } else if (kind == CadSelectionKind::Point) {
            project_.SetPointVisible(project_.Points()[index].name, visible);
        } else if (kind == CadSelectionKind::Wire) {
            project_.SetWireVisible(project_.Wires()[index].name, visible);
        } else if (kind == CadSelectionKind::Surface) {
            project_.SetSurfaceVisible(project_.Surfaces()[index].name, visible);
        } else if (kind == CadSelectionKind::Plate) {
            project_.SetPlateVisible(project_.Plates()[index].name, visible);
        } else {
            project_.SetBodyVisible(project_.Bodies()[index].name, visible);
        }
        MarkModified();
        RefreshPlaneChoices();
        viewport_->SetProject(&project_, false);
        RefreshActiveWorkPlane();
        RefreshReference();
        UpdateSelection({}, true);
        RefreshExportSummary();
        statusBar()->showMessage(visible ? QStringLiteral("再表示しました") : QStringLiteral("非表示にしました"), 2000);
    });

    auto* toolsDock = new QDockWidget(QStringLiteral("作図と編集"), this);
    toolsDock->setObjectName("toolsDock");
    toolsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* toolsPanel = new QWidget;
    auto* toolsLayout = new QVBoxLayout(toolsPanel);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(5);
    auto* workflowPanel = new QWidget;
    workflowPanel->setObjectName(QStringLiteral("workflowPanel"));
    auto* workflowLayout = new QHBoxLayout(workflowPanel);
    workflowLayout->setContentsMargins(6, 6, 6, 0);
    workflowLayout->setSpacing(4);
    const std::array<QString, 4> workflowNames = {
        QStringLiteral("1 平面"), QStringLiteral("2 作図"),
        QStringLiteral("3 面・板"), QStringLiteral("4 出力"),
    };
    const std::array<int, 4> workflowTabs = {1, 0, 5, 6};
    for (std::size_t index = 0; index < workflowButtons_.size(); ++index) {
        workflowButtons_[index] = new QPushButton(workflowNames[index]);
        workflowButtons_[index]->setObjectName(QStringLiteral("workflowButton"));
        workflowButtons_[index]->setCheckable(true);
        workflowButtons_[index]->setToolTip(index == 0 ? QStringLiteral("作図する位置と向きを決める")
            : index == 1 ? QStringLiteral("作業平面へ線や曲線を描く")
            : index == 2 ? QStringLiteral("ワイヤーから面・板・治具を作る")
                         : QStringLiteral("1:1図面または3Dモデルを保存する"));
        workflowLayout->addWidget(workflowButtons_[index], 1);
        connect(workflowButtons_[index], &QPushButton::clicked, this, [this, tab = workflowTabs[index]] {
            SetViewportTool(ViewportTool::Select);
            toolsTabs_->setCurrentIndex(tab);
        });
    }
    toolsLayout->addWidget(workflowPanel);
    toolsTabs_ = new QTabWidget;
    toolsTabs_->addTab(BuildDrawingPanel(), QStringLiteral("作図"));
    toolsTabs_->addTab(BuildPlanePanel(), QStringLiteral("作業平面"));
    toolsTabs_->addTab(BuildWirePanel(), QStringLiteral("数値入力"));
    toolsTabs_->addTab(BuildEditPanel(), QStringLiteral("編集"));
    toolsTabs_->addTab(BuildMachiningPanel(), QStringLiteral("加工"));
    toolsTabs_->addTab(BuildSurfacePanel(), QStringLiteral("面"));
    toolsTabs_->addTab(BuildOutputPanel(), QStringLiteral("出力"));
    toolsTabs_->addTab(BuildDisplayPanel(), QStringLiteral("表示"));
    toolsTabs_->addTab(BuildInfoPanel(), QStringLiteral("情報"));
    LoadDisplaySettings();
    connect(toolsTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        const ViewportTool tool = viewport_->Tool();
        const bool drawingTool = tool == ViewportTool::DrawLine
            || tool == ViewportTool::DrawPoint
            || tool == ViewportTool::DrawPolyline || tool == ViewportTool::DrawRectangle
            || tool == ViewportTool::DrawCircle || tool == ViewportTool::DrawArc
            || tool == ViewportTool::DrawBezier || tool == ViewportTool::DrawSpline;
        const bool editTool = tool == ViewportTool::MoveSelection || tool == ViewportTool::CopySelection
            || tool == ViewportTool::MirrorSelection || tool == ViewportTool::RotateSelection
            || tool == ViewportTool::SplitWire || tool == ViewportTool::TrimWire
            || tool == ViewportTool::ExtendWire || tool == ViewportTool::Coincident
            || tool == ViewportTool::Tangent || tool == ViewportTool::Curvature;
        if ((drawingTool && index != 0) || (editTool && index != 3)
            || (tool == ViewportTool::MoveGridOrigin && index != 0)
            || (tool == ViewportTool::Measure && index != 8)) {
            SetViewportTool(ViewportTool::Select);
        }
        UpdatePlateSplitPreview();
        UpdatePlateAssemblyGuidePreview();
        RefreshBeginnerGuide();
    });
    toolsLayout->addWidget(toolsTabs_, 1);
    toolsDock->setWidget(toolsPanel);
    addDockWidget(Qt::RightDockWidgetArea, toolsDock);
    toolsDock->setMinimumWidth(380);
    toolsDock->setMaximumWidth(460);

    statusBar()->showMessage(QStringLiteral("準備完了"));
    setStyleSheet(R"(
        QMainWindow { background: #eef0f2; }
        QDockWidget { color: #26323a; font-weight: 600; }
        QDockWidget::title { background: #e2e6e9; padding: 7px; text-align: left; }
        QTabWidget::pane { border: 1px solid #c7cdd2; background: #ffffff; }
        QTabBar::tab { padding: 8px 12px; background: #e8ebed; }
        QTabBar::tab:selected { background: #ffffff; color: #075f69; }
        QLineEdit, QComboBox, QDoubleSpinBox { min-height: 26px; border: 1px solid #aeb7be; background: #ffffff; padding: 1px 4px; }
        QPushButton { min-height: 30px; border: 1px solid #8d9aa3; background: #f6f7f8; padding: 3px 10px; }
        QPushButton:hover { background: #e8edef; }
        QPushButton#primaryButton { background: #087780; color: white; border: 1px solid #075f69; font-weight: 600; }
        QPushButton#primaryButton:hover { background: #09666e; }
        QPushButton#primaryButton:disabled { background: #d7dcdf; color: #7c868d; border-color: #bcc4c9; }
        QPushButton#workflowButton { min-height: 28px; padding: 2px 5px; font-weight: 600; }
        QPushButton#workflowButton:checked { background: #d7edef; color: #075f69; border: 2px solid #087780; }
        QGroupBox#beginnerGuide { border: 1px solid #9eabb3; margin-top: 9px; background: #ffffff; }
        QGroupBox#beginnerGuide::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #42515a; }
        QLabel#beginnerGuideTitle { color: #17242b; font-weight: 700; font-size: 14px; }
        QLabel#beginnerGuideNext { color: #075f69; background: #e4f2f3; border-left: 4px solid #087780; padding: 6px; font-weight: 600; }
        QLabel#beginnerGuideSteps { color: #34444d; }
        QLabel#beginnerGuideContext { color: #5d6970; background: #f1f3f4; padding: 5px; }
        QPushButton#guideNextButton { background: #087780; color: white; border-color: #075f69; font-weight: 600; }
        QToolButton#drawingToolButton { min-height: 38px; border: 1px solid #8d9aa3; background: #f6f7f8; padding: 3px 8px; }
        QToolButton#drawingToolButton:hover { background: #e8edef; }
        QToolButton#drawingToolButton:checked { background: #087780; color: white; border-color: #075f69; font-weight: 600; }
        QTreeWidget { border: 0; background: #fafbfb; }
        QTreeWidget::item { min-height: 25px; }
        QTreeWidget::item:selected { background: #cce5e7; color: #17242b; }
    )");
}

void MainWindow::BuildDrawingActions()
{
    selectToolAction_ = new QAction(QStringLiteral("選択"), this);
    pointToolAction_ = new QAction(QStringLiteral("作図点"), this);
    lineToolAction_ = new QAction(QStringLiteral("直線"), this);
    polylineToolAction_ = new QAction(QStringLiteral("ポリライン"), this);
    rectangleToolAction_ = new QAction(QStringLiteral("矩形"), this);
    circleToolAction_ = new QAction(QStringLiteral("円"), this);
    arcToolAction_ = new QAction(QStringLiteral("円弧"), this);
    bezierToolAction_ = new QAction(QStringLiteral("ベジェ"), this);
    splineToolAction_ = new QAction(QStringLiteral("スプライン"), this);
    moveToolAction_ = new QAction(QStringLiteral("移動"), this);
    copyToolAction_ = new QAction(QStringLiteral("コピー"), this);
    mirrorToolAction_ = new QAction(QStringLiteral("ミラー複製"), this);
    rotateToolAction_ = new QAction(QStringLiteral("回転"), this);
    splitToolAction_ = new QAction(QStringLiteral("分割"), this);
    trimToolAction_ = new QAction(QStringLiteral("トリム"), this);
    extendToolAction_ = new QAction(QStringLiteral("延長"), this);
    coincidentToolAction_ = new QAction(QStringLiteral("端点一致"), this);
    tangentToolAction_ = new QAction(QStringLiteral("接線接続"), this);
    curvatureToolAction_ = new QAction(QStringLiteral("曲率接続"), this);
    gridOriginToolAction_ = new QAction(QStringLiteral("画面で基準を合わせる"), this);
    removeCoincidentAction_ = new QAction(QStringLiteral("一致解除"), this);
    removeTangentAction_ = new QAction(QStringLiteral("滑らか解除"), this);
    measureToolAction_ = new QAction(QStringLiteral("測定"), this);
    joinWiresAction_ = new QAction(QStringLiteral("結合"), this);
    meetLinesAction_ = new QAction(QStringLiteral("2線を交点まで"), this);
    setReferenceAction_ = new QAction(QStringLiteral("基準線に設定"), this);
    clearReferenceAction_ = new QAction(QStringLiteral("基準解除"), this);
    moveToolAction_->setToolTip(QStringLiteral("選択したワイヤーを基準点と移動先の2点で移動"));
    copyToolAction_->setToolTip(QStringLiteral("選択したワイヤーを基準点と移動先の2点で複製"));
    mirrorToolAction_->setToolTip(QStringLiteral("選択したワイヤーを作図面上の2点軸で反転複製"));
    rotateToolAction_->setToolTip(QStringLiteral("中心・角度基準・回転先の3点で選択ワイヤーを回転"));
    splitToolAction_->setToolTip(QStringLiteral("選択した1本のワイヤーをクリック位置で分割"));
    trimToolAction_->setToolTip(QStringLiteral("3D画面で赤く表示された線・曲線部分をクリックして削除"));
    extendToolAction_->setToolTip(QStringLiteral("3D画面で線・曲線の端側をクリックし、最初の境界まで延長"));
    coincidentToolAction_->setToolTip(QStringLiteral("固定側、追従側の順にワイヤー端点を3D画面で指定"));
    tangentToolAction_->setToolTip(QStringLiteral("固定側の端点、追従するベジェ・B-spline・円弧端点の順に3D画面で指定"));
    curvatureToolAction_->setToolTip(QStringLiteral("固定側の端点、曲率まで追従するベジェ端点の順に3D画面で指定"));
    gridOriginToolAction_->setToolTip(
        QStringLiteral("点グリッドの点を掴み、作図上の合わせたい点までドラッグ"));
    removeCoincidentAction_->setToolTip(QStringLiteral("選択したワイヤーの端点一致と、それに付随する接線関係を解除"));
    removeTangentAction_->setToolTip(QStringLiteral("選択したワイヤーのG1/G2関係を解除し、端点一致は残す"));
    measureToolAction_->setToolTip(QStringLiteral("3D画面で2点距離、3点角度、接線・法線角を直接測定"));
    joinWiresAction_->setToolTip(QStringLiteral("端点がつながる直線・ポリラインを1本へ結合"));
    meetLinesAction_->setToolTip(QStringLiteral("選択した2直線をトリムまたは延長して交点で合わせる"));
    setReferenceAction_->setToolTip(QStringLiteral("選択した1本の直線を変形や平面作成の基準線にする"));
    clearReferenceAction_->setToolTip(QStringLiteral("現在の基準線を解除する"));
    pointToolAction_->setToolTip(QStringLiteral("交点・端点・格子点または任意位置に、スナップ基準として残る点を置く"));
    lineToolAction_->setToolTip(QStringLiteral("始点と終点を指定。Shiftで作図面の水平・垂直へ固定"));
    polylineToolAction_->setToolTip(QStringLiteral("点を続けて指定。Shiftで次の辺を水平・垂直へ固定"));
    rectangleToolAction_->setToolTip(QStringLiteral("対角2点を指定。Shiftで正方形へ固定"));
    arcToolAction_->setToolTip(QStringLiteral("3点、両端＋半径、または始点＋接線/法線方向で円弧を作図"));
    splineToolAction_->setToolTip(QStringLiteral("通過点を4点以上指定し、Enterまたは右クリックで完了"));
    clearReferenceAction_->setEnabled(false);

    selectToolAction_->setShortcut(Qt::Key_V);
    pointToolAction_->setShortcut(Qt::Key_D);
    lineToolAction_->setShortcut(Qt::Key_L);
    polylineToolAction_->setShortcut(Qt::Key_P);
    rectangleToolAction_->setShortcut(Qt::Key_R);
    circleToolAction_->setShortcut(Qt::Key_C);
    arcToolAction_->setShortcut(Qt::Key_A);
    bezierToolAction_->setShortcut(Qt::Key_B);
    splineToolAction_->setShortcut(Qt::Key_S);
    coincidentToolAction_->setShortcut(Qt::Key_I);
    tangentToolAction_->setShortcut(Qt::Key_T);
    curvatureToolAction_->setShortcut(QKeySequence(QStringLiteral("Shift+T")));
    measureToolAction_->setShortcut(Qt::Key_M);
    trimToolAction_->setShortcut(Qt::Key_X);
    extendToolAction_->setShortcut(Qt::Key_E);

    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    for (QAction* action : {
             selectToolAction_, pointToolAction_, lineToolAction_, polylineToolAction_, rectangleToolAction_,
             circleToolAction_, arcToolAction_, bezierToolAction_, splineToolAction_,
             moveToolAction_, copyToolAction_, mirrorToolAction_, rotateToolAction_, splitToolAction_,
             trimToolAction_, extendToolAction_, coincidentToolAction_, tangentToolAction_,
             curvatureToolAction_, measureToolAction_,
             gridOriginToolAction_}) {
        action->setCheckable(true);
        toolGroup->addAction(action);
    }
    selectToolAction_->setChecked(true);

    connect(selectToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::Select); });
    connect(pointToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawPoint); });
    connect(lineToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawLine); });
    connect(polylineToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawPolyline); });
    connect(rectangleToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawRectangle); });
    connect(circleToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawCircle); });
    connect(arcToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawArc); });
    connect(bezierToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawBezier); });
    connect(splineToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawSpline); });
    connect(moveToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::MoveSelection); });
    connect(copyToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::CopySelection); });
    connect(mirrorToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::MirrorSelection); });
    connect(rotateToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::RotateSelection); });
    connect(splitToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::SplitWire); });
    connect(trimToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::TrimWire); });
    connect(extendToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::ExtendWire); });
    connect(coincidentToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::Coincident); });
    connect(tangentToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::Tangent); });
    connect(curvatureToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::Curvature); });
    connect(gridOriginToolAction_, &QAction::triggered, this, [this] {
        SetViewportTool(ViewportTool::MoveGridOrigin);
    });
    connect(removeCoincidentAction_, &QAction::triggered, this, &MainWindow::RemoveSelectedCoincidences);
    connect(removeTangentAction_, &QAction::triggered, this, &MainWindow::RemoveSelectedTangencies);
    connect(measureToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::Measure); });
    connect(joinWiresAction_, &QAction::triggered, this, &MainWindow::JoinSelectedWires);
    connect(meetLinesAction_, &QAction::triggered, this, &MainWindow::ApplyMeetSelectedLines);
    connect(setReferenceAction_, &QAction::triggered, this, &MainWindow::SetReferenceFromSelection);
    connect(clearReferenceAction_, &QAction::triggered, this, &MainWindow::ClearReference);

    snapAction_ = new QAction(QStringLiteral("スナップ"), this);
    snapAction_->setCheckable(true);
    snapAction_->setChecked(true);
    snapAction_->setToolTip(QStringLiteral(
        "交点・作図点・端点・格子点へ吸着。Ctrlを押している間は完全解除。"
        "入力前の右クリックは近くの構造点を始点にします"));
    alignPlaneAction_ = new QAction(QStringLiteral("正対"), this);
    alignPlaneAction_->setToolTip(QStringLiteral("作図面を真正面から見る"));
    finishDrawingAction_ = new QAction(style()->standardIcon(QStyle::SP_DialogApplyButton), QStringLiteral("完了"), this);
    cancelDrawingAction_ = new QAction(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("取消"), this);
    finishDrawingAction_->setEnabled(false);
    cancelDrawingAction_->setEnabled(false);

    connect(alignPlaneAction_, &QAction::triggered, viewport_, &CadViewport::AlignToActiveWorkPlane);
    connect(snapAction_, &QAction::toggled, viewport_, &CadViewport::SetSnapEnabled);
    connect(finishDrawingAction_, &QAction::triggered, viewport_, &CadViewport::FinishDrawing);
    connect(cancelDrawingAction_, &QAction::triggered, viewport_, &CadViewport::CancelDrawing);
}

QWidget* MainWindow::BuildDrawingPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* planeLabel = new QLabel(QStringLiteral("作図面"));
    planeLabel->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(planeLabel);

    auto* planeRow = new QWidget;
    auto* planeLayout = new QHBoxLayout(planeRow);
    planeLayout->setContentsMargins(0, 0, 0, 0);
    planeLayout->setSpacing(6);
    activePlaneCombo_ = new QComboBox;
    activePlaneCombo_->setObjectName("activePlaneCombo");
    planeLayout->addWidget(activePlaneCombo_, 1);
    auto* alignButton = new QToolButton;
    alignButton->setDefaultAction(alignPlaneAction_);
    alignButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    planeLayout->addWidget(alignButton);
    layout->addWidget(planeRow);

    drawingStateLabel_ = new QLabel(QStringLiteral("選択"));
    drawingStateLabel_->setStyleSheet("color: #075f69; font-weight: 600; padding: 4px 0;");
    layout->addWidget(drawingStateLabel_);

    auto* toolGrid = new QGridLayout;
    toolGrid->setContentsMargins(0, 0, 0, 0);
    toolGrid->setHorizontalSpacing(6);
    toolGrid->setVerticalSpacing(6);
    const auto addToolButton = [&](QAction* action, int row, int column, int columnSpan = 1) {
        auto* button = new QToolButton;
        button->setObjectName("drawingToolButton");
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toolGrid->addWidget(button, row, column, 1, columnSpan);
    };
    addToolButton(selectToolAction_, 0, 0);
    addToolButton(lineToolAction_, 0, 1);
    addToolButton(polylineToolAction_, 1, 0);
    addToolButton(rectangleToolAction_, 1, 1);
    addToolButton(circleToolAction_, 2, 0);
    addToolButton(arcToolAction_, 2, 1);
    addToolButton(bezierToolAction_, 3, 0);
    addToolButton(splineToolAction_, 3, 1);
    addToolButton(pointToolAction_, 4, 0, 2);
    layout->addLayout(toolGrid);

    drawingConstruction_ = new QCheckBox(QStringLiteral("補助線として作図"));
    drawingConstruction_->setToolTip(QStringLiteral("スナップや寸法基準に使い、面や切断出力には含めない"));
    layout->addWidget(drawingConstruction_);

    drawingDimensionSection_ = new QWidget;
    auto* dimensionLayout = new QVBoxLayout(drawingDimensionSection_);
    dimensionLayout->setContentsMargins(0, 2, 0, 0);
    dimensionLayout->setSpacing(6);
    auto* dimensionLabel = new QLabel(QStringLiteral("実寸で確定"));
    dimensionLabel->setStyleSheet("font-weight: 600; color: #26323a;");
    dimensionLayout->addWidget(dimensionLabel);

    drawingDimensionStack_ = new QStackedWidget;
    QFormLayout* dimensionForm = nullptr;

    QWidget* segmentPage = MakeFormPage(dimensionForm);
    drawingLengthField_ = MakePositiveField(10.0);
    drawingLengthField_->setSuffix(QStringLiteral(" mm"));
    drawingAngleField_ = MakeNumberField(0.0);
    drawingAngleField_->setRange(-360.0, 360.0);
    drawingAngleField_->setDecimals(2);
    drawingAngleField_->setSuffix(QStringLiteral(" °"));
    drawingAngleField_->setToolTip(QStringLiteral("作図面の横方向を0°、縦方向を90°とする角度"));
    dimensionForm->addRow(QStringLiteral("長さ"), drawingLengthField_);
    dimensionForm->addRow(QStringLiteral("角度"), drawingAngleField_);
    drawingDimensionStack_->addWidget(segmentPage);

    QWidget* rectanglePage = MakeFormPage(dimensionForm);
    drawingWidthField_ = MakePositiveField(10.0);
    drawingWidthField_->setSuffix(QStringLiteral(" mm"));
    drawingHeightField_ = MakePositiveField(10.0);
    drawingHeightField_->setSuffix(QStringLiteral(" mm"));
    dimensionForm->addRow(QStringLiteral("幅"), drawingWidthField_);
    dimensionForm->addRow(QStringLiteral("高さ"), drawingHeightField_);
    drawingDimensionStack_->addWidget(rectanglePage);

    QWidget* circlePage = MakeFormPage(dimensionForm);
    drawingRadiusField_ = MakePositiveField(5.0);
    drawingRadiusField_->setSuffix(QStringLiteral(" mm"));
    dimensionForm->addRow(QStringLiteral("半径"), drawingRadiusField_);
    drawingDimensionStack_->addWidget(circlePage);

    auto* arcPage = new QWidget;
    auto* arcLayout = new QVBoxLayout(arcPage);
    arcLayout->setContentsMargins(0, 0, 0, 0);
    arcLayout->setSpacing(6);
    arcDrawingMode_ = new QComboBox;
    arcDrawingMode_->addItem(QStringLiteral("3点（始点・通過点・終点）"),
        static_cast<int>(ArcDrawingMode::ThreePoints));
    arcDrawingMode_->addItem(QStringLiteral("両端＋半径"),
        static_cast<int>(ArcDrawingMode::EndpointsRadius));
    arcDrawingMode_->addItem(QStringLiteral("始点＋接線/法線方向"),
        static_cast<int>(ArcDrawingMode::StartTangent));
    arcLayout->addWidget(arcDrawingMode_);
    auto* arcCommonForm = new QFormLayout;
    arcCommonForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    arcRadiusField_ = MakePositiveField(5.0);
    arcRadiusField_->setSuffix(QStringLiteral(" mm"));
    arcRadiusField_->setToolTip(QStringLiteral("数値または計算式を入力できます"));
    arcRadiusLabel_ = new QLabel(QStringLiteral("半径"));
    arcCommonForm->addRow(arcRadiusLabel_, arcRadiusField_);
    arcLayout->addLayout(arcCommonForm);
    arcParameterStack_ = new QStackedWidget;

    auto* threePointPage = new QLabel(QStringLiteral(
        "中央画面で始点、通過点、終点の順に指定します。"));
    threePointPage->setWordWrap(true);
    arcParameterStack_->addWidget(threePointPage);

    QWidget* endpointArcPage = MakeFormPage(dimensionForm);
    arcBulgeSide_ = new QComboBox;
    arcBulgeSide_->addItem(QStringLiteral("進行方向の左"), true);
    arcBulgeSide_->addItem(QStringLiteral("進行方向の右"), false);
    dimensionForm->addRow(QStringLiteral("膨らむ側"), arcBulgeSide_);
    arcParameterStack_->addWidget(endpointArcPage);

    QWidget* tangentArcPage = MakeFormPage(dimensionForm);
    arcDirectionBasis_ = new QComboBox;
    arcDirectionBasis_->addItem(QStringLiteral("接線角"), 0);
    arcDirectionBasis_->addItem(QStringLiteral("法線角"), 1);
    arcDirectionAngle_ = MakeNumberField(0.0);
    arcDirectionAngle_->setRange(-360.0, 360.0);
    arcDirectionAngle_->setDecimals(3);
    arcDirectionAngle_->setSuffix(QStringLiteral(" °"));
    arcDirectionAngle_->setToolTip(QStringLiteral("作図面の横方向を0°、縦方向を90°とする角度"));
    arcExtentMode_ = new QComboBox;
    arcExtentMode_->addItem(QStringLiteral("中心角"), 0);
    arcExtentMode_->addItem(QStringLiteral("円弧長"), 1);
    arcExtentLabel_ = new QLabel(QStringLiteral("中心角"));
    arcExtentValue_ = MakePositiveField(90.0);
    arcExtentValue_->setRange(0.001, 359.999);
    arcExtentValue_->setDecimals(3);
    arcExtentValue_->setSuffix(QStringLiteral(" °"));
    arcTurnSide_ = new QComboBox;
    arcTurnSide_->addItem(QStringLiteral("左へ曲がる"), 1.0);
    arcTurnSide_->addItem(QStringLiteral("右へ曲がる"), -1.0);
    dimensionForm->addRow(QStringLiteral("方向の種類"), arcDirectionBasis_);
    dimensionForm->addRow(QStringLiteral("方向角"), arcDirectionAngle_);
    dimensionForm->addRow(QStringLiteral("円弧量"), arcExtentMode_);
    dimensionForm->addRow(arcExtentLabel_, arcExtentValue_);
    dimensionForm->addRow(QStringLiteral("曲がる側"), arcTurnSide_);
    arcParameterStack_->addWidget(tangentArcPage);
    arcLayout->addWidget(arcParameterStack_);
    drawingDimensionStack_->addWidget(arcPage);
    dimensionLayout->addWidget(drawingDimensionStack_);

    drawingDimensionCommitButton_ = new QPushButton(QStringLiteral("寸法で確定"));
    drawingDimensionCommitButton_->setObjectName("primaryButton");
    drawingDimensionCommitButton_->setEnabled(false);
    drawingDimensionCommitButton_->setToolTip(QStringLiteral("始点を基準に入力した実寸で形を作成"));
    dimensionLayout->addWidget(drawingDimensionCommitButton_);
    layout->addWidget(drawingDimensionSection_);

    auto* snapRow = new QWidget;
    auto* snapLayout = new QHBoxLayout(snapRow);
    snapLayout->setContentsMargins(0, 2, 0, 0);
    snapLayout->setSpacing(6);
    auto* snapButton = new QToolButton;
    snapButton->setDefaultAction(snapAction_);
    snapButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    snapLayout->addWidget(snapButton);
    snapLayout->addWidget(new QLabel(QStringLiteral("主点間隔")));
    snapStepField_ = new ExpressionDoubleSpinBox;
    snapStepField_->setRange(0.01, 1000.0);
    snapStepField_->setDecimals(2);
    snapStepField_->setSingleStep(0.5);
    snapStepField_->setValue(1.0);
    snapStepField_->setSuffix(QStringLiteral(" mm"));
    snapStepField_->setToolTip(QStringLiteral("数値または計算式を入力できます。例: 1/4"));
    snapLayout->addWidget(snapStepField_, 1);
    layout->addWidget(snapRow);

    auto* gridBox = new QGroupBox(QStringLiteral("点グリッド"));
    auto* gridLayout = new QFormLayout(gridBox);
    gridLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    gridPointsVisible_ = new QCheckBox(QStringLiteral("表示"));
    gridPointsVisible_->setChecked(true);
    gridSubdivision_ = new QComboBox;
    gridSubdivision_->addItem(QStringLiteral("主点のみ"), 1);
    gridSubdivision_->addItem(QStringLiteral("1/2 間隔に副点"), 2);
    gridSubdivision_->addItem(QStringLiteral("1/3 間隔に副点"), 3);
    gridSubdivision_->addItem(QStringLiteral("1/4 間隔に副点"), 4);
    gridSubdivision_->setCurrentIndex(0);
    gridSubdivision_->setToolTip(QStringLiteral("大きい主点の間を小さい副点で分割"));
    gridOrigin_[0] = MakeNumberField(0.0);
    gridOrigin_[1] = MakeNumberField(0.0);
    for (QDoubleSpinBox* field : gridOrigin_) {
        field->setRange(-100000.0, 100000.0);
        field->setDecimals(3);
        field->setSuffix(QStringLiteral(" mm"));
    }
    auto* resetGridOrigin = new QPushButton(QStringLiteral("基準を 0, 0 に戻す"));
    auto* moveGridOrigin = new QToolButton;
    moveGridOrigin->setDefaultAction(gridOriginToolAction_);
    moveGridOrigin->setToolButtonStyle(Qt::ToolButtonTextOnly);
    moveGridOrigin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    gridLayout->addRow(gridPointsVisible_);
    gridLayout->addRow(QStringLiteral("副点"), gridSubdivision_);
    gridLayout->addRow(QStringLiteral("基準 X"), gridOrigin_[0]);
    gridLayout->addRow(QStringLiteral("基準 Y"), gridOrigin_[1]);
    gridLayout->addRow(moveGridOrigin);
    gridLayout->addRow(resetGridOrigin);
    layout->addWidget(gridBox);

    auto* commandRow = new QWidget;
    auto* commandLayout = new QHBoxLayout(commandRow);
    commandLayout->setContentsMargins(0, 0, 0, 0);
    commandLayout->setSpacing(6);
    auto* finishButton = new QToolButton;
    finishButton->setDefaultAction(finishDrawingAction_);
    finishButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* cancelButton = new QToolButton;
    cancelButton->setDefaultAction(cancelDrawingAction_);
    cancelButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    commandLayout->addWidget(finishButton, 1);
    commandLayout->addWidget(cancelButton, 1);
    layout->addWidget(commandRow);
    layout->addStretch(1);

    connect(activePlaneCombo_, &QComboBox::currentIndexChanged, this, [this] {
        RefreshActiveWorkPlane();
        viewport_->AlignToActiveWorkPlane();
    });
    connect(snapStepField_, &QDoubleSpinBox::valueChanged, viewport_, &CadViewport::SetSnapStep);
    connect(gridPointsVisible_, &QCheckBox::toggled, viewport_, &CadViewport::SetGridPointsVisible);
    connect(gridSubdivision_, &QComboBox::currentIndexChanged, this, [this] {
        viewport_->SetGridSubdivision(gridSubdivision_->currentData().toInt());
    });
    viewport_->SetGridSubdivision(gridSubdivision_->currentData().toInt());
    const auto updateGridOrigin = [this] {
        viewport_->SetGridOrigin(gridOrigin_[0]->value(), gridOrigin_[1]->value());
    };
    connect(gridOrigin_[0], &QDoubleSpinBox::valueChanged, this, updateGridOrigin);
    connect(gridOrigin_[1], &QDoubleSpinBox::valueChanged, this, updateGridOrigin);
    connect(resetGridOrigin, &QPushButton::clicked, this, [this] {
        gridOrigin_[0]->setValue(0.0);
        gridOrigin_[1]->setValue(0.0);
    });
    connect(drawingDimensionCommitButton_, &QPushButton::clicked, this, &MainWindow::CommitDrawingDimensions);
    connect(arcDrawingMode_, &QComboBox::currentIndexChanged, this, [this] {
        const auto mode = static_cast<ArcDrawingMode>(arcDrawingMode_->currentData().toInt());
        arcParameterStack_->setCurrentIndex(arcDrawingMode_->currentIndex());
        viewport_->SetArcDrawingMode(mode);
        UpdateArcConfiguration();
        UpdateDrawingPanel(viewport_->Tool(), viewport_->DrawingPointCount());
    });
    connect(arcRadiusField_, &QDoubleSpinBox::valueChanged, this, &MainWindow::UpdateArcConfiguration);
    connect(arcBulgeSide_, &QComboBox::currentIndexChanged, this, &MainWindow::UpdateArcConfiguration);
    connect(arcDirectionBasis_, &QComboBox::currentIndexChanged, this, &MainWindow::UpdateArcConfiguration);
    connect(arcDirectionAngle_, &QDoubleSpinBox::valueChanged, this, &MainWindow::UpdateArcConfiguration);
    connect(arcExtentMode_, &QComboBox::currentIndexChanged, this, &MainWindow::UpdateArcConfiguration);
    connect(arcExtentValue_, &QDoubleSpinBox::valueChanged, this, &MainWindow::UpdateArcConfiguration);
    connect(arcTurnSide_, &QComboBox::currentIndexChanged, this, &MainWindow::UpdateArcConfiguration);
    for (const QKeySequence key : {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)}) {
        auto* shortcut = new QShortcut(key, drawingDimensionSection_);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, &MainWindow::CommitDrawingDimensions);
    }
    UpdateArcConfiguration();
    UpdateDrawingPanel(ViewportTool::Select, 0);
    return panel;
}

QWidget* MainWindow::BuildPlanePanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    planeName_ = new QLineEdit("plane_1");
    planeMethod_ = new QComboBox;
    planeMethod_->addItems({
        QStringLiteral("標準平面"),
        QStringLiteral("点 + 法線"),
        QStringLiteral("3点"),
        QStringLiteral("既存平面からオフセット"),
        QStringLiteral("軸まわりに回転"),
    });
    form->addRow(QStringLiteral("名前"), planeName_);
    form->addRow(QStringLiteral("作り方"), planeMethod_);
    layout->addLayout(form);

    planeParameters_ = new QStackedWidget;
    QFormLayout* pageForm = nullptr;

    QWidget* standardPage = MakeFormPage(pageForm);
    standardPlane_ = new QComboBox;
    standardPlane_->addItems({QStringLiteral("上面 XY"), QStringLiteral("正面 XZ"), QStringLiteral("側面 YZ")});
    pageForm->addRow(QStringLiteral("向き"), standardPlane_);
    planeParameters_->addWidget(standardPage);

    QWidget* pointNormalPage = MakeFormPage(pageForm);
    pageForm->addRow(QStringLiteral("通過点"), MakeVector3Editor(pointNormalOrigin_));
    pageForm->addRow(QStringLiteral("法線"), MakeVector3Editor(pointNormalDirection_, {0.0, 0.0, 1.0}));
    pageForm->addRow(QStringLiteral("横方向"), MakeVector3Editor(pointNormalUAxis_, {1.0, 0.0, 0.0}));
    planeParameters_->addWidget(pointNormalPage);

    QWidget* threePointPage = MakeFormPage(pageForm);
    pageForm->addRow(QStringLiteral("点 A"), MakeVector3Editor(threePointA_));
    pageForm->addRow(QStringLiteral("点 B"), MakeVector3Editor(threePointB_, {10.0, 0.0, 0.0}));
    pageForm->addRow(QStringLiteral("点 C"), MakeVector3Editor(threePointC_, {0.0, 10.0, 0.0}));
    planeParameters_->addWidget(threePointPage);

    QWidget* offsetPage = MakeFormPage(pageForm);
    offsetSourcePlane_ = new QComboBox;
    pageForm->addRow(QStringLiteral("基準平面"), offsetSourcePlane_);
    planeParameters_->addWidget(offsetPage);

    QWidget* rotatePage = MakeFormPage(pageForm);
    rotateSourcePlane_ = new QComboBox;
    rotateAngle_ = MakeNumberField(30.0);
    rotateAngle_->setSuffix(QStringLiteral(" °"));
    pageForm->addRow(QStringLiteral("基準平面"), rotateSourcePlane_);
    pageForm->addRow(QStringLiteral("回転軸の点"), MakeVector3Editor(rotateAxisPoint_));
    pageForm->addRow(QStringLiteral("回転軸の向き"), MakeVector3Editor(rotateAxisDirection_, {1.0, 0.0, 0.0}));
    pageForm->addRow(QStringLiteral("基準角度"), rotateAngle_);
    planeParameters_->addWidget(rotatePage);

    layout->addWidget(planeParameters_);
    connect(planeMethod_, &QComboBox::currentIndexChanged, planeParameters_, &QStackedWidget::setCurrentIndex);

    auto* referenceGroup = new QGroupBox(QStringLiteral("基準線"));
    auto* referenceLayout = new QVBoxLayout(referenceGroup);
    planeReferenceLabel_ = new QLabel(QStringLiteral("未設定"));
    planeReferenceLabel_->setStyleSheet("color: #075f69; font-weight: 600;");
    referenceLayout->addWidget(planeReferenceLabel_);
    auto* referenceButtons = new QHBoxLayout;
    auto* setReferenceButton = new QToolButton;
    setReferenceButton->setDefaultAction(setReferenceAction_);
    setReferenceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* useReferenceButton = new QPushButton(QStringLiteral("回転軸に使用"));
    connect(useReferenceButton, &QPushButton::clicked, this, &MainWindow::UseReferenceForPlaneRotation);
    auto* clearReferenceButton = new QToolButton;
    clearReferenceButton->setDefaultAction(clearReferenceAction_);
    clearReferenceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    referenceButtons->addWidget(setReferenceButton, 1);
    referenceButtons->addWidget(useReferenceButton, 1);
    referenceButtons->addWidget(clearReferenceButton);
    referenceLayout->addLayout(referenceButtons);
    layout->addWidget(referenceGroup);

    auto* adjustment = new QGroupBox(QStringLiteral("共通調整"));
    auto* adjustmentForm = new QFormLayout(adjustment);
    planeOffset_ = MakeNumberField(0.0);
    planeOffset_->setSuffix(QStringLiteral(" mm"));
    planeTilt_ = MakeNumberField(0.0);
    planeTilt_->setRange(-360.0, 360.0);
    planeTilt_->setSuffix(QStringLiteral(" °"));
    adjustmentForm->addRow(QStringLiteral("法線方向オフセット"), planeOffset_);
    adjustmentForm->addRow(QStringLiteral("平面内X軸まわり角度"), planeTilt_);
    layout->addWidget(adjustment);

    auto* commandRow = new QHBoxLayout;
    auto* viewButton = new QPushButton(QStringLiteral("この向きで見る"));
    viewButton->setObjectName("primaryButton");
    connect(viewButton, &QPushButton::clicked, this, &MainWindow::AlignViewportFromPlaneInputs);
    commandRow->addWidget(viewButton, 1);
    auto* addButton = new QPushButton(QStringLiteral("平面を作成"));
    connect(addButton, &QPushButton::clicked, this, &MainWindow::AddWorkPlane);
    commandRow->addWidget(addButton, 1);
    layout->addLayout(commandRow);
    layout->addStretch(1);
    return panel;
}

QWidget* MainWindow::BuildWirePanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    wireName_ = new QLineEdit("wire_1");
    wireKind_ = new QComboBox;
    wireKind_->addItems({
        QStringLiteral("3D直線"),
        QStringLiteral("3Dベジェ曲線"),
        QStringLiteral("平面上の直線"),
        QStringLiteral("平面上の円"),
        QStringLiteral("平面上の円弧"),
        QStringLiteral("平面上のベジェ曲線"),
    });
    wirePlane_ = new QComboBox;
    wirePolicy_ = new QComboBox;
    wirePolicy_->addItems({
        QStringLiteral("平面拘束なし"),
        QStringLiteral("作業平面を編集基準に使用"),
        QStringLiteral("作業平面に固定"),
    });
    form->addRow(QStringLiteral("名前"), wireName_);
    form->addRow(QStringLiteral("種類"), wireKind_);
    form->addRow(QStringLiteral("作業平面"), wirePlane_);
    form->addRow(QStringLiteral("平面との関係"), wirePolicy_);
    layout->addLayout(form);

    wireParameters_ = new QStackedWidget;
    QFormLayout* pageForm = nullptr;

    QWidget* linePage = MakeFormPage(pageForm);
    pageForm->addRow(QStringLiteral("始点"), MakeVector3Editor(lineStart_));
    pageForm->addRow(QStringLiteral("終点"), MakeVector3Editor(lineEnd_, {10.0, 0.0, 0.0}));
    wireParameters_->addWidget(linePage);

    QWidget* bezierPage = MakeFormPage(pageForm);
    pageForm->addRow(QStringLiteral("始点"), MakeVector3Editor(bezierStart_));
    pageForm->addRow(QStringLiteral("制御点 1"), MakeVector3Editor(bezierControl1_, {3.0, 0.0, 3.0}));
    pageForm->addRow(QStringLiteral("制御点 2"), MakeVector3Editor(bezierControl2_, {7.0, 0.0, 3.0}));
    pageForm->addRow(QStringLiteral("終点"), MakeVector3Editor(bezierEnd_, {10.0, 0.0, 0.0}));
    wireParameters_->addWidget(bezierPage);

    QWidget* sketchLinePage = MakeFormPage(pageForm);
    pageForm->addRow(QStringLiteral("始点"), MakeVector2Editor(sketchLineStart_));
    pageForm->addRow(QStringLiteral("終点"), MakeVector2Editor(sketchLineEnd_, {10.0, 0.0}));
    wireParameters_->addWidget(sketchLinePage);

    QWidget* circlePage = MakeFormPage(pageForm);
    circleRadius_ = MakePositiveField(3.0);
    circleRadius_->setSuffix(QStringLiteral(" mm"));
    pageForm->addRow(QStringLiteral("中心"), MakeVector2Editor(circleCenter_));
    pageForm->addRow(QStringLiteral("半径"), circleRadius_);
    wireParameters_->addWidget(circlePage);

    QWidget* arcPage = MakeFormPage(pageForm);
    arcRadius_ = MakePositiveField(3.0);
    arcRadius_->setSuffix(QStringLiteral(" mm"));
    arcStartAngle_ = MakeNumberField(0.0);
    arcStartAngle_->setSuffix(QStringLiteral(" °"));
    arcSweepAngle_ = MakeNumberField(90.0);
    arcSweepAngle_->setRange(-360.0, 360.0);
    arcSweepAngle_->setSuffix(QStringLiteral(" °"));
    pageForm->addRow(QStringLiteral("中心"), MakeVector2Editor(arcCenter_));
    pageForm->addRow(QStringLiteral("半径"), arcRadius_);
    pageForm->addRow(QStringLiteral("開始角"), arcStartAngle_);
    pageForm->addRow(QStringLiteral("中心角"), arcSweepAngle_);
    wireParameters_->addWidget(arcPage);

    QWidget* sketchBezierPage = MakeFormPage(pageForm);
    pageForm->addRow(QStringLiteral("始点"), MakeVector2Editor(sketchBezierStart_));
    pageForm->addRow(QStringLiteral("制御点 1"), MakeVector2Editor(sketchBezierControl1_, {3.0, 3.0}));
    pageForm->addRow(QStringLiteral("制御点 2"), MakeVector2Editor(sketchBezierControl2_, {7.0, 3.0}));
    pageForm->addRow(QStringLiteral("終点"), MakeVector2Editor(sketchBezierEnd_, {10.0, 0.0}));
    wireParameters_->addWidget(sketchBezierPage);
    layout->addWidget(wireParameters_);

    connect(wireKind_, &QComboBox::currentIndexChanged, this, [this](int index) {
        wireParameters_->setCurrentIndex(index);
        const bool usesPlane = index >= 2;
        wirePlane_->setEnabled(usesPlane);
        if (usesPlane && wirePolicy_->currentIndex() == 0) {
            wirePolicy_->setCurrentIndex(1);
        } else if (!usesPlane) {
            wirePolicy_->setCurrentIndex(0);
        }
    });
    wirePlane_->setEnabled(false);

    auto* addButton = new QPushButton(QStringLiteral("ワイヤーを追加"));
    addButton->setObjectName("primaryButton");
    connect(addButton, &QPushButton::clicked, this, &MainWindow::AddWire);
    layout->addWidget(addButton);
    layout->addStretch(1);
    return panel;
}

QWidget* MainWindow::BuildEditPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* referenceLabel = new QLabel(QStringLiteral("変形の基準線"));
    referenceLabel->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(referenceLabel);
    transformReferenceLabel_ = new QLabel(QStringLiteral("未設定（ミラーは2点で軸を指定）"));
    transformReferenceLabel_->setStyleSheet("color: #075f69; font-weight: 600;");
    layout->addWidget(transformReferenceLabel_);
    auto* referenceRow = new QHBoxLayout;
    auto* setReferenceButton = new QToolButton;
    setReferenceButton->setDefaultAction(setReferenceAction_);
    setReferenceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* clearReferenceButton = new QToolButton;
    clearReferenceButton->setDefaultAction(clearReferenceAction_);
    clearReferenceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    referenceRow->addWidget(setReferenceButton, 1);
    referenceRow->addWidget(clearReferenceButton);
    layout->addLayout(referenceRow);

    auto* directLabel = new QLabel(QStringLiteral("3Dで直接変形"));
    directLabel->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(directLabel);
    auto* directGrid = new QGridLayout;
    directGrid->setContentsMargins(0, 0, 0, 0);
    directGrid->setHorizontalSpacing(6);
    directGrid->setVerticalSpacing(6);
    const auto addDirectButton = [&](QAction* action, int row, int column, int columnSpan = 1) {
        auto* button = new QToolButton;
        button->setObjectName("drawingToolButton");
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        directGrid->addWidget(button, row, column, 1, columnSpan);
    };
    addDirectButton(moveToolAction_, 0, 0);
    addDirectButton(copyToolAction_, 0, 1);
    addDirectButton(rotateToolAction_, 1, 0);
    addDirectButton(mirrorToolAction_, 1, 1);
    addDirectButton(splitToolAction_, 2, 0);
    addDirectButton(joinWiresAction_, 2, 1);
    addDirectButton(trimToolAction_, 3, 0);
    addDirectButton(extendToolAction_, 3, 1);
    addDirectButton(coincidentToolAction_, 4, 0);
    addDirectButton(removeCoincidentAction_, 4, 1);
    addDirectButton(tangentToolAction_, 5, 0);
    addDirectButton(curvatureToolAction_, 5, 1);
    addDirectButton(removeTangentAction_, 6, 0, 2);
    addDirectButton(meetLinesAction_, 7, 0, 2);
    layout->addLayout(directGrid);

    auto* offsetLabel = new QLabel(QStringLiteral("平行オフセット複製"));
    offsetLabel->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 4px;");
    layout->addWidget(offsetLabel);
    wireOffsetSelectionLabel_ = new QLabel(QStringLiteral("3D画面でワイヤーを選択"));
    wireOffsetSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(wireOffsetSelectionLabel_);
    auto* offsetRow = new QWidget;
    auto* offsetLayout = new QHBoxLayout(offsetRow);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
    offsetLayout->setSpacing(6);
    wireOffsetSide_ = new QComboBox;
    wireOffsetSide_->addItems({QStringLiteral("線の左"), QStringLiteral("線の右")});
    wireOffsetSide_->setToolTip(QStringLiteral("ワイヤーの始点から終点へ見た左右。紫線で結果を確認"));
    wireOffsetDistance_ = MakePositiveField(1.0);
    wireOffsetDistance_->setSuffix(QStringLiteral(" mm"));
    wireOffsetApplyButton_ = new QPushButton(QStringLiteral("複製"));
    wireOffsetApplyButton_->setEnabled(false);
    offsetLayout->addWidget(wireOffsetSide_);
    offsetLayout->addWidget(wireOffsetDistance_, 1);
    offsetLayout->addWidget(wireOffsetApplyButton_);
    layout->addWidget(offsetRow);

    auto* numericLabel = new QLabel(QStringLiteral("選択内容の数値編集"));
    numericLabel->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 4px;");
    layout->addWidget(numericLabel);

    editSelectionLabel_ = new QLabel(QStringLiteral("選択なし"));
    editSelectionLabel_->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(editSelectionLabel_);

    editParameters_ = new QStackedWidget;
    editParameters_->addWidget(new QLabel(QStringLiteral("選択なし")));

    QFormLayout* planeForm = nullptr;
    QWidget* planePage = MakeFormPage(planeForm);
    planeForm->addRow(QStringLiteral("原点"), MakeVector3Editor(editPlaneOrigin_));
    planeForm->addRow(QStringLiteral("法線"), MakeVector3Editor(editPlaneNormal_, {0.0, 0.0, 1.0}));
    planeForm->addRow(QStringLiteral("平面内 X"), MakeVector3Editor(editPlaneUAxis_, {1.0, 0.0, 0.0}));
    editParameters_->addWidget(planePage);

    auto* wirePage = new QWidget;
    auto* wireLayout = new QVBoxLayout(wirePage);
    wireLayout->setContentsMargins(0, 6, 0, 6);
    auto* metadataForm = new QFormLayout;
    editWireSourcePlane_ = new QComboBox;
    editWirePolicy_ = new QComboBox;
    editWirePolicy_->addItems({
        QStringLiteral("平面拘束なし"),
        QStringLiteral("作業平面を編集基準に使用"),
        QStringLiteral("作業平面に固定"),
    });
    metadataForm->addRow(QStringLiteral("作成元平面"), editWireSourcePlane_);
    metadataForm->addRow(QStringLiteral("平面との関係"), editWirePolicy_);
    editWireConstruction_ = new QCheckBox(QStringLiteral("補助線（面・出力から除外）"));
    metadataForm->addRow(QStringLiteral("用途"), editWireConstruction_);
    wireLayout->addLayout(metadataForm);

    editWireConstraintPanel_ = new QGroupBox(QStringLiteral("直線の寸法を保持"));
    auto* constraintLayout = new QFormLayout(editWireConstraintPanel_);
    constraintLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    constraintLayout->setContentsMargins(8, 8, 8, 8);

    auto* lengthRow = new QWidget;
    auto* lengthLayout = new QHBoxLayout(lengthRow);
    lengthLayout->setContentsMargins(0, 0, 0, 0);
    lengthLayout->setSpacing(6);
    editWireLockLength_ = new QCheckBox(QStringLiteral("固定"));
    editWireConstraintLength_ = MakePositiveField(10.0);
    editWireConstraintLength_->setSuffix(QStringLiteral(" mm"));
    lengthLayout->addWidget(editWireLockLength_);
    lengthLayout->addWidget(editWireConstraintLength_, 1);
    constraintLayout->addRow(QStringLiteral("長さ"), lengthRow);

    auto* angleRow = new QWidget;
    auto* angleLayout = new QHBoxLayout(angleRow);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    angleLayout->setSpacing(5);
    editWireLockAngle_ = new QCheckBox(QStringLiteral("固定"));
    editWireConstraintAngle_ = MakeNumberField(0.0);
    editWireConstraintAngle_->setRange(-360.0, 360.0);
    editWireConstraintAngle_->setSuffix(QStringLiteral(" °"));
    editWireConstraintAngle_->setToolTip(QStringLiteral("作業平面のU方向が0°、V方向が90°"));
    auto* horizontalButton = new QPushButton(QStringLiteral("水平 0°"));
    auto* verticalButton = new QPushButton(QStringLiteral("垂直 90°"));
    angleLayout->addWidget(editWireLockAngle_);
    angleLayout->addWidget(editWireConstraintAngle_, 1);
    angleLayout->addWidget(horizontalButton);
    angleLayout->addWidget(verticalButton);
    constraintLayout->addRow(QStringLiteral("平面内角度"), angleRow);
    wireLayout->addWidget(editWireConstraintPanel_);

    connect(editWireLockLength_, &QCheckBox::toggled, editWireConstraintLength_, &QWidget::setEnabled);
    connect(editWireLockAngle_, &QCheckBox::toggled, editWireConstraintAngle_, &QWidget::setEnabled);
    connect(horizontalButton, &QPushButton::clicked, this, [this] {
        editWireLockAngle_->setChecked(true);
        editWireConstraintAngle_->setValue(0.0);
    });
    connect(verticalButton, &QPushButton::clicked, this, [this] {
        editWireLockAngle_->setChecked(true);
        editWireConstraintAngle_->setValue(90.0);
    });
    editWireConstraintLength_->setEnabled(false);
    editWireConstraintAngle_->setEnabled(false);

    editWireGeometry_ = new QStackedWidget;
    editWirePointTable_ = new QTableWidget;
    editWirePointTable_->setColumnCount(3);
    editWirePointTable_->setHorizontalHeaderLabels({"X", "Y", "Z"});
    editWirePointTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    editWirePointTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    editWirePointTable_->setSelectionMode(QAbstractItemView::NoSelection);
    editWirePointTable_->setMinimumHeight(190);
    editWireGeometry_->addWidget(editWirePointTable_);

    QFormLayout* arcForm = nullptr;
    QWidget* arcPage = MakeFormPage(arcForm);
    auto* radiusRow = new QWidget;
    auto* radiusLayout = new QHBoxLayout(radiusRow);
    radiusLayout->setContentsMargins(0, 0, 0, 0);
    radiusLayout->setSpacing(6);
    editWireLockRadius_ = new QCheckBox(QStringLiteral("固定"));
    editArcRadius_ = MakePositiveField(1.0);
    editArcRadius_->setSuffix(QStringLiteral(" mm"));
    radiusLayout->addWidget(editWireLockRadius_);
    radiusLayout->addWidget(editArcRadius_, 1);
    editArcStartAngle_ = MakeNumberField(0.0);
    editArcStartAngle_->setSuffix(QStringLiteral(" °"));
    editArcSweepAngle_ = MakeNumberField(90.0);
    editArcSweepAngle_->setRange(-360.0, 360.0);
    editArcSweepAngle_->setSuffix(QStringLiteral(" °"));
    arcForm->addRow(QStringLiteral("中心"), MakeVector3Editor(editArcCenter_));
    arcForm->addRow(QStringLiteral("円の X 軸"), MakeVector3Editor(editArcUAxis_, {1.0, 0.0, 0.0}));
    arcForm->addRow(QStringLiteral("円の Y 軸"), MakeVector3Editor(editArcVAxis_, {0.0, 1.0, 0.0}));
    arcForm->addRow(QStringLiteral("半径"), radiusRow);
    arcForm->addRow(QStringLiteral("開始角"), editArcStartAngle_);
    arcForm->addRow(QStringLiteral("中心角"), editArcSweepAngle_);
    editWireGeometry_->addWidget(arcPage);
    wireLayout->addWidget(editWireGeometry_);
    editParameters_->addWidget(wirePage);
    layout->addWidget(editParameters_);

    editApplyButton_ = new QPushButton(QStringLiteral("変更を適用"));
    editApplyButton_->setObjectName("primaryButton");
    connect(editApplyButton_, &QPushButton::clicked, this, &MainWindow::ApplySelectedEdit);
    layout->addWidget(editApplyButton_);
    layout->addStretch(1);

    connect(wireOffsetDistance_, &QDoubleSpinBox::valueChanged, this, &MainWindow::UpdateWireOffsetPreview);
    connect(wireOffsetSide_, &QComboBox::currentIndexChanged, this, &MainWindow::UpdateWireOffsetPreview);
    connect(wireOffsetApplyButton_, &QPushButton::clicked, this, &MainWindow::ApplyWireOffset);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    return scroll;
}

QWidget* MainWindow::BuildMachiningPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    chamferName_ = new QLineEdit("chamfer_1");
    machiningType_ = new QComboBox;
    machiningType_->addItems({QStringLiteral("C面取り"), QStringLiteral("R丸め")});
    chamferFirstWire_ = new QComboBox;
    chamferSecondWire_ = new QComboBox;
    machiningPickFirstButton_ = new QPushButton(QStringLiteral("3D選択"));
    machiningPickSecondButton_ = new QPushButton(QStringLiteral("3D選択"));
    machiningPickFirstButton_->setCheckable(true);
    machiningPickSecondButton_->setCheckable(true);
    connect(machiningPickFirstButton_, &QPushButton::clicked, this, [this] { BeginMachiningPick(0); });
    connect(machiningPickSecondButton_, &QPushButton::clicked, this, [this] { BeginMachiningPick(1); });
    chamferFirstBranch_ = new QComboBox;
    chamferSecondBranch_ = new QComboBox;
    const QStringList branchChoices = {QStringLiteral("自動"), QStringLiteral("始点側"), QStringLiteral("終点側")};
    chamferFirstBranch_->addItems(branchChoices);
    chamferSecondBranch_->addItems(branchChoices);

    form->addRow(QStringLiteral("加工種類"), machiningType_);
    form->addRow(QStringLiteral("加工線の名前"), chamferName_);
    auto* firstPicker = new QWidget;
    auto* firstPickerLayout = new QHBoxLayout(firstPicker);
    firstPickerLayout->setContentsMargins(0, 0, 0, 0);
    firstPickerLayout->setSpacing(5);
    firstPickerLayout->addWidget(chamferFirstWire_, 1);
    firstPickerLayout->addWidget(machiningPickFirstButton_);
    form->addRow(QStringLiteral("直線 A"), firstPicker);
    form->addRow(QStringLiteral("A の残す側"), chamferFirstBranch_);
    auto* secondPicker = new QWidget;
    auto* secondPickerLayout = new QHBoxLayout(secondPicker);
    secondPickerLayout->setContentsMargins(0, 0, 0, 0);
    secondPickerLayout->setSpacing(5);
    secondPickerLayout->addWidget(chamferSecondWire_, 1);
    secondPickerLayout->addWidget(machiningPickSecondButton_);
    form->addRow(QStringLiteral("直線 B"), secondPicker);
    form->addRow(QStringLiteral("B の残す側"), chamferSecondBranch_);
    layout->addLayout(form);

    machiningValues_ = new QStackedWidget;
    QFormLayout* chamferForm = nullptr;
    QWidget* chamferPage = MakeFormPage(chamferForm);
    chamferFirstDistance_ = MakePositiveField(1.0);
    chamferSecondDistance_ = MakePositiveField(1.0);
    chamferFirstDistance_->setSuffix(QStringLiteral(" mm"));
    chamferSecondDistance_->setSuffix(QStringLiteral(" mm"));
    chamferForm->addRow(QStringLiteral("A の切戻し"), chamferFirstDistance_);
    chamferForm->addRow(QStringLiteral("B の切戻し"), chamferSecondDistance_);
    machiningValues_->addWidget(chamferPage);

    QFormLayout* filletForm = nullptr;
    QWidget* filletPage = MakeFormPage(filletForm);
    filletRadius_ = MakePositiveField(1.0);
    filletRadius_->setSuffix(QStringLiteral(" mm"));
    filletForm->addRow(QStringLiteral("半径"), filletRadius_);
    machiningValues_->addWidget(filletPage);
    layout->addWidget(machiningValues_);

    machiningApplyButton_ = new QPushButton(QStringLiteral("C面取りを作成"));
    machiningApplyButton_->setObjectName("primaryButton");
    connect(machiningApplyButton_, &QPushButton::clicked, this, [this] {
        if (machiningType_->currentIndex() == 0) {
            ApplyLineChamfer();
        } else {
            ApplyLineFillet();
        }
    });
    connect(machiningType_, &QComboBox::currentIndexChanged, this, [this](int index) {
        machiningValues_->setCurrentIndex(index);
        machiningApplyButton_->setText(index == 0 ? QStringLiteral("C面取りを作成") : QStringLiteral("R丸めを作成"));
        chamferName_->setText(index == 0 ? SuggestedChamferName() : SuggestedFilletName());
    });
    layout->addWidget(machiningApplyButton_);
    layout->addStretch(1);
    return panel;
}

QWidget* MainWindow::BuildSurfacePanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* createTitle = new QLabel(QStringLiteral("ワイヤーから面"));
    createTitle->setProperty("manualAnchor", QStringLiteral("surfaceCreate"));
    createTitle->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(createTitle);

    auto* createForm = new QFormLayout;
    createForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    surfaceType_ = new QComboBox;
    surfaceType_->addItems({
        QStringLiteral("閉じた輪郭から平面"),
        QStringLiteral("2断面から曲面"),
        QStringLiteral("3断面以上からロフト"),
    });
    surfaceName_ = new QLineEdit(QStringLiteral("surface_1"));
    createForm->addRow(QStringLiteral("作り方"), surfaceType_);
    createForm->addRow(QStringLiteral("面の名前"), surfaceName_);
    layout->addLayout(createForm);

    surfaceSelectionLabel_ = new QLabel(QStringLiteral("選択: ワイヤー0本"));
    surfaceSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(surfaceSelectionLabel_);

    auto* createButton = new QPushButton(QStringLiteral("選択ワイヤーから面を作成"));
    createButton->setObjectName("primaryButton");
    connect(createButton, &QPushButton::clicked, this, &MainWindow::CreateSurfaceFromSelection);
    layout->addWidget(createButton);

    auto* projectionTitle = new QLabel(QStringLiteral("平面図を面へ投影"));
    projectionTitle->setProperty("manualAnchor", QStringLiteral("surfaceProjection"));
    projectionTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(projectionTitle);

    auto* projectionForm = new QFormLayout;
    projectionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    projectionSurface_ = new QComboBox;
    projectionPlane_ = new QComboBox;
    projectionForm->addRow(QStringLiteral("投影先の面"), projectionSurface_);
    projectionForm->addRow(QStringLiteral("平面図"), projectionPlane_);
    layout->addLayout(projectionForm);

    projectionSelectionLabel_ = new QLabel(QStringLiteral("投影するワイヤー: 0本"));
    projectionSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(projectionSelectionLabel_);

    auto* projectButton = new QPushButton(QStringLiteral("選択ワイヤーを面へ投影"));
    projectButton->setObjectName("primaryButton");
    connect(projectButton, &QPushButton::clicked, this, &MainWindow::ProjectSelectedWiresToSurface);
    layout->addWidget(projectButton);

    auto* lightCaseBox = new QGroupBox(QStringLiteral("飛び出すライトケース"));
    lightCaseBox->setObjectName(QStringLiteral("lightCaseSection"));
    lightCaseBox->setProperty("manualAnchor", QStringLiteral("lightCase"));
    auto* lightCaseLayout = new QVBoxLayout(lightCaseBox);
    lightCaseLayout->setSpacing(8);

    lightCaseSelectionLabel_ = new QLabel(QStringLiteral("選択: 最前面の閉じた輪郭0本 / 接続先0個"));
    lightCaseSelectionLabel_->setWordWrap(true);
    lightCaseSelectionLabel_->setStyleSheet("color: #5c6670;");
    lightCaseLayout->addWidget(lightCaseSelectionLabel_);

    auto* lightCaseForm = new QFormLayout;
    lightCaseForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    lightCaseRootName_ = new QLineEdit(QStringLiteral("light_root_1"));
    lightCaseSurfaceName_ = new QLineEdit(QStringLiteral("light_case_1"));
    lightCaseDirectionMode_ = new QComboBox;
    lightCaseDirectionMode_->addItems({
        QStringLiteral("最前面輪郭の法線（自動）"),
        QStringLiteral("設定した基準線と平行"),
        QStringLiteral("任意方向 XYZ"),
    });
    lightCaseDirectionEditor_ = MakeVector3Editor(lightCaseDirection_, {0.0, 0.0, -1.0});
    lightCaseDirectionEditor_->setEnabled(false);
    lightCaseForm->addRow(QStringLiteral("根元ワイヤー名"), lightCaseRootName_);
    lightCaseForm->addRow(QStringLiteral("ケース側面名"), lightCaseSurfaceName_);
    lightCaseForm->addRow(QStringLiteral("伸ばす方向"), lightCaseDirectionMode_);
    lightCaseForm->addRow(QStringLiteral("XYZ（任意時）"), lightCaseDirectionEditor_);
    lightCaseLayout->addLayout(lightCaseForm);

    auto* referenceRow = new QWidget;
    auto* referenceLayout = new QHBoxLayout(referenceRow);
    referenceLayout->setContentsMargins(0, 0, 0, 0);
    referenceLayout->setSpacing(6);
    lightCaseReferenceLabel_ = new QLabel(QStringLiteral("基準線: 未設定"));
    lightCaseReferenceLabel_->setStyleSheet("color: #5c6670;");
    auto* setLightCaseReference = new QPushButton(QStringLiteral("選択直線を基準に設定"));
    setLightCaseReference->setToolTip(QStringLiteral("斜め方向の基準にする直線を1本選択して押します"));
    connect(setLightCaseReference, &QPushButton::clicked, this, &MainWindow::SetReferenceFromSelection);
    referenceLayout->addWidget(lightCaseReferenceLabel_, 1);
    referenceLayout->addWidget(setLightCaseReference);
    lightCaseLayout->addWidget(referenceRow);

    auto* createLightCaseButton = new QPushButton(QStringLiteral("根元ワイヤーとケース側面を作る"));
    createLightCaseButton->setObjectName("primaryButton");
    createLightCaseButton->setToolTip(QStringLiteral("閉じた最前面輪郭1本と、接続先の面または板1枚を3D画面で選択します"));
    connect(createLightCaseButton, &QPushButton::clicked, this, &MainWindow::CreateProtrudingLightCase);
    connect(lightCaseDirectionMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        lightCaseDirectionEditor_->setEnabled(index == 2);
    });
    lightCaseLayout->addWidget(createLightCaseButton);
    layout->addWidget(lightCaseBox);

    auto* plateTitle = new QLabel(QStringLiteral("ワイヤー / 面から3D板を作る"));
    plateTitle->setProperty("manualAnchor", QStringLiteral("plateCreate"));
    plateTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(plateTitle);

    auto* plateForm = new QFormLayout;
    plateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    plateName_ = new QLineEdit(QStringLiteral("plate_1"));
    plateSurface_ = new QComboBox;
    plateThickness_ = MakePositiveField(0.5);
    plateThickness_->setSuffix(QStringLiteral(" mm"));
    plateVariableThickness_ = new QCheckBox(QStringLiteral("終端まで板厚を変化"));
    plateEndThickness_ = MakePositiveField(0.5);
    plateEndThickness_->setSuffix(QStringLiteral(" mm"));
    plateEndThickness_->setEnabled(false);
    plateDirection_ = new QComboBox;
    plateDirection_->addItem(QStringLiteral("+側へ（法線矢印側）"), static_cast<int>(PlateThicknessDirection::Positive));
    plateDirection_->addItem(QStringLiteral("中央（両側へ半分）"), static_cast<int>(PlateThicknessDirection::Centered));
    plateDirection_->addItem(QStringLiteral("-側へ（矢印と反対）"), static_cast<int>(PlateThicknessDirection::Negative));
    plateMaterial_ = new QComboBox;
    plateMaterial_->addItem(QStringLiteral("プラ板"), QStringLiteral("styrene"));
    plateMaterial_->addItem(QStringLiteral("紙・厚紙"), QStringLiteral("paper"));
    plateMaterial_->addItem(QStringLiteral("真鍮板"), QStringLiteral("brass"));
    plateMaterial_->addItem(QStringLiteral("その他"), QStringLiteral("other"));
    plateForm->addRow(QStringLiteral("板の名前"), plateName_);
    plateForm->addRow(QStringLiteral("元の面"), plateSurface_);
    plateForm->addRow(QStringLiteral("始端の板厚"), plateThickness_);
    plateForm->addRow(plateVariableThickness_);
    plateForm->addRow(QStringLiteral("終端の板厚"), plateEndThickness_);
    plateForm->addRow(QStringLiteral("厚み方向"), plateDirection_);
    plateForm->addRow(QStringLiteral("材質"), plateMaterial_);
    layout->addLayout(plateForm);

    connect(plateVariableThickness_, &QCheckBox::toggled, plateEndThickness_, &QWidget::setEnabled);
    connect(plateThickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!plateVariableThickness_->isChecked()) {
            plateEndThickness_->setValue(value);
        }
    });

    auto* wirePlateButton = new QPushButton(QStringLiteral("選択ワイヤーから3D板を直接作る"));
    wirePlateButton->setObjectName("primaryButton");
    wirePlateButton->setToolTip(QStringLiteral("1閉輪郭は平板、2断面は曲面板、3断面以上はロフト板"));
    connect(wirePlateButton, &QPushButton::clicked, this, &MainWindow::CreatePlateFromSelectedWires);
    layout->addWidget(wirePlateButton);

    auto* plateButton = new QPushButton(QStringLiteral("この面を板材にする"));
    plateButton->setObjectName("primaryButton");
    connect(plateButton, &QPushButton::clicked, this, &MainWindow::CreatePlateFromSurface);
    layout->addWidget(plateButton);

    auto* plateUpdateButton = new QPushButton(QStringLiteral("選択中の板材へ設定"));
    connect(plateUpdateButton, &QPushButton::clicked, this, &MainWindow::UpdateSelectedPlate);
    layout->addWidget(plateUpdateButton);

    auto* offsetWireBox = new QGroupBox(QStringLiteral("板厚位置にワイヤーを作る"));
    offsetWireBox->setObjectName(QStringLiteral("plateOffsetSection"));
    offsetWireBox->setProperty("manualAnchor", QStringLiteral("plateOffset"));
    auto* offsetWireLayout = new QVBoxLayout(offsetWireBox);
    plateOffsetSelectionLabel_ = new QLabel(QStringLiteral("選択: 板材0枚 / 投影ワイヤー0本"));
    plateOffsetSelectionLabel_->setStyleSheet("color: #5c6670;");
    plateOffsetLayer_ = new QComboBox;
    plateOffsetLayer_->addItem(QStringLiteral("+側表面（法線矢印側）"), 1.0);
    plateOffsetLayer_->addItem(QStringLiteral("板厚の中央"), 0.5);
    plateOffsetLayer_->addItem(QStringLiteral("-側表面（矢印と反対）"), 0.0);
    auto* offsetWireButton = new QPushButton(QStringLiteral("選択輪郭をこの位置へ複製"));
    connect(offsetWireButton, &QPushButton::clicked, this, &MainWindow::CreatePlateOffsetWires);
    offsetWireLayout->addWidget(plateOffsetSelectionLabel_);
    offsetWireLayout->addWidget(plateOffsetLayer_);
    offsetWireLayout->addWidget(offsetWireButton);
    layout->addWidget(offsetWireBox);

    auto* jigTitle = new QLabel(QStringLiteral("曲面から成形治具"));
    jigTitle->setProperty("manualAnchor", QStringLiteral("surfaceJig"));
    jigTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(jigTitle);

    auto* jigForm = new QFormLayout;
    jigForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    jigName_ = new QLineEdit(QStringLiteral("jig_1"));
    jigSurface_ = new QComboBox;
    jigSide_ = new QComboBox;
    jigSide_->addItem(QStringLiteral("外側の型"), static_cast<int>(JigSide::Positive));
    jigSide_->addItem(QStringLiteral("内側の型"), static_cast<int>(JigSide::Negative));
    jigClearance_ = MakeNumberField(0.15);
    jigClearance_->setRange(0.0, 20.0);
    jigClearance_->setDecimals(3);
    jigClearance_->setSingleStep(0.05);
    jigClearance_->setSuffix(QStringLiteral(" mm"));
    jigThickness_ = MakePositiveField(3.0);
    jigThickness_->setSuffix(QStringLiteral(" mm"));
    jigMinimumWall_ = MakePositiveField(1.2);
    jigMinimumWall_->setSuffix(QStringLiteral(" mm"));
    jigForm->addRow(QStringLiteral("治具の名前"), jigName_);
    jigForm->addRow(QStringLiteral("元の面"), jigSurface_);
    jigForm->addRow(QStringLiteral("型の側"), jigSide_);
    jigForm->addRow(QStringLiteral("成形の隙間"), jigClearance_);
    jigForm->addRow(QStringLiteral("治具の厚み"), jigThickness_);
    jigForm->addRow(QStringLiteral("必要最小肉厚"), jigMinimumWall_);
    layout->addLayout(jigForm);

    jigAnalysisLabel_ = new QLabel(QStringLiteral("厚みを設定して治具を作成します"));
    jigAnalysisLabel_->setWordWrap(true);
    jigAnalysisLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(jigAnalysisLabel_);

    auto* jigCreateButton = new QPushButton(QStringLiteral("この面から成形治具を作る"));
    jigCreateButton->setObjectName("primaryButton");
    connect(jigCreateButton, &QPushButton::clicked, this, &MainWindow::CreateSurfaceJig);
    layout->addWidget(jigCreateButton);
    auto* jigUpdateButton = new QPushButton(QStringLiteral("選択中の治具へ設定"));
    connect(jigUpdateButton, &QPushButton::clicked, this, &MainWindow::UpdateSelectedBody);
    layout->addWidget(jigUpdateButton);

    auto* openingTitle = new QLabel(QStringLiteral("板材に開口"));
    openingTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(openingTitle);
    plateOpeningSelectionLabel_ = new QLabel(QStringLiteral("選択: 板材0枚 / 閉じた投影輪郭0本"));
    plateOpeningSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(plateOpeningSelectionLabel_);

    auto* openingButtons = new QWidget;
    openingButtons->setObjectName(QStringLiteral("plateOpeningSection"));
    openingButtons->setProperty("manualAnchor", QStringLiteral("plateOpening"));
    auto* openingButtonLayout = new QHBoxLayout(openingButtons);
    openingButtonLayout->setContentsMargins(0, 0, 0, 0);
    openingButtonLayout->setSpacing(6);
    auto* addOpeningButton = new QPushButton(QStringLiteral("開口に追加"));
    addOpeningButton->setObjectName("primaryButton");
    connect(addOpeningButton, &QPushButton::clicked, this, &MainWindow::AddSelectedPlateOpenings);
    auto* removeOpeningButton = new QPushButton(QStringLiteral("開口から外す"));
    connect(removeOpeningButton, &QPushButton::clicked, this, &MainWindow::RemoveSelectedPlateOpenings);
    openingButtonLayout->addWidget(addOpeningButton, 1);
    openingButtonLayout->addWidget(removeOpeningButton, 1);
    layout->addWidget(openingButtons);

    auto* reliefTitle = new QLabel(QStringLiteral("展開時の切れ目"));
    reliefTitle->setProperty("manualAnchor", QStringLiteral("plateRelief"));
    reliefTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(reliefTitle);
    plateReliefSelectionLabel_ = new QLabel(QStringLiteral("選択: 板材0枚 / 投影ワイヤー0本"));
    plateReliefSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(plateReliefSelectionLabel_);
    auto* reliefButtons = new QWidget;
    auto* reliefButtonLayout = new QHBoxLayout(reliefButtons);
    reliefButtonLayout->setContentsMargins(0, 0, 0, 0);
    reliefButtonLayout->setSpacing(6);
    auto* addReliefButton = new QPushButton(QStringLiteral("切れ目に追加"));
    addReliefButton->setObjectName("primaryButton");
    addReliefButton->setToolTip(QStringLiteral("板材と、その面へ投影した開いた線を3D画面で選択"));
    auto* removeReliefButton = new QPushButton(QStringLiteral("切れ目から外す"));
    connect(addReliefButton, &QPushButton::clicked, this, &MainWindow::AddSelectedPlateReliefCuts);
    connect(removeReliefButton, &QPushButton::clicked, this, &MainWindow::RemoveSelectedPlateReliefCuts);
    reliefButtonLayout->addWidget(addReliefButton, 1);
    reliefButtonLayout->addWidget(removeReliefButton, 1);
    layout->addWidget(reliefButtons);

    auto* splitLineTitle = new QLabel(QStringLiteral("展開片の分割線"));
    splitLineTitle->setProperty("manualAnchor", QStringLiteral("plateSplitLine"));
    splitLineTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(splitLineTitle);
    plateSplitLineSelectionLabel_ = new QLabel(QStringLiteral("選択: 板材0枚 / 投影ワイヤー0本"));
    plateSplitLineSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(plateSplitLineSelectionLabel_);
    auto* splitLineButtons = new QWidget;
    auto* splitLineButtonLayout = new QHBoxLayout(splitLineButtons);
    splitLineButtonLayout->setContentsMargins(0, 0, 0, 0);
    splitLineButtonLayout->setSpacing(6);
    auto* addSplitLineButton = new QPushButton(QStringLiteral("分割線にする"));
    addSplitLineButton->setObjectName("primaryButton");
    addSplitLineButton->setToolTip(QStringLiteral("板材と、その面へ投影した線を3D画面で選択"));
    auto* removeSplitLineButton = new QPushButton(QStringLiteral("分割線を解除"));
    connect(addSplitLineButton, &QPushButton::clicked, this, &MainWindow::AddSelectedPlateSplitLines);
    connect(removeSplitLineButton, &QPushButton::clicked, this, &MainWindow::RemoveSelectedPlateSplitLines);
    splitLineButtonLayout->addWidget(addSplitLineButton, 1);
    splitLineButtonLayout->addWidget(removeSplitLineButton, 1);
    layout->addWidget(splitLineButtons);

    auto* splitTitle = new QLabel(QStringLiteral("板材を分割"));
    splitTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(splitTitle);
    plateSplitSelectionLabel_ = new QLabel(QStringLiteral("選択: 板材0枚"));
    plateSplitSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(plateSplitSelectionLabel_);

    auto* splitForm = new QFormLayout;
    splitForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    plateSplitAxis_ = new QComboBox;
    plateSplitAxis_->addItem(QStringLiteral("断面内（屋根・側面方向）"), static_cast<int>(PlateSplitAxis::U));
    plateSplitAxis_->addItem(QStringLiteral("長手方向（前後方向）"), static_cast<int>(PlateSplitAxis::V));
    splitForm->addRow(QStringLiteral("分割方向"), plateSplitAxis_);

    auto* splitPositionControl = new QWidget;
    auto* splitPositionLayout = new QHBoxLayout(splitPositionControl);
    splitPositionLayout->setContentsMargins(0, 0, 0, 0);
    splitPositionLayout->setSpacing(7);
    plateSplitSlider_ = new QSlider(Qt::Horizontal);
    plateSplitSlider_->setRange(10, 990);
    plateSplitSlider_->setValue(500);
    plateSplitPosition_ = new ExpressionDoubleSpinBox;
    plateSplitPosition_->setRange(1.0, 99.0);
    plateSplitPosition_->setDecimals(1);
    plateSplitPosition_->setSingleStep(1.0);
    plateSplitPosition_->setSuffix(QStringLiteral(" %"));
    plateSplitPosition_->setValue(50.0);
    plateSplitPosition_->setFixedWidth(92);
    splitPositionLayout->addWidget(plateSplitSlider_, 1);
    splitPositionLayout->addWidget(plateSplitPosition_);
    splitForm->addRow(QStringLiteral("分割位置"), splitPositionControl);
    layout->addLayout(splitForm);

    connect(plateSplitAxis_, &QComboBox::currentIndexChanged, this, [this] {
        UpdatePlateSplitPreview();
    });
    connect(plateSplitSlider_, &QSlider::valueChanged, this, [this](int value) {
        const QSignalBlocker blocker(plateSplitPosition_);
        plateSplitPosition_->setValue(static_cast<double>(value) / 10.0);
        UpdatePlateSplitPreview();
    });
    connect(plateSplitPosition_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const QSignalBlocker blocker(plateSplitSlider_);
        plateSplitSlider_->setValue(static_cast<int>(std::lround(value * 10.0)));
        UpdatePlateSplitPreview();
    });

    auto* splitButton = new QPushButton(QStringLiteral("選択中の板材を2分割"));
    splitButton->setObjectName("primaryButton");
    splitButton->setProperty("plateSplitAction", true);
    splitButton->setProperty("manualAnchor", QStringLiteral("plateSplit"));
    connect(splitButton, &QPushButton::clicked, this, &MainWindow::SplitSelectedPlate);
    layout->addWidget(splitButton);
    layout->addStretch(1);

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(panel);
    return scrollArea;
}

QWidget* MainWindow::BuildOutputPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("作業平面の1:1図面"));
    title->setProperty("manualAnchor", QStringLiteral("planarOutput"));
    title->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(title);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    exportPlane_ = new QComboBox;
    exportScope_ = new QComboBox;
    exportScope_->addItems({QStringLiteral("出力面上の全ワイヤー"), QStringLiteral("選択したワイヤーのみ")});
    form->addRow(QStringLiteral("出力面"), exportPlane_);
    form->addRow(QStringLiteral("対象"), exportScope_);
    form->addRow(QStringLiteral("寸法"), new QLabel(QStringLiteral("mm / 1:1")));
    layout->addLayout(form);

    exportSummary_ = new QLabel(QStringLiteral("0本"));
    exportSummary_->setStyleSheet("color: #5c6670;");
    layout->addWidget(exportSummary_);

    auto* svgButton = new QPushButton(QStringLiteral("SVGを保存"));
    svgButton->setObjectName("primaryButton");
    svgButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    auto* dxfButton = new QPushButton(QStringLiteral("DXFを保存"));
    dxfButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(svgButton, &QPushButton::clicked, this, [this] { ExportPlanar(false); });
    connect(dxfButton, &QPushButton::clicked, this, [this] { ExportPlanar(true); });
    connect(exportPlane_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    connect(exportScope_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    layout->addWidget(svgButton);
    layout->addWidget(dxfButton);

    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    auto* plateTitle = new QLabel(QStringLiteral("選択板材の1:1展開図"));
    plateTitle->setProperty("manualAnchor", QStringLiteral("plateFlatPattern"));
    plateTitle->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(plateTitle);

    plateFlatPatternSummary_ = new QLabel(QStringLiteral("選択板材: なし"));
    plateFlatPatternSummary_->setWordWrap(true);
    plateFlatPatternSummary_->setStyleSheet("color: #5c6670;");
    layout->addWidget(plateFlatPatternSummary_);

    auto* flatModelForm = new QFormLayout;
    flatModelForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    plateFlatPatternName_ = new QLineEdit(QStringLiteral("developed_1"));
    plateFlatPatternPlane_ = new QComboBox;
    plateFlatPatternAutoRelief_ = new QCheckBox(QStringLiteral("自動切れ込み／分割を使う"));
    plateFlatPatternAutoRelief_->setObjectName(QStringLiteral("plateFlatPatternAutoRelief"));
    plateFlatPatternAutoRelief_->setToolTip(
        QStringLiteral("二方向に曲がる面を、紙や薄板で組める展開形状へ変換"));
    plateFlatPatternReliefStyle_ = new QComboBox;
    plateFlatPatternReliefStyle_->addItem(
        QStringLiteral("帯に完全分割（最大精度）"),
        static_cast<int>(AutomaticReliefStyle::SplitPieces));
    plateFlatPatternReliefStyle_->addItem(
        QStringLiteral("V字切れ込み（一体板）"),
        static_cast<int>(AutomaticReliefStyle::VNotch));
    plateFlatPatternReliefStyle_->addItem(
        QStringLiteral("先端R付きV字（一体板）"),
        static_cast<int>(AutomaticReliefStyle::RoundedVNotch));
    plateFlatPatternReliefStyle_->setCurrentIndex(2);
    plateFlatPatternReliefStyle_->setToolTip(
        QStringLiteral("完全分割は精度優先、V字は部材をつないだまま丸みを寄せます"));
    plateFlatPatternFidelity_ = new QSlider(Qt::Horizontal);
    plateFlatPatternFidelity_->setRange(1, 10);
    plateFlatPatternFidelity_->setValue(5);
    plateFlatPatternFidelity_->setTickPosition(QSlider::TicksBelow);
    plateFlatPatternFidelity_->setTickInterval(1);
    plateFlatPatternFidelityLabel_ = new QLabel(QStringLiteral("5 / 10（標準）"));
    auto* fidelityControl = new QWidget;
    auto* fidelityLayout = new QHBoxLayout(fidelityControl);
    fidelityLayout->setContentsMargins(0, 0, 0, 0);
    fidelityLayout->setSpacing(7);
    fidelityLayout->addWidget(plateFlatPatternFidelity_, 1);
    fidelityLayout->addWidget(plateFlatPatternFidelityLabel_);
    plateFlatPatternReliefSpacing_ = MakePositiveField(8.0);
    plateFlatPatternReliefSpacing_->setRange(1.0, 100.0);
    plateFlatPatternReliefSpacing_->setSingleStep(1.0);
    plateFlatPatternReliefSpacing_->setSuffix(QStringLiteral(" mm"));
    plateFlatPatternReliefSpacing_->setToolTip(
        QStringLiteral("小さくすると切れ込みが増え、丸い角を細かく追従します"));
    plateFlatPatternReliefDepth_ = MakePositiveField(55.0);
    plateFlatPatternReliefDepth_->setRange(5.0, 95.0);
    plateFlatPatternReliefDepth_->setSingleStep(5.0);
    plateFlatPatternReliefDepth_->setSuffix(QStringLiteral(" %"));
    plateFlatPatternReliefDepth_->setToolTip(
        QStringLiteral("外周から板幅の何%まで切り込むかを指定します"));
    plateFlatPatternNotchAngle_ = MakePositiveField(18.0);
    plateFlatPatternNotchAngle_->setRange(1.0, 120.0);
    plateFlatPatternNotchAngle_->setSingleStep(1.0);
    plateFlatPatternNotchAngle_->setSuffix(QStringLiteral(" °"));
    plateFlatPatternNotchAngle_->setToolTip(
        QStringLiteral("V字の開き。大きくすると組立時に強く絞れます"));
    plateFlatPatternNotchTipRadius_ = MakePositiveField(0.5);
    plateFlatPatternNotchTipRadius_->setRange(0.05, 10.0);
    plateFlatPatternNotchTipRadius_->setDecimals(2);
    plateFlatPatternNotchTipRadius_->setSingleStep(0.1);
    plateFlatPatternNotchTipRadius_->setSuffix(QStringLiteral(" mm"));
    plateFlatPatternNotchTipRadius_->setToolTip(
        QStringLiteral("切れ込み底の丸み。割れ防止や工具径に合わせます"));
    plateFlatPatternMinimumBendAngle_ = MakePositiveField(2.0);
    plateFlatPatternMinimumBendAngle_->setRange(0.1, 45.0);
    plateFlatPatternMinimumBendAngle_->setDecimals(1);
    plateFlatPatternMinimumBendAngle_->setSingleStep(0.5);
    plateFlatPatternMinimumBendAngle_->setSuffix(QStringLiteral(" °"));
    plateFlatPatternMinimumBendAngle_->setToolTip(
        QStringLiteral("この角度より小さい局所的な曲がりでは自動線を増やしません"));
    plateAssemblyGuidePreview_ = new QCheckBox(QStringLiteral("組立3Dで折り目・切れ目を表示"));
    plateAssemblyGuidePreview_->setObjectName(QStringLiteral("plateAssemblyGuidePreview"));
    plateAssemblyGuidePreview_->setChecked(true);
    plateAssemblyGuidePreview_->setToolTip(
        QStringLiteral("選択板材の完成位置へ、折り目を青破線、切れ目を赤線で重ねます"));
    plateFlatPatternFoldSpacing_ = MakePositiveField(8.0);
    plateFlatPatternFoldSpacing_->setRange(1.0, 100.0);
    plateFlatPatternFoldSpacing_->setSuffix(QStringLiteral(" mm"));
    plateFlatPatternCutWidth_ = MakePositiveField(0.2);
    plateFlatPatternCutWidth_->setRange(0.05, 3.0);
    plateFlatPatternCutWidth_->setDecimals(2);
    plateFlatPatternCutWidth_->setSingleStep(0.05);
    plateFlatPatternCutWidth_->setSuffix(QStringLiteral(" mm"));
    flatModelForm->addRow(QStringLiteral("展開部材名"), plateFlatPatternName_);
    flatModelForm->addRow(QStringLiteral("配置する平面"), plateFlatPatternPlane_);
    flatModelForm->addRow(plateFlatPatternAutoRelief_);
    flatModelForm->addRow(QStringLiteral("二方向曲面の処理"), plateFlatPatternReliefStyle_);
    flatModelForm->addRow(QStringLiteral("立体再現度"), fidelityControl);
    flatModelForm->addRow(QStringLiteral("切れ込み間隔"), plateFlatPatternReliefSpacing_);
    flatModelForm->addRow(QStringLiteral("切れ込み深さ"), plateFlatPatternReliefDepth_);
    flatModelForm->addRow(QStringLiteral("V字の開き角"), plateFlatPatternNotchAngle_);
    flatModelForm->addRow(QStringLiteral("切れ込み先端R"), plateFlatPatternNotchTipRadius_);
    flatModelForm->addRow(QStringLiteral("反応する最小曲がり"), plateFlatPatternMinimumBendAngle_);
    flatModelForm->addRow(plateAssemblyGuidePreview_);
    flatModelForm->addRow(QStringLiteral("折り線間隔"), plateFlatPatternFoldSpacing_);
    flatModelForm->addRow(QStringLiteral("3D切り幅"), plateFlatPatternCutWidth_);
    layout->addLayout(flatModelForm);

    auto* createFlatModelButton = new QPushButton(QStringLiteral("展開ワイヤー＋3D板を作成"));
    createFlatModelButton->setObjectName("primaryButton");
    createFlatModelButton->setProperty("plateFlatPatternModelAction", true);
    createFlatModelButton->setToolTip(QStringLiteral("選択板材を配置平面へ展開し、編集可能な線と厚み付き板を作成"));
    connect(createFlatModelButton, &QPushButton::clicked,
        this, &MainWindow::CreateSelectedPlateFlatPatternModel);
    layout->addWidget(createFlatModelButton);

    auto* pdfForm = new QFormLayout;
    pdfForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    platePdfPaper_ = new QComboBox;
    platePdfPaper_->addItem(QStringLiteral("A4（自動向き）"), static_cast<int>(QPageSize::A4));
    platePdfPaper_->addItem(QStringLiteral("A3（自動向き）"), static_cast<int>(QPageSize::A3));
    platePdfOverlap_ = MakeNumberField(5.0);
    platePdfOverlap_->setRange(0.0, 20.0);
    platePdfOverlap_->setDecimals(1);
    platePdfOverlap_->setSingleStep(1.0);
    platePdfOverlap_->setSuffix(QStringLiteral(" mm"));
    pdfForm->addRow(QStringLiteral("PDF用紙"), platePdfPaper_);
    pdfForm->addRow(QStringLiteral("ページ重なり"), platePdfOverlap_);
    layout->addLayout(pdfForm);

    auto* platePdfButton = new QPushButton(QStringLiteral("1:1 PDFを保存"));
    platePdfButton->setObjectName("primaryButton");
    platePdfButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    platePdfButton->setProperty("platePdfAction", true);

    auto* plateSvgButton = new QPushButton(QStringLiteral("展開SVGを保存"));
    plateSvgButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    plateSvgButton->setProperty("plateFlatPatternAction", true);
    auto* plateDxfButton = new QPushButton(QStringLiteral("展開DXFを保存"));
    plateDxfButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(platePdfButton, &QPushButton::clicked, this, &MainWindow::ExportSelectedPlatePdf);
    connect(plateSvgButton, &QPushButton::clicked, this, [this] { ExportSelectedPlate(false); });
    connect(plateDxfButton, &QPushButton::clicked, this, [this] { ExportSelectedPlate(true); });
    connect(platePdfPaper_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    connect(platePdfOverlap_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    const auto updateReliefControls = [this, fidelityControl, flatModelForm] {
        const bool enabled = plateFlatPatternAutoRelief_->isChecked();
        const AutomaticReliefStyle style = static_cast<AutomaticReliefStyle>(
            plateFlatPatternReliefStyle_->currentData().toInt());
        const bool splitPieces = style == AutomaticReliefStyle::SplitPieces;
        const bool rounded = style == AutomaticReliefStyle::RoundedVNotch;
        plateFlatPatternReliefStyle_->setEnabled(enabled);
        fidelityControl->setEnabled(enabled && splitPieces);
        plateFlatPatternReliefSpacing_->setEnabled(enabled && !splitPieces);
        plateFlatPatternReliefDepth_->setEnabled(enabled && !splitPieces);
        plateFlatPatternNotchAngle_->setEnabled(enabled && !splitPieces);
        plateFlatPatternNotchTipRadius_->setEnabled(enabled && rounded);
        plateFlatPatternMinimumBendAngle_->setEnabled(enabled);
        flatModelForm->setRowVisible(fidelityControl, enabled && splitPieces);
        flatModelForm->setRowVisible(plateFlatPatternReliefSpacing_, enabled && !splitPieces);
        flatModelForm->setRowVisible(plateFlatPatternReliefDepth_, enabled && !splitPieces);
        flatModelForm->setRowVisible(plateFlatPatternNotchAngle_, enabled && !splitPieces);
        flatModelForm->setRowVisible(plateFlatPatternNotchTipRadius_, enabled && rounded);
        flatModelForm->setRowVisible(plateFlatPatternMinimumBendAngle_, enabled);
        RefreshExportSummary();
    };
    connect(plateFlatPatternAutoRelief_, &QCheckBox::toggled, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternReliefStyle_, &QComboBox::currentIndexChanged, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternFidelity_, &QSlider::valueChanged, this, [this](int value) {
        const QString level = value <= 3
            ? QStringLiteral("粗い")
            : value <= 7 ? QStringLiteral("標準") : QStringLiteral("細かい");
        plateFlatPatternFidelityLabel_->setText(QStringLiteral("%1 / 10（%2）").arg(value).arg(level));
        RefreshExportSummary();
    });
    connect(plateAssemblyGuidePreview_, &QCheckBox::toggled, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternFoldSpacing_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternReliefSpacing_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternReliefDepth_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternNotchAngle_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternNotchTipRadius_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternMinimumBendAngle_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    updateReliefControls();
    layout->addWidget(platePdfButton);
    layout->addWidget(plateSvgButton);
    layout->addWidget(plateDxfButton);

    auto* bodySeparator = new QFrame;
    bodySeparator->setFrameShape(QFrame::HLine);
    bodySeparator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(bodySeparator);

    auto* bodyTitle = new QLabel(QStringLiteral("3DモデルのSTL / STEP出力"));
    bodyTitle->setObjectName(QStringLiteral("modelOutputSection"));
    bodyTitle->setProperty("manualAnchor", QStringLiteral("modelOutput"));
    bodyTitle->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(bodyTitle);
    modelExportScope_ = new QComboBox;
    modelExportScope_->addItems({
        QStringLiteral("3D画面で選択した部分"),
        QStringLiteral("表示中の3Dモデル全体"),
    });
    layout->addWidget(modelExportScope_);
    bodyExportSummary_ = new QLabel(QStringLiteral("選択3D部品: なし"));
    bodyExportSummary_->setWordWrap(true);
    bodyExportSummary_->setStyleSheet("color: #5c6670;");
    layout->addWidget(bodyExportSummary_);

    auto* bodyStlButton = new QPushButton(QStringLiteral("STLを保存"));
    bodyStlButton->setObjectName("primaryButton");
    bodyStlButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    auto* bodyStepButton = new QPushButton(QStringLiteral("STEPを保存"));
    bodyStepButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(bodyStlButton, &QPushButton::clicked, this, [this] { ExportSelectedBody(false); });
    connect(bodyStepButton, &QPushButton::clicked, this, [this] { ExportSelectedBody(true); });
    connect(modelExportScope_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    layout->addWidget(bodyStlButton);
    layout->addWidget(bodyStepButton);
    layout->addStretch(1);
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(panel);
    return scrollArea;
}

QWidget* MainWindow::BuildDisplayPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    const auto makeWidthField = [] (double value) {
        auto* field = new ExpressionDoubleSpinBox;
        field->setRange(0.25, 12.0);
        field->setDecimals(2);
        field->setSingleStep(0.25);
        field->setSuffix(QStringLiteral(" px"));
        field->setValue(value);
        field->setKeyboardTracking(false);
        return field;
    };
    const auto makeOpacityField = [] (double value) {
        auto* field = new ExpressionDoubleSpinBox;
        field->setRange(0.0, 100.0);
        field->setDecimals(0);
        field->setSingleStep(5.0);
        field->setSuffix(QStringLiteral(" %"));
        field->setValue(value);
        field->setKeyboardTracking(false);
        return field;
    };
    const auto makeLineStyle = [] {
        auto* combo = new QComboBox;
        combo->addItem(QStringLiteral("実線"), static_cast<int>(Qt::SolidLine));
        combo->addItem(QStringLiteral("破線"), static_cast<int>(Qt::DashLine));
        combo->addItem(QStringLiteral("点線"), static_cast<int>(Qt::DotLine));
        combo->addItem(QStringLiteral("一点鎖線"), static_cast<int>(Qt::DashDotLine));
        return combo;
    };
    const auto makeColorButton = [this](const QColor& color) {
        auto* button = new QPushButton;
        button->setToolTip(QStringLiteral("色を選択"));
        SetDisplayColorButton(button, color);
        return button;
    };

    auto* wireBox = new QGroupBox(QStringLiteral("通常ワイヤー"));
    auto* wireForm = new QFormLayout(wireBox);
    wireColor_ = makeColorButton(QColor("#263b44"));
    wireWidth_ = makeWidthField(2.0);
    wireStyle_ = makeLineStyle();
    wireForm->addRow(QStringLiteral("色"), wireColor_);
    wireForm->addRow(QStringLiteral("太さ"), wireWidth_);
    wireForm->addRow(QStringLiteral("線種"), wireStyle_);
    layout->addWidget(wireBox);

    auto* constructionBox = new QGroupBox(QStringLiteral("補助線"));
    auto* constructionForm = new QFormLayout(constructionBox);
    constructionColor_ = makeColorButton(QColor("#697984"));
    constructionWidth_ = makeWidthField(1.7);
    constructionStyle_ = makeLineStyle();
    constructionStyle_->setCurrentIndex(1);
    constructionForm->addRow(QStringLiteral("色"), constructionColor_);
    constructionForm->addRow(QStringLiteral("太さ"), constructionWidth_);
    constructionForm->addRow(QStringLiteral("線種"), constructionStyle_);
    layout->addWidget(constructionBox);

    auto* surfaceBox = new QGroupBox(QStringLiteral("面"));
    auto* surfaceForm = new QFormLayout(surfaceBox);
    surfaceFillColor_ = makeColorButton(QColor("#1f848a"));
    surfaceOpacity_ = makeOpacityField(26.0);
    surfaceEdgeColor_ = makeColorButton(QColor("#277b80"));
    surfaceEdgeWidth_ = makeWidthField(1.4);
    surfaceEdgeStyle_ = makeLineStyle();
    surfaceForm->addRow(QStringLiteral("塗り色"), surfaceFillColor_);
    surfaceForm->addRow(QStringLiteral("不透明度"), surfaceOpacity_);
    surfaceForm->addRow(QStringLiteral("輪郭色"), surfaceEdgeColor_);
    surfaceForm->addRow(QStringLiteral("輪郭太さ"), surfaceEdgeWidth_);
    surfaceForm->addRow(QStringLiteral("輪郭線種"), surfaceEdgeStyle_);
    layout->addWidget(surfaceBox);

    auto* plateBox = new QGroupBox(QStringLiteral("板材"));
    auto* plateForm = new QFormLayout(plateBox);
    plateFillColor_ = makeColorButton(QColor("#b2c2cb"));
    plateOpacity_ = makeOpacityField(62.0);
    plateEdgeColor_ = makeColorButton(QColor("#586970"));
    plateEdgeWidth_ = makeWidthField(1.0);
    plateEdgeStyle_ = makeLineStyle();
    plateForm->addRow(QStringLiteral("塗り色"), plateFillColor_);
    plateForm->addRow(QStringLiteral("不透明度"), plateOpacity_);
    plateForm->addRow(QStringLiteral("輪郭色"), plateEdgeColor_);
    plateForm->addRow(QStringLiteral("輪郭太さ"), plateEdgeWidth_);
    plateForm->addRow(QStringLiteral("輪郭線種"), plateEdgeStyle_);
    layout->addWidget(plateBox);

    auto* environmentBox = new QGroupBox(QStringLiteral("背景・点グリッド"));
    environmentBox->setProperty("manualAnchor", QStringLiteral("displaySettings"));
    auto* environmentForm = new QFormLayout(environmentBox);
    backgroundColor_ = makeColorButton(QColor("#f5f6f7"));
    majorGridColor_ = makeColorButton(QColor("#9aa8b0"));
    minorGridColor_ = makeColorButton(QColor("#c5cdd2"));
    environmentForm->addRow(QStringLiteral("背景色"), backgroundColor_);
    environmentForm->addRow(QStringLiteral("主点色"), majorGridColor_);
    environmentForm->addRow(QStringLiteral("副点色"), minorGridColor_);
    layout->addWidget(environmentBox);

    auto* resetButton = new QPushButton(QStringLiteral("表示設定を初期値に戻す"));
    layout->addWidget(resetButton);
    layout->addStretch(1);

    const auto changed = [this] {
        ApplyDisplaySettings();
        SaveDisplaySettings();
    };
    for (QPushButton* button : {wireColor_, constructionColor_, surfaceFillColor_,
             surfaceEdgeColor_, plateFillColor_, plateEdgeColor_, backgroundColor_,
             majorGridColor_, minorGridColor_}) {
        connect(button, &QPushButton::clicked, this, [this, button] { ChooseDisplayColor(button); });
    }
    for (QDoubleSpinBox* field : {wireWidth_, constructionWidth_, surfaceOpacity_,
             surfaceEdgeWidth_, plateOpacity_, plateEdgeWidth_}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, changed);
    }
    for (QComboBox* combo : {wireStyle_, constructionStyle_, surfaceEdgeStyle_, plateEdgeStyle_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, changed);
    }
    connect(resetButton, &QPushButton::clicked, this, [this] {
        loadingDisplaySettings_ = true;
        SetDisplayColorButton(wireColor_, QColor("#263b44"));
        wireWidth_->setValue(2.0);
        wireStyle_->setCurrentIndex(wireStyle_->findData(static_cast<int>(Qt::SolidLine)));
        SetDisplayColorButton(constructionColor_, QColor("#697984"));
        constructionWidth_->setValue(1.7);
        constructionStyle_->setCurrentIndex(constructionStyle_->findData(static_cast<int>(Qt::DashLine)));
        SetDisplayColorButton(surfaceFillColor_, QColor("#1f848a"));
        surfaceOpacity_->setValue(26.0);
        SetDisplayColorButton(surfaceEdgeColor_, QColor("#277b80"));
        surfaceEdgeWidth_->setValue(1.4);
        surfaceEdgeStyle_->setCurrentIndex(surfaceEdgeStyle_->findData(static_cast<int>(Qt::SolidLine)));
        SetDisplayColorButton(plateFillColor_, QColor("#b2c2cb"));
        plateOpacity_->setValue(62.0);
        SetDisplayColorButton(plateEdgeColor_, QColor("#586970"));
        plateEdgeWidth_->setValue(1.0);
        plateEdgeStyle_->setCurrentIndex(plateEdgeStyle_->findData(static_cast<int>(Qt::SolidLine)));
        SetDisplayColorButton(backgroundColor_, QColor("#f5f6f7"));
        SetDisplayColorButton(majorGridColor_, QColor("#9aa8b0"));
        SetDisplayColorButton(minorGridColor_, QColor("#c5cdd2"));
        loadingDisplaySettings_ = false;
        ApplyDisplaySettings();
        SaveDisplaySettings();
    });

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(panel);
    return scrollArea;
}

void MainWindow::SetDisplayColorButton(QPushButton* button, const QColor& color)
{
    if (button == nullptr || !color.isValid()) {
        return;
    }
    const QColor textColor = color.lightnessF() < 0.52 ? QColor("#ffffff") : QColor("#17242b");
    button->setProperty("displayColor", color.name(QColor::HexRgb));
    button->setText(color.name(QColor::HexRgb).toUpper());
    button->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border: 1px solid #66747c; font-weight: 600;")
            .arg(color.name(QColor::HexRgb), textColor.name(QColor::HexRgb)));
}

QColor MainWindow::DisplayColor(const QPushButton* button)
{
    return button == nullptr ? QColor() : QColor(button->property("displayColor").toString());
}

void MainWindow::ChooseDisplayColor(QPushButton* button)
{
    const QColor color = QColorDialog::getColor(
        DisplayColor(button), this, QStringLiteral("表示色を選択"));
    if (!color.isValid()) {
        return;
    }
    SetDisplayColorButton(button, color);
    ApplyDisplaySettings();
    SaveDisplaySettings();
}

void MainWindow::ApplyDisplaySettings()
{
    if (viewport_ == nullptr || wireStyle_ == nullptr) {
        return;
    }
    viewport_->SetWireAppearance(
        DisplayColor(wireColor_), wireWidth_->value(),
        static_cast<Qt::PenStyle>(wireStyle_->currentData().toInt()));
    viewport_->SetConstructionWireAppearance(
        DisplayColor(constructionColor_), constructionWidth_->value(),
        static_cast<Qt::PenStyle>(constructionStyle_->currentData().toInt()));
    viewport_->SetSurfaceAppearance(
        DisplayColor(surfaceFillColor_), static_cast<int>(surfaceOpacity_->value()),
        DisplayColor(surfaceEdgeColor_), surfaceEdgeWidth_->value(),
        static_cast<Qt::PenStyle>(surfaceEdgeStyle_->currentData().toInt()));
    viewport_->SetPlateAppearance(
        DisplayColor(plateFillColor_), static_cast<int>(plateOpacity_->value()),
        DisplayColor(plateEdgeColor_), plateEdgeWidth_->value(),
        static_cast<Qt::PenStyle>(plateEdgeStyle_->currentData().toInt()));
    viewport_->SetBackgroundColor(DisplayColor(backgroundColor_));
    viewport_->SetGridColors(DisplayColor(majorGridColor_), DisplayColor(minorGridColor_));
}

void MainWindow::LoadDisplaySettings()
{
    if (wireColor_ == nullptr) {
        return;
    }
    if (IsAutomationInvocation()) {
        ApplyDisplaySettings();
        return;
    }
    loadingDisplaySettings_ = true;
    QSettings settings;
    const auto loadColor = [&settings](QPushButton* button, const char* key) {
        const QColor fallback = DisplayColor(button);
        const QColor stored(settings.value(QString::fromLatin1(key), fallback.name()).toString());
        SetDisplayColorButton(button, stored.isValid() ? stored : fallback);
    };
    const auto loadStyle = [&settings](QComboBox* combo, const char* key) {
        const int style = settings.value(QString::fromLatin1(key), combo->currentData()).toInt();
        const int index = combo->findData(style);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    };
    loadColor(wireColor_, "display/wireColor");
    wireWidth_->setValue(settings.value("display/wireWidth", wireWidth_->value()).toDouble());
    loadStyle(wireStyle_, "display/wireStyle");
    loadColor(constructionColor_, "display/constructionColor");
    constructionWidth_->setValue(settings.value("display/constructionWidth", constructionWidth_->value()).toDouble());
    loadStyle(constructionStyle_, "display/constructionStyle");
    loadColor(surfaceFillColor_, "display/surfaceFillColor");
    surfaceOpacity_->setValue(settings.value("display/surfaceOpacity", surfaceOpacity_->value()).toDouble());
    loadColor(surfaceEdgeColor_, "display/surfaceEdgeColor");
    surfaceEdgeWidth_->setValue(settings.value("display/surfaceEdgeWidth", surfaceEdgeWidth_->value()).toDouble());
    loadStyle(surfaceEdgeStyle_, "display/surfaceEdgeStyle");
    loadColor(plateFillColor_, "display/plateFillColor");
    plateOpacity_->setValue(settings.value("display/plateOpacity", plateOpacity_->value()).toDouble());
    loadColor(plateEdgeColor_, "display/plateEdgeColor");
    plateEdgeWidth_->setValue(settings.value("display/plateEdgeWidth", plateEdgeWidth_->value()).toDouble());
    loadStyle(plateEdgeStyle_, "display/plateEdgeStyle");
    loadColor(backgroundColor_, "display/backgroundColor");
    loadColor(majorGridColor_, "display/majorGridColor");
    loadColor(minorGridColor_, "display/minorGridColor");
    loadingDisplaySettings_ = false;
    ApplyDisplaySettings();
}

void MainWindow::SaveDisplaySettings() const
{
    if (loadingDisplaySettings_ || IsAutomationInvocation() || wireColor_ == nullptr) {
        return;
    }
    QSettings settings;
    settings.setValue("display/wireColor", DisplayColor(wireColor_).name());
    settings.setValue("display/wireWidth", wireWidth_->value());
    settings.setValue("display/wireStyle", wireStyle_->currentData());
    settings.setValue("display/constructionColor", DisplayColor(constructionColor_).name());
    settings.setValue("display/constructionWidth", constructionWidth_->value());
    settings.setValue("display/constructionStyle", constructionStyle_->currentData());
    settings.setValue("display/surfaceFillColor", DisplayColor(surfaceFillColor_).name());
    settings.setValue("display/surfaceOpacity", surfaceOpacity_->value());
    settings.setValue("display/surfaceEdgeColor", DisplayColor(surfaceEdgeColor_).name());
    settings.setValue("display/surfaceEdgeWidth", surfaceEdgeWidth_->value());
    settings.setValue("display/surfaceEdgeStyle", surfaceEdgeStyle_->currentData());
    settings.setValue("display/plateFillColor", DisplayColor(plateFillColor_).name());
    settings.setValue("display/plateOpacity", plateOpacity_->value());
    settings.setValue("display/plateEdgeColor", DisplayColor(plateEdgeColor_).name());
    settings.setValue("display/plateEdgeWidth", plateEdgeWidth_->value());
    settings.setValue("display/plateEdgeStyle", plateEdgeStyle_->currentData());
    settings.setValue("display/backgroundColor", DisplayColor(backgroundColor_).name());
    settings.setValue("display/majorGridColor", DisplayColor(majorGridColor_).name());
    settings.setValue("display/minorGridColor", DisplayColor(minorGridColor_).name());
}

QWidget* MainWindow::BuildInfoPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);

    auto* measurementBox = new QGroupBox(QStringLiteral("測定"));
    measurementBox->setProperty("manualAnchor", QStringLiteral("measurement"));
    auto* measurementLayout = new QVBoxLayout(measurementBox);
    measurementLayout->setContentsMargins(10, 10, 10, 10);
    measurementLayout->setSpacing(8);
    measurementMode_ = new QComboBox;
    measurementMode_->addItems({
        QStringLiteral("2点間（3D距離）"),
        QStringLiteral("3点角度（3D）"),
        QStringLiteral("要素（接線・法線）"),
    });
    measurementStateLabel_ = new QLabel(QStringLiteral("1点目"));
    measurementStateLabel_->setStyleSheet("font-weight: 600; color: #26323a;");
    measurementResultLabel_ = new QLabel(QStringLiteral("未測定"));
    measurementResultLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    measurementResultLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    measurementResultLabel_->setWordWrap(true);
    measurementResultLabel_->setMinimumHeight(118);
    measurementResultLabel_->setStyleSheet("background: #f5f7f8; border: 1px solid #d4dade; padding: 8px;");
    measurementMetric_ = new QComboBox;
    measurementMetric_->setEnabled(false);
    measurementName_ = new QLineEdit(SuggestedDimensionName());
    measurementName_->setPlaceholderText(QStringLiteral("寸法名"));
    measurementSaveButton_ = new QPushButton(QStringLiteral("寸法を残す"));
    measurementSaveButton_->setEnabled(false);
    measurementClearButton_ = new QPushButton(QStringLiteral("測定を消去"));
    auto* saveRow = new QWidget;
    auto* saveRowLayout = new QHBoxLayout(saveRow);
    saveRowLayout->setContentsMargins(0, 0, 0, 0);
    saveRowLayout->setSpacing(6);
    saveRowLayout->addWidget(measurementName_, 1);
    saveRowLayout->addWidget(measurementSaveButton_);
    measurementLayout->addWidget(measurementMode_);
    measurementLayout->addWidget(measurementStateLabel_);
    measurementLayout->addWidget(measurementResultLabel_);
    measurementLayout->addWidget(measurementMetric_);
    measurementLayout->addWidget(saveRow);
    measurementLayout->addWidget(measurementClearButton_);
    layout->addWidget(measurementBox);

    auto* savedBox = new QGroupBox(QStringLiteral("残した参照寸法"));
    auto* savedLayout = new QVBoxLayout(savedBox);
    savedLayout->setContentsMargins(10, 10, 10, 10);
    savedLayout->setSpacing(7);
    referenceDimensionList_ = new QListWidget;
    referenceDimensionList_->setSelectionMode(QAbstractItemView::SingleSelection);
    referenceDimensionList_->setMinimumHeight(130);
    referenceDimensionDeleteButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("選択した寸法を削除"));
    referenceDimensionDeleteButton_->setEnabled(false);
    savedLayout->addWidget(referenceDimensionList_);
    savedLayout->addWidget(referenceDimensionDeleteButton_);
    layout->addWidget(savedBox);

    connect(measurementMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->SetMeasurementMode(index == 0 ? MeasurementMode::TwoPoints
            : index == 1 ? MeasurementMode::ThreePointsAngle
                         : MeasurementMode::Elements);
        SetViewportTool(ViewportTool::Measure);
    });
    connect(measurementClearButton_, &QPushButton::clicked, viewport_, &CadViewport::ClearMeasurement);
    connect(measurementName_, &QLineEdit::textChanged, this, [this] {
        measurementSaveButton_->setEnabled(
            measurementMetric_->count() > 0 && !measurementName_->text().trimmed().isEmpty());
    });
    connect(measurementSaveButton_, &QPushButton::clicked, this, &MainWindow::SaveCurrentMeasurement);
    connect(referenceDimensionDeleteButton_, &QPushButton::clicked,
        this, &MainWindow::DeleteSelectedReferenceDimension);
    connect(referenceDimensionList_, &QListWidget::currentRowChanged, this, [this](int row) {
        referenceDimensionDeleteButton_->setEnabled(row >= 0);
    });
    connect(referenceDimensionList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const std::string name = ToName(item->data(kDimensionNameRole).toString());
        const bool visible = item->checkState() == Qt::Checked;
        const auto position = std::find_if(
            project_.ReferenceDimensions().begin(), project_.ReferenceDimensions().end(),
            [&](const ReferenceDimension& dimension) { return dimension.name == name; });
        if (position == project_.ReferenceDimensions().end() || position->visible == visible) {
            return;
        }
        RecordUndo();
        project_.SetReferenceDimensionVisible(name, visible);
        MarkModified();
        RefreshModelViews(false);
    });

    auto* selectionLabel = new QLabel(QStringLiteral("選択情報"));
    selectionLabel->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 5px;");
    layout->addWidget(selectionLabel);
    infoLabel_ = new QLabel(QStringLiteral("選択なし"));
    infoLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    infoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);
    layout->addStretch(1);
    return panel;
}

void MainWindow::OpenManual(const QString& anchor)
{
    const QDir applicationDirectory(QApplication::applicationDirPath());
    const QStringList candidates = {
        applicationDirectory.filePath(QStringLiteral("manual.html")),
        applicationDirectory.filePath(QStringLiteral("../../docs/manual.html")),
    };
    for (const QString& candidate : candidates) {
        if (!QFileInfo::exists(candidate)) {
            continue;
        }
        QUrl url = QUrl::fromLocalFile(QFileInfo(candidate).absoluteFilePath());
        if (!anchor.isEmpty()) {
            url.setFragment(anchor);
        }
        QDesktopServices::openUrl(url);
        return;
    }
    statusBar()->showMessage(QStringLiteral("manual.html が見つかりません"), 5000);
}

void MainWindow::OpenLegalNotices()
{
    const QDir applicationDirectory(QApplication::applicationDirPath());
    const QStringList candidates = {
        applicationDirectory.filePath(QStringLiteral("legal/README-JA.txt")),
        applicationDirectory.filePath(QStringLiteral("../../legal/README-JA.txt")),
    };
    QString noticePath;
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            noticePath = QFileInfo(candidate).absoluteFilePath();
            break;
        }
    }

    QMessageBox dialog(this);
    dialog.setWindowTitle(QStringLiteral("使用ライブラリと権利"));
    dialog.setIcon(QMessageBox::Information);
    dialog.setText(QStringLiteral(
        "kachakachaCAD %1\n\n"
        "本体は GPL-3.0-or-later で公開しています。\n\n"
        "Qt 6.9.2（LGPLv3）と Open CASCADE Technology 8.0.1"
        "（LGPLv2.1 + 追加例外）を共有DLLとして使用しています。\n\n"
        "利用者は各ライセンスに従ってDLLを調査・変更・差し替えできます。"
        "本ソフトで作成した模型データに、本体のGPLやこれらのライブラリのライセンスが"
        "自動的に適用されることはありません。実在車両やロゴなど第三者の権利は別途確認してください。")
        .arg(QApplication::applicationVersion()));
    QPushButton* openButton = nullptr;
    if (!noticePath.isEmpty()) {
        openButton = dialog.addButton(QStringLiteral("詳細文書を開く"), QMessageBox::ActionRole);
    }
    dialog.addButton(QStringLiteral("閉じる"), QMessageBox::RejectRole);
    dialog.exec();
    if (openButton != nullptr && dialog.clickedButton() == openButton) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(noticePath));
    }
}

void MainWindow::BuildMenusAndToolbar()
{
    QAction* newAction = new QAction(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("新規"), this);
    QAction* openAction = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("開く"), this);
    QAction* saveAction = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("保存"), this);
    QAction* saveAsAction = new QAction(QStringLiteral("名前を付けて保存"), this);
    QAction* fitAction = new QAction(QStringLiteral("全体表示"), this);
    QAction* deleteAction = new QAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("削除"), this);
    QAction* exitAction = new QAction(QStringLiteral("終了"), this);
    undoAction_ = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), QStringLiteral("元に戻す"), this);
    redoAction_ = new QAction(style()->standardIcon(QStyle::SP_ArrowForward), QStringLiteral("やり直す"), this);
    hideSelectedAction_ = new QAction(QStringLiteral("隠す"), this);
    showAllObjectsAction_ = new QAction(QStringLiteral("全て表示"), this);
    designDisplayAction_ = new QAction(QStringLiteral("設計"), this);
    finishedDisplayAction_ = new QAction(QStringLiteral("完成形"), this);
    isolateDisplayAction_ = new QAction(QStringLiteral("選択だけ"), this);
    QAction* manualAction = new QAction(QStringLiteral("操作マニュアル"), this);
    QAction* legalAction = new QAction(QStringLiteral("使用ライブラリと権利"), this);

    auto* displayModeGroup = new QActionGroup(this);
    displayModeGroup->setExclusive(true);
    for (QAction* action : {designDisplayAction_, finishedDisplayAction_, isolateDisplayAction_}) {
        action->setCheckable(true);
        displayModeGroup->addAction(action);
    }
    designDisplayAction_->setChecked(true);

    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    deleteAction->setShortcut(QKeySequence::Delete);
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_->setShortcut(QKeySequence::Redo);
    hideSelectedAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    showAllObjectsAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    designDisplayAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    finishedDisplayAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    isolateDisplayAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    hideSelectedAction_->setToolTip(QStringLiteral("選択した作業平面・ワイヤー・面・板材を隠す"));
    showAllObjectsAction_->setToolTip(QStringLiteral("隠した作業平面・ワイヤー・面・板材をすべて表示"));
    designDisplayAction_->setToolTip(QStringLiteral("作業平面、ワイヤー、面、板材、立体を通常どおり表示"));
    finishedDisplayAction_->setToolTip(QStringLiteral("板材と立体だけを一時表示。設計データや履歴は変更しません"));
    isolateDisplayAction_->setToolTip(QStringLiteral("現在選択している要素だけを一時表示。設計データや履歴は変更しません"));

    connect(newAction, &QAction::triggered, this, &MainWindow::NewProject);
    connect(openAction, &QAction::triggered, this, &MainWindow::OpenProject);
    connect(saveAction, &QAction::triggered, this, &MainWindow::SaveProject);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::SaveProjectAs);
    connect(fitAction, &QAction::triggered, viewport_, &CadViewport::FitAll);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::DeleteSelection);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::Undo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::Redo);
    connect(hideSelectedAction_, &QAction::triggered, this, &MainWindow::HideSelected);
    connect(showAllObjectsAction_, &QAction::triggered, this, &MainWindow::ShowAllObjects);
    connect(designDisplayAction_, &QAction::triggered, this, [this] {
        SetDisplayMode(ViewportDisplayMode::Design);
    });
    connect(finishedDisplayAction_, &QAction::triggered, this, [this] {
        SetDisplayMode(ViewportDisplayMode::FinishedModel);
    });
    connect(isolateDisplayAction_, &QAction::triggered, this, [this] {
        SetDisplayMode(ViewportDisplayMode::IsolatedSelection);
    });
    connect(manualAction, &QAction::triggered, this, [this] { OpenManual(); });
    connect(legalAction, &QAction::triggered, this, &MainWindow::OpenLegalNotices);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("ファイル"));
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("編集"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);
    editMenu->addSeparator();
    editMenu->addAction(moveToolAction_);
    editMenu->addAction(copyToolAction_);
    editMenu->addAction(rotateToolAction_);
    editMenu->addAction(mirrorToolAction_);
    editMenu->addAction(splitToolAction_);
    editMenu->addAction(trimToolAction_);
    editMenu->addAction(extendToolAction_);
    editMenu->addAction(coincidentToolAction_);
    editMenu->addAction(removeCoincidentAction_);
    editMenu->addAction(tangentToolAction_);
    editMenu->addAction(curvatureToolAction_);
    editMenu->addAction(removeTangentAction_);
    editMenu->addAction(joinWiresAction_);
    editMenu->addAction(meetLinesAction_);
    editMenu->addAction(setReferenceAction_);
    editMenu->addAction(clearReferenceAction_);
    editMenu->addSeparator();
    editMenu->addAction(deleteAction);
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("表示"));
    viewMenu->addAction(fitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(designDisplayAction_);
    viewMenu->addAction(finishedDisplayAction_);
    viewMenu->addAction(isolateDisplayAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(hideSelectedAction_);
    viewMenu->addAction(showAllObjectsAction_);

    QMenu* drawMenu = menuBar()->addMenu(QStringLiteral("作図"));
    drawMenu->addAction(selectToolAction_);
    drawMenu->addAction(lineToolAction_);
    drawMenu->addAction(polylineToolAction_);
    drawMenu->addAction(rectangleToolAction_);
    drawMenu->addAction(circleToolAction_);
    drawMenu->addAction(arcToolAction_);
    drawMenu->addAction(bezierToolAction_);
    drawMenu->addAction(splineToolAction_);
    drawMenu->addSeparator();
    drawMenu->addAction(measureToolAction_);
    drawMenu->addSeparator();
    drawMenu->addAction(finishDrawingAction_);
    drawMenu->addAction(cancelDrawingAction_);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("ヘルプ"));
    helpMenu->addAction(manualAction);
    helpMenu->addAction(legalAction);

    QToolBar* toolbar = addToolBar(QStringLiteral("基本操作"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(newAction);
    toolbar->addAction(openAction);
    toolbar->addAction(saveAction);
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    toolbar->addSeparator();
    toolbar->addAction(fitAction);
    toolbar->addAction(measureToolAction_);
    toolbar->addSeparator();
    toolbar->addAction(designDisplayAction_);
    toolbar->addAction(finishedDisplayAction_);
    toolbar->addAction(isolateDisplayAction_);
    toolbar->addSeparator();
    toolbar->addAction(hideSelectedAction_);
    toolbar->addAction(showAllObjectsAction_);
    toolbar->addAction(deleteAction);

    QToolBar* drawingToolbar = addToolBar(QStringLiteral("平面作図"));
    drawingToolbar->setMovable(false);
    drawingToolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    drawingToolbar->addAction(selectToolAction_);
    drawingToolbar->addAction(lineToolAction_);
    drawingToolbar->addAction(polylineToolAction_);
    drawingToolbar->addAction(rectangleToolAction_);
    drawingToolbar->addAction(circleToolAction_);
    drawingToolbar->addAction(arcToolAction_);
    drawingToolbar->addAction(bezierToolAction_);
    drawingToolbar->addAction(splineToolAction_);
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(finishDrawingAction_);
    drawingToolbar->addAction(cancelDrawingAction_);

    QToolBar* transformToolbar = addToolBar(QStringLiteral("直接変形"));
    transformToolbar->setMovable(false);
    transformToolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    transformToolbar->addAction(moveToolAction_);
    transformToolbar->addAction(copyToolAction_);
    transformToolbar->addAction(rotateToolAction_);
    transformToolbar->addAction(mirrorToolAction_);
    transformToolbar->addAction(splitToolAction_);
    transformToolbar->addAction(trimToolAction_);
    transformToolbar->addAction(extendToolAction_);
    transformToolbar->addAction(coincidentToolAction_);
    transformToolbar->addAction(tangentToolAction_);
    transformToolbar->addAction(curvatureToolAction_);
    transformToolbar->addAction(joinWiresAction_);
    transformToolbar->addAction(meetLinesAction_);
    transformToolbar->addSeparator();
    transformToolbar->addAction(setReferenceAction_);
    UpdateHistoryActions();
}

void MainWindow::NewProject()
{
    if (!ConfirmDiscardChanges()) {
        return;
    }

    RemoveAutosave();
    project_ = Project{};
    project_.AddWorkPlane("top_XY", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
    project_.AddWorkPlane("front_XZ", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}));
    project_.AddWorkPlane("side_YZ", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    referenceWireName_.reset();
    currentPath_.clear();
    modified_ = false;
    undoStack_.clear();
    redoStack_.clear();
    UpdateHistoryActions();
    ResetDisplayMode();
    modelFilter_->clear();
    RefreshModelViews(true);
    toolsTabs_->setCurrentIndex(0);
    SetViewportTool(ViewportTool::DrawLine);
    viewport_->SetIsometricView();
    setWindowTitle(QStringLiteral("kachakachaCAD - 無題"));
    statusBar()->showMessage(QStringLiteral("新しいプロジェクト"), 3000);
}

void MainWindow::OpenProject()
{
    if (!ConfirmDiscardChanges()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("プロジェクトを開く"), {}, QStringLiteral("kachakachaCAD (*.kcd);;すべてのファイル (*.*)"));
    if (!path.isEmpty()) {
        LoadProjectFile(path);
    }
}

bool MainWindow::LoadProjectFile(const QString& path)
{
    try {
        const std::filesystem::path nativePath(path.toStdWString());
        std::ifstream input(nativePath);
        if (!input) {
            throw std::runtime_error("ファイルを開けませんでした。");
        }
        project_ = LoadProjectScript(input, nativePath.string());
        if (!IsAutomationInvocation()) {
            RemoveAutosave();
        }
        referenceWireName_.reset();
        currentPath_ = path;
        modified_ = false;
        undoStack_.clear();
        redoStack_.clear();
        UpdateHistoryActions();
        ResetDisplayMode();
        modelFilter_->clear();
        RefreshModelViews(true);
        toolsTabs_->setCurrentIndex(0);
        SetViewportTool(ViewportTool::DrawLine);
        viewport_->SetIsometricView();
        setWindowTitle(QStringLiteral("kachakachaCAD - %1").arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("読み込みました: %1").arg(path), 4000);
        return true;
    } catch (const std::exception& error) {
        if (IsAutomationInvocation()) {
            qWarning() << "project load failed:" << QString::fromUtf8(error.what());
        } else {
            QMessageBox::critical(this, QStringLiteral("読み込みエラー"), QString::fromUtf8(error.what()));
        }
        return false;
    }
}

bool MainWindow::ExportFirstBodyForAutomation(const QString& stlPath, const QString& stepPath)
{
    try {
        if (project_.Bodies().empty()) {
            throw std::invalid_argument("自動出力する治具がプロジェクトにありません。");
        }
        if (stlPath.isEmpty() && stepPath.isEmpty()) {
            throw std::invalid_argument("自動出力先が指定されていません。");
        }
        const auto& namedBody = project_.Bodies().front();
        const auto analysis = kachakacha::occt::AnalyzeBodyShape(namedBody.body, 0.01);
        if (!analysis.validBRep || !analysis.closedSolid) {
            throw std::runtime_error("治具が閉じた有効な立体ではありません。");
        }
        if (!stlPath.isEmpty()) {
            kachakacha::occt::WriteBodyStl(
                std::filesystem::path(stlPath.toStdWString()), namedBody.body);
        }
        if (!stepPath.isEmpty()) {
            kachakacha::occt::WriteBodyStep(
                std::filesystem::path(stepPath.toStdWString()), namedBody.body);
        }
        UpdateSelection({CadSelectionKind::Body, 0}, true);
        toolsTabs_->setCurrentIndex(6);
        viewport_->FitAll();
        statusBar()->showMessage(QStringLiteral("完成確認用の治具出力が完了しました"), 5000);
        return true;
    } catch (const std::exception& error) {
        statusBar()->showMessage(
            QStringLiteral("治具の自動出力に失敗しました: %1").arg(QString::fromUtf8(error.what())),
            8000);
        return false;
    }
}

bool MainWindow::PrepareManualScreenshot(const QString& state)
{
    const auto findSelection = [this](CadSelectionKind kind, std::string_view name)
        -> std::optional<CadSelection> {
        if (kind == CadSelectionKind::WorkPlane) {
            for (int index = 0; index < static_cast<int>(project_.WorkPlanes().size()); ++index) {
                if (project_.WorkPlanes()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Point) {
            for (int index = 0; index < static_cast<int>(project_.Points().size()); ++index) {
                if (project_.Points()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Wire) {
            for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
                if (project_.Wires()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Surface) {
            for (int index = 0; index < static_cast<int>(project_.Surfaces().size()); ++index) {
                if (project_.Surfaces()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Plate) {
            for (int index = 0; index < static_cast<int>(project_.Plates().size()); ++index) {
                if (project_.Plates()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Body) {
            for (int index = 0; index < static_cast<int>(project_.Bodies().size()); ++index) {
                if (project_.Bodies()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        }
        return std::nullopt;
    };
    const auto select = [&](std::initializer_list<std::pair<CadSelectionKind, std::string_view>> names) {
        std::vector<CadSelection> selections;
        for (const auto& [kind, name] : names) {
            const auto found = findSelection(kind, name);
            if (!found.has_value()) {
                qWarning() << "manual screenshot object not found:" << QString::fromUtf8(name.data(), static_cast<int>(name.size()));
                return false;
            }
            selections.push_back(*found);
        }
        UpdateSelections(std::move(selections), true);
        return true;
    };
    const auto showTab = [this](int index, double scrollFraction = 0.0) {
        toolsTabs_->setCurrentIndex(index);
        QApplication::processEvents();
        if (auto* area = qobject_cast<QScrollArea*>(toolsTabs_->widget(index))) {
            QScrollBar* scroll = area->verticalScrollBar();
            scroll->setValue(static_cast<int>(std::round(scroll->maximum()
                * std::clamp(scrollFraction, 0.0, 1.0))));
        }
    };
    const auto revealSection = [this](int tabIndex, const QString& anchorName) {
        QWidget* tab = toolsTabs_->widget(tabIndex);
        auto* area = qobject_cast<QScrollArea*>(tab);
        if (area == nullptr && tab != nullptr) {
            area = tab->findChild<QScrollArea*>();
        }
        if (area == nullptr) {
            qWarning() << "manual screenshot scroll area not found:" << tabIndex;
            return;
        }
        QApplication::processEvents();
        const auto children = tab->findChildren<QWidget*>();
        const auto anchor = std::find_if(children.begin(), children.end(), [&](QWidget* child) {
            return child->property("manualAnchor").toString() == anchorName;
        });
        if (anchor != children.end()) {
            QWidget* content = area->widget();
            QScrollBar* scroll = area->verticalScrollBar();
            const int anchorTop = (*anchor)->mapTo(content, QPoint(0, 0)).y();
            const int sectionTopValue = anchorTop - 12;
            scroll->setValue(std::clamp(sectionTopValue, scroll->minimum(), scroll->maximum()));
            QApplication::processEvents();
        } else {
            qWarning() << "manual screenshot anchor not found:" << anchorName;
        }
    };
    const auto setActivePlane = [this](const QString& name) {
        const int index = activePlaneCombo_->findText(name);
        if (index < 0) {
            return false;
        }
        activePlaneCombo_->setCurrentIndex(index);
        RefreshActiveWorkPlane();
        return true;
    };
    int finalRevealTab = -1;
    QString finalRevealAnchor;

    UpdateSelection({}, true);
    SetViewportTool(ViewportTool::Select);
    viewport_->SetIsometricView();

    if (state == QStringLiteral("overview") || state == QStringLiteral("view")) {
        showTab(0);
        viewport_->FitAll();
    } else if (state == QStringLiteral("grid") || state == QStringLiteral("drawing")
        || state == QStringLiteral("snap")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name,
                wire.metadata.sourcePlaneName.has_value()
                    && *wire.metadata.sourcePlaneName == "front");
        }
        gridPointsVisible_->setChecked(true);
        snapAction_->setChecked(true);
        snapStepField_->setValue(1.0);
        gridSubdivision_->setCurrentIndex(gridSubdivision_->findData(4));
        gridOrigin_[0]->setValue(0.5);
        gridOrigin_[1]->setValue(0.5);
        RefreshModelViews(false);
        showTab(0);
        if (state == QStringLiteral("drawing")) {
            SetViewportTool(ViewportTool::DrawLine);
        } else if (state == QStringLiteral("snap")) {
            const auto front = project_.FindWorkPlane("front");
            if (!front.has_value()) {
                return false;
            }
            for (const auto& wire : project_.Wires()) {
                project_.SetWireVisible(wire.name, false);
            }
            WireMetadata metadata;
            metadata.sourcePlaneName = "front";
            metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
            project_.AddWire("snap_horizontal", Wire::Line(
                front->ToWorld(-6.0, 1.0), front->ToWorld(4.0, 1.0)), metadata);
            project_.AddWire("snap_vertical", Wire::Line(
                front->ToWorld(-1.0, -3.0), front->ToWorld(-1.0, 5.0)), metadata);
            project_.AddPoint("snap_reference", front->ToWorld(2.0, 3.0), "front");
            RefreshModelViews(false);
            SetViewportTool(ViewportTool::DrawPoint);
        } else {
            SetViewportTool(ViewportTool::MoveGridOrigin);
        }
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
        if (state == QStringLiteral("drawing")) {
            QApplication::processEvents();
            const QPointF start(viewport_->width() * 0.38, viewport_->height() * 0.58);
            const QPointF end = start + QPointF(135.0, -70.0);
            const auto sendViewportMouse = [this](
                                               QEvent::Type type,
                                               QPointF position,
                                               Qt::MouseButton button,
                                               Qt::MouseButtons buttons) {
                const QPointF globalPosition = viewport_->mapToGlobal(position.toPoint());
                QMouseEvent event(
                    type, position, globalPosition, button, buttons, Qt::NoModifier);
                QApplication::sendEvent(viewport_, &event);
            };
            sendViewportMouse(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
            sendViewportMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendViewportMouse(QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
            sendViewportMouse(QEvent::MouseMove, end, Qt::NoButton, Qt::NoButton);
            const QString expression = QStringLiteral("(10/2)*3");
            for (const QChar character : expression) {
                QWidget* target = QApplication::focusWidget();
                if (target == nullptr) {
                    target = viewport_;
                }
                QKeyEvent keyEvent(
                    QEvent::KeyPress, character.unicode(), Qt::NoModifier, QString(character));
                QApplication::sendEvent(target, &keyEvent);
            }
        } else if (state == QStringLiteral("snap")) {
            QApplication::processEvents();
            const QPointF intersection(viewport_->width() * 0.5, viewport_->height() * 0.5);
            const QPointF globalPosition = viewport_->mapToGlobal(intersection.toPoint());
            QMouseEvent moveEvent(
                QEvent::MouseMove, intersection, globalPosition,
                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport_, &moveEvent);
        }
    } else if (state == QStringLiteral("arc-endpoints")
        || state == QStringLiteral("arc-tangent")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, false);
        }
        RefreshModelViews(false);
        showTab(0);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
        const auto front = project_.FindWorkPlane("front");
        if (!front.has_value()) {
            return false;
        }
        const auto clickWorld = [this](Vector3 point) {
            const QPointF position = viewport_->ScreenPoint(point);
            const QPointF globalPosition = viewport_->mapToGlobal(position.toPoint());
            QMouseEvent moveEvent(
                QEvent::MouseMove, position, globalPosition,
                Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(viewport_, &moveEvent);
            QMouseEvent pressEvent(
                QEvent::MouseButtonPress, position, globalPosition,
                Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
            QApplication::sendEvent(viewport_, &pressEvent);
            QMouseEvent releaseEvent(
                QEvent::MouseButtonRelease, position, globalPosition,
                Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(viewport_, &releaseEvent);
        };
        if (state == QStringLiteral("arc-endpoints")) {
            arcDrawingMode_->setCurrentIndex(1);
            arcRadiusField_->setValue(5.0);
            arcBulgeSide_->setCurrentIndex(arcBulgeSide_->findData(true));
            UpdateArcConfiguration();
            SetViewportTool(ViewportTool::DrawArc);
            clickWorld(front->ToWorld(-3.0, 0.0));
            clickWorld(front->ToWorld(3.0, 0.0));
        } else {
            arcDrawingMode_->setCurrentIndex(2);
            arcDirectionBasis_->setCurrentIndex(arcDirectionBasis_->findData(1));
            arcDirectionAngle_->setValue(90.0);
            arcRadiusField_->setValue(4.0);
            arcExtentMode_->setCurrentIndex(arcExtentMode_->findData(0));
            arcExtentValue_->setValue(160.0);
            arcTurnSide_->setCurrentIndex(arcTurnSide_->findData(1.0));
            UpdateArcConfiguration();
            SetViewportTool(ViewportTool::DrawArc);
            clickWorld(front->ToWorld(-2.5, -1.0));
        }
    } else if (state == QStringLiteral("workplane")) {
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, true);
        }
        RefreshModelViews(false);
        if (!select({{CadSelectionKind::WorkPlane, "free_paper"}})) {
            return false;
        }
        showTab(1);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("numeric")) {
        if (!select({{CadSelectionKind::Wire, "origin_to_point"}})) {
            return false;
        }
        showTab(2);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("edit")) {
        if (!select({{CadSelectionKind::Wire, "front_nose_curve"}})) {
            return false;
        }
        showTab(3);
        (void)viewport_->AlignToSelection();
    } else if (state == QStringLiteral("transforms")) {
        if (!select({
                {CadSelectionKind::Wire, "front_window_bottom"},
                {CadSelectionKind::Wire, "front_window_top"},
            })) {
            return false;
        }
        showTab(3, 0.0);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("trim") || state == QStringLiteral("extend")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, false);
        }
        const auto front = project_.FindWorkPlane("front");
        if (!front.has_value()) {
            return false;
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = "front";
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        project_.AddWire("edit_boundary_left", Wire::Line(
            front->ToWorld(-4.0, -5.0), front->ToWorld(-4.0, 5.0)), metadata);
        const bool trimState = state == QStringLiteral("trim");
        project_.AddWire("edit_boundary_right", Wire::Line(
            front->ToWorld(trimState ? 4.0 : -3.0, -5.0),
            front->ToWorld(trimState ? 4.0 : -3.0, trimState ? 5.0 : 8.0)), metadata);
        project_.AddWire("edit_target", trimState
                ? Wire::CubicBezier(
                    front->ToWorld(-8.0, 0.0), front->ToWorld(-5.0, 3.0),
                    front->ToWorld(5.0, -3.0), front->ToWorld(8.0, 0.0))
                : Wire::CircularArc(
                    front->ToWorld(0.0, 0.0), front->UAxis(), front->VAxis(),
                    5.0, 0.0, kPi * 0.5),
            metadata);
        RefreshModelViews(false);
        showTab(3, 0.0);
        SetViewportTool(trimState ? ViewportTool::TrimWire : ViewportTool::ExtendWire);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
        QApplication::processEvents();
        const Vector3 hoverPoint = project_.Wires().back().wire.Evaluate(trimState ? 0.5 : 0.85);
        const QPointF screenPoint = viewport_->ScreenPoint(hoverPoint);
        const QPointF globalPoint = viewport_->mapToGlobal(screenPoint.toPoint());
        QMouseEvent moveEvent(
            QEvent::MouseMove, screenPoint, globalPoint,
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport_, &moveEvent);
    } else if (state == QStringLiteral("machining")) {
        if (!select({
                {CadSelectionKind::Wire, "front_window_bottom"},
                {CadSelectionKind::Wire, "front_window_top"},
            })) {
            return false;
        }
        showTab(4);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("surface")) {
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, wire.name.ends_with("_joined"));
        }
        for (const auto& plate : project_.Plates()) {
            project_.SetPlateVisible(plate.name, false);
        }
        for (const auto& body : project_.Bodies()) {
            project_.SetBodyVisible(body.name, false);
        }
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Wire, "nose_0_joined"},
                {CadSelectionKind::Wire, "nose_4_joined"},
                {CadSelectionKind::Wire, "nose_8_joined"},
                {CadSelectionKind::Wire, "nose_12_joined"},
                {CadSelectionKind::Wire, "nose_18_joined"},
            })) {
            return false;
        }
        showTab(5, 0.0);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("composite-surface")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, false);
        }
        for (const auto& surface : project_.Surfaces()) {
            project_.SetSurfaceVisible(surface.name, false);
        }
        const auto front = project_.FindWorkPlane("front");
        if (!front.has_value()) {
            return false;
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = "front";
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        project_.AddWire("free_outline_bottom", Wire::Line(
            front->ToWorld(-6.0, -4.0), front->ToWorld(6.0, -4.0)), metadata);
        project_.AddWire("free_outline_arc", Wire::CircularArcThroughThreePoints(
            front->ToWorld(6.0, -4.0), front->ToWorld(8.0, 0.0),
            front->ToWorld(6.0, 4.0)), metadata);
        project_.AddWire("free_outline_top", Wire::Line(
            front->ToWorld(6.0, 4.0), front->ToWorld(-4.0, 5.0)), metadata);
        project_.AddWire("free_outline_left", Wire::Line(
            front->ToWorld(-4.0, 5.0), front->ToWorld(-6.0, -4.0)), metadata);
        project_.AddPlanarSurface("free_outline_surface", {
            "free_outline_top", "free_outline_bottom",
            "free_outline_left", "free_outline_arc",
        });
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Wire, "free_outline_bottom"},
                {CadSelectionKind::Wire, "free_outline_arc"},
                {CadSelectionKind::Wire, "free_outline_top"},
                {CadSelectionKind::Wire, "free_outline_left"},
                {CadSelectionKind::Surface, "free_outline_surface"},
            })) {
            return false;
        }
        surfaceType_->setCurrentIndex(0);
        showTab(5, 0.0);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("projection")) {
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name,
                wire.name == "light_left_plan" || wire.name == "light_right_plan"
                    || wire.name == "windscreen_plan");
        }
        for (const auto& plate : project_.Plates()) {
            project_.SetPlateVisible(plate.name, false);
        }
        project_.SetSurfaceVisible("nose_skin", true);
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Wire, "light_left_plan"},
                {CadSelectionKind::Wire, "light_right_plan"},
                {CadSelectionKind::Wire, "windscreen_plan"},
            })) {
            return false;
        }
        const int targetIndex = projectionSurface_->findText(QStringLiteral("nose_skin"));
        if (targetIndex >= 0) {
            projectionSurface_->setCurrentIndex(targetIndex);
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("surfaceProjection");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("lightcase")) {
        referenceWireName_ = "lamp_axis";
        RefreshReference();
        lightCaseDirectionMode_->setCurrentIndex(1);
        if (!select({
                {CadSelectionKind::Wire, "lamp_front"},
                {CadSelectionKind::Plate, "body_front_panel"},
            })) {
            return false;
        }
        showTab(5, 0.22);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("lightCase");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("plate") || state == QStringLiteral("direction")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(5, 0.35);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateOffset");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("plate-create")) {
        if (!select({{CadSelectionKind::Surface, "nose_skin"}})) {
            return false;
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateCreate");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("jig")) {
        if (!select({{CadSelectionKind::Surface, "nose_skin"}})) {
            return false;
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("surfaceJig");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("openings")) {
        project_.SetWireVisible("light_left_on_skin", true);
        project_.SetWireVisible("light_right_on_skin", true);
        project_.SetWireVisible("windscreen_on_skin", true);
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Plate, "nose_panel_front"},
                {CadSelectionKind::Wire, "light_left_on_skin"},
                {CadSelectionKind::Wire, "light_right_on_skin"},
                {CadSelectionKind::Wire, "windscreen_on_skin"},
            })) {
            return false;
        }
        showTab(5, 0.78);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateOpening");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("relief")) {
        project_.SetWireVisible("light_left_on_skin", true);
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Plate, "nose_panel_front"},
                {CadSelectionKind::Wire, "light_left_on_skin"},
            })) {
            return false;
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateRelief");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("split")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(5, 1.0);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateSplit");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("flat-pattern")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        plateFlatPatternAutoRelief_->setChecked(true);
        showTab(6, 0.45);
        finalRevealTab = 6;
        finalRevealAnchor = QStringLiteral("plateFlatPattern");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("planar-output")) {
        if (!select({
                {CadSelectionKind::Wire, "front_window_bottom"},
                {CadSelectionKind::Wire, "front_window_top"},
            })) {
            return false;
        }
        const int planeIndex = exportPlane_->findText(QStringLiteral("front"));
        if (planeIndex >= 0) {
            exportPlane_->setCurrentIndex(planeIndex);
        }
        exportScope_->setCurrentIndex(1);
        showTab(6);
        finalRevealTab = 6;
        finalRevealAnchor = QStringLiteral("planarOutput");
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("output")) {
        if (!select({
                {CadSelectionKind::Plate, "nose_panel_front"},
                {CadSelectionKind::Body, "nose_forming_jig"},
            })) {
            return false;
        }
        showTab(6, 1.0);
        finalRevealTab = 6;
        finalRevealAnchor = QStringLiteral("modelOutput");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("display") || state == QStringLiteral("display-grid")) {
        showTab(7);
        if (state == QStringLiteral("display-grid")) {
            finalRevealTab = 7;
            finalRevealAnchor = QStringLiteral("displaySettings");
        }
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("measure-3d")) {
        showTab(8);
        measurementMode_->setCurrentIndex(1);
        UpdateMeasurement({
            {MeasurementPickKind::Point, -1, {0.0, 0.0, 0.0}, 0.0},
            {MeasurementPickKind::Point, -1, {8.0, 0.0, 0.0}, 0.0},
            {MeasurementPickKind::Point, -1, {0.0, 6.0, 4.0}, 0.0},
        });
        viewport_->SetIsometricView();
        viewport_->FitAll();
        finalRevealTab = 8;
        finalRevealAnchor = QStringLiteral("measurement");
    } else if (state == QStringLiteral("measure-normal")) {
        const auto curve = findSelection(CadSelectionKind::Wire, "front_nose_curve");
        const auto line = findSelection(CadSelectionKind::Wire, "origin_to_point");
        if (!curve.has_value() || !line.has_value()) {
            return false;
        }
        showTab(8);
        measurementMode_->setCurrentIndex(2);
        UpdateMeasurement({
            {MeasurementPickKind::Wire, curve->index,
                project_.Wires()[curve->index].wire.Evaluate(0.55), 0.55},
            {MeasurementPickKind::Wire, line->index,
                project_.Wires()[line->index].wire.Evaluate(0.5), 0.5},
        });
        viewport_->SetIsometricView();
        viewport_->FitAll();
        finalRevealTab = 8;
        finalRevealAnchor = QStringLiteral("measurement");
    } else if (state == QStringLiteral("inspection")) {
        modelFilter_->setText(QStringLiteral("nose_panel"));
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(8);
        SetDisplayMode(ViewportDisplayMode::FinishedModel);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("info")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(8);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else {
        qWarning() << "unknown manual screenshot state:" << state;
        return false;
    }

    QApplication::processEvents();
    if (state == QStringLiteral("split")) {
        QTimer::singleShot(0, this, [this] {
            if (auto* area = qobject_cast<QScrollArea*>(toolsTabs_->widget(5))) {
                if (area->widget() != nullptr && area->widget()->layout() != nullptr) {
                    area->widget()->layout()->activate();
                }
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
            }
        });
    } else if (finalRevealTab >= 0) {
        revealSection(finalRevealTab, finalRevealAnchor);
    }
    statusBar()->showMessage(QStringLiteral("マニュアル画像: %1").arg(state));
    QApplication::processEvents();
    return true;
}

void MainWindow::SaveProject()
{
    if (currentPath_.isEmpty()) {
        SaveProjectAs();
    } else {
        SaveProjectFile(currentPath_);
    }
}

void MainWindow::SaveProjectAs()
{
    QString suggested = currentPath_.isEmpty() ? QStringLiteral("kachakacha-project.kcd") : currentPath_;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("プロジェクトを保存"), suggested, QStringLiteral("kachakachaCAD (*.kcd)"));
    if (!path.isEmpty()) {
        SaveProjectFile(path.endsWith(".kcd", Qt::CaseInsensitive) ? path : path + ".kcd");
    }
}

void MainWindow::ExportPlanar(bool dxf)
{
    try {
        const QString planeText = exportPlane_->currentText();
        const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(planeText));
        if (!plane.has_value()) {
            throw std::invalid_argument("出力する作業平面を選択してください。");
        }

        std::vector<kachakacha::model::NamedWire> wires;
        if (exportScope_->currentIndex() == 0) {
            for (const auto& wire : project_.Wires()) {
                if (!wire.metadata.construction && WireLiesOnWorkPlane(wire.wire, *plane)) {
                    wires.push_back(wire);
                }
            }
        } else {
            for (const CadSelection& selection : viewport_->Selections()) {
                if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Wires().size())
                    && !project_.Wires()[selection.index].metadata.construction) {
                    wires.push_back(project_.Wires()[selection.index]);
                }
            }
        }
        if (wires.empty()) {
            throw std::invalid_argument(exportScope_->currentIndex() == 0
                    ? "この出力面上にワイヤーがありません。"
                    : "出力するワイヤーを3D画面で選択してください。");
        }

        const QString extension = dxf ? QStringLiteral(".dxf") : QStringLiteral(".svg");
        const QString filter = dxf ? QStringLiteral("DXF図面 (*.dxf)") : QStringLiteral("SVG図面 (*.svg)");
        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        const QString suggested = suggestedDirectory + planeText + extension;
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("1:1板材図面を保存"), suggested, filter);
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(extension, Qt::CaseInsensitive)) {
            path += extension;
        }

        const std::filesystem::path nativePath(path.toStdWString());
        std::ofstream output(nativePath, std::ios::binary);
        if (!output) {
            throw std::runtime_error("出力ファイルを開けませんでした。");
        }
        if (dxf) {
            WritePlanarDxf(output, *plane, wires);
        } else {
            WritePlanarSvg(output, *plane, wires);
        }
        output.close();
        if (!output) {
            throw std::runtime_error("出力ファイルの保存に失敗しました。");
        }
        exportSummary_->setText(QStringLiteral("%1本を保存: %2").arg(wires.size()).arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("1:1図面を保存しました: %1").arg(path), 5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

int MainWindow::SelectedPlateIndexForExport() const
{
    std::vector<int> plateIndices;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            plateIndices.push_back(selection.index);
        }
    }
    std::sort(plateIndices.begin(), plateIndices.end());
    plateIndices.erase(std::unique(plateIndices.begin(), plateIndices.end()), plateIndices.end());
    if (plateIndices.size() != 1) {
        throw std::invalid_argument("展開する板材を1枚だけ選択してください。");
    }
    return plateIndices.front();
}

void MainWindow::ExportSelectedBody(bool step)
{
    try {
        kachakacha::occt::ModelShapeSelection exportSelection;
        if (modelExportScope_->currentIndex() == 0) {
            for (const CadSelection& selection : viewport_->Selections()) {
                if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Plates().size())) {
                    exportSelection.plateNames.push_back(project_.Plates()[selection.index].name);
                } else if (selection.kind == CadSelectionKind::Body && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Bodies().size())) {
                    exportSelection.bodyNames.push_back(project_.Bodies()[selection.index].name);
                }
            }
        } else {
            for (const auto& plate : project_.Plates()) {
                if (plate.visible) {
                    exportSelection.plateNames.push_back(plate.name);
                }
            }
            for (const auto& body : project_.Bodies()) {
                if (body.visible) {
                    exportSelection.bodyNames.push_back(body.name);
                }
            }
        }
        if (exportSelection.Empty()) {
            throw std::invalid_argument(modelExportScope_->currentIndex() == 0
                    ? "出力する板材または治具を3D画面で選択してください。"
                    : "表示中の板材または治具がありません。");
        }

        statusBar()->showMessage(QStringLiteral("選択した3D部品の閉形状を検査しています..."));
        QApplication::processEvents();
        const auto analysis = kachakacha::occt::AnalyzeModelShape(
            project_, exportSelection, 0.01);
        if (!analysis.validBRep || !analysis.closedSolid) {
            throw std::runtime_error("選択した3D部品に閉じていない形状があるため出力できません。");
        }

        const QString extension = step ? QStringLiteral(".step") : QStringLiteral(".stl");
        const QString filter = step ? QStringLiteral("STEP CAD形状 (*.step *.stp)")
                                    : QStringLiteral("STL 3Dプリント形状 (*.stl)");
        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        QString suggestedName = QStringLiteral("selected-model");
        if (analysis.partCount == 1) {
            suggestedName = !exportSelection.plateNames.empty()
                ? ToQString(exportSelection.plateNames.front())
                : ToQString(exportSelection.bodyNames.front());
        }
        QString path = QFileDialog::getSaveFileName(
            this,
            step ? QStringLiteral("選択3DモデルのSTEPを保存") : QStringLiteral("選択3DモデルのSTLを保存"),
            suggestedDirectory + suggestedName + extension,
            filter);
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(extension, Qt::CaseInsensitive)
            && !(step && path.endsWith(QStringLiteral(".stp"), Qt::CaseInsensitive))) {
            path += extension;
        }

        statusBar()->showMessage(step
            ? QStringLiteral("CAD形状をSTEPへ書き出しています...")
            : QStringLiteral("出力時だけ高精度メッシュを作りSTLへ書き出しています..."));
        QApplication::processEvents();
        const std::filesystem::path outputPath(path.toStdWString());
        if (step) {
            kachakacha::occt::WriteModelStep(outputPath, project_, exportSelection);
        } else {
            kachakacha::occt::WriteModelStl(outputPath, project_, exportSelection);
        }
        bodyExportSummary_->setStyleSheet("color: #35664a;");
        bodyExportSummary_->setText(
            QStringLiteral("%1部品（板材%2 / 治具%3）| 体積 %4 mm³ | 保存済み: %5")
                .arg(analysis.partCount)
                .arg(analysis.plateCount)
                .arg(analysis.bodyCount)
                .arg(analysis.volumeCubicMillimeters, 0, 'f', 1)
                .arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("選択した3Dモデルを保存しました: %1").arg(path), 5000);
    } catch (const std::exception& error) {
        if (bodyExportSummary_ != nullptr) {
            bodyExportSummary_->setStyleSheet("color: #a32734;");
            bodyExportSummary_->setText(QStringLiteral("3D出力不可: %1").arg(QString::fromUtf8(error.what())));
        }
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

bool MainWindow::ConfirmPlateFlatPatternAccuracy(const kachakacha::io::PlateFlatPattern& pattern)
{
    constexpr double warningToleranceMillimeters = 0.1;
    const double estimatedError = pattern.analysis.MaximumEstimatedErrorMillimeters();
    if ((pattern.analysis.classification != PlateDevelopability::DoubleCurved
            || pattern.analysis.pieceCount > 1)
        && estimatedError <= warningToleranceMillimeters) {
        return true;
    }
    const QString reason = pattern.analysis.classification == PlateDevelopability::DoubleCurved
        ? QStringLiteral("この板材は二方向に曲がるため、平面へ正確には展開できません。")
        : QStringLiteral("推定ずれが工作許容値 0.10 mm を超えています。");
    return QMessageBox::warning(
        this,
        QStringLiteral("展開精度の確認"),
        QStringLiteral("%1\n\n最大推定ずれ: %2 mm\n分割または成形代を検討してください。\n\nこのまま近似図を保存しますか？")
            .arg(reason)
            .arg(estimatedError, 0, 'f', 3),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel) == QMessageBox::Yes;
}

PlateFlatPatternOptions MainWindow::PlateFlatPatternOptionsFromUi() const
{
    PlateFlatPatternOptions options;
    if (plateFlatPatternAutoRelief_ != nullptr) {
        options.includeAutomaticReliefCuts = plateFlatPatternAutoRelief_->isChecked();
    }
    if (plateFlatPatternReliefStyle_ != nullptr) {
        options.automaticReliefStyle = static_cast<AutomaticReliefStyle>(
            plateFlatPatternReliefStyle_->currentData().toInt());
    }
    if (plateFlatPatternFoldSpacing_ != nullptr) {
        options.foldSpacingMillimeters = plateFlatPatternFoldSpacing_->value();
    }
    if (plateFlatPatternFidelity_ != nullptr) {
        options.papercraftFidelity = plateFlatPatternFidelity_->value();
    }
    if (plateFlatPatternReliefSpacing_ != nullptr) {
        options.reliefCutSpacingMillimeters = plateFlatPatternReliefSpacing_->value();
    }
    if (plateFlatPatternReliefDepth_ != nullptr) {
        options.reliefCutDepthRatio = plateFlatPatternReliefDepth_->value() / 100.0;
    }
    if (plateFlatPatternNotchAngle_ != nullptr) {
        options.reliefNotchAngleDegrees = plateFlatPatternNotchAngle_->value();
    }
    if (plateFlatPatternNotchTipRadius_ != nullptr) {
        options.reliefNotchTipRadiusMillimeters = plateFlatPatternNotchTipRadius_->value();
    }
    if (plateFlatPatternMinimumBendAngle_ != nullptr) {
        options.minimumFoldAngleDegrees = plateFlatPatternMinimumBendAngle_->value();
    }
    return options;
}

void MainWindow::UpdatePlateAssemblyGuidePreview()
{
    if (viewport_ == nullptr || plateAssemblyGuidePreview_ == nullptr
        || toolsTabs_ == nullptr || toolsTabs_->currentIndex() != 6
        || !plateAssemblyGuidePreview_->isChecked()) {
        if (viewport_ != nullptr) {
            viewport_->SetPlateAssemblyGuidePreview(std::nullopt, {}, {});
        }
        return;
    }

    std::vector<int> plateIndices;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            plateIndices.push_back(selection.index);
        }
    }
    std::sort(plateIndices.begin(), plateIndices.end());
    plateIndices.erase(std::unique(plateIndices.begin(), plateIndices.end()), plateIndices.end());
    if (plateIndices.size() != 1 || !project_.Plates()[plateIndices.front()].visible) {
        viewport_->SetPlateAssemblyGuidePreview(std::nullopt, {}, {});
        return;
    }

    try {
        PlateFlatPatternOptions options = PlateFlatPatternOptionsFromUi();
        options.uSegments = 96;
        options.vSegments = 48;
        options.openingSamples = 96;
        const auto guide = BuildPlateAssemblyGuide(
            project_, project_.Plates()[plateIndices.front()], options);
        std::vector<std::vector<Vector3>> foldLines;
        std::vector<std::vector<Vector3>> reliefCuts;
        foldLines.reserve(guide.foldLines.size());
        reliefCuts.reserve(guide.reliefCuts.size());
        for (const auto& path : guide.foldLines) {
            foldLines.push_back(path.points);
        }
        for (const auto& path : guide.reliefCuts) {
            reliefCuts.push_back(path.points);
        }
        for (const auto& path : guide.splitLines) {
            reliefCuts.push_back(path.points);
        }
        viewport_->SetPlateAssemblyGuidePreview(
            plateIndices.front(), std::move(foldLines), std::move(reliefCuts));
    } catch (const std::exception&) {
        viewport_->SetPlateAssemblyGuidePreview(std::nullopt, {}, {});
    }
}

void MainWindow::ExportSelectedPlate(bool dxf)
{
    try {
        const auto& plate = project_.Plates()[SelectedPlateIndexForExport()];
        const PlateFlatPatternOptions flatOptions = PlateFlatPatternOptionsFromUi();
        const auto pattern = BuildPlateFlatPattern(project_, plate, flatOptions);
        if (!ConfirmPlateFlatPatternAccuracy(pattern)) {
            return;
        }
        const double estimatedError = pattern.analysis.MaximumEstimatedErrorMillimeters();

        const QString extension = dxf ? QStringLiteral(".dxf") : QStringLiteral(".svg");
        const QString filter = dxf ? QStringLiteral("DXF展開図 (*.dxf)") : QStringLiteral("SVG展開図 (*.svg)");
        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("板材の1:1展開図を保存"),
            suggestedDirectory + ToQString(plate.name) + QStringLiteral("_flat") + extension,
            filter);
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(extension, Qt::CaseInsensitive)) {
            path += extension;
        }

        std::ofstream output(std::filesystem::path(path.toStdWString()), std::ios::binary);
        if (!output) {
            throw std::runtime_error("出力ファイルを開けませんでした。");
        }
        if (dxf) {
            WritePlateFlatPatternDxf(output, pattern);
        } else {
            WritePlateFlatPatternSvg(output, pattern, flatOptions);
        }
        output.close();
        if (!output) {
            throw std::runtime_error("出力ファイルの保存に失敗しました。");
        }
        plateFlatPatternSummary_->setText(
            QStringLiteral("%1 | 展開片 %2枚 | 最大推定ずれ %3 mm | 開口 %4個 | 折り線 %5本 | 切れ目 %6本 | 保存済み: %7")
                .arg(ToQString(plate.name))
                .arg(pattern.analysis.pieceCount)
                .arg(estimatedError, 0, 'f', 3)
                .arg(pattern.openings.size())
                .arg(pattern.foldLines.size())
                .arg(pattern.reliefCuts.size())
                .arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("板材の1:1展開図を保存しました: %1").arg(path), 5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ExportSelectedPlatePdf()
{
    try {
        const auto& plate = project_.Plates()[SelectedPlateIndexForExport()];
        const auto pattern = BuildPlateFlatPattern(project_, plate, PlateFlatPatternOptionsFromUi());
        if (!ConfirmPlateFlatPatternAccuracy(pattern)) {
            return;
        }

        PlatePdfOptions options;
        options.pageSize = static_cast<QPageSize::PageSizeId>(platePdfPaper_->currentData().toInt());
        options.overlapMillimeters = platePdfOverlap_->value();
        const auto pdfLayout = CalculatePlatePdfLayout(pattern, options);

        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("板材の1:1印刷PDFを保存"),
            suggestedDirectory + ToQString(plate.name) + QStringLiteral("_flat.pdf"),
            QStringLiteral("PDF図面 (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".pdf");
        }

        WritePlateFlatPatternPdf(path, pattern, options);
        plateFlatPatternSummary_->setText(
            QStringLiteral("%1 | 展開片 %2枚 | 最大推定ずれ %3 mm | 開口 %4個 | PDF %5ページ")
                .arg(ToQString(plate.name))
                .arg(pattern.analysis.pieceCount)
                .arg(pattern.analysis.MaximumEstimatedErrorMillimeters(), 0, 'f', 3)
                .arg(pattern.openings.size())
                .arg(pdfLayout.PageCount()));
        statusBar()->showMessage(
            QStringLiteral("1:1印刷PDFを保存しました: %1（%2ページ）")
                .arg(path)
                .arg(pdfLayout.PageCount()),
            5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::CreateSelectedPlateFlatPatternModel()
{
    try {
        const int sourceIndex = SelectedPlateIndexForExport();
        const auto sourcePlate = project_.Plates()[sourceIndex];
        const PlateFlatPatternOptions options = PlateFlatPatternOptionsFromUi();
        const auto pattern = BuildPlateFlatPattern(project_, sourcePlate, options);
        if (!ConfirmPlateFlatPatternAccuracy(pattern)) {
            return;
        }
        if (plateFlatPatternPlane_ == nullptr || plateFlatPatternPlane_->currentText().isEmpty()) {
            throw std::invalid_argument("展開部材を配置する作業平面を選択してください。");
        }
        const std::optional<WorkPlane> targetPlane = project_.FindWorkPlane(
            ToName(plateFlatPatternPlane_->currentText()));
        if (!targetPlane.has_value()) {
            throw std::invalid_argument("展開部材の配置平面が見つかりません。");
        }
        const std::string prefix = ToName(plateFlatPatternName_->text().trimmed());
        if (prefix.empty()) {
            throw std::invalid_argument("展開部材名を入力してください。");
        }

        Project candidate = project_;
        const auto result = AddPlateFlatPatternModel(
            candidate,
            sourcePlate,
            pattern,
            *targetPlane,
            prefix,
            plateFlatPatternCutWidth_->value());
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(true);
        const auto platePosition = std::find_if(
            project_.Plates().begin(), project_.Plates().end(), [&](const auto& plate) {
                return plate.name == result.plateName;
            });
        if (platePosition != project_.Plates().end()) {
            UpdateSelection({
                CadSelectionKind::Plate,
                static_cast<int>(std::distance(project_.Plates().begin(), platePosition))}, true);
        }
        plateFlatPatternName_->setText(SuggestedDirectGroupName(QStringLiteral("developed")));
        plateFlatPatternSummary_->setStyleSheet("color: #35664a;");
        plateFlatPatternSummary_->setText(
            QStringLiteral("%1を作成 | 展開板%2枚 | 開口%3 | 折り線%4 | 切れ目%5 | 3D板あり")
                .arg(ToQString(result.plateName))
                .arg(result.plateNames.size())
                .arg(pattern.openings.size())
                .arg(result.foldWireNames.size())
                .arg(result.reliefCutWireNames.size()));
        statusBar()->showMessage(
            QStringLiteral("展開ワイヤーと厚み付き3D板を作成しました: %1")
                .arg(ToQString(result.plateName)),
            5000);
    } catch (const std::exception& error) {
        plateFlatPatternSummary_->setStyleSheet("color: #a32734;");
        plateFlatPatternSummary_->setText(QStringLiteral("展開部材を作成できません: %1")
            .arg(QString::fromUtf8(error.what())));
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

bool MainWindow::SaveProjectFile(const QString& path)
{
    try {
        std::ostringstream serialized;
        WriteProjectScript(serialized, project_);
        if (!serialized) {
            throw std::runtime_error("保存中にエラーが発生しました。");
        }

        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly)) {
            throw std::runtime_error(output.errorString().toUtf8().constData());
        }
        const QByteArray contents = QByteArray::fromStdString(serialized.str());
        if (output.write(contents) != contents.size() || !output.commit()) {
            throw std::runtime_error(output.errorString().toUtf8().constData());
        }
        currentPath_ = path;
        modified_ = false;
        RemoveAutosave();
        setWindowTitle(QStringLiteral("kachakachaCAD - %1").arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("保存しました: %1").arg(path), 4000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("保存エラー"), QString::fromUtf8(error.what()));
        return false;
    }
}

void MainWindow::SetReferenceFromSelection()
{
    const auto& selections = viewport_->Selections();
    if (selections.size() != 1 || selections.front().kind != CadSelectionKind::Wire
        || selections.front().index < 0
        || selections.front().index >= static_cast<int>(project_.Wires().size())
        || project_.Wires()[selections.front().index].wire.Kind() != WireKind::Line) {
        statusBar()->showMessage(QStringLiteral("基準にする直線を1本だけ選択してください"), 4000);
        return;
    }

    referenceWireName_ = project_.Wires()[selections.front().index].name;
    RefreshReference();
    UpdateSelection({}, true);
    statusBar()->showMessage(QStringLiteral("基準線を設定しました: %1").arg(ToQString(*referenceWireName_)), 3000);
}

void MainWindow::UpdateMeasurement(const std::vector<MeasurementPick>& picks)
{
    if (measurementStateLabel_ == nullptr || measurementResultLabel_ == nullptr
        || measurementMetric_ == nullptr || measurementSaveButton_ == nullptr) {
        return;
    }

    lastMeasurementPicks_ = picks;
    measurementMetric_->clear();
    measurementMetric_->setEnabled(false);
    measurementSaveButton_->setEnabled(false);
    const auto addMetric = [this](QString text, ReferenceDimensionKind kind) {
        measurementMetric_->addItem(std::move(text), static_cast<int>(kind));
        measurementMetric_->setEnabled(true);
        measurementSaveButton_->setEnabled(!measurementName_->text().trimmed().isEmpty());
    };

    const bool pointMode = viewport_->CurrentMeasurementMode() == MeasurementMode::TwoPoints;
    const bool angleMode = viewport_->CurrentMeasurementMode() == MeasurementMode::ThreePointsAngle;
    if (picks.empty()) {
        measurementStateLabel_->setText(pointMode ? QStringLiteral("1点目")
            : angleMode ? QStringLiteral("角度の頂点")
                        : QStringLiteral("1つ目の要素"));
        measurementResultLabel_->setText(QStringLiteral("未測定"));
        viewport_->SetMeasurementOverlay(std::nullopt, std::nullopt, {});
        return;
    }

    const auto pointLines = [](Vector3 point) {
        return QStringList{
            QStringLiteral("X  %1 mm").arg(Number(point.x)),
            QStringLiteral("Y  %1 mm").arg(Number(point.y)),
            QStringLiteral("Z  %1 mm").arg(Number(point.z)),
        };
    };
    const auto requireWire = [this](const MeasurementPick& pick) -> const Wire& {
        if (pick.kind != MeasurementPickKind::Wire || pick.index < 0
            || pick.index >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("測定するワイヤーが見つかりません。");
        }
        return project_.Wires()[pick.index].wire;
    };
    const auto requirePlane = [this](const MeasurementPick& pick) -> const WorkPlane& {
        if (pick.kind != MeasurementPickKind::WorkPlane || pick.index < 0
            || pick.index >= static_cast<int>(project_.WorkPlanes().size())) {
            throw std::invalid_argument("測定する作業平面が見つかりません。");
        }
        return project_.WorkPlanes()[pick.index].plane;
    };

    try {
        if (angleMode) {
            if (picks.size() == 1) {
                measurementStateLabel_->setText(QStringLiteral("1方向目の点"));
                measurementResultLabel_->setText(
                    QStringLiteral("角度の頂点\n%1").arg(pointLines(picks[0].point).join('\n')));
                viewport_->SetMeasurementOverlay(
                    picks[0].point, std::nullopt, QStringLiteral("頂点"));
                return;
            }
            if (picks.size() == 2) {
                const double firstLength = (picks[1].point - picks[0].point).Length();
                measurementStateLabel_->setText(QStringLiteral("2方向目の点"));
                measurementResultLabel_->setText(QStringList{
                    QStringLiteral("1方向目  %1 mm").arg(Number(firstLength)),
                    QStringLiteral("次に2方向目の点を指定"),
                }.join('\n'));
                viewport_->SetMeasurementOverlay(
                    picks[0].point, picks[1].point,
                    QStringLiteral("%1 mm").arg(Number(firstLength)));
                return;
            }
            const auto angle = MeasureThreePointAngle(
                picks[0].point, picks[1].point, picks[2].point);
            const double firstLength = (picks[1].point - picks[0].point).Length();
            const double secondLength = (picks[2].point - picks[0].point).Length();
            measurementStateLabel_->setText(QStringLiteral("3D角度"));
            measurementResultLabel_->setText(QStringList{
                QStringLiteral("3D角度  %1°").arg(Number(angle.directedDegrees)),
                QStringLiteral("最小角  %1°").arg(Number(angle.acuteDegrees)),
                QStringLiteral("頂点→1方向目  %1 mm").arg(Number(firstLength)),
                QStringLiteral("頂点→2方向目  %1 mm").arg(Number(secondLength)),
            }.join('\n'));
            viewport_->SetMeasurementAngleOverlay(
                picks[0].point, picks[1].point, picks[2].point,
                QStringLiteral("%1°").arg(Number(angle.directedDegrees)));
            return;
        }

        if (pointMode) {
            if (picks.size() == 1) {
                measurementStateLabel_->setText(QStringLiteral("2点目"));
                measurementResultLabel_->setText(pointLines(picks[0].point).join('\n'));
                viewport_->SetMeasurementOverlay(picks[0].point, std::nullopt, QStringLiteral("P1"));
                return;
            }

            const Vector3 delta = picks[1].point - picks[0].point;
            const double distance = delta.Length();
            QStringList result{
                QStringLiteral("3D距離  %1 mm").arg(Number(distance)),
                QStringLiteral("ΔX  %1 mm").arg(Number(delta.x)),
                QStringLiteral("ΔY  %1 mm").arg(Number(delta.y)),
                QStringLiteral("ΔZ  %1 mm").arg(Number(delta.z)),
                QStringLiteral("XY投影  %1 mm").arg(Number(std::hypot(delta.x, delta.y))),
                QStringLiteral("XZ投影  %1 mm").arg(Number(std::hypot(delta.x, delta.z))),
                QStringLiteral("YZ投影  %1 mm").arg(Number(std::hypot(delta.y, delta.z))),
            };
            if (distance > 1.0e-12) {
                result << QStringLiteral("X/Y/Z軸との角度  %1° / %2° / %3°")
                    .arg(Number(MeasureDirectionsAngle(delta, {1.0, 0.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(delta, {0.0, 1.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(delta, {0.0, 0.0, 1.0}).directedDegrees));
                result << QStringLiteral("XY / XZ / YZ方向角  %1° / %2° / %3°")
                    .arg(Number(std::atan2(delta.y, delta.x) * 180.0 / kPi))
                    .arg(Number(std::atan2(delta.z, delta.x) * 180.0 / kPi))
                    .arg(Number(std::atan2(delta.z, delta.y) * 180.0 / kPi));
            }
            measurementStateLabel_->setText(QStringLiteral("2点間"));
            measurementResultLabel_->setText(result.join('\n'));
            addMetric(QStringLiteral("残す値: 3D距離"), ReferenceDimensionKind::PointDistance);
            viewport_->SetMeasurementOverlay(
                picks[0].point,
                picks[1].point,
                QStringLiteral("%1 mm").arg(Number(distance)));
            return;
        }

        if (picks.size() == 1) {
            const MeasurementPick& pick = picks[0];
            measurementStateLabel_->setText(QStringLiteral("1要素"));
            if (pick.kind == MeasurementPickKind::Point) {
                measurementResultLabel_->setText(pointLines(pick.point).join('\n'));
                viewport_->SetMeasurementOverlay(pick.point, std::nullopt, QStringLiteral("点"));
                return;
            }
            if (pick.kind == MeasurementPickKind::WorkPlane) {
                const WorkPlane& plane = requirePlane(pick);
                measurementResultLabel_->setText(QStringList{
                    ToQString(project_.WorkPlanes()[pick.index].name),
                    QStringLiteral("原点  %1").arg(VectorText(plane.Origin())),
                    QStringLiteral("法線  %1").arg(VectorText(plane.Normal())),
                }.join('\n'));
                viewport_->SetMeasurementOverlay(plane.Origin(), std::nullopt, QStringLiteral("作業平面"));
                return;
            }

            const Wire& wire = requireWire(pick);
            const double length = MeasureWireLength(wire);
            const Vector3 tangent = MeasureWireTangent(wire, pick.wireParameter);
            const std::optional<Vector3> normal = MeasureWireCurvatureNormal(
                wire, pick.wireParameter);
            QStringList result{
                ToQString(project_.Wires()[pick.index].name),
                QStringLiteral("全長  %1 mm").arg(Number(length)),
            };
            const std::optional<double> radius = MeasureWireRadius(wire);
            if (radius.has_value()) {
                addMetric(QStringLiteral("残す値: 半径"), ReferenceDimensionKind::WireRadius);
                result << QStringLiteral("半径  %1 mm").arg(Number(*radius));
                result << QStringLiteral("直径  %1 mm").arg(Number(*radius * 2.0));
                result << QStringLiteral("中心角  %1°")
                    .arg(Number(std::abs(wire.ArcData().sweepAngleRadians) * 180.0 / kPi));
            }
            result << QStringLiteral("クリック位置の接線とX/Y/Z軸  %1° / %2° / %3°")
                .arg(Number(MeasureDirectionsAngle(tangent, {1.0, 0.0, 0.0}).directedDegrees))
                .arg(Number(MeasureDirectionsAngle(tangent, {0.0, 1.0, 0.0}).directedDegrees))
                .arg(Number(MeasureDirectionsAngle(tangent, {0.0, 0.0, 1.0}).directedDegrees));
            if (normal.has_value()) {
                result << QStringLiteral("曲率法線とX/Y/Z軸  %1° / %2° / %3°")
                    .arg(Number(MeasureDirectionsAngle(*normal, {1.0, 0.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(*normal, {0.0, 1.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(*normal, {0.0, 0.0, 1.0}).directedDegrees));
            } else {
                result << QStringLiteral("曲率法線  なし（直線または曲率0）");
            }
            addMetric(QStringLiteral("残す値: ワイヤー全長"), ReferenceDimensionKind::WireLength);
            measurementResultLabel_->setText(result.join('\n'));
            if (radius.has_value()) {
                viewport_->SetMeasurementOverlay(
                    wire.ArcData().center,
                    pick.point,
                    QStringLiteral("R %1 mm").arg(Number(*radius)));
            } else {
                viewport_->SetMeasurementOverlay(
                    wire.Start(),
                    wire.End(),
                    QStringLiteral("L %1 mm").arg(Number(length)));
            }
            return;
        }

        const MeasurementPick& first = picks[0];
        const MeasurementPick& second = picks[1];
        measurementStateLabel_->setText(QStringLiteral("2要素"));
        if (first.kind == MeasurementPickKind::Wire && second.kind == MeasurementPickKind::Wire) {
            const Wire& firstWire = requireWire(first);
            const Wire& secondWire = requireWire(second);
            const auto distance = MeasureWireToWireDistance(firstWire, secondWire);
            const Vector3 firstTangent = MeasureWireTangent(firstWire, first.wireParameter);
            const Vector3 secondTangent = MeasureWireTangent(secondWire, second.wireParameter);
            const auto angle = MeasureDirectionsAngle(firstTangent, secondTangent);
            const auto firstNormal = MeasureWireCurvatureNormal(firstWire, first.wireParameter);
            const auto secondNormal = MeasureWireCurvatureNormal(secondWire, second.wireParameter);
            QStringList result{
                QStringLiteral("最短距離  %1 mm").arg(Number(distance.distanceMillimeters)),
                QStringLiteral("クリック点間  %1 mm").arg(Number((second.point - first.point).Length())),
                QStringLiteral("クリック位置の接線角  %1°").arg(Number(angle.directedDegrees)),
                QStringLiteral("最小交角  %1°").arg(Number(angle.acuteDegrees)),
            };
            if (firstNormal.has_value() && secondNormal.has_value()) {
                const auto normalAngle = MeasureDirectionsAngle(*firstNormal, *secondNormal);
                result << QStringLiteral("曲率法線同士の角度  %1°").arg(Number(normalAngle.directedDegrees));
            } else if (firstNormal.has_value()) {
                const auto normalAngle = MeasureDirectionsAngle(*firstNormal, secondTangent);
                result << QStringLiteral("1本目の曲率法線と2本目の接線  %1°")
                    .arg(Number(normalAngle.directedDegrees));
            } else if (secondNormal.has_value()) {
                const auto normalAngle = MeasureDirectionsAngle(firstTangent, *secondNormal);
                result << QStringLiteral("1本目の接線と2本目の曲率法線  %1°")
                    .arg(Number(normalAngle.directedDegrees));
            }
            measurementResultLabel_->setText(result.join('\n'));
            addMetric(QStringLiteral("残す値: 最短距離"), ReferenceDimensionKind::WireDistance);
            addMetric(QStringLiteral("残す値: クリック位置の接線角"), ReferenceDimensionKind::WireAngle);
            viewport_->SetMeasurementOverlay(
                distance.firstPoint,
                distance.secondPoint,
                QStringLiteral("最短 %1 mm").arg(Number(distance.distanceMillimeters)));
            return;
        }

        const auto pointAndWire = [&](const MeasurementPick& pointPick, const MeasurementPick& wirePick) {
            const auto distance = MeasurePointToWireDistance(pointPick.point, requireWire(wirePick));
            measurementResultLabel_->setText(QStringList{
                QStringLiteral("点からワイヤーの最短距離  %1 mm").arg(Number(distance.distanceMillimeters)),
                QStringLiteral("クリック点間  %1 mm").arg(Number((wirePick.point - pointPick.point).Length())),
            }.join('\n'));
            addMetric(QStringLiteral("残す値: 点からワイヤーの最短距離"),
                ReferenceDimensionKind::PointWireDistance);
            viewport_->SetMeasurementOverlay(
                distance.firstPoint,
                distance.secondPoint,
                QStringLiteral("%1 mm").arg(Number(distance.distanceMillimeters)));
        };
        if (first.kind == MeasurementPickKind::Point && second.kind == MeasurementPickKind::Wire) {
            pointAndWire(first, second);
            return;
        }
        if (first.kind == MeasurementPickKind::Wire && second.kind == MeasurementPickKind::Point) {
            pointAndWire(second, first);
            return;
        }

        const auto wireAndPlane = [&](const MeasurementPick& wirePick, const MeasurementPick& planePick) {
            const Wire& wire = requireWire(wirePick);
            const WorkPlane& plane = requirePlane(planePick);
            const double signedDistance = MeasureSignedPointToPlaneDistance(wirePick.point, plane);
            const double angle = MeasureDirectionToPlaneAngleDegrees(
                MeasureWireTangent(wire, wirePick.wireParameter), plane);
            measurementResultLabel_->setText(QStringList{
                QStringLiteral("線・接線と平面の角度  %1°").arg(Number(angle)),
                QStringLiteral("クリック位置から平面  %1 mm").arg(Number(std::abs(signedDistance))),
            }.join('\n'));
            addMetric(QStringLiteral("残す値: 線・接線と平面の角度"),
                ReferenceDimensionKind::WirePlaneAngle);
            addMetric(QStringLiteral("残す値: クリック位置から平面"),
                ReferenceDimensionKind::PointPlaneDistance);
            viewport_->SetMeasurementOverlay(
                wirePick.point,
                wirePick.point - plane.Normal() * signedDistance,
                QStringLiteral("%1°").arg(Number(angle)));
        };
        if (first.kind == MeasurementPickKind::Wire && second.kind == MeasurementPickKind::WorkPlane) {
            wireAndPlane(first, second);
            return;
        }
        if (first.kind == MeasurementPickKind::WorkPlane && second.kind == MeasurementPickKind::Wire) {
            wireAndPlane(second, first);
            return;
        }

        const auto pointAndPlane = [&](const MeasurementPick& pointPick, const MeasurementPick& planePick) {
            const WorkPlane& plane = requirePlane(planePick);
            const double signedDistance = MeasureSignedPointToPlaneDistance(pointPick.point, plane);
            measurementResultLabel_->setText(QStringLiteral("点から平面  %1 mm")
                .arg(Number(std::abs(signedDistance))));
            addMetric(QStringLiteral("残す値: 点から平面の距離"),
                ReferenceDimensionKind::PointPlaneDistance);
            viewport_->SetMeasurementOverlay(
                pointPick.point,
                pointPick.point - plane.Normal() * signedDistance,
                QStringLiteral("%1 mm").arg(Number(std::abs(signedDistance))));
        };
        if (first.kind == MeasurementPickKind::Point && second.kind == MeasurementPickKind::WorkPlane) {
            pointAndPlane(first, second);
            return;
        }
        if (first.kind == MeasurementPickKind::WorkPlane && second.kind == MeasurementPickKind::Point) {
            pointAndPlane(second, first);
            return;
        }

        if (first.kind == MeasurementPickKind::WorkPlane
            && second.kind == MeasurementPickKind::WorkPlane) {
            const WorkPlane& firstPlane = requirePlane(first);
            const WorkPlane& secondPlane = requirePlane(second);
            const double angle = MeasurePlaneToPlaneAngleDegrees(firstPlane, secondPlane);
            QStringList result{QStringLiteral("平面同士の角度  %1°").arg(Number(angle))};
            if (angle <= 1.0e-7) {
                result << QStringLiteral("平行間隔  %1 mm")
                    .arg(Number(std::abs(MeasureSignedPointToPlaneDistance(firstPlane.Origin(), secondPlane))));
            }
            measurementResultLabel_->setText(result.join('\n'));
            addMetric(QStringLiteral("残す値: 平面同士の角度"), ReferenceDimensionKind::PlaneAngle);
            if (angle <= 1.0e-7) {
                addMetric(QStringLiteral("残す値: 平行間隔"), ReferenceDimensionKind::PlaneDistance);
            }
            viewport_->SetMeasurementOverlay(
                firstPlane.Origin(),
                secondPlane.Origin(),
                QStringLiteral("%1°").arg(Number(angle)));
            return;
        }

        measurementResultLabel_->setText(QStringLiteral("この組合せは測定できません"));
    } catch (const std::exception& error) {
        measurementResultLabel_->setText(QString::fromUtf8(error.what()));
        viewport_->SetMeasurementOverlay(std::nullopt, std::nullopt, {});
    }
}

void MainWindow::SaveCurrentMeasurement()
{
    try {
        ValidateObjectName(measurementName_->text());
        if (measurementMetric_->currentIndex() < 0) {
            throw std::invalid_argument("残す測定値を選択してください。");
        }

        ReferenceDimension dimension;
        dimension.name = ToName(measurementName_->text());
        dimension.kind = static_cast<ReferenceDimensionKind>(measurementMetric_->currentData().toInt());

        const auto makeReference = [this](const MeasurementPick& pick) {
            DimensionReference reference;
            reference.wireParameter = pick.wireParameter;
            if (pick.kind == MeasurementPickKind::Wire
                || (pick.kind == MeasurementPickKind::Point && pick.index >= 0)) {
                if (pick.index < 0 || pick.index >= static_cast<int>(project_.Wires().size())) {
                    throw std::invalid_argument("寸法が参照するワイヤーが見つかりません。");
                }
                reference.kind = DimensionReferenceKind::Wire;
                reference.objectName = project_.Wires()[pick.index].name;
                return reference;
            }
            if (pick.kind == MeasurementPickKind::WorkPlane) {
                if (pick.index < 0 || pick.index >= static_cast<int>(project_.WorkPlanes().size())) {
                    throw std::invalid_argument("寸法が参照する作業平面が見つかりません。");
                }
                reference.kind = DimensionReferenceKind::WorkPlane;
                reference.objectName = project_.WorkPlanes()[pick.index].name;
                return reference;
            }
            reference.kind = DimensionReferenceKind::FixedPoint;
            reference.point = pick.point;
            return reference;
        };
        const auto findPick = [&](MeasurementPickKind kind) -> const MeasurementPick& {
            const auto position = std::find_if(
                lastMeasurementPicks_.begin(), lastMeasurementPicks_.end(),
                [&](const MeasurementPick& pick) { return pick.kind == kind; });
            if (position == lastMeasurementPicks_.end()) {
                throw std::invalid_argument("測定対象が不足しています。もう一度測定してください。");
            }
            return *position;
        };
        const auto findOtherThan = [&](MeasurementPickKind kind) -> const MeasurementPick& {
            const auto position = std::find_if(
                lastMeasurementPicks_.begin(), lastMeasurementPicks_.end(),
                [&](const MeasurementPick& pick) { return pick.kind != kind; });
            if (position == lastMeasurementPicks_.end()) {
                throw std::invalid_argument("測定対象が不足しています。もう一度測定してください。");
            }
            return *position;
        };
        const auto requireTwoPicks = [&] {
            if (lastMeasurementPicks_.size() < 2) {
                throw std::invalid_argument("測定対象が不足しています。もう一度測定してください。");
            }
        };

        switch (dimension.kind) {
        case ReferenceDimensionKind::PointDistance:
            requireTwoPicks();
            dimension.first = makeReference(lastMeasurementPicks_[0]);
            dimension.second = makeReference(lastMeasurementPicks_[1]);
            break;
        case ReferenceDimensionKind::WireLength:
        case ReferenceDimensionKind::WireRadius:
            dimension.first = makeReference(findPick(MeasurementPickKind::Wire));
            break;
        case ReferenceDimensionKind::WireDistance:
        case ReferenceDimensionKind::WireAngle:
            requireTwoPicks();
            dimension.first = makeReference(lastMeasurementPicks_[0]);
            dimension.second = makeReference(lastMeasurementPicks_[1]);
            break;
        case ReferenceDimensionKind::PointWireDistance:
            dimension.first = makeReference(findPick(MeasurementPickKind::Point));
            dimension.second = makeReference(findPick(MeasurementPickKind::Wire));
            break;
        case ReferenceDimensionKind::PointPlaneDistance:
            dimension.first = makeReference(findOtherThan(MeasurementPickKind::WorkPlane));
            dimension.second = makeReference(findPick(MeasurementPickKind::WorkPlane));
            break;
        case ReferenceDimensionKind::WirePlaneAngle:
            dimension.first = makeReference(findPick(MeasurementPickKind::Wire));
            dimension.second = makeReference(findPick(MeasurementPickKind::WorkPlane));
            break;
        case ReferenceDimensionKind::PlaneAngle:
        case ReferenceDimensionKind::PlaneDistance:
            requireTwoPicks();
            dimension.first = makeReference(lastMeasurementPicks_[0]);
            dimension.second = makeReference(lastMeasurementPicks_[1]);
            break;
        }

        Project candidate = project_;
        candidate.AddReferenceDimension(dimension);
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        measurementName_->setText(SuggestedDimensionName());
        statusBar()->showMessage(
            QStringLiteral("参照寸法を残しました: %1").arg(ToQString(dimension.name)), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("参照寸法"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::DeleteSelectedReferenceDimension()
{
    QListWidgetItem* item = referenceDimensionList_->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(QStringLiteral("削除する参照寸法を選択してください"), 2500);
        return;
    }
    const std::string name = ToName(item->data(kDimensionNameRole).toString());
    const auto position = std::find_if(
        project_.ReferenceDimensions().begin(), project_.ReferenceDimensions().end(),
        [&](const ReferenceDimension& dimension) { return dimension.name == name; });
    if (position == project_.ReferenceDimensions().end()) {
        return;
    }
    RecordUndo();
    (void)project_.RemoveReferenceDimension(name);
    MarkModified();
    RefreshModelViews(false);
    statusBar()->showMessage(QStringLiteral("参照寸法を削除しました: %1").arg(ToQString(name)), 2500);
}

void MainWindow::RefreshReferenceDimensions()
{
    if (referenceDimensionList_ == nullptr || viewport_ == nullptr) {
        return;
    }

    const QString selectedName = referenceDimensionList_->currentItem() != nullptr
        ? referenceDimensionList_->currentItem()->data(kDimensionNameRole).toString()
        : QString();
    const QSignalBlocker blocker(referenceDimensionList_);
    referenceDimensionList_->clear();
    std::vector<ReferenceDimensionOverlay> overlays;
    for (const ReferenceDimension& dimension : project_.ReferenceDimensions()) {
        try {
            const auto result = project_.EvaluateReferenceDimension(dimension.name);
            const QString value = ReferenceDimensionValueText(dimension.kind, result.value);
            auto* item = new QListWidgetItem(
                QStringLiteral("%1   %2   %3")
                    .arg(ToQString(dimension.name), ReferenceDimensionKindText(dimension.kind), value),
                referenceDimensionList_);
            item->setData(kDimensionNameRole, ToQString(dimension.name));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(dimension.visible ? Qt::Checked : Qt::Unchecked);
            if (ToQString(dimension.name) == selectedName) {
                referenceDimensionList_->setCurrentItem(item);
            }
            if (dimension.visible) {
                overlays.push_back({
                    result.firstPoint,
                    result.secondPoint,
                    QStringLiteral("%1  %2").arg(ToQString(dimension.name), value),
                });
            }
        } catch (const std::exception& error) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1   [参照切れ]").arg(ToQString(dimension.name)),
                referenceDimensionList_);
            item->setData(kDimensionNameRole, ToQString(dimension.name));
            item->setToolTip(QString::fromUtf8(error.what()));
        }
    }
    referenceDimensionDeleteButton_->setEnabled(referenceDimensionList_->currentRow() >= 0);
    viewport_->SetReferenceDimensionOverlays(std::move(overlays));
}

void MainWindow::ClearReference()
{
    referenceWireName_.reset();
    RefreshReference();
    statusBar()->showMessage(QStringLiteral("基準線を解除しました"), 2500);
}

void MainWindow::RefreshReference()
{
    int referenceIndex = -1;
    if (referenceWireName_.has_value()) {
        for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
            if (project_.Wires()[index].name == *referenceWireName_
                && project_.Wires()[index].wire.Kind() == WireKind::Line) {
                referenceIndex = index;
                break;
            }
        }
    }
    if (referenceIndex < 0) {
        referenceWireName_.reset();
    }

    viewport_->SetReference(referenceIndex >= 0
            ? CadSelection{CadSelectionKind::Wire, referenceIndex}
            : CadSelection{});
    const QString text = referenceWireName_.has_value()
        ? ToQString(*referenceWireName_)
        : QStringLiteral("未設定");
    if (planeReferenceLabel_ != nullptr) {
        planeReferenceLabel_->setText(text);
    }
    if (transformReferenceLabel_ != nullptr) {
        const QString planeName = activePlaneCombo_ != nullptr && !activePlaneCombo_->currentText().isEmpty()
            ? activePlaneCombo_->currentText()
            : QStringLiteral("未設定");
        transformReferenceLabel_->setText(referenceWireName_.has_value()
                ? QStringLiteral("基準線: %1  /  面: %2").arg(text, planeName)
                : QStringLiteral("基準線: 2点指定  /  面: %1").arg(planeName));
    }
    if (lightCaseReferenceLabel_ != nullptr) {
        lightCaseReferenceLabel_->setText(QStringLiteral("基準線: %1").arg(text));
    }
    clearReferenceAction_->setEnabled(referenceWireName_.has_value());
}

void MainWindow::UseReferenceForPlaneRotation()
{
    if (!referenceWireName_.has_value()) {
        statusBar()->showMessage(QStringLiteral("先に直線を選択して基準線に設定してください"), 3500);
        return;
    }
    const auto found = std::find_if(project_.Wires().begin(), project_.Wires().end(), [this](const auto& wire) {
        return wire.name == *referenceWireName_ && wire.wire.Kind() == WireKind::Line;
    });
    if (found == project_.Wires().end()) {
        ClearReference();
        return;
    }

    const Vector3 point = found->wire.Start();
    const Vector3 direction = found->wire.End() - found->wire.Start();
    const std::array<double, 3> pointValues = {point.x, point.y, point.z};
    const std::array<double, 3> directionValues = {direction.x, direction.y, direction.z};
    for (int axis = 0; axis < 3; ++axis) {
        rotateAxisPoint_[axis]->setValue(pointValues[axis]);
        rotateAxisDirection_[axis]->setValue(directionValues[axis]);
    }
    planeMethod_->setCurrentIndex(4);
    const int activeIndex = rotateSourcePlane_->findText(activePlaneCombo_->currentText());
    if (activeIndex >= 0) {
        rotateSourcePlane_->setCurrentIndex(activeIndex);
    }
    statusBar()->showMessage(QStringLiteral("基準線を作業平面の回転軸に設定しました"), 3000);
}

void MainWindow::SetViewportTool(ViewportTool tool)
{
    const bool isTransform = tool == ViewportTool::MoveSelection
        || tool == ViewportTool::CopySelection
        || tool == ViewportTool::MirrorSelection
        || tool == ViewportTool::RotateSelection;
    const bool isSplit = tool == ViewportTool::SplitWire;
    const bool isDirectLineEdit = tool == ViewportTool::TrimWire
        || tool == ViewportTool::ExtendWire;
    const bool isCoincident = tool == ViewportTool::Coincident;
    const bool isTangent = tool == ViewportTool::Tangent;
    const bool isCurvature = tool == ViewportTool::Curvature;
    if (tool != ViewportTool::Select && !isSplit && !isDirectLineEdit
        && !isCoincident && !isTangent && !isCurvature
        && tool != ViewportTool::Measure) {
        const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(activePlaneCombo_->currentText()));
        if (!plane.has_value()) {
            selectToolAction_->setChecked(true);
            viewport_->SetTool(ViewportTool::Select);
            statusBar()->showMessage(QStringLiteral("作図する平面を選択してください"), 3000);
            return;
        }
    }
    if (isTransform || isSplit) {
        std::vector<int> selectedWires;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                selectedWires.push_back(selection.index);
            }
        }
        if (selectedWires.empty()) {
            selectToolAction_->setChecked(true);
            viewport_->SetTool(ViewportTool::Select);
            statusBar()->showMessage(QStringLiteral("3D画面で操作するワイヤーを選択してください"), 3500);
            return;
        }
        if (isSplit && selectedWires.size() != 1) {
            selectToolAction_->setChecked(true);
            viewport_->SetTool(ViewportTool::Select);
            statusBar()->showMessage(QStringLiteral("分割するワイヤーを1本だけ選択してください"), 3500);
            return;
        }
        if (isSplit && project_.Wires()[selectedWires.front()].wire.Kind() == WireKind::Circle) {
            selectToolAction_->setChecked(true);
            viewport_->SetTool(ViewportTool::Select);
            statusBar()->showMessage(QStringLiteral("円の分割は2点指定が必要です。現在は直線・ポリライン・円弧・ベジェを分割できます"), 5000);
            return;
        }
        toolsTabs_->setCurrentIndex(3);
        if (tool == ViewportTool::MirrorSelection && referenceWireName_.has_value()) {
            const auto reference = std::find_if(project_.Wires().begin(), project_.Wires().end(), [this](const auto& wire) {
                return wire.name == *referenceWireName_ && wire.wire.Kind() == WireKind::Line;
            });
            const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(activePlaneCombo_->currentText()));
            if (reference == project_.Wires().end() || !plane.has_value()) {
                RefreshReference();
            } else if (std::abs(plane->Project(reference->wire.Start()).w) > 1.0e-7
                || std::abs(plane->Project(reference->wire.End()).w) > 1.0e-7) {
                selectToolAction_->setChecked(true);
                viewport_->SetTool(ViewportTool::Select);
                statusBar()->showMessage(QStringLiteral("基準線が現在の作図面上にありません"), 4000);
                return;
            } else {
                ApplyViewportMirror(
                    reference->wire.Start(),
                    reference->wire.End() - reference->wire.Start(),
                    plane->Normal());
                selectToolAction_->setChecked(true);
                viewport_->SetTool(ViewportTool::Select);
                return;
            }
        }
    }

    viewport_->SetTool(tool);
    if (isDirectLineEdit) {
        toolsTabs_->setCurrentIndex(3);
    } else if (tool == ViewportTool::Measure) {
        toolsTabs_->setCurrentIndex(8);
    } else if (tool == ViewportTool::MoveGridOrigin) {
        toolsTabs_->setCurrentIndex(0);
        gridPointsVisible_->setChecked(true);
    }
    selectToolAction_->setChecked(tool == ViewportTool::Select);
    pointToolAction_->setChecked(tool == ViewportTool::DrawPoint);
    lineToolAction_->setChecked(tool == ViewportTool::DrawLine);
    polylineToolAction_->setChecked(tool == ViewportTool::DrawPolyline);
    rectangleToolAction_->setChecked(tool == ViewportTool::DrawRectangle);
    circleToolAction_->setChecked(tool == ViewportTool::DrawCircle);
    arcToolAction_->setChecked(tool == ViewportTool::DrawArc);
    bezierToolAction_->setChecked(tool == ViewportTool::DrawBezier);
    splineToolAction_->setChecked(tool == ViewportTool::DrawSpline);
    moveToolAction_->setChecked(tool == ViewportTool::MoveSelection);
    copyToolAction_->setChecked(tool == ViewportTool::CopySelection);
    mirrorToolAction_->setChecked(tool == ViewportTool::MirrorSelection);
    rotateToolAction_->setChecked(tool == ViewportTool::RotateSelection);
    splitToolAction_->setChecked(tool == ViewportTool::SplitWire);
    trimToolAction_->setChecked(tool == ViewportTool::TrimWire);
    extendToolAction_->setChecked(tool == ViewportTool::ExtendWire);
    coincidentToolAction_->setChecked(tool == ViewportTool::Coincident);
    tangentToolAction_->setChecked(tool == ViewportTool::Tangent);
    curvatureToolAction_->setChecked(tool == ViewportTool::Curvature);
    measureToolAction_->setChecked(tool == ViewportTool::Measure);
    gridOriginToolAction_->setChecked(tool == ViewportTool::MoveGridOrigin);
    switch (tool) {
    case ViewportTool::Select:
        statusBar()->showMessage(QStringLiteral("選択モード"), 2500);
        break;
    case ViewportTool::MoveGridOrigin:
        statusBar()->showMessage(
            QStringLiteral("点グリッド: 動かす点を掴み、作図上の合わせたい点までドラッグ"), 5000);
        break;
    case ViewportTool::DrawPoint:
        statusBar()->showMessage(
            QStringLiteral("作図点: 位置をクリック。Ctrlを押している間は吸着しません"), 4500);
        break;
    case ViewportTool::DrawLine:
        statusBar()->showMessage(
            QStringLiteral("直線: 左クリックで始点。近くの点からは右クリック。Ctrl中は吸着なし"),
            5000);
        break;
    case ViewportTool::DrawPolyline:
        statusBar()->showMessage(QStringLiteral("ポリライン作図モード"), 2500);
        break;
    case ViewportTool::DrawRectangle:
        statusBar()->showMessage(QStringLiteral("矩形作図モード"), 2500);
        break;
    case ViewportTool::DrawCircle:
        statusBar()->showMessage(QStringLiteral("円作図モード"), 2500);
        break;
    case ViewportTool::DrawArc:
        statusBar()->showMessage(
            viewport_->CurrentArcDrawingMode() == ArcDrawingMode::ThreePoints
                ? QStringLiteral("3点円弧作図モード")
                : viewport_->CurrentArcDrawingMode() == ArcDrawingMode::EndpointsRadius
                ? QStringLiteral("始点と終点を指定し、半径で円弧を作るモード")
                : QStringLiteral("始点から接線・法線角を指定して円弧を伸ばすモード"),
            3500);
        break;
    case ViewportTool::DrawBezier:
        statusBar()->showMessage(QStringLiteral("ベジェ曲線作図モード"), 2500);
        break;
    case ViewportTool::DrawSpline:
        statusBar()->showMessage(QStringLiteral("通過点スプライン: 4点以上を指定し、Enterまたは右クリックで完了"), 4500);
        break;
    case ViewportTool::MoveSelection:
        statusBar()->showMessage(QStringLiteral("移動: 基準点と移動先を3D画面で指定"), 3500);
        break;
    case ViewportTool::CopySelection:
        statusBar()->showMessage(QStringLiteral("コピー: 基準点と移動先を3D画面で指定"), 3500);
        break;
    case ViewportTool::MirrorSelection:
        statusBar()->showMessage(QStringLiteral("ミラー複製: 軸の1点目と2点目を3D画面で指定"), 3500);
        break;
    case ViewportTool::RotateSelection:
        statusBar()->showMessage(QStringLiteral("回転: 中心、角度の基準点、回転先の順に3D画面で指定"), 4500);
        break;
    case ViewportTool::SplitWire:
        statusBar()->showMessage(QStringLiteral("分割: 選択したワイヤー上の分けたい位置をクリック"), 4000);
        break;
    case ViewportTool::TrimWire:
        statusBar()->showMessage(QStringLiteral("トリム: 赤く表示される線・曲線部分をクリックして削除。右クリックまたはEscで終了"), 6000);
        break;
    case ViewportTool::ExtendWire:
        statusBar()->showMessage(QStringLiteral("延長: 伸ばしたい端側をクリック。緑の最初の境界まで延長。右クリックまたはEscで終了"), 6000);
        break;
    case ViewportTool::Coincident:
        statusBar()->showMessage(QStringLiteral("端点一致: 動かさない固定側、追従させる側の順に端点をクリック"), 5000);
        break;
    case ViewportTool::Tangent:
        statusBar()->showMessage(QStringLiteral("接線接続: 固定側の端点、追従するベジェ・B-spline・円弧端点の順にクリック"), 5000);
        break;
    case ViewportTool::Curvature:
        statusBar()->showMessage(QStringLiteral("曲率接続: 固定側の端点、曲率まで追従するベジェ端点の順にクリック"), 5000);
        break;
    case ViewportTool::Measure:
        statusBar()->showMessage(
            measurementMode_ != nullptr && measurementMode_->currentIndex() == 2
                ? QStringLiteral("要素測定: 線・曲線・作業平面を1つまたは2つ指定")
                : measurementMode_ != nullptr && measurementMode_->currentIndex() == 1
                ? QStringLiteral("3点角度: 頂点、1方向目、2方向目の順に3D点を指定")
                : QStringLiteral("2点測定: 既存点・線上点を3D画面で指定"),
            4500);
        break;
    }
}

void MainWindow::UpdateDrawingPanel(ViewportTool tool, std::size_t pointCount)
{
    QString state;
    switch (tool) {
    case ViewportTool::Select:
        state = QStringLiteral("選択");
        break;
    case ViewportTool::MoveGridOrigin:
        state = QStringLiteral("点グリッド基準 · 格子点をドラッグ");
        break;
    case ViewportTool::DrawPoint:
        state = QStringLiteral("作図点 · 位置をクリック");
        break;
    case ViewportTool::DrawLine:
        state = QStringLiteral("直線 · %1  %2 / 2点")
            .arg(pointCount == 0 ? QStringLiteral("始点") : QStringLiteral("終点"))
            .arg(pointCount);
        break;
    case ViewportTool::DrawPolyline:
        state = QStringLiteral("ポリライン  %1点").arg(pointCount);
        break;
    case ViewportTool::DrawRectangle:
        state = QStringLiteral("矩形 · %1  %2 / 2点")
            .arg(pointCount == 0 ? QStringLiteral("1点目") : QStringLiteral("対角点"))
            .arg(pointCount);
        break;
    case ViewportTool::DrawCircle:
        state = QStringLiteral("円 · %1  %2 / 2点")
            .arg(pointCount == 0 ? QStringLiteral("中心") : QStringLiteral("円周点"))
            .arg(pointCount);
        break;
    case ViewportTool::DrawArc:
        if (viewport_->CurrentArcDrawingMode() == ArcDrawingMode::ThreePoints) {
            state = QStringLiteral("3点円弧 · %1  %2 / 3点")
                .arg(pointCount == 0 ? QStringLiteral("始点") : pointCount == 1 ? QStringLiteral("通過点") : QStringLiteral("終点"))
                .arg(pointCount);
        } else if (viewport_->CurrentArcDrawingMode() == ArcDrawingMode::EndpointsRadius) {
            state = pointCount < 2
                ? QStringLiteral("両端＋半径 · %1  %2 / 2点")
                    .arg(pointCount == 0 ? QStringLiteral("始点") : QStringLiteral("終点"))
                    .arg(pointCount)
                : QStringLiteral("両端＋半径 · 半径と膨らむ側を確認");
        } else {
            state = pointCount == 0
                ? QStringLiteral("始点＋方向 · 始点を指定")
                : QStringLiteral("始点＋方向 · 方向・半径・円弧量を確認");
        }
        break;
    case ViewportTool::DrawBezier:
        state = QStringLiteral("ベジェ曲線 · %1  %2 / 4点")
            .arg(pointCount == 0 ? QStringLiteral("始点")
                : pointCount == 1 ? QStringLiteral("制御点1")
                : pointCount == 2 ? QStringLiteral("制御点2")
                                  : QStringLiteral("終点"))
            .arg(pointCount);
        break;
    case ViewportTool::DrawSpline:
        state = pointCount < 4
            ? QStringLiteral("通過点スプライン  %1 / 4点以上").arg(pointCount)
            : QStringLiteral("通過点スプライン  %1点 · 完了できます").arg(pointCount);
        break;
    case ViewportTool::MoveSelection:
        state = QStringLiteral("移動 · %1  %2 / 2点")
            .arg(pointCount == 0 ? QStringLiteral("基準点") : QStringLiteral("移動先"))
            .arg(pointCount);
        break;
    case ViewportTool::CopySelection:
        state = QStringLiteral("コピー · %1  %2 / 2点")
            .arg(pointCount == 0 ? QStringLiteral("基準点") : QStringLiteral("移動先"))
            .arg(pointCount);
        break;
    case ViewportTool::MirrorSelection:
        state = QStringLiteral("ミラー複製 · %1  %2 / 2点")
            .arg(pointCount == 0 ? QStringLiteral("軸の1点目") : QStringLiteral("軸の2点目"))
            .arg(pointCount);
        break;
    case ViewportTool::RotateSelection:
        state = QStringLiteral("回転 · %1  %2 / 3点")
            .arg(pointCount == 0 ? QStringLiteral("中心")
                : pointCount == 1 ? QStringLiteral("角度の基準")
                                  : QStringLiteral("回転先"))
            .arg(pointCount);
        break;
    case ViewportTool::SplitWire:
        state = QStringLiteral("分割 · ワイヤー上をクリック");
        break;
    case ViewportTool::TrimWire:
        state = QStringLiteral("トリム · 赤い部分をクリック");
        break;
    case ViewportTool::ExtendWire:
        state = QStringLiteral("延長 · 伸ばす端側をクリック");
        break;
    case ViewportTool::Coincident:
        state = viewport_ != nullptr && viewport_->CoincidencePicks().empty()
            ? QStringLiteral("端点一致 · 固定側をクリック")
            : QStringLiteral("端点一致 · 追従側をクリック");
        break;
    case ViewportTool::Tangent:
        state = viewport_ != nullptr && viewport_->CoincidencePicks().empty()
            ? QStringLiteral("接線接続 · 固定側をクリック")
            : QStringLiteral("接線接続 · 追従曲線をクリック");
        break;
    case ViewportTool::Curvature:
        state = viewport_ != nullptr && viewport_->CoincidencePicks().empty()
            ? QStringLiteral("曲率接続 · 固定側をクリック")
            : QStringLiteral("曲率接続 · 追従ベジェをクリック");
        break;
    case ViewportTool::Measure:
        state = QStringLiteral("測定");
        break;
    }
    if (drawingStateLabel_ != nullptr) {
        drawingStateLabel_->setText(state);
    }
    if (finishDrawingAction_ != nullptr) {
        finishDrawingAction_->setEnabled(
            (tool == ViewportTool::DrawPolyline && pointCount >= 2)
            || (tool == ViewportTool::DrawSpline && pointCount >= 4));
    }
    if (cancelDrawingAction_ != nullptr) {
        cancelDrawingAction_->setEnabled(pointCount > 0);
    }
    if (drawingConstruction_ != nullptr) {
        drawingConstruction_->setVisible(tool != ViewportTool::DrawPoint);
    }
    RefreshBeginnerGuide();

    if (drawingDimensionSection_ == nullptr || drawingDimensionStack_ == nullptr
        || drawingDimensionCommitButton_ == nullptr) {
        return;
    }

    int dimensionPage = -1;
    if (tool == ViewportTool::DrawLine || tool == ViewportTool::DrawPolyline
        || tool == ViewportTool::DrawSpline
        || tool == ViewportTool::DrawBezier) {
        dimensionPage = 0;
    } else if (tool == ViewportTool::DrawRectangle) {
        dimensionPage = 1;
    } else if (tool == ViewportTool::DrawCircle) {
        dimensionPage = 2;
    } else if (tool == ViewportTool::DrawArc) {
        dimensionPage = 3;
    }
    drawingDimensionSection_->setVisible(dimensionPage >= 0);
    if (dimensionPage < 0) {
        return;
    }
    drawingDimensionStack_->setCurrentIndex(dimensionPage);
    bool canCommit = pointCount > 0;
    QString commitText = canCommit ? QStringLiteral("寸法で確定") : QStringLiteral("始点を指定");
    if (tool == ViewportTool::DrawArc) {
        const ArcDrawingMode mode = viewport_->CurrentArcDrawingMode();
        canCommit = mode == ArcDrawingMode::ThreePoints ? false
            : mode == ArcDrawingMode::EndpointsRadius ? pointCount == 2
            : pointCount == 1;
        commitText = mode == ArcDrawingMode::ThreePoints
            ? QStringLiteral("3点を画面で指定")
            : canCommit ? QStringLiteral("この円弧を作成")
                        : mode == ArcDrawingMode::EndpointsRadius
                        ? QStringLiteral("始点と終点を指定")
                        : QStringLiteral("始点を指定");
    }
    drawingDimensionCommitButton_->setEnabled(canCommit);
    drawingDimensionCommitButton_->setText(commitText);

    if (tool == ViewportTool::DrawArc
        && viewport_->CurrentArcDrawingMode() != ArcDrawingMode::ThreePoints
        && canCommit && QApplication::focusWidget() == viewport_) {
        QDoubleSpinBox* firstField = viewport_->CurrentArcDrawingMode()
                == ArcDrawingMode::EndpointsRadius
            ? arcRadiusField_
            : arcDirectionAngle_;
        firstField->setFocus(Qt::OtherFocusReason);
        firstField->selectAll();
    }

    const DrawingMeasurements measurements = viewport_->CurrentDrawingMeasurements();
    if (!measurements.available) {
        return;
    }
    const auto updateField = [](QDoubleSpinBox* field, double value) {
        if (field == nullptr || field->hasFocus()) {
            return;
        }
        const QSignalBlocker blocker(field);
        field->setValue(value);
    };
    if (dimensionPage == 0) {
        updateField(drawingLengthField_, measurements.lengthMillimeters);
        updateField(drawingAngleField_, measurements.angleDegrees);
    } else if (dimensionPage == 1) {
        updateField(drawingWidthField_, measurements.widthMillimeters);
        updateField(drawingHeightField_, measurements.heightMillimeters);
    } else if (dimensionPage == 2) {
        updateField(drawingRadiusField_, measurements.radiusMillimeters);
    }
}

void MainWindow::RefreshBeginnerGuide()
{
    if (viewport_ == nullptr || toolsTabs_ == nullptr || beginnerGuideTitle_ == nullptr
        || beginnerGuideNext_ == nullptr || beginnerGuideSteps_ == nullptr
        || beginnerGuideContext_ == nullptr || beginnerGuideNextButton_ == nullptr) {
        return;
    }

    const int tab = toolsTabs_->currentIndex();
    const int activeStage = tab == 1 ? 0
        : (tab == 0 || tab == 2 || tab == 3 || tab == 4) ? 1
        : tab == 5 ? 2
        : tab == 6 ? 3 : -1;
    for (std::size_t index = 0; index < workflowButtons_.size(); ++index) {
        if (workflowButtons_[index] != nullptr) {
            const QSignalBlocker blocker(workflowButtons_[index]);
            workflowButtons_[index]->setChecked(static_cast<int>(index) == activeStage);
        }
    }

    const auto& selections = viewport_->Selections();
    std::size_t pointSelectionCount = 0;
    std::size_t wireCount = 0;
    std::size_t surfaceCount = 0;
    std::size_t plateCount = 0;
    std::size_t bodyCount = 0;
    for (const CadSelection& selection : selections) {
        pointSelectionCount += selection.kind == CadSelectionKind::Point ? 1 : 0;
        wireCount += selection.kind == CadSelectionKind::Wire ? 1 : 0;
        surfaceCount += selection.kind == CadSelectionKind::Surface ? 1 : 0;
        plateCount += selection.kind == CadSelectionKind::Plate ? 1 : 0;
        bodyCount += selection.kind == CadSelectionKind::Body ? 1 : 0;
    }
    QString selectionText = QStringLiteral("なし");
    if (!selections.empty()) {
        QStringList parts;
        if (pointSelectionCount > 0) {
            parts << QStringLiteral("作図点%1個").arg(pointSelectionCount);
        }
        if (wireCount > 0) {
            parts << QStringLiteral("ワイヤー%1本").arg(wireCount);
        }
        if (surfaceCount > 0) {
            parts << QStringLiteral("面%1枚").arg(surfaceCount);
        }
        if (plateCount > 0) {
            parts << QStringLiteral("板材%1枚").arg(plateCount);
        }
        if (bodyCount > 0) {
            parts << QStringLiteral("立体%1個").arg(bodyCount);
        }
        const std::size_t described = pointSelectionCount + wireCount + surfaceCount + plateCount + bodyCount;
        if (selections.size() > described) {
            parts << QStringLiteral("作業平面など%1個").arg(selections.size() - described);
        }
        selectionText = parts.join(QStringLiteral(" / "));
    }
    const QString activePlane = activePlaneCombo_ != nullptr && !activePlaneCombo_->currentText().isEmpty()
        ? activePlaneCombo_->currentText() : QStringLiteral("なし");
    beginnerGuideContext_->setText(
        QStringLiteral("作図面: %1\n選択: %2").arg(activePlane, selectionText));

    const auto setGuide = [this](const QString& title, const QString& next,
                              const QString& steps, const QString& anchor,
                              int nextTab = -1, const QString& nextTabText = {}) {
        beginnerGuideTitle_->setText(title);
        beginnerGuideNext_->setText(next);
        beginnerGuideSteps_->setText(steps);
        beginnerGuideManualAnchor_ = anchor;
        beginnerGuideNextTab_ = nextTab;
        beginnerGuideNextButton_->setVisible(nextTab >= 0);
        beginnerGuideNextButton_->setText(nextTabText);
    };

    const ViewportTool tool = viewport_->Tool();
    const std::size_t pointCount = viewport_->DrawingPointCount();
    if (tool != ViewportTool::Select) {
        const QString startSnapHint = QStringLiteral(
            "\n入力前の右クリック: 近くの交点・端点・作図点を始点"
            "\nCtrl中: 完全に吸着しない");
        switch (tool) {
        case ViewportTool::MoveGridOrigin:
            setGuide(QStringLiteral("点グリッドの基準を合わせる"),
                QStringLiteral("次: 動かす格子点から合わせたい点までドラッグ"),
                QStringLiteral("1  大きい主点または小さい副点を掴む\n2  線の端点・制御点までドラッグ\n3  離すと基準X/Yへ反映。Escで取消"),
                QStringLiteral("grid"));
            return;
        case ViewportTool::DrawPoint:
            setGuide(QStringLiteral("作図点を置く"),
                QStringLiteral("次: 点を置く位置を中央画面でクリック"),
                QStringLiteral("交点・端点・格子点は薄い記号が出た時だけ吸着\n記号がなければ任意位置。Ctrl中は完全に吸着しません"),
                QStringLiteral("drawing"));
            return;
        case ViewportTool::DrawLine:
            setGuide(QStringLiteral("直線を描く"),
                pointCount == 0 ? QStringLiteral("次: 始点を中央画面でクリック")
                                : QStringLiteral("次: 終点をクリック、または数字を入力"),
                QStringLiteral("1  始点\n2  数字を打つと長さ欄へ入力\n3  Tabで角度、Enterで確定") + startSnapHint,
                QStringLiteral("drawing"));
            return;
        case ViewportTool::DrawPolyline:
            setGuide(QStringLiteral("ポリラインを描く"),
                pointCount == 0 ? QStringLiteral("次: 始点をクリック")
                                : QStringLiteral("次: 折れ点をクリック、または数字を入力"),
                QStringLiteral("1  始点\n2  各区間は長さ→Tab→角度→Enter\n3  入力後の右クリック・完了で全体を確定") + startSnapHint,
                QStringLiteral("drawing"));
            return;
        case ViewportTool::DrawRectangle:
            setGuide(QStringLiteral("矩形を描く"),
                pointCount == 0 ? QStringLiteral("次: 1つ目の角をクリック")
                                : QStringLiteral("次: 対角をクリック、または数字を入力"),
                QStringLiteral("1  角を1点\n2  数字を打つと幅欄へ入力\n3  Tabで高さ、Enterで確定") + startSnapHint,
                QStringLiteral("drawing"));
            return;
        case ViewportTool::DrawCircle:
            setGuide(QStringLiteral("円を描く"),
                pointCount == 0 ? QStringLiteral("次: 中心をクリック")
                                : QStringLiteral("次: 円周をクリック、または半径を入力"),
                QStringLiteral("1  中心\n2  数字を打つと半径欄へ入力\n3  Enterで確定") + startSnapHint,
                QStringLiteral("drawing"));
            return;
        case ViewportTool::DrawArc:
            if (viewport_->CurrentArcDrawingMode() == ArcDrawingMode::ThreePoints) {
                setGuide(QStringLiteral("3点円弧を描く"),
                    pointCount == 0 ? QStringLiteral("次: 始点をクリック")
                        : pointCount == 1 ? QStringLiteral("次: 円弧が通る点をクリック")
                                          : QStringLiteral("次: 終点をクリック"),
                    QStringLiteral("1  始点\n2  通過点\n3  終点\n各点は距離→Tab→角度→Enterでも指定") + startSnapHint,
                    QStringLiteral("drawing"));
            } else if (viewport_->CurrentArcDrawingMode() == ArcDrawingMode::EndpointsRadius) {
                setGuide(QStringLiteral("両端と半径で円弧を描く"),
                    pointCount == 0 ? QStringLiteral("次: 始点をクリック")
                        : pointCount == 1 ? QStringLiteral("次: 終点をクリック")
                                          : QStringLiteral("次: 半径と膨らむ側を確認して作成"),
                    QStringLiteral("1  始点\n2  終点\n3  半径（両端間隔の1/2以上）\n4  左右を確認して作成") + startSnapHint,
                    QStringLiteral("drawing"));
            } else {
                setGuide(QStringLiteral("始点から指定方向へ円弧を伸ばす"),
                    pointCount == 0 ? QStringLiteral("次: 始点をクリック")
                                    : QStringLiteral("次: 接線/法線角、半径、円弧量を確認して作成"),
                    QStringLiteral("1  始点\n2  接線角または法線角\n3  半径\n4  中心角または円弧長\n5  左右を確認して作成") + startSnapHint,
                    QStringLiteral("drawing"));
            }
            return;
        case ViewportTool::DrawBezier:
            setGuide(QStringLiteral("ベジェ曲線を描く"),
                pointCount == 0 ? QStringLiteral("次: 始点をクリック")
                    : pointCount == 1 ? QStringLiteral("次: 始点側の制御点")
                    : pointCount == 2 ? QStringLiteral("次: 終点側の制御点")
                                      : QStringLiteral("次: 終点をクリック"),
                QStringLiteral("1  始点\n2  制御点1\n3  制御点2\n4  終点\n各点は距離・角度でも指定") + startSnapHint,
                QStringLiteral("drawing"));
            return;
        case ViewportTool::DrawSpline:
            setGuide(QStringLiteral("通過点スプラインを描く"),
                pointCount < 4 ? QStringLiteral("次: 通過点を%1点以上まで追加").arg(4 - pointCount)
                               : QStringLiteral("次: Enterで確定、または通過点を追加"),
                QStringLiteral("1  通過点をクリックまたは距離・角度で追加\n2  入力後の右クリック・完了で確定") + startSnapHint,
                QStringLiteral("drawing"));
            return;
        case ViewportTool::MoveSelection:
        case ViewportTool::CopySelection:
            setGuide(tool == ViewportTool::MoveSelection ? QStringLiteral("選択線を移動") : QStringLiteral("選択線をコピー"),
                pointCount == 0 ? QStringLiteral("次: 移動元の基準点をクリック")
                                : QStringLiteral("次: 移動先をクリック"),
                QStringLiteral("1  基準点\n2  移動先\nEscで取消"), QStringLiteral("edit"));
            return;
        case ViewportTool::MirrorSelection:
            setGuide(QStringLiteral("選択線をミラー複製"),
                pointCount == 0 ? QStringLiteral("次: ミラー軸の1点目") : QStringLiteral("次: ミラー軸の2点目"),
                QStringLiteral("1  軸の1点目\n2  軸の2点目\n基準線を設定済みなら即時実行"),
                QStringLiteral("edit"));
            return;
        case ViewportTool::RotateSelection:
            setGuide(QStringLiteral("選択線を回転"),
                pointCount == 0 ? QStringLiteral("次: 回転中心")
                    : pointCount == 1 ? QStringLiteral("次: 角度0°の基準点")
                                      : QStringLiteral("次: 回転先"),
                QStringLiteral("1  回転中心\n2  角度の基準点\n3  回転先"), QStringLiteral("edit"));
            return;
        case ViewportTool::SplitWire:
            setGuide(QStringLiteral("ワイヤーを分割"), QStringLiteral("次: 選択線上の分けたい位置をクリック"),
                QStringLiteral("1  分割する線を1本選択\n2  分割道具\n3  線上をクリック"), QStringLiteral("edit"));
            return;
        case ViewportTool::TrimWire:
            setGuide(QStringLiteral("不要な線・曲線部分をトリム"),
                QStringLiteral("次: 赤く表示された部分をクリック"),
                QStringLiteral("1  トリム道具を選択\n2  消したい線・曲線部分へマウスを置く\n3  赤い事前表示を確認してクリック\n続けて処理可能。右クリックまたはEscで終了"),
                QStringLiteral("edit"));
            return;
        case ViewportTool::ExtendWire:
            setGuide(QStringLiteral("線・曲線を境界まで延長"),
                QStringLiteral("次: 伸ばしたい端側へマウスを置いてクリック"),
                QStringLiteral("1  延長道具を選択\n2  線・曲線の伸ばしたい端側へマウスを置く\n3  緑の到達先を確認してクリック\n最初に交わる表示中の線・曲線まで延長"),
                QStringLiteral("edit"));
            return;
        case ViewportTool::Coincident:
        case ViewportTool::Tangent:
        case ViewportTool::Curvature: {
            const bool first = viewport_->CoincidencePicks().empty();
            const QString title = tool == ViewportTool::Coincident ? QStringLiteral("端点を一致")
                : tool == ViewportTool::Tangent ? QStringLiteral("端点を接線接続")
                                                : QStringLiteral("端点を曲率接続");
            setGuide(title,
                first ? QStringLiteral("次: 動かさない固定側の端点")
                      : QStringLiteral("次: 形を追従させる側の端点"),
                QStringLiteral("1  固定側の端点\n2  追従側の端点\nEscで取消"), QStringLiteral("edit"));
            return;
        }
        case ViewportTool::Measure:
            setGuide(QStringLiteral("形状を測る"),
                QStringLiteral("次: 中央画面で測る点・線・平面を指定"),
                QStringLiteral("1  情報タブで2点間/要素を選択\n2  中央画面で対象を指定\n3  必要なら参照寸法として残す"),
                QStringLiteral("measure"));
            return;
        case ViewportTool::Select:
            break;
        }
    }

    switch (tab) {
    case 0:
        setGuide(QStringLiteral("作業平面へ作図"), QStringLiteral("次: 直線・矩形・円などの道具を選ぶ"),
            QStringLiteral("1  作図面と向きを確認\n2  道具を選択\n3  中央画面へ直接作図"),
            QStringLiteral("drawing"), 1, QStringLiteral("作業平面を作る"));
        break;
    case 1:
        setGuide(QStringLiteral("作図する紙を置く"), QStringLiteral("次: 作り方を選び、位置と向きを入力"),
            QStringLiteral("1  作り方と値を指定\n2  この向きで見る\n3  必要なら平面を作成"),
            QStringLiteral("planes"), 0, QStringLiteral("作図へ進む"));
        break;
    case 2:
        setGuide(QStringLiteral("座標で正確にワイヤーを作る"), QStringLiteral("次: 種類を選び、座標を入力"),
            QStringLiteral("1  ワイヤー種類\n2  XYZまたは平面内XY\n3  ワイヤーを追加"),
            QStringLiteral("numeric"));
        break;
    case 3:
        setGuide(QStringLiteral("ワイヤー・平面を編集"),
            selections.empty() ? QStringLiteral("次: 中央画面で編集対象を選ぶ")
                               : QStringLiteral("次: 点をドラッグ、または右欄で数値変更"),
            QStringLiteral("1  対象を直接選択\n2  点移動・変形・拘束を選択\n3  数値変更は「変更を適用」"),
            QStringLiteral("edit"), wireCount >= 2 ? 5 : -1,
            wireCount >= 2 ? QStringLiteral("面・板へ進む") : QString());
        break;
    case 4:
        setGuide(QStringLiteral("交差する角を加工"), QStringLiteral("次: 直線2本をCtrl+クリックで選ぶ"),
            QStringLiteral("1  直線A/Bを3Dで選択\n2  C面取り/R丸めと寸法\n3  作成"),
            QStringLiteral("machining"));
        break;
    case 5:
        if (plateCount > 0) {
            setGuide(QStringLiteral("板材を仕上げる"), QStringLiteral("次: 開口、途中切れ目、展開分割線を指定"),
                QStringLiteral("1  板材と投影ワイヤーを選択\n2  開口・切れ目・分割線\n3  出力タブで紙片数を確認"),
                QStringLiteral("projection"), 6, QStringLiteral("出力へ進む"));
        } else if (surfaceCount > 0) {
            setGuide(QStringLiteral("面を板材・治具にする"), QStringLiteral("次: 板厚方向または治具の側を決める"),
                QStringLiteral("1  面を選択\n2  板厚・方向または治具設定\n3  作成"),
                QStringLiteral("plate"));
        } else {
            setGuide(QStringLiteral("ワイヤーから面を作る"),
                wireCount > 0 ? QStringLiteral("次: 選択順と本数を確認して面を作成")
                              : QStringLiteral("次: 中央画面で輪郭・断面を順に選ぶ"),
                QStringLiteral("1  断面を順にCtrl+クリック\n2  面の作り方を選択\n3  面を作成"),
                QStringLiteral("surface"));
        }
        break;
    case 6:
        setGuide(QStringLiteral("製作データを出力"),
            plateCount + bodyCount > 0 ? QStringLiteral("次: 出力範囲と形式を確認して保存")
                                       : QStringLiteral("次: 中央画面で板材・治具を選ぶ"),
            QStringLiteral("1  出力対象を直接選択\n2  二方向曲面は完全分割/V字を選択\n3  組立3Dで線を確認\n4  1:1図面または3Dを保存"),
            QStringLiteral("output"));
        break;
    case 7:
        setGuide(QStringLiteral("見た目を調整"), QStringLiteral("次: 色・太さ・線種を選ぶ"),
            QStringLiteral("1  対象の表示グループを開く\n2  色・太さ・線種・透明度を変更\n3  設定は次回起動にも残る"),
            QStringLiteral("display"));
        break;
    case 8:
        setGuide(QStringLiteral("寸法と選択情報を確認"), QStringLiteral("次: 対象を選ぶか、測定を開始"),
            QStringLiteral("1  2点距離・3点角度・要素から方式を選択\n2  中央画面でガイド順に指定\n3  距離・接線・法線角を確認"),
            QStringLiteral("measure"));
        break;
    default:
        setGuide(QStringLiteral("作業を選んでください"), QStringLiteral("次: 上の製作工程を選ぶ"),
            QStringLiteral("1  平面\n2  作図\n3  面・板\n4  出力"), QStringLiteral("start"));
        break;
    }
}

void MainWindow::CommitDrawingDimensions()
{
    if (viewport_ == nullptr || drawingDimensionCommitButton_ == nullptr
        || !drawingDimensionCommitButton_->isEnabled()) {
        return;
    }

    bool committed = false;
    try {
        if (viewport_->Tool() == ViewportTool::DrawArc
            && viewport_->CurrentArcDrawingMode() != ArcDrawingMode::ThreePoints) {
            UpdateArcConfiguration();
            committed = viewport_->CommitConfiguredArc();
        } else if (viewport_->Tool() == ViewportTool::DrawLine || viewport_->Tool() == ViewportTool::DrawPolyline
        || viewport_->Tool() == ViewportTool::DrawSpline || viewport_->Tool() == ViewportTool::DrawArc
        || viewport_->Tool() == ViewportTool::DrawBezier) {
            committed = viewport_->CommitDrawingDimensions(
                drawingLengthField_->value(), drawingAngleField_->value());
        } else if (viewport_->Tool() == ViewportTool::DrawRectangle) {
            committed = viewport_->CommitDrawingDimensions(
                drawingWidthField_->value(), drawingHeightField_->value());
        } else if (viewport_->Tool() == ViewportTool::DrawCircle) {
            committed = viewport_->CommitDrawingDimensions(drawingRadiusField_->value());
        }
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4500);
    }

    if (!committed) {
        statusBar()->showMessage(QStringLiteral("始点を3D画面で指定してください"), 3000);
    }
    viewport_->setFocus();
}

void MainWindow::UpdateArcConfiguration()
{
    if (viewport_ == nullptr || arcDrawingMode_ == nullptr || arcParameterStack_ == nullptr
        || arcRadiusField_ == nullptr || arcDirectionBasis_ == nullptr
        || arcDirectionAngle_ == nullptr || arcExtentMode_ == nullptr
        || arcExtentValue_ == nullptr || arcTurnSide_ == nullptr || arcBulgeSide_ == nullptr) {
        return;
    }
    const ArcDrawingMode mode = static_cast<ArcDrawingMode>(
        arcDrawingMode_->currentData().toInt());
    arcParameterStack_->setCurrentIndex(arcDrawingMode_->currentIndex());
    const bool configured = mode != ArcDrawingMode::ThreePoints;
    arcRadiusLabel_->setVisible(configured);
    arcRadiusField_->setVisible(configured);

    const bool lengthMode = arcExtentMode_->currentData().toInt() == 1;
    const bool wasLength = arcExtentValue_->suffix().contains(QStringLiteral("mm"));
    if (lengthMode != wasLength) {
        const QSignalBlocker blocker(arcExtentValue_);
        const double radius = std::max(arcRadiusField_->value(), 1.0e-9);
        const double converted = lengthMode
            ? radius * arcExtentValue_->value() * kPi / 180.0
            : arcExtentValue_->value() / radius * 180.0 / kPi;
        arcExtentValue_->setRange(
            0.001,
            lengthMode ? radius * 2.0 * kPi - 1.0e-6 : 359.999);
        arcExtentValue_->setSuffix(lengthMode ? QStringLiteral(" mm") : QStringLiteral(" °"));
        arcExtentValue_->setValue(converted);
    } else {
        const QSignalBlocker blocker(arcExtentValue_);
        arcExtentValue_->setRange(
            0.001,
            lengthMode ? arcRadiusField_->value() * 2.0 * kPi - 1.0e-6 : 359.999);
    }
    arcExtentLabel_->setText(lengthMode ? QStringLiteral("円弧長") : QStringLiteral("中心角"));

    const double turn = arcTurnSide_->currentData().toDouble();
    double tangentAngle = arcDirectionAngle_->value();
    if (arcDirectionBasis_->currentData().toInt() == 1) {
        tangentAngle -= turn * 90.0;
    }
    const double sweepDegrees = (lengthMode
        ? arcExtentValue_->value() / arcRadiusField_->value() * 180.0 / kPi
        : arcExtentValue_->value()) * turn;
    viewport_->SetConfiguredArc(
        arcRadiusField_->value(), tangentAngle, sweepDegrees,
        arcBulgeSide_->currentData().toBool());
}

void MainWindow::RefreshActiveWorkPlane()
{
    if (activePlaneCombo_ == nullptr) {
        return;
    }
    std::optional<WorkPlane> plane;
    const std::string selectedName = ToName(activePlaneCombo_->currentText());
    for (const auto& namedPlane : project_.WorkPlanes()) {
        if (namedPlane.name == selectedName && namedPlane.visible) {
            plane = namedPlane.plane;
            break;
        }
    }
    viewport_->SetActiveWorkPlane(plane);
    const bool canDraw = plane.has_value();
    pointToolAction_->setEnabled(canDraw);
    lineToolAction_->setEnabled(canDraw);
    polylineToolAction_->setEnabled(canDraw);
    rectangleToolAction_->setEnabled(canDraw);
    circleToolAction_->setEnabled(canDraw);
    arcToolAction_->setEnabled(canDraw);
    bezierToolAction_->setEnabled(canDraw);
    splineToolAction_->setEnabled(canDraw);
    gridOriginToolAction_->setEnabled(canDraw);
    moveToolAction_->setEnabled(canDraw);
    copyToolAction_->setEnabled(canDraw);
    mirrorToolAction_->setEnabled(canDraw);
    rotateToolAction_->setEnabled(canDraw);
    splitToolAction_->setEnabled(!project_.Wires().empty());
    coincidentToolAction_->setEnabled(project_.Wires().size() >= 2);
    tangentToolAction_->setEnabled(project_.Wires().size() >= 2);
    curvatureToolAction_->setEnabled(project_.Wires().size() >= 2);
    removeCoincidentAction_->setEnabled(!project_.CoincidentConstraints().empty());
    removeTangentAction_->setEnabled(!project_.TangentConstraints().empty());
    joinWiresAction_->setEnabled(project_.Wires().size() >= 2);
    RefreshReference();
    if (!canDraw && viewport_->Tool() != ViewportTool::Select
        && viewport_->Tool() != ViewportTool::SplitWire
        && viewport_->Tool() != ViewportTool::Coincident
        && viewport_->Tool() != ViewportTool::Tangent
        && viewport_->Tool() != ViewportTool::Curvature
        && viewport_->Tool() != ViewportTool::Measure) {
        SetViewportTool(ViewportTool::Select);
    }
    UpdateWireOffsetPreview();
    RefreshBeginnerGuide();
}

void MainWindow::AddViewportPoint(Vector3 point)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        RecordUndo();
        project_.AddPoint(
            ToName(SuggestedDirectGroupName(QStringLiteral("point"))), point, planeName);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("作図点を作成しました"), 1800);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 3500);
    }
}

void MainWindow::AddViewportLine(Vector3 start, Vector3 end)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        const Wire line = Wire::Line(start, end);
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("line"))), line, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("直線を作成しました"), 1800);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 3500);
    }
}

void MainWindow::AddViewportPolyline(const std::vector<Vector3>& points)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        const Wire polyline = Wire::Polyline(points);
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("polyline"))), polyline, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("ポリラインを作成しました"), 1800);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 3500);
    }
}

void MainWindow::AddViewportRectangle(const std::array<Vector3, 4>& corners)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        const Wire rectangle = Wire::Polyline({corners[0], corners[1], corners[2], corners[3], corners[0]});
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("rectangle"))), rectangle, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("矩形を作成しました"), 1800);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 3500);
    }
}

void MainWindow::AddViewportCircle(Vector3 center, double radius)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        const std::optional<WorkPlane> plane = project_.FindWorkPlane(planeName);
        if (!plane.has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        const Wire circle = Wire::Circle(center, plane->UAxis(), plane->VAxis(), radius);
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("circle"))), circle, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("円を作成しました"), 1800);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 3500);
    }
}

void MainWindow::AddViewportArc(Vector3 start, Vector3 through, Vector3 end)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        const Wire arc = Wire::CircularArcThroughThreePoints(start, through, end);
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("arc"))), arc, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("3点円弧を作成しました"), 1800);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 3500);
    }
}

void MainWindow::AddViewportArcWire(const Wire& arc)
{
    try {
        if (arc.Kind() != WireKind::CircularArc) {
            throw std::invalid_argument("作成結果が円弧ではありません。");
        }
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("arc"))), arc, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("指定した半径・方向で円弧を作成しました"), 2200);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4500);
    }
}

void MainWindow::AddViewportBezier(const std::array<Vector3, 4>& points)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        const Wire bezier = Wire::CubicBezier(points[0], points[1], points[2], points[3]);
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("bezier"))), bezier, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("ベジェ曲線を作成しました"), 1800);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 3500);
    }
}

void MainWindow::AddViewportSpline(const std::vector<Vector3>& throughPoints)
{
    try {
        const std::string planeName = ToName(activePlaneCombo_->currentText());
        if (!project_.FindWorkPlane(planeName).has_value()) {
            throw std::invalid_argument("作図面が見つかりません。");
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = planeName;
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        metadata.construction = drawingConstruction_ != nullptr && drawingConstruction_->isChecked();
        const Wire spline = Wire::InterpolatingCubicBSpline(throughPoints);
        RecordUndo();
        project_.AddWire(ToName(SuggestedDirectGroupName(QStringLiteral("spline"))), spline, metadata);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("%1点を通るスプラインを作成しました").arg(throughPoints.size()), 2200);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4000);
    }
}

void MainWindow::ApplyViewportWireEdit(int wireIndex, const Wire& replacement)
{
    try {
        if (wireIndex < 0 || wireIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("編集する曲線が見つかりません。");
        }
        const auto selections = viewport_->Selections();
        const std::string wireName = project_.Wires()[wireIndex].name;
        Project candidate = project_;
        candidate.UpdateWire(wireName, replacement);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(QStringLiteral("点またはハンドルを移動しました（元に戻す: Ctrl+Z）"), 2500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4000);
    }
}

void MainWindow::ApplyViewportTranslation(Vector3 delta, bool copy)
{
    try {
        if (!delta.IsFinite() || delta.LengthSquared() <= 1.0e-18) {
            throw std::invalid_argument("移動量を指定してください。");
        }

        std::vector<int> indices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                indices.push_back(selection.index);
            }
        }
        if (indices.empty()) {
            throw std::invalid_argument("移動またはコピーするワイヤーを選択してください。");
        }

        std::vector<kachakacha::model::NamedWire> sources;
        std::vector<Wire> transformed;
        sources.reserve(indices.size());
        transformed.reserve(indices.size());
        for (int index : indices) {
            const auto source = project_.Wires()[index];
            const Wire result = source.wire.Translated(delta);
            if (source.metadata.planePolicy == WirePlanePolicy::LockedToPlane
                || source.metadata.lineConstraints.angleDegrees.has_value()) {
                if (!source.metadata.sourcePlaneName.has_value()) {
                    throw std::invalid_argument("作業平面に固定されたワイヤーの基準平面がありません。");
                }
                const auto sourcePlane = project_.FindWorkPlane(*source.metadata.sourcePlaneName);
                if (!sourcePlane.has_value() || !WireLiesOnPlane(result, *sourcePlane)) {
                    throw std::invalid_argument("作業平面に固定されたワイヤーは、その平面外へ移動できません。");
                }
            }
            sources.push_back(source);
            transformed.push_back(result);
        }

        RecordUndo();
        std::vector<CadSelection> resultingSelections;
        if (copy) {
            for (std::size_t index = 0; index < sources.size(); ++index) {
                const QString name = SuggestedDirectGroupName(ToQString(sources[index].name) + QStringLiteral("_copy"));
                project_.AddWire(ToName(name), transformed[index], sources[index].metadata);
                resultingSelections.push_back({CadSelectionKind::Wire, static_cast<int>(project_.Wires().size() - 1)});
            }
        } else {
            for (std::size_t index = 0; index < sources.size(); ++index) {
                project_.UpdateWire(sources[index].name, transformed[index]);
                resultingSelections.push_back({CadSelectionKind::Wire, indices[index]});
            }
        }
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(std::move(resultingSelections), true);
        statusBar()->showMessage(
            copy ? QStringLiteral("%1本のワイヤーをコピーしました").arg(sources.size())
                 : QStringLiteral("%1本のワイヤーを移動しました").arg(sources.size()),
            2500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4000);
    }
}

void MainWindow::ApplyViewportMirror(Vector3 linePoint, Vector3 lineDirection, Vector3 planeNormal)
{
    try {
        std::vector<int> indices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                indices.push_back(selection.index);
            }
        }
        if (indices.empty()) {
            throw std::invalid_argument("ミラー複製するワイヤーを選択してください。");
        }

        std::vector<kachakacha::model::NamedWire> sources;
        std::vector<Wire> mirrored;
        sources.reserve(indices.size());
        mirrored.reserve(indices.size());
        for (int index : indices) {
            const auto source = project_.Wires()[index];
            const Wire result = source.wire.Mirrored(linePoint, lineDirection, planeNormal);
            if (source.metadata.planePolicy == WirePlanePolicy::LockedToPlane
                || source.metadata.lineConstraints.angleDegrees.has_value()) {
                if (!source.metadata.sourcePlaneName.has_value()) {
                    throw std::invalid_argument("作業平面に固定されたワイヤーの基準平面がありません。");
                }
                const auto sourcePlane = project_.FindWorkPlane(*source.metadata.sourcePlaneName);
                if (!sourcePlane.has_value() || !WireLiesOnPlane(result, *sourcePlane)) {
                    throw std::invalid_argument("この軸では、固定されたワイヤーが作業平面外へ出ます。");
                }
            }
            sources.push_back(source);
            mirrored.push_back(result);
        }

        RecordUndo();
        std::vector<CadSelection> resultingSelections;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            const QString name = SuggestedDirectGroupName(ToQString(sources[index].name) + QStringLiteral("_mirror"));
            project_.AddWire(
                ToName(name),
                mirrored[index],
                RetargetLineConstraints(project_, sources[index].metadata, mirrored[index], false));
            resultingSelections.push_back({CadSelectionKind::Wire, static_cast<int>(project_.Wires().size() - 1)});
        }
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(std::move(resultingSelections), true);
        statusBar()->showMessage(QStringLiteral("%1本のワイヤーをミラー複製しました").arg(sources.size()), 2500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4000);
    }
}

void MainWindow::ApplyViewportRotation(Vector3 axisPoint, Vector3 axisDirection, double angleRadians)
{
    try {
        if (!axisPoint.IsFinite() || !axisDirection.IsFinite() || axisDirection.LengthSquared() <= 1.0e-18
            || !std::isfinite(angleRadians) || std::abs(angleRadians) <= 1.0e-12) {
            throw std::invalid_argument("回転の中心と角度を指定してください。");
        }

        std::vector<int> indices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                indices.push_back(selection.index);
            }
        }
        if (indices.empty()) {
            throw std::invalid_argument("回転するワイヤーを選択してください。");
        }

        std::vector<kachakacha::model::NamedWire> sources;
        std::vector<Wire> rotated;
        sources.reserve(indices.size());
        rotated.reserve(indices.size());
        for (int index : indices) {
            const auto source = project_.Wires()[index];
            const Wire result = source.wire.RotatedAroundAxis(axisPoint, axisDirection, angleRadians);
            if (source.metadata.planePolicy == WirePlanePolicy::LockedToPlane
                || source.metadata.lineConstraints.angleDegrees.has_value()) {
                if (!source.metadata.sourcePlaneName.has_value()) {
                    throw std::invalid_argument("作業平面に固定されたワイヤーの基準平面がありません。");
                }
                const auto sourcePlane = project_.FindWorkPlane(*source.metadata.sourcePlaneName);
                if (!sourcePlane.has_value() || !WireLiesOnPlane(result, *sourcePlane)) {
                    throw std::invalid_argument("この回転では、固定されたワイヤーが作業平面外へ出ます。");
                }
            }
            sources.push_back(source);
            rotated.push_back(result);
        }

        RecordUndo();
        std::vector<CadSelection> resultingSelections;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            project_.UpdateWireAndMetadata(
                sources[index].name,
                rotated[index],
                RetargetLineConstraints(project_, sources[index].metadata, rotated[index], false));
            resultingSelections.push_back({CadSelectionKind::Wire, indices[index]});
        }
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(std::move(resultingSelections), true);
        statusBar()->showMessage(
            QStringLiteral("%1本を %2 度回転しました")
                .arg(sources.size())
                .arg(angleRadians * 180.0 / kPi, 0, 'f', 2),
            3000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4000);
    }
}

void MainWindow::ApplySplitWire(int wireIndex, double parameter)
{
    try {
        if (wireIndex < 0 || wireIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("分割するワイヤーが見つかりません。");
        }
        if (!std::isfinite(parameter) || parameter <= 1.0e-8 || parameter >= 1.0 - 1.0e-8) {
            throw std::invalid_argument("端点から離れた分割位置を指定してください。");
        }

        const auto source = project_.Wires()[wireIndex];
        const auto parts = source.wire.SplitAt(parameter);
        const QString groupName = SuggestedDirectGroupName(ToQString(source.name) + QStringLiteral("_part"));
        const std::string firstName = ToName(groupName + QStringLiteral("_1"));
        const std::string secondName = ToName(groupName + QStringLiteral("_2"));

        RecordUndo();
        if (referenceWireName_.has_value() && *referenceWireName_ == source.name) {
            referenceWireName_.reset();
        }
        project_.RemoveWire(source.name);
        project_.AddWire(
            firstName,
            parts.first,
            RetargetLineConstraints(project_, source.metadata, parts.first, true));
        const int firstIndex = static_cast<int>(project_.Wires().size() - 1);
        project_.AddWire(
            secondName,
            parts.second,
            RetargetLineConstraints(project_, source.metadata, parts.second, true));
        const int secondIndex = static_cast<int>(project_.Wires().size() - 1);

        MarkModified();
        RefreshModelViews(false);
        SetViewportTool(ViewportTool::Select);
        UpdateSelections({
            {CadSelectionKind::Wire, firstIndex},
            {CadSelectionKind::Wire, secondIndex},
        }, true);
        statusBar()->showMessage(QStringLiteral("ワイヤーを2本に分割しました"), 3000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4500);
    }
}

void MainWindow::ApplyDirectLineTrim(int wireIndex, double parameter)
{
    try {
        if (wireIndex < 0 || wireIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("トリムする線・曲線が見つかりません。");
        }
        const auto source = project_.Wires()[wireIndex];
        if (source.projection.has_value() || source.plateOffset.has_value()) {
            throw std::invalid_argument("投影線・板厚位置線は、元の作図線を編集してください。");
        }

        std::vector<Wire> boundaries;
        boundaries.reserve(project_.Wires().size());
        for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
            const auto& candidate = project_.Wires()[index];
            if (index == wireIndex
                || !viewport_->IsDisplayed(CadSelectionKind::Wire, index, candidate.visible)) {
                continue;
            }
            boundaries.push_back(candidate.wire);
        }
        const auto result = TrimWireAtBoundaries(source.wire, parameter, boundaries);

        Project candidate = project_;
        std::vector<CadSelection> resultingSelections;
        bool clearReference = false;
        if (result.retained.size() == 1) {
            candidate.UpdateWireAndMetadata(
                source.name,
                result.retained.front(),
                RetargetLineConstraints(project_, source.metadata, result.retained.front(), true));
            resultingSelections.push_back({CadSelectionKind::Wire, wireIndex});
        } else {
            const QString groupName = SuggestedDirectGroupName(
                ToQString(source.name) + QStringLiteral("_trim"));
            const std::string firstName = ToName(groupName + QStringLiteral("_1"));
            const std::string secondName = ToName(groupName + QStringLiteral("_2"));
            candidate.RemoveWire(source.name);
            candidate.AddWire(
                firstName,
                result.retained[0],
                RetargetLineConstraints(project_, source.metadata, result.retained[0], true));
            const int firstIndex = static_cast<int>(candidate.Wires().size() - 1);
            candidate.AddWire(
                secondName,
                result.retained[1],
                RetargetLineConstraints(project_, source.metadata, result.retained[1], true));
            const int secondIndex = static_cast<int>(candidate.Wires().size() - 1);
            resultingSelections = {
                {CadSelectionKind::Wire, firstIndex},
                {CadSelectionKind::Wire, secondIndex},
            };
            clearReference = referenceWireName_.has_value()
                && *referenceWireName_ == source.name;
        }

        RecordUndo();
        project_ = std::move(candidate);
        if (clearReference) {
            referenceWireName_.reset();
        }
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(std::move(resultingSelections), true);
        statusBar()->showMessage(
            result.retained.size() == 1
                ? QStringLiteral("指定した部分を境界までトリムしました")
                : QStringLiteral("指定した中間部分を削除し、曲線を2本に分けました"),
            3500);
    } catch (const std::exception& error) {
        QString message = QString::fromUtf8(error.what());
        if (message.contains(QStringLiteral("No visible wire boundary"))) {
            message = QStringLiteral("この部分を区切る表示中の線・曲線がありません。");
        } else if (message.contains(QStringLiteral("complete target wire"))) {
            message = QStringLiteral("ワイヤー全体が消えるためトリムできません。");
        } else if (message.contains(QStringLiteral("too short"))) {
            message = QStringLiteral("交点から少し離れた、消したい線・曲線部分を指してください。");
        } else if (message.startsWith(QStringLiteral("Wire is used"))) {
            message = QStringLiteral("このワイヤーは面・投影・開口・拘束で使用中のため中央を2本に分けられません。派生形状を外すか、元図の段階でトリムしてください。");
        }
        statusBar()->showMessage(message, 5000);
    }
}

void MainWindow::ApplyDirectLineExtend(int wireIndex, double parameter)
{
    try {
        if (wireIndex < 0 || wireIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("延長する線・曲線が見つかりません。");
        }
        const auto source = project_.Wires()[wireIndex];
        if (source.projection.has_value() || source.plateOffset.has_value()) {
            throw std::invalid_argument("投影線・板厚位置線は、元の作図線を編集してください。");
        }

        std::vector<Wire> boundaries;
        boundaries.reserve(project_.Wires().size());
        for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
            const auto& candidate = project_.Wires()[index];
            if (index == wireIndex
                || !viewport_->IsDisplayed(CadSelectionKind::Wire, index, candidate.visible)) {
                continue;
            }
            boundaries.push_back(candidate.wire);
        }
        const auto result = ExtendWireToBoundary(source.wire, parameter, boundaries);
        Project candidate = project_;
        candidate.UpdateWireAndMetadata(
            source.name,
            result.extended,
            RetargetLineConstraints(project_, source.metadata, result.extended, true));

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection({CadSelectionKind::Wire, wireIndex}, true);
        statusBar()->showMessage(QStringLiteral("ワイヤーを最初の境界まで延長しました"), 3500);
    } catch (const std::exception& error) {
        QString message = QString::fromUtf8(error.what());
        if (message.contains(QStringLiteral("No visible wire boundary"))) {
            message = QStringLiteral("選んだ端の先に交わる表示中の線・曲線がありません。");
        }
        statusBar()->showMessage(message, 5000);
    }
}

void MainWindow::ApplyEndpointCoincidence(WireEndpointPick anchor, WireEndpointPick follower)
{
    try {
        if (anchor.wireIndex < 0 || follower.wireIndex < 0
            || anchor.wireIndex >= static_cast<int>(project_.Wires().size())
            || follower.wireIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("一致させる端点が見つかりません。");
        }

        const auto& anchorWire = project_.Wires()[anchor.wireIndex];
        const auto& followerWire = project_.Wires()[follower.wireIndex];
        const std::string anchorName = anchorWire.name;
        const std::string followerName = followerWire.name;
        Project candidate = project_;
        candidate.AddWireCoincidentConstraint(
            {anchorName, anchor.endpoint},
            {followerName, follower.endpoint});

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections({
            {CadSelectionKind::Wire, anchor.wireIndex},
            {CadSelectionKind::Wire, follower.wireIndex},
        }, true);
        const auto endpointText = [](kachakacha::model::WireEndpoint endpoint) {
            return endpoint == kachakacha::model::WireEndpoint::Start
                ? QStringLiteral("始点")
                : QStringLiteral("終点");
        };
        statusBar()->showMessage(
            QStringLiteral("%1の%2へ、%3の%4を追従させました")
                .arg(ToQString(anchorName), endpointText(anchor.endpoint),
                    ToQString(followerName), endpointText(follower.endpoint)),
            4500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(
            QStringLiteral("端点一致を適用できません: %1").arg(QString::fromUtf8(error.what())),
            6000);
    }
}

void MainWindow::ApplyEndpointTangency(WireEndpointPick anchor, WireEndpointPick follower)
{
    ApplyEndpointContinuity(anchor, follower, WireContinuity::G1Tangent);
}

void MainWindow::ApplyEndpointCurvature(WireEndpointPick anchor, WireEndpointPick follower)
{
    ApplyEndpointContinuity(anchor, follower, WireContinuity::G2Curvature);
}

void MainWindow::ApplyEndpointContinuity(
    WireEndpointPick anchor,
    WireEndpointPick follower,
    WireContinuity continuity)
{
    try {
        if (anchor.wireIndex < 0 || follower.wireIndex < 0
            || anchor.wireIndex >= static_cast<int>(project_.Wires().size())
            || follower.wireIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("接線接続する端点が見つかりません。");
        }
        const auto& anchorWire = project_.Wires()[anchor.wireIndex];
        const auto& followerWire = project_.Wires()[follower.wireIndex];
        if (continuity == WireContinuity::G2Curvature
            && followerWire.wire.Kind() != WireKind::CubicBezier) {
            throw std::invalid_argument("G2の追従側にはベジェ曲線の端点を指定してください。");
        }
        if (continuity == WireContinuity::G1Tangent
            && followerWire.wire.Kind() != WireKind::CubicBezier
            && followerWire.wire.Kind() != WireKind::CubicBSpline
            && followerWire.wire.Kind() != WireKind::CircularArc) {
            throw std::invalid_argument("追従側にはベジェ、B-spline、または円弧の端点を指定してください。");
        }
        const std::string anchorName = anchorWire.name;
        const std::string followerName = followerWire.name;
        const auto sameEndpoint = [](const kachakacha::model::WireEndpointReference& reference,
                                      const std::string& name,
                                      kachakacha::model::WireEndpoint endpoint) {
            return reference.wireName == name && reference.endpoint == endpoint;
        };

        Project candidate = project_;
        const bool alreadyCoincident = std::any_of(
            candidate.CoincidentConstraints().begin(), candidate.CoincidentConstraints().end(),
            [&](const auto& constraint) {
                return sameEndpoint(constraint.anchor, anchorName, anchor.endpoint)
                    && sameEndpoint(constraint.follower, followerName, follower.endpoint);
            });
        if (!alreadyCoincident) {
            candidate.AddWireCoincidentConstraint(
                {anchorName, anchor.endpoint},
                {followerName, follower.endpoint});
        }
        candidate.AddWireTangentConstraint(
            {anchorName, anchor.endpoint},
            {followerName, follower.endpoint},
            continuity);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections({
            {CadSelectionKind::Wire, anchor.wireIndex},
            {CadSelectionKind::Wire, follower.wireIndex},
        }, true);
        statusBar()->showMessage(
            QStringLiteral("%1から%2へ%3で滑らかに接続しました")
                .arg(ToQString(anchorName), ToQString(followerName),
                    continuity == WireContinuity::G2Curvature
                        ? QStringLiteral("G2（曲率）")
                        : QStringLiteral("G1（接線）")),
            4500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(
            QStringLiteral("滑らか接続を適用できません: %1").arg(QString::fromUtf8(error.what())),
            6000);
    }
}

void MainWindow::RemoveSelectedCoincidences()
{
    std::vector<std::string> wireNames;
    std::vector<CadSelection> selections;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind != CadSelectionKind::Wire || selection.index < 0
            || selection.index >= static_cast<int>(project_.Wires().size())) {
            continue;
        }
        const std::string& name = project_.Wires()[selection.index].name;
        if (std::find(wireNames.begin(), wireNames.end(), name) == wireNames.end()) {
            wireNames.push_back(name);
            selections.push_back(selection);
        }
    }
    if (wireNames.empty()) {
        statusBar()->showMessage(QStringLiteral("一致を解除するワイヤーを3D画面で選択してください"), 4000);
        return;
    }

    Project candidate = project_;
    std::size_t removed = 0;
    for (const std::string& name : wireNames) {
        removed += candidate.RemoveWireCoincidentConstraints(name);
    }
    if (removed == 0) {
        statusBar()->showMessage(QStringLiteral("選択したワイヤーには端点一致がありません"), 3000);
        return;
    }

    RecordUndo();
    project_ = std::move(candidate);
    MarkModified();
    RefreshModelViews(false);
    UpdateSelections(std::move(selections), true);
    statusBar()->showMessage(QStringLiteral("端点一致を%1件解除しました").arg(removed), 3500);
}

void MainWindow::RemoveSelectedTangencies()
{
    std::vector<std::string> wireNames;
    std::vector<CadSelection> selections;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind != CadSelectionKind::Wire || selection.index < 0
            || selection.index >= static_cast<int>(project_.Wires().size())) {
            continue;
        }
        const std::string& name = project_.Wires()[selection.index].name;
        if (std::find(wireNames.begin(), wireNames.end(), name) == wireNames.end()) {
            wireNames.push_back(name);
            selections.push_back(selection);
        }
    }
    if (wireNames.empty()) {
        statusBar()->showMessage(QStringLiteral("G1/G2を解除するワイヤーを3D画面で選択してください"), 4000);
        return;
    }

    Project candidate = project_;
    std::size_t removed = 0;
    for (const std::string& name : wireNames) {
        removed += candidate.RemoveWireTangentConstraints(name);
    }
    if (removed == 0) {
        statusBar()->showMessage(QStringLiteral("選択したワイヤーにはG1/G2接続がありません"), 3000);
        return;
    }

    RecordUndo();
    project_ = std::move(candidate);
    MarkModified();
    RefreshModelViews(false);
    UpdateSelections(std::move(selections), true);
    statusBar()->showMessage(
        QStringLiteral("G1/G2接続を%1件解除しました。端点一致は残っています").arg(removed),
        4000);
}

void MainWindow::JoinSelectedWires()
{
    try {
        std::vector<int> indices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                indices.push_back(selection.index);
            }
        }
        if (indices.size() < 2) {
            throw std::invalid_argument("端点がつながる直線またはポリラインを2本以上選択してください。");
        }

        std::vector<kachakacha::model::NamedWire> sources;
        std::vector<Wire> wires;
        sources.reserve(indices.size());
        wires.reserve(indices.size());
        for (int index : indices) {
            const auto source = project_.Wires()[index];
            if (!sources.empty()
                && (source.metadata.planePolicy != sources.front().metadata.planePolicy
                    || source.metadata.sourcePlaneName != sources.front().metadata.sourcePlaneName)) {
                throw std::invalid_argument("平面との関係が同じワイヤー同士を選択してください。");
            }
            sources.push_back(source);
            wires.push_back(source.wire);
        }

        const Wire joined = JoinLineChain(wires);
        const QString name = SuggestedDirectGroupName(ToQString(sources.front().name) + QStringLiteral("_joined"));
        const bool removesReference = referenceWireName_.has_value()
            && std::any_of(sources.begin(), sources.end(), [this](const auto& source) {
                   return source.name == *referenceWireName_;
               });

        RecordUndo();
        if (removesReference) {
            referenceWireName_.reset();
        }
        for (const auto& source : sources) {
            project_.RemoveWire(source.name);
        }
        WireMetadata joinedMetadata = sources.front().metadata;
        joinedMetadata.lineConstraints = {};
        project_.AddWire(ToName(name), joined, std::move(joinedMetadata));
        const int joinedIndex = static_cast<int>(project_.Wires().size() - 1);

        MarkModified();
        RefreshModelViews(false);
        SetViewportTool(ViewportTool::Select);
        UpdateSelection({CadSelectionKind::Wire, joinedIndex}, true);
        statusBar()->showMessage(QStringLiteral("%1本を1本のポリラインに結合しました").arg(sources.size()), 3000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4500);
    }
}

void MainWindow::UpdateWireOffsetPreview()
{
    if (wireOffsetSelectionLabel_ == nullptr || wireOffsetDistance_ == nullptr
        || wireOffsetSide_ == nullptr || wireOffsetApplyButton_ == nullptr || viewport_ == nullptr) {
        return;
    }

    std::vector<Wire> previews;
    wireOffsetApplyButton_->setEnabled(false);
    const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(activePlaneCombo_->currentText()));
    if (!plane.has_value()) {
        wireOffsetSelectionLabel_->setText(QStringLiteral("作図面を選択してください"));
        viewport_->SetWireOffsetPreview({});
        return;
    }

    const auto& selections = viewport_->Selections();
    if (selections.empty()) {
        wireOffsetSelectionLabel_->setText(QStringLiteral("3D画面でワイヤーを選択"));
        viewport_->SetWireOffsetPreview({});
        return;
    }

    const double distance = wireOffsetDistance_->value()
        * (wireOffsetSide_->currentIndex() == 0 ? 1.0 : -1.0);
    for (const CadSelection& selection : selections) {
        if (selection.kind != CadSelectionKind::Wire || selection.index < 0
            || selection.index >= static_cast<int>(project_.Wires().size())) {
            wireOffsetSelectionLabel_->setText(QStringLiteral("ワイヤーだけを選択してください"));
            viewport_->SetWireOffsetPreview({});
            return;
        }
        const auto& source = project_.Wires()[selection.index];
        if (source.projection.has_value()) {
            wireOffsetSelectionLabel_->setText(QStringLiteral("投影結果ではなく元の平面図を選択してください"));
            viewport_->SetWireOffsetPreview({});
            return;
        }
        if (source.wire.Kind() == WireKind::CubicBezier
            || source.wire.Kind() == WireKind::CubicBSpline) {
            wireOffsetSelectionLabel_->setText(QStringLiteral("曲線の厳密オフセットは未対応です"));
            viewport_->SetWireOffsetPreview({});
            return;
        }
        try {
            previews.push_back(OffsetPlanarWire(source.wire, *plane, distance));
        } catch (const std::exception&) {
            wireOffsetSelectionLabel_->setText(QStringLiteral("選択線・作図面・距離を確認してください"));
            viewport_->SetWireOffsetPreview({});
            return;
        }
    }

    wireOffsetSelectionLabel_->setText(QStringLiteral("%1本を紫線の位置へ複製").arg(previews.size()));
    wireOffsetApplyButton_->setEnabled(!previews.empty());
    viewport_->SetWireOffsetPreview(std::move(previews));
}

void MainWindow::ApplyWireOffset()
{
    try {
        UpdateWireOffsetPreview();
        if (!wireOffsetApplyButton_->isEnabled()) {
            throw std::invalid_argument("オフセットできるワイヤーを選択してください。");
        }
        const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(activePlaneCombo_->currentText()));
        if (!plane.has_value()) {
            throw std::invalid_argument("作図面を選択してください。");
        }

        const std::string planeName = ToName(activePlaneCombo_->currentText());
        const double distance = wireOffsetDistance_->value()
            * (wireOffsetSide_->currentIndex() == 0 ? 1.0 : -1.0);
        std::vector<std::pair<kachakacha::model::NamedWire, Wire>> results;
        for (const CadSelection& selection : viewport_->Selections()) {
            const auto source = project_.Wires()[selection.index];
            results.emplace_back(source, OffsetPlanarWire(source.wire, *plane, distance));
        }

        RecordUndo();
        std::vector<CadSelection> createdSelections;
        for (auto& [source, offset] : results) {
            WireMetadata metadata = source.metadata;
            metadata.sourcePlaneName = planeName;
            if (metadata.planePolicy == WirePlanePolicy::Free3D) {
                metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
            }
            metadata = RetargetLineConstraints(project_, std::move(metadata), offset, false);
            if (metadata.curveConstraints.radiusMillimeters.has_value()
                && (offset.Kind() == WireKind::Circle || offset.Kind() == WireKind::CircularArc)) {
                metadata.curveConstraints.radiusMillimeters = offset.ArcData().radius;
            }
            const QString name = SuggestedDirectGroupName(
                ToQString(source.name) + QStringLiteral("_offset"));
            project_.AddWire(ToName(name), std::move(offset), std::move(metadata));
            createdSelections.push_back({
                CadSelectionKind::Wire,
                static_cast<int>(project_.Wires().size() - 1),
            });
        }

        MarkModified();
        RefreshModelViews(false);
        SetViewportTool(ViewportTool::Select);
        UpdateSelections(createdSelections, true);
        statusBar()->showMessage(
            QStringLiteral("%1本を%2 mmオフセット複製しました")
                .arg(results.size())
                .arg(std::abs(distance), 0, 'f', 3),
            3000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4500);
    }
}

void MainWindow::CreateSurfaceFromSelection()
{
    try {
        ValidateObjectName(surfaceName_->text());
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }

        const int surfaceMode = surfaceType_->currentIndex();
        if ((surfaceMode == 0 && wireIndices.empty())
            || (surfaceMode == 1 && wireIndices.size() != 2)
            || (surfaceMode == 2 && wireIndices.size() < 3)) {
            if (surfaceMode == 0) {
                throw std::invalid_argument("閉じた輪郭を作る直線・曲線を1本以上選択してください。");
            }
            if (surfaceMode == 1) {
                throw std::invalid_argument("断面ワイヤーを2本選択してください。");
            }
            throw std::invalid_argument("車体の前から後ろの順に、断面ワイヤーを3本以上選択してください。");
        }
        for (int index : wireIndices) {
            if (project_.Wires()[index].metadata.construction) {
                throw std::invalid_argument("補助線は面の境界や断面には使えません。通常線へ戻してから選択してください。");
            }
        }

        Project candidate = project_;
        const std::string name = ToName(surfaceName_->text());
        if (surfaceMode == 0) {
            std::vector<std::string> boundaryNames;
            boundaryNames.reserve(wireIndices.size());
            for (int index : wireIndices) {
                boundaryNames.push_back(candidate.Wires()[index].name);
            }
            candidate.AddPlanarSurface(name, std::move(boundaryNames));
        } else if (surfaceMode == 1) {
            candidate.AddRuledSurface(
                name,
                candidate.Wires()[wireIndices[0]].name,
                candidate.Wires()[wireIndices[1]].name);
        } else {
            std::vector<std::string> sectionNames;
            sectionNames.reserve(wireIndices.size());
            for (int index : wireIndices) {
                sectionNames.push_back(candidate.Wires()[index].name);
            }
            candidate.AddLoftSurface(name, std::move(sectionNames));
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const int surfaceIndex = static_cast<int>(project_.Surfaces().size() - 1);
        UpdateSelection({CadSelectionKind::Surface, surfaceIndex}, true);
        toolsTabs_->setCurrentIndex(5);
        surfaceName_->setText(SuggestedSurfaceName());
        const QString message = surfaceMode == 0
            ? wireIndices.size() == 1
                ? QStringLiteral("閉じたワイヤーから平面を作成しました")
                : QStringLiteral("%1本の直線・曲線をつないで平面を作成しました")
                      .arg(wireIndices.size())
            : surfaceMode == 1
            ? QStringLiteral("2断面から曲面を作成しました")
            : QStringLiteral("%1断面からロフト面を作成しました").arg(wireIndices.size());
        statusBar()->showMessage(message, 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ProjectSelectedWiresToSurface()
{
    try {
        const std::optional<WorkPlane> drawingPlane = project_.FindWorkPlane(ToName(projectionPlane_->currentText()));
        const std::optional<kachakacha::model::Surface> targetSurface = project_.FindSurface(ToName(projectionSurface_->currentText()));
        if (!drawingPlane.has_value()) {
            throw std::invalid_argument("平面図を描いた作業平面を選択してください。");
        }
        if (!targetSurface.has_value()) {
            throw std::invalid_argument("投影先の面を選択してください。");
        }

        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (wireIndices.empty()) {
            throw std::invalid_argument("投影する平面図ワイヤーを3D画面で選択してください。");
        }
        for (int index : wireIndices) {
            const auto& source = project_.Wires()[index];
            if (source.metadata.construction) {
                throw std::invalid_argument("補助線は面へ投影できません。通常線へ戻してから選択してください。");
            }
            if (source.projection.has_value()) {
                throw std::invalid_argument("投影後のワイヤーではなく、元の平面図を選択してください。");
            }
            if (!WireLiesOnPlane(source.wire, *drawingPlane)) {
                throw std::invalid_argument("選択ワイヤーが指定した平面図上にありません。");
            }
        }
        Project candidate = project_;
        std::vector<std::string> createdNames;
        for (int index : wireIndices) {
            const std::string sourceName = project_.Wires()[index].name;
            const QString name = SuggestedDirectGroupName(
                ToQString(sourceName) + QStringLiteral("_on_") + projectionSurface_->currentText());
            candidate.AddProjectedWire(
                ToName(name), sourceName, ToName(projectionSurface_->currentText()), drawingPlane->Normal());
            createdNames.push_back(ToName(name));
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        std::vector<CadSelection> resultingSelections;
        for (const std::string& name : createdNames) {
            const auto found = std::find_if(project_.Wires().begin(), project_.Wires().end(), [&](const auto& wire) {
                return wire.name == name;
            });
            resultingSelections.push_back({
                CadSelectionKind::Wire,
                static_cast<int>(std::distance(project_.Wires().begin(), found)),
            });
        }
        UpdateSelections(std::move(resultingSelections), true);
        toolsTabs_->setCurrentIndex(5);
        statusBar()->showMessage(QStringLiteral("%1本の平面図ワイヤーを面へ投影しました").arg(createdNames.size()), 4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::CreateProtrudingLightCase()
{
    try {
        ValidateObjectName(lightCaseRootName_->text());
        ValidateObjectName(lightCaseSurfaceName_->text());

        int frontWireIndex = -1;
        std::string targetSurfaceName;
        QString targetDisplayName;
        int targetCount = 0;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                if (frontWireIndex >= 0) {
                    throw std::invalid_argument("ライトケース最前面の閉じた輪郭は1本だけ選択してください。");
                }
                frontWireIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
                && selection.index < static_cast<int>(project_.Surfaces().size())) {
                ++targetCount;
                targetSurfaceName = project_.Surfaces()[selection.index].name;
                targetDisplayName = ToQString(targetSurfaceName);
            } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                ++targetCount;
                targetSurfaceName = project_.Plates()[selection.index].sourceSurfaceName;
                targetDisplayName = ToQString(project_.Plates()[selection.index].name);
            }
        }
        if (frontWireIndex < 0 || targetCount != 1) {
            throw std::invalid_argument(
                "最前面の閉じた輪郭1本と、根元を置く面または板1枚を3D画面で選択してください。");
        }

        const auto& front = project_.Wires()[frontWireIndex];
        if (front.metadata.construction) {
            throw std::invalid_argument("補助線はライトケース最前面に使えません。通常線へ戻してください。");
        }
        if (front.projection.has_value() || front.plateOffset.has_value()) {
            throw std::invalid_argument("投影後や板厚位置の輪郭ではなく、ライト最前面の元輪郭を選択してください。");
        }
        if (!front.wire.IsClosed()) {
            throw std::invalid_argument("ライトケース最前面には閉じた輪郭を選択してください。");
        }

        Vector3 direction;
        if (lightCaseDirectionMode_->currentIndex() == 0) {
            try {
                const auto frontSurface = kachakacha::model::Surface::Planar(front.wire);
                if (!frontSurface.PlanarWorkPlane().has_value()) {
                    throw std::invalid_argument("planar work plane is unavailable");
                }
                direction = frontSurface.PlanarWorkPlane()->Normal();
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "最前面輪郭は、1枚の作業平面上にある閉じた輪郭にしてください。");
            }
        } else if (lightCaseDirectionMode_->currentIndex() == 1) {
            if (!referenceWireName_.has_value()) {
                throw std::invalid_argument("伸ばす方向の基準にする直線を先に設定してください。");
            }
            const auto reference = std::find_if(
                project_.Wires().begin(), project_.Wires().end(), [this](const auto& wire) {
                    return wire.name == *referenceWireName_ && wire.wire.Kind() == WireKind::Line;
                });
            if (reference == project_.Wires().end()) {
                throw std::invalid_argument("設定した基準線が見つかりません。もう一度設定してください。");
            }
            direction = reference->wire.End() - reference->wire.Start();
        } else {
            direction = ReadVector3(lightCaseDirection_);
        }
        if (!direction.IsFinite() || direction.LengthSquared() <= 1.0e-18) {
            throw std::invalid_argument("伸ばす方向には0ではないXYZ方向を指定してください。");
        }
        direction = direction.Normalized();

        const std::string rootName = ToName(lightCaseRootName_->text());
        const std::string sideName = ToName(lightCaseSurfaceName_->text());
        Project candidate = project_;
        try {
            candidate.AddProjectedWire(rootName, front.name, targetSurfaceName, direction);
        } catch (const std::invalid_argument& error) {
            const std::string message = error.what();
            if (message.starts_with("Projection") || message.starts_with("Projected")) {
                throw std::invalid_argument(
                    "最前面輪郭から伸ばした線が接続先に届きません。方向と接続先の範囲を確認してください。");
            }
            throw;
        }
        candidate.AddRuledSurface(sideName, front.name, rootName);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const auto root = std::find_if(project_.Wires().begin(), project_.Wires().end(),
            [&](const auto& wire) { return wire.name == rootName; });
        const auto side = std::find_if(project_.Surfaces().begin(), project_.Surfaces().end(),
            [&](const auto& surface) { return surface.name == sideName; });
        UpdateSelections({
            {CadSelectionKind::Wire, static_cast<int>(std::distance(project_.Wires().begin(), root))},
            {CadSelectionKind::Surface, static_cast<int>(std::distance(project_.Surfaces().begin(), side))},
        }, true);

        lightCaseRootName_->setText(SuggestedDirectGroupName(QStringLiteral("light_root")));
        int nextCaseNumber = 1;
        while (project_.FindSurface(ToName(QStringLiteral("light_case_%1").arg(nextCaseNumber))).has_value()) {
            ++nextCaseNumber;
        }
        lightCaseSurfaceName_->setText(QStringLiteral("light_case_%1").arg(nextCaseNumber));
        statusBar()->showMessage(
            QStringLiteral("%1へ根元輪郭を投影し、斜めライトケース側面を作成しました")
                .arg(targetDisplayName),
            4500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 6500);
    }
}

void MainWindow::CreatePlateFromSurface()
{
    try {
        ValidateObjectName(plateName_->text());
        const std::string sourceSurfaceName = ToName(plateSurface_->currentText());
        if (!project_.FindSurface(sourceSurfaceName).has_value()) {
            throw std::invalid_argument("板材にする面を3D画面または一覧で選択してください。");
        }

        Project candidate = project_;
        const std::string name = ToName(plateName_->text());
        const auto direction = static_cast<PlateThicknessDirection>(plateDirection_->currentData().toInt());
        candidate.AddPlate(
            name,
            sourceSurfaceName,
            plateThickness_->value(),
            plateVariableThickness_->isChecked()
                ? plateEndThickness_->value() : plateThickness_->value(),
            direction,
            ToName(plateMaterial_->currentData().toString()));
        candidate.SetSurfaceVisible(sourceSurfaceName, false);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const int plateIndex = static_cast<int>(project_.Plates().size() - 1);
        UpdateSelection({CadSelectionKind::Plate, plateIndex}, true);
        toolsTabs_->setCurrentIndex(5);
        plateName_->setText(SuggestedPlateName());
        statusBar()->showMessage(QStringLiteral("板厚 %1 mm の板材を作成しました").arg(plateThickness_->value()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::CreatePlateFromSelectedWires()
{
    try {
        ValidateObjectName(plateName_->text());
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())
                && std::find(wireIndices.begin(), wireIndices.end(), selection.index) == wireIndices.end()) {
                wireIndices.push_back(selection.index);
            }
        }
        if (wireIndices.empty()) {
            throw std::invalid_argument("3D板の輪郭または断面ワイヤーを3D画面で選択してください。");
        }
        if (wireIndices.size() == 1 && !project_.Wires()[wireIndices.front()].wire.IsClosed()) {
            throw std::invalid_argument("1本から平板を作る場合は閉じた輪郭を選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = ToName(plateName_->text());
        std::string surfaceName = plateName + "_surface";
        int suffix = 2;
        while (candidate.FindSurface(surfaceName).has_value()) {
            surfaceName = plateName + "_surface" + std::to_string(suffix++);
        }
        std::vector<std::string> wireNames;
        wireNames.reserve(wireIndices.size());
        for (int index : wireIndices) {
            wireNames.push_back(candidate.Wires()[index].name);
        }
        bool formsPlanarClosedContour = wireNames.size() == 1;
        if (wireNames.size() > 1) {
            std::vector<Wire> boundaryWires;
            boundaryWires.reserve(wireIndices.size());
            for (int index : wireIndices) {
                boundaryWires.push_back(candidate.Wires()[index].wire);
            }
            try {
                const Wire joined = JoinWireChain(boundaryWires);
                (void)kachakacha::model::Surface::Planar(joined);
                formsPlanarClosedContour = true;
            } catch (const std::invalid_argument&) {
                formsPlanarClosedContour = false;
            }
        }
        if (formsPlanarClosedContour) {
            candidate.AddPlanarSurface(surfaceName, wireNames);
        } else if (wireNames.size() == 1) {
            candidate.AddPlanarSurface(surfaceName, wireNames.front());
        } else if (wireNames.size() == 2) {
            candidate.AddRuledSurface(surfaceName, wireNames[0], wireNames[1]);
        } else {
            candidate.AddLoftSurface(surfaceName, wireNames);
        }
        const auto direction = static_cast<PlateThicknessDirection>(plateDirection_->currentData().toInt());
        candidate.AddPlate(
            plateName,
            surfaceName,
            plateThickness_->value(),
            plateVariableThickness_->isChecked()
                ? plateEndThickness_->value() : plateThickness_->value(),
            direction,
            ToName(plateMaterial_->currentData().toString()));
        candidate.SetSurfaceVisible(surfaceName, false);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(
            {CadSelectionKind::Plate, static_cast<int>(project_.Plates().size() - 1)}, true);
        plateName_->setText(SuggestedPlateName());
        statusBar()->showMessage(
            plateVariableThickness_->isChecked()
                ? QStringLiteral("選択断面から可変板厚の3D板を作成しました")
                : QStringLiteral("選択ワイヤーから3D板を作成しました"),
            4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 6000);
    }
}

void MainWindow::UpdateSelectedPlate()
{
    try {
        const CadSelection selection = viewport_->Selection();
        if (selection.kind != CadSelectionKind::Plate || selection.index < 0
            || selection.index >= static_cast<int>(project_.Plates().size())) {
            throw std::invalid_argument("変更する板材を3D画面またはモデル一覧で選択してください。");
        }
        const std::string sourceSurfaceName = ToName(plateSurface_->currentText());
        const auto direction = static_cast<PlateThicknessDirection>(plateDirection_->currentData().toInt());

        Project candidate = project_;
        candidate.UpdatePlate(
            candidate.Plates()[selection.index].name,
            sourceSurfaceName,
            plateThickness_->value(),
            plateVariableThickness_->isChecked()
                ? plateEndThickness_->value() : plateThickness_->value(),
            direction,
            ToName(plateMaterial_->currentData().toString()));
        candidate.SetSurfaceVisible(sourceSurfaceName, false);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(selection, true);
        statusBar()->showMessage(QStringLiteral("板材の板厚・方向・材質を更新しました"), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::CreatePlateOffsetWires()
{
    try {
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("板厚位置の基準にする板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、その元の面へ投影したワイヤーを選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        const double throughThickness = plateOffsetLayer_->currentData().toDouble();
        const std::string layerSuffix = throughThickness > 0.75 ? "_plus"
            : throughThickness < 0.25 ? "_minus" : "_center";
        std::vector<std::string> createdNames;
        for (int wireIndex : wireIndices) {
            const std::string sourceName = candidate.Wires()[wireIndex].name;
            std::string name = sourceName + layerSuffix;
            int suffix = 2;
            const auto nameExists = [&](const std::string& candidateName) {
                return std::any_of(candidate.Wires().begin(), candidate.Wires().end(),
                    [&](const auto& wire) { return wire.name == candidateName; });
            };
            while (nameExists(name)) {
                name = sourceName + layerSuffix + std::to_string(suffix++);
            }
            candidate.AddPlateOffsetWire(name, sourceName, plateName, throughThickness);
            createdNames.push_back(std::move(name));
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        std::vector<CadSelection> createdSelections;
        for (const std::string& name : createdNames) {
            const auto position = std::find_if(project_.Wires().begin(), project_.Wires().end(),
                [&](const auto& wire) { return wire.name == name; });
            createdSelections.push_back({CadSelectionKind::Wire,
                static_cast<int>(std::distance(project_.Wires().begin(), position))});
        }
        UpdateSelections(std::move(createdSelections), true);
        statusBar()->showMessage(
            QStringLiteral("板厚位置へ%1本のワイヤーを作成しました").arg(createdNames.size()), 4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 6000);
    }
}

void MainWindow::CreateSurfaceJig()
{
    try {
        ValidateObjectName(jigName_->text());
        const std::string sourceSurfaceName = ToName(jigSurface_->currentText());
        if (!project_.FindSurface(sourceSurfaceName).has_value()) {
            throw std::invalid_argument("治具の元にする面を3D画面または一覧で選択してください。");
        }

        Project candidate = project_;
        const std::string name = ToName(jigName_->text());
        const auto side = static_cast<JigSide>(jigSide_->currentData().toInt());
        candidate.AddSurfaceJig(
            name,
            sourceSurfaceName,
            {},
            side,
            jigClearance_->value(),
            jigThickness_->value());
        candidate.SetSurfaceVisible(sourceSurfaceName, false);

        const auto analysis = candidate.Bodies().back().body.AnalyzePrintability(jigMinimumWall_->value());
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const int bodyIndex = static_cast<int>(project_.Bodies().size() - 1);
        UpdateSelection({CadSelectionKind::Body, bodyIndex}, true);
        toolsTabs_->setCurrentIndex(5);
        jigName_->setText(SuggestedBodyName());
        jigAnalysisLabel_->setStyleSheet(
            analysis.meetsMinimumWall ? "color: #35664a;" : "color: #a32734;");
        jigAnalysisLabel_->setText(analysis.meetsMinimumWall
            ? QStringLiteral("造形確認: 最小肉厚 %1 mm を満たします").arg(analysis.minimumWallMillimeters, 0, 'f', 2)
            : QStringLiteral("造形警告: 厚み %1 mm は必要最小肉厚に不足します").arg(analysis.minimumWallMillimeters, 0, 'f', 2));
        statusBar()->showMessage(QStringLiteral("曲面から成形治具を作成しました"), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::UpdateSelectedBody()
{
    try {
        const CadSelection selection = viewport_->Selection();
        if (selection.kind != CadSelectionKind::Body || selection.index < 0
            || selection.index >= static_cast<int>(project_.Bodies().size())) {
            throw std::invalid_argument("変更する治具を3D画面またはモデル一覧で選択してください。");
        }
        const std::string sourceSurfaceName = ToName(jigSurface_->currentText());
        const auto side = static_cast<JigSide>(jigSide_->currentData().toInt());

        Project candidate = project_;
        const auto& current = candidate.Bodies()[selection.index];
        candidate.UpdateSurfaceJig(
            current.name,
            sourceSurfaceName,
            current.body.Range(),
            side,
            jigClearance_->value(),
            jigThickness_->value());
        candidate.SetSurfaceVisible(sourceSurfaceName, false);
        const auto analysis = candidate.Bodies()[selection.index].body.AnalyzePrintability(jigMinimumWall_->value());

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(selection, true);
        jigAnalysisLabel_->setStyleSheet(
            analysis.meetsMinimumWall ? "color: #35664a;" : "color: #a32734;");
        jigAnalysisLabel_->setText(analysis.meetsMinimumWall
            ? QStringLiteral("造形確認: 最小肉厚 %1 mm を満たします").arg(analysis.minimumWallMillimeters, 0, 'f', 2)
            : QStringLiteral("造形警告: 厚み %1 mm は必要最小肉厚に不足します").arg(analysis.minimumWallMillimeters, 0, 'f', 2));
        statusBar()->showMessage(QStringLiteral("治具の側・隙間・厚みを更新しました"), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::AddSelectedPlateOpenings()
{
    try {
        const std::vector<CadSelection> selections = viewport_->Selections();
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : selections) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("開口を作る板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、その面へ投影した閉じた輪郭を選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        for (int wireIndex : wireIndices) {
            candidate.AddPlateOpening(plateName, candidate.Wires()[wireIndex].name);
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(QStringLiteral("板材へ%1個の開口を追加しました").arg(wireIndices.size()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::RemoveSelectedPlateOpenings()
{
    try {
        const std::vector<CadSelection> selections = viewport_->Selections();
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : selections) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("開口を外す板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、開口から外す輪郭を選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        for (int wireIndex : wireIndices) {
            candidate.RemovePlateOpening(plateName, candidate.Wires()[wireIndex].name);
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(QStringLiteral("板材から%1個の開口を外しました").arg(wireIndices.size()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::AddSelectedPlateReliefCuts()
{
    try {
        const std::vector<CadSelection> selections = viewport_->Selections();
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : selections) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("切れ目を設定する板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、その面へ投影した切れ目ワイヤーを選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        for (int wireIndex : wireIndices) {
            candidate.AddPlateReliefCut(plateName, candidate.Wires()[wireIndex].name);
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(
            QStringLiteral("展開用の手動切れ目を%1本追加しました").arg(wireIndices.size()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::RemoveSelectedPlateReliefCuts()
{
    try {
        const std::vector<CadSelection> selections = viewport_->Selections();
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : selections) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("切れ目を外す板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、切れ目から外すワイヤーを選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        for (int wireIndex : wireIndices) {
            candidate.RemovePlateReliefCut(plateName, candidate.Wires()[wireIndex].name);
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(
            QStringLiteral("展開用の切れ目から%1本外しました").arg(wireIndices.size()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::AddSelectedPlateSplitLines()
{
    try {
        const std::vector<CadSelection> selections = viewport_->Selections();
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : selections) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("分割線を設定する板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、その面へ投影した分割ワイヤーを選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        for (int wireIndex : wireIndices) {
            candidate.AddPlateSplitLine(plateName, candidate.Wires()[wireIndex].name);
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(
            QStringLiteral("展開片を分ける分割線を%1本追加しました").arg(wireIndices.size()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::RemoveSelectedPlateSplitLines()
{
    try {
        const std::vector<CadSelection> selections = viewport_->Selections();
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : selections) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("分割線を解除する板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、解除する分割ワイヤーを選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        for (int wireIndex : wireIndices) {
            candidate.RemovePlateSplitLine(plateName, candidate.Wires()[wireIndex].name);
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(
            QStringLiteral("展開片の分割線を%1本解除しました").arg(wireIndices.size()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::SplitSelectedPlate()
{
    try {
        std::vector<int> selectedPlateIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                selectedPlateIndices.push_back(selection.index);
            }
        }
        std::sort(selectedPlateIndices.begin(), selectedPlateIndices.end());
        selectedPlateIndices.erase(
            std::unique(selectedPlateIndices.begin(), selectedPlateIndices.end()),
            selectedPlateIndices.end());
        if (selectedPlateIndices.size() != 1) {
            throw std::invalid_argument("分割する板材を1枚だけ選択してください。");
        }
        const int plateIndex = selectedPlateIndices.front();
        const std::string sourceName = project_.Plates()[plateIndex].name;
        std::vector<std::string> reservedNames;
        const auto uniquePieceName = [this, &sourceName, &reservedNames](std::string suffix) {
            std::string candidate = sourceName + std::move(suffix);
            int number = 2;
            while (project_.FindPlate(candidate).has_value()
                || std::find(reservedNames.begin(), reservedNames.end(), candidate) != reservedNames.end()) {
                candidate = sourceName + "_part" + std::to_string(number++);
            }
            reservedNames.push_back(candidate);
            return candidate;
        };
        const std::string firstName = uniquePieceName("_part1");
        const std::string secondName = uniquePieceName("_part2");
        const auto axis = static_cast<PlateSplitAxis>(plateSplitAxis_->currentData().toInt());
        const double parameter = plateSplitPosition_->value() / 100.0;

        Project candidate = project_;
        candidate.SplitPlate(sourceName, axis, parameter, firstName, secondName);
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);

        std::vector<CadSelection> pieces;
        for (int index = 0; index < static_cast<int>(project_.Plates().size()); ++index) {
            if (project_.Plates()[index].name == firstName || project_.Plates()[index].name == secondName) {
                pieces.push_back({CadSelectionKind::Plate, index});
            }
        }
        UpdateSelections(std::move(pieces), true);
        statusBar()->showMessage(
            QStringLiteral("板材を%1%の位置で2部品に分割しました").arg(plateSplitPosition_->value()),
            4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::UpdatePlateSplitPreview()
{
    if (viewport_ == nullptr || plateSplitAxis_ == nullptr || plateSplitPosition_ == nullptr) {
        return;
    }
    if (toolsTabs_ == nullptr || toolsTabs_->currentIndex() != 5) {
        viewport_->SetPlateSplitPreview(std::nullopt, 0.5);
        return;
    }
    viewport_->SetPlateSplitPreview(
        static_cast<PlateSplitAxis>(plateSplitAxis_->currentData().toInt()),
        plateSplitPosition_->value() / 100.0);
}

void MainWindow::ApplyMeetSelectedLines()
{
    try {
        std::vector<int> indices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                indices.push_back(selection.index);
            }
        }
        if (indices.size() != 2) {
            throw std::invalid_argument("交点で合わせる2本の直線を選択してください。");
        }
        const auto first = project_.Wires()[indices[0]];
        const auto second = project_.Wires()[indices[1]];
        const auto result = MeetLinesAtIntersection(
            first.wire, RetainedLineEnd::Automatic,
            second.wire, RetainedLineEnd::Automatic);

        RecordUndo();
        project_.UpdateWireAndMetadata(
            first.name,
            result.first,
            RetargetLineConstraints(project_, first.metadata, result.first, true));
        project_.UpdateWireAndMetadata(
            second.name,
            result.second,
            RetargetLineConstraints(project_, second.metadata, result.second, true));
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections({
            {CadSelectionKind::Wire, indices[0]},
            {CadSelectionKind::Wire, indices[1]},
        }, true);
        statusBar()->showMessage(QStringLiteral("2本の直線を交点までトリム・延長しました"), 3000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 4000);
    }
}

bool MainWindow::RunCreationSelfTest()
{
    const auto fail = [](const char* stage) {
        qWarning() << "self-test failed:" << stage;
        QFile report(QStringLiteral("self-test-failure.txt"));
        if (report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            report.write(stage);
            report.write("\n");
        }
        return false;
    };
    const std::string initialDrawingPlaneName = ToName(activePlaneCombo_->currentText());
    const std::optional<WorkPlane> initialDrawingPlane = project_.FindWorkPlane(initialDrawingPlaneName);
    if (!initialDrawingPlane.has_value()) {
        return fail("find initial spline drawing plane");
    }
    const std::size_t beforeInitialSpline = project_.Wires().size();
    AddViewportSpline({
        initialDrawingPlane->ToWorld(-8.0, 0.0),
        initialDrawingPlane->ToWorld(-3.0, 4.0),
        initialDrawingPlane->ToWorld(3.0, -2.0),
        initialDrawingPlane->ToWorld(8.0, 1.0),
    });
    if (project_.Wires().size() != beforeInitialSpline + 1
        || project_.Wires().back().wire.Kind() != WireKind::CubicBSpline) {
        return fail("create initial B-spline");
    }
    UpdateSelection(
        {CadSelectionKind::Wire, static_cast<int>(project_.Wires().size()) - 1}, true);
    if (editWirePointTable_->rowCount() != 4) {
        return fail("select initial B-spline");
    }
    const int initialSplineIndex = static_cast<int>(project_.Wires().size()) - 1;
    const Vector3 originalControlPoint = project_.Wires().back().wire.ControlPoints()[1];
    std::vector<Vector3> movedControlPoints = project_.Wires().back().wire.ControlPoints();
    movedControlPoints[1] = movedControlPoints[1] + initialDrawingPlane->UAxis() * 1.0;
    ApplyViewportWireEdit(initialSplineIndex, Wire::CubicBSpline(movedControlPoints));
    if (!kachakacha::geometry::AlmostEqual(
            project_.Wires().back().wire.ControlPoints()[1], movedControlPoints[1], 1.0e-9)) {
        return fail("drag B-spline control point");
    }
    Undo();
    if (project_.Wires().size() != beforeInitialSpline + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires().back().wire.ControlPoints()[1], originalControlPoint, 1.0e-9)) {
        return fail("undo B-spline control point drag");
    }
    Undo();
    if (project_.Wires().size() != beforeInitialSpline) {
        return fail("undo initial B-spline");
    }
    const std::size_t initialPlaneCount = project_.WorkPlanes().size();
    const std::size_t initialWireCount = project_.Wires().size();
    const std::size_t initialDimensionCount = project_.ReferenceDimensions().size();
    if (toolsTabs_->count() != 9
        || toolsTabs_->tabText(0) != QStringLiteral("作図")
        || toolsTabs_->tabText(5) != QStringLiteral("面")
        || toolsTabs_->tabText(6) != QStringLiteral("出力")
        || toolsTabs_->tabText(7) != QStringLiteral("表示")
        || toolsTabs_->tabText(8) != QStringLiteral("情報")
        || activePlaneCombo_->count() == 0
        || plateFlatPatternSummary_ == nullptr
        || plateAssemblyGuidePreview_ == nullptr
        || platePdfPaper_ == nullptr
        || platePdfOverlap_ == nullptr
        || measurementMode_ == nullptr
        || measurementResultLabel_ == nullptr
        || measurementMetric_ == nullptr
        || measurementName_ == nullptr
        || measurementSaveButton_ == nullptr
        || referenceDimensionList_ == nullptr
        || referenceDimensionDeleteButton_ == nullptr
        || measureToolAction_ == nullptr
        || coincidentToolAction_ == nullptr
        || tangentToolAction_ == nullptr
        || curvatureToolAction_ == nullptr
        || removeCoincidentAction_ == nullptr
        || removeTangentAction_ == nullptr
        || editWireLockRadius_ == nullptr
        || drawingConstruction_ == nullptr
        || editWireConstruction_ == nullptr
        || gridPointsVisible_ == nullptr
        || gridSubdivision_ == nullptr
        || gridOrigin_[0] == nullptr
        || gridOrigin_[1] == nullptr
        || gridOriginToolAction_ == nullptr
        || wireColor_ == nullptr
        || wireWidth_ == nullptr
        || wireStyle_ == nullptr
        || surfaceFillColor_ == nullptr
        || plateFillColor_ == nullptr
        || backgroundColor_ == nullptr
        || majorGridColor_ == nullptr
        || minorGridColor_ == nullptr
        || modelFilter_ == nullptr
        || designDisplayAction_ == nullptr
        || finishedDisplayAction_ == nullptr
        || isolateDisplayAction_ == nullptr
        || pointToolAction_ == nullptr
        || modelExportScope_ == nullptr
        || plateVariableThickness_ == nullptr
        || plateEndThickness_ == nullptr
        || plateOffsetLayer_ == nullptr
        || lightCaseSelectionLabel_ == nullptr
        || lightCaseReferenceLabel_ == nullptr
        || lightCaseRootName_ == nullptr
        || lightCaseSurfaceName_ == nullptr
        || lightCaseDirectionMode_ == nullptr
        || lightCaseDirection_[0] == nullptr
        || beginnerGuideTitle_ == nullptr
        || beginnerGuideNext_ == nullptr
        || beginnerGuideSteps_ == nullptr
        || beginnerGuideContext_ == nullptr
        || beginnerGuideManualButton_ == nullptr
        || std::any_of(workflowButtons_.begin(), workflowButtons_.end(), [](QPushButton* button) {
            return button == nullptr;
        })) {
        return fail("drawing workbench is primary");
    }
    toolsTabs_->setCurrentIndex(0);
    gridSubdivision_->setCurrentIndex(gridSubdivision_->findData(4));
    if (viewport_->GridSubdivision() != 4) {
        return fail("set JWCAD-style quarter grid subdivisions");
    }
    viewport_->SetGridOrigin(1.25, -2.5);
    if (std::abs(viewport_->GridOriginU() - 1.25) > 1.0e-9
        || std::abs(viewport_->GridOriginV() + 2.5) > 1.0e-9) {
        return fail("set numeric grid origin");
    }
    gridOrigin_[0]->setValue(0.0);
    gridOrigin_[1]->setValue(0.0);
    viewport_->SetGridOrigin(0.0, 0.0);
    SetViewportTool(ViewportTool::MoveGridOrigin);
    if (viewport_->Tool() != ViewportTool::MoveGridOrigin || !gridOriginToolAction_->isChecked()) {
        return fail("activate drag grid origin tool");
    }
    SetViewportTool(ViewportTool::Select);

    SetDisplayColorButton(wireColor_, QColor("#325a6b"));
    wireWidth_->setValue(3.25);
    wireStyle_->setCurrentIndex(wireStyle_->findData(static_cast<int>(Qt::DotLine)));
    SetDisplayColorButton(backgroundColor_, QColor("#f0f3f4"));
    ApplyDisplaySettings();
    if (viewport_->WireColorSetting() != QColor("#325a6b")
        || std::abs(viewport_->WireWidthSetting() - 3.25) > 1.0e-9
        || viewport_->WireStyleSetting() != Qt::DotLine
        || viewport_->BackgroundColor() != QColor("#f0f3f4")) {
        return fail("apply display color width style and background");
    }
    SetDisplayColorButton(wireColor_, QColor("#263b44"));
    wireWidth_->setValue(2.0);
    wireStyle_->setCurrentIndex(wireStyle_->findData(static_cast<int>(Qt::SolidLine)));
    SetDisplayColorButton(backgroundColor_, QColor("#f5f6f7"));
    ApplyDisplaySettings();

    SetViewportTool(ViewportTool::DrawLine);
    toolsTabs_->setCurrentIndex(5);
    if (viewport_->Tool() != ViewportTool::Select
        || beginnerGuideTitle_->text().isEmpty()
        || !workflowButtons_[2]->isChecked()) {
        return fail("beginner workflow guide follows tab and cancels stale tools");
    }
    toolsTabs_->setCurrentIndex(0);
    SetViewportTool(ViewportTool::DrawLine);

    planeName_->setText("__ui_test_plane");
    planeMethod_->setCurrentIndex(0);
    standardPlane_->setCurrentIndex(1);
    planeOffset_->setValue(2.5);
    planeTilt_->setValue(12.0);
    AddWorkPlane();
    if (project_.WorkPlanes().size() != initialPlaneCount + 1 || !project_.FindWorkPlane("__ui_test_plane").has_value()) {
        return fail("add workplane");
    }
    const std::size_t planeCountBeforeViewOnly = project_.WorkPlanes().size();
    const WorkPlane viewOnlyPlane = WorkPlaneFromInputs();
    AlignViewportFromPlaneInputs();
    if (project_.WorkPlanes().size() != planeCountBeforeViewOnly
        || !kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), viewOnlyPlane.Normal(), 1.0e-8)) {
        return fail("view without creating workplane");
    }

    wireName_->setText("__ui_test_line3d");
    wireKind_->setCurrentIndex(0);
    lineStart_[0]->setValue(0.0);
    lineStart_[1]->setValue(0.0);
    lineStart_[2]->setValue(0.0);
    lineEnd_[0]->setValue(2.0);
    lineEnd_[1]->setValue(7.0);
    lineEnd_[2]->setValue(4.0);
    AddWire();

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount)}, false);
    qobject_cast<QDoubleSpinBox*>(editWirePointTable_->cellWidget(1, 0))->setValue(3.0);
    qobject_cast<QDoubleSpinBox*>(editWirePointTable_->cellWidget(1, 1))->setValue(8.0);
    qobject_cast<QDoubleSpinBox*>(editWirePointTable_->cellWidget(1, 2))->setValue(5.0);
    ApplySelectedEdit();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[initialWireCount].wire.End(), {3.0, 8.0, 5.0})) {
        return fail("edit wire");
    }
    Undo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[initialWireCount].wire.End(), {2.0, 7.0, 4.0})) {
        return fail("undo wire edit");
    }
    Redo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[initialWireCount].wire.End(), {3.0, 8.0, 5.0})) {
        return fail("redo wire edit");
    }

    wireName_->setText("__ui_test_sketch_line");
    wireKind_->setCurrentIndex(2);
    wirePlane_->setCurrentText("__ui_test_plane");
    sketchLineStart_[0]->setValue(1.0);
    sketchLineStart_[1]->setValue(2.0);
    sketchLineEnd_[0]->setValue(5.0);
    sketchLineEnd_[1]->setValue(6.0);
    AddWire();

    const std::size_t constrainedLineIndex = initialWireCount + 1;
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(constrainedLineIndex)}, false);
    editWireLockLength_->setChecked(true);
    editWireConstraintLength_->setValue(8.0);
    editWireLockAngle_->setChecked(true);
    editWireConstraintAngle_->setValue(0.0);
    ApplySelectedEdit();
    if (project_.Wires()[constrainedLineIndex].metadata.lineConstraints.lengthMillimeters != 8.0
        || project_.Wires()[constrainedLineIndex].metadata.lineConstraints.angleDegrees != 0.0
        || std::abs((project_.Wires()[constrainedLineIndex].wire.End()
                - project_.Wires()[constrainedLineIndex].wire.Start()).Length() - 8.0) > 1.0e-8) {
        return fail("apply line dimensions");
    }
    Undo();
    if (!project_.Wires()[constrainedLineIndex].metadata.lineConstraints.Empty()) {
        return fail("undo line dimensions");
    }
    Redo();
    if (project_.Wires()[constrainedLineIndex].metadata.lineConstraints.lengthMillimeters != 8.0
        || project_.Wires()[constrainedLineIndex].metadata.lineConstraints.angleDegrees != 0.0) {
        return fail("redo line dimensions");
    }

    wireName_->setText("__ui_test_lineB");
    wireKind_->setCurrentIndex(0);
    lineStart_[0]->setValue(0.0);
    lineStart_[1]->setValue(0.0);
    lineStart_[2]->setValue(0.0);
    lineEnd_[0]->setValue(0.0);
    lineEnd_[1]->setValue(10.0);
    lineEnd_[2]->setValue(0.0);
    AddWire();

    chamferName_->setText("__ui_test_chamfer");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(initialWireCount)},
        {CadSelectionKind::Wire, static_cast<int>(initialWireCount + 2)},
    }, true);
    if (chamferFirstWire_->currentText() != "__ui_test_line3d"
        || chamferSecondWire_->currentText() != "__ui_test_lineB") {
        return fail("multi-select chamfer inputs");
    }
    chamferFirstBranch_->setCurrentIndex(0);
    chamferSecondBranch_->setCurrentIndex(0);
    chamferFirstDistance_->setValue(0.5);
    chamferSecondDistance_->setValue(0.75);
    ApplyLineChamfer();

    if (project_.Wires().size() != initialWireCount + 4) {
        return fail("create chamfer count");
    }
    if (project_.Wires()[initialWireCount + 3].name != "__ui_test_chamfer") {
        return fail("create chamfer name");
    }

    Undo();
    machiningType_->setCurrentIndex(1);
    chamferName_->setText("__ui_test_fillet");
    machiningPickFirstButton_->click();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount)}, true);
    machiningPickSecondButton_->click();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount + 2)}, true);
    if (chamferFirstWire_->currentText() != "__ui_test_line3d"
        || chamferSecondWire_->currentText() != "__ui_test_lineB") {
        return fail("individual machining picks");
    }
    filletRadius_->setValue(0.5);
    ApplyLineFillet();

    if (project_.Wires().size() != initialWireCount + 4) {
        return fail("create fillet count");
    }
    const auto& line3d = project_.Wires()[initialWireCount];
    const auto& sketchLine = project_.Wires()[initialWireCount + 1];
    const auto& fillet = project_.Wires()[initialWireCount + 3];
    const bool result = kachakacha::geometry::AlmostEqual(line3d.wire.End(), {3.0, 8.0, 5.0})
        && sketchLine.metadata.sourcePlaneName == "__ui_test_plane"
        && sketchLine.metadata.planePolicy == WirePlanePolicy::ReferenceOnly
        && fillet.name == "__ui_test_fillet"
        && fillet.wire.Kind() == WireKind::CircularArc;
    if (!result) {
        return fail("pre-direct drawing checks");
    }

    int drawingPlaneIndex = activePlaneCombo_->findText(QStringLiteral("top_XY"));
    if (drawingPlaneIndex < 0 && activePlaneCombo_->count() > 1) {
        drawingPlaneIndex = 1;
    }
    if (drawingPlaneIndex < 0) {
        return fail("find drawing workplane");
    }
    activePlaneCombo_->setCurrentIndex(drawingPlaneIndex);
    const std::string drawingPlaneName = ToName(activePlaneCombo_->currentText());
    const auto selectedPlaneIterator = std::find_if(
        project_.WorkPlanes().begin(), project_.WorkPlanes().end(),
        [&](const auto& plane) { return plane.name == drawingPlaneName; });
    if (selectedPlaneIterator == project_.WorkPlanes().end()) {
        return fail("find selected drawing workplane");
    }
    const WorkPlane selectedDrawingPlane = selectedPlaneIterator->plane;
    const int selectedPlaneIndex = static_cast<int>(
        std::distance(project_.WorkPlanes().begin(), selectedPlaneIterator));
    viewport_->AlignToActiveWorkPlane();
    snapAction_->setChecked(true);
    snapStepField_->setValue(1.0);

    const auto sendMouse = [this](
                               QEvent::Type type,
                               QPointF position,
                               Qt::MouseButton button,
                               Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPointF globalPosition(viewport_->mapToGlobal(position.toPoint()));
        QMouseEvent event(type, position, globalPosition, button, buttons, modifiers);
        QApplication::sendEvent(viewport_, &event);
    };
    const auto click = [&](QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sendMouse(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
        sendMouse(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
        sendMouse(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
    };
    const auto rightClick = [&](QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sendMouse(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, modifiers);
        sendMouse(QEvent::MouseButtonPress, position, Qt::RightButton, Qt::RightButton, modifiers);
        sendMouse(QEvent::MouseButtonRelease, position, Qt::RightButton, Qt::NoButton, modifiers);
    };
    const auto dragMouse = [&](QPointF start, QPointF end, Qt::MouseButton button,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sendMouse(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
        sendMouse(QEvent::MouseButtonPress, start, button, button, modifiers);
        sendMouse(QEvent::MouseMove, end, Qt::NoButton, button, modifiers);
        sendMouse(QEvent::MouseButtonRelease, end, button, Qt::NoButton, modifiers);
    };
    const auto drag = [&](QPointF start, QPointF end) {
        dragMouse(start, end, Qt::LeftButton);
    };
    const QPointF center(viewport_->width() * 0.5, viewport_->height() * 0.5);
    const QPointF cubeCenter(viewport_->width() - 60.0, 49.0);
    const QPointF cubeHome(viewport_->width() - 60.0, 102.0);
    const QPointF cubeSelection(viewport_->width() - 60.0, 135.0);
    const auto cubePointForDirection = [this, cubeCenter](Vector3 direction) {
        const Vector3 view = viewport_->ViewDirection();
        const Vector3 up = viewport_->ViewUpDirection();
        const Vector3 right = kachakacha::geometry::Cross(up, view).Normalized();
        return cubeCenter + QPointF(
            kachakacha::geometry::Dot(direction, right) * 22.0,
            -kachakacha::geometry::Dot(direction, up) * 22.0);
    };
    const std::size_t beforeViewCube = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawLine);
    click(cubeHome);
    const QPointF cubeTop = cubePointForDirection({0.0, 0.0, 1.0});
    click(cubeTop);
    if (!kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), {0.0, 0.0, 1.0}, 1.0e-8)) {
        return fail("view cube top face");
    }
    click(cubeHome);
    const Vector3 homeDirection = viewport_->ViewDirection();
    const Vector3 cornerDirection{
        homeDirection.x >= 0.0 ? 1.0 : -1.0,
        homeDirection.y >= 0.0 ? 1.0 : -1.0,
        homeDirection.z >= 0.0 ? 1.0 : -1.0,
    };
    click(cubePointForDirection(cornerDirection));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), cornerDirection.Normalized(), 1.0e-8)) {
        return fail("view cube corner view");
    }
    const Vector3 directionBeforeRoll = viewport_->ViewDirection();
    const Vector3 upBeforeRoll = viewport_->ViewUpDirection();
    click(QPointF(viewport_->width() - 40.0, 10.0));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeRoll, 1.0e-8)
        || kachakacha::geometry::AlmostEqual(
            viewport_->ViewUpDirection(), upBeforeRoll, 1.0e-8)) {
        return fail("view cube roll control");
    }
    click(cubeHome);
    const Vector3 directionBeforeWorldX = viewport_->ViewDirection();
    click(QPointF(cubeCenter.x() - 146.0, 30.0));
    if (kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeWorldX, 1.0e-8)
        || std::abs(viewport_->ViewDirection().x - directionBeforeWorldX.x) > 1.0e-8) {
        return fail("view cube absolute X rotation");
    }
    const Vector3 directionBeforeRelativeX = viewport_->ViewDirection();
    const Vector3 rightBeforeRelativeX = viewport_->ViewRightDirection();
    click(QPointF(cubeCenter.x() - 82.0, 30.0));
    if (kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeRelativeX, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            viewport_->ViewRightDirection(), rightBeforeRelativeX, 1.0e-8)) {
        return fail("view cube relative X rotation");
    }
    const Vector3 directionBeforeFineRotation = viewport_->ViewDirection();
    click(QPointF(cubeCenter.x() - 118.0, 30.0), Qt::ShiftModifier);
    const double fineAngle = std::acos(std::clamp(
        kachakacha::geometry::Dot(directionBeforeFineRotation, viewport_->ViewDirection()),
        -1.0, 1.0));
    if (std::abs(fineAngle - 5.0 * kPi / 180.0) > 1.0e-8) {
        return fail("view cube five degree fine rotation");
    }
    click(cubeHome);
    const Vector3 beforeTinyCubeMotion = viewport_->ViewDirection();
    drag(cubeHome, cubeHome + QPointF(2.0, 0.0));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), beforeTinyCubeMotion, 1.0e-8)) {
        return fail("view cube ignores accidental motion");
    }
    const Vector3 beforeCubeDrag = viewport_->ViewDirection();
    const QPointF draggableCubeFace = cubePointForDirection({0.0, 0.0, 1.0});
    drag(draggableCubeFace, draggableCubeFace + QPointF(18.0, -10.0));
    if (kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), beforeCubeDrag, 1.0e-8)
        || project_.Wires().size() != beforeViewCube
        || viewport_->DrawingPointCount() != 0) {
        return fail("view cube drag without drawing");
    }
    const Vector3 directionBeforeFit = viewport_->ViewDirection();
    const Vector3 upBeforeFit = viewport_->ViewUpDirection();
    viewport_->FitAll();
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeFit, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            viewport_->ViewUpDirection(), upBeforeFit, 1.0e-8)) {
        return fail("fit all preserves view cube orientation");
    }
    SetViewportTool(ViewportTool::Select);
    const Vector3 beforeLeftDrag = viewport_->ViewDirection();
    drag(center + QPointF(-180.0, 150.0), center + QPointF(-130.0, 120.0));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), beforeLeftDrag, 1.0e-8)) {
        return fail("left drag must not orbit");
    }
    dragMouse(
        center + QPointF(-180.0, 150.0), center + QPointF(-130.0, 120.0),
        Qt::MiddleButton, Qt::ShiftModifier);
    if (kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), beforeLeftDrag, 1.0e-8)) {
        return fail("shift middle drag orbits");
    }
    SetViewportTool(ViewportTool::DrawLine);
    const Vector3 viewBeforeSelectionAlignment = viewport_->ViewDirection();
    const Vector3 selectedPlaneNormal = selectedDrawingPlane.Normal();
    const Vector3 expectedSelectionView = kachakacha::geometry::Dot(
            selectedPlaneNormal, viewBeforeSelectionAlignment) < 0.0
        ? -selectedPlaneNormal
        : selectedPlaneNormal;
    UpdateSelection({CadSelectionKind::WorkPlane, selectedPlaneIndex}, true);
    click(cubeSelection);
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), expectedSelectionView, 1.0e-8)
        || project_.Wires().size() != beforeViewCube
        || viewport_->DrawingPointCount() != 0) {
        return fail("view cube align selected workplane");
    }
    viewport_->AlignToActiveWorkPlane();

    const std::size_t directStart = project_.Wires().size();

    SetViewportTool(ViewportTool::DrawLine);
    click(center + QPointF(-150.0, 90.0));
    click(center + QPointF(-60.0, 90.0));
    const std::size_t afterLine = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawRectangle);
    drag(center + QPointF(15.0, -105.0), center + QPointF(120.0, -35.0));
    const std::size_t afterRectangle = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawCircle);
    click(center + QPointF(90.0, 80.0));
    click(center + QPointF(135.0, 80.0));

    Undo();
    if (project_.Wires().size() != directStart + 2) {
        return fail("undo direct circle");
    }
    Redo();

    SetViewportTool(ViewportTool::DrawPolyline);
    const std::size_t beforeCancelledPolyline = project_.Wires().size();
    click(center + QPointF(-175.0, -75.0));
    click(center + QPointF(-135.0, -105.0));
    cancelDrawingAction_->trigger();
    if (viewport_->DrawingPointCount() != 0 || project_.Wires().size() != beforeCancelledPolyline) {
        return fail("cancel direct polyline");
    }
    click(center + QPointF(-150.0, -20.0));
    click(center + QPointF(-110.0, -60.0));
    click(center + QPointF(-65.0, -20.0));
    if (viewport_->DrawingPointCount() != 3 || !finishDrawingAction_->isEnabled()) {
        return fail("polyline drawing state");
    }
    finishDrawingAction_->trigger();

    SetViewportTool(ViewportTool::DrawArc);
    click(center + QPointF(-20.0, 70.0));
    click(center + QPointF(20.0, 35.0));
    click(center + QPointF(60.0, 70.0));

    SetViewportTool(ViewportTool::DrawBezier);
    click(center + QPointF(-145.0, 35.0));
    click(center + QPointF(-125.0, -5.0));
    click(center + QPointF(-85.0, -5.0));
    click(center + QPointF(-65.0, 35.0));
    if (project_.Wires().size() != directStart + 6
        || project_.Wires()[directStart].wire.Kind() != WireKind::Line
        || project_.Wires()[directStart + 1].wire.Kind() != WireKind::Polyline
        || !project_.Wires()[directStart + 1].wire.IsClosed()
        || project_.Wires()[directStart + 2].wire.Kind() != WireKind::Circle
        || project_.Wires()[directStart + 3].wire.Kind() != WireKind::Polyline
        || project_.Wires()[directStart + 4].wire.Kind() != WireKind::CircularArc
        || project_.Wires()[directStart + 5].wire.Kind() != WireKind::CubicBezier
        || project_.Wires()[directStart].metadata.sourcePlaneName != drawingPlaneName
        || project_.Wires()[directStart].metadata.planePolicy != WirePlanePolicy::ReferenceOnly) {
        qWarning() << "direct drawing self-test failed"
                   << "initial" << directStart
                   << "line" << afterLine
                   << "rectangle" << afterRectangle
                   << "circle" << project_.Wires().size();
        return fail("direct drawing result");
    }

    {
        const Project snapTestProject = project_;
        const auto snapTestUndo = undoStack_;
        const auto snapTestRedo = redoStack_;
        const std::size_t snapWireStart = project_.Wires().size();

        SetViewportTool(ViewportTool::DrawLine);
        click(center + QPointF(-120.0, 0.0), Qt::ControlModifier);
        click(center + QPointF(120.0, 0.0), Qt::ControlModifier);
        SetViewportTool(ViewportTool::DrawLine);
        click(center + QPointF(0.0, -105.0), Qt::ControlModifier);
        click(center + QPointF(0.0, 105.0), Qt::ControlModifier);
        if (project_.Wires().size() != snapWireStart + 2) {
            return fail("create crossing snap test lines");
        }
        SetViewportTool(ViewportTool::DrawPoint);
        sendMouse(QEvent::MouseMove, center + QPointF(5.0, 0.0), Qt::NoButton, Qt::NoButton);
        if (!viewport_->DrawingSnapHover().has_value()
            || viewport_->DrawingSnapHover()->kind != DrawingSnapKind::Intersection) {
            return fail("moderate intersection snap candidate");
        }
        const std::size_t pointStart = project_.Points().size();
        click(center + QPointF(5.0, 0.0));
        if (project_.Points().size() != pointStart + 1) {
            return fail("create point at intersection");
        }

        const WorkPlane& snapPlane = selectedDrawingPlane;
        const auto firstStart = snapPlane.Project(project_.Wires()[snapWireStart].wire.Start());
        const auto firstEnd = snapPlane.Project(project_.Wires()[snapWireStart].wire.End());
        const auto secondStart = snapPlane.Project(project_.Wires()[snapWireStart + 1].wire.Start());
        const auto secondEnd = snapPlane.Project(project_.Wires()[snapWireStart + 1].wire.End());
        const double firstU = firstEnd.u - firstStart.u;
        const double firstV = firstEnd.v - firstStart.v;
        const double secondU = secondEnd.u - secondStart.u;
        const double secondV = secondEnd.v - secondStart.v;
        const double denominator = firstU * secondV - firstV * secondU;
        if (std::abs(denominator) <= 1.0e-12) {
            return fail("crossing snap test geometry");
        }
        const double deltaU = secondStart.u - firstStart.u;
        const double deltaV = secondStart.v - firstStart.v;
        const double parameter = (deltaU * secondV - deltaV * secondU) / denominator;
        const Vector3 expectedIntersection = snapPlane.ToWorld(
            firstStart.u + firstU * parameter,
            firstStart.v + firstV * parameter);
        if (!kachakacha::geometry::AlmostEqual(
                project_.Points().back().point, expectedIntersection, 1.0e-8)) {
            return fail("point uses exact line intersection");
        }
        SetViewportTool(ViewportTool::DrawLine);
        const std::size_t nearbyStartWireCount = project_.Wires().size();
        rightClick(center + QPointF(11.0, 0.0));
        if (viewport_->DrawingPointCount() != 1) {
            return fail("right click begins at nearby structural point");
        }
        click(center + QPointF(90.0, 55.0), Qt::ControlModifier);
        if (project_.Wires().size() != nearbyStartWireCount + 1
            || !kachakacha::geometry::AlmostEqual(
                project_.Wires().back().wire.Start(), expectedIntersection, 1.0e-8)) {
            return fail("right click line uses exact nearby intersection");
        }

        SetViewportTool(ViewportTool::DrawPoint);
        const QPointF bypassPosition = center + QPointF(1.0, 0.0);
        sendMouse(
            QEvent::MouseMove,
            bypassPosition,
            Qt::NoButton,
            Qt::NoButton,
            Qt::ControlModifier);
        if (viewport_->DrawingSnapHover().has_value()) {
            return fail("ctrl bypass hides snap candidate");
        }
        click(bypassPosition, Qt::ControlModifier);
        if (project_.Points().size() != pointStart + 2
            || kachakacha::geometry::AlmostEqual(
                project_.Points().back().point, expectedIntersection, 1.0e-8)) {
            return fail("ctrl bypass creates unsnapped point");
        }
        std::optional<QPointF> freeScreenPoint;
        for (int y = 25; y <= 85 && !freeScreenPoint.has_value(); ++y) {
            for (int x = 25; x <= 85; ++x) {
                const QPointF candidate = center + QPointF(x, y);
                sendMouse(QEvent::MouseMove, candidate, Qt::NoButton, Qt::NoButton);
                if (!viewport_->DrawingSnapHover().has_value()) {
                    freeScreenPoint = candidate;
                    break;
                }
            }
        }
        if (!freeScreenPoint.has_value()) {
            return fail("find unsnapped drawing position");
        }
        click(*freeScreenPoint);
        if (project_.Points().size() != pointStart + 3) {
            return fail("create freely positioned point");
        }
        const auto freeCoordinates = snapPlane.Project(project_.Points().back().point);
        const double minorStep = snapStepField_->value()
            / static_cast<double>(viewport_->GridSubdivision());
        const double nearestGridU = viewport_->GridOriginU()
            + std::round((freeCoordinates.u - viewport_->GridOriginU()) / minorStep) * minorStep;
        const double nearestGridV = viewport_->GridOriginV()
            + std::round((freeCoordinates.v - viewport_->GridOriginV()) / minorStep) * minorStep;
        if (std::abs(freeCoordinates.u - nearestGridU) <= 1.0e-9
            && std::abs(freeCoordinates.v - nearestGridV) <= 1.0e-9) {
            return fail("free point must not be forced onto grid");
        }

        project_ = snapTestProject;
        undoStack_ = snapTestUndo;
        redoStack_ = snapTestRedo;
        RefreshModelViews(false);
        UpdateHistoryActions();
    }

    {
        const Project directEditSavedProject = project_;
        const auto directEditSavedUndo = undoStack_;
        const auto directEditSavedRedo = redoStack_;
        const auto directEditSavedReference = referenceWireName_;
        const bool directEditSavedModified = modified_;
        const QString directEditSavedTitle = windowTitle();
        const Vector3 directEditSavedViewTarget = viewport_->ViewTarget();
        const double directEditSavedViewScale = viewport_->ViewScale();

        Project directEditProject;
        directEditProject.AddWorkPlane(
            "direct_edit_XY",
            WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
        directEditProject.AddWire("boundary_left", Wire::Line({-4.0, -5.0, 0.0}, {-4.0, 5.0, 0.0}));
        directEditProject.AddWire("boundary_right", Wire::Line({4.0, -5.0, 0.0}, {4.0, 5.0, 0.0}));
        directEditProject.AddWire("trim_target", Wire::Line({-8.0, 0.0, 0.0}, {8.0, 0.0, 0.0}));
        directEditProject.AddWire("extend_target", Wire::Line({-2.0, 3.0, 0.0}, {2.0, 3.0, 0.0}));
        project_ = std::move(directEditProject);
        undoStack_.clear();
        redoStack_.clear();
        referenceWireName_.reset();
        RefreshModelViews(true);
        SetDisplayMode(ViewportDisplayMode::Design);
        const int directEditPlane = activePlaneCombo_->findText(QStringLiteral("direct_edit_XY"));
        if (directEditPlane < 0) {
            return fail("find direct trim test plane");
        }
        activePlaneCombo_->setCurrentIndex(directEditPlane);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();

        SetViewportTool(ViewportTool::TrimWire);
        click(viewport_->ScreenPoint({0.0, 0.0, 0.0}));
        if (project_.Wires().size() != 5
            || !kachakacha::geometry::AlmostEqual(project_.Wires()[3].wire.End(), {-4.0, 0.0, 0.0}, 1.0e-8)
            || !kachakacha::geometry::AlmostEqual(project_.Wires()[4].wire.Start(), {4.0, 0.0, 0.0}, 1.0e-8)) {
            return fail("direct middle line trim");
        }
        Undo();
        if (project_.Wires().size() != 4
            || project_.Wires()[2].name != "trim_target") {
            return fail("undo direct line trim");
        }

        SetViewportTool(ViewportTool::ExtendWire);
        const QPointF extendPick = viewport_->ScreenPoint({1.7, 3.0, 0.0});
        sendMouse(QEvent::MouseMove, extendPick, Qt::NoButton, Qt::NoButton);
        if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
            || viewport_->HoveredSelection().index != 3) {
            return fail("direct line extend target hover");
        }
        if (!viewport_->DirectLineEditPreviewReady()) {
            return fail("direct line extend preview");
        }
        click(extendPick);
        if (project_.Wires().size() != 4) {
            return fail("direct line extend wire count");
        }
        if (kachakacha::geometry::AlmostEqual(
                project_.Wires()[3].wire.End(), {2.0, 3.0, 0.0}, 1.0e-8)) {
            return fail("direct line extend unchanged");
        }
        if (!kachakacha::geometry::AlmostEqual(
                project_.Wires()[3].wire.End(), {4.0, 3.0, 0.0}, 1.0e-8)) {
            return fail("direct line extend wrong endpoint");
        }
        Undo();
        if (!kachakacha::geometry::AlmostEqual(
                project_.Wires()[3].wire.End(), {2.0, 3.0, 0.0}, 1.0e-8)) {
            return fail("undo direct line extend");
        }
        rightClick(viewport_->ScreenPoint({0.0, 3.0, 0.0}));
        if (viewport_->Tool() != ViewportTool::Select) {
            return fail("right click exits direct line edit");
        }

        project_.AddWire("curve_trim_target", Wire::CubicBezier(
            {-8.0, -3.0, 0.0}, {-5.0, -1.0, 0.0},
            {5.0, -5.0, 0.0}, {8.0, -3.0, 0.0}));
        RefreshModelViews(false);
        viewport_->FitAll();
        const int curveTrimIndex = 4;
        SetViewportTool(ViewportTool::TrimWire);
        const QPointF curveTrimPick = viewport_->ScreenPoint(
            project_.Wires()[curveTrimIndex].wire.Evaluate(0.5));
        sendMouse(QEvent::MouseMove, curveTrimPick, Qt::NoButton, Qt::NoButton);
        if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
            || viewport_->HoveredSelection().index != curveTrimIndex
            || !viewport_->DirectLineEditPreviewReady()) {
            return fail("direct curve trim preview");
        }
        click(curveTrimPick);
        if (project_.Wires().size() != 6
            || project_.Wires()[4].wire.Kind() != WireKind::CubicBezier
            || project_.Wires()[5].wire.Kind() != WireKind::CubicBezier) {
            return fail("direct cubic Bezier middle trim");
        }
        Undo();
        if (project_.Wires().size() != 5
            || project_.Wires()[curveTrimIndex].name != "curve_trim_target") {
            return fail("undo direct curve trim");
        }
        project_.SetWireVisible("curve_trim_target", false);

        project_.AddWire("curve_extend_boundary", Wire::Line(
            {14.0, -5.0, 0.0}, {14.0, 5.0, 0.0}));
        project_.AddWire("curve_extend_target", Wire::CubicBezier(
            {0.0, -4.0, 0.0}, {3.0, -4.0, 0.0},
            {7.0, -4.0, 0.0}, {10.0, -2.0, 0.0}));
        RefreshModelViews(false);
        viewport_->FitAll();
        const int curveExtendIndex = 6;
        SetViewportTool(ViewportTool::ExtendWire);
        const QPointF curveExtendPick = viewport_->ScreenPoint(
            project_.Wires()[curveExtendIndex].wire.Evaluate(0.9));
        sendMouse(QEvent::MouseMove, curveExtendPick, Qt::NoButton, Qt::NoButton);
        if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
            || viewport_->HoveredSelection().index != curveExtendIndex
            || !viewport_->DirectLineEditPreviewReady()) {
            return fail("direct curve extend preview");
        }
        click(curveExtendPick);
        if (project_.Wires()[curveExtendIndex].wire.Kind() != WireKind::CubicBezier
            || std::abs(project_.Wires()[curveExtendIndex].wire.End().x - 14.0) > 1.0e-7
            || project_.Wires()[curveExtendIndex].wire.End().y <= -2.0) {
            return fail("direct cubic Bezier extend");
        }
        Undo();
        if (!kachakacha::geometry::AlmostEqual(
                project_.Wires()[curveExtendIndex].wire.End(), {10.0, -2.0, 0.0}, 1.0e-8)) {
            return fail("undo direct curve extend");
        }

        const std::size_t configuredArcStart = project_.Wires().size();
        arcDrawingMode_->setCurrentIndex(1);
        arcRadiusField_->setValue(4.0);
        arcBulgeSide_->setCurrentIndex(arcBulgeSide_->findData(true));
        UpdateArcConfiguration();
        SetViewportTool(ViewportTool::DrawArc);
        click(viewport_->ScreenPoint({-3.0, 1.0, 0.0}), Qt::ControlModifier);
        click(viewport_->ScreenPoint({3.0, 1.0, 0.0}), Qt::ControlModifier);
        if (viewport_->DrawingPointCount() != 2 || !drawingDimensionCommitButton_->isEnabled()) {
            return fail("endpoint radius arc input state");
        }
        CommitDrawingDimensions();
        if (project_.Wires().size() != configuredArcStart + 1
            || project_.Wires().back().wire.Kind() != WireKind::CircularArc
            || std::abs(project_.Wires().back().wire.ArcData().radius - 4.0) > 1.0e-8) {
            return fail("endpoint radius arc drawing");
        }

        arcDrawingMode_->setCurrentIndex(2);
        arcDirectionBasis_->setCurrentIndex(arcDirectionBasis_->findData(0));
        arcDirectionAngle_->setValue(0.0);
        arcRadiusField_->setValue(2.0);
        arcExtentMode_->setCurrentIndex(arcExtentMode_->findData(0));
        arcExtentValue_->setValue(90.0);
        arcTurnSide_->setCurrentIndex(arcTurnSide_->findData(1.0));
        UpdateArcConfiguration();
        SetViewportTool(ViewportTool::DrawArc);
        click(viewport_->ScreenPoint({0.0, 2.0, 0.0}), Qt::ControlModifier);
        if (viewport_->DrawingPointCount() != 1 || !drawingDimensionCommitButton_->isEnabled()) {
            return fail("start tangent arc input state");
        }
        CommitDrawingDimensions();
        const Wire& tangentArc = project_.Wires().back().wire;
        if (project_.Wires().size() != configuredArcStart + 2
            || tangentArc.Kind() != WireKind::CircularArc
            || std::abs(tangentArc.ArcData().radius - 2.0) > 1.0e-8
            || std::abs(tangentArc.ArcData().sweepAngleRadians - kPi * 0.5) > 1.0e-8
            || kachakacha::geometry::Dot(MeasureWireTangent(tangentArc, 0.0), {1.0, 0.0, 0.0}) < 0.999999) {
            return fail("start tangent arc drawing");
        }
        arcDrawingMode_->setCurrentIndex(0);

        project_ = directEditSavedProject;
        undoStack_ = directEditSavedUndo;
        redoStack_ = directEditSavedRedo;
        referenceWireName_ = directEditSavedReference;
        modified_ = directEditSavedModified;
        setWindowTitle(directEditSavedTitle);
        RefreshModelViews(false);
        UpdateHistoryActions();
        activePlaneCombo_->setCurrentIndex(drawingPlaneIndex);
        viewport_->AlignToActiveWorkPlane();
        viewport_->RestoreViewFraming(directEditSavedViewTarget, directEditSavedViewScale);
    }

    SetViewportTool(ViewportTool::Select);
    sendMouse(QEvent::MouseMove, center + QPointF(-105.0, 90.0), Qt::NoButton, Qt::NoButton);
    if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
        || viewport_->HoveredSelection().index != static_cast<int>(directStart)) {
        return fail("wire hover hit");
    }
    SetViewportTool(ViewportTool::Coincident);
    click(center + QPointF(-60.0, 90.0));
    if (viewport_->CoincidencePicks().size() != 1
        || viewport_->CoincidencePicks().front().wireIndex != static_cast<int>(directStart)
        || viewport_->CoincidencePicks().front().endpoint != kachakacha::model::WireEndpoint::End) {
        return fail("direct endpoint coincidence pick");
    }
    viewport_->ClearCoincidencePicks();
    if (!viewport_->CoincidencePicks().empty()) {
        return fail("clear endpoint coincidence pick");
    }
    SetViewportTool(ViewportTool::Tangent);
    click(center + QPointF(-60.0, 90.0));
    if (viewport_->CoincidencePicks().size() != 1
        || viewport_->CoincidencePicks().front().wireIndex != static_cast<int>(directStart)) {
        return fail("direct tangent anchor pick");
    }
    const std::size_t tangentCountBeforeArc = project_.TangentConstraints().size();
    const std::size_t coincidenceCountBeforeArc = project_.CoincidentConstraints().size();
    const Wire arcBeforeTangent = project_.Wires()[directStart + 4].wire;
    click(center + QPointF(-20.0, 70.0));
    if (project_.TangentConstraints().size() != tangentCountBeforeArc + 1
        || project_.CoincidentConstraints().size() != coincidenceCountBeforeArc + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End(),
            project_.Wires()[directStart + 4].wire.Start(), 1.0e-8)
        || kachakacha::geometry::Dot(
            MeasureWireTangent(project_.Wires()[directStart].wire, 1.0),
            MeasureWireTangent(project_.Wires()[directStart + 4].wire, 0.0)) < 0.999999) {
        return fail("direct arc tangent connection");
    }
    Undo();
    if (project_.TangentConstraints().size() != tangentCountBeforeArc
        || project_.CoincidentConstraints().size() != coincidenceCountBeforeArc
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart + 4].wire.Start(), arcBeforeTangent.Start(), 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart + 4].wire.End(), arcBeforeTangent.End(), 1.0e-8)) {
        return fail("undo direct arc tangent connection");
    }
    viewport_->ClearCoincidencePicks();
    SetViewportTool(ViewportTool::Curvature);
    click(center + QPointF(-60.0, 90.0));
    if (viewport_->CoincidencePicks().size() != 1
        || viewport_->CoincidencePicks().front().wireIndex != static_cast<int>(directStart)) {
        return fail("direct curvature anchor pick");
    }
    viewport_->ClearCoincidencePicks();

    const std::size_t exactStart = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawLine);
    click(center + QPointF(-205.0, 135.0));
    QLineEdit* exactLengthEditor = drawingLengthField_->findChild<QLineEdit*>();
    if (exactLengthEditor == nullptr) {
        return fail("find expression dimension editor");
    }
    exactLengthEditor->setText(QStringLiteral("(10/2)*3"));
    drawingLengthField_->interpretText();
    if (!kachakacha::geometry::AlmostEqual(drawingLengthField_->value(), 15.0, 1.0e-10)) {
        return fail("expression dimension field");
    }
    drawingLengthField_->setValue(12.5);
    drawingAngleField_->setValue(30.0);
    CommitDrawingDimensions();

    SetViewportTool(ViewportTool::DrawRectangle);
    const QPointF exactRectangleStart = center + QPointF(155.0, -120.0);
    click(exactRectangleStart);
    sendMouse(QEvent::MouseMove, exactRectangleStart + QPointF(-45.0, 30.0), Qt::NoButton, Qt::NoButton);
    drawingWidthField_->setValue(8.0);
    drawingHeightField_->setValue(4.0);
    CommitDrawingDimensions();

    SetViewportTool(ViewportTool::DrawCircle);
    click(center + QPointF(175.0, 125.0));
    drawingRadiusField_->setValue(3.25);
    CommitDrawingDimensions();

    SetViewportTool(ViewportTool::DrawLine);
    const QPointF constrainedStart = center + QPointF(-205.0, -135.0);
    const QPointF constrainedEnd = constrainedStart + QPointF(65.0, 25.0);
    click(constrainedStart);
    sendMouse(QEvent::MouseMove, constrainedEnd, Qt::NoButton, Qt::NoButton, Qt::ShiftModifier);
    sendMouse(QEvent::MouseButtonPress, constrainedEnd, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(QEvent::MouseButtonRelease, constrainedEnd, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);

    const std::optional<WorkPlane> exactPlane = project_.FindWorkPlane(drawingPlaneName);
    kachakacha::model::PlaneCoordinates constrainedStartCoordinates;
    kachakacha::model::PlaneCoordinates constrainedEndCoordinates;
    if (exactPlane.has_value() && project_.Wires().size() == exactStart + 4) {
        constrainedStartCoordinates = exactPlane->Project(project_.Wires()[exactStart + 3].wire.Start());
        constrainedEndCoordinates = exactPlane->Project(project_.Wires()[exactStart + 3].wire.End());
    }
    if (project_.Wires().size() != exactStart + 4
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[exactStart].wire.End() - project_.Wires()[exactStart].wire.Start()).Length(),
            12.5, 1.0e-8)
        || project_.Wires()[exactStart + 1].wire.Kind() != WireKind::Polyline
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[exactStart + 1].wire.ControlPoints()[1]
                - project_.Wires()[exactStart + 1].wire.ControlPoints()[0]).Length(),
            8.0, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[exactStart + 1].wire.ControlPoints()[2]
                - project_.Wires()[exactStart + 1].wire.ControlPoints()[1]).Length(),
            4.0, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[exactStart + 2].wire.ArcData().radius, 3.25, 1.0e-8)
        || !exactPlane.has_value()
        || (std::abs(constrainedEndCoordinates.u - constrainedStartCoordinates.u) > 1.0e-8
            && std::abs(constrainedEndCoordinates.v - constrainedStartCoordinates.v) > 1.0e-8)) {
        return fail("dimension-driven direct drawing");
    }
    Undo();
    Undo();
    Undo();
    Undo();
    if (project_.Wires().size() != exactStart) {
        return fail("undo dimension-driven drawing");
    }

    const auto sendKey = [this](int key, const QString& text = QString()) {
        QWidget* target = QApplication::focusWidget();
        if (target == nullptr) {
            target = viewport_;
        }
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
        QApplication::sendEvent(target, &press);
        QWidget* releaseTarget = QApplication::focusWidget();
        if (releaseTarget == nullptr) {
            releaseTarget = target;
        }
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
        QApplication::sendEvent(releaseTarget, &release);
    };
    const auto typeExpression = [&sendKey](const QString& expression) {
        for (const QChar character : expression) {
            sendKey(character.unicode(), QString(character));
        }
    };
    SetViewportTool(ViewportTool::DrawLine);
    click(center + QPointF(-205.0, 155.0));
    typeExpression(QStringLiteral("(10/2)*3"));
    sendKey(Qt::Key_Tab);
    typeExpression(QStringLiteral("(45/3)*2"));
    sendKey(Qt::Key_Return);
    if (project_.Wires().size() != exactStart + 1
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires().back().wire.End() - project_.Wires().back().wire.Start()).Length(),
            15.0, 1.0e-8)) {
        return fail("cursor expression drawing");
    }
    const auto expressionStart = exactPlane->Project(project_.Wires().back().wire.Start());
    const auto expressionEnd = exactPlane->Project(project_.Wires().back().wire.End());
    if (!kachakacha::geometry::AlmostEqual(
            std::atan2(
                expressionEnd.v - expressionStart.v,
                expressionEnd.u - expressionStart.u) * 180.0 / kPi,
            30.0, 1.0e-8)) {
        return fail("cursor expression angle");
    }
    Undo();
    if (project_.Wires().size() != exactStart) {
        return fail("undo cursor expression drawing");
    }

    const std::size_t radiusCircleIndex = directStart + 2;
    const double radiusBeforeConstraint = project_.Wires()[radiusCircleIndex].wire.ArcData().radius;
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(radiusCircleIndex)}, true);
    editWireLockRadius_->setChecked(true);
    editArcRadius_->setValue(6.5);
    ApplySelectedEdit();
    if (project_.Wires()[radiusCircleIndex].metadata.curveConstraints.radiusMillimeters != 6.5
        || std::abs(project_.Wires()[radiusCircleIndex].wire.ArcData().radius - 6.5) > 1.0e-8) {
        return fail("apply radius constraint");
    }
    Undo();
    if (!project_.Wires()[radiusCircleIndex].metadata.curveConstraints.Empty()
        || std::abs(project_.Wires()[radiusCircleIndex].wire.ArcData().radius - radiusBeforeConstraint) > 1.0e-8) {
        return fail("undo radius constraint");
    }
    Redo();
    if (project_.Wires()[radiusCircleIndex].metadata.curveConstraints.radiusMillimeters != 6.5
        || std::abs(project_.Wires()[radiusCircleIndex].wire.ArcData().radius - 6.5) > 1.0e-8) {
        return fail("redo radius constraint");
    }

    const Wire offsetLineSource = project_.Wires()[directStart].wire;
    const double offsetCircleRadius = project_.Wires()[directStart + 2].wire.ArcData().radius;
    const std::size_t offsetStart = project_.Wires().size();
    SetViewportTool(ViewportTool::Select);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 2)},
    }, true);
    wireOffsetDistance_->setValue(1.25);
    wireOffsetSide_->setCurrentIndex(0);
    UpdateWireOffsetPreview();
    if (!wireOffsetApplyButton_->isEnabled() || viewport_->WireOffsetPreviewCount() != 2) {
        return fail("multi-wire offset preview");
    }
    ApplyWireOffset();
    if (project_.Wires().size() != offsetStart + 2
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[offsetStart].wire.Start() - offsetLineSource.Start()).Length(), 1.25, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            std::abs(project_.Wires()[offsetStart + 1].wire.ArcData().radius - offsetCircleRadius),
            1.25, 1.0e-8)
        || project_.Wires()[offsetStart + 1].metadata.curveConstraints.radiusMillimeters
            != project_.Wires()[offsetStart + 1].wire.ArcData().radius
        || project_.Wires()[offsetStart].metadata.sourcePlaneName != drawingPlaneName
        || project_.Wires()[offsetStart + 1].metadata.planePolicy != WirePlanePolicy::ReferenceOnly) {
        return fail("multi-wire offset result");
    }
    Undo();
    if (project_.Wires().size() != offsetStart) {
        return fail("undo multi-wire offset");
    }

    SetViewportTool(ViewportTool::Select);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 1)},
    }, true);
    const Wire beforeMove = project_.Wires()[directStart].wire;
    SetViewportTool(ViewportTool::MoveSelection);
    click(center + QPointF(-35.0, -150.0));
    click(center + QPointF(35.0, -150.0));
    if (project_.Wires().size() != directStart + 6) {
        return fail("direct move count");
    }
    const Vector3 moveDelta = project_.Wires()[directStart].wire.Start() - beforeMove.Start();
    if (moveDelta.LengthSquared() <= 1.0e-9
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End() - beforeMove.End(), moveDelta, 1.0e-8)) {
        return fail("direct move geometry");
    }
    Undo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), beforeMove.Start())) {
        return fail("undo direct move");
    }
    Redo();
    if (!kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.Start(), beforeMove.Start() + moveDelta)) {
        return fail("redo direct move");
    }

    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 1)},
    }, true);
    const Wire beforeRotation = project_.Wires()[directStart].wire;
    SetViewportTool(ViewportTool::RotateSelection);
    click(center + QPointF(5.0, 145.0));
    click(center + QPointF(75.0, 145.0));
    click(center + QPointF(5.0, 75.0));
    if (kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), beforeRotation.Start(), 1.0e-8)
        || std::abs((project_.Wires()[directStart].wire.End() - project_.Wires()[directStart].wire.Start()).Length()
                - (beforeRotation.End() - beforeRotation.Start()).Length())
            > 1.0e-8) {
        return fail("direct rotation geometry");
    }
    const Wire afterRotation = project_.Wires()[directStart].wire;
    Undo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), beforeRotation.Start(), 1.0e-8)) {
        return fail("undo direct rotation");
    }
    Redo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), afterRotation.Start(), 1.0e-8)) {
        return fail("redo direct rotation");
    }

    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 1)},
    }, true);
    const std::size_t copyStart = project_.Wires().size();
    SetViewportTool(ViewportTool::CopySelection);
    click(center + QPointF(-170.0, 145.0));
    click(center + QPointF(-170.0, 75.0));
    if (project_.Wires().size() != copyStart + 2
        || project_.Wires()[copyStart].wire.Kind() != WireKind::Line
        || project_.Wires()[copyStart + 1].wire.Kind() != WireKind::Polyline
        || project_.Wires()[copyStart].metadata.sourcePlaneName != drawingPlaneName) {
        return fail("direct copy");
    }
    Undo();
    if (project_.Wires().size() != copyStart) {
        return fail("undo direct copy");
    }
    Redo();
    if (project_.Wires().size() != copyStart + 2) {
        return fail("redo direct copy");
    }

    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(copyStart)},
        {CadSelectionKind::Wire, static_cast<int>(copyStart + 1)},
    }, true);
    const std::size_t mirrorStart = project_.Wires().size();
    SetViewportTool(ViewportTool::MirrorSelection);
    click(center + QPointF(175.0, -125.0));
    click(center + QPointF(175.0, 125.0));
    if (project_.Wires().size() != mirrorStart + 2
        || project_.Wires()[mirrorStart].wire.Kind() != WireKind::Line
        || project_.Wires()[mirrorStart + 1].wire.Kind() != WireKind::Polyline
        || project_.Wires()[mirrorStart].name.find("mirror") == std::string::npos) {
        return fail("direct mirror copy");
    }

    const std::size_t meetStart = project_.Wires().size();
    AddViewportLine({-14.0, -9.0, 0.0}, {-5.0, -9.0, 0.0});
    AddViewportLine({-2.0, -5.0, 0.0}, {-2.0, 3.0, 0.0});
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(meetStart)},
        {CadSelectionKind::Wire, static_cast<int>(meetStart + 1)},
    }, true);
    meetLinesAction_->trigger();
    const Vector3 expectedIntersection{-2.0, -9.0, 0.0};
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[meetStart].wire.End(), expectedIntersection)
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[meetStart + 1].wire.Start(), expectedIntersection)) {
        return fail("meet selected lines");
    }
    Undo();
    if (kachakacha::geometry::AlmostEqual(project_.Wires()[meetStart].wire.End(), expectedIntersection)) {
        return fail("undo meet selected lines");
    }
    Redo();

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(meetStart)}, true);
    SetReferenceFromSelection();
    if (!referenceWireName_.has_value()
        || *referenceWireName_ != project_.Wires()[meetStart].name
        || !viewport_->Selections().empty()) {
        return fail("set explicit reference line");
    }
    UseReferenceForPlaneRotation();
    if (planeMethod_->currentIndex() != 4
        || !kachakacha::geometry::AlmostEqual(ReadVector3(rotateAxisPoint_), project_.Wires()[meetStart].wire.Start())
        || !kachakacha::geometry::AlmostEqual(
            ReadVector3(rotateAxisDirection_),
            project_.Wires()[meetStart].wire.End() - project_.Wires()[meetStart].wire.Start())) {
        return fail("reference line as plane rotation axis");
    }
    const std::size_t referenceMirrorStart = project_.Wires().size();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(directStart + 2)}, true);
    mirrorToolAction_->trigger();
    if (project_.Wires().size() != referenceMirrorStart + 1
        || project_.Wires()[referenceMirrorStart].wire.Kind() != WireKind::Circle
        || project_.Wires()[referenceMirrorStart].name.find("mirror") == std::string::npos) {
        return fail("mirror from explicit reference line");
    }

    const QPointF splitLineStart = center + QPointF(20.0, 145.0);
    const QPointF splitLineEnd = center + QPointF(140.0, 145.0);
    const std::size_t splitStart = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawLine);
    click(splitLineStart);
    click(splitLineEnd);
    if (project_.Wires().size() != splitStart + 1) {
        return fail("create line for direct split");
    }
    const Wire splitSource = project_.Wires()[splitStart].wire;
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(splitStart)}, true);
    SetViewportTool(ViewportTool::SplitWire);
    click((splitLineStart + splitLineEnd) * 0.5);
    if (project_.Wires().size() != splitStart + 2
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.End(), project_.Wires()[splitStart + 1].wire.Start(), 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.Start(), splitSource.Start(), 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart + 1].wire.End(), splitSource.End(), 1.0e-8)) {
        return fail("direct split geometry");
    }
    Undo();
    if (project_.Wires().size() != splitStart + 1
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.Start(), splitSource.Start(), 1.0e-8)) {
        return fail("undo direct split");
    }
    Redo();
    if (project_.Wires().size() != splitStart + 2) {
        return fail("redo direct split");
    }
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(splitStart)},
        {CadSelectionKind::Wire, static_cast<int>(splitStart + 1)},
    }, true);
    joinWiresAction_->trigger();
    if (project_.Wires().size() != splitStart + 1
        || project_.Wires()[splitStart].wire.Kind() != WireKind::Polyline
        || (!kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.Start(), splitSource.Start(), 1.0e-8)
            && !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.End(), splitSource.Start(), 1.0e-8))) {
        return fail("join selected split parts");
    }
    Undo();
    if (project_.Wires().size() != splitStart + 2) {
        return fail("undo direct join");
    }
    Redo();
    if (project_.Wires().size() != splitStart + 1) {
        return fail("redo direct join");
    }

    const auto exportPlane = project_.FindWorkPlane(drawingPlaneName);
    if (!exportPlane.has_value()) {
        return fail("find planar export workplane");
    }
    std::vector<kachakacha::model::NamedWire> exportWires;
    for (const auto& wire : project_.Wires()) {
        if (WireLiesOnWorkPlane(wire.wire, *exportPlane)) {
            exportWires.push_back(wire);
        }
    }
    std::ostringstream svgOutput;
    std::ostringstream dxfOutput;
    WritePlanarSvg(svgOutput, *exportPlane, exportWires);
    WritePlanarDxf(dxfOutput, *exportPlane, exportWires);
    if (svgOutput.str().find("mm\" height=") == std::string::npos
        || svgOutput.str().find("<polyline") == std::string::npos
        || dxfOutput.str().find("$INSUNITS\n70\n4") == std::string::npos) {
        return fail("planar output from UI project");
    }

    const std::size_t compositeWireStart = project_.Wires().size();
    const std::size_t compositeSurfaceStart = project_.Surfaces().size();
    project_.AddWire("__ui_composite_bottom", Wire::Line(
        {0.0, 0.0, 0.0}, {8.0, 0.0, 0.0}));
    project_.AddWire("__ui_composite_right", Wire::CircularArcThroughThreePoints(
        {8.0, 0.0, 0.0}, {10.0, 3.0, 0.0}, {8.0, 6.0, 0.0}));
    project_.AddWire("__ui_composite_top", Wire::Line(
        {0.0, 6.0, 0.0}, {8.0, 6.0, 0.0}));
    project_.AddWire("__ui_composite_left", Wire::Line(
        {0.0, 6.0, 0.0}, {0.0, 0.0, 0.0}));
    RefreshModelViews(false);
    surfaceType_->setCurrentIndex(0);
    surfaceName_->setText("__ui_composite_surface");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart + 2)},
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart)},
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart + 3)},
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart + 1)},
    }, true);
    CreateSurfaceFromSelection();
    if (project_.Surfaces().size() != compositeSurfaceStart + 1
        || project_.Surfaces().back().surface.Kind() != SurfaceKind::Planar
        || project_.Surfaces().back().sourceWireNames.size() != 4
        || !project_.Surfaces().back().surface.FirstBoundary().IsClosed()) {
        return fail("create planar surface from separate lines and curve");
    }
    Undo();
    if (project_.Surfaces().size() != compositeSurfaceStart) {
        return fail("undo composite planar surface");
    }

    const std::size_t surfaceWireStart = project_.Wires().size();
    const std::size_t surfaceStart = project_.Surfaces().size();
    const std::size_t plateStart = project_.Plates().size();
    const std::size_t bodyStart = project_.Bodies().size();
    project_.AddWorkPlane("__ui_section_a_plane", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    project_.AddWorkPlane("__ui_section_mid_plane", WorkPlane::FromPointNormal({6.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    project_.AddWorkPlane("__ui_section_b_plane", WorkPlane::FromPointNormal({12.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    project_.AddWorkPlane("__ui_light_plan", WorkPlane::FromPointNormal({0.0, 0.0, 12.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
    project_.AddWire("__ui_section_a", Wire::CubicBezier(
        {0.0, -6.0, 0.0}, {0.0, -2.0, 3.0}, {0.0, 2.0, 3.0}, {0.0, 6.0, 0.0}),
        WireMetadata{"__ui_section_a_plane", WirePlanePolicy::ReferenceOnly});
    project_.AddWire("__ui_section_mid", Wire::CubicBezier(
        {6.0, -6.0, 0.0}, {6.0, -2.0, 6.0}, {6.0, 2.0, 6.0}, {6.0, 6.0, 0.0}),
        WireMetadata{"__ui_section_mid_plane", WirePlanePolicy::ReferenceOnly});
    project_.AddWire("__ui_section_b", Wire::CubicBezier(
        {12.0, -6.0, 0.0}, {12.0, -2.0, 5.0}, {12.0, 2.0, 5.0}, {12.0, 6.0, 0.0}),
        WireMetadata{"__ui_section_b_plane", WirePlanePolicy::ReferenceOnly});
    project_.AddWire("__ui_light_plan_circle", Wire::Circle(
        {6.0, 0.0, 12.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.25),
        WireMetadata{"__ui_light_plan", WirePlanePolicy::ReferenceOnly});
    RefreshModelViews(false);

    plateName_->setText("__ui_direct_variable_plate");
    plateThickness_->setValue(0.4);
    plateVariableThickness_->setChecked(true);
    plateEndThickness_->setValue(0.9);
    plateDirection_->setCurrentIndex(1);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 1)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 2)},
    }, true);
    CreatePlateFromSelectedWires();
    if (project_.Surfaces().size() != surfaceStart + 1
        || project_.Plates().size() != plateStart + 1
        || !project_.Plates().back().plate.HasVariableThickness()
        || std::abs(project_.Plates().back().plate.EndThickness() - 0.9) > 1.0e-12) {
        return fail("direct variable plate from selected wires");
    }
    Undo();
    if (project_.Surfaces().size() != surfaceStart || project_.Plates().size() != plateStart) {
        return fail("undo direct variable plate");
    }
    plateVariableThickness_->setChecked(false);

    surfaceType_->setCurrentIndex(2);
    surfaceName_->setText("__ui_nose_skin");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 1)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 2)},
    }, true);
    CreateSurfaceFromSelection();
    if (project_.Surfaces().size() != surfaceStart + 1
        || project_.Surfaces()[surfaceStart].surface.Kind() != SurfaceKind::Loft
        || viewport_->Selection().kind != CadSelectionKind::Surface) {
        return fail("create loft surface from selected sections");
    }
    Undo();
    if (project_.Surfaces().size() != surfaceStart) {
        return fail("undo loft surface");
    }
    Redo();
    if (project_.Surfaces().size() != surfaceStart + 1) {
        return fail("redo loft surface");
    }

    projectionSurface_->setCurrentText("__ui_nose_skin");
    projectionPlane_->setCurrentText("__ui_light_plan");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 3)},
        {CadSelectionKind::Surface, static_cast<int>(surfaceStart)},
    }, true);
    ProjectSelectedWiresToSurface();
    const std::size_t projectedLightIndex = surfaceWireStart + 4;
    if (project_.Wires().size() != surfaceWireStart + 5
        || !project_.Wires()[projectedLightIndex].projection.has_value()
        || project_.Wires()[projectedLightIndex].projection->sourceWireName != "__ui_light_plan_circle"
        || !project_.Wires()[projectedLightIndex].wire.IsClosed()) {
        return fail("project planar light drawing to selected surface");
    }
    Undo();
    if (project_.Wires().size() != surfaceWireStart + 4) {
        return fail("undo surface projection");
    }
    Redo();
    if (project_.Wires().size() != surfaceWireStart + 5
        || !project_.Wires()[projectedLightIndex].projection.has_value()) {
        return fail("redo surface projection");
    }
    const std::string projectedLightName = project_.Wires()[projectedLightIndex].name;

    plateSurface_->setCurrentText("__ui_nose_skin");
    plateName_->setText("__ui_nose_plate");
    plateThickness_->setValue(0.5);
    plateDirection_->setCurrentIndex(1);
    plateMaterial_->setCurrentIndex(0);
    CreatePlateFromSurface();
    if (project_.Plates().size() != plateStart + 1
        || project_.Plates()[plateStart].sourceSurfaceName != "__ui_nose_skin"
        || std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.5) > 1.0e-12
        || project_.Surfaces()[surfaceStart].visible
        || viewport_->Selection().kind != CadSelectionKind::Plate) {
        return fail("create plate from loft surface");
    }
    Undo();
    if (project_.Plates().size() != plateStart || !project_.Surfaces()[surfaceStart].visible) {
        return fail("undo plate creation");
    }
    Redo();
    if (project_.Plates().size() != plateStart + 1 || project_.Surfaces()[surfaceStart].visible) {
        return fail("redo plate creation");
    }

    const std::size_t lightCaseWireCount = project_.Wires().size();
    const std::size_t lightCaseSurfaceCount = project_.Surfaces().size();
    lightCaseRootName_->setText(QStringLiteral("__ui_light_root"));
    lightCaseSurfaceName_->setText(QStringLiteral("__ui_light_case_side"));
    lightCaseDirectionMode_->setCurrentIndex(2);
    lightCaseDirection_[0]->setValue(0.0);
    lightCaseDirection_[1]->setValue(0.0);
    lightCaseDirection_[2]->setValue(-1.0);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 3)},
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
    }, true);
    CreateProtrudingLightCase();
    if (project_.Wires().size() != lightCaseWireCount + 1
        || project_.Surfaces().size() != lightCaseSurfaceCount + 1
        || !project_.Wires().back().projection.has_value()
        || project_.Wires().back().projection->targetSurfaceName != "__ui_nose_skin"
        || project_.Surfaces().back().sourceWireNames
            != std::vector<std::string>{"__ui_light_plan_circle", "__ui_light_root"}) {
        return fail("create protruding light case from selected front and plate");
    }
    Undo();
    if (project_.Wires().size() != lightCaseWireCount
        || project_.Surfaces().size() != lightCaseSurfaceCount) {
        return fail("undo protruding light case");
    }
    Redo();
    if (project_.Wires().size() != lightCaseWireCount + 1
        || project_.Surfaces().size() != lightCaseSurfaceCount + 1
        || !project_.Wires().back().projection.has_value()) {
        return fail("redo protruding light case");
    }

    jigSurface_->setCurrentText(QStringLiteral("__ui_nose_skin"));
    jigName_->setText(QStringLiteral("__ui_nose_jig"));
    jigSide_->setCurrentIndex(jigSide_->findData(static_cast<int>(JigSide::Negative)));
    jigClearance_->setValue(0.15);
    jigThickness_->setValue(3.0);
    jigMinimumWall_->setValue(1.2);
    CreateSurfaceJig();
    if (project_.Bodies().size() != bodyStart + 1
        || project_.Bodies()[bodyStart].sourceSurfaceName != "__ui_nose_skin"
        || project_.Bodies()[bodyStart].body.Side() != JigSide::Negative
        || std::abs(project_.Bodies()[bodyStart].body.ClearanceMillimeters() - 0.15) > 1.0e-12
        || viewport_->Selection().kind != CadSelectionKind::Body) {
        return fail("create forming jig from loft surface");
    }
    Undo();
    if (project_.Bodies().size() != bodyStart) {
        return fail("undo forming jig creation");
    }
    Redo();
    if (project_.Bodies().size() != bodyStart + 1) {
        return fail("redo forming jig creation");
    }
    UpdateSelection({CadSelectionKind::Body, static_cast<int>(bodyStart)}, true);
    jigThickness_->setValue(4.0);
    UpdateSelectedBody();
    if (std::abs(project_.Bodies()[bodyStart].body.ThicknessMillimeters() - 4.0) > 1.0e-12
        || !jigAnalysisLabel_->text().contains(QStringLiteral("満たします"))) {
        return fail("update forming jig and analyze wall");
    }
    Undo();
    if (std::abs(project_.Bodies()[bodyStart].body.ThicknessMillimeters() - 3.0) > 1.0e-12) {
        return fail("undo forming jig update");
    }
    Redo();

    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart)}, true);
    plateThickness_->setValue(0.7);
    plateVariableThickness_->setChecked(true);
    plateEndThickness_->setValue(1.1);
    plateDirection_->setCurrentIndex(0);
    plateMaterial_->setCurrentIndex(1);
    UpdateSelectedPlate();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.7) > 1.0e-12
        || std::abs(project_.Plates()[plateStart].plate.EndThickness() - 1.1) > 1.0e-12
        || project_.Plates()[plateStart].plate.Direction() != PlateThicknessDirection::Positive
        || project_.Plates()[plateStart].material != "paper") {
        return fail("update selected plate properties");
    }
    Undo();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.5) > 1.0e-12) {
        return fail("undo plate property update");
    }
    Redo();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.7) > 1.0e-12
        || std::abs(project_.Plates()[plateStart].plate.EndThickness() - 1.1) > 1.0e-12) {
        return fail("redo plate property update");
    }

    const std::size_t beforePlateOffsetWire = project_.Wires().size();
    UpdateSelections({
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
        {CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)},
    }, true);
    plateOffsetLayer_->setCurrentIndex(0);
    CreatePlateOffsetWires();
    if (project_.Wires().size() != beforePlateOffsetWire + 1
        || !project_.Wires().back().plateOffset.has_value()
        || project_.Wires().back().plateOffset->plateName != "__ui_nose_plate") {
        return fail("create plate thickness position wire");
    }
    Undo();
    if (project_.Wires().size() != beforePlateOffsetWire) {
        return fail("undo plate thickness position wire");
    }

    UpdateSelections({
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
        {CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)},
    }, true);
    AddSelectedPlateOpenings();
    if (project_.Plates()[plateStart].openingWireNames
        != std::vector<std::string>{projectedLightName}) {
        return fail("add selected plate opening");
    }
    Undo();
    if (!project_.Plates()[plateStart].openingWireNames.empty()) {
        return fail("undo plate opening");
    }
    Redo();
    if (project_.Plates()[plateStart].openingWireNames.size() != 1) {
        return fail("redo plate opening");
    }
    UpdateSelections({
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
        {CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)},
    }, true);
    RemoveSelectedPlateOpenings();
    if (!project_.Plates()[plateStart].openingWireNames.empty()) {
        return fail("remove selected plate opening");
    }
    Undo();
    if (project_.Plates()[plateStart].openingWireNames.size() != 1) {
        return fail("undo removing plate opening");
    }
    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart)}, true);
    if (!infoLabel_->text().contains(QStringLiteral("工作判定"))
        || !infoLabel_->text().contains(QStringLiteral("開口"))) {
        return fail("plate opening and forming information");
    }

    plateSplitAxis_->setCurrentIndex(1);
    plateSplitPosition_->setValue(25.0);
    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart)}, true);
    SplitSelectedPlate();
    if (project_.Plates().size() != plateStart + 2
        || std::abs(project_.Plates()[plateStart].plate.Range().maximumV - 0.25) > 1.0e-12
        || std::abs(project_.Plates()[plateStart + 1].plate.Range().minimumV - 0.25) > 1.0e-12
        || !project_.Plates()[plateStart].openingWireNames.empty()
        || project_.Plates()[plateStart + 1].openingWireNames
            != std::vector<std::string>{projectedLightName}) {
        return fail("split selected plate and assign opening");
    }
    if (std::abs(project_.Plates()[plateStart].plate.EndThickness() - 0.8) > 1.0e-12
        || std::abs(project_.Plates()[plateStart + 1].plate.Thickness() - 0.8) > 1.0e-12
        || std::abs(project_.Plates()[plateStart + 1].plate.EndThickness() - 1.1) > 1.0e-12) {
        return fail("split variable plate thickness profile");
    }
    const std::string firstPieceName = project_.Plates()[plateStart].name;
    const std::string secondPieceName = project_.Plates()[plateStart + 1].name;
    Undo();
    if (project_.Plates().size() != plateStart + 1
        || !project_.Plates()[plateStart].plate.Range().IsFull()) {
        return fail("undo plate split");
    }
    Redo();
    if (project_.Plates().size() != plateStart + 2
        || project_.Plates()[plateStart].name != firstPieceName
        || project_.Plates()[plateStart + 1].name != secondPieceName) {
        return fail("redo plate split");
    }
    try {
        PlateFlatPatternOptions selfTestOptions;
        selfTestOptions.uSegments = 40;
        selfTestOptions.vSegments = 16;
        selfTestOptions.openingSamples = 48;
        const auto firstPattern = BuildPlateFlatPattern(project_, project_.Plates()[plateStart], selfTestOptions);
        const auto secondPattern = BuildPlateFlatPattern(project_, project_.Plates()[plateStart + 1], selfTestOptions);
        PlatePdfOptions pdfOptions;
        pdfOptions.overlapMillimeters = platePdfOverlap_->value();
        const auto pdfLayout = CalculatePlatePdfLayout(secondPattern, pdfOptions);
        std::ostringstream flatSvg;
        std::ostringstream flatDxf;
        WritePlateFlatPatternSvg(flatSvg, secondPattern, selfTestOptions);
        WritePlateFlatPatternDxf(flatDxf, secondPattern);
        if (firstPattern.openings.size() + secondPattern.openings.size() != 1
            || flatSvg.str().find("CUT_OUTER") == std::string::npos
            || flatSvg.str().find("CUT_OPENING") == std::string::npos
            || flatDxf.str().find("$INSUNITS\n70\n4") == std::string::npos
            || pdfLayout.PageCount() < 1) {
            return fail("split plate flat-pattern output");
        }
    } catch (const std::exception&) {
        return fail("build split plate flat patterns");
    }

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)}, true);
    HideSelected();
    if (project_.Wires()[projectedLightIndex].visible) {
        return fail("hide selected projected wire");
    }
    Undo();
    if (!project_.Wires()[projectedLightIndex].visible) {
        return fail("undo hide selected");
    }
    Redo();
    if (project_.Wires()[projectedLightIndex].visible) {
        return fail("redo hide selected");
    }
    ShowAllObjects();
    if (!project_.Wires()[projectedLightIndex].visible || !project_.Surfaces()[surfaceStart].visible) {
        return fail("show all objects");
    }

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount)}, false);
    editWireConstruction_->setChecked(true);
    ApplySelectedEdit();
    if (!project_.Wires()[initialWireCount].metadata.construction) {
        return fail("set construction wire");
    }
    Undo();
    if (project_.Wires()[initialWireCount].metadata.construction) {
        return fail("undo construction wire");
    }
    Redo();
    if (!project_.Wires()[initialWireCount].metadata.construction) {
        return fail("redo construction wire");
    }

    std::ostringstream directDrawingScript;
    WriteProjectScript(directDrawingScript, project_);
    std::istringstream directDrawingInput(directDrawingScript.str());
    const Project reloadedProject = LoadProjectScript(directDrawingInput, "direct-editing-self-test");
    if (reloadedProject.Wires().size() != project_.Wires().size()
        || reloadedProject.Wires()[directStart + 5].wire.Kind() != WireKind::CubicBezier
        || reloadedProject.Wires()[constrainedLineIndex].metadata.lineConstraints.lengthMillimeters != 8.0
        || reloadedProject.Wires()[constrainedLineIndex].metadata.lineConstraints.angleDegrees != 0.0
        || !reloadedProject.Wires()[initialWireCount].metadata.construction
        || reloadedProject.Wires()[mirrorStart].metadata.sourcePlaneName != drawingPlaneName
        || reloadedProject.Wires()[referenceMirrorStart].wire.Kind() != WireKind::Circle
        || reloadedProject.Wires()[splitStart].wire.Kind() != WireKind::Polyline
        || reloadedProject.Surfaces().size() != project_.Surfaces().size()
        || reloadedProject.Surfaces()[surfaceStart].surface.Kind() != SurfaceKind::Loft
        || reloadedProject.Plates().size() != project_.Plates().size()
        || reloadedProject.Plates()[plateStart].sourceSurfaceName != "__ui_nose_skin"
        || std::abs(reloadedProject.Plates()[plateStart].plate.EndThickness() - 0.8) > 1.0e-12
        || std::abs(reloadedProject.Plates()[plateStart].plate.Range().maximumV - 0.25) > 1.0e-12
        || reloadedProject.Plates()[plateStart + 1].openingWireNames
            != std::vector<std::string>{projectedLightName}
        || reloadedProject.Bodies().size() != project_.Bodies().size()
        || reloadedProject.Bodies()[bodyStart].sourceSurfaceName != "__ui_nose_skin"
        || std::abs(reloadedProject.Bodies()[bodyStart].body.ThicknessMillimeters() - 4.0) > 1.0e-12
        || !reloadedProject.Wires()[projectedLightIndex].projection.has_value()
        || !kachakacha::geometry::AlmostEqual(reloadedProject.Wires()[meetStart].wire.End(), expectedIntersection)) {
        return fail("save and reload direct editing");
    }

    SetViewportTool(ViewportTool::Select);
    viewport_->SetIsometricView();
    viewport_->FitAll();
    toolsTabs_->setCurrentIndex(6);
    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart + 1)}, true);
    if (!plateFlatPatternSummary_->text().contains(QStringLiteral("PDF"))) {
        return fail("plate PDF output summary");
    }
    try {
        const auto expectedGuide = BuildPlateAssemblyGuide(
            project_, project_.Plates()[plateStart + 1], PlateFlatPatternOptionsFromUi());
        if (!plateAssemblyGuidePreview_->isChecked()
            || viewport_->PlateAssemblyFoldGuideCount() != expectedGuide.foldLines.size()
            || viewport_->PlateAssemblyReliefGuideCount()
                != expectedGuide.reliefCuts.size() + expectedGuide.splitLines.size()) {
            return fail("assembled fold and relief preview");
        }
        plateAssemblyGuidePreview_->setChecked(false);
        if (viewport_->PlateAssemblyFoldGuideCount() != 0
            || viewport_->PlateAssemblyReliefGuideCount() != 0) {
            return fail("hide assembled fold and relief preview");
        }
        plateAssemblyGuidePreview_->setChecked(true);
    } catch (const std::exception&) {
        return fail("build assembled fold and relief preview");
    }
    if (auto* outputScrollArea = qobject_cast<QScrollArea*>(toolsTabs_->widget(6))) {
        outputScrollArea->widget()->adjustSize();
        QApplication::processEvents();
        const auto buttons = outputScrollArea->findChildren<QPushButton*>();
        const auto pdfButton = std::find_if(buttons.begin(), buttons.end(), [](const QPushButton* button) {
            return button->property("platePdfAction").toBool();
        });
        const auto flatModelButton = std::find_if(buttons.begin(), buttons.end(), [](const QPushButton* button) {
            return button->property("plateFlatPatternModelAction").toBool();
        });
        if (pdfButton == buttons.end()) {
            return fail("plate PDF output button");
        }
        if (flatModelButton == buttons.end() || plateFlatPatternAutoRelief_ == nullptr
            || plateAssemblyGuidePreview_ == nullptr
            || plateFlatPatternPlane_ == nullptr || plateFlatPatternPlane_->count() == 0
            || plateFlatPatternCutWidth_ == nullptr) {
            return fail("flat-pattern wire and 3D plate controls");
        }
        QPushButton* pdfButtonPointer = *pdfButton;
        QTimer::singleShot(0, outputScrollArea, [outputScrollArea, pdfButtonPointer] {
            outputScrollArea->ensureWidgetVisible(pdfButtonPointer, 0, 12);
        });
    }
    toolsTabs_->setCurrentIndex(3);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 2)},
    }, true);
    wireOffsetDistance_->setValue(1.25);
    wireOffsetSide_->setCurrentIndex(0);
    UpdateWireOffsetPreview();
    if (viewport_->WireOffsetPreviewCount() != 2) {
        return fail("final offset preview state");
    }

    measurementMode_->setCurrentIndex(2);
    UpdateMeasurement({
        {MeasurementPickKind::Wire, static_cast<int>(directStart + 2), project_.Wires()[directStart + 2].wire.Start(), 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("半径"))) {
        return fail("measure circle radius");
    }
    const int radiusMetric = measurementMetric_->findData(
        static_cast<int>(ReferenceDimensionKind::WireRadius));
    if (radiusMetric < 0 || measurementMetric_->findData(
            static_cast<int>(ReferenceDimensionKind::WireLength)) < 0) {
        return fail("offer persistent circle dimensions");
    }
    measurementMetric_->setCurrentIndex(radiusMetric);
    measurementName_->setText(QStringLiteral("__ui_radius_dimension"));
    SaveCurrentMeasurement();
    if (project_.ReferenceDimensions().size() != initialDimensionCount + 1
        || referenceDimensionList_->count() != static_cast<int>(initialDimensionCount + 1)
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount + 1
        || std::abs(project_.EvaluateReferenceDimension("__ui_radius_dimension").value
            - project_.Wires()[directStart + 2].wire.ArcData().radius) > 1.0e-9) {
        return fail("save persistent radius dimension");
    }
    Undo();
    if (project_.ReferenceDimensions().size() != initialDimensionCount) {
        return fail("undo persistent dimension");
    }
    Redo();
    if (project_.ReferenceDimensions().size() != initialDimensionCount + 1
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount + 1) {
        return fail("redo persistent dimension");
    }
    for (int row = 0; row < referenceDimensionList_->count(); ++row) {
        QListWidgetItem* item = referenceDimensionList_->item(row);
        if (item->data(kDimensionNameRole).toString() == QStringLiteral("__ui_radius_dimension")) {
            item->setCheckState(Qt::Unchecked);
            break;
        }
    }
    if (project_.ReferenceDimensions().back().visible
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount) {
        return fail("hide persistent dimension");
    }
    Undo();
    if (!project_.ReferenceDimensions().back().visible
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount + 1) {
        return fail("undo hidden persistent dimension");
    }
    UpdateMeasurement({
        {MeasurementPickKind::Wire, static_cast<int>(directStart), project_.Wires()[directStart].wire.Start(), 0.0},
        {MeasurementPickKind::Wire, static_cast<int>(meetStart), project_.Wires()[meetStart].wire.Start(), 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("最短距離"))
        || !measurementResultLabel_->text().contains(QStringLiteral("最小交角"))) {
        return fail("measure wire relation");
    }
    measurementMode_->setCurrentIndex(0);
    UpdateMeasurement({
        {MeasurementPickKind::Point, -1, {0.0, 0.0, 0.0}, 0.0},
        {MeasurementPickKind::Point, -1, {3.0, 4.0, 12.0}, 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("13.000 mm"))
        || !measurementResultLabel_->text().contains(QStringLiteral("XY投影"))
        || measurementMetric_->findData(static_cast<int>(ReferenceDimensionKind::PointDistance)) < 0) {
        return fail("measure two points");
    }
    measurementMode_->setCurrentIndex(1);
    UpdateMeasurement({
        {MeasurementPickKind::Point, -1, {0.0, 0.0, 0.0}, 0.0},
        {MeasurementPickKind::Point, -1, {3.0, 0.0, 0.0}, 0.0},
        {MeasurementPickKind::Point, -1, {0.0, 0.0, 4.0}, 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("3D角度  90.000°"))) {
        return fail("measure three point 3D angle");
    }
    measurementMode_->setCurrentIndex(2);
    UpdateMeasurement({
        {MeasurementPickKind::Wire, static_cast<int>(directStart + 2),
            project_.Wires()[directStart + 2].wire.Start(), 0.0},
        {MeasurementPickKind::Wire, static_cast<int>(directStart),
            project_.Wires()[directStart].wire.Start(), 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("曲率法線"))) {
        return fail("measure curve normal against line");
    }
    for (int row = 0; row < referenceDimensionList_->count(); ++row) {
        if (referenceDimensionList_->item(row)->data(kDimensionNameRole).toString()
            == QStringLiteral("__ui_radius_dimension")) {
            referenceDimensionList_->setCurrentRow(row);
            break;
        }
    }
    DeleteSelectedReferenceDimension();
    if (project_.ReferenceDimensions().size() != initialDimensionCount
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount) {
        return fail("delete persistent dimension");
    }

    const std::size_t coincidenceCount = project_.CoincidentConstraints().size();
    ApplyEndpointCoincidence(
        {static_cast<int>(directStart), kachakacha::model::WireEndpoint::End, project_.Wires()[directStart].wire.End()},
        {static_cast<int>(meetStart), kachakacha::model::WireEndpoint::Start, project_.Wires()[meetStart].wire.Start()});
    if (project_.CoincidentConstraints().size() != coincidenceCount + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End(), project_.Wires()[meetStart].wire.Start(), 1.0e-8)) {
        return fail("apply endpoint coincidence");
    }
    Undo();
    if (project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("undo endpoint coincidence");
    }
    Redo();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(meetStart)}, true);
    RemoveSelectedCoincidences();
    if (project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("remove endpoint coincidence");
    }

    const std::size_t tangentCount = project_.TangentConstraints().size();
    ApplyEndpointTangency(
        {static_cast<int>(directStart), kachakacha::model::WireEndpoint::End, project_.Wires()[directStart].wire.End()},
        {static_cast<int>(directStart + 5), kachakacha::model::WireEndpoint::Start,
            project_.Wires()[directStart + 5].wire.Start()});
    if (project_.TangentConstraints().size() != tangentCount + 1
        || project_.CoincidentConstraints().size() != coincidenceCount + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End(), project_.Wires()[directStart + 5].wire.Start(), 1.0e-8)
        || kachakacha::geometry::Dot(
            MeasureWireTangent(project_.Wires()[directStart].wire, 1.0),
            MeasureWireTangent(project_.Wires()[directStart + 5].wire, 0.0)) < 0.999999) {
        return fail("apply endpoint tangency");
    }
    Undo();
    if (project_.TangentConstraints().size() != tangentCount
        || project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("undo endpoint tangency");
    }
    Redo();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(directStart + 5)}, true);
    RemoveSelectedTangencies();
    if (project_.TangentConstraints().size() != tangentCount
        || project_.CoincidentConstraints().size() != coincidenceCount + 1) {
        return fail("remove endpoint tangency only");
    }
    ApplyEndpointCurvature(
        {static_cast<int>(directStart), kachakacha::model::WireEndpoint::End, project_.Wires()[directStart].wire.End()},
        {static_cast<int>(directStart + 5), kachakacha::model::WireEndpoint::Start,
            project_.Wires()[directStart + 5].wire.Start()});
    const auto& curvatureConstraint = project_.TangentConstraints().back();
    const auto& curvaturePoints = project_.Wires()[directStart + 5].wire.ControlPoints();
    const Vector3 curvatureTangent = (curvaturePoints[1] - curvaturePoints[0]).Normalized();
    const Vector3 curvatureSecondDerivative =
        (curvaturePoints[2] - curvaturePoints[1] * 2.0 + curvaturePoints[0]) * 6.0;
    const Vector3 normalSecondDerivative = curvatureSecondDerivative
        - curvatureTangent * kachakacha::geometry::Dot(curvatureSecondDerivative, curvatureTangent);
    if (project_.TangentConstraints().size() != tangentCount + 1
        || curvatureConstraint.continuity != WireContinuity::G2Curvature
        || normalSecondDerivative.Length() > 1.0e-8) {
        return fail("apply endpoint curvature");
    }
    Undo();
    if (project_.TangentConstraints().size() != tangentCount) {
        return fail("undo endpoint curvature");
    }
    Redo();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(directStart + 5)}, true);
    RemoveSelectedTangencies();
    if (project_.TangentConstraints().size() != tangentCount) {
        return fail("remove endpoint curvature only");
    }
    RemoveSelectedCoincidences();
    if (project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("remove tangent endpoint coincidence");
    }

    modelFilter_->setText(QStringLiteral("__ui_nose_skin"));
    bool matchingTreeItemVisible = false;
    QTreeWidgetItemIterator filteredIterator(modelTree_);
    while (*filteredIterator) {
        QTreeWidgetItem* item = *filteredIterator;
        if (item->text(0) == QStringLiteral("__ui_nose_skin") && !item->isHidden()) {
            matchingTreeItemVisible = true;
            break;
        }
        ++filteredIterator;
    }
    if (!matchingTreeItemVisible) {
        return fail("filter model tree by object name");
    }
    modelFilter_->clear();

    const std::size_t historySizeBeforeDisplayMode = undoStack_.size();
    UpdateSelections({
        {CadSelectionKind::Surface, static_cast<int>(surfaceStart)},
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
    }, true);
    SetDisplayMode(ViewportDisplayMode::IsolatedSelection);
    if (viewport_->DisplayMode() != ViewportDisplayMode::IsolatedSelection) {
        return fail("isolate selected model objects");
    }
    SetDisplayMode(ViewportDisplayMode::FinishedModel);
    if (viewport_->DisplayMode() != ViewportDisplayMode::FinishedModel
        || viewport_->Selections().size() != 1
        || viewport_->Selections().front().kind != CadSelectionKind::Plate) {
        return fail("show completed plates and bodies only");
    }
    SetDisplayMode(ViewportDisplayMode::Design);
    if (viewport_->DisplayMode() != ViewportDisplayMode::Design
        || undoStack_.size() != historySizeBeforeDisplayMode) {
        return fail("restore design display without history change");
    }

    toolsTabs_->setCurrentIndex(8);
    QApplication::processEvents();
    return true;
}

WorkPlane MainWindow::WorkPlaneFromInputs() const
{
    std::optional<WorkPlane> basePlane;
    switch (planeMethod_->currentIndex()) {
    case 0:
        if (standardPlane_->currentIndex() == 0) {
            basePlane = WorkPlane::FromPointNormal({}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
        } else if (standardPlane_->currentIndex() == 1) {
            basePlane = WorkPlane::FromPointNormal({}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0});
        } else {
            basePlane = WorkPlane::FromPointNormal({}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        }
        break;
    case 1:
        basePlane = WorkPlane::FromPointNormal(ReadVector3(pointNormalOrigin_), ReadVector3(pointNormalDirection_), ReadVector3(pointNormalUAxis_));
        break;
    case 2:
        basePlane = WorkPlane::FromThreePoints(ReadVector3(threePointA_), ReadVector3(threePointB_), ReadVector3(threePointC_));
        break;
    case 3:
        basePlane = project_.FindWorkPlane(ToName(offsetSourcePlane_->currentText()));
        if (!basePlane.has_value()) {
            throw std::invalid_argument("基準平面を選択してください。");
        }
        break;
    case 4:
        basePlane = project_.FindWorkPlane(ToName(rotateSourcePlane_->currentText()));
        if (!basePlane.has_value()) {
            throw std::invalid_argument("基準平面を選択してください。");
        }
        basePlane = basePlane->RotateAroundAxis(
            ReadVector3(rotateAxisPoint_),
            ReadVector3(rotateAxisDirection_),
            rotateAngle_->value() * kPi / 180.0);
        break;
    default:
        throw std::invalid_argument("作業平面の作り方を選択してください。");
    }

    WorkPlane finalPlane = basePlane->Offset(planeOffset_->value());
    if (std::abs(planeTilt_->value()) > 1.0e-12) {
        finalPlane = finalPlane.RotateAroundAxis(finalPlane.Origin(), finalPlane.UAxis(), planeTilt_->value() * kPi / 180.0);
    }
    return finalPlane;
}

void MainWindow::AlignViewportFromPlaneInputs()
{
    try {
        viewport_->AlignToWorkPlane(WorkPlaneFromInputs());
        statusBar()->showMessage(QStringLiteral("作業平面を作らず、この向きへ正対しました"), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("この向きで表示できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::AddWorkPlane()
{
    try {
        ValidateObjectName(planeName_->text());
        const WorkPlane finalPlane = WorkPlaneFromInputs();
        const std::string newName = ToName(planeName_->text());
        if (project_.FindWorkPlane(newName).has_value()) {
            throw std::invalid_argument("同じ名前の作業平面があります。");
        }
        RecordUndo();
        project_.AddWorkPlane(newName, finalPlane);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection({CadSelectionKind::WorkPlane, static_cast<int>(project_.WorkPlanes().size()) - 1}, true);
        planeName_->setText(SuggestedPlaneName());
        statusBar()->showMessage(QStringLiteral("作業平面を追加しました"), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("作業平面を作成できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::AddWire()
{
    try {
        ValidateObjectName(wireName_->text());
        std::optional<Wire> wire;
        WireMetadata metadata;
        const int kind = wireKind_->currentIndex();
        if (kind == 0) {
            wire = Wire::Line(ReadVector3(lineStart_), ReadVector3(lineEnd_));
        } else if (kind == 1) {
            wire = Wire::CubicBezier(ReadVector3(bezierStart_), ReadVector3(bezierControl1_), ReadVector3(bezierControl2_), ReadVector3(bezierEnd_));
        } else {
            const std::string planeName = ToName(wirePlane_->currentText());
            const std::optional<WorkPlane> plane = project_.FindWorkPlane(planeName);
            if (!plane.has_value()) {
                throw std::invalid_argument("作図する作業平面を選択してください。");
            }
            const Sketch sketch(*plane);
            metadata.sourcePlaneName = planeName;
            switch (kind) {
            case 2:
                wire = sketch.MakeLine(ReadVector2(sketchLineStart_), ReadVector2(sketchLineEnd_));
                break;
            case 3:
                wire = sketch.MakeCircle(ReadVector2(circleCenter_), circleRadius_->value());
                break;
            case 4:
                wire = sketch.MakeCircularArc(ReadVector2(arcCenter_), arcRadius_->value(), arcStartAngle_->value() * kPi / 180.0, arcSweepAngle_->value() * kPi / 180.0);
                break;
            case 5:
                wire = sketch.MakeCubicBezier(ReadVector2(sketchBezierStart_), ReadVector2(sketchBezierControl1_), ReadVector2(sketchBezierControl2_), ReadVector2(sketchBezierEnd_));
                break;
            default:
                break;
            }
        }

        switch (wirePolicy_->currentIndex()) {
        case 0:
            metadata.planePolicy = WirePlanePolicy::Free3D;
            break;
        case 1:
            metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
            break;
        case 2:
            metadata.planePolicy = WirePlanePolicy::LockedToPlane;
            break;
        }
        const std::string newName = ToName(wireName_->text());
        for (const auto& existingWire : project_.Wires()) {
            if (existingWire.name == newName) {
                throw std::invalid_argument("同じ名前のワイヤーがあります。");
            }
        }
        RecordUndo();
        project_.AddWire(newName, *wire, metadata);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection({CadSelectionKind::Wire, static_cast<int>(project_.Wires().size()) - 1}, true);
        wireName_->setText(SuggestedWireName());
        statusBar()->showMessage(QStringLiteral("ワイヤーを追加しました"), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("ワイヤーを作成できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::ApplySelectedEdit()
{
    const CadSelection selection = viewport_->Selection();
    try {
        if (selection.kind == CadSelectionKind::WorkPlane
            && selection.index >= 0
            && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
            const auto& namedPlane = project_.WorkPlanes()[selection.index];
            const std::string planeName = namedPlane.name;
            const WorkPlane replacement = WorkPlane::FromOriginAxes(
                ReadVector3(editPlaneOrigin_),
                ReadVector3(editPlaneUAxis_),
                ReadVector3(editPlaneNormal_));
            RecordUndo();
            project_.UpdateWorkPlane(planeName, replacement);
        } else if (selection.kind == CadSelectionKind::Wire
            && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            const auto& namedWire = project_.Wires()[selection.index];
            const std::string wireName = namedWire.name;
            std::optional<Wire> replacement;
            switch (namedWire.wire.Kind()) {
            case WireKind::Line:
                replacement = Wire::Line(ReadTablePoint(editWirePointTable_, 0), ReadTablePoint(editWirePointTable_, 1));
                break;
            case WireKind::Polyline: {
                std::vector<Vector3> points;
                points.reserve(editWirePointTable_->rowCount());
                for (int row = 0; row < editWirePointTable_->rowCount(); ++row) {
                    points.push_back(ReadTablePoint(editWirePointTable_, row));
                }
                replacement = Wire::Polyline(std::move(points));
                break;
            }
            case WireKind::CubicBezier:
                replacement = Wire::CubicBezier(
                    ReadTablePoint(editWirePointTable_, 0),
                    ReadTablePoint(editWirePointTable_, 1),
                    ReadTablePoint(editWirePointTable_, 2),
                    ReadTablePoint(editWirePointTable_, 3));
                break;
            case WireKind::CubicBSpline: {
                std::vector<Vector3> points;
                points.reserve(editWirePointTable_->rowCount());
                for (int row = 0; row < editWirePointTable_->rowCount(); ++row) {
                    points.push_back(ReadTablePoint(editWirePointTable_, row));
                }
                replacement = Wire::CubicBSplineWithKnots(
                    std::move(points), namedWire.wire.BSplineKnots());
                break;
            }
            case WireKind::Circle:
                replacement = Wire::Circle(
                    ReadVector3(editArcCenter_),
                    ReadVector3(editArcUAxis_),
                    ReadVector3(editArcVAxis_),
                    editArcRadius_->value());
                break;
            case WireKind::CircularArc:
                replacement = Wire::CircularArc(
                    ReadVector3(editArcCenter_),
                    ReadVector3(editArcUAxis_),
                    ReadVector3(editArcVAxis_),
                    editArcRadius_->value(),
                    editArcStartAngle_->value() * kPi / 180.0,
                    editArcSweepAngle_->value() * kPi / 180.0);
                break;
            }

            WireMetadata metadata = namedWire.metadata;
            if (editWireSourcePlane_->currentIndex() > 0) {
                metadata.sourcePlaneName = ToName(editWireSourcePlane_->currentText());
                if (!project_.FindWorkPlane(*metadata.sourcePlaneName).has_value()) {
                    throw std::invalid_argument("作成元平面を選択してください。");
                }
            } else {
                metadata.sourcePlaneName.reset();
            }
            metadata.planePolicy = static_cast<WirePlanePolicy>(editWirePolicy_->currentIndex());
            metadata.construction = editWireConstruction_->isChecked();
            metadata.lineConstraints = {};
            metadata.curveConstraints = {};
            if (namedWire.wire.Kind() == WireKind::Line) {
                if (editWireLockLength_->isChecked()) {
                    metadata.lineConstraints.lengthMillimeters = editWireConstraintLength_->value();
                }
                if (editWireLockAngle_->isChecked()) {
                    if (!metadata.sourcePlaneName.has_value()) {
                        throw std::invalid_argument("角度を固定する作業平面を選択してください。");
                    }
                    metadata.lineConstraints.angleDegrees = editWireConstraintAngle_->value();
                }
            } else if ((namedWire.wire.Kind() == WireKind::Circle
                           || namedWire.wire.Kind() == WireKind::CircularArc)
                && editWireLockRadius_->isChecked()) {
                metadata.curveConstraints.radiusMillimeters = editArcRadius_->value();
            }

            RecordUndo();
            project_.UpdateWireAndMetadata(wireName, *replacement, std::move(metadata));
        } else {
            statusBar()->showMessage(QStringLiteral("編集する項目を選択してください"), 2500);
            return;
        }

        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(selection, true);
        statusBar()->showMessage(QStringLiteral("数値変更を適用しました"), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("変更を適用できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::ApplyLineChamfer()
{
    try {
        ValidateObjectName(chamferName_->text());
        if (chamferFirstWire_->currentIndex() < 0 || chamferSecondWire_->currentIndex() < 0) {
            throw std::invalid_argument("面取りする2本の直線を選択してください。");
        }

        const int firstIndex = chamferFirstWire_->currentData().toInt();
        const int secondIndex = chamferSecondWire_->currentData().toInt();
        if (firstIndex == secondIndex) {
            throw std::invalid_argument("異なる2本の直線を選択してください。");
        }
        if (firstIndex < 0 || secondIndex < 0
            || firstIndex >= static_cast<int>(project_.Wires().size())
            || secondIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("選択した直線が見つかりません。");
        }

        const auto first = project_.Wires()[firstIndex];
        const auto second = project_.Wires()[secondIndex];
        const std::string chamferName = ToName(chamferName_->text());
        for (const auto& existingWire : project_.Wires()) {
            if (existingWire.name == chamferName) {
                throw std::invalid_argument("同じ名前のワイヤーがあります。");
            }
        }

        const auto result = ChamferIntersectingLines(
            first.wire,
            static_cast<RetainedLineEnd>(chamferFirstBranch_->currentIndex()),
            chamferFirstDistance_->value(),
            second.wire,
            static_cast<RetainedLineEnd>(chamferSecondBranch_->currentIndex()),
            chamferSecondDistance_->value());

        WireMetadata chamferMetadata;
        if (first.metadata.sourcePlaneName == second.metadata.sourcePlaneName
            && first.metadata.planePolicy == second.metadata.planePolicy) {
            chamferMetadata = first.metadata;
        }
        chamferMetadata.lineConstraints = {};

        RecordUndo();
        project_.UpdateWireAndMetadata(
            first.name,
            result.trimmedFirst,
            RetargetLineConstraints(project_, first.metadata, result.trimmedFirst, true));
        project_.UpdateWireAndMetadata(
            second.name,
            result.trimmedSecond,
            RetargetLineConstraints(project_, second.metadata, result.trimmedSecond, true));
        project_.AddWire(chamferName, result.chamfer, chamferMetadata);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection({CadSelectionKind::Wire, static_cast<int>(project_.Wires().size()) - 1}, true);
        chamferName_->setText(SuggestedChamferName());
        statusBar()->showMessage(QStringLiteral("C面取りを作成しました"), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("C面取りを作成できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::ApplyLineFillet()
{
    try {
        ValidateObjectName(chamferName_->text());
        if (chamferFirstWire_->currentIndex() < 0 || chamferSecondWire_->currentIndex() < 0) {
            throw std::invalid_argument("R丸めする2本の直線を選択してください。");
        }

        const int firstIndex = chamferFirstWire_->currentData().toInt();
        const int secondIndex = chamferSecondWire_->currentData().toInt();
        if (firstIndex == secondIndex) {
            throw std::invalid_argument("異なる2本の直線を選択してください。");
        }
        if (firstIndex < 0 || secondIndex < 0
            || firstIndex >= static_cast<int>(project_.Wires().size())
            || secondIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("選択した直線が見つかりません。");
        }

        const auto first = project_.Wires()[firstIndex];
        const auto second = project_.Wires()[secondIndex];
        const std::string filletName = ToName(chamferName_->text());
        for (const auto& existingWire : project_.Wires()) {
            if (existingWire.name == filletName) {
                throw std::invalid_argument("同じ名前のワイヤーがあります。");
            }
        }

        const auto result = FilletIntersectingLines(
            first.wire,
            static_cast<RetainedLineEnd>(chamferFirstBranch_->currentIndex()),
            second.wire,
            static_cast<RetainedLineEnd>(chamferSecondBranch_->currentIndex()),
            filletRadius_->value());

        WireMetadata filletMetadata;
        if (first.metadata.sourcePlaneName == second.metadata.sourcePlaneName
            && first.metadata.planePolicy == second.metadata.planePolicy) {
            filletMetadata = first.metadata;
        }
        filletMetadata.lineConstraints = {};

        RecordUndo();
        project_.UpdateWireAndMetadata(
            first.name,
            result.trimmedFirst,
            RetargetLineConstraints(project_, first.metadata, result.trimmedFirst, true));
        project_.UpdateWireAndMetadata(
            second.name,
            result.trimmedSecond,
            RetargetLineConstraints(project_, second.metadata, result.trimmedSecond, true));
        project_.AddWire(filletName, result.fillet, filletMetadata);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection({CadSelectionKind::Wire, static_cast<int>(project_.Wires().size()) - 1}, true);
        chamferName_->setText(SuggestedFilletName());
        statusBar()->showMessage(QStringLiteral("R丸めを作成しました"), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("R丸めを作成できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::Undo()
{
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(project_);
    project_ = std::move(undoStack_.back());
    undoStack_.pop_back();
    MarkModified();
    RefreshModelViews(false);
    UpdateHistoryActions();
    statusBar()->showMessage(QStringLiteral("元に戻しました"), 2000);
}

void MainWindow::Redo()
{
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(project_);
    project_ = std::move(redoStack_.back());
    redoStack_.pop_back();
    MarkModified();
    RefreshModelViews(false);
    UpdateHistoryActions();
    statusBar()->showMessage(QStringLiteral("やり直しました"), 2000);
}

void MainWindow::RecordUndo()
{
    undoStack_.push_back(project_);
    if (undoStack_.size() > 100) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
    UpdateHistoryActions();
}

void MainWindow::UpdateHistoryActions()
{
    if (undoAction_ != nullptr) {
        undoAction_->setEnabled(!undoStack_.empty());
    }
    if (redoAction_ != nullptr) {
        redoAction_->setEnabled(!redoStack_.empty());
    }
}

void MainWindow::DeleteSelection()
{
    const CadSelection selection = viewport_->Selection();
    if (selection.kind == CadSelectionKind::None) {
        statusBar()->showMessage(QStringLiteral("削除する項目を選択してください"), 2500);
        return;
    }

    QString name;
    QString detail;
    if (selection.kind == CadSelectionKind::WorkPlane && selection.index >= 0
        && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
        name = ToQString(project_.WorkPlanes()[selection.index].name);
        detail = QStringLiteral("作業平面を削除します。平面から作ったワイヤーは3D形状として残ります。");
    } else if (selection.kind == CadSelectionKind::Point && selection.index >= 0
        && selection.index < static_cast<int>(project_.Points().size())) {
        name = ToQString(project_.Points()[selection.index].name);
        detail = QStringLiteral("作図点を削除します。ワイヤーなどの形状は変わりません。");
    } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
        && selection.index < static_cast<int>(project_.Wires().size())) {
        name = ToQString(project_.Wires()[selection.index].name);
        detail = project_.Wires()[selection.index].projection.has_value()
            ? QStringLiteral("面へ投影したワイヤーを削除します。元の平面図は残ります。")
            : QStringLiteral("ワイヤーを削除します。");
    } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
        && selection.index < static_cast<int>(project_.Surfaces().size())) {
        name = ToQString(project_.Surfaces()[selection.index].name);
        detail = QStringLiteral("面を削除します。先に面上の投影ワイヤーと板材を削除してください。");
    } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
        && selection.index < static_cast<int>(project_.Plates().size())) {
        name = ToQString(project_.Plates()[selection.index].name);
        detail = QStringLiteral("板材を削除します。元の面は残ります。");
    } else if (selection.kind == CadSelectionKind::Body && selection.index >= 0
        && selection.index < static_cast<int>(project_.Bodies().size())) {
        name = ToQString(project_.Bodies()[selection.index].name);
        detail = QStringLiteral("成形治具を削除します。元の面は残ります。");
    } else {
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("削除"), QStringLiteral("%1\n\n%2").arg(name, detail)) != QMessageBox::Yes) {
        return;
    }
    try {
        Project candidate = project_;
        if (selection.kind == CadSelectionKind::WorkPlane) {
            candidate.RemoveWorkPlane(ToName(name));
        } else if (selection.kind == CadSelectionKind::Point) {
            candidate.RemovePoint(ToName(name));
        } else if (selection.kind == CadSelectionKind::Wire) {
            candidate.RemoveWire(ToName(name));
        } else if (selection.kind == CadSelectionKind::Surface) {
            candidate.RemoveSurface(ToName(name));
        } else if (selection.kind == CadSelectionKind::Plate) {
            candidate.RemovePlate(ToName(name));
        } else {
            candidate.RemoveBody(ToName(name));
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(QStringLiteral("削除しました: %1").arg(name), 3000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::HideSelected()
{
    const std::vector<CadSelection> selections = viewport_->Selections();
    bool hasVisibleSelection = false;
    for (const CadSelection& selection : selections) {
        if (selection.kind == CadSelectionKind::WorkPlane && selection.index >= 0
            && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.WorkPlanes()[selection.index].visible;
        } else if (selection.kind == CadSelectionKind::Point && selection.index >= 0
            && selection.index < static_cast<int>(project_.Points().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Points()[selection.index].visible;
        } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Wires()[selection.index].visible;
        } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
            && selection.index < static_cast<int>(project_.Surfaces().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Surfaces()[selection.index].visible;
        } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Plates()[selection.index].visible;
        } else if (selection.kind == CadSelectionKind::Body && selection.index >= 0
            && selection.index < static_cast<int>(project_.Bodies().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Bodies()[selection.index].visible;
        }
    }
    if (!hasVisibleSelection) {
        statusBar()->showMessage(QStringLiteral("隠す対象を3D画面またはモデル一覧で選択してください"), 3000);
        return;
    }

    RecordUndo();
    for (const CadSelection& selection : selections) {
        if (selection.kind == CadSelectionKind::WorkPlane && selection.index >= 0
            && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
            project_.SetWorkPlaneVisible(project_.WorkPlanes()[selection.index].name, false);
        } else if (selection.kind == CadSelectionKind::Point && selection.index >= 0
            && selection.index < static_cast<int>(project_.Points().size())) {
            project_.SetPointVisible(project_.Points()[selection.index].name, false);
        } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            project_.SetWireVisible(project_.Wires()[selection.index].name, false);
        } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
            && selection.index < static_cast<int>(project_.Surfaces().size())) {
            project_.SetSurfaceVisible(project_.Surfaces()[selection.index].name, false);
        } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            project_.SetPlateVisible(project_.Plates()[selection.index].name, false);
        } else if (selection.kind == CadSelectionKind::Body && selection.index >= 0
            && selection.index < static_cast<int>(project_.Bodies().size())) {
            project_.SetBodyVisible(project_.Bodies()[selection.index].name, false);
        }
    }
    MarkModified();
    RefreshModelViews(false);
    statusBar()->showMessage(QStringLiteral("選択した%1個を隠しました").arg(selections.size()), 2500);
}

void MainWindow::ShowAllObjects()
{
    const bool hasHiddenObjects = std::any_of(project_.WorkPlanes().begin(), project_.WorkPlanes().end(), [](const auto& plane) {
        return !plane.visible;
    }) || std::any_of(project_.Points().begin(), project_.Points().end(), [](const auto& point) {
        return !point.visible;
    }) || std::any_of(project_.Wires().begin(), project_.Wires().end(), [](const auto& wire) {
        return !wire.visible;
    }) || std::any_of(project_.Surfaces().begin(), project_.Surfaces().end(), [](const auto& surface) {
        return !surface.visible;
    }) || std::any_of(project_.Plates().begin(), project_.Plates().end(), [](const auto& plate) {
        return !plate.visible;
    }) || std::any_of(project_.Bodies().begin(), project_.Bodies().end(), [](const auto& body) {
        return !body.visible;
    }) || std::any_of(project_.ReferenceDimensions().begin(), project_.ReferenceDimensions().end(), [](const auto& dimension) {
        return !dimension.visible;
    });
    if (!hasHiddenObjects) {
        statusBar()->showMessage(QStringLiteral("すべて表示されています"), 2000);
        return;
    }

    RecordUndo();
    for (const auto& plane : project_.WorkPlanes()) {
        project_.SetWorkPlaneVisible(plane.name, true);
    }
    for (const auto& point : project_.Points()) {
        project_.SetPointVisible(point.name, true);
    }
    for (const auto& wire : project_.Wires()) {
        project_.SetWireVisible(wire.name, true);
    }
    for (const auto& surface : project_.Surfaces()) {
        project_.SetSurfaceVisible(surface.name, true);
    }
    for (const auto& plate : project_.Plates()) {
        project_.SetPlateVisible(plate.name, true);
    }
    for (const auto& body : project_.Bodies()) {
        project_.SetBodyVisible(body.name, true);
    }
    for (const auto& dimension : project_.ReferenceDimensions()) {
        project_.SetReferenceDimensionVisible(dimension.name, true);
    }
    MarkModified();
    RefreshModelViews(false);
    statusBar()->showMessage(QStringLiteral("すべて再表示しました"), 2500);
}

void MainWindow::ApplyModelTreeFilter()
{
    if (modelFilter_ == nullptr || modelTree_ == nullptr) {
        return;
    }

    const QString term = modelFilter_->text().trimmed();
    for (int rootIndex = 0; rootIndex < modelTree_->topLevelItemCount(); ++rootIndex) {
        QTreeWidgetItem* root = modelTree_->topLevelItem(rootIndex);
        const bool rootMatches = term.isEmpty()
            || root->text(0).contains(term, Qt::CaseInsensitive);
        bool childMatches = false;
        for (int childIndex = 0; childIndex < root->childCount(); ++childIndex) {
            QTreeWidgetItem* child = root->child(childIndex);
            const bool matches = term.isEmpty() || rootMatches
                || child->text(0).contains(term, Qt::CaseInsensitive);
            child->setHidden(!matches);
            childMatches = childMatches || matches;
        }
        const bool rootVisible = term.isEmpty() || rootMatches || childMatches;
        root->setHidden(!rootVisible);
        if (!term.isEmpty() && rootVisible) {
            root->setExpanded(true);
        }
    }
}

void MainWindow::SetDisplayMode(ViewportDisplayMode mode)
{
    if (viewport_ == nullptr) {
        return;
    }

    const auto restoreCurrentModeAction = [this] {
        const ViewportDisplayMode currentMode = viewport_->DisplayMode();
        const QSignalBlocker designBlocker(designDisplayAction_);
        const QSignalBlocker finishedBlocker(finishedDisplayAction_);
        const QSignalBlocker isolateBlocker(isolateDisplayAction_);
        designDisplayAction_->setChecked(currentMode == ViewportDisplayMode::Design);
        finishedDisplayAction_->setChecked(currentMode == ViewportDisplayMode::FinishedModel);
        isolateDisplayAction_->setChecked(currentMode == ViewportDisplayMode::IsolatedSelection);
    };
    if (viewport_->DrawingPointCount() > 0) {
        restoreCurrentModeAction();
        statusBar()->showMessage(
            QStringLiteral("作図中です。先に「完了」または「取消」を押してください"), 4000);
        return;
    }

    std::vector<CadSelection> isolatedSelections;
    if (mode == ViewportDisplayMode::IsolatedSelection) {
        isolatedSelections = viewport_->Selections();
        if (isolatedSelections.empty()) {
            restoreCurrentModeAction();
            statusBar()->showMessage(QStringLiteral("先に3D画面またはモデル一覧で要素を選択してください"), 4000);
            return;
        }
    }

    SetViewportTool(ViewportTool::Select);
    if (mode == ViewportDisplayMode::FinishedModel) {
        std::vector<CadSelection> finishedSelections;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Plate
                || selection.kind == CadSelectionKind::Body) {
                finishedSelections.push_back(selection);
            }
        }
        UpdateSelections(std::move(finishedSelections), true);
    }
    viewport_->SetDisplayMode(mode, std::move(isolatedSelections));

    {
        const QSignalBlocker designBlocker(designDisplayAction_);
        const QSignalBlocker finishedBlocker(finishedDisplayAction_);
        const QSignalBlocker isolateBlocker(isolateDisplayAction_);
        designDisplayAction_->setChecked(mode == ViewportDisplayMode::Design);
        finishedDisplayAction_->setChecked(mode == ViewportDisplayMode::FinishedModel);
        isolateDisplayAction_->setChecked(mode == ViewportDisplayMode::IsolatedSelection);
    }
    viewport_->FitAll();

    const QString message = mode == ViewportDisplayMode::Design
        ? QStringLiteral("設計表示に戻しました")
        : mode == ViewportDisplayMode::FinishedModel
            ? QStringLiteral("完成形だけを一時表示しています")
            : QStringLiteral("選択した要素だけを一時表示しています");
    statusBar()->showMessage(message, 3000);
}

void MainWindow::ResetDisplayMode()
{
    if (viewport_ != nullptr) {
        viewport_->SetDisplayMode(ViewportDisplayMode::Design);
    }
    if (designDisplayAction_ == nullptr || finishedDisplayAction_ == nullptr
        || isolateDisplayAction_ == nullptr) {
        return;
    }
    const QSignalBlocker designBlocker(designDisplayAction_);
    const QSignalBlocker finishedBlocker(finishedDisplayAction_);
    const QSignalBlocker isolateBlocker(isolateDisplayAction_);
    designDisplayAction_->setChecked(true);
    finishedDisplayAction_->setChecked(false);
    isolateDisplayAction_->setChecked(false);
}

void MainWindow::RefreshModelViews(bool fitView)
{
    if (viewport_->DisplayMode() == ViewportDisplayMode::IsolatedSelection) {
        ResetDisplayMode();
    }
    modelTree_->blockSignals(true);
    modelTree_->clear();
    auto* planeRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("作業平面 (%1)").arg(project_.WorkPlanes().size())});
    for (int index = 0; index < static_cast<int>(project_.WorkPlanes().size()); ++index) {
        auto* item = new QTreeWidgetItem(planeRoot, {ToQString(project_.WorkPlanes()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::WorkPlane));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.WorkPlanes()[index].visible ? Qt::Checked : Qt::Unchecked);
    }
    auto* pointRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("作図点 (%1)").arg(project_.Points().size())});
    for (int index = 0; index < static_cast<int>(project_.Points().size()); ++index) {
        auto* item = new QTreeWidgetItem(pointRoot, {ToQString(project_.Points()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::Point));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.Points()[index].visible ? Qt::Checked : Qt::Unchecked);
    }
    auto* wireRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("ワイヤー (%1)").arg(project_.Wires().size())});
    for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
        const auto& wire = project_.Wires()[index];
        const QString label = wire.metadata.construction
            ? QStringLiteral("%1 （補助）").arg(ToQString(wire.name))
            : ToQString(wire.name);
        auto* item = new QTreeWidgetItem(wireRoot, {label});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::Wire));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.Wires()[index].visible ? Qt::Checked : Qt::Unchecked);
    }
    auto* coincidenceRoot = new QTreeWidgetItem(
        modelTree_, {QStringLiteral("端点一致 (%1)").arg(project_.CoincidentConstraints().size())});
    const auto endpointText = [](kachakacha::model::WireEndpoint endpoint) {
        return endpoint == kachakacha::model::WireEndpoint::Start
            ? QStringLiteral("始点")
            : QStringLiteral("終点");
    };
    for (const auto& constraint : project_.CoincidentConstraints()) {
        auto* item = new QTreeWidgetItem(coincidenceRoot, {
            QStringLiteral("%1:%2  =  %3:%4")
                .arg(ToQString(constraint.anchor.wireName), endpointText(constraint.anchor.endpoint),
                    ToQString(constraint.follower.wireName), endpointText(constraint.follower.endpoint)),
        });
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setToolTip(0, QStringLiteral("左が固定側、右が追従側"));
    }
    auto* tangentRoot = new QTreeWidgetItem(
        modelTree_, {QStringLiteral("滑らか接続 (%1)").arg(project_.TangentConstraints().size())});
    for (const auto& constraint : project_.TangentConstraints()) {
        auto* item = new QTreeWidgetItem(tangentRoot, {
            QStringLiteral("%1  %2:%3  →  %4:%5")
                .arg(constraint.continuity == WireContinuity::G2Curvature
                        ? QStringLiteral("G2") : QStringLiteral("G1"),
                    ToQString(constraint.anchor.wireName), endpointText(constraint.anchor.endpoint),
                    ToQString(constraint.follower.wireName), endpointText(constraint.follower.endpoint)),
        });
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setToolTip(0,
            constraint.continuity == WireContinuity::G2Curvature
                ? QStringLiteral("右側のベジェ曲線が左側の位置・接線・曲率へ追従")
                : QStringLiteral("右側のベジェ曲線または円弧が左側の接線方向へ追従"));
    }
    auto* dimensionRoot = new QTreeWidgetItem(
        modelTree_, {QStringLiteral("参照寸法 (%1)").arg(project_.ReferenceDimensions().size())});
    for (const ReferenceDimension& dimension : project_.ReferenceDimensions()) {
        QString label = ToQString(dimension.name);
        try {
            const auto result = project_.EvaluateReferenceDimension(dimension.name);
            label = QStringLiteral("%1  %2")
                .arg(label, ReferenceDimensionValueText(dimension.kind, result.value));
        } catch (const std::exception&) {
            label += QStringLiteral("  [参照切れ]");
        }
        auto* item = new QTreeWidgetItem(dimensionRoot, {label});
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setToolTip(0, ReferenceDimensionKindText(dimension.kind));
    }
    auto* surfaceRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("面 (%1)").arg(project_.Surfaces().size())});
    for (int index = 0; index < static_cast<int>(project_.Surfaces().size()); ++index) {
        auto* item = new QTreeWidgetItem(surfaceRoot, {ToQString(project_.Surfaces()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::Surface));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.Surfaces()[index].visible ? Qt::Checked : Qt::Unchecked);
    }
    auto* plateRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("板材 (%1)").arg(project_.Plates().size())});
    for (int index = 0; index < static_cast<int>(project_.Plates().size()); ++index) {
        auto* item = new QTreeWidgetItem(plateRoot, {ToQString(project_.Plates()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::Plate));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.Plates()[index].visible ? Qt::Checked : Qt::Unchecked);
    }
    auto* bodyRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("治具・立体 (%1)").arg(project_.Bodies().size())});
    for (int index = 0; index < static_cast<int>(project_.Bodies().size()); ++index) {
        auto* item = new QTreeWidgetItem(bodyRoot, {ToQString(project_.Bodies()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::Body));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.Bodies()[index].visible ? Qt::Checked : Qt::Unchecked);
    }
    planeRoot->setExpanded(true);
    pointRoot->setExpanded(true);
    wireRoot->setExpanded(true);
    coincidenceRoot->setExpanded(true);
    tangentRoot->setExpanded(true);
    dimensionRoot->setExpanded(true);
    surfaceRoot->setExpanded(true);
    plateRoot->setExpanded(true);
    bodyRoot->setExpanded(true);
    modelTree_->blockSignals(false);
    ApplyModelTreeFilter();

    RefreshPlaneChoices();
    RefreshWireChoices();
    RefreshSurfaceChoices();
    viewport_->SetProject(&project_, fitView);
    RefreshReferenceDimensions();
    RefreshActiveWorkPlane();
    RefreshReference();
    UpdateSelection({}, false);
}

void MainWindow::RefreshPlaneChoices()
{
    const auto refresh = [this](QComboBox* combo) {
        if (combo == nullptr) {
            return;
        }
        const QSignalBlocker blocker(combo);
        const QString previous = combo->currentText();
        combo->clear();
        for (const auto& plane : project_.WorkPlanes()) {
            combo->addItem(ToQString(plane.name));
        }
        const int previousIndex = combo->findText(previous);
        if (previousIndex >= 0) {
            combo->setCurrentIndex(previousIndex);
        }
    };
    refresh(offsetSourcePlane_);
    refresh(rotateSourcePlane_);
    refresh(wirePlane_);
    refresh(exportPlane_);
    refresh(plateFlatPatternPlane_);
    refresh(projectionPlane_);

    if (activePlaneCombo_ != nullptr) {
        const QSignalBlocker blocker(activePlaneCombo_);
        const QString previous = activePlaneCombo_->currentText();
        activePlaneCombo_->clear();
        activePlaneCombo_->addItem(QStringLiteral("作図面なし"));
        for (const auto& plane : project_.WorkPlanes()) {
            if (plane.visible) {
                activePlaneCombo_->addItem(ToQString(plane.name));
            }
        }
        const int previousIndex = activePlaneCombo_->findText(previous);
        if (previousIndex >= 0) {
            activePlaneCombo_->setCurrentIndex(previousIndex);
        } else if (activePlaneCombo_->count() > 1) {
            activePlaneCombo_->setCurrentIndex(1);
        }
    }

    const QString previousEditSource = editWireSourcePlane_->currentText();
    editWireSourcePlane_->clear();
    editWireSourcePlane_->addItem(QStringLiteral("なし"));
    for (const auto& plane : project_.WorkPlanes()) {
        editWireSourcePlane_->addItem(ToQString(plane.name));
    }
    const int previousEditIndex = editWireSourcePlane_->findText(previousEditSource);
    editWireSourcePlane_->setCurrentIndex(previousEditIndex >= 0 ? previousEditIndex : 0);
    RefreshExportSummary();
}

void MainWindow::RefreshWireChoices()
{
    const auto refresh = [this](QComboBox* combo) {
        const QString previous = combo->currentText();
        combo->clear();
        for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
            if (project_.Wires()[index].wire.Kind() == WireKind::Line) {
                combo->addItem(ToQString(project_.Wires()[index].name), index);
            }
        }
        const int previousIndex = combo->findText(previous);
        if (previousIndex >= 0) {
            combo->setCurrentIndex(previousIndex);
        }
    };
    refresh(chamferFirstWire_);
    refresh(chamferSecondWire_);
    if (chamferSecondWire_->count() > 1 && chamferSecondWire_->currentIndex() == chamferFirstWire_->currentIndex()) {
        chamferSecondWire_->setCurrentIndex(1);
    }
}

void MainWindow::RefreshSurfaceChoices()
{
    const auto refresh = [this](QComboBox* combo) {
        if (combo == nullptr) {
            return;
        }
        const QSignalBlocker blocker(combo);
        const QString previous = combo->currentText();
        combo->clear();
        for (const auto& surface : project_.Surfaces()) {
            combo->addItem(ToQString(surface.name));
        }
        const int previousIndex = combo->findText(previous);
        if (previousIndex >= 0) {
            combo->setCurrentIndex(previousIndex);
        }
    };
    refresh(projectionSurface_);
    refresh(plateSurface_);
    refresh(jigSurface_);
}

void MainWindow::RefreshExportSummary()
{
    if (exportPlane_ == nullptr || exportScope_ == nullptr || exportSummary_ == nullptr || viewport_ == nullptr) {
        return;
    }
    const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(exportPlane_->currentText()));
    if (!plane.has_value()) {
        exportSummary_->setText(QStringLiteral("出力面なし"));
    } else {
        std::size_t count = 0;
        if (exportScope_->currentIndex() == 0) {
            count = static_cast<std::size_t>(std::count_if(project_.Wires().begin(), project_.Wires().end(), [&](const auto& wire) {
                return !wire.metadata.construction && WireLiesOnWorkPlane(wire.wire, *plane);
            }));
        } else {
            count = static_cast<std::size_t>(std::count_if(viewport_->Selections().begin(), viewport_->Selections().end(), [this](const auto& selection) {
                return selection.kind == CadSelectionKind::Wire && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Wires().size())
                    && !project_.Wires()[selection.index].metadata.construction;
            }));
        }
        exportSummary_->setText(QStringLiteral("出力対象: %1本").arg(count));
    }

    if (bodyExportSummary_ != nullptr && modelExportScope_ != nullptr) {
        std::size_t plateCount = 0;
        std::size_t bodyCount = 0;
        if (modelExportScope_->currentIndex() == 0) {
            for (const CadSelection& selection : viewport_->Selections()) {
                if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Plates().size())) {
                    ++plateCount;
                } else if (selection.kind == CadSelectionKind::Body && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Bodies().size())) {
                    ++bodyCount;
                }
            }
        } else {
            plateCount = static_cast<std::size_t>(std::count_if(
                project_.Plates().begin(), project_.Plates().end(), [](const auto& plate) {
                    return plate.visible;
                }));
            bodyCount = static_cast<std::size_t>(std::count_if(
                project_.Bodies().begin(), project_.Bodies().end(), [](const auto& body) {
                    return body.visible;
                }));
        }
        bodyExportSummary_->setStyleSheet("color: #5c6670;");
        bodyExportSummary_->setText(plateCount + bodyCount == 0
            ? QStringLiteral("3D出力対象: なし")
            : QStringLiteral("3D出力対象: %1部品（板材%2 / 治具%3）")
                .arg(plateCount + bodyCount).arg(plateCount).arg(bodyCount));
    }

    if (plateFlatPatternSummary_ == nullptr) {
        return;
    }
    UpdatePlateAssemblyGuidePreview();
    std::vector<int> plateIndices;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            plateIndices.push_back(selection.index);
        }
    }
    std::sort(plateIndices.begin(), plateIndices.end());
    plateIndices.erase(std::unique(plateIndices.begin(), plateIndices.end()), plateIndices.end());
    if (plateIndices.empty()) {
        plateFlatPatternSummary_->setStyleSheet("color: #5c6670;");
        plateFlatPatternSummary_->setText(QStringLiteral("選択板材: なし"));
        return;
    }
    if (plateIndices.size() > 1) {
        plateFlatPatternSummary_->setStyleSheet("color: #5c6670;");
        plateFlatPatternSummary_->setText(QStringLiteral("選択板材: %1枚（1枚に絞って出力）").arg(plateIndices.size()));
        return;
    }

    try {
        PlateFlatPatternOptions previewOptions = PlateFlatPatternOptionsFromUi();
        previewOptions.uSegments = 48;
        previewOptions.vSegments = 16;
        previewOptions.openingSamples = 48;
        previewOptions.includeOpenings = false;
        const auto& namedPlate = project_.Plates()[plateIndices.front()];
        const auto pattern = BuildPlateFlatPattern(project_, namedPlate, previewOptions);
        PlatePdfOptions pdfOptions;
        pdfOptions.pageSize = static_cast<QPageSize::PageSizeId>(platePdfPaper_->currentData().toInt());
        pdfOptions.overlapMillimeters = platePdfOverlap_->value();
        const auto pdfLayout = CalculatePlatePdfLayout(pattern, pdfOptions);
        const bool manualSplitOverridesNotches = !namedPlate.splitWireNames.empty()
            && previewOptions.includeAutomaticReliefCuts
            && previewOptions.automaticReliefStyle != AutomaticReliefStyle::SplitPieces;
        const QString shape = manualSplitOverridesNotches
            ? QStringLiteral("手動分割を優先 %1片").arg(pattern.analysis.pieceCount)
            : pattern.analysis.automaticNotchCount > 0
            ? QStringLiteral("一体板・V字切れ込み %1本")
                .arg(pattern.analysis.automaticNotchCount)
            : pattern.analysis.pieceCount > 1
            ? QStringLiteral("ペーパークラフト %1片").arg(pattern.analysis.pieceCount)
            : pattern.analysis.classification == PlateDevelopability::Planar
            ? QStringLiteral("平面板")
            : pattern.analysis.classification == PlateDevelopability::Developable
            ? QStringLiteral("一方向曲げ")
            : QStringLiteral("二方向曲面・要確認");
        const bool warning = (pattern.analysis.classification == PlateDevelopability::DoubleCurved
                && pattern.analysis.pieceCount <= 1)
            || pattern.analysis.MaximumEstimatedErrorMillimeters() > 0.1;
        plateFlatPatternSummary_->setStyleSheet(warning ? "color: #a32734;" : "color: #35664a;");
        plateFlatPatternSummary_->setText(
            QStringLiteral("%1 | %2 | 最大推定ずれ %3 mm | 開口 %4 | 折り線 %5 | 切れ目 %6 | PDF %7ページ")
                .arg(ToQString(namedPlate.name), shape)
                .arg(pattern.analysis.MaximumEstimatedErrorMillimeters(), 0, 'f', 3)
                .arg(namedPlate.openingWireNames.size())
                .arg(pattern.foldLines.size())
                .arg(pattern.reliefCuts.size())
                .arg(pdfLayout.PageCount()));
    } catch (const std::exception& error) {
        plateFlatPatternSummary_->setStyleSheet("color: #a32734;");
        plateFlatPatternSummary_->setText(QStringLiteral("展開不可: %1").arg(QString::fromUtf8(error.what())));
    }
}

void MainWindow::UpdateSelection(CadSelection selection, bool updateTree)
{
    if (selection.kind == CadSelectionKind::None) {
        UpdateSelections({}, updateTree);
    } else {
        UpdateSelections({selection}, updateTree);
    }
}

void MainWindow::UpdateSelections(std::vector<CadSelection> selections, bool updateTree)
{
    viewport_->SetSelections(selections);
    if (updateTree) {
        modelTree_->blockSignals(true);
        modelTree_->clearSelection();
        QTreeWidgetItem* lastSelectedItem = nullptr;
        QTreeWidgetItemIterator iterator(modelTree_);
        while (*iterator) {
            QTreeWidgetItem* item = *iterator;
            const auto matching = std::find_if(selections.begin(), selections.end(), [&](const CadSelection& selection) {
                return item->data(0, kSelectionKindRole).isValid()
                    && item->data(0, kSelectionIndexRole).isValid()
                    && item->data(0, kSelectionKindRole).toInt() == static_cast<int>(selection.kind)
                    && item->data(0, kSelectionIndexRole).toInt() == selection.index;
            });
            if (matching != selections.end()) {
                item->setSelected(true);
                if (matching == selections.end() - 1) {
                    lastSelectedItem = item;
                }
            }
            ++iterator;
        }
        if (lastSelectedItem != nullptr) {
            modelTree_->setCurrentItem(lastSelectedItem, 0, QItemSelectionModel::NoUpdate);
            modelTree_->scrollToItem(lastSelectedItem);
        }
        modelTree_->blockSignals(false);
    }

    std::size_t selectedWireCount = 0;
    std::size_t selectedSurfaceCount = 0;
    std::size_t selectedPlateCount = 0;
    std::size_t selectedProjectedWireCount = 0;
    std::size_t selectedClosedProjectedWireCount = 0;
    QStringList lightCaseFrontNames;
    QStringList lightCaseTargetNames;
    for (const CadSelection& item : selections) {
        if (item.kind == CadSelectionKind::Wire && item.index >= 0
            && item.index < static_cast<int>(project_.Wires().size())) {
            ++selectedWireCount;
            const auto& wire = project_.Wires()[item.index];
            if (wire.projection.has_value()) {
                ++selectedProjectedWireCount;
            }
            if (wire.projection.has_value() && wire.wire.IsClosed()) {
                ++selectedClosedProjectedWireCount;
            }
            if (!wire.metadata.construction && !wire.projection.has_value()
                && !wire.plateOffset.has_value() && wire.wire.IsClosed()) {
                lightCaseFrontNames.push_back(ToQString(wire.name));
            }
            if (!wire.projection.has_value() && wire.metadata.sourcePlaneName.has_value() && projectionPlane_ != nullptr) {
                const int planeIndex = projectionPlane_->findText(ToQString(*wire.metadata.sourcePlaneName));
                if (planeIndex >= 0) {
                    projectionPlane_->setCurrentIndex(planeIndex);
                }
            }
        } else if (item.kind == CadSelectionKind::Surface && item.index >= 0
            && item.index < static_cast<int>(project_.Surfaces().size())) {
            ++selectedSurfaceCount;
            lightCaseTargetNames.push_back(ToQString(project_.Surfaces()[item.index].name));
            if (projectionSurface_ != nullptr) {
                const int surfaceIndex = projectionSurface_->findText(ToQString(project_.Surfaces()[item.index].name));
                if (surfaceIndex >= 0) {
                    projectionSurface_->setCurrentIndex(surfaceIndex);
                }
            }
            if (plateSurface_ != nullptr) {
                const int surfaceIndex = plateSurface_->findText(ToQString(project_.Surfaces()[item.index].name));
                if (surfaceIndex >= 0) {
                    plateSurface_->setCurrentIndex(surfaceIndex);
                }
            }
            if (jigSurface_ != nullptr) {
                const int surfaceIndex = jigSurface_->findText(ToQString(project_.Surfaces()[item.index].name));
                if (surfaceIndex >= 0) {
                    jigSurface_->setCurrentIndex(surfaceIndex);
                }
            }
        } else if (item.kind == CadSelectionKind::Plate && item.index >= 0
            && item.index < static_cast<int>(project_.Plates().size())) {
            ++selectedPlateCount;
            lightCaseTargetNames.push_back(ToQString(project_.Plates()[item.index].name));
        }
    }
    if (surfaceSelectionLabel_ != nullptr) {
        surfaceSelectionLabel_->setText(QStringLiteral("選択: ワイヤー%1本 / 面%2枚")
                .arg(selectedWireCount)
                .arg(selectedSurfaceCount));
    }
    if (projectionSelectionLabel_ != nullptr) {
        projectionSelectionLabel_->setText(QStringLiteral("投影するワイヤー: %1本").arg(selectedWireCount));
    }
    if (lightCaseSelectionLabel_ != nullptr) {
        if (lightCaseFrontNames.size() == 1 && lightCaseTargetNames.size() == 1) {
            lightCaseSelectionLabel_->setText(
                QStringLiteral("最前面: %1\n根元の接続先: %2")
                    .arg(lightCaseFrontNames.front(), lightCaseTargetNames.front()));
        } else {
            lightCaseSelectionLabel_->setText(
                QStringLiteral("選択: 最前面の閉じた輪郭%1本 / 接続先%2個")
                    .arg(lightCaseFrontNames.size())
                    .arg(lightCaseTargetNames.size()));
        }
    }
    if (plateOpeningSelectionLabel_ != nullptr) {
        plateOpeningSelectionLabel_->setText(QStringLiteral("選択: 板材%1枚 / 閉じた投影輪郭%2本")
                .arg(selectedPlateCount)
                .arg(selectedClosedProjectedWireCount));
    }
    if (plateReliefSelectionLabel_ != nullptr) {
        plateReliefSelectionLabel_->setText(QStringLiteral("選択: 板材%1枚 / 投影ワイヤー%2本")
                .arg(selectedPlateCount)
                .arg(selectedProjectedWireCount));
    }
    if (plateSplitLineSelectionLabel_ != nullptr) {
        plateSplitLineSelectionLabel_->setText(QStringLiteral("選択: 板材%1枚 / 投影ワイヤー%2本")
                .arg(selectedPlateCount)
                .arg(selectedProjectedWireCount));
    }
    if (plateOffsetSelectionLabel_ != nullptr) {
        plateOffsetSelectionLabel_->setText(QStringLiteral("選択: 板材%1枚 / 投影ワイヤー%2本")
                .arg(selectedPlateCount)
                .arg(selectedProjectedWireCount));
    }
    if (plateSplitSelectionLabel_ != nullptr) {
        plateSplitSelectionLabel_->setText(QStringLiteral("選択: 板材%1枚").arg(selectedPlateCount));
    }
    const CadSelection selection = selections.empty() ? CadSelection{} : selections.back();
    if (pendingMachiningPickSlot_ >= 0 && selection.kind == CadSelectionKind::Wire
        && selection.index >= 0 && selection.index < static_cast<int>(project_.Wires().size())
        && project_.Wires()[selection.index].wire.Kind() == WireKind::Line) {
        QComboBox* target = pendingMachiningPickSlot_ == 0 ? chamferFirstWire_ : chamferSecondWire_;
        const int comboIndex = target->findData(selection.index);
        if (comboIndex >= 0) {
            target->setCurrentIndex(comboIndex);
            pendingMachiningPickSlot_ = -1;
            machiningPickFirstButton_->setChecked(false);
            machiningPickSecondButton_->setChecked(false);
            statusBar()->showMessage(QStringLiteral("加工する直線を設定しました"), 2000);
        }
    } else if (pendingMachiningPickSlot_ < 0) {
        SyncMachiningSelection(selections);
    }

    if (selection.kind == CadSelectionKind::WorkPlane && selection.index >= 0 && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
        const auto& named = project_.WorkPlanes()[selection.index];
        const int activeIndex = activePlaneCombo_->findText(ToQString(named.name));
        if (activeIndex >= 0 && activePlaneCombo_->currentIndex() != activeIndex) {
            activePlaneCombo_->setCurrentIndex(activeIndex);
        }
        infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: 作業平面<br><br>原点<br>%2<br><br>法線<br>%3<br><br>平面内 X<br>%4")
            .arg(ToQString(named.name), VectorText(named.plane.Origin()), VectorText(named.plane.Normal()), VectorText(named.plane.UAxis())));
    } else if (selection.kind == CadSelectionKind::Point && selection.index >= 0
        && selection.index < static_cast<int>(project_.Points().size())) {
        const auto& named = project_.Points()[selection.index];
        const QString source = named.sourcePlaneName.has_value()
            ? ToQString(*named.sourcePlaneName) : QStringLiteral("なし");
        infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: 作図点<br>作成元平面: %2<br><br>位置<br>%3")
            .arg(ToQString(named.name), source, VectorText(named.point)));
    } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0 && selection.index < static_cast<int>(project_.Wires().size())) {
        const auto& named = project_.Wires()[selection.index];
        const QString source = named.metadata.sourcePlaneName.has_value() ? ToQString(*named.metadata.sourcePlaneName) : QStringLiteral("なし");
        if (named.plateOffset.has_value()) {
            const QString layer = named.plateOffset->throughThickness > 0.75
                ? QStringLiteral("+側表面")
                : named.plateOffset->throughThickness < 0.25
                ? QStringLiteral("-側表面") : QStringLiteral("板厚中央");
            infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: 板厚位置ワイヤー<br>元の輪郭: %2<br>対象板材: %3<br>位置: %4<br><br>始点<br>%5<br><br>終点<br>%6")
                    .arg(ToQString(named.name),
                        ToQString(named.plateOffset->sourceWireName),
                        ToQString(named.plateOffset->plateName),
                        layer,
                        VectorText(named.wire.Start()),
                        VectorText(named.wire.End())));
        } else if (named.projection.has_value()) {
            infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: 面上の投影ワイヤー<br>元の平面図: %2<br>対象面: %3<br>投影方向<br>%4<br><br>始点<br>%5<br><br>終点<br>%6")
                    .arg(ToQString(named.name),
                        ToQString(named.projection->sourceWireName),
                        ToQString(named.projection->targetSurfaceName),
                        VectorText(named.projection->direction),
                        VectorText(named.wire.Start()),
                        VectorText(named.wire.End())));
        } else {
            QString details = QStringLiteral("<b>%1</b><br><br>種類: %2<br>用途: %3<br>平面との関係: %4<br>作成元平面: %5<br><br>始点<br>%6<br><br>終点<br>%7")
                .arg(ToQString(named.name), WireKindText(named.wire.Kind()),
                    named.metadata.construction ? QStringLiteral("補助線") : QStringLiteral("通常線"),
                    PolicyText(named.metadata.planePolicy), source,
                    VectorText(named.wire.Start()), VectorText(named.wire.End()));
            if (named.wire.Kind() == WireKind::Line) {
                const double length = (named.wire.End() - named.wire.Start()).Length();
                details += QStringLiteral("<br><br>長さ: %1 mm%2")
                    .arg(Number(length), named.metadata.lineConstraints.lengthMillimeters.has_value()
                            ? QStringLiteral(" （固定中）") : QString());
                if (named.metadata.lineConstraints.angleDegrees.has_value()) {
                    details += QStringLiteral("<br>平面内角度: %1° （固定中）")
                        .arg(Number(*named.metadata.lineConstraints.angleDegrees));
                }
            } else if (named.wire.Kind() == WireKind::Circle
                || named.wire.Kind() == WireKind::CircularArc) {
                details += QStringLiteral("<br><br>半径: %1 mm%2")
                    .arg(Number(named.wire.ArcData().radius),
                        named.metadata.curveConstraints.radiusMillimeters.has_value()
                            ? QStringLiteral(" （固定中）") : QString());
            }
            const std::size_t coincidenceCount = std::count_if(
                project_.CoincidentConstraints().begin(), project_.CoincidentConstraints().end(),
                [&](const auto& constraint) {
                    return constraint.anchor.wireName == named.name
                        || constraint.follower.wireName == named.name;
                });
            if (coincidenceCount > 0) {
                details += QStringLiteral("<br>端点一致: %1件").arg(coincidenceCount);
            }
            const std::size_t tangentCount = std::count_if(
                project_.TangentConstraints().begin(), project_.TangentConstraints().end(),
                [&](const auto& constraint) {
                    return constraint.anchor.wireName == named.name
                        || constraint.follower.wireName == named.name;
                });
            const std::size_t curvatureCount = std::count_if(
                project_.TangentConstraints().begin(), project_.TangentConstraints().end(),
                [&](const auto& constraint) {
                    return constraint.continuity == WireContinuity::G2Curvature
                        && (constraint.anchor.wireName == named.name
                            || constraint.follower.wireName == named.name);
                });
            if (tangentCount > curvatureCount) {
                details += QStringLiteral("<br>G1接線接続: %1件").arg(tangentCount - curvatureCount);
            }
            if (curvatureCount > 0) {
                details += QStringLiteral("<br>G2曲率接続: %1件").arg(curvatureCount);
            }
            infoLabel_->setText(details);
        }
    } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
        && selection.index < static_cast<int>(project_.Surfaces().size())) {
        const auto& named = project_.Surfaces()[selection.index];
        const QString kind = named.surface.Kind() == SurfaceKind::Planar
            ? QStringLiteral("平面")
            : named.surface.Kind() == SurfaceKind::Ruled
            ? QStringLiteral("2断面の曲面")
            : QStringLiteral("複数断面のロフト面");
        QString sources;
        for (const std::string& sourceName : named.sourceWireNames) {
            if (!sources.isEmpty()) {
                sources += QStringLiteral(" / ");
            }
            sources += ToQString(sourceName);
        }
        infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: %2<br>元ワイヤー: %3")
                .arg(ToQString(named.name), kind, sources));
    } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
        && selection.index < static_cast<int>(project_.Plates().size())) {
        const auto& named = project_.Plates()[selection.index];
        if (plateSurface_ != nullptr) {
            plateSurface_->setCurrentText(ToQString(named.sourceSurfaceName));
        }
        if (plateThickness_ != nullptr) {
            plateThickness_->setValue(named.plate.Thickness());
        }
        if (plateVariableThickness_ != nullptr && plateEndThickness_ != nullptr) {
            plateVariableThickness_->setChecked(named.plate.HasVariableThickness());
            plateEndThickness_->setValue(named.plate.EndThickness());
        }
        if (plateDirection_ != nullptr) {
            plateDirection_->setCurrentIndex(plateDirection_->findData(static_cast<int>(named.plate.Direction())));
        }
        if (plateMaterial_ != nullptr) {
            const int materialIndex = plateMaterial_->findData(ToQString(named.material));
            plateMaterial_->setCurrentIndex(materialIndex >= 0 ? materialIndex : plateMaterial_->count() - 1);
        }
        const QString direction = named.plate.Direction() == PlateThicknessDirection::Positive
            ? QStringLiteral("+側（法線矢印側）")
            : named.plate.Direction() == PlateThicknessDirection::Centered
            ? QStringLiteral("中央（両側へ半分）")
            : QStringLiteral("-側（矢印と反対）");
        const QString material = named.material == "styrene" ? QStringLiteral("プラ板")
            : named.material == "paper" ? QStringLiteral("紙・厚紙")
            : named.material == "brass" ? QStringLiteral("真鍮板")
            : QStringLiteral("その他");
        const auto developability = named.plate.AnalyzeDevelopability();
        const QString forming = developability.classification == PlateDevelopability::Planar
            ? QStringLiteral("平面板: そのまま切り出せます")
            : developability.classification == PlateDevelopability::Developable
            ? QStringLiteral("一方向曲げ: 展開できる可能性が高いです")
            : QStringLiteral("二方向曲面: 分割または成形が必要です");
        QString openings;
        for (const std::string& openingName : named.openingWireNames) {
            if (!openings.isEmpty()) {
                openings += QStringLiteral(" / ");
            }
            openings += ToQString(openingName);
        }
        if (openings.isEmpty()) {
            openings = QStringLiteral("なし");
        }
        QString reliefCuts;
        for (const std::string& cutName : named.reliefCutWireNames) {
            if (!reliefCuts.isEmpty()) {
                reliefCuts += QStringLiteral(" / ");
            }
            reliefCuts += ToQString(cutName);
        }
        if (reliefCuts.isEmpty()) {
            reliefCuts = QStringLiteral("なし");
        }
        QString splitLines;
        for (const std::string& splitName : named.splitWireNames) {
            if (!splitLines.isEmpty()) {
                splitLines += QStringLiteral(" / ");
            }
            splitLines += ToQString(splitName);
        }
        if (splitLines.isEmpty()) {
            splitLines = QStringLiteral("なし");
        }
        const auto& range = named.plate.Range();
        const QString rangeText = QStringLiteral("断面内 %1-%2% / 長手 %3-%4%")
            .arg(range.minimumU * 100.0, 0, 'f', 1)
            .arg(range.maximumU * 100.0, 0, 'f', 1)
            .arg(range.minimumV * 100.0, 0, 'f', 1)
            .arg(range.maximumV * 100.0, 0, 'f', 1);
        const QString plateKind = range.IsFull() ? QStringLiteral("板材") : QStringLiteral("分割した板材");
        const QString thicknessText = named.plate.HasVariableThickness()
            ? QStringLiteral("%1 mm → %2 mm")
                .arg(named.plate.Thickness()).arg(named.plate.EndThickness())
            : QStringLiteral("%1 mm").arg(named.plate.Thickness());
        infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: %2<br>元の面: %3<br>板厚: %4<br>厚み方向: %5<br>材質: %6<br>開口: %7<br>途中切れ目: %8<br>展開分割線: %9<br>部品範囲: %10<br><br>工作判定: %11")
                .arg(ToQString(named.name))
                .arg(plateKind)
                .arg(ToQString(named.sourceSurfaceName))
                .arg(thicknessText)
                .arg(direction)
                .arg(material)
                .arg(openings)
                .arg(reliefCuts)
                .arg(splitLines)
                .arg(rangeText)
                .arg(forming));
    } else if (selection.kind == CadSelectionKind::Body && selection.index >= 0
        && selection.index < static_cast<int>(project_.Bodies().size())) {
        const auto& named = project_.Bodies()[selection.index];
        const auto& body = named.body;
        if (jigSurface_ != nullptr) {
            jigSurface_->setCurrentText(ToQString(named.sourceSurfaceName));
        }
        if (jigSide_ != nullptr) {
            jigSide_->setCurrentIndex(jigSide_->findData(static_cast<int>(body.Side())));
        }
        jigClearance_->setValue(body.ClearanceMillimeters());
        jigThickness_->setValue(body.ThicknessMillimeters());
        const auto analysis = body.AnalyzePrintability(jigMinimumWall_->value());
        jigAnalysisLabel_->setStyleSheet(
            analysis.meetsMinimumWall ? "color: #35664a;" : "color: #a32734;");
        jigAnalysisLabel_->setText(analysis.meetsMinimumWall
            ? QStringLiteral("造形確認: 最小肉厚 %1 mm を満たします").arg(analysis.minimumWallMillimeters, 0, 'f', 2)
            : QStringLiteral("造形警告: 厚み %1 mm は必要最小肉厚に不足します").arg(analysis.minimumWallMillimeters, 0, 'f', 2));
        const auto& range = body.Range();
        const QString side = body.Side() == JigSide::Positive
            ? QStringLiteral("外側の型") : QStringLiteral("内側の型");
        infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: 成形治具<br>元の面: %2<br>型の側: %3<br>成形の隙間: %4 mm<br>治具の厚み: %5 mm<br>使用範囲: U %6-%7% / V %8-%9%")
                .arg(ToQString(named.name), ToQString(named.sourceSurfaceName), side)
                .arg(body.ClearanceMillimeters(), 0, 'f', 3)
                .arg(body.ThicknessMillimeters(), 0, 'f', 3)
                .arg(range.minimumU * 100.0, 0, 'f', 1)
                .arg(range.maximumU * 100.0, 0, 'f', 1)
                .arg(range.minimumV * 100.0, 0, 'f', 1)
                .arg(range.maximumV * 100.0, 0, 'f', 1));
        if (bodyExportSummary_ != nullptr) {
            bodyExportSummary_->setStyleSheet(analysis.meetsMinimumWall
                ? "color: #35664a;" : "color: #a32734;");
            bodyExportSummary_->setText(analysis.meetsMinimumWall
                ? QStringLiteral("%1 | 肉厚 %2 mm | STL/STEP出力可能")
                    .arg(ToQString(named.name)).arg(body.ThicknessMillimeters(), 0, 'f', 2)
                : QStringLiteral("%1 | 肉厚不足: %2 mm")
                    .arg(ToQString(named.name)).arg(body.ThicknessMillimeters(), 0, 'f', 2));
        }
    } else {
        infoLabel_->setText(QStringLiteral("選択なし"));
    }
    PopulateEditPanel(selection);
    if (selections.size() > 1) {
        const auto wireCount = std::count_if(selections.begin(), selections.end(), [](const CadSelection& item) {
            return item.kind == CadSelectionKind::Wire;
        });
        editSelectionLabel_->setText(static_cast<std::size_t>(wireCount) == selections.size()
                ? QStringLiteral("%1本のワイヤーを選択").arg(wireCount)
                : QStringLiteral("%1個を選択").arg(selections.size()));
        editParameters_->setCurrentIndex(0);
        if (auto* label = qobject_cast<QLabel*>(editParameters_->widget(0))) {
            label->setText(QStringLiteral("複数選択中"));
        }
        editApplyButton_->setEnabled(false);
    } else {
        if (auto* label = qobject_cast<QLabel*>(editParameters_->widget(0))) {
            if (selection.kind == CadSelectionKind::Wire
                && selection.index >= 0 && selection.index < static_cast<int>(project_.Wires().size())
                && (project_.Wires()[selection.index].projection.has_value()
                    || project_.Wires()[selection.index].plateOffset.has_value())) {
                label->setText(project_.Wires()[selection.index].plateOffset.has_value()
                    ? QStringLiteral("板厚位置ワイヤーは元の投影輪郭または板材を編集します")
                    : QStringLiteral("投影ワイヤーは元の平面図を編集します"));
            } else if (selection.kind == CadSelectionKind::Surface) {
                label->setText(QStringLiteral("面は元の境界・断面ワイヤーを編集します"));
            } else if (selection.kind == CadSelectionKind::Plate) {
                label->setText(QStringLiteral("板材は元の面と板材欄から作り直します"));
            } else if (selection.kind == CadSelectionKind::Body) {
                label->setText(QStringLiteral("治具は面タブで側・隙間・厚みを変更します"));
            } else {
                label->setText(QStringLiteral("選択なし"));
            }
        }
        const bool editableWire = selection.kind == CadSelectionKind::Wire
            && selection.index >= 0 && selection.index < static_cast<int>(project_.Wires().size())
            && !project_.Wires()[selection.index].projection.has_value()
            && !project_.Wires()[selection.index].plateOffset.has_value();
        editApplyButton_->setEnabled(selection.kind == CadSelectionKind::WorkPlane || editableWire);
    }
    UpdatePlateSplitPreview();
    RefreshExportSummary();
    UpdateWireOffsetPreview();
    RefreshBeginnerGuide();
}

void MainWindow::SyncMachiningSelection(const std::vector<CadSelection>& selections)
{
    std::vector<int> lineIndices;
    for (const CadSelection& selection : selections) {
        if (selection.kind == CadSelectionKind::Wire
            && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())
            && project_.Wires()[selection.index].wire.Kind() == WireKind::Line) {
            lineIndices.push_back(selection.index);
        }
    }
    if (lineIndices.size() != 2) {
        return;
    }

    const int firstComboIndex = chamferFirstWire_->findData(lineIndices[0]);
    const int secondComboIndex = chamferSecondWire_->findData(lineIndices[1]);
    if (firstComboIndex >= 0 && secondComboIndex >= 0) {
        chamferFirstWire_->setCurrentIndex(firstComboIndex);
        chamferSecondWire_->setCurrentIndex(secondComboIndex);
        statusBar()->showMessage(QStringLiteral("選択した2本を加工対象に設定しました"), 2500);
    }
}

void MainWindow::BeginMachiningPick(int slot)
{
    QPushButton* requestedButton = slot == 0 ? machiningPickFirstButton_ : machiningPickSecondButton_;
    if (!requestedButton->isChecked()) {
        pendingMachiningPickSlot_ = -1;
        machiningPickFirstButton_->setChecked(false);
        machiningPickSecondButton_->setChecked(false);
        statusBar()->showMessage(QStringLiteral("3D選択を解除しました"), 1500);
        return;
    }

    pendingMachiningPickSlot_ = slot;
    machiningPickFirstButton_->setChecked(slot == 0);
    machiningPickSecondButton_->setChecked(slot == 1);
    statusBar()->showMessage(slot == 0 ? QStringLiteral("直線Aを3Dで選択") : QStringLiteral("直線Bを3Dで選択"));
}

void MainWindow::PopulateEditPanel(CadSelection selection)
{
    if (selection.kind == CadSelectionKind::WorkPlane
        && selection.index >= 0
        && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
        const auto& namedPlane = project_.WorkPlanes()[selection.index];
        editSelectionLabel_->setText(ToQString(namedPlane.name));
        const std::array<Vector3, 3> values = {namedPlane.plane.Origin(), namedPlane.plane.Normal(), namedPlane.plane.UAxis()};
        const std::array<std::array<QDoubleSpinBox*, 3>*, 3> editors = {&editPlaneOrigin_, &editPlaneNormal_, &editPlaneUAxis_};
        for (int group = 0; group < 3; ++group) {
            (*editors[group])[0]->setValue(values[group].x);
            (*editors[group])[1]->setValue(values[group].y);
            (*editors[group])[2]->setValue(values[group].z);
        }
        editParameters_->setCurrentIndex(1);
        return;
    }

    if (selection.kind == CadSelectionKind::Wire
        && selection.index >= 0
        && selection.index < static_cast<int>(project_.Wires().size())) {
        const auto& namedWire = project_.Wires()[selection.index];
        editSelectionLabel_->setText(ToQString(namedWire.name));
        if (namedWire.projection.has_value()) {
            editParameters_->setCurrentIndex(0);
            return;
        }
        if (namedWire.metadata.sourcePlaneName.has_value()) {
            const int sourceIndex = editWireSourcePlane_->findText(ToQString(*namedWire.metadata.sourcePlaneName));
            editWireSourcePlane_->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : 0);
        } else {
            editWireSourcePlane_->setCurrentIndex(0);
        }
        editWirePolicy_->setCurrentIndex(static_cast<int>(namedWire.metadata.planePolicy));
        editWireConstruction_->setChecked(namedWire.metadata.construction);
        const bool isLine = namedWire.wire.Kind() == WireKind::Line;
        editWireConstraintPanel_->setEnabled(isLine);
        editWireLockLength_->setChecked(
            isLine && namedWire.metadata.lineConstraints.lengthMillimeters.has_value());
        editWireLockAngle_->setChecked(
            isLine && namedWire.metadata.lineConstraints.angleDegrees.has_value());
        if (isLine) {
            const double currentLength = (namedWire.wire.End() - namedWire.wire.Start()).Length();
            editWireConstraintLength_->setValue(
                namedWire.metadata.lineConstraints.lengthMillimeters.value_or(currentLength));

            double currentAngle = 0.0;
            if (namedWire.metadata.lineConstraints.angleDegrees.has_value()) {
                currentAngle = *namedWire.metadata.lineConstraints.angleDegrees;
            } else if (namedWire.metadata.sourcePlaneName.has_value()) {
                const auto plane = project_.FindWorkPlane(*namedWire.metadata.sourcePlaneName);
                if (plane.has_value()) {
                    const auto start = plane->Project(namedWire.wire.Start());
                    const auto end = plane->Project(namedWire.wire.End());
                    currentAngle = std::atan2(end.v - start.v, end.u - start.u) * 180.0 / kPi;
                }
            }
            editWireConstraintAngle_->setValue(currentAngle);
        }

        if (namedWire.wire.Kind() == WireKind::Circle || namedWire.wire.Kind() == WireKind::CircularArc) {
            const auto arc = namedWire.wire.ArcData();
            const std::array<Vector3, 3> values = {arc.center, arc.uAxis, arc.vAxis};
            const std::array<std::array<QDoubleSpinBox*, 3>*, 3> editors = {&editArcCenter_, &editArcUAxis_, &editArcVAxis_};
            for (int group = 0; group < 3; ++group) {
                (*editors[group])[0]->setValue(values[group].x);
                (*editors[group])[1]->setValue(values[group].y);
                (*editors[group])[2]->setValue(values[group].z);
            }
            editWireLockRadius_->setChecked(
                namedWire.metadata.curveConstraints.radiusMillimeters.has_value());
            editArcRadius_->setValue(arc.radius);
            editArcStartAngle_->setValue(arc.startAngleRadians * 180.0 / kPi);
            editArcSweepAngle_->setValue(arc.sweepAngleRadians * 180.0 / kPi);
            const bool circle = namedWire.wire.Kind() == WireKind::Circle;
            editArcStartAngle_->setEnabled(!circle);
            editArcSweepAngle_->setEnabled(!circle);
            editWireGeometry_->setCurrentIndex(1);
        } else {
            editWireLockRadius_->setChecked(false);
            PopulateWirePointTable(namedWire);
            editWireGeometry_->setCurrentIndex(0);
        }
        editParameters_->setCurrentIndex(2);
        return;
    }

    editSelectionLabel_->setText(QStringLiteral("選択なし"));
    editParameters_->setCurrentIndex(0);
}

void MainWindow::PopulateWirePointTable(const kachakacha::model::NamedWire& namedWire)
{
    const auto& points = namedWire.wire.ControlPoints();
    editWirePointTable_->clearContents();
    editWirePointTable_->setRowCount(static_cast<int>(points.size()));

    QStringList rowLabels;
    if (namedWire.wire.Kind() == WireKind::Line) {
        rowLabels = {QStringLiteral("始点"), QStringLiteral("終点")};
    } else if (namedWire.wire.Kind() == WireKind::CubicBezier) {
        rowLabels = {QStringLiteral("始点"), QStringLiteral("制御点 1"), QStringLiteral("制御点 2"), QStringLiteral("終点")};
    } else if (namedWire.wire.Kind() == WireKind::CubicBSpline) {
        for (int index = 0; index < static_cast<int>(points.size()); ++index) {
            rowLabels.append(index == 0 ? QStringLiteral("始点")
                : index == static_cast<int>(points.size()) - 1 ? QStringLiteral("終点")
                : QStringLiteral("制御点 %1").arg(index));
        }
    } else {
        for (int index = 0; index < static_cast<int>(points.size()); ++index) {
            rowLabels.append(QStringLiteral("点 %1").arg(index + 1));
        }
    }
    editWirePointTable_->setVerticalHeaderLabels(rowLabels);

    for (int row = 0; row < static_cast<int>(points.size()); ++row) {
        const std::array<double, 3> values = {points[row].x, points[row].y, points[row].z};
        for (int column = 0; column < 3; ++column) {
            QDoubleSpinBox* field = MakeNumberField(values[column]);
            field->setFrame(false);
            editWirePointTable_->setCellWidget(row, column, field);
        }
    }
}

void MainWindow::MarkModified()
{
    if (!modified_) {
        modified_ = true;
        if (!windowTitle().endsWith('*')) {
            setWindowTitle(windowTitle() + " *");
        }
    }
}

QString MainWindow::AutosavePath() const
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("recovery.kcd"));
}

void MainWindow::WriteAutosave()
{
    if (!modified_) {
        return;
    }
    try {
        const QString path = AutosavePath();
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo fileInfo(path);
        if (!QDir().mkpath(fileInfo.absolutePath())) {
            throw std::runtime_error("自動保存先を作成できませんでした。");
        }
        std::ostringstream serialized;
        WriteProjectScript(serialized, project_);
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly)) {
            throw std::runtime_error(output.errorString().toUtf8().constData());
        }
        const QByteArray contents = QByteArray::fromStdString(serialized.str());
        if (output.write(contents) != contents.size() || !output.commit()) {
            throw std::runtime_error(output.errorString().toUtf8().constData());
        }
        statusBar()->showMessage(QStringLiteral("自動保存しました"), 1600);
    } catch (const std::exception& error) {
        statusBar()->showMessage(
            QStringLiteral("自動保存に失敗しました: %1").arg(QString::fromUtf8(error.what())), 5000);
    }
}

void MainWindow::OfferAutosaveRecovery()
{
    if (!currentPath_.isEmpty() || IsAutomationInvocation()) {
        return;
    }
    const QString path = AutosavePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("作業を復元"),
        QStringLiteral("前回の異常終了または未保存作業が見つかりました。\n直前の自動保存から復元しますか？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        RemoveAutosave();
        return;
    }

    try {
        const std::filesystem::path nativePath(path.toStdWString());
        std::ifstream input(nativePath);
        if (!input) {
            throw std::runtime_error("復元ファイルを開けませんでした。");
        }
        project_ = LoadProjectScript(input, nativePath.string());
        referenceWireName_.reset();
        currentPath_.clear();
        modified_ = true;
        undoStack_.clear();
        redoStack_.clear();
        UpdateHistoryActions();
        RefreshModelViews(true);
        toolsTabs_->setCurrentIndex(0);
        viewport_->SetIsometricView();
        setWindowTitle(QStringLiteral("kachakachaCAD - 復元した未保存作業 *"));
        statusBar()->showMessage(QStringLiteral("自動保存から作業を復元しました"), 5000);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("復元エラー"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::RemoveAutosave()
{
    const QString path = AutosavePath();
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
}

bool MainWindow::ConfirmDiscardChanges()
{
    if (!modified_) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("未保存の変更"),
        QStringLiteral("変更を保存しますか？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        SaveProject();
        return !modified_;
    }
    return true;
}

QString MainWindow::SuggestedPlaneName() const
{
    int number = static_cast<int>(project_.WorkPlanes().size()) + 1;
    while (project_.FindWorkPlane(QStringLiteral("plane_%1").arg(number).toStdString()).has_value()) {
        ++number;
    }
    return QStringLiteral("plane_%1").arg(number);
}

QString MainWindow::SuggestedWireName() const
{
    int number = static_cast<int>(project_.Wires().size()) + 1;
    const auto exists = [this](const std::string& name) {
        for (const auto& wire : project_.Wires()) {
            if (wire.name == name) {
                return true;
            }
        }
        return false;
    };
    while (exists(QStringLiteral("wire_%1").arg(number).toStdString())) {
        ++number;
    }
    return QStringLiteral("wire_%1").arg(number);
}

QString MainWindow::SuggestedDirectGroupName(const QString& prefix) const
{
    int number = 1;
    for (;;) {
        const QString candidate = QStringLiteral("%1_%2").arg(prefix).arg(number);
        const std::string exactName = candidate.toStdString();
        const std::string memberPrefix = exactName + "_";
        const bool exists = std::any_of(project_.Wires().begin(), project_.Wires().end(), [&](const auto& wire) {
            return wire.name == exactName || wire.name.starts_with(memberPrefix);
        }) || std::any_of(project_.Points().begin(), project_.Points().end(), [&](const auto& point) {
            return point.name == exactName || point.name.starts_with(memberPrefix);
        });
        if (!exists) {
            return candidate;
        }
        ++number;
    }
}

QString MainWindow::SuggestedChamferName() const
{
    int number = 1;
    const auto exists = [this](const std::string& name) {
        for (const auto& wire : project_.Wires()) {
            if (wire.name == name) {
                return true;
            }
        }
        return false;
    };
    while (exists(QStringLiteral("chamfer_%1").arg(number).toStdString())) {
        ++number;
    }
    return QStringLiteral("chamfer_%1").arg(number);
}

QString MainWindow::SuggestedFilletName() const
{
    int number = 1;
    const auto exists = [this](const std::string& name) {
        for (const auto& wire : project_.Wires()) {
            if (wire.name == name) {
                return true;
            }
        }
        return false;
    };
    while (exists(QStringLiteral("fillet_%1").arg(number).toStdString())) {
        ++number;
    }
    return QStringLiteral("fillet_%1").arg(number);
}

QString MainWindow::SuggestedSurfaceName() const
{
    int number = static_cast<int>(project_.Surfaces().size()) + 1;
    while (project_.FindSurface(ToName(QStringLiteral("surface_%1").arg(number))).has_value()) {
        ++number;
    }
    return QStringLiteral("surface_%1").arg(number);
}

QString MainWindow::SuggestedPlateName() const
{
    int number = static_cast<int>(project_.Plates().size()) + 1;
    while (project_.FindPlate(ToName(QStringLiteral("plate_%1").arg(number))).has_value()) {
        ++number;
    }
    return QStringLiteral("plate_%1").arg(number);
}

QString MainWindow::SuggestedBodyName() const
{
    int number = static_cast<int>(project_.Bodies().size()) + 1;
    while (project_.FindBody(ToName(QStringLiteral("jig_%1").arg(number))).has_value()) {
        ++number;
    }
    return QStringLiteral("jig_%1").arg(number);
}

QString MainWindow::SuggestedDimensionName() const
{
    int number = static_cast<int>(project_.ReferenceDimensions().size()) + 1;
    const auto exists = [this](const std::string& name) {
        return std::any_of(
            project_.ReferenceDimensions().begin(), project_.ReferenceDimensions().end(),
            [&](const ReferenceDimension& dimension) { return dimension.name == name; });
    };
    while (exists(ToName(QStringLiteral("dim_%1").arg(number)))) {
        ++number;
    }
    return QStringLiteral("dim_%1").arg(number);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (ConfirmDiscardChanges()) {
        if (!IsAutomationInvocation()) {
            RemoveAutosave();
        }
        event->accept();
    } else {
        event->ignore();
    }
}
