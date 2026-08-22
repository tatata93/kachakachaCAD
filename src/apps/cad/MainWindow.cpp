#include "MainWindow.h"

#include "kachakacha/io/PlanarExport.h"
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
#include <QScrollArea>
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
using kachakacha::io::WireLiesOnWorkPlane;
using kachakacha::io::WritePlanarDxf;
using kachakacha::io::WritePlanarSvg;
using kachakacha::io::WriteProjectScript;
using kachakacha::model::Project;
using kachakacha::model::PlateDevelopability;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Sketch;
using kachakacha::model::SurfaceKind;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;
using kachakacha::model::WireMetadata;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;
using kachakacha::model::ChamferIntersectingLines;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::JoinLineChain;
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
    viewport_->SetIsometricView();

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
    viewport_->SetRotationRequestedCallback([this](Vector3 axisPoint, Vector3 axisDirection, double angleRadians) {
        ApplyViewportRotation(axisPoint, axisDirection, angleRadians);
    });
    viewport_->SetSplitRequestedCallback([this](int wireIndex, double parameter) {
        ApplySplitWire(wireIndex, parameter);
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
        } else if (kind == CadSelectionKind::Wire && index >= 0 && index < static_cast<int>(project_.Wires().size())) {
            currentVisibility = project_.Wires()[index].visible;
        } else if (kind == CadSelectionKind::Surface && index >= 0 && index < static_cast<int>(project_.Surfaces().size())) {
            currentVisibility = project_.Surfaces()[index].visible;
        } else if (kind == CadSelectionKind::Plate && index >= 0 && index < static_cast<int>(project_.Plates().size())) {
            currentVisibility = project_.Plates()[index].visible;
        } else {
            return;
        }
        if (currentVisibility == visible) {
            return;
        }

        RecordUndo();
        if (kind == CadSelectionKind::WorkPlane) {
            project_.SetWorkPlaneVisible(project_.WorkPlanes()[index].name, visible);
        } else if (kind == CadSelectionKind::Wire) {
            project_.SetWireVisible(project_.Wires()[index].name, visible);
        } else if (kind == CadSelectionKind::Surface) {
            project_.SetSurfaceVisible(project_.Surfaces()[index].name, visible);
        } else {
            project_.SetPlateVisible(project_.Plates()[index].name, visible);
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
    toolsTabs_ = new QTabWidget;
    toolsTabs_->addTab(BuildDrawingPanel(), QStringLiteral("作図"));
    toolsTabs_->addTab(BuildPlanePanel(), QStringLiteral("作業平面"));
    toolsTabs_->addTab(BuildWirePanel(), QStringLiteral("数値入力"));
    toolsTabs_->addTab(BuildEditPanel(), QStringLiteral("編集"));
    toolsTabs_->addTab(BuildMachiningPanel(), QStringLiteral("加工"));
    toolsTabs_->addTab(BuildSurfacePanel(), QStringLiteral("面"));
    toolsTabs_->addTab(BuildOutputPanel(), QStringLiteral("出力"));
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
    rotateToolAction_ = new QAction(QStringLiteral("回転"), this);
    splitToolAction_ = new QAction(QStringLiteral("分割"), this);
    joinWiresAction_ = new QAction(QStringLiteral("結合"), this);
    meetLinesAction_ = new QAction(QStringLiteral("交点まで"), this);
    setReferenceAction_ = new QAction(QStringLiteral("基準線に設定"), this);
    clearReferenceAction_ = new QAction(QStringLiteral("基準解除"), this);
    moveToolAction_->setToolTip(QStringLiteral("選択したワイヤーを基準点と移動先の2点で移動"));
    copyToolAction_->setToolTip(QStringLiteral("選択したワイヤーを基準点と移動先の2点で複製"));
    mirrorToolAction_->setToolTip(QStringLiteral("選択したワイヤーを作図面上の2点軸で反転複製"));
    rotateToolAction_->setToolTip(QStringLiteral("中心・角度基準・回転先の3点で選択ワイヤーを回転"));
    splitToolAction_->setToolTip(QStringLiteral("選択した1本のワイヤーをクリック位置で分割"));
    joinWiresAction_->setToolTip(QStringLiteral("端点がつながる直線・ポリラインを1本へ結合"));
    meetLinesAction_->setToolTip(QStringLiteral("選択した2直線をトリムまたは延長して交点で合わせる"));
    setReferenceAction_->setToolTip(QStringLiteral("選択した1本の直線を変形や平面作成の基準線にする"));
    clearReferenceAction_->setToolTip(QStringLiteral("現在の基準線を解除する"));
    clearReferenceAction_->setEnabled(false);

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
             moveToolAction_, copyToolAction_, mirrorToolAction_, rotateToolAction_, splitToolAction_}) {
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
    connect(rotateToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::RotateSelection); });
    connect(splitToolAction_, &QAction::triggered, this, [this] { SetViewportTool(ViewportTool::SplitWire); });
    connect(joinWiresAction_, &QAction::triggered, this, &MainWindow::JoinSelectedWires);
    connect(meetLinesAction_, &QAction::triggered, this, &MainWindow::ApplyMeetSelectedLines);
    connect(setReferenceAction_, &QAction::triggered, this, &MainWindow::SetReferenceFromSelection);
    connect(clearReferenceAction_, &QAction::triggered, this, &MainWindow::ClearReference);

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
    addDirectButton(meetLinesAction_, 3, 0, 2);
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

QWidget* MainWindow::BuildSurfacePanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* createTitle = new QLabel(QStringLiteral("ワイヤーから面"));
    createTitle->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(createTitle);

    auto* createForm = new QFormLayout;
    createForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    surfaceType_ = new QComboBox;
    surfaceType_->addItems({
        QStringLiteral("閉じた1本から平面"),
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

    auto* plateTitle = new QLabel(QStringLiteral("面を板材にする"));
    plateTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(plateTitle);

    auto* plateForm = new QFormLayout;
    plateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    plateName_ = new QLineEdit(QStringLiteral("plate_1"));
    plateSurface_ = new QComboBox;
    plateThickness_ = MakePositiveField(0.5);
    plateThickness_->setSuffix(QStringLiteral(" mm"));
    plateDirection_ = new QComboBox;
    plateDirection_->addItem(QStringLiteral("面から外側へ"), static_cast<int>(PlateThicknessDirection::Positive));
    plateDirection_->addItem(QStringLiteral("面を中央に"), static_cast<int>(PlateThicknessDirection::Centered));
    plateDirection_->addItem(QStringLiteral("面から内側へ"), static_cast<int>(PlateThicknessDirection::Negative));
    plateMaterial_ = new QComboBox;
    plateMaterial_->addItem(QStringLiteral("プラ板"), QStringLiteral("styrene"));
    plateMaterial_->addItem(QStringLiteral("紙・厚紙"), QStringLiteral("paper"));
    plateMaterial_->addItem(QStringLiteral("真鍮板"), QStringLiteral("brass"));
    plateMaterial_->addItem(QStringLiteral("その他"), QStringLiteral("other"));
    plateForm->addRow(QStringLiteral("板の名前"), plateName_);
    plateForm->addRow(QStringLiteral("元の面"), plateSurface_);
    plateForm->addRow(QStringLiteral("板厚"), plateThickness_);
    plateForm->addRow(QStringLiteral("厚み方向"), plateDirection_);
    plateForm->addRow(QStringLiteral("材質"), plateMaterial_);
    layout->addLayout(plateForm);

    auto* plateButton = new QPushButton(QStringLiteral("この面を板材にする"));
    plateButton->setObjectName("primaryButton");
    connect(plateButton, &QPushButton::clicked, this, &MainWindow::CreatePlateFromSurface);
    layout->addWidget(plateButton);

    auto* plateUpdateButton = new QPushButton(QStringLiteral("選択中の板材へ設定"));
    connect(plateUpdateButton, &QPushButton::clicked, this, &MainWindow::UpdateSelectedPlate);
    layout->addWidget(plateUpdateButton);

    auto* openingTitle = new QLabel(QStringLiteral("板材に開口"));
    openingTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(openingTitle);
    plateOpeningSelectionLabel_ = new QLabel(QStringLiteral("選択: 板材0枚 / 閉じた投影輪郭0本"));
    plateOpeningSelectionLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(plateOpeningSelectionLabel_);

    auto* openingButtons = new QWidget;
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

    auto* title = new QLabel(QStringLiteral("1:1 板材図面"));
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
    auto* dxfButton = new QPushButton(QStringLiteral("DXFを保存"));
    connect(svgButton, &QPushButton::clicked, this, [this] { ExportPlanar(false); });
    connect(dxfButton, &QPushButton::clicked, this, [this] { ExportPlanar(true); });
    connect(exportPlane_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    connect(exportScope_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    layout->addWidget(svgButton);
    layout->addWidget(dxfButton);
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
    hideSelectedAction_ = new QAction(QStringLiteral("隠す"), this);
    showAllObjectsAction_ = new QAction(QStringLiteral("全て表示"), this);

    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    deleteAction->setShortcut(QKeySequence::Delete);
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_->setShortcut(QKeySequence::Redo);
    hideSelectedAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    showAllObjectsAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    hideSelectedAction_->setToolTip(QStringLiteral("選択した作業平面・ワイヤー・面・板材を隠す"));
    showAllObjectsAction_->setToolTip(QStringLiteral("隠した作業平面・ワイヤー・面・板材をすべて表示"));

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
    editMenu->addAction(joinWiresAction_);
    editMenu->addAction(meetLinesAction_);
    editMenu->addAction(setReferenceAction_);
    editMenu->addAction(clearReferenceAction_);
    editMenu->addSeparator();
    editMenu->addAction(deleteAction);
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("表示"));
    viewMenu->addAction(fitAction);
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
        referenceWireName_.reset();
        currentPath_ = path;
        modified_ = false;
        undoStack_.clear();
        redoStack_.clear();
        UpdateHistoryActions();
        RefreshModelViews(true);
        toolsTabs_->setCurrentIndex(0);
        SetViewportTool(ViewportTool::DrawLine);
        viewport_->SetIsometricView();
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
                if (WireLiesOnWorkPlane(wire.wire, *plane)) {
                    wires.push_back(wire);
                }
            }
        } else {
            for (const CadSelection& selection : viewport_->Selections()) {
                if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                    && selection.index < static_cast<int>(project_.Wires().size())) {
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
    if (tool != ViewportTool::Select && !isSplit) {
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
    rotateToolAction_->setChecked(tool == ViewportTool::RotateSelection);
    splitToolAction_->setChecked(tool == ViewportTool::SplitWire);
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
    case ViewportTool::RotateSelection:
        statusBar()->showMessage(QStringLiteral("回転: 中心、角度の基準点、回転先の順に3D画面で指定"), 4500);
        break;
    case ViewportTool::SplitWire:
        statusBar()->showMessage(QStringLiteral("分割: 選択したワイヤー上の分けたい位置をクリック"), 4000);
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
    lineToolAction_->setEnabled(canDraw);
    polylineToolAction_->setEnabled(canDraw);
    rectangleToolAction_->setEnabled(canDraw);
    circleToolAction_->setEnabled(canDraw);
    arcToolAction_->setEnabled(canDraw);
    bezierToolAction_->setEnabled(canDraw);
    moveToolAction_->setEnabled(canDraw);
    copyToolAction_->setEnabled(canDraw);
    mirrorToolAction_->setEnabled(canDraw);
    rotateToolAction_->setEnabled(canDraw);
    splitToolAction_->setEnabled(!project_.Wires().empty());
    joinWiresAction_->setEnabled(project_.Wires().size() >= 2);
    RefreshReference();
    if (!canDraw && viewport_->Tool() != ViewportTool::Select && viewport_->Tool() != ViewportTool::SplitWire) {
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
            if (source.metadata.planePolicy == WirePlanePolicy::LockedToPlane) {
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
            project_.UpdateWire(sources[index].name, rotated[index]);
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
        project_.AddWire(firstName, parts.first, source.metadata);
        const int firstIndex = static_cast<int>(project_.Wires().size() - 1);
        project_.AddWire(secondName, parts.second, source.metadata);
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
            if (source.wire.Kind() != WireKind::Line && source.wire.Kind() != WireKind::Polyline) {
                throw std::invalid_argument("結合できるのは直線とポリラインです。");
            }
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
        project_.AddWire(ToName(name), joined, sources.front().metadata);
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
        if ((surfaceMode == 0 && wireIndices.size() != 1)
            || (surfaceMode == 1 && wireIndices.size() != 2)
            || (surfaceMode == 2 && wireIndices.size() < 3)) {
            if (surfaceMode == 0) {
                throw std::invalid_argument("閉じたワイヤーを1本だけ選択してください。");
            }
            if (surfaceMode == 1) {
                throw std::invalid_argument("断面ワイヤーを2本選択してください。");
            }
            throw std::invalid_argument("車体の前から後ろの順に、断面ワイヤーを3本以上選択してください。");
        }

        Project candidate = project_;
        const std::string name = ToName(surfaceName_->text());
        if (surfaceMode == 0) {
            candidate.AddPlanarSurface(name, candidate.Wires()[wireIndices[0]].name);
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
            ? QStringLiteral("閉じたワイヤーから平面を作成しました")
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
    if (toolsTabs_->count() != 8
        || toolsTabs_->tabText(0) != QStringLiteral("作図")
        || toolsTabs_->tabText(5) != QStringLiteral("面")
        || toolsTabs_->tabText(6) != QStringLiteral("出力")
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
    const QPointF cubeTop(viewport_->width() - 60.0, 24.0);
    const QPointF cube3d(viewport_->width() - 60.0, 96.0);
    const std::size_t beforeViewCube = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawLine);
    click(cubeTop);
    if (!kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), {0.0, 0.0, 1.0}, 1.0e-8)) {
        return fail("view cube top face");
    }
    click(cube3d);
    const Vector3 beforeCubeDrag = viewport_->ViewDirection();
    drag(QPointF(viewport_->width() - 60.0, 50.0), QPointF(viewport_->width() - 42.0, 40.0));
    if (kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), beforeCubeDrag, 1.0e-8)
        || project_.Wires().size() != beforeViewCube
        || viewport_->DrawingPointCount() != 0) {
        return fail("view cube drag without drawing");
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

    const std::size_t surfaceWireStart = project_.Wires().size();
    const std::size_t surfaceStart = project_.Surfaces().size();
    const std::size_t plateStart = project_.Plates().size();
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

    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart)}, true);
    plateThickness_->setValue(0.7);
    plateDirection_->setCurrentIndex(0);
    plateMaterial_->setCurrentIndex(1);
    UpdateSelectedPlate();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.7) > 1.0e-12
        || project_.Plates()[plateStart].plate.Direction() != PlateThicknessDirection::Positive
        || project_.Plates()[plateStart].material != "paper") {
        return fail("update selected plate properties");
    }
    Undo();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.5) > 1.0e-12) {
        return fail("undo plate property update");
    }
    Redo();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.7) > 1.0e-12) {
        return fail("redo plate property update");
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

    std::ostringstream directDrawingScript;
    WriteProjectScript(directDrawingScript, project_);
    std::istringstream directDrawingInput(directDrawingScript.str());
    const Project reloadedProject = LoadProjectScript(directDrawingInput, "direct-editing-self-test");
    if (reloadedProject.Wires().size() != project_.Wires().size()
        || reloadedProject.Wires()[directStart + 5].wire.Kind() != WireKind::CubicBezier
        || reloadedProject.Wires()[mirrorStart].metadata.sourcePlaneName != drawingPlaneName
        || reloadedProject.Wires()[referenceMirrorStart].wire.Kind() != WireKind::Circle
        || reloadedProject.Wires()[splitStart].wire.Kind() != WireKind::Polyline
        || reloadedProject.Surfaces().size() != project_.Surfaces().size()
        || reloadedProject.Surfaces()[surfaceStart].surface.Kind() != SurfaceKind::Loft
        || reloadedProject.Plates().size() != project_.Plates().size()
        || reloadedProject.Plates()[plateStart].sourceSurfaceName != "__ui_nose_skin"
        || reloadedProject.Plates()[plateStart].openingWireNames
            != std::vector<std::string>{projectedLightName}
        || !reloadedProject.Wires()[projectedLightIndex].projection.has_value()
        || !kachakacha::geometry::AlmostEqual(reloadedProject.Wires()[meetStart].wire.End(), expectedIntersection)) {
        return fail("save and reload direct editing");
    }

    SetViewportTool(ViewportTool::Select);
    viewport_->SetIsometricView();
    viewport_->FitAll();
    UpdateSelections({
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
        {CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)},
    }, true);
    toolsTabs_->setCurrentIndex(5);
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
            project_.UpdateWire(wireName, *replacement);
            project_.SetWireMetadata(wireName, metadata);
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
    if (selection.kind == CadSelectionKind::WorkPlane && selection.index >= 0
        && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
        name = ToQString(project_.WorkPlanes()[selection.index].name);
        detail = QStringLiteral("作業平面を削除します。平面から作ったワイヤーは3D形状として残ります。");
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
        } else if (selection.kind == CadSelectionKind::Wire) {
            candidate.RemoveWire(ToName(name));
        } else if (selection.kind == CadSelectionKind::Surface) {
            candidate.RemoveSurface(ToName(name));
        } else {
            candidate.RemovePlate(ToName(name));
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
        } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Wires()[selection.index].visible;
        } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
            && selection.index < static_cast<int>(project_.Surfaces().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Surfaces()[selection.index].visible;
        } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            hasVisibleSelection = hasVisibleSelection || project_.Plates()[selection.index].visible;
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
        } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            project_.SetWireVisible(project_.Wires()[selection.index].name, false);
        } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
            && selection.index < static_cast<int>(project_.Surfaces().size())) {
            project_.SetSurfaceVisible(project_.Surfaces()[selection.index].name, false);
        } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            project_.SetPlateVisible(project_.Plates()[selection.index].name, false);
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
    }) || std::any_of(project_.Wires().begin(), project_.Wires().end(), [](const auto& wire) {
        return !wire.visible;
    }) || std::any_of(project_.Surfaces().begin(), project_.Surfaces().end(), [](const auto& surface) {
        return !surface.visible;
    }) || std::any_of(project_.Plates().begin(), project_.Plates().end(), [](const auto& plate) {
        return !plate.visible;
    });
    if (!hasHiddenObjects) {
        statusBar()->showMessage(QStringLiteral("すべて表示されています"), 2000);
        return;
    }

    RecordUndo();
    for (const auto& plane : project_.WorkPlanes()) {
        project_.SetWorkPlaneVisible(plane.name, true);
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
    MarkModified();
    RefreshModelViews(false);
    statusBar()->showMessage(QStringLiteral("すべて再表示しました"), 2500);
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
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.WorkPlanes()[index].visible ? Qt::Checked : Qt::Unchecked);
    }
    auto* wireRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("ワイヤー (%1)").arg(project_.Wires().size())});
    for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
        auto* item = new QTreeWidgetItem(wireRoot, {ToQString(project_.Wires()[index].name)});
        item->setData(0, kSelectionKindRole, static_cast<int>(CadSelectionKind::Wire));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, project_.Wires()[index].visible ? Qt::Checked : Qt::Unchecked);
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
    planeRoot->setExpanded(true);
    wireRoot->setExpanded(true);
    surfaceRoot->setExpanded(true);
    plateRoot->setExpanded(true);
    modelTree_->blockSignals(false);

    RefreshPlaneChoices();
    RefreshWireChoices();
    RefreshSurfaceChoices();
    viewport_->SetProject(&project_, fitView);
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
}

void MainWindow::RefreshExportSummary()
{
    if (exportPlane_ == nullptr || exportScope_ == nullptr || exportSummary_ == nullptr || viewport_ == nullptr) {
        return;
    }
    const std::optional<WorkPlane> plane = project_.FindWorkPlane(ToName(exportPlane_->currentText()));
    if (!plane.has_value()) {
        exportSummary_->setText(QStringLiteral("出力面なし"));
        return;
    }

    std::size_t count = 0;
    if (exportScope_->currentIndex() == 0) {
        count = static_cast<std::size_t>(std::count_if(project_.Wires().begin(), project_.Wires().end(), [&](const auto& wire) {
            return WireLiesOnWorkPlane(wire.wire, *plane);
        }));
    } else {
        count = static_cast<std::size_t>(std::count_if(viewport_->Selections().begin(), viewport_->Selections().end(), [this](const auto& selection) {
            return selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size());
        }));
    }
    exportSummary_->setText(QStringLiteral("出力対象: %1本").arg(count));
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

    std::size_t selectedWireCount = 0;
    std::size_t selectedSurfaceCount = 0;
    std::size_t selectedPlateCount = 0;
    std::size_t selectedClosedProjectedWireCount = 0;
    for (const CadSelection& item : selections) {
        if (item.kind == CadSelectionKind::Wire && item.index >= 0
            && item.index < static_cast<int>(project_.Wires().size())) {
            ++selectedWireCount;
            const auto& wire = project_.Wires()[item.index];
            if (wire.projection.has_value() && wire.wire.IsClosed()) {
                ++selectedClosedProjectedWireCount;
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
        } else if (item.kind == CadSelectionKind::Plate && item.index >= 0
            && item.index < static_cast<int>(project_.Plates().size())) {
            ++selectedPlateCount;
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
    if (plateOpeningSelectionLabel_ != nullptr) {
        plateOpeningSelectionLabel_->setText(QStringLiteral("選択: 板材%1枚 / 閉じた投影輪郭%2本")
                .arg(selectedPlateCount)
                .arg(selectedClosedProjectedWireCount));
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
        if (named.projection.has_value()) {
            infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: 面上の投影ワイヤー<br>元の平面図: %2<br>対象面: %3<br>投影方向<br>%4<br><br>始点<br>%5<br><br>終点<br>%6")
                    .arg(ToQString(named.name),
                        ToQString(named.projection->sourceWireName),
                        ToQString(named.projection->targetSurfaceName),
                        VectorText(named.projection->direction),
                        VectorText(named.wire.Start()),
                        VectorText(named.wire.End())));
        } else {
            infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: %2<br>平面との関係: %3<br>作成元平面: %4<br><br>始点<br>%5<br><br>終点<br>%6")
                    .arg(ToQString(named.name), WireKindText(named.wire.Kind()), PolicyText(named.metadata.planePolicy), source, VectorText(named.wire.Start()), VectorText(named.wire.End())));
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
        if (plateDirection_ != nullptr) {
            plateDirection_->setCurrentIndex(plateDirection_->findData(static_cast<int>(named.plate.Direction())));
        }
        if (plateMaterial_ != nullptr) {
            const int materialIndex = plateMaterial_->findData(ToQString(named.material));
            plateMaterial_->setCurrentIndex(materialIndex >= 0 ? materialIndex : plateMaterial_->count() - 1);
        }
        const QString direction = named.plate.Direction() == PlateThicknessDirection::Positive
            ? QStringLiteral("面から外側へ")
            : named.plate.Direction() == PlateThicknessDirection::Centered
            ? QStringLiteral("面を中央に")
            : QStringLiteral("面から内側へ");
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
        infoLabel_->setText(QStringLiteral("<b>%1</b><br><br>種類: 板材<br>元の面: %2<br>板厚: %3 mm<br>厚み方向: %4<br>材質: %5<br>開口: %6<br><br>工作判定: %7")
                .arg(ToQString(named.name), ToQString(named.sourceSurfaceName))
                .arg(named.plate.Thickness())
                .arg(direction, material, openings, forming));
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
                && project_.Wires()[selection.index].projection.has_value()) {
                label->setText(QStringLiteral("投影ワイヤーは元の平面図を編集します"));
            } else if (selection.kind == CadSelectionKind::Surface) {
                label->setText(QStringLiteral("面は元の境界・断面ワイヤーを編集します"));
            } else if (selection.kind == CadSelectionKind::Plate) {
                label->setText(QStringLiteral("板材は元の面と板材欄から作り直します"));
            } else {
                label->setText(QStringLiteral("選択なし"));
            }
        }
        const bool editableWire = selection.kind == CadSelectionKind::Wire
            && selection.index >= 0 && selection.index < static_cast<int>(project_.Wires().size())
            && !project_.Wires()[selection.index].projection.has_value();
        editApplyButton_->setEnabled(selection.kind == CadSelectionKind::WorkPlane || editableWire);
    }
    RefreshExportSummary();
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

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (ConfirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}
