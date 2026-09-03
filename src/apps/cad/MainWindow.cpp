#include "MainWindow.h"
#include "CollapsibleSection.h"
#include "MainWindowUiHelpers.h"
#include "ModelTreeWidget.h"
#include "PartModelPanel.h"
#include "PlatePdfExport.h"

#include "kachakacha/io/PartPatterns.h"
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
#include <QInputDialog>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
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
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
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
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::AddPlateAssemblyMotionModel;
using kachakacha::io::AddPlateFlatPatternModel;
using kachakacha::io::BuildPlateAssemblyGuide;
using kachakacha::io::BuildPlateAssemblyMotion;
using kachakacha::io::BuildPlateFlatPattern;
using kachakacha::io::BuildAllPartPatterns;
using kachakacha::io::FabricationPanelDirection;
using kachakacha::io::PapercraftCutDirection;
using kachakacha::io::PlateAssemblyStrategy;
using kachakacha::io::PlateFlatPatternOptions;
using kachakacha::io::ReliefNotchStyle;
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
using kachakacha::model::CutPolylineCorner;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::IntersectWires;
using kachakacha::model::RoundPolylineCorner;
using kachakacha::model::JoinLineChain;
using kachakacha::model::kWireChainConnectionTolerance;
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

using namespace mainwindow_helpers;

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
    viewport_->onSelectContextMenu = [this](const QPoint& globalPosition) {
        ShowViewportContextMenu(globalPosition);
    };
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
    viewport_->SetEscapeRequestedCallback([this] {
        // 他CAD同様: Esc = 選択モード・何も選ばれていない状態へ。
        SetViewportTool(ViewportTool::Select);
        UpdateSelections({}, true);
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
    modelDock_ = modelDock;
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
    auto* modelTree = new ModelTreeWidget;
    modelTree->onMoveRequested = [this](const QList<QTreeWidgetItem*>& dragged, QTreeWidgetItem* target) {
        return HandleModelTreeDrop(dragged, target);
    };
    modelTree_ = modelTree;
    modelTree_->setHeaderHidden(true);
    modelTree_->setAlternatingRowColors(true);
    modelTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    modelTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(modelTree_, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& position) { ShowModelTreeContextMenu(position); });
    modelTree_->header()->setStretchLastSection(true);
    modelLayout->addWidget(modelFilter_);
    modelLayout->addWidget(modelTree_, 1);

    auto* beginnerGuide = new QGroupBox;
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
    auto* guideSection = new CollapsibleSection(
        QStringLiteral("操作ガイド"), beginnerGuide, true);
    guideSection_ = guideSection;
    modelLayout->addWidget(guideSection);

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
        if (item == nullptr) {
            return;
        }
        if (item->data(0, kSetNameRole).isValid()) {
            // 部材グループのチェック=グループ一括の表示/非表示。
            const std::string setName = ToName(item->data(0, kSetNameRole).toString());
            const bool visible = item->checkState(0) == Qt::Checked;
            const auto sets = project_.ObjectSets();
            const auto set = std::find_if(sets.begin(), sets.end(),
                [&](const auto& candidate) { return candidate.name == setName; });
            if (set == sets.end()
                || (set->state == kachakacha::model::ObjectSetState::Hidden) != visible) {
                return;
            }
            RecordUndo();
            project_.SetObjectSetState(setName, visible
                    ? kachakacha::model::ObjectSetState::Visible
                    : kachakacha::model::ObjectSetState::Hidden);
            MarkModified();
            viewport_->SetProject(&project_, false);
            UpdateSelection({}, false);
            statusBar()->showMessage(visible
                    ? QStringLiteral("部材グループを表示しました")
                    : QStringLiteral("部材グループを非表示にしました"), 2000);
            return;
        }
        if (item->parent() == nullptr) {
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
    toolsDock_ = toolsDock;
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
    const std::array<int, 4> workflowTabs = {1, 0, 2, 3};
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
    workflowPanel_ = workflowPanel;
    // モード切替は上部ツールバーへ移設(ADR 0025)。ボタンは互換のため残すが隠す。
    workflowPanel->setVisible(false);
    toolsLayout->addWidget(workflowPanel);
    toolsTabs_ = new QTabWidget;
    // ADR 0020 第2段: 作図に使う4タブ(作図/数値入力/編集/加工)を
    // 1つの「スケッチ」タブへ統合し、折りたたみセクションで区分けする。
    {
        auto* sketchPanel = new QWidget;
        auto* sketchLayout = new QVBoxLayout(sketchPanel);
        sketchLayout->setContentsMargins(6, 6, 6, 6);
        sketchLayout->setSpacing(6);
        sketchLayout->addWidget(new CollapsibleSection(
            QStringLiteral("作図"), BuildDrawingPanel(), true));
        sketchLayout->addWidget(new CollapsibleSection(
            QStringLiteral("数値入力"), BuildWirePanel(), false));
        sketchLayout->addWidget(new CollapsibleSection(
            QStringLiteral("編集"), BuildEditPanel(), false));
        sketchLayout->addWidget(new CollapsibleSection(
            QStringLiteral("加工（面取り・交点）"), BuildMachiningPanel(), false));
        sketchLayout->addStretch(1);
        auto* sketchScroll = new QScrollArea;
        sketchScroll->setWidgetResizable(true);
        sketchScroll->setFrameShape(QFrame::NoFrame);
        sketchScroll->setWidget(sketchPanel);
        toolsTabs_->addTab(sketchScroll, QStringLiteral("スケッチ"));
    }
    toolsTabs_->addTab(BuildPlanePanel(), QStringLiteral("作業平面"));
    toolsTabs_->addTab(BuildSurfacePanel(), QStringLiteral("面・板"));
    toolsTabs_->addTab(BuildOutputPanel(), QStringLiteral("出力"));
    toolsTabs_->addTab(BuildDisplayPanel(), QStringLiteral("表示"));
    toolsTabs_->addTab(BuildInfoPanel(), QStringLiteral("情報"));
    toolsTabs_->addTab(BuildPartModelPanelTab(), QStringLiteral("部材"));
    // 右パネルは選択中モードのパネルだけを表示する(オーナー指示)。
    // タブ自体は残し(セルフテスト互換・プログラムからの切替用)、見出しだけ隠す。
    toolsTabs_->tabBar()->hide();
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
        if (((drawingTool || editTool) && index != 0)
            || (tool == ViewportTool::MoveGridOrigin && index != 0)
            || (tool == ViewportTool::Measure && index != 5)) {
            SetViewportTool(ViewportTool::Select);
        }
        UpdatePlateSplitPreview();
        UpdatePlateAssemblyGuidePreview();
        SyncWorkModeToTab(index);
        RefreshBeginnerGuide();
    });
    toolsLayout->addWidget(toolsTabs_, 1);
    toolsDock->setWidget(toolsPanel);
    addDockWidget(Qt::RightDockWidgetArea, toolsDock);
    toolsDock->setMinimumWidth(380);
    toolsDock->setMaximumWidth(560);

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
        QToolButton#modeButtonDrawing, QToolButton#modeButtonSurface,
        QToolButton#modeButtonPartModel, QToolButton#modeButtonOutput {
            font-weight: 700; font-size: 13px; padding: 4px 12px; margin: 1px;
            border-radius: 4px; border: 1px solid; }
        QToolButton#modeButtonDrawing { background: #e2f1f2; color: #075f69; border-color: #77b4b9; }
        QToolButton#modeButtonDrawing:checked { background: #087780; color: #ffffff; border-color: #075f69; }
        QToolButton#modeButtonSurface { background: #e4edf8; color: #24507e; border-color: #86a9cf; }
        QToolButton#modeButtonSurface:checked { background: #2f6db3; color: #ffffff; border-color: #24507e; }
        QToolButton#modeButtonPartModel { background: #f7ecdf; color: #7d5218; border-color: #cfa877; }
        QToolButton#modeButtonPartModel:checked { background: #b3762f; color: #ffffff; border-color: #7d5218; }
        QToolButton#modeButtonOutput { background: #e5f2e9; color: #245c38; border-color: #83bd97; }
        QToolButton#modeButtonOutput:checked { background: #2f8b57; color: #ffffff; border-color: #245c38; }
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
    chamferAction_ = new QAction(QStringLiteral("C面取り"), this);
    filletAction_ = new QAction(QStringLiteral("R面取り"), this);
    cornerEditAction_ = new QAction(QStringLiteral("角の加工"), this);
    intersectionPointsAction_ = new QAction(QStringLiteral("交点に点"), this);
    lineBetweenPointsAction_ = new QAction(QStringLiteral("2点を線で結ぶ"), this);
    offsetApplyAction_ = new QAction(QStringLiteral("オフセット"), this);
    chamferAction_->setToolTip(
        QStringLiteral("選択した2直線をC面取り。距離はスケッチタブ「加工」の値を使用"));
    filletAction_->setToolTip(
        QStringLiteral("選択した2直線を円弧でつなぐ。半径はスケッチタブ「加工」の値を使用"));
    cornerEditAction_->setToolTip(
        QStringLiteral("選択したポリラインの角を落とす/丸める。数値はスケッチタブ「加工」の値を使用"));
    intersectionPointsAction_->setToolTip(QStringLiteral("選択した2本のワイヤーの交点すべてに作図点を作成"));
    lineBetweenPointsAction_->setToolTip(QStringLiteral("選択した2つの点(3D空間の任意交点も可)を直線で結ぶ"));
    offsetApplyAction_->setToolTip(
        QStringLiteral("選択した平面ワイヤーを平行オフセット。距離はスケッチタブ「編集」の値を使用"));
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
    joinWiresAction_->setToolTip(QStringLiteral(
        "端点がつながる直線・円弧・ベジェ・スプラインを1本のポリラインへ結合"));
    meetLinesAction_->setToolTip(QStringLiteral("選択した2直線をトリムまたは延長して交点で合わせる"));
    setReferenceAction_->setToolTip(QStringLiteral("選択した1本の直線を変形や平面作成の基準線にする"));
    clearReferenceAction_->setToolTip(QStringLiteral("現在の基準線を解除する"));
    pointToolAction_->setToolTip(QStringLiteral(
        "交点・端点・格子点または任意位置に点を置く。吸着時は大きな丸を表示し、Ctrl中は吸着しません"));
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
    connect(chamferAction_, &QAction::triggered, this, &MainWindow::ApplyLineChamfer);
    connect(filletAction_, &QAction::triggered, this, &MainWindow::ApplyLineFillet);
    connect(cornerEditAction_, &QAction::triggered, this, &MainWindow::ApplyPolylineCornerEdit);
    connect(intersectionPointsAction_, &QAction::triggered, this, &MainWindow::CreateIntersectionPoints);
    connect(lineBetweenPointsAction_, &QAction::triggered, this, &MainWindow::CreateLineBetweenSelectedPoints);
    connect(offsetApplyAction_, &QAction::triggered, this, &MainWindow::ApplyWireOffset);
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

void MainWindow::ShowShortcutReference()
{
    QMessageBox::information(this, QStringLiteral("ショートカット一覧"), QStringLiteral(
        "Esc\t選択モードへ戻る（選択解除・作図の取り消し）\n"
        "Enter\t作図中の線・曲線を確定\n"
        "数字・式\t作図中に入力すると実寸で確定（例: (180/2)*3）\n"
        "Shift\tビュー回転ボタンを15°→5°に\n"
        "マウスホイール\tズーム\n"
        "右ドラッグ\tビュー回転\n"
        "中ドラッグ\tパン（Shift+中ドラッグで回転）\n"
        "右クリック\t選択ツール中: コンテキストメニュー\n"
        "\t作図ツール中: 近くのスナップ点から引き始め\n"
        "ビューキューブ\t面・辺・角クリックで正対、ドラッグで自由回転\n"
        "色付きリング\tモデルの軸まわりに回転（ドラッグで連続）\n"
        "灰色の矢印\t画面基準で回転（位置固定、ドラッグで連続）"));
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

    // ソフト全体の設定はここに集約する(オーナー指示、ADR 0025)。
    QMenu* settingsMenu = menuBar()->addMenu(QStringLiteral("設定"));
    auto* shortcutAction = new QAction(QStringLiteral("ショートカット一覧…"), this);
    connect(shortcutAction, &QAction::triggered, this, &MainWindow::ShowShortcutReference);
    settingsMenu->addAction(shortcutAction);
    auto* displaySettingsAction = new QAction(QStringLiteral("表示・線の設定（表示タブ）"), this);
    connect(displaySettingsAction, &QAction::triggered, this, [this] {
        toolsTabs_->setCurrentIndex(4);
        if (toolsDock_ != nullptr) {
            toolsDock_->setVisible(true);
        }
    });
    settingsMenu->addAction(displaySettingsAction);
    auto* measurementWindowAction = new QAction(QStringLiteral("測定結果ウィンドウ"), this);
    connect(measurementWindowAction, &QAction::triggered, this, [this] {
        EnsureMeasurementWindow();
        UpdateMeasurementWindow();
        measurementWindow_->show();
        measurementWindow_->raise();
    });
    settingsMenu->addAction(measurementWindowAction);

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
    toolbar->addSeparator();
    toolbar->addAction(designDisplayAction_);
    toolbar->addAction(finishedDisplayAction_);
    toolbar->addAction(isolateDisplayAction_);
    toolbar->addSeparator();
    toolbar->addAction(hideSelectedAction_);
    toolbar->addAction(showAllObjectsAction_);
    toolbar->addAction(deleteAction);

    // --- 2段目: モード切替+常時ツール(正対・選択・測定)+作図面(ADR 0025) ---
    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar* modeToolbar = addToolBar(QStringLiteral("モード"));
    modeToolbar->setMovable(false);
    modeToolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    drawingModeAction_ = new QAction(QStringLiteral("作図"), this);
    surfaceModeAction_ = new QAction(QStringLiteral("面・板材"), this);
    partModelModeAction_ = new QAction(QStringLiteral("近似モデル"), this);
    outputModeAction_ = new QAction(QStringLiteral("出力"), this);
    auto* modeGroup = new QActionGroup(this);
    for (QAction* action : {drawingModeAction_, surfaceModeAction_, partModelModeAction_, outputModeAction_}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
        modeToolbar->addAction(action);
    }
    drawingModeAction_->setToolTip(QStringLiteral("平面を作り、線・曲線を描き、編集する"));
    surfaceModeAction_->setToolTip(QStringLiteral("ワイヤーから面・板材・ライトケースを作る"));
    partModelModeAction_->setToolTip(QStringLiteral("板材を製作用の部材近似モデルにする"));
    outputModeAction_->setToolTip(QStringLiteral("出力対象と形式(.kcd / STL / STEP / 図面)を選んで書き出す"));
    // 各モードボタンに固有色を付ける(QSSはBuildUiのsetStyleSheetにまとめてある)。
    if (QWidget* button = modeToolbar->widgetForAction(drawingModeAction_)) {
        button->setObjectName(QStringLiteral("modeButtonDrawing"));
    }
    if (QWidget* button = modeToolbar->widgetForAction(surfaceModeAction_)) {
        button->setObjectName(QStringLiteral("modeButtonSurface"));
    }
    if (QWidget* button = modeToolbar->widgetForAction(partModelModeAction_)) {
        button->setObjectName(QStringLiteral("modeButtonPartModel"));
    }
    if (QWidget* button = modeToolbar->widgetForAction(outputModeAction_)) {
        button->setObjectName(QStringLiteral("modeButtonOutput"));
    }
    connect(drawingModeAction_, &QAction::triggered, this, [this] { SetWorkMode(WorkMode::Drawing); });
    connect(surfaceModeAction_, &QAction::triggered, this, [this] { SetWorkMode(WorkMode::SurfacePlate); });
    connect(partModelModeAction_, &QAction::triggered, this, [this] { SetWorkMode(WorkMode::PartModel); });
    connect(outputModeAction_, &QAction::triggered, this, [this] { SetWorkMode(WorkMode::Output); });
    modeToolbar->addSeparator();
    modeToolbar->addAction(alignPlaneAction_);
    modeToolbar->addAction(selectToolAction_);
    modeToolbar->addAction(measureToolAction_);
    modeToolbar->addSeparator();
    {
        auto* planeCaption = new QLabel(QStringLiteral(" 作図面 "));
        planeCaption->setStyleSheet("color: #26323a; font-weight: 600;");
        modeToolbar->addWidget(planeCaption);
        modeToolbar->addWidget(activePlaneCombo_);
    }
    modeToolbar->addAction(snapAction_);

    // --- 3段目以降: 選択中モードのツール列(モードで表示を切り替える) ---
    addToolBarBreak(Qt::TopToolBarArea);
    drawingToolbar_ = addToolBar(QStringLiteral("作図ツール"));
    drawingToolbar_->setMovable(false);
    drawingToolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    {
        auto* makePlaneAction = new QAction(QStringLiteral("平面を作る"), this);
        makePlaneAction->setToolTip(QStringLiteral("作業平面の作成・回転・オフセット(右パネルに詳細)"));
        connect(makePlaneAction, &QAction::triggered, this, [this] {
            toolsTabs_->setCurrentIndex(1);
        });
        drawingToolbar_->addAction(makePlaneAction);
        drawingToolbar_->addSeparator();
    }
    drawingToolbar_->addAction(lineToolAction_);
    drawingToolbar_->addAction(polylineToolAction_);
    drawingToolbar_->addAction(rectangleToolAction_);
    drawingToolbar_->addAction(circleToolAction_);
    drawingToolbar_->addAction(arcToolAction_);
    drawingToolbar_->addAction(bezierToolAction_);
    drawingToolbar_->addAction(splineToolAction_);
    drawingToolbar_->addAction(pointToolAction_);
    drawingToolbar_->addSeparator();
    drawingToolbar_->addAction(finishDrawingAction_);
    drawingToolbar_->addAction(cancelDrawingAction_);

    transformToolbar_ = addToolBar(QStringLiteral("直接変形"));
    transformToolbar_->setMovable(false);
    transformToolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    transformToolbar_->addAction(moveToolAction_);
    transformToolbar_->addAction(copyToolAction_);
    transformToolbar_->addAction(rotateToolAction_);
    transformToolbar_->addAction(mirrorToolAction_);
    transformToolbar_->addAction(splitToolAction_);
    transformToolbar_->addAction(trimToolAction_);
    transformToolbar_->addAction(extendToolAction_);
    transformToolbar_->addAction(coincidentToolAction_);
    transformToolbar_->addAction(tangentToolAction_);
    transformToolbar_->addAction(curvatureToolAction_);
    transformToolbar_->addAction(joinWiresAction_);
    transformToolbar_->addAction(meetLinesAction_);
    transformToolbar_->addSeparator();
    transformToolbar_->addAction(setReferenceAction_);

    // 面・板材モードのツール列(パラメータは右パネル)。
    surfaceToolbar_ = addToolBar(QStringLiteral("面・板材ツール"));
    surfaceToolbar_->setMovable(false);
    surfaceToolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    {
        const auto addSurfaceCommand = [this](const QString& text, const QString& tip, auto slot) {
            auto* action = new QAction(text, this);
            action->setToolTip(tip);
            connect(action, &QAction::triggered, this, slot);
            surfaceToolbar_->addAction(action);
            return action;
        };
        addSurfaceCommand(QStringLiteral("面を作成"),
            QStringLiteral("選択した輪郭・断面ワイヤーから面を作る(詳細は右パネル)"),
            &MainWindow::CreateSurfaceFromSelection);
        addSurfaceCommand(QStringLiteral("ゴードン面"),
            QStringLiteral("縦横の断面ネットワークから面を作る"),
            &MainWindow::CreateGordonSurfaceFromSelection);
        addSurfaceCommand(QStringLiteral("面へ投影"),
            QStringLiteral("選択ワイヤーを面へ投影する(窓・ライトの下書きに)"),
            &MainWindow::ProjectSelectedWiresToSurface);
        surfaceToolbar_->addSeparator();
        addSurfaceCommand(QStringLiteral("板材化"),
            QStringLiteral("選択した面に厚みを付けて板材にする(厚み・方向は右パネル)"),
            &MainWindow::CreatePlateFromSurface);
        addSurfaceCommand(QStringLiteral("厚み位置のワイヤ"),
            QStringLiteral("板材の任意の厚み位置に輪郭ワイヤーを作る"),
            &MainWindow::CreatePlateOffsetWires);
        addSurfaceCommand(QStringLiteral("ライトケース"),
            QStringLiteral("前面形状から突出するライトケースを作る"),
            &MainWindow::CreateProtrudingLightCase);
        surfaceToolbar_->addSeparator();
        addSurfaceCommand(QStringLiteral("成形治具"),
            QStringLiteral("面から曲げ成形用の治具を作る"),
            &MainWindow::CreateSurfaceJig);
    }

    // 加工系(作図モードの2列目。オーナー指示: 縦2列可)。
    addToolBarBreak(Qt::TopToolBarArea);
    machiningToolbar_ = addToolBar(QStringLiteral("加工"));
    machiningToolbar_->setMovable(false);
    machiningToolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    machiningToolbar_->addAction(chamferAction_);
    machiningToolbar_->addAction(filletAction_);
    machiningToolbar_->addAction(cornerEditAction_);
    machiningToolbar_->addSeparator();
    machiningToolbar_->addAction(intersectionPointsAction_);
    machiningToolbar_->addAction(lineBetweenPointsAction_);
    machiningToolbar_->addAction(offsetApplyAction_);
    machiningToolbar_->addSeparator();
    machiningToolbar_->addAction(removeCoincidentAction_);
    machiningToolbar_->addAction(removeTangentAction_);
    machiningToolbar_->addAction(clearReferenceAction_);

    SetWorkMode(WorkMode::Drawing);
    UpdateHistoryActions();
}

void MainWindow::SetWorkMode(WorkMode mode)
{
    // ツール列の表示切り替え+右パネルの対応タブへ移動。
    const bool drawing = mode == WorkMode::Drawing;
    const bool surface = mode == WorkMode::SurfacePlate;
    if (drawingToolbar_ != nullptr) {
        drawingToolbar_->setVisible(drawing);
        transformToolbar_->setVisible(drawing);
        machiningToolbar_->setVisible(drawing);
        surfaceToolbar_->setVisible(surface);
    }
    QAction* action = mode == WorkMode::Drawing ? drawingModeAction_
        : mode == WorkMode::SurfacePlate ? surfaceModeAction_
        : mode == WorkMode::PartModel ? partModelModeAction_
                                      : outputModeAction_;
    if (action != nullptr && !action->isChecked()) {
        action->setChecked(true);
    }
    if (syncingWorkMode_ || toolsTabs_ == nullptr) {
        return;
    }
    syncingWorkMode_ = true;
    const int tab = mode == WorkMode::Drawing ? 0
        : mode == WorkMode::SurfacePlate ? 2
        : mode == WorkMode::PartModel ? 6
                                      : 3;
    if (toolsTabs_->currentIndex() != tab) {
        toolsTabs_->setCurrentIndex(tab);
    }
    syncingWorkMode_ = false;
}

void MainWindow::SyncWorkModeToTab(int tabIndex)
{
    if (syncingWorkMode_ || drawingModeAction_ == nullptr) {
        return;
    }
    syncingWorkMode_ = true;
    switch (tabIndex) {
    case 0:
    case 1:
        SetWorkMode(WorkMode::Drawing);
        break;
    case 2:
        SetWorkMode(WorkMode::SurfacePlate);
        break;
    case 3:
        SetWorkMode(WorkMode::Output);
        break;
    case 6:
        SetWorkMode(WorkMode::PartModel);
        break;
    default:
        break; // 表示・情報タブはモードと独立
    }
    syncingWorkMode_ = false;
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
    gordonGuideNames_.clear();
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
        gordonGuideNames_.clear();
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
        RemoveAutosave();
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
        toolsTabs_->setCurrentIndex(0);
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
        toolsTabs_->setCurrentIndex(0);
    } else if (tool == ViewportTool::Measure) {
        toolsTabs_->setCurrentIndex(5);
        EnsureMeasurementWindow();
        UpdateMeasurementWindow();
        measurementWindow_->show();
        measurementWindow_->raise();
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
        : tab == 0 ? 1
        : tab == 2 ? 2
        : tab == 3 ? 3 : -1;
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
                QStringLiteral("交点・端点・既存点は大きな丸が出た位置へ吸着\n丸がなければ任意位置。Ctrl中は完全に吸着しません"),
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
                QStringLiteral("projection"), 3, QStringLiteral("出力へ進む"));
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
            QStringLiteral("1  出力対象を直接選択\n2  分割方向・再現度・切れ込みを指定\n3  スライダーで曲げ状態と部品を確認\n4  1:1図面または現在の3Dを保存"),
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
            throw std::invalid_argument(
                "端点がつながる直線・円弧・ベジェ・スプラインを2本以上選択してください。");
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


WorkPlane MainWindow::WorkPlaneFromInputs() const
{
    std::vector<const WorkPlane*> selectedPlanes;
    std::vector<Vector3> selectedPoints;
    std::vector<const Wire*> selectedWires;
    std::vector<const kachakacha::model::Surface*> selectedSurfaces;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind == CadSelectionKind::WorkPlane && selection.index >= 0
            && selection.index < static_cast<int>(project_.WorkPlanes().size())) {
            selectedPlanes.push_back(&project_.WorkPlanes()[selection.index].plane);
        } else if (selection.kind == CadSelectionKind::Point && selection.index >= 0
            && selection.index < static_cast<int>(project_.Points().size())) {
            selectedPoints.push_back(project_.Points()[selection.index].point);
        } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            selectedWires.push_back(&project_.Wires()[selection.index].wire);
        } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
            && selection.index < static_cast<int>(project_.Surfaces().size())) {
            selectedSurfaces.push_back(&project_.Surfaces()[selection.index].surface);
        } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            selectedSurfaces.push_back(&project_.Plates()[selection.index].plate.SourceSurface());
        }
    }

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
        if (selectedPoints.empty()) {
            basePlane = WorkPlane::FromThreePoints(
                ReadVector3(threePointA_), ReadVector3(threePointB_),
                ReadVector3(threePointC_));
        } else {
            if (selectedPoints.size() != 3) {
                throw std::invalid_argument("作図点を3つ選択してください。");
            }
            basePlane = WorkPlane::FromThreePoints(
                selectedPoints[0], selectedPoints[1], selectedPoints[2]);
        }
        break;
    case 3:
        if (selectedPlanes.size() > 1) {
            throw std::invalid_argument("基準にする作業平面を1つだけ選択してください。");
        }
        basePlane = selectedPlanes.empty()
            ? project_.FindWorkPlane(ToName(offsetSourcePlane_->currentText()))
            : std::optional<WorkPlane>{*selectedPlanes.front()};
        if (!basePlane.has_value()) {
            throw std::invalid_argument("基準平面を選択してください。");
        }
        break;
    case 4: {
        if (selectedPlanes.size() > 1) {
            throw std::invalid_argument("傾ける基準平面を1つだけ選択してください。");
        }
        basePlane = selectedPlanes.empty()
            ? project_.FindWorkPlane(ToName(rotateSourcePlane_->currentText()))
            : std::optional<WorkPlane>{*selectedPlanes.front()};
        if (!basePlane.has_value()) {
            throw std::invalid_argument("基準平面を選択してください。");
        }
        Vector3 axisPoint = ReadVector3(rotateAxisPoint_);
        Vector3 axisDirection = ReadVector3(rotateAxisDirection_);
        if (!selectedWires.empty()) {
            if (selectedWires.size() != 1
                || selectedWires.front()->Kind() != WireKind::Line) {
                throw std::invalid_argument("回転軸にする直線を1本だけ選択してください。");
            }
            axisPoint = selectedWires.front()->Start();
            axisDirection = selectedWires.front()->End() - axisPoint;
        }
        basePlane = basePlane->RotateAroundAxis(
            axisPoint,
            axisDirection,
            rotateAngle_->value() * kPi / 180.0);
        break;
    }
    case 5:
        if (selectedPlanes.size() != 1 || selectedPoints.size() != 1) {
            throw std::invalid_argument("作業平面1つと作図点1つを選択してください。");
        }
        basePlane = WorkPlane::FromPointNormal(
            selectedPoints.front(), selectedPlanes.front()->Normal(),
            selectedPlanes.front()->UAxis());
        break;
    case 6: {
        if (selectedPlanes.size() != 2) {
            throw std::invalid_argument("平行な作業平面を2つ選択してください。");
        }
        const Vector3 firstNormal = selectedPlanes[0]->Normal();
        const double alignment = kachakacha::geometry::Dot(
            firstNormal, selectedPlanes[1]->Normal());
        if (std::abs(alignment) < 1.0 - 1.0e-6) {
            throw std::invalid_argument("中央面を作る2平面は平行である必要があります。");
        }
        const double separation = kachakacha::geometry::Dot(
            selectedPlanes[1]->Origin() - selectedPlanes[0]->Origin(),
            firstNormal);
        basePlane = WorkPlane::FromPointNormal(
            selectedPlanes[0]->Origin() + firstNormal * (separation * 0.5),
            firstNormal, selectedPlanes[0]->UAxis());
        break;
    }
    case 7: {
        if (selectedWires.size() != 2
            || selectedWires[0]->Kind() != WireKind::Line
            || selectedWires[1]->Kind() != WireKind::Line) {
            throw std::invalid_argument("同一平面上の直線を2本選択してください。");
        }
        const Vector3 firstPoint = selectedWires[0]->Start();
        const Vector3 firstDirection
            = (selectedWires[0]->End() - firstPoint).Normalized();
        const Vector3 secondPoint = selectedWires[1]->Start();
        const Vector3 secondDirection
            = (selectedWires[1]->End() - secondPoint).Normalized();
        Vector3 normal = kachakacha::geometry::Cross(
            firstDirection, secondDirection);
        if (normal.LengthSquared() <= 1.0e-14) {
            normal = kachakacha::geometry::Cross(
                firstDirection, secondPoint - firstPoint);
        } else {
            const Vector3 normalUnit = normal.Normalized();
            if (std::abs(kachakacha::geometry::Dot(
                    secondPoint - firstPoint, normalUnit)) > 1.0e-5) {
                throw std::invalid_argument("選択した2直線は同一平面上にありません。");
            }
        }
        if (normal.LengthSquared() <= 1.0e-14) {
            throw std::invalid_argument("同一直線上の2本だけでは平面の向きを決められません。");
        }
        basePlane = WorkPlane::FromPointNormal(
            firstPoint, normal, firstDirection);
        break;
    }
    case 8: {
        if (selectedWires.size() != 1 || selectedPoints.size() != 1
            || selectedWires.front()->Kind() != WireKind::Line) {
            throw std::invalid_argument("直線1本と作図点1つを選択してください。");
        }
        const Vector3 direction
            = selectedWires.front()->End() - selectedWires.front()->Start();
        basePlane = WorkPlane::FromPointNormal(
            selectedPoints.front(), direction);
        break;
    }
    case 9: {
        if (selectedWires.size() != 1) {
            throw std::invalid_argument("経路にする線または曲線を1本選択してください。");
        }
        const double parameter = pathPlanePosition_->value() / 100.0;
        const double before = std::max(0.0, parameter - 1.0e-4);
        const double after = std::min(1.0, parameter + 1.0e-4);
        const Vector3 tangent
            = selectedWires.front()->Evaluate(after)
            - selectedWires.front()->Evaluate(before);
        if (tangent.LengthSquared() <= 1.0e-18) {
            throw std::invalid_argument("指定位置で曲線の向きを計算できません。");
        }
        basePlane = WorkPlane::FromPointNormal(
            selectedWires.front()->Evaluate(parameter), tangent);
        break;
    }
    case 10: {
        if (selectedSurfaces.size() != 1 || selectedPoints.size() != 1) {
            throw std::invalid_argument("面または板1つと作図点1つを選択してください。");
        }
        const auto& surface = *selectedSurfaces.front();
        const Vector3 target = selectedPoints.front();
        double bestU = 0.0;
        double bestV = 0.0;
        double bestDistance = (surface.Evaluate(0.0, 0.0) - target).LengthSquared();
        constexpr int grid = 32;
        for (int uIndex = 0; uIndex <= grid; ++uIndex) {
            for (int vIndex = 0; vIndex <= grid; ++vIndex) {
                const double u = static_cast<double>(uIndex) / grid;
                const double v = static_cast<double>(vIndex) / grid;
                const double distance
                    = (surface.Evaluate(u, v) - target).LengthSquared();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestU = u;
                    bestV = v;
                }
            }
        }
        double step = 1.0 / grid;
        for (int iteration = 0; iteration < 14; ++iteration) {
            for (int uDirection = -1; uDirection <= 1; ++uDirection) {
                for (int vDirection = -1; vDirection <= 1; ++vDirection) {
                    const double u = std::clamp(
                        bestU + step * uDirection, 0.0, 1.0);
                    const double v = std::clamp(
                        bestV + step * vDirection, 0.0, 1.0);
                    const double distance
                        = (surface.Evaluate(u, v) - target).LengthSquared();
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestU = u;
                        bestV = v;
                    }
                }
            }
            step *= 0.5;
        }
        const double derivativeStep = 1.0e-4;
        Vector3 uAxis = surface.Evaluate(
                            std::min(1.0, bestU + derivativeStep), bestV)
            - surface.Evaluate(
                std::max(0.0, bestU - derivativeStep), bestV);
        if (uAxis.LengthSquared() <= 1.0e-18) {
            uAxis = surface.Evaluate(
                        bestU, std::min(1.0, bestV + derivativeStep))
                - surface.Evaluate(
                    bestU, std::max(0.0, bestV - derivativeStep));
        }
        basePlane = WorkPlane::FromPointNormal(
            surface.Evaluate(bestU, bestV), surface.Normal(bestU, bestV), uAxis);
        break;
    }
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

void MainWindow::ApplyPolylineCornerEdit()
{
    try {
        if (polylineCornerWire_->currentIndex() < 0) {
            throw std::invalid_argument("角を加工するポリラインを選択してください。");
        }
        const int wireIndex = polylineCornerWire_->currentData().toInt();
        if (wireIndex < 0 || wireIndex >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("選択したポリラインが見つかりません。");
        }
        const auto named = project_.Wires()[wireIndex];
        const bool chamfer = machiningType_->currentIndex() == 0;
        const auto result = chamfer
            ? CutPolylineCorner(
                  named.wire, polylineCornerVertex_->value(),
                  chamferFirstDistance_->value())
            : RoundPolylineCorner(
                  named.wire, polylineCornerVertex_->value(),
                  filletRadius_->value());

        kachakacha::model::WireMetadata metadata = named.metadata;
        metadata.lineConstraints = {};
        RecordUndo();
        project_.UpdateWireAndMetadata(named.name, result.wire, metadata);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection({CadSelectionKind::Wire, wireIndex}, true);
        statusBar()->showMessage(chamfer
                ? QStringLiteral("頂点%1をC面取りしました（%2）")
                      .arg(polylineCornerVertex_->value())
                      .arg(ToQString(named.name))
                : QStringLiteral("頂点%1をR丸めしました（%2）")
                      .arg(polylineCornerVertex_->value())
                      .arg(ToQString(named.name)),
            3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this,
            QStringLiteral("角を加工できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::CreateIntersectionPoints()
{
    try {
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (wireIndices.size() != 2) {
            throw std::invalid_argument(
                "3Dビューで線を2本選んでください（2本目はCtrl+クリック）。");
        }
        const auto& first = project_.Wires()[wireIndices[0]];
        const auto& second = project_.Wires()[wireIndices[1]];
        const auto intersections = IntersectWires(first.wire, second.wire, 1.0e-3);
        if (intersections.empty()) {
            throw std::invalid_argument(
                "交点が見つかりません（3D空間ですれ違っている可能性があります）。");
        }

        RecordUndo();
        int created = 0;
        for (const auto& point : intersections) {
            std::string name;
            for (int suffix = 1;; ++suffix) {
                name = "交点" + std::to_string(suffix);
                bool taken = false;
                for (const auto& namedPoint : project_.Points()) {
                    if (namedPoint.name == name) {
                        taken = true;
                        break;
                    }
                }
                if (!taken) {
                    break;
                }
            }
            project_.AddPoint(name, point, std::nullopt);
            ++created;
        }
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("%1 と %2 の交点に点を %3 個作成しました")
                .arg(ToQString(first.name), ToQString(second.name))
                .arg(created),
            4000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this,
            QStringLiteral("交点を作成できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::CreateLineBetweenSelectedPoints()
{
    try {
        std::vector<int> pointIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Point && selection.index >= 0
                && selection.index < static_cast<int>(project_.Points().size())) {
                pointIndices.push_back(selection.index);
            }
        }
        if (pointIndices.size() != 2) {
            throw std::invalid_argument(
                "3Dビューで点を2つ選んでください（2つ目はCtrl+クリック）。");
        }
        const auto& first = project_.Points()[pointIndices[0]];
        const auto& second = project_.Points()[pointIndices[1]];
        if ((first.point - second.point).Length() <= 1.0e-9) {
            throw std::invalid_argument("2つの点が同じ位置にあります。");
        }

        RecordUndo();
        const std::string name
            = ToName(SuggestedDirectGroupName(QStringLiteral("line")));
        project_.AddWire(name, Wire::Line(first.point, second.point), {});
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(
            {CadSelectionKind::Wire, static_cast<int>(project_.Wires().size()) - 1}, true);
        statusBar()->showMessage(
            QStringLiteral("%1 と %2 を結ぶ線 %3 を作成しました")
                .arg(ToQString(first.name), ToQString(second.name), ToQString(name)),
            4000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this,
            QStringLiteral("線を作成できません"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::ExpandSketchSection(const QString& title)
{
    QWidget* tab = toolsTabs_ != nullptr ? toolsTabs_->widget(0) : nullptr;
    if (tab == nullptr) {
        return;
    }
    const auto children = tab->findChildren<QWidget*>();
    for (QWidget* child : children) {
        // moc不使用のため dynamic_cast で判定する。
        if (auto* section = dynamic_cast<CollapsibleSection*>(child)) {
            if (section->Title() == title) {
                section->SetExpanded(true);
            }
        }
    }
}

void MainWindow::PrepareMachiningForWires(int firstIndex, int secondIndex)
{
    toolsTabs_->setCurrentIndex(0);
    ExpandSketchSection(QStringLiteral("加工（面取り・交点）"));
    const auto selectByData = [](QComboBox* combo, int wireIndex) {
        if (combo == nullptr) {
            return false;
        }
        const int position = combo->findData(wireIndex);
        if (position >= 0) {
            combo->setCurrentIndex(position);
            return true;
        }
        return false;
    };
    const bool firstSet = selectByData(chamferFirstWire_, firstIndex);
    const bool secondSet = selectByData(chamferSecondWire_, secondIndex);
    statusBar()->showMessage(firstSet && secondSet
            ? QStringLiteral("加工対象に設定しました。種類と値を選んで作成してください")
            : QStringLiteral("直線以外は加工対象にできません（ポリラインは「ポリラインの角」を使用）"),
        5000);
}

void MainWindow::ShowViewportContextMenu(const QPoint& globalPosition)
{
    QMenu menu(this);
    const auto& selections = viewport_->Selections();
    std::vector<int> wireIndices;
    std::vector<int> pointIndices;
    int plateIndex = -1;
    for (const CadSelection& selection : selections) {
        if (selection.kind == CadSelectionKind::Wire) {
            wireIndices.push_back(selection.index);
        } else if (selection.kind == CadSelectionKind::Point) {
            pointIndices.push_back(selection.index);
        } else if (selection.kind == CadSelectionKind::Plate) {
            plateIndex = selection.index;
        }
    }

    if (wireIndices.size() == 2) {
        menu.addAction(QStringLiteral("交点に点を作成"),
            this, &MainWindow::CreateIntersectionPoints);
        menu.addAction(QStringLiteral("この2本を面取り（C/R）の対象にする"),
            this, [this, wireIndices] {
                PrepareMachiningForWires(wireIndices[0], wireIndices[1]);
            });
        menu.addSeparator();
    }
    if (pointIndices.size() == 2) {
        menu.addAction(QStringLiteral("2点を結ぶ線を作成"),
            this, &MainWindow::CreateLineBetweenSelectedPoints);
        menu.addSeparator();
    }
    if (plateIndex >= 0 && plateIndex < static_cast<int>(project_.Plates().size())) {
        const QString plateName = ToQString(project_.Plates()[plateIndex].name);
        menu.addAction(QStringLiteral("部材近似へ（%1）").arg(plateName),
            this, [this, plateName] {
                toolsTabs_->setCurrentIndex(6);
                if (partModelPanel_ != nullptr) {
                    partModelPanel_->SelectPlate(plateName);
                }
            });
        menu.addSeparator();
    }

    QMenu* drawMenu = menu.addMenu(QStringLiteral("作図ツール"));
    for (QAction* action : {lineToolAction_, polylineToolAction_, rectangleToolAction_,
             circleToolAction_, arcToolAction_, bezierToolAction_, splineToolAction_,
             pointToolAction_}) {
        drawMenu->addAction(action);
    }
    QMenu* editMenu = menu.addMenu(QStringLiteral("編集ツール"));
    for (QAction* action : {moveToolAction_, copyToolAction_, mirrorToolAction_,
             rotateToolAction_, splitToolAction_, trimToolAction_, extendToolAction_}) {
        editMenu->addAction(action);
    }
    menu.addSeparator();
    if (!selections.empty()) {
        menu.addAction(hideSelectedAction_);
        menu.addAction(QStringLiteral("選択に正対"),
            this, [this] { (void)viewport_->AlignToSelection(); });
    }
    menu.addAction(showAllObjectsAction_);
    menu.addAction(QStringLiteral("全体表示"), this, [this] { viewport_->FitAll(); });
    menu.exec(globalPosition);
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
        if (IsOriginPlaneName(project_.WorkPlanes()[selection.index].name)) {
            statusBar()->showMessage(
                QStringLiteral("原点の基準平面（top_XY / front_XZ / side_YZ）は削除できません"), 3500);
            return;
        }
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
    // 部材グループ化で3階層になったため再帰で判定する。
    // 自分か祖先が一致すれば子孫ごと表示、子孫が一致すれば祖先も表示。
    const std::function<bool(QTreeWidgetItem*, bool)> applyFilter =
        [&](QTreeWidgetItem* item, bool ancestorMatches) -> bool {
        const bool selfMatches = term.isEmpty()
            || item->text(0).contains(term, Qt::CaseInsensitive);
        bool descendantMatches = false;
        for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
            descendantMatches = applyFilter(
                                    item->child(childIndex), ancestorMatches || selfMatches)
                || descendantMatches;
        }
        const bool visible = term.isEmpty() || ancestorMatches || selfMatches || descendantMatches;
        item->setHidden(!visible);
        if (!term.isEmpty() && visible && item->childCount() > 0) {
            item->setExpanded(true);
        }
        return selfMatches || descendantMatches;
    };
    for (int rootIndex = 0; rootIndex < modelTree_->topLevelItemCount(); ++rootIndex) {
        applyFilter(modelTree_->topLevelItem(rootIndex), false);
    }
}

void MainWindow::ShowModelTreeContextMenu(const QPoint& position)
{
    using kachakacha::model::ObjectSet;
    using kachakacha::model::ObjectSetState;
    using kachakacha::model::ProjectObjectKind;
    QTreeWidgetItem* clicked = modelTree_->itemAt(position);

    const auto toObjectKind = [](CadSelectionKind kind) -> std::optional<ProjectObjectKind> {
        switch (kind) {
        case CadSelectionKind::WorkPlane: return ProjectObjectKind::WorkPlane;
        case CadSelectionKind::Point: return ProjectObjectKind::Point;
        case CadSelectionKind::Wire: return ProjectObjectKind::Wire;
        case CadSelectionKind::Surface: return ProjectObjectKind::Surface;
        case CadSelectionKind::Plate: return ProjectObjectKind::Plate;
        case CadSelectionKind::Body: return ProjectObjectKind::Body;
        default: return std::nullopt;
        }
    };
    const auto objectName = [this](CadSelectionKind kind, int index) -> std::optional<std::string> {
        switch (kind) {
        case CadSelectionKind::WorkPlane:
            if (index >= 0 && index < static_cast<int>(project_.WorkPlanes().size())) {
                return project_.WorkPlanes()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Point:
            if (index >= 0 && index < static_cast<int>(project_.Points().size())) {
                return project_.Points()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Wire:
            if (index >= 0 && index < static_cast<int>(project_.Wires().size())) {
                return project_.Wires()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Surface:
            if (index >= 0 && index < static_cast<int>(project_.Surfaces().size())) {
                return project_.Surfaces()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Plate:
            if (index >= 0 && index < static_cast<int>(project_.Plates().size())) {
                return project_.Plates()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Body:
            if (index >= 0 && index < static_cast<int>(project_.Bodies().size())) {
                return project_.Bodies()[index].name;
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    };

    // 対象 = ツリーで選択中の要素(クリック位置の要素も含める)。
    std::vector<std::pair<ProjectObjectKind, std::string>> targets;
    QList<QTreeWidgetItem*> items = modelTree_->selectedItems();
    if (clicked != nullptr && !items.contains(clicked)) {
        items.prepend(clicked);
    }
    for (QTreeWidgetItem* item : items) {
        if (item == nullptr || !item->data(0, kSelectionKindRole).isValid()
            || !item->data(0, kSelectionIndexRole).isValid()) {
            continue;
        }
        const auto kind = toObjectKind(
            static_cast<CadSelectionKind>(item->data(0, kSelectionKindRole).toInt()));
        if (!kind.has_value()) {
            continue;
        }
        const auto name = objectName(
            static_cast<CadSelectionKind>(item->data(0, kSelectionKindRole).toInt()),
            item->data(0, kSelectionIndexRole).toInt());
        if (!name.has_value()) {
            continue;
        }
        if (*kind == ProjectObjectKind::WorkPlane && IsOriginPlaneName(*name)) {
            continue; // 原点平面はグループへ移せない
        }
        const auto duplicate = std::find_if(targets.begin(), targets.end(), [&](const auto& target) {
            return target.first == *kind && target.second == *name;
        });
        if (duplicate == targets.end()) {
            targets.emplace_back(*kind, *name);
        }
    }
    std::optional<std::string> clickedSetName;
    if (clicked != nullptr && clicked->data(0, kSetNameRole).isValid()) {
        clickedSetName = ToName(clicked->data(0, kSetNameRole).toString());
    }

    const auto finishSetEdit = [this](const QString& message) {
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(message, 3000);
    };
    const auto createSet = [this]() -> std::optional<std::string> {
        const QString input = QInputDialog::getText(this,
            QStringLiteral("新しい部材グループ"),
            QStringLiteral("部材グループ名（例: 前面、右側面、屋根）:"));
        if (input.trimmed().isEmpty()) {
            return std::nullopt;
        }
        try {
            ValidateObjectName(input);
            const std::string name = ToName(input);
            for (const ObjectSet& set : project_.ObjectSets()) {
                if (set.name == name) {
                    throw std::invalid_argument("同じ名前の部材グループがあります。");
                }
            }
            RecordUndo();
            project_.CreateObjectSet(name);
            return name;
        } catch (const std::exception& error) {
            QMessageBox::warning(this, QStringLiteral("部材グループ"),
                QString::fromUtf8(error.what()));
            return std::nullopt;
        }
    };
    const auto assignTargets = [this, targets, finishSetEdit](const std::string& setName) {
        for (const auto& [kind, name] : targets) {
            project_.AssignObjectToSet(kind, name, setName);
        }
        finishSetEdit(QStringLiteral("%1個を部材「%2」へ割り当てました")
                .arg(targets.size()).arg(ToQString(setName)));
    };

    QMenu menu(this);
    if (!targets.empty()) {
        QMenu* assignMenu = menu.addMenu(
            QStringLiteral("部材グループへ割り当て（%1個）").arg(targets.size()));
        const auto setPathLabel = [this](const ObjectSet& set) {
            QString label = ToQString(set.name);
            std::string current = set.parentName;
            int guard = 0;
            while (!current.empty() && guard++ < 16) {
                label = ToQString(current) + QStringLiteral(" / ") + label;
                const auto sets = project_.ObjectSets();
                const auto parent = std::find_if(sets.begin(), sets.end(),
                    [&](const ObjectSet& candidate) { return candidate.name == current; });
                current = parent != sets.end() ? parent->parentName : std::string();
            }
            return label;
        };
        for (const ObjectSet& set : project_.ObjectSets()) {
            if (set.automatic) {
                continue;
            }
            QAction* action = assignMenu->addAction(setPathLabel(set));
            const std::string setName = set.name;
            connect(action, &QAction::triggered, this, [this, assignTargets, setName] {
                RecordUndo();
                assignTargets(setName);
            });
        }
        if (!assignMenu->isEmpty()) {
            assignMenu->addSeparator();
        }
        QAction* assignNew = assignMenu->addAction(QStringLiteral("新しい部材グループ…"));
        connect(assignNew, &QAction::triggered, this, [this, createSet, assignTargets] {
            if (const auto name = createSet(); name.has_value()) {
                assignTargets(*name);
            }
        });
        QAction* unassign = menu.addAction(QStringLiteral("部材グループから外す"));
        connect(unassign, &QAction::triggered, this, [this, targets, finishSetEdit] {
            RecordUndo();
            for (const auto& [kind, name] : targets) {
                project_.RemoveObjectFromSets(kind, name);
            }
            finishSetEdit(QStringLiteral("部材グループから外しました"));
        });
        menu.addSeparator();
    }
    if (clickedSetName.has_value()) {
        const auto sets = project_.ObjectSets();
        const auto set = std::find_if(sets.begin(), sets.end(),
            [&](const ObjectSet& candidate) { return candidate.name == *clickedSetName; });
        if (set != sets.end()) {
            QMenu* stateMenu = menu.addMenu(QStringLiteral("表示状態"));
            const auto addStateAction = [&](const QString& label, ObjectSetState state) {
                QAction* action = stateMenu->addAction(label);
                action->setCheckable(true);
                action->setChecked(set->state == state);
                const std::string setName = *clickedSetName;
                connect(action, &QAction::triggered, this, [this, setName, state, finishSetEdit] {
                    RecordUndo();
                    project_.SetObjectSetState(setName, state);
                    viewport_->SetProject(&project_, false);
                    finishSetEdit(QStringLiteral("表示状態を変更しました"));
                });
            };
            addStateAction(QStringLiteral("表示"), ObjectSetState::Visible);
            addStateAction(QStringLiteral("参照のみ（スナップ可・編集不可）"), ObjectSetState::ReferenceOnly);
            addStateAction(QStringLiteral("非表示"), ObjectSetState::Hidden);
            QAction* exportAction = menu.addAction(QStringLiteral(".kcd書き出しに含める"));
            exportAction->setCheckable(true);
            exportAction->setChecked(set->exportEnabled);
            {
                const std::string setName = *clickedSetName;
                const bool nextEnabled = !set->exportEnabled;
                connect(exportAction, &QAction::triggered, this,
                    [this, setName, nextEnabled, finishSetEdit] {
                        RecordUndo();
                        project_.SetObjectSetExport(setName, nextEnabled);
                        finishSetEdit(nextEnabled
                                ? QStringLiteral("書き出し対象にしました")
                                : QStringLiteral("書き出しから除外しました（出力タブの「出力対象のみで書き出し」に反映）"));
                    });
            }
            {
                QAction* childSetAction = menu.addAction(QStringLiteral("子グループを作成…"));
                const std::string parentName = *clickedSetName;
                connect(childSetAction, &QAction::triggered, this,
                    [this, createSet, finishSetEdit, parentName] {
                        if (const auto name = createSet(); name.has_value()) {
                            project_.SetObjectSetParent(*name, parentName);
                            finishSetEdit(QStringLiteral("子グループ「%1」を作成しました")
                                    .arg(ToQString(*name)));
                        }
                    });
            }
            if (!set->automatic) {
                QAction* removeAction = menu.addAction(
                    QStringLiteral("部材グループを削除（中身と子グループは1つ上へ）"));
                const std::string setName = *clickedSetName;
                connect(removeAction, &QAction::triggered, this, [this, setName, finishSetEdit] {
                    RecordUndo();
                    project_.RemoveObjectSet(setName);
                    finishSetEdit(QStringLiteral("部材グループを削除しました"));
                });
            }
            menu.addSeparator();
        }
    }
    QAction* newSetAction = menu.addAction(QStringLiteral("新しい部材グループを作成…"));
    connect(newSetAction, &QAction::triggered, this, [createSet, finishSetEdit] {
        if (const auto name = createSet(); name.has_value()) {
            finishSetEdit(QStringLiteral("部材グループ「%1」を作成しました").arg(ToQString(*name)));
        }
    });
    menu.exec(modelTree_->viewport()->mapToGlobal(position));
}

bool MainWindow::HandleModelTreeDrop(
    const QList<QTreeWidgetItem*>& dragged, QTreeWidgetItem* target)
{
    using kachakacha::model::ObjectSet;
    using kachakacha::model::ProjectObjectKind;
    // ドロップ先のグループ = ドロップ位置から親方向へ最初に見つかる部材グループ。
    // 見つからない場合、「未分類」配下や空欄なら最上位(未所属)扱い、それ以外は無視。
    std::optional<std::string> targetSet;
    for (QTreeWidgetItem* node = target; node != nullptr; node = node->parent()) {
        if (node->data(0, kSetNameRole).isValid()) {
            targetSet = ToName(node->data(0, kSetNameRole).toString());
            break;
        }
    }
    if (!targetSet.has_value() && target != nullptr) {
        QTreeWidgetItem* top = target;
        while (top->parent() != nullptr) {
            top = top->parent();
        }
        if (top->text(0) != QStringLiteral("未分類")) {
            return false;
        }
    }

    const auto toObjectKind = [](CadSelectionKind kind) -> std::optional<ProjectObjectKind> {
        switch (kind) {
        case CadSelectionKind::WorkPlane: return ProjectObjectKind::WorkPlane;
        case CadSelectionKind::Point: return ProjectObjectKind::Point;
        case CadSelectionKind::Wire: return ProjectObjectKind::Wire;
        case CadSelectionKind::Surface: return ProjectObjectKind::Surface;
        case CadSelectionKind::Plate: return ProjectObjectKind::Plate;
        case CadSelectionKind::Body: return ProjectObjectKind::Body;
        default: return std::nullopt;
        }
    };
    const auto objectNameOf = [this](CadSelectionKind kind, int index) -> std::optional<std::string> {
        switch (kind) {
        case CadSelectionKind::WorkPlane:
            if (index >= 0 && index < static_cast<int>(project_.WorkPlanes().size())) {
                return project_.WorkPlanes()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Point:
            if (index >= 0 && index < static_cast<int>(project_.Points().size())) {
                return project_.Points()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Wire:
            if (index >= 0 && index < static_cast<int>(project_.Wires().size())) {
                return project_.Wires()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Surface:
            if (index >= 0 && index < static_cast<int>(project_.Surfaces().size())) {
                return project_.Surfaces()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Plate:
            if (index >= 0 && index < static_cast<int>(project_.Plates().size())) {
                return project_.Plates()[index].name;
            }
            return std::nullopt;
        case CadSelectionKind::Body:
            if (index >= 0 && index < static_cast<int>(project_.Bodies().size())) {
                return project_.Bodies()[index].name;
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    };

    std::vector<std::string> draggedSets;
    std::vector<std::pair<ProjectObjectKind, std::string>> draggedObjects;
    for (QTreeWidgetItem* item : dragged) {
        if (item == nullptr) {
            continue;
        }
        if (item->data(0, kSetNameRole).isValid()) {
            const std::string setName = ToName(item->data(0, kSetNameRole).toString());
            if (!targetSet.has_value() || *targetSet != setName) {
                draggedSets.push_back(setName);
            }
            continue;
        }
        if (!item->data(0, kSelectionKindRole).isValid()
            || !item->data(0, kSelectionIndexRole).isValid()) {
            continue;
        }
        const CadSelectionKind kind =
            static_cast<CadSelectionKind>(item->data(0, kSelectionKindRole).toInt());
        const auto objectKind = toObjectKind(kind);
        const auto name = objectNameOf(kind, item->data(0, kSelectionIndexRole).toInt());
        if (objectKind.has_value() && name.has_value()
            && !(*objectKind == ProjectObjectKind::WorkPlane && IsOriginPlaneName(*name))) {
            draggedObjects.emplace_back(*objectKind, *name);
        }
    }
    // ドラッグ中のグループの配下要素は、グループごと動くので個別移動から除く。
    if (!draggedSets.empty()) {
        std::erase_if(draggedObjects, [&](const auto& object) {
            for (const ObjectSet& set : project_.ObjectSets()) {
                if (std::find(draggedSets.begin(), draggedSets.end(), set.name) == draggedSets.end()) {
                    continue;
                }
                for (const auto& member : set.members) {
                    if (member.kind == object.first && member.name == object.second) {
                        return true;
                    }
                }
            }
            return false;
        });
    }
    if (draggedSets.empty() && draggedObjects.empty()) {
        return false;
    }

    RecordUndo();
    QStringList problems;
    for (const std::string& setName : draggedSets) {
        try {
            project_.SetObjectSetParent(setName, targetSet.value_or(std::string()));
        } catch (const std::exception& error) {
            problems << QString::fromUtf8(error.what());
        }
    }
    for (const auto& [kind, name] : draggedObjects) {
        try {
            if (targetSet.has_value()) {
                project_.AssignObjectToSet(kind, name, *targetSet);
            } else {
                project_.RemoveObjectFromSets(kind, name);
            }
        } catch (const std::exception& error) {
            problems << QString::fromUtf8(error.what());
        }
    }
    MarkModified();
    RefreshModelViews(false);
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("部材グループ"),
            problems.join(QStringLiteral("\n")));
    } else {
        statusBar()->showMessage(targetSet.has_value()
                ? QStringLiteral("部材「%1」へ移動しました").arg(ToQString(*targetSet))
                : QStringLiteral("未分類へ移動しました"),
            3000);
    }
    return true;
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

    // 部材グループ(ObjectSet)ごとにまとめて表示する(ADR 0024)。
    // 所属は「1オブジェクト=最大1グループ」。未所属は「未分類」へ。
    using kachakacha::model::ObjectSet;
    using kachakacha::model::ObjectSetState;
    using kachakacha::model::ProjectObjectKind;
    const auto setOf = [this](ProjectObjectKind kind, const std::string& name) -> const ObjectSet* {
        for (const ObjectSet& set : project_.ObjectSets()) {
            for (const auto& member : set.members) {
                if (member.kind == kind && member.name == name) {
                    return &set;
                }
            }
        }
        return nullptr;
    };
    const auto addObjectItem = [this](QTreeWidgetItem* parent, CadSelectionKind kind, int index,
                                   const QString& label, bool visible) {
        auto* item = new QTreeWidgetItem(parent, {label});
        item->setData(0, kSelectionKindRole, static_cast<int>(kind));
        item->setData(0, kSelectionIndexRole, index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable
            | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        return item;
    };
    // 各コンテナ(部材グループまたは未分類)に、種類ごとの小見出し+要素を作る。
    const auto addKindSections = [&](QTreeWidgetItem* container, const ObjectSet* owner) {
        const auto belongs = [&](ProjectObjectKind kind, const std::string& name) {
            return setOf(kind, name) == owner;
        };
        const auto section = [&](const QString& title, auto&& fill) {
            auto* root = new QTreeWidgetItem({QStringLiteral("")});
            int count = fill(root);
            if (count == 0) {
                delete root;
                return;
            }
            root->setText(0, QStringLiteral("%1 (%2)").arg(title).arg(count));
            container->addChild(root);
            root->setExpanded(true);
        };
        section(QStringLiteral("作業平面"), [&](QTreeWidgetItem* root) {
            int count = 0;
            for (int index = 0; index < static_cast<int>(project_.WorkPlanes().size()); ++index) {
                const auto& plane = project_.WorkPlanes()[index];
                if (IsOriginPlaneName(plane.name)) continue; // 原点平面は最上部の専用ノードへ
                if (!belongs(ProjectObjectKind::WorkPlane, plane.name)) continue;
                addObjectItem(root, CadSelectionKind::WorkPlane, index, ToQString(plane.name), plane.visible);
                ++count;
            }
            return count;
        });
        section(QStringLiteral("作図点"), [&](QTreeWidgetItem* root) {
            int count = 0;
            for (int index = 0; index < static_cast<int>(project_.Points().size()); ++index) {
                const auto& point = project_.Points()[index];
                if (!belongs(ProjectObjectKind::Point, point.name)) continue;
                addObjectItem(root, CadSelectionKind::Point, index, ToQString(point.name), point.visible);
                ++count;
            }
            return count;
        });
        section(QStringLiteral("ワイヤー"), [&](QTreeWidgetItem* root) {
            int count = 0;
            for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
                const auto& wire = project_.Wires()[index];
                if (!belongs(ProjectObjectKind::Wire, wire.name)) continue;
                const QString label = wire.metadata.construction
                    ? QStringLiteral("%1 （補助）").arg(ToQString(wire.name))
                    : ToQString(wire.name);
                addObjectItem(root, CadSelectionKind::Wire, index, label, wire.visible);
                ++count;
            }
            return count;
        });
        section(QStringLiteral("面"), [&](QTreeWidgetItem* root) {
            int count = 0;
            for (int index = 0; index < static_cast<int>(project_.Surfaces().size()); ++index) {
                const auto& surface = project_.Surfaces()[index];
                if (!belongs(ProjectObjectKind::Surface, surface.name)) continue;
                addObjectItem(root, CadSelectionKind::Surface, index, ToQString(surface.name), surface.visible);
                ++count;
            }
            return count;
        });
        section(QStringLiteral("板材"), [&](QTreeWidgetItem* root) {
            int count = 0;
            for (int index = 0; index < static_cast<int>(project_.Plates().size()); ++index) {
                const auto& plate = project_.Plates()[index];
                if (!belongs(ProjectObjectKind::Plate, plate.name)) continue;
                addObjectItem(root, CadSelectionKind::Plate, index, ToQString(plate.name), plate.visible);
                ++count;
            }
            return count;
        });
        section(QStringLiteral("治具・立体"), [&](QTreeWidgetItem* root) {
            int count = 0;
            for (int index = 0; index < static_cast<int>(project_.Bodies().size()); ++index) {
                const auto& body = project_.Bodies()[index];
                if (!belongs(ProjectObjectKind::Body, body.name)) continue;
                addObjectItem(root, CadSelectionKind::Body, index, ToQString(body.name), body.visible);
                ++count;
            }
            return count;
        });
    };

    // 原点の基準平面は常に最上部へ固定表示する(削除・グループ移動は不可)。
    // 別のプロジェクトを読み込んだ場合は存在しないことがあるので、あるときだけ出す。
    const bool hasOriginPlanes = std::any_of(
        project_.WorkPlanes().begin(), project_.WorkPlanes().end(),
        [](const auto& plane) { return IsOriginPlaneName(plane.name); });
    if (hasOriginPlanes) {
        auto* originRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("原点")});
        originRoot->setToolTip(0, QStringLiteral(
            "初期の基準平面（top_XY / front_XZ / side_YZ）。削除やグループ移動はできません"));
        QFont originFont = originRoot->font(0);
        originFont.setBold(true);
        originRoot->setFont(0, originFont);
        originRoot->setIcon(0, style()->standardIcon(QStyle::SP_ComputerIcon));
        for (int index = 0; index < static_cast<int>(project_.WorkPlanes().size()); ++index) {
            const auto& plane = project_.WorkPlanes()[index];
            if (!IsOriginPlaneName(plane.name)) {
                continue;
            }
            auto* item = addObjectItem(
                originRoot, CadSelectionKind::WorkPlane, index, ToQString(plane.name), plane.visible);
            item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
        }
        originRoot->setExpanded(true);
    }

    // グループはエクスプローラのフォルダのように入れ子にできる(parentName)。
    const std::function<void(const ObjectSet&, QTreeWidgetItem*)> addSetNode =
        [&](const ObjectSet& set, QTreeWidgetItem* parentItem) {
        QString label = ToQString(set.name);
        QStringList notes;
        if (set.state == ObjectSetState::ReferenceOnly) notes << QStringLiteral("参照のみ");
        if (!set.exportEnabled) notes << QStringLiteral("出力しない");
        if (!notes.isEmpty()) {
            label += QStringLiteral("  [%1]").arg(notes.join(QStringLiteral("・")));
        }
        auto* setRoot = parentItem != nullptr
            ? new QTreeWidgetItem(parentItem, {label})
            : new QTreeWidgetItem(modelTree_, {label});
        setRoot->setData(0, kSetNameRole, ToQString(set.name));
        setRoot->setFlags(setRoot->flags() | Qt::ItemIsUserCheckable
            | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        setRoot->setCheckState(0, set.state == ObjectSetState::Hidden ? Qt::Unchecked : Qt::Checked);
        setRoot->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        setRoot->setToolTip(0, QStringLiteral(
            "部材グループ。チェックで一括表示/非表示。ドラッグで入れ子に移動、"
            "右クリックで割り当て・出力設定"));
        QFont setFont = setRoot->font(0);
        setFont.setBold(true);
        setRoot->setFont(0, setFont);
        addKindSections(setRoot, &set);
        for (const ObjectSet& child : project_.ObjectSets()) {
            if (child.parentName == set.name) {
                addSetNode(child, setRoot);
            }
        }
        setRoot->setExpanded(true);
    };
    const auto setExists = [this](const std::string& name) {
        for (const ObjectSet& set : project_.ObjectSets()) {
            if (set.name == name) {
                return true;
            }
        }
        return false;
    };
    for (const ObjectSet& set : project_.ObjectSets()) {
        if (set.parentName.empty() || !setExists(set.parentName)) {
            addSetNode(set, nullptr);
        }
    }
    {
        auto* unassignedRoot = new QTreeWidgetItem(modelTree_, {QStringLiteral("未分類")});
        unassignedRoot->setFlags(unassignedRoot->flags() | Qt::ItemIsDropEnabled);
        unassignedRoot->setToolTip(0, QStringLiteral(
            "どの部材グループにも属さないオブジェクト。右クリックかドラッグで部材へ移せます"));
        addKindSections(unassignedRoot, nullptr);
        unassignedRoot->setExpanded(true);
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
    coincidenceRoot->setExpanded(true);
    tangentRoot->setExpanded(true);
    dimensionRoot->setExpanded(true);
    modelTree_->blockSignals(false);
    ApplyModelTreeFilter();

    RefreshPlaneChoices();
    RefreshWireChoices();
    RefreshSurfaceChoices();
    if (partModelPanel_ != nullptr) {
        partModelPanel_->RefreshFromProject(project_);
        UpdatePartFoldPreview();
    }
    viewport_->SetProject(&project_, fitView);
    RefreshReferenceDimensions();
    RefreshActiveWorkPlane();
    RefreshReference();
    gordonGuideNames_.erase(
        std::remove_if(gordonGuideNames_.begin(), gordonGuideNames_.end(),
            [this](const std::string& name) {
                return std::none_of(project_.Wires().begin(), project_.Wires().end(),
                    [&name](const kachakacha::model::NamedWire& wire) { return wire.name == name; });
            }),
        gordonGuideNames_.end());
    RefreshGordonGuideLabel();
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
    if (polylineCornerWire_ != nullptr) {
        const QSignalBlocker blocker(polylineCornerWire_);
        const QString previous = polylineCornerWire_->currentText();
        polylineCornerWire_->clear();
        for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
            if (project_.Wires()[index].wire.Kind() == WireKind::Polyline) {
                polylineCornerWire_->addItem(ToQString(project_.Wires()[index].name), index);
            }
        }
        const int previousIndex = polylineCornerWire_->findText(previous);
        if (previousIndex >= 0) {
            polylineCornerWire_->setCurrentIndex(previousIndex);
        }
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
    QStringList selectedWireNames;
    QStringList lightCaseFrontNames;
    QStringList lightCaseTargetNames;
    for (const CadSelection& item : selections) {
        if (item.kind == CadSelectionKind::Wire && item.index >= 0
            && item.index < static_cast<int>(project_.Wires().size())) {
            ++selectedWireCount;
            const auto& wire = project_.Wires()[item.index];
            selectedWireNames.push_back(ToQString(wire.name));
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
        if (surfaceType_ != nullptr && surfaceType_->currentIndex() == 3
            && !selectedWireNames.isEmpty()) {
            QStringList roles;
            for (int index = 0; index < selectedWireNames.size(); ++index) {
                roles.push_back(index < 2
                        ? QStringLiteral("外形%1: %2").arg(index + 1).arg(selectedWireNames[index])
                        : QStringLiteral("断面%1: %2").arg(index - 1).arg(selectedWireNames[index]));
            }
            surfaceSelectionLabel_->setText(
                QStringLiteral("選択順\n%1").arg(roles.join(QStringLiteral("\n"))));
        } else {
            surfaceSelectionLabel_->setText(QStringLiteral("選択: ワイヤー%1本 / 面%2枚")
                    .arg(selectedWireCount)
                    .arg(selectedSurfaceCount));
        }
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
            : named.surface.Kind() == SurfaceKind::Loft
            ? QStringLiteral("複数断面のロフト面")
            : QStringLiteral("外形ガイド付きロフト面");
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
    UpdateMeasurementWindow();
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

QString MainWindow::SuggestedName(
    const QString& prefix, int startNumber, const std::function<bool(const QString& candidate)>& exists) const
{
    int number = startNumber;
    for (;;) {
        const QString candidate = QStringLiteral("%1_%2").arg(prefix).arg(number);
        if (!exists(candidate)) {
            return candidate;
        }
        ++number;
    }
}

QString MainWindow::SuggestedPlaneName() const
{
    return SuggestedName(QStringLiteral("plane"), static_cast<int>(project_.WorkPlanes().size()) + 1,
        [this](const QString& candidate) { return project_.FindWorkPlane(candidate.toStdString()).has_value(); });
}

QString MainWindow::SuggestedWireName() const
{
    const auto exists = [this](const std::string& name) {
        for (const auto& wire : project_.Wires()) {
            if (wire.name == name) {
                return true;
            }
        }
        return false;
    };
    return SuggestedName(QStringLiteral("wire"), static_cast<int>(project_.Wires().size()) + 1,
        [&exists](const QString& candidate) { return exists(candidate.toStdString()); });
}

QString MainWindow::SuggestedDirectGroupName(const QString& prefix) const
{
    const auto exists = [this](const QString& candidate) {
        const std::string exactName = candidate.toStdString();
        const std::string memberPrefix = exactName + "_";
        return std::any_of(project_.Wires().begin(), project_.Wires().end(), [&](const auto& wire) {
            return wire.name == exactName || wire.name.starts_with(memberPrefix);
        }) || std::any_of(project_.Points().begin(), project_.Points().end(), [&](const auto& point) {
            return point.name == exactName || point.name.starts_with(memberPrefix);
        });
    };
    return SuggestedName(prefix, 1, exists);
}

QString MainWindow::SuggestedChamferName() const
{
    const auto exists = [this](const std::string& name) {
        for (const auto& wire : project_.Wires()) {
            if (wire.name == name) {
                return true;
            }
        }
        return false;
    };
    return SuggestedName(
        QStringLiteral("chamfer"), 1, [&exists](const QString& candidate) { return exists(candidate.toStdString()); });
}

QString MainWindow::SuggestedFilletName() const
{
    const auto exists = [this](const std::string& name) {
        for (const auto& wire : project_.Wires()) {
            if (wire.name == name) {
                return true;
            }
        }
        return false;
    };
    return SuggestedName(
        QStringLiteral("fillet"), 1, [&exists](const QString& candidate) { return exists(candidate.toStdString()); });
}

QString MainWindow::SuggestedSurfaceName() const
{
    return SuggestedName(QStringLiteral("surface"), static_cast<int>(project_.Surfaces().size()) + 1,
        [this](const QString& candidate) { return project_.FindSurface(ToName(candidate)).has_value(); });
}

QString MainWindow::SuggestedPlateName() const
{
    return SuggestedName(QStringLiteral("plate"), static_cast<int>(project_.Plates().size()) + 1,
        [this](const QString& candidate) { return project_.FindPlate(ToName(candidate)).has_value(); });
}

QString MainWindow::SuggestedBodyName() const
{
    return SuggestedName(QStringLiteral("jig"), static_cast<int>(project_.Bodies().size()) + 1,
        [this](const QString& candidate) { return project_.FindBody(ToName(candidate)).has_value(); });
}

QString MainWindow::SuggestedDimensionName() const
{
    const auto exists = [this](const QString& candidate) {
        const std::string name = ToName(candidate);
        return std::any_of(
            project_.ReferenceDimensions().begin(), project_.ReferenceDimensions().end(),
            [&](const ReferenceDimension& dimension) { return dimension.name == name; });
    };
    return SuggestedName(QStringLiteral("dim"), static_cast<int>(project_.ReferenceDimensions().size()) + 1, exists);
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
