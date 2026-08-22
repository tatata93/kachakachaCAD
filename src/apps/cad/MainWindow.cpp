#include "MainWindow.h"

#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/model/Sketch.h"
#include "kachakacha/model/WireOperations.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDebug>
#include <QDoubleSpinBox>
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
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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
using kachakacha::io::WriteProjectScript;
using kachakacha::model::Project;
using kachakacha::model::Sketch;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;
using kachakacha::model::WireMetadata;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;
using kachakacha::model::ChamferIntersectingLines;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::MeetLinesAtIntersection;
using kachakacha::model::RetainedLineEnd;

namespace {

constexpr int kSelectionKindRole = Qt::UserRole;
constexpr int kSelectionIndexRole = Qt::UserRole + 1;
constexpr double kPi = 3.14159265358979323846;

QDoubleSpinBox* MakeNumberField(double value = 0.0)
{
    auto* field = new QDoubleSpinBox;
    field->setRange(-1000000.0, 1000000.0);
    field->setDecimals(4);
    field->setSingleStep(0.5);
    field->setValue(value);
    field->setKeyboardTracking(false);
    field->setMinimumWidth(72);
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
    viewport_->AlignToActiveWorkPlane();

    resize(1380, 820);
    setMinimumSize(1040, 650);
    setWindowTitle(QStringLiteral("kachakachaCAD - 無題"));
}

void MainWindow::BuildUi()
{
    viewport_ = new CadViewport;
    setCentralWidget(viewport_);
    viewport_->SetSelectionChangedCallback([this](const std::vector<CadSelection>& selections) {
        UpdateSelections(selections, true);
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
    viewport_->SetBezierCreatedCallback([this](const std::array<Vector3, 4>& points) {
        AddViewportBezier(points);
    });
    viewport_->SetTranslationRequestedCallback([this](Vector3 delta, bool copy) {
        ApplyViewportTranslation(delta, copy);
    });
    viewport_->SetMirrorRequestedCallback([this](Vector3 linePoint, Vector3 lineDirection, Vector3 planeNormal) {
        ApplyViewportMirror(linePoint, lineDirection, planeNormal);
    });
    BuildDrawingActions();
    viewport_->SetDrawingStateChangedCallback([this](ViewportTool tool, std::size_t pointCount) {
        UpdateDrawingPanel(tool, pointCount);
    });

    auto* modelDock = new QDockWidget(QStringLiteral("モデル"), this);
    modelDock->setObjectName("modelDock");
    modelDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    modelTree_ = new QTreeWidget;
    modelTree_->setHeaderHidden(true);
    modelTree_->setAlternatingRowColors(true);
    modelTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    modelTree_->header()->setStretchLastSection(true);
    modelDock->setWidget(modelTree_);
    addDockWidget(Qt::LeftDockWidgetArea, modelDock);
    modelDock->setMinimumWidth(220);

    connect(modelTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const QList<QTreeWidgetItem*> items = modelTree_->selectedItems();
        std::vector<CadSelection> selections;
        for (QTreeWidgetItem* item : items) {
            if (item->parent() == nullptr) {
                continue;
            }
            selections.push_back({
                static_cast<CadSelectionKind>(item->data(0, kSelectionKindRole).toInt()),
                item->data(0, kSelectionIndexRole).toInt(),
            });
        }
        QTreeWidgetItem* current = modelTree_->currentItem();
        if (current != nullptr && current->parent() != nullptr) {
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

    auto* toolsDock = new QDockWidget(QStringLiteral("作図と編集"), this);
    toolsDock->setObjectName("toolsDock");
    toolsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    toolsTabs_ = new QTabWidget;
    toolsTabs_->addTab(BuildDrawingPanel(), QStringLiteral("作図"));
    toolsTabs_->addTab(BuildPlanePanel(), QStringLiteral("作業平面"));
    toolsTabs_->addTab(BuildWirePanel(), QStringLiteral("数値入力"));
    toolsTabs_->addTab(BuildEditPanel(), QStringLiteral("編集"));
    toolsTabs_->addTab(BuildMachiningPanel(), QStringLiteral("加工"));
    toolsTabs_->addTab(BuildInfoPanel(), QStringLiteral("情報"));
    toolsDock->setWidget(toolsTabs_);
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
    lineToolAction_ = new QAction(QStringLiteral("直線"), this);
    polylineToolAction_ = new QAction(QStringLiteral("ポリライン"), this);
    rectangleToolAction_ = new QAction(QStringLiteral("矩形"), this);
    circleToolAction_ = new QAction(QStringLiteral("円"), this);
    arcToolAction_ = new QAction(QStringLiteral("3点円弧"), this);
    bezierToolAction_ = new QAction(QStringLiteral("ベジェ"), this);
    moveToolAction_ = new QAction(QStringLiteral("移動"), this);
    copyToolAction_ = new QAction(QStringLiteral("コピー"), this);
    mirrorToolAction_ = new QAction(QStringLiteral("ミラー複製"), this);
    meetLinesAction_ = new QAction(QStringLiteral("交点まで"), this);
    moveToolAction_->setToolTip(QStringLiteral("選択したワイヤーを基準点と移動先の2点で移動"));
    copyToolAction_->setToolTip(QStringLiteral("選択したワイヤーを基準点と移動先の2点で複製"));
    mirrorToolAction_->setToolTip(QStringLiteral("選択したワイヤーを作図面上の2点軸で反転複製"));
    meetLinesAction_->setToolTip(QStringLiteral("選択した2直線をトリムまたは延長して交点で合わせる"));

    selectToolAction_->setShortcut(Qt::Key_V);
    lineToolAction_->setShortcut(Qt::Key_L);
    polylineToolAction_->setShortcut(Qt::Key_P);
    rectangleToolAction_->setShortcut(Qt::Key_R);
    circleToolAction_->setShortcut(Qt::Key_C);
    arcToolAction_->setShortcut(Qt::Key_A);
    bezierToolAction_->setShortcut(Qt::Key_B);

    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    for (QAction* action : {
             selectToolAction_, lineToolAction_, polylineToolAction_, rectangleToolAction_,
             circleToolAction_, arcToolAction_, bezierToolAction_,
             moveToolAction_, copyToolAction_, mirrorToolAction_}) {
        action->setCheckable(true);
        toolGroup->addAction(action);
    }
    selectToolAction_->setChecked(true);

    connect(selectToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::Select); });
    connect(lineToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawLine); });
    connect(polylineToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawPolyline); });
    connect(rectangleToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawRectangle); });
    connect(circleToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawCircle); });
    connect(arcToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawArc); });
    connect(bezierToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::DrawBezier); });
    connect(moveToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::MoveSelection); });
    connect(copyToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::CopySelection); });
    connect(mirrorToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::MirrorSelection); });
    connect(meetLinesAction_, &QAction::triggered, this, &MainWindow::ApplyMeetSelectedLines);

    snapAction_ = new QAction(QStringLiteral("スナップ"), this);
    snapAction_->setCheckable(true);
    snapAction_->setChecked(true);
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
    addToolButton(bezierToolAction_, 3, 0, 2);
    layout->addLayout(toolGrid);

    auto* snapRow = new QWidget;
    auto* snapLayout = new QHBoxLayout(snapRow);
    snapLayout->setContentsMargins(0, 2, 0, 0);
    snapLayout->setSpacing(6);
    auto* snapButton = new QToolButton;
    snapButton->setDefaultAction(snapAction_);
    snapButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    snapLayout->addWidget(snapButton);
    snapLayout->addWidget(new QLabel(QStringLiteral("間隔")));
    snapStepField_ = new QDoubleSpinBox;
    snapStepField_->setRange(0.01, 1000.0);
    snapStepField_->setDecimals(2);
    snapStepField_->setSingleStep(0.5);
    snapStepField_->setValue(1.0);
    snapStepField_->setSuffix(QStringLiteral(" mm"));
    snapLayout->addWidget(snapStepField_, 1);
    layout->addWidget(snapRow);

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

    auto* addButton = new QPushButton(QStringLiteral("作業平面を追加"));
    addButton->setObjectName("primaryButton");
    connect(addButton, &QPushButton::clicked, this, &MainWindow::AddWorkPlane);
    layout->addWidget(addButton);
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

    auto* directLabel = new QLabel(QStringLiteral("3Dで直接変形"));
    directLabel->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(directLabel);
    auto* directGrid = new QGridLayout;
    directGrid->setContentsMargins(0, 0, 0, 0);
    directGrid->setHorizontalSpacing(6);
    directGrid->setVerticalSpacing(6);
    const auto addDirectButton = [&](QAction* action, int row, int column) {
        auto* button = new QToolButton;
        button->setObjectName("drawingToolButton");
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        directGrid->addWidget(button, row, column);
    };
    addDirectButton(moveToolAction_, 0, 0);
    addDirectButton(copyToolAction_, 0, 1);
    addDirectButton(mirrorToolAction_, 1, 0);
    addDirectButton(meetLinesAction_, 1, 1);
    layout->addLayout(directGrid);

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
    wireLayout->addLayout(metadataForm);

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
    editArcRadius_ = MakePositiveField(1.0);
    editArcRadius_->setSuffix(QStringLiteral(" mm"));
    editArcStartAngle_ = MakeNumberField(0.0);
    editArcStartAngle_->setSuffix(QStringLiteral(" °"));
    editArcSweepAngle_ = MakeNumberField(90.0);
    editArcSweepAngle_->setRange(-360.0, 360.0);
    editArcSweepAngle_->setSuffix(QStringLiteral(" °"));
    arcForm->addRow(QStringLiteral("中心"), MakeVector3Editor(editArcCenter_));
    arcForm->addRow(QStringLiteral("円の X 軸"), MakeVector3Editor(editArcUAxis_, {1.0, 0.0, 0.0}));
    arcForm->addRow(QStringLiteral("円の Y 軸"), MakeVector3Editor(editArcVAxis_, {0.0, 1.0, 0.0}));
    arcForm->addRow(QStringLiteral("半径"), editArcRadius_);
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
    return panel;
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

QWidget* MainWindow::BuildInfoPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    infoLabel_ = new QLabel(QStringLiteral("選択なし"));
    infoLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    infoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);
    layout->addStretch(1);
    return panel;
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

    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    deleteAction->setShortcut(QKeySequence::Delete);
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_->setShortcut(QKeySequence::Redo);

    connect(newAction, &QAction::triggered, this, &MainWindow::NewProject);
    connect(openAction, &QAction::triggered, this, &MainWindow::OpenProject);
    connect(saveAction, &QAction::triggered, this, &MainWindow::SaveProject);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::SaveProjectAs);
    connect(fitAction, &QAction::triggered, viewport_, &CadViewport::FitAll);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::DeleteSelection);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::Undo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::Redo);

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
    editMenu->addAction(mirrorToolAction_);
    editMenu->addAction(meetLinesAction_);
    editMenu->addSeparator();
    editMenu->addAction(deleteAction);
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("表示"));
    viewMenu->addAction(fitAction);

    QMenu* drawMenu = menuBar()->addMenu(QStringLiteral("作図"));
    drawMenu->addAction(selectToolAction_);
    drawMenu->addAction(lineToolAction_);
    drawMenu->addAction(polylineToolAction_);
    drawMenu->addAction(rectangleToolAction_);
    drawMenu->addAction(circleToolAction_);
    drawMenu->addAction(arcToolAction_);
    drawMenu->addAction(bezierToolAction_);
    drawMenu->addSeparator();
    drawMenu->addAction(finishDrawingAction_);
    drawMenu->addAction(cancelDrawingAction_);

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
    drawingToolbar->addSeparator();
    drawingToolbar->addAction(finishDrawingAction_);
    drawingToolbar->addAction(cancelDrawingAction_);

    QToolBar* transformToolbar = addToolBar(QStringLiteral("直接変形"));
    transformToolbar->setMovable(false);
    transformToolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    transformToolbar->addAction(moveToolAction_);
    transformToolbar->addAction(copyToolAction_);
    transformToolbar->addAction(mirrorToolAction_);
    transformToolbar->addAction(meetLinesAction_);
    UpdateHistoryActions();
}

void MainWindow::NewProject()
{
    if (!ConfirmDiscardChanges()) {
        return;
    }

    project_ = Project{};
    project_.AddWorkPlane("top_XY", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
    project_.AddWorkPlane("front_XZ", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}));
    project_.AddWorkPlane("side_YZ", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    currentPath_.clear();
    modified_ = false;
    undoStack_.clear();
    redoStack_.clear();
    UpdateHistoryActions();
    RefreshModelViews(true);
    toolsTabs_->setCurrentIndex(0);
    SetViewportTool(ViewportTool::DrawLine);
    viewport_->AlignToActiveWorkPlane();
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
        currentPath_ = path;
        modified_ = false;
        undoStack_.clear();
        redoStack_.clear();
        UpdateHistoryActions();
        RefreshModelViews(true);
        toolsTabs_->setCurrentIndex(0);
        SetViewportTool(ViewportTool::DrawLine);
        viewport_->AlignToActiveWorkPlane();
        setWindowTitle(QStringLiteral("kachakachaCAD - %1").arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("読み込みました: %1").arg(path), 4000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("読み込みエラー"), QString::fromUtf8(error.what()));
        return false;
    }
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
        setWindowTitle(QStringLiteral("kachakachaCAD - %1").arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("保存しました: %1").arg(path), 4000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("保存エラー"), QString::fromUtf8(error.what()));
        return false;
    }
}

void MainWindow::SetViewportTool(ViewportTool tool)
{
    const bool isTransform = tool == ViewportTool::MoveSelection
        || tool == ViewportTool::CopySelection
        || tool == ViewportTool::MirrorSelection;
    if (tool != ViewportTool::Select) {
        const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(activePlaneCombo_->currentText()));
        if (!plane.has_value()) {
            selectToolAction_->setChecked(true);
            viewport_->SetTool(ViewportTool::Select);
            statusBar()->showMessage(QStringLiteral("作図する平面を選択してください"), 3000);
            return;
        }
    }
    if (isTransform) {
        const bool hasSelectedWire = std::any_of(
            viewport_->Selections().begin(), viewport_->Selections().end(), [this](const CadSelection& selection) {
                return selection.kind == CadSelectionKind::Wire && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Wires().size());
            });
        if (!hasSelectedWire) {
            selectToolAction_->setChecked(true);
            viewport_->SetTool(ViewportTool::Select);
            statusBar()->showMessage(QStringLiteral("3D画面で変形するワイヤーを選択してください"), 3500);
            return;
        }
        toolsTabs_->setCurrentIndex(3);
    }

    viewport_->SetTool(tool);
    selectToolAction_->setChecked(tool == ViewportTool::Select);
    lineToolAction_->setChecked(tool == ViewportTool::DrawLine);
    polylineToolAction_->setChecked(tool == ViewportTool::DrawPolyline);
    rectangleToolAction_->setChecked(tool == ViewportTool::DrawRectangle);
    circleToolAction_->setChecked(tool == ViewportTool::DrawCircle);
    arcToolAction_->setChecked(tool == ViewportTool::DrawArc);
    bezierToolAction_->setChecked(tool == ViewportTool::DrawBezier);
    moveToolAction_->setChecked(tool == ViewportTool::MoveSelection);
    copyToolAction_->setChecked(tool == ViewportTool::CopySelection);
    mirrorToolAction_->setChecked(tool == ViewportTool::MirrorSelection);
    switch (tool) {
    case ViewportTool::Select:
        statusBar()->showMessage(QStringLiteral("選択モード"), 2500);
        break;
    case ViewportTool::DrawLine:
        statusBar()->showMessage(QStringLiteral("直線作図モード"), 2500);
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
        statusBar()->showMessage(QStringLiteral("3点円弧作図モード"), 2500);
        break;
    case ViewportTool::DrawBezier:
        statusBar()->showMessage(QStringLiteral("ベジェ曲線作図モード"), 2500);
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
    }
}

void MainWindow::UpdateDrawingPanel(ViewportTool tool, std::size_t pointCount)
{
    QString state;
    switch (tool) {
    case ViewportTool::Select:
        state = QStringLiteral("選択");
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
        state = QStringLiteral("3点円弧 · %1  %2 / 3点")
            .arg(pointCount == 0 ? QStringLiteral("始点") : pointCount == 1 ? QStringLiteral("通過点") : QStringLiteral("終点"))
            .arg(pointCount);
        break;
    case ViewportTool::DrawBezier:
        state = QStringLiteral("ベジェ曲線 · %1  %2 / 4点")
            .arg(pointCount == 0 ? QStringLiteral("始点")
                : pointCount == 1 ? QStringLiteral("制御点1")
                : pointCount == 2 ? QStringLiteral("制御点2")
                                  : QStringLiteral("終点"))
            .arg(pointCount);
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
    }
    if (drawingStateLabel_ != nullptr) {
        drawingStateLabel_->setText(state);
    }
    if (finishDrawingAction_ != nullptr) {
        finishDrawingAction_->setEnabled(tool == ViewportTool::DrawPolyline && pointCount >= 2);
    }
    if (cancelDrawingAction_ != nullptr) {
        cancelDrawingAction_->setEnabled(pointCount > 0);
    }
}

void MainWindow::RefreshActiveWorkPlane()
{
    if (activePlaneCombo_ == nullptr) {
        return;
    }
    const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(activePlaneCombo_->currentText()));
    viewport_->SetActiveWorkPlane(plane);
    const bool canDraw = plane.has_value();
    lineToolAction_->setEnabled(canDraw);
    polylineToolAction_->setEnabled(canDraw);
    rectangleToolAction_->setEnabled(canDraw);
    circleToolAction_->setEnabled(canDraw);
    arcToolAction_->setEnabled(canDraw);
    bezierToolAction_->setEnabled(canDraw);
    moveToolAction_->setEnabled(canDraw);
    copyToolAction_->setEnabled(canDraw);
    mirrorToolAction_->setEnabled(canDraw);
    if (!canDraw && viewport_->Tool() != ViewportTool::Select) {
        SetViewportTool(ViewportTool::Select);
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
            if (source.metadata.planePolicy == WirePlanePolicy::LockedToPlane) {
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
            if (source.metadata.planePolicy == WirePlanePolicy::LockedToPlane) {
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
            project_.AddWire(ToName(name), mirrored[index], sources[index].metadata);
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
        project_.UpdateWire(first.name, result.first);
        project_.UpdateWire(second.name, result.second);
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
        return false;
    };
    const std::size_t initialPlaneCount = project_.WorkPlanes().size();
    const std::size_t initialWireCount = project_.Wires().size();
    if (toolsTabs_->count() != 6
        || toolsTabs_->tabText(0) != QStringLiteral("作図")
        || activePlaneCombo_->count() == 0) {
        return fail("drawing workbench is primary");
    }

    planeName_->setText("__ui_test_plane");
    planeMethod_->setCurrentIndex(0);
    standardPlane_->setCurrentIndex(1);
    planeOffset_->setValue(2.5);
    planeTilt_->setValue(12.0);
    AddWorkPlane();
    if (project_.WorkPlanes().size() != initialPlaneCount + 1 || !project_.FindWorkPlane("__ui_test_plane").has_value()) {
        return fail("add workplane");
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
    if (drawingPlaneIndex < 0 && activePlaneCombo_->count() > 0) {
        drawingPlaneIndex = 0;
    }
    if (drawingPlaneIndex < 0) {
        return fail("find drawing workplane");
    }
    activePlaneCombo_->setCurrentIndex(drawingPlaneIndex);
    const std::string drawingPlaneName = ToName(activePlaneCombo_->currentText());
    viewport_->AlignToActiveWorkPlane();
    snapAction_->setChecked(true);
    snapStepField_->setValue(1.0);

    const auto sendMouse = [this](QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const QPointF globalPosition(viewport_->mapToGlobal(position.toPoint()));
        QMouseEvent event(type, position, globalPosition, button, buttons, Qt::NoModifier);
        QApplication::sendEvent(viewport_, &event);
    };
    const auto click = [&](QPointF position) {
        sendMouse(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
        sendMouse(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    };
    const auto drag = [&](QPointF start, QPointF end) {
        sendMouse(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
        sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    };
    const QPointF center(viewport_->width() * 0.5, viewport_->height() * 0.5);
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

    std::ostringstream directDrawingScript;
    WriteProjectScript(directDrawingScript, project_);
    std::istringstream directDrawingInput(directDrawingScript.str());
    const Project reloadedProject = LoadProjectScript(directDrawingInput, "direct-editing-self-test");
    if (reloadedProject.Wires().size() != project_.Wires().size()
        || reloadedProject.Wires()[directStart + 5].wire.Kind() != WireKind::CubicBezier
        || reloadedProject.Wires()[mirrorStart].metadata.sourcePlaneName != drawingPlaneName
        || !kachakacha::geometry::AlmostEqual(reloadedProject.Wires()[meetStart].wire.End(), expectedIntersection)) {
        return fail("save and reload direct editing");
    }

    toolsTabs_->setCurrentIndex(3);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(mirrorStart)},
        {CadSelectionKind::Wire, static_cast<int>(mirrorStart + 1)},
    }, true);
    SetViewportTool(ViewportTool::MoveSelection);
    click(center + QPointF(-25.0, -135.0));
    sendMouse(QEvent::MouseMove, center + QPointF(45.0, -105.0), Qt::NoButton, Qt::NoButton);
    return true;
}

void MainWindow::AddWorkPlane()
{
    try {
        ValidateObjectName(planeName_->text());
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
            const WorkPlane replacement = WorkPlane::FromOriginAxes(
                ReadVector3(editPlaneOrigin_),
                ReadVector3(editPlaneUAxis_),
                ReadVector3(editPlaneNormal_));
            RecordUndo();
            project_.UpdateWorkPlane(namedPlane.name, replacement);
        } else if (selection.kind == CadSelectionKind::Wire
            && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            const auto& namedWire = project_.Wires()[selection.index];
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

            WireMetadata metadata;
            if (editWireSourcePlane_->currentIndex() > 0) {
                metadata.sourcePlaneName = ToName(editWireSourcePlane_->currentText());
                if (!project_.FindWorkPlane(*metadata.sourcePlaneName).has_value()) {
                    throw std::invalid_argument("作成元平面を選択してください。");
                }
            }
            metadata.planePolicy = static_cast<WirePlanePolicy>(editWirePolicy_->currentIndex());

            RecordUndo();
            project_.UpdateWire(namedWire.name, *replacement);
            project_.SetWireMetadata(namedWire.name, metadata);
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

        RecordUndo();
        project_.UpdateWire(first.name, result.trimmedFirst);
        project_.UpdateWire(second.name, result.trimmedSecond);
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

        RecordUndo();
        project_.UpdateWire(first.name, result.trimmedFirst);
        project_.UpdateWire(second.name, result.trimmedSecond);
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
    if (selection.kind == CadSelectionKind::WorkPlane && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
        name = ToQString(project_.WorkPlanes()[selection.index].name);
        detail = QStringLiteral("作業平面を削除します。平面から作ったワイヤーは3D形状として残ります。");
    } else if (selection.kind == CadSelectionKind::Wire && selection.index < static_cast<int>(project_.Wires().size())) {
        name = ToQString(project_.Wires()[selection.index].name);
        detail = QStringLiteral("ワイヤーを削除します。");
    } else {
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("削除"), QStringLiteral("%1\n\n%2").arg(name, detail)) != QMessageBox::Yes) {
        return;
    }
    if (selection.kind == CadSelectionKind::WorkPlane) {
        RecordUndo();
        project_.RemoveWorkPlane(ToName(name));
    } else {
        RecordUndo();
        project_.RemoveWire(ToName(name));
    }
    MarkModified();
    RefreshModelViews(false);
    statusBar()->showMessage(QStringLiteral("削除しました: %1").arg(name), 3000);
}

void MainWindow::RefreshModelViews(bool fitView)
{
    modelTree_->blockSignals(true);
    modelTree_->clear();
    auto* planeRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("作業平面 (%1)").arg(project_.WorkPlanes().size())});
    for (int index = 0; index < static_cast<int>(project_.WorkPlanes().size()); ++index) {
        auto* item = new QTreeWidgetItem(planeRoot, {ToQString(project_.WorkPlanes()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::WorkPlane));
        item->setData(0, kSelectionIndexRole, index);
    }
    auto* wireRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("ワイヤー (%1)").arg(project_.Wires().size())});
    for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
        auto* item = new QTreeWidgetItem(wireRoot, {ToQString(project_.Wires()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::Wire));
        item->setData(0, kSelectionIndexRole, index);
    }
    planeRoot->setExpanded(true);
    wireRoot->setExpanded(true);
    modelTree_->blockSignals(false);

    RefreshPlaneChoices();
    RefreshWireChoices();
    viewport_->SetProject(&project_, fitView);
    RefreshActiveWorkPlane();
    UpdateSelection({}, false);
}

void MainWindow::RefreshPlaneChoices()
{
    const auto refresh = [this](QComboBox* combo) {
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
    refresh(activePlaneCombo_);

    const QString previousEditSource = editWireSourcePlane_->currentText();
    editWireSourcePlane_->clear();
    editWireSourcePlane_->addItem(QStringLiteral("なし"));
    for (const auto& plane : project_.WorkPlanes()) {
        editWireSourcePlane_->addItem(ToQString(plane.name));
    }
    const int previousEditIndex = editWireSourcePlane_->findText(previousEditSource);
    editWireSourcePlane_->setCurrentIndex(previousEditIndex >= 0 ? previousEditIndex : 0);
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
                return item->data(0, kSelectionKindRole).toInt() == static_cast<int>(selection.kind)
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
    } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0 && selection.index < static_cast<int>(project_.Wires().size())) {
        const auto& named = project_.Wires()[selection.index];
        const QString source = named.metadata.sourcePlaneName.has_value() ? ToQString(*named.metadata.sourcePlaneName) : QStringLiteral("なし");
        infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: %2<br>平面との関係: %3<br>作成元平面: %4<br><br>始点<br>%5<br><br>終点<br>%6")
            .arg(ToQString(named.name), WireKindText(named.wire.Kind()), PolicyText(named.metadata.planePolicy), source, VectorText(named.wire.Start()), VectorText(named.wire.End())));
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
            label->setText(QStringLiteral("選択なし"));
        }
        editApplyButton_->setEnabled(selection.kind != CadSelectionKind::None);
    }
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
        if (namedWire.metadata.sourcePlaneName.has_value()) {
            const int sourceIndex = editWireSourcePlane_->findText(ToQString(*namedWire.metadata.sourcePlaneName));
            editWireSourcePlane_->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : 0);
        } else {
            editWireSourcePlane_->setCurrentIndex(0);
        }
        editWirePolicy_->setCurrentIndex(static_cast<int>(namedWire.metadata.planePolicy));

        if (namedWire.wire.Kind() == WireKind::Circle || namedWire.wire.Kind() == WireKind::CircularArc) {
            const auto arc = namedWire.wire.ArcData();
            const std::array<Vector3, 3> values = {arc.center, arc.uAxis, arc.vAxis};
            const std::array<std::array<QDoubleSpinBox*, 3>*, 3> editors = {&editArcCenter_, &editArcUAxis_, &editArcVAxis_};
            for (int group = 0; group < 3; ++group) {
                (*editors[group])[0]->setValue(values[group].x);
                (*editors[group])[1]->setValue(values[group].y);
                (*editors[group])[2]->setValue(values[group].z);
            }
            editArcRadius_->setValue(arc.radius);
            editArcStartAngle_->setValue(arc.startAngleRadians * 180.0 / kPi);
            editArcSweepAngle_->setValue(arc.sweepAngleRadians * 180.0 / kPi);
            const bool circle = namedWire.wire.Kind() == WireKind::Circle;
            editArcStartAngle_->setEnabled(!circle);
            editArcSweepAngle_->setEnabled(!circle);
            editWireGeometry_->setCurrentIndex(1);
        } else {
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

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (ConfirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}
