// 右パネル各タブのUI構築(スケッチ/作業平面/面・板/表示/情報)と表示設定。
// MainWindow.cpp から逐語移動(ADR 0018/0022)。ロジックはここに足さない。

#include "MainWindow.h"
#include "CollapsibleSection.h"
#include "MainWindowUiHelpers.h"
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
#include <QHBoxLayout>
#include <QItemSelectionModel>
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

QWidget* MainWindow::BuildDrawingPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 作図面コンボは上部ツールバーに常時表示する(ADR 0021: モードの可視化)。
    activePlaneCombo_ = new QComboBox;
    activePlaneCombo_->setObjectName("activePlaneCombo");
    activePlaneCombo_->setMinimumWidth(150);

    drawingStateLabel_ = new QLabel(QStringLiteral("選択"));
    drawingStateLabel_->setStyleSheet("color: #075f69; font-weight: 600; padding: 4px 0;");
    layout->addWidget(drawingStateLabel_);

    // 作図モード専用: 使う道具を右パネルからも選べるボタン群(オーナー指示、ADR 0025)。
    // QToolButton::setDefaultAction でツールバーの QAction と状態が同期する。
    {
        auto* toolGrid = new QGridLayout;
        toolGrid->setContentsMargins(0, 2, 0, 4);
        toolGrid->setHorizontalSpacing(5);
        toolGrid->setVerticalSpacing(5);
        const std::array<QAction*, 18> toolActions = {
            selectToolAction_, pointToolAction_, lineToolAction_,
            polylineToolAction_, rectangleToolAction_, circleToolAction_,
            arcToolAction_, bezierToolAction_, splineToolAction_,
            moveToolAction_, copyToolAction_, rotateToolAction_,
            mirrorToolAction_, splitToolAction_, trimToolAction_,
            extendToolAction_, chamferAction_, filletAction_,
        };
        int cell = 0;
        for (QAction* action : toolActions) {
            if (action == nullptr) {
                continue;
            }
            auto* button = new QToolButton;
            button->setDefaultAction(action);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setMinimumHeight(28);
            toolGrid->addWidget(button, cell / 3, cell % 3);
            ++cell;
        }
        layout->addLayout(toolGrid);
    }

    drawingConstruction_ = new QCheckBox(QStringLiteral("補助線として作図"));
    drawingConstruction_->setToolTip(QStringLiteral("スナップや寸法基準に使い、面や切断出力には含めない"));
    layout->addWidget(drawingConstruction_);

    drawingKeepCurvePoints_ = new QCheckBox(QStringLiteral("指定した点を作図点として残す"));
    drawingKeepCurvePoints_->setToolTip(QStringLiteral(
        "ベジェ・スプラインの確定時に、クリックした通過点・制御点を\n"
        "作図点(<線名>_点N)としても作成します"));
    drawingKeepCurvePoints_->setVisible(false);
    layout->addWidget(drawingKeepCurvePoints_);

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
    for (const QKeySequence& key : {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)}) {
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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    planeName_ = new QLineEdit("plane_1");
    planeMethod_ = new QComboBox;
    planeMethod_->addItems({
        QStringLiteral("XYZの基準面"),
        QStringLiteral("位置と向きを数値指定"),
        QStringLiteral("3点を通る"),
        QStringLiteral("平面から平行距離"),
        QStringLiteral("直線を軸に傾ける"),
        QStringLiteral("点を通る平行面"),
        QStringLiteral("2平面の中央面"),
        QStringLiteral("2直線を含む面"),
        QStringLiteral("点を通る直線直角面"),
        QStringLiteral("線上位置の直角面"),
        QStringLiteral("曲面近傍の接平面"),
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
    auto* threePointHint = new QLabel(QStringLiteral(
        "3Dで作図点を3つ選択すると、その点を優先します。未選択時は下の座標を使います。"));
    threePointHint->setWordWrap(true);
    pageForm->addRow(threePointHint);
    pageForm->addRow(QStringLiteral("点 A"), MakeVector3Editor(threePointA_));
    pageForm->addRow(QStringLiteral("点 B"), MakeVector3Editor(threePointB_, {10.0, 0.0, 0.0}));
    pageForm->addRow(QStringLiteral("点 C"), MakeVector3Editor(threePointC_, {0.0, 10.0, 0.0}));
    planeParameters_->addWidget(threePointPage);

    QWidget* offsetPage = MakeFormPage(pageForm);
    offsetSourcePlane_ = new QComboBox;
    auto* offsetHint = new QLabel(QStringLiteral(
        "3Dで作業平面を1つ選択した場合は、その平面を優先します。距離は下の共通調整で指定します。"));
    offsetHint->setWordWrap(true);
    pageForm->addRow(offsetHint);
    pageForm->addRow(QStringLiteral("基準平面"), offsetSourcePlane_);
    planeParameters_->addWidget(offsetPage);

    QWidget* rotatePage = MakeFormPage(pageForm);
    rotateSourcePlane_ = new QComboBox;
    rotateAngle_ = MakeNumberField(30.0);
    rotateAngle_->setSuffix(QStringLiteral(" °"));
    auto* rotateHint = new QLabel(QStringLiteral(
        "3Dで作業平面と直線を選択した場合は、その組み合わせを優先します。"));
    rotateHint->setWordWrap(true);
    pageForm->addRow(rotateHint);
    pageForm->addRow(QStringLiteral("基準平面"), rotateSourcePlane_);
    pageForm->addRow(QStringLiteral("回転軸の点"), MakeVector3Editor(rotateAxisPoint_));
    pageForm->addRow(QStringLiteral("回転軸の向き"), MakeVector3Editor(rotateAxisDirection_, {1.0, 0.0, 0.0}));
    pageForm->addRow(QStringLiteral("基準角度"), rotateAngle_);
    planeParameters_->addWidget(rotatePage);

    const auto addSelectionPage = [&](const QString& instruction) {
        QFormLayout* selectionForm = nullptr;
        QWidget* page = MakeFormPage(selectionForm);
        auto* label = new QLabel(instruction);
        label->setWordWrap(true);
        label->setStyleSheet("color: #435159;");
        selectionForm->addRow(label);
        planeParameters_->addWidget(page);
        return selectionForm;
    };

    addSelectionPage(QStringLiteral(
        "3Dで作業平面1つと作図点1つを選択します。選んだ点を通り、元の平面と同じ向きになります。"));
    addSelectionPage(QStringLiteral(
        "3Dで平行な作業平面を2つ選択します。ちょうど中央の位置に平面を作ります。"));
    addSelectionPage(QStringLiteral(
        "3Dで同一平面上にある直線を2本選択します。交差線または平行線に対応します。"));
    addSelectionPage(QStringLiteral(
        "3Dで直線1本と作図点1つを選択します。点を通り、直線に直角な平面を作ります。"));

    QFormLayout* pathForm = addSelectionPage(QStringLiteral(
        "3Dで線または曲線を1本選択します。指定位置で経路に直角な平面を作ります。"));
    pathPlanePosition_ = MakeNumberField(50.0);
    pathPlanePosition_->setRange(0.0, 100.0);
    pathPlanePosition_->setSuffix(QStringLiteral(" %"));
    pathForm->addRow(QStringLiteral("線上位置"), pathPlanePosition_);

    addSelectionPage(QStringLiteral(
        "3Dで面または板1つと作図点1つを選択します。点に最も近い曲面位置へ接する平面を作ります。"));

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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

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
    return panel;
}

QWidget* MainWindow::BuildMachiningPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* toolHint = new QLabel(QStringLiteral(
        "ツール列の「C面取り」「R面取り」ボタン: 3D画面で直線のペアを次々クリック→\n"
        "下の値を入力→Enterで全ペアへ一括適用。離れた線は交点まで自動延長します。\n"
        "下の欄は選択済みの2直線へ1組だけ作る従来方式です。"));
    toolHint->setWordWrap(true);
    toolHint->setStyleSheet("color: #51626b;");
    layout->addWidget(toolHint);

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
        // 面取りツールでペア選択中なら一括適用(#7)。それ以外は従来の単発作成。
        if (viewport_->Tool() == ViewportTool::CornerPick && !cornerToolPairs_.empty()) {
            ApplyCornerToolPairs();
        } else if (machiningType_->currentIndex() == 0) {
            ApplyLineChamfer();
        } else {
            ApplyLineFillet();
        }
    });
    // 値の変更は選択中ペアのプレビューへ即反映(#7)。
    connect(chamferFirstDistance_, &QDoubleSpinBox::valueChanged,
        this, &MainWindow::RefreshCornerToolPreview);
    connect(chamferSecondDistance_, &QDoubleSpinBox::valueChanged,
        this, &MainWindow::RefreshCornerToolPreview);
    connect(filletRadius_, &QDoubleSpinBox::valueChanged,
        this, &MainWindow::RefreshCornerToolPreview);
    connect(machiningType_, &QComboBox::currentIndexChanged, this, [this](int index) {
        machiningValues_->setCurrentIndex(index);
        machiningApplyButton_->setText(index == 0 ? QStringLiteral("C面取りを作成") : QStringLiteral("R丸めを作成"));
        chamferName_->setText(index == 0 ? SuggestedChamferName() : SuggestedFilletName());
    });
    layout->addWidget(machiningApplyButton_);

    // --- ポリラインの角(頂点単位のC面取り/R丸め) ---
    auto* cornerTitle = new QLabel(QStringLiteral("ポリラインの角"));
    cornerTitle->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(cornerTitle);
    auto* cornerForm = new QFormLayout;
    cornerForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    polylineCornerWire_ = new QComboBox;
    polylineCornerWire_->setToolTip(
        QStringLiteral("角を加工するポリライン(多角形・折れ線)を選びます"));
    polylineCornerVertex_ = new QSpinBox;
    polylineCornerVertex_->setRange(0, 9999);
    polylineCornerVertex_->setToolTip(QStringLiteral(
        "頂点番号(0始まり)。閉じた輪郭では0が始点/終点の角です。\n"
        "開いた折れ線の両端は角ではないため加工できません"));
    cornerForm->addRow(QStringLiteral("ポリライン"), polylineCornerWire_);
    cornerForm->addRow(QStringLiteral("頂点番号"), polylineCornerVertex_);
    layout->addLayout(cornerForm);
    auto* cornerButton = new QPushButton(QStringLiteral("角を加工（上の種類と値を使用）"));
    cornerButton->setToolTip(QStringLiteral(
        "上の「加工種類」がC面取りなら「Aの切戻し」を、R丸めなら「半径」を使い、\n"
        "選んだ頂点の角を1本のポリラインのまま加工します(2D/3Dどちらの輪郭も可)"));
    connect(cornerButton, &QPushButton::clicked,
        this, &MainWindow::ApplyPolylineCornerEdit);
    layout->addWidget(cornerButton);

    // --- 作図補助(交点・点結び) ---
    auto* aidTitle = new QLabel(QStringLiteral("交点と点結び"));
    aidTitle->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(aidTitle);
    auto* intersectButton = new QPushButton(
        QStringLiteral("選択した2本の線の交点に点を作成"));
    intersectButton->setToolTip(QStringLiteral(
        "3Dビューで線を2本(Ctrl+クリックで追加)選んで実行します。\n"
        "3D空間で交わる線・円・曲線どうしの交点すべてに点を作ります"));
    connect(intersectButton, &QPushButton::clicked,
        this, &MainWindow::CreateIntersectionPoints);
    layout->addWidget(intersectButton);
    auto* joinPointsButton = new QPushButton(
        QStringLiteral("選択した2点を結ぶ線を作成"));
    joinPointsButton->setToolTip(QStringLiteral(
        "点を2つ選んで実行すると、その2点を結ぶ3D直線を作ります。\n"
        "交点に作った点どうしを結べば「任意の交点から任意の交点への線」になります"));
    connect(joinPointsButton, &QPushButton::clicked,
        this, &MainWindow::CreateLineBetweenSelectedPoints);
    layout->addWidget(joinPointsButton);

    layout->addStretch(1);
    return panel;
}

QWidget* MainWindow::BuildSurfacePanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* createTitle = new QLabel(QStringLiteral("ワイヤーから面"));
    createTitle->setProperty("manualAnchor", QStringLiteral("surfaceCreate"));
    createTitle->setStyleSheet("font-weight: 600; color: #26323a;");
    layout->addWidget(createTitle);

    auto* createForm = new QFormLayout;
    createForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    surfaceType_ = new QComboBox;
    surfaceType_->addItem(QStringLiteral("自動（選択から判定・推奨）"), -1);
    surfaceType_->addItem(QStringLiteral("閉じた輪郭から平面"), 0);
    surfaceType_->addItem(QStringLiteral("2断面から曲面"), 1);
    surfaceType_->addItem(QStringLiteral("3断面以上からロフト"), 2);
    surfaceType_->addItem(QStringLiteral("外形ガイド2本＋断面1本以上"), 3);
    surfaceType_->setToolTip(QStringLiteral(
        "「自動」は選択内容から判定します: 閉じる輪郭→平面 / 断面2→曲面 /\n"
        "断面3以上→ロフト / 断面の端が載る線2本があればガイド付き。\n"
        "判定結果は選択のたびにラベルと半透明プレビューで確認できます"));
    surfaceName_ = new QLineEdit(QStringLiteral("surface_1"));
    createForm->addRow(QStringLiteral("作り方"), surfaceType_);
    createForm->addRow(QStringLiteral("面の名前"), surfaceName_);
    surfaceKeepSectionWires_ = new QCheckBox(QStringLiteral("構成線をワイヤにする"));
    surfaceKeepSectionWires_->setToolTip(QStringLiteral(
        "面の各断面位置の線(<面名>_構成線N)を独立ワイヤとしても作ります。\n"
        "複数線をつないだ断面や隠し断面も1本の連続線になり、工作の基準線に使えます"));
    createForm->addRow(surfaceKeepSectionWires_);
    layout->addLayout(createForm);

    surfaceSelectionLabel_ = new QLabel(QStringLiteral("選択: ワイヤー0本"));
    surfaceSelectionLabel_->setStyleSheet("color: #5c6670;");
    surfaceSelectionLabel_->setWordWrap(true);
    layout->addWidget(surfaceSelectionLabel_);

    auto* groupHint = new QLabel(QStringLiteral(
        "複数線で1つの輪郭・断面を作る場合は、3Dで線を選んで下の表へ役割ごとに追加します。"));
    groupHint->setWordWrap(true);
    groupHint->setStyleSheet("color: #4f5b63;");
    layout->addWidget(groupHint);

    surfaceSelectChainButton_ = new QPushButton(QStringLiteral("接続区間を一括選択"));
    surfaceSelectChainButton_->setToolTip(QStringLiteral(
        "線を1本選んで実行すると、端点で続く直線・円弧・ベジェ・スプラインを"
        "枝分かれ点までまとめて選択します"));
    connect(surfaceSelectChainButton_, &QPushButton::clicked,
        this, &MainWindow::SelectConnectedSurfaceWireChain);
    layout->addWidget(surfaceSelectChainButton_);

    surfaceInputTable_ = new QTableWidget(0, 4);
    surfaceInputTable_->setHorizontalHeaderLabels({
        QStringLiteral("役割"), QStringLiteral("状態"),
        QStringLiteral("本数"), QStringLiteral("元の線")});
    surfaceInputTable_->verticalHeader()->setVisible(false);
    surfaceInputTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    surfaceInputTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    surfaceInputTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    surfaceInputTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    surfaceInputTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    surfaceInputTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    surfaceInputTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    surfaceInputTable_->setMinimumHeight(118);
    surfaceInputTable_->setMaximumHeight(170);
    surfaceInputTable_->setToolTip(QStringLiteral(
        "1行が1つの連続輪郭・外形・断面です。行内の線は端点でつながっている必要があります"));
    layout->addWidget(surfaceInputTable_);

    auto* groupAddRow = new QHBoxLayout;
    groupAddRow->setContentsMargins(0, 0, 0, 0);
    surfaceAddBoundaryOrGuideButton_ = new QPushButton(QStringLiteral("輪郭を追加"));
    surfaceAddSectionButton_ = new QPushButton(QStringLiteral("断面を追加"));
    surfaceAppendGroupButton_ = new QPushButton(QStringLiteral("選択行へ線を追加"));
    groupAddRow->addWidget(surfaceAddBoundaryOrGuideButton_);
    groupAddRow->addWidget(surfaceAddSectionButton_);
    groupAddRow->addWidget(surfaceAppendGroupButton_);
    layout->addLayout(groupAddRow);

    auto* groupEditRow = new QHBoxLayout;
    groupEditRow->setContentsMargins(0, 0, 0, 0);
    auto* moveGroupUpButton = new QToolButton;
    moveGroupUpButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    moveGroupUpButton->setToolTip(QStringLiteral("選択した断面を1つ前へ移動"));
    auto* moveGroupDownButton = new QToolButton;
    moveGroupDownButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    moveGroupDownButton->setToolTip(QStringLiteral("選択した断面を1つ後ろへ移動"));
    auto* removeGroupButton = new QPushButton(QStringLiteral("選択行を削除"));
    auto* clearGroupsButton = new QPushButton(QStringLiteral("割当をすべて消す"));
    groupEditRow->addWidget(moveGroupUpButton);
    groupEditRow->addWidget(moveGroupDownButton);
    groupEditRow->addWidget(removeGroupButton);
    groupEditRow->addWidget(clearGroupsButton);
    layout->addLayout(groupEditRow);

    connect(surfaceAddBoundaryOrGuideButton_, &QPushButton::clicked, this, [this] {
        AddSelectedSurfaceInputGroup(
            ConfiguredSurfaceMode() == 0
                ? SurfaceInputRole::Boundary : SurfaceInputRole::Guide);
    });
    connect(surfaceAddSectionButton_, &QPushButton::clicked, this, [this] {
        AddSelectedSurfaceInputGroup(SurfaceInputRole::Section);
    });
    connect(surfaceAppendGroupButton_, &QPushButton::clicked,
        this, &MainWindow::AppendSelectedWiresToSurfaceInputGroup);
    connect(removeGroupButton, &QPushButton::clicked,
        this, &MainWindow::RemoveSelectedSurfaceInputGroup);
    connect(clearGroupsButton, &QPushButton::clicked,
        this, &MainWindow::ClearSurfaceInputGroups);
    const auto moveGroup = [this](int direction) {
        const int row = surfaceInputTable_->currentRow();
        const int target = row + direction;
        if (row < 0 || target < 0
            || target >= static_cast<int>(surfaceInputGroups_.size())) {
            statusBar()->showMessage(QStringLiteral("移動する表の行を選択してください"), 3000);
            return;
        }
        if (surfaceInputGroups_[row].role != surfaceInputGroups_[target].role) {
            statusBar()->showMessage(
                QStringLiteral("外形と断面の区分を越えて移動することはできません"), 3500);
            return;
        }
        std::swap(surfaceInputGroups_[row], surfaceInputGroups_[target]);
        RefreshSurfaceInputTable();
        surfaceInputTable_->selectRow(target);
    };
    connect(moveGroupUpButton, &QToolButton::clicked,
        this, [moveGroup] { moveGroup(-1); });
    connect(moveGroupDownButton, &QToolButton::clicked,
        this, [moveGroup] { moveGroup(1); });
    connect(surfaceInputTable_, &QTableWidget::cellClicked,
        this, [this](int row, int) {
            if (row < 0 || row >= static_cast<int>(surfaceInputGroups_.size())) {
                return;
            }
            std::vector<CadSelection> selections;
            for (const std::string& name : surfaceInputGroups_[row].wireNames) {
                const auto found = std::find_if(
                    project_.Wires().begin(), project_.Wires().end(),
                    [&](const auto& wire) { return wire.name == name; });
                if (found != project_.Wires().end()) {
                    selections.push_back({
                        CadSelectionKind::Wire,
                        static_cast<int>(std::distance(project_.Wires().begin(), found)),
                    });
                }
            }
            UpdateSelections(std::move(selections), true);
        });
    connect(surfaceType_, &QComboBox::currentIndexChanged, this, [this] {
        surfaceInputGroups_.clear();
        RefreshSurfaceInputTable();
        UpdateSelections(viewport_->Selections(), false);
    });
    RefreshSurfaceInputTable();

    surfaceCreateButton_ = new QPushButton(QStringLiteral("選択ワイヤーから面を作成"));
    surfaceCreateButton_->setObjectName("primaryButton");
    connect(surfaceCreateButton_, &QPushButton::clicked,
        this, &MainWindow::CreateSurfaceFromSelection);
    layout->addWidget(surfaceCreateButton_);

    auto* autoSurfaceButton = new QPushButton(
        QStringLiteral("おまかせで面を作る（選択順・向き不問）"));
    autoSurfaceButton->setObjectName("primaryButton");
    autoSurfaceButton->setToolTip(QStringLiteral(
        "選んだ線から前提なしに面を作ります(オーナー指示)。\n"
        "線は端点の近さで自動連結・自動反転し、小さな隙間は自動で閉じます。\n"
        "閉じた輪郭1つ→平面またはパッチ面(穴埋め)、2本→ルールド、\n"
        "3本以上→自動整列してロフト。作れないときは理由を表示します"));
    connect(autoSurfaceButton, &QPushButton::clicked,
        this, &MainWindow::CreateAutoSurfaceFromSelection);
    layout->addWidget(autoSurfaceButton);

    auto* gordonTitle = new QLabel(QStringLiteral("断面と外形ガイドから面（Gordon面）"));
    gordonTitle->setProperty("manualAnchor", QStringLiteral("surfaceGordon"));
    gordonTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(gordonTitle);

    gordonGuideLabel_ = new QLabel(QStringLiteral("外形ガイド: (なし)"));
    gordonGuideLabel_->setWordWrap(true);
    gordonGuideLabel_->setStyleSheet("color: #5c6670;");
    layout->addWidget(gordonGuideLabel_);

    auto* gordonGuideButtons = new QWidget;
    auto* gordonGuideButtonLayout = new QHBoxLayout(gordonGuideButtons);
    gordonGuideButtonLayout->setContentsMargins(0, 0, 0, 0);
    gordonGuideButtonLayout->setSpacing(6);
    auto* addGordonGuideButton = new QPushButton(QStringLiteral("選択を外形ガイドに追加"));
    addGordonGuideButton->setToolTip(QStringLiteral("外形ガイドにする線を3D画面またはモデル一覧で選択してから押します"));
    connect(addGordonGuideButton, &QPushButton::clicked, this, &MainWindow::AddSelectedGordonGuides);
    auto* clearGordonGuideButton = new QPushButton(QStringLiteral("外形ガイドを全解除"));
    connect(clearGordonGuideButton, &QPushButton::clicked, this, &MainWindow::ClearGordonGuides);
    gordonGuideButtonLayout->addWidget(addGordonGuideButton, 1);
    gordonGuideButtonLayout->addWidget(clearGordonGuideButton, 1);
    layout->addWidget(gordonGuideButtons);

    auto* createGordonButton = new QPushButton(QStringLiteral("断面と外形で面を作成"));
    createGordonButton->setObjectName("primaryButton");
    createGordonButton->setToolTip(QStringLiteral("断面ワイヤーを2本以上、通し方向の手前から奥の順に3D画面で選択してから押します"));
    connect(createGordonButton, &QPushButton::clicked, this, &MainWindow::CreateGordonSurfaceFromSelection);
    layout->addWidget(createGordonButton);

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

    wrapProjectionOpenings_ = new QCheckBox(QStringLiteral("閉じた輪郭は面ごとの開口として登録"));
    wrapProjectionOpenings_->setChecked(true);
    wrapProjectionOpenings_->setToolTip(QStringLiteral(
        "回り込み投影で分割された閉じた輪郭を、その面の開口(窓)として自動登録します。\n"
        "実物と同じく、角で2枚の板それぞれに窓が開きます"));
    layout->addWidget(wrapProjectionOpenings_);
    auto* wrapProjectButton = new QPushButton(QStringLiteral("複数の面へ回り込み投影"));
    wrapProjectButton->setObjectName("primaryButton");
    wrapProjectButton->setToolTip(QStringLiteral(
        "パノラミックウインドウのように角をまたぐ窓に使います(#14)。\n"
        "下書きワイヤーと投影先の面2枚以上を3D画面で一緒に選択してから押します。\n"
        "下書きは面ごとに載る区間へ分割され、各面へ投影されます(下書きの分割片は非表示で保持)"));
    connect(wrapProjectButton, &QPushButton::clicked,
        this, &MainWindow::ProjectSelectedWiresAcrossSurfaces);
    layout->addWidget(wrapProjectButton);

    auto* lightCaseBox = new QGroupBox(QStringLiteral("飛び出すライトケース"));
    lightCaseBox->setObjectName(QStringLiteral("lightCaseSection"));
    lightCaseBox->setProperty("manualAnchor", QStringLiteral("lightCase"));
    auto* lightCaseLayout = new QVBoxLayout(lightCaseBox);
    lightCaseLayout->setSpacing(5);

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

    auto* plateTitle = new QLabel(QStringLiteral("厚み化（ワイヤ・面・板）"));
    plateTitle->setProperty("manualAnchor", QStringLiteral("plateCreate"));
    plateTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(plateTitle);

    // 厚み化の専用フォーム(オーナー指示: UIの使い回しをやめ、厚みの設定と
    // 出力[ワイヤ][面][板]のチェックで何を作るかを選ぶ)。
    auto* plateForm = new QFormLayout;
    plateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    plateName_ = new QLineEdit(QStringLiteral("plate_1"));
    plateSurface_ = new QComboBox;
    plateThickness_ = MakePositiveField(0.5);
    plateThickness_->setSuffix(QStringLiteral(" mm"));
    plateVariableThickness_ = new QCheckBox(QStringLiteral("終端まで厚みを変化"));
    plateEndThickness_ = MakePositiveField(0.5);
    plateEndThickness_->setSuffix(QStringLiteral(" mm"));
    plateEndThickness_->setEnabled(false);
    plateDirection_ = new QComboBox;
    plateDirection_->addItem(QStringLiteral("+側へ（法線矢印側）"), static_cast<int>(PlateThicknessDirection::Positive));
    plateDirection_->addItem(QStringLiteral("中央（両側へ半分）"), static_cast<int>(PlateThicknessDirection::Centered));
    plateDirection_->addItem(QStringLiteral("-側へ（矢印と反対）"), static_cast<int>(PlateThicknessDirection::Negative));
    plateForm->addRow(QStringLiteral("名前"), plateName_);
    plateForm->addRow(QStringLiteral("元の面"), plateSurface_);
    plateForm->addRow(QStringLiteral("厚み（始端）"), plateThickness_);
    plateForm->addRow(plateVariableThickness_);
    plateForm->addRow(QStringLiteral("厚み（終端）"), plateEndThickness_);
    plateForm->addRow(QStringLiteral("厚み方向"), plateDirection_);

    // その厚みを何にするか: 3つのチェックの組み合わせで出力を選ぶ。
    auto* thicknessOutputs = new QWidget;
    auto* thicknessOutputLayout = new QHBoxLayout(thicknessOutputs);
    thicknessOutputLayout->setContentsMargins(0, 0, 0, 0);
    thicknessOutputLayout->setSpacing(10);
    thicknessMakeWire_ = new QCheckBox(QStringLiteral("ワイヤ"));
    thicknessMakeWire_->setToolTip(QStringLiteral(
        "元面の輪郭・断面を厚みぶん法線方向へずらした独立ワイヤを作ります"));
    thicknessMakeSurface_ = new QCheckBox(QStringLiteral("面"));
    thicknessMakeSurface_->setToolTip(QStringLiteral(
        "厚みぶん法線方向へずらした面(反対側表面、断面ロフト近似)を作ります"));
    thicknessMakePlate_ = new QCheckBox(QStringLiteral("板"));
    thicknessMakePlate_->setChecked(true);
    thicknessMakePlate_->setToolTip(QStringLiteral(
        "閉じた3D板材を作ります(材質は板にだけ使われます)"));
    thicknessOutputLayout->addWidget(thicknessMakeWire_);
    thicknessOutputLayout->addWidget(thicknessMakeSurface_);
    thicknessOutputLayout->addWidget(thicknessMakePlate_);
    thicknessOutputLayout->addStretch(1);
    plateForm->addRow(QStringLiteral("厚みで作るもの"), thicknessOutputs);

    plateMaterial_ = new QComboBox;
    plateMaterial_->addItem(QStringLiteral("プラ板"), QStringLiteral("styrene"));
    plateMaterial_->addItem(QStringLiteral("紙・厚紙"), QStringLiteral("paper"));
    plateMaterial_->addItem(QStringLiteral("真鍮板"), QStringLiteral("brass"));
    plateMaterial_->addItem(QStringLiteral("その他"), QStringLiteral("other"));
    plateForm->addRow(QStringLiteral("材質（板のみ）"), plateMaterial_);
    layout->addLayout(plateForm);

    connect(plateVariableThickness_, &QCheckBox::toggled, plateEndThickness_, &QWidget::setEnabled);
    connect(plateThickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!plateVariableThickness_->isChecked()) {
            plateEndThickness_->setValue(value);
        }
    });
    connect(thicknessMakePlate_, &QCheckBox::toggled, plateMaterial_, &QWidget::setEnabled);

    auto* plateButton = new QPushButton(QStringLiteral("選択した面へ厚みを適用"));
    plateButton->setObjectName("primaryButton");
    plateButton->setToolTip(QStringLiteral(
        "「元の面」の面へ厚みを適用し、チェックした出力(ワイヤ・面・板)を作ります"));
    connect(plateButton, &QPushButton::clicked, this, &MainWindow::CreatePlateFromSurface);
    layout->addWidget(plateButton);

    auto* wirePlateButton = new QPushButton(QStringLiteral("選択ワイヤーから直接厚み化（板）"));
    wirePlateButton->setToolTip(QStringLiteral(
        "選択したワイヤーから面を作り、そのまま板にします。\n"
        "通常は1閉輪郭で平板、2断面で曲面板、3断面以上でロフト板。"
        "外形ガイド方式を選んだ場合は、外形2本＋断面1本以上から板を作ります"));
    connect(wirePlateButton, &QPushButton::clicked, this, &MainWindow::CreatePlateFromSelectedWires);
    layout->addWidget(wirePlateButton);

    auto* plateUpdateButton = new QPushButton(QStringLiteral("選択中の板材へ設定"));
    connect(plateUpdateButton, &QPushButton::clicked, this, &MainWindow::UpdateSelectedPlate);
    layout->addWidget(plateUpdateButton);

    // 板材化後の補助: 投影輪郭を板厚位置へ複製する(旧「厚み位置のワイヤ」タブを
    // 廃止し、厚み化セクションのサブ機能として残す。オーナー指示: 被るタブは消す)。
    auto* offsetWireBox = new QGroupBox(QStringLiteral("投影輪郭を厚み位置へ複製"));
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
    offsetWireButton->setToolTip(QStringLiteral(
        "できあがった板材の任意の厚み位置に輪郭ワイヤーを作ります(治具や罫書き線用)。\n"
        "板材1枚と、その元面へ投影された輪郭を選択してから押します"));
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

    auto* surfaceOpeningHint = new QLabel(QStringLiteral(
        "面の段階でも開口を登録できます。面1つ+閉じた投影輪郭を選択:"));
    surfaceOpeningHint->setWordWrap(true);
    surfaceOpeningHint->setStyleSheet("color: #5c6670; margin-top: 4px;");
    layout->addWidget(surfaceOpeningHint);
    auto* surfaceOpeningButtons = new QWidget;
    auto* surfaceOpeningLayout = new QHBoxLayout(surfaceOpeningButtons);
    surfaceOpeningLayout->setContentsMargins(0, 0, 0, 0);
    surfaceOpeningLayout->setSpacing(6);
    auto* addSurfaceOpeningButton = new QPushButton(QStringLiteral("面の開口に追加"));
    addSurfaceOpeningButton->setToolTip(QStringLiteral(
        "面に登録した開口は、面入力の近似モデル・型紙・実体化と、\n"
        "この面から後で作る板材へ自動で引き継がれます"));
    connect(addSurfaceOpeningButton, &QPushButton::clicked,
        this, &MainWindow::AddSelectedSurfaceOpenings);
    auto* removeSurfaceOpeningButton = new QPushButton(QStringLiteral("面の開口から外す"));
    connect(removeSurfaceOpeningButton, &QPushButton::clicked,
        this, &MainWindow::RemoveSelectedSurfaceOpenings);
    surfaceOpeningLayout->addWidget(addSurfaceOpeningButton, 1);
    surfaceOpeningLayout->addWidget(removeSurfaceOpeningButton, 1);
    layout->addWidget(surfaceOpeningButtons);

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
    plateSplitAxis_->addItem(QStringLiteral("断面に沿う方向（周方向）"), static_cast<int>(PlateSplitAxis::U));
    plateSplitAxis_->addItem(QStringLiteral("長手方向（通し方向）"), static_cast<int>(PlateSplitAxis::V));
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

    // --- 積層(重ね板、合意9) ---
    auto* laminateTitle = new QLabel(QStringLiteral("板材を重ねて積層"));
    laminateTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(laminateTitle);
    auto* laminateHint = new QLabel(QStringLiteral(
        "薄板を重ねて段差の表現や補強をします。同じ元面の積層は下の板の外側へ\n"
        "自動でずれて重なり、下の板の厚みを変えると追従します。"));
    laminateHint->setWordWrap(true);
    laminateHint->setStyleSheet("color: #5b6a74;");
    layout->addWidget(laminateHint);
    auto* laminateForm = new QFormLayout;
    laminateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    laminateThicknessSpin_ = MakePositiveField(0.3);
    laminateThicknessSpin_->setSuffix(QStringLiteral(" mm"));
    laminateForm->addRow(QStringLiteral("1層の板厚"), laminateThicknessSpin_);
    laminateCountSpin_ = new QSpinBox;
    laminateCountSpin_->setRange(1, 20);
    laminateCountSpin_->setValue(1);
    laminateCountSpin_->setToolTip(QStringLiteral("追加する層の枚数(下の板は数えません)"));
    laminateForm->addRow(QStringLiteral("追加する層数"), laminateCountSpin_);
    laminateTargetHeightSpin_ = new QDoubleSpinBox;
    laminateTargetHeightSpin_->setRange(0.0, 100.0);
    laminateTargetHeightSpin_->setDecimals(2);
    laminateTargetHeightSpin_->setSingleStep(0.1);
    laminateTargetHeightSpin_->setSuffix(QStringLiteral(" mm"));
    laminateTargetHeightSpin_->setToolTip(
        QStringLiteral("目標の段差高さ。入力すると必要な層数を下に提案します(0なら未使用)"));
    laminateForm->addRow(QStringLiteral("目標高さ"), laminateTargetHeightSpin_);
    laminateSuggestLabel_ = new QLabel;
    laminateSuggestLabel_->setStyleSheet("color: #5b6a74;");
    laminateForm->addRow(QString(), laminateSuggestLabel_);
    layout->addLayout(laminateForm);
    const auto updateLaminateSuggestion = [this] {
        const double height = laminateTargetHeightSpin_->value();
        const double thickness = laminateThicknessSpin_->value();
        if (height <= 0.0 || thickness <= 0.0) {
            laminateSuggestLabel_->clear();
            return;
        }
        const int layers = std::max(1, static_cast<int>(std::ceil(height / thickness - 1.0e-9)));
        laminateSuggestLabel_->setText(
            QStringLiteral("提案: %1層（%2 mm × %1 = %3 mm）")
                .arg(layers)
                .arg(thickness, 0, 'f', 2)
                .arg(layers * thickness, 0, 'f', 2));
    };
    connect(laminateThicknessSpin_, &QDoubleSpinBox::valueChanged, this,
        [updateLaminateSuggestion](double) { updateLaminateSuggestion(); });
    connect(laminateTargetHeightSpin_, &QDoubleSpinBox::valueChanged, this,
        [updateLaminateSuggestion](double) { updateLaminateSuggestion(); });
    auto* laminateAddButton = new QPushButton(QStringLiteral("選択中の板材の上に積層を追加"));
    laminateAddButton->setObjectName("primaryButton");
    laminateAddButton->setToolTip(QStringLiteral(
        "3D画面で板材を1枚選んでから押します。その外側へ指定枚数の層を追加します"));
    connect(laminateAddButton, &QPushButton::clicked, this, &MainWindow::AddLaminationToSelectedPlate);
    layout->addWidget(laminateAddButton);
    auto* laminateLinkRow = new QHBoxLayout;
    auto* laminateLinkButton = new QPushButton(QStringLiteral("選択2枚を積層関係に"));
    laminateLinkButton->setToolTip(QStringLiteral(
        "全層を自分で描いた場合に使います。先に選んだ板が下、後に選んだ板が上です。\n"
        "同じ元面なら幾何も追従し、別の面なら関係の記録だけを付けます"));
    connect(laminateLinkButton, &QPushButton::clicked, this, &MainWindow::LinkSelectedPlatesAsLaminate);
    auto* laminateClearButton = new QPushButton(QStringLiteral("積層関係を解除"));
    laminateClearButton->setToolTip(QStringLiteral("選択中の板材の積層関係を外します"));
    connect(laminateClearButton, &QPushButton::clicked, this, &MainWindow::ClearSelectedPlateLaminate);
    laminateLinkRow->addWidget(laminateLinkButton, 1);
    laminateLinkRow->addWidget(laminateClearButton, 1);
    layout->addLayout(laminateLinkRow);

    // --- 押し出し(統合。オーナー指示: 厚み化・オフセット面もここへ) ---
    auto* sweepTitle = new QLabel(QStringLiteral("押し出し"));
    sweepTitle->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 10px;");
    layout->addWidget(sweepTitle);
    auto* extrudeHint = new QLabel(QStringLiteral(
        "3D画面や一覧で押し出す物（線・面。複数可）を選び、方向と距離を決めて"
        "「押し出す」を押します。閉じた輪郭ならふた面も作れます。"
        "作られた物は元の線・面の編集に追従します。"));
    extrudeHint->setWordWrap(true);
    extrudeHint->setStyleSheet("color: #4a5a63;");
    layout->addWidget(extrudeHint);
    auto* extrudeForm = new QFormLayout;
    extrudeForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    extrudeDirection_ = new QComboBox;
    extrudeDirection_->addItem(QStringLiteral("面・作図面の法線（自動）"), -1);
    extrudeDirection_->addItem(QStringLiteral("法線の反対（自動・逆）"), -2);
    extrudeDirection_->addItem(QStringLiteral("+X"), 0);
    extrudeDirection_->addItem(QStringLiteral("-X"), 1);
    extrudeDirection_->addItem(QStringLiteral("+Y"), 2);
    extrudeDirection_->addItem(QStringLiteral("-Y"), 3);
    extrudeDirection_->addItem(QStringLiteral("+Z"), 4);
    extrudeDirection_->addItem(QStringLiteral("-Z"), 5);
    extrudeDirection_->setToolTip(QStringLiteral(
        "自動: 面はその面の法線、線は作図面の法線（作図面が無ければ+Z）"));
    extrudeForm->addRow(QStringLiteral("押し出し方向"), extrudeDirection_);
    extrudeDistance_ = MakePositiveField(20.0);
    extrudeDistance_->setSuffix(QStringLiteral(" mm"));
    extrudeForm->addRow(QStringLiteral("押し出し距離"), extrudeDistance_);
    extrudeToSurfaceCheck_ = new QCheckBox(QStringLiteral("距離のかわりに選んだ面まで"));
    extrudeToSurfaceCheck_->setToolTip(QStringLiteral(
        "押し出し先を面で指定します（線の押し出しのみ）。線の各点をその面まで伸ばします"));
    extrudeForm->addRow(extrudeToSurfaceCheck_);
    extrudeTargetSurface_ = new QComboBox;
    extrudeTargetSurface_->setEnabled(false);
    extrudeForm->addRow(QStringLiteral("到達面"), extrudeTargetSurface_);
    connect(extrudeToSurfaceCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        extrudeTargetSurface_->setEnabled(checked);
        extrudeDistance_->setEnabled(!checked);
    });
    layout->addLayout(extrudeForm);

    auto* extrudeMakeLabel = new QLabel(QStringLiteral("押し出しで作るもの"));
    extrudeMakeLabel->setStyleSheet("font-weight: 600; color: #26323a; margin-top: 4px;");
    layout->addWidget(extrudeMakeLabel);
    extrudeMakeTipWire_ = new QCheckBox(QStringLiteral("先端のワイヤ"));
    extrudeMakeTipWire_->setChecked(true);
    extrudeMakeTipWire_->setToolTip(QStringLiteral(
        "押し出した先の輪郭線。元の線を編集すると追従します"));
    extrudeMakeSide_ = new QCheckBox(QStringLiteral("側面（押し出した面）"));
    extrudeMakeSide_->setChecked(true);
    extrudeMakeSide_->setToolTip(QStringLiteral("元の輪郭と先端の輪郭の間に張る面"));
    extrudeMakeCap_ = new QCheckBox(QStringLiteral("先端のふた面（閉じた輪郭のみ）"));
    extrudeMakeCap_->setToolTip(QStringLiteral(
        "先端側を塞ぐ面。閉じた輪郭のときだけ作れます"));
    extrudeMakeBottom_ = new QCheckBox(QStringLiteral("元の位置のふた面（閉じた輪郭のみ）"));
    extrudeMakeBottom_->setToolTip(QStringLiteral(
        "元の位置側を塞ぐ面。側面＋両ふたで全面が面付きになります"));
    extrudeMakePlate_ = new QCheckBox(QStringLiteral("板材にする（厚みを付ける）"));
    extrudeMakePlate_->setToolTip(QStringLiteral(
        "面に厚みを与えて実物の板にします。面を押し出したときは厚み化と同じです"));
    layout->addWidget(extrudeMakeTipWire_);
    layout->addWidget(extrudeMakeSide_);
    layout->addWidget(extrudeMakeCap_);
    layout->addWidget(extrudeMakeBottom_);
    layout->addWidget(extrudeMakePlate_);
    auto* extrudePlateForm = new QFormLayout;
    extrudePlateForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    extrudePlateThickness_ = MakePositiveField(0.5);
    extrudePlateThickness_->setSuffix(QStringLiteral(" mm"));
    extrudePlateForm->addRow(QStringLiteral("板厚"), extrudePlateThickness_);
    extrudePlateMaterial_ = new QComboBox;
    extrudePlateMaterial_->addItem(QStringLiteral("プラ板"), QStringLiteral("プラ板"));
    extrudePlateMaterial_->addItem(QStringLiteral("紙"), QStringLiteral("紙"));
    extrudePlateMaterial_->addItem(QStringLiteral("金属"), QStringLiteral("金属"));
    extrudePlateForm->addRow(QStringLiteral("材料"), extrudePlateMaterial_);
    layout->addLayout(extrudePlateForm);
    auto* extrudeButton = new QPushButton(QStringLiteral("押し出す"));
    extrudeButton->setObjectName("primaryButton");
    extrudeButton->setToolTip(QStringLiteral(
        "選択した線・面を押し出します。線は複数同時でも構いません。\n"
        "面を選んだ場合は厚み方向の押し出し（＝厚み化・オフセット面）になります"));
    connect(extrudeButton, &QPushButton::clicked, this, &MainWindow::ExtrudeSelection);
    layout->addWidget(extrudeButton);

    auto* revolveForm = new QFormLayout;
    revolveForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    revolveAxis_ = new QComboBox;
    revolveAxis_->addItem(QStringLiteral("X軸まわり"), 0);
    revolveAxis_->addItem(QStringLiteral("Y軸まわり"), 1);
    revolveAxis_->addItem(QStringLiteral("Z軸まわり"), 2);
    revolveAxis_->setToolTip(QStringLiteral(
        "軸は原点を通ります。作図点を1つ一緒に選ぶと、その点を通る軸になります"));
    revolveForm->addRow(QStringLiteral("回転軸"), revolveAxis_);
    revolveAngle_ = new QDoubleSpinBox;
    revolveAngle_->setRange(1.0, 360.0);
    revolveAngle_->setDecimals(1);
    revolveAngle_->setValue(360.0);
    revolveAngle_->setSuffix(QStringLiteral(" °"));
    revolveForm->addRow(QStringLiteral("回転角"), revolveAngle_);
    revolveSections_ = new QSpinBox;
    revolveSections_->setRange(4, 64);
    revolveSections_->setValue(24);
    revolveSections_->setToolTip(QStringLiteral("回転を近似する断面の数。多いほど滑らか"));
    revolveForm->addRow(QStringLiteral("断面数"), revolveSections_);
    layout->addLayout(revolveForm);
    auto* revolveButton = new QPushButton(QStringLiteral("選択ワイヤーを回転して面を作成"));
    revolveButton->setObjectName("primaryButton");
    revolveButton->setToolTip(QStringLiteral(
        "3D画面で断面ワイヤーを1本選んでから押します(回転体・ろくろ形状)。\n"
        "作図点を1つ追加選択すると、その点を通る軸で回します"));
    connect(revolveButton, &QPushButton::clicked, this, &MainWindow::CreateRevolvedSurface);
    layout->addWidget(revolveButton);

    auto* offsetForm = new QFormLayout;
    offsetForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    offsetSurfaceDistance_ = new QDoubleSpinBox;
    offsetSurfaceDistance_->setRange(-50.0, 50.0);
    offsetSurfaceDistance_->setDecimals(2);
    offsetSurfaceDistance_->setSingleStep(0.1);
    offsetSurfaceDistance_->setValue(0.5);
    offsetSurfaceDistance_->setSuffix(QStringLiteral(" mm"));
    offsetSurfaceDistance_->setToolTip(QStringLiteral(
        "法線方向のオフセット量。+は法線側、-は反対側"));
    offsetForm->addRow(QStringLiteral("オフセット量"), offsetSurfaceDistance_);
    layout->addLayout(offsetForm);
    auto* offsetButton = new QPushButton(QStringLiteral("選択面のオフセット面を作成"));
    offsetButton->setObjectName("primaryButton");
    offsetButton->setToolTip(QStringLiteral(
        "3D画面で面を1つ選んでから押します。法線方向へずらした近似面(断面ロフト)を作ります。\n"
        "内張り・裏打ちの土台に使えます(積層は板材どうしの「積層」も参照)"));
    connect(offsetButton, &QPushButton::clicked, this, &MainWindow::CreateOffsetSurfaceApproximation);
    layout->addWidget(offsetButton);
    layout->addStretch(1);

    // モードのツール(上部)で選んだ1セクションだけを表示する(ADR 0025)。
    surfaceSections_ = SectionizeVerticalLayout(layout, {
        QStringLiteral("ワイヤーから面"),
        QStringLiteral("断面と外形ガイドから面（Gordon面）"),
        QStringLiteral("平面図を面へ投影"),
        QStringLiteral("飛び出すライトケース"),
        QStringLiteral("厚み化（ワイヤ・面・板）"),
        QStringLiteral("曲面から成形治具"),
        QStringLiteral("板材に開口"),
        QStringLiteral("展開時の切れ目"),
        QStringLiteral("展開片の分割線"),
        QStringLiteral("板材を分割"),
        QStringLiteral("板材を重ねて積層"),
        QStringLiteral("押し出し"),
    });

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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

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

    // スケッチ用スナップ・点グリッド設定(ADR 0021: 設定は表示タブへ集約)
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
    gridOutsideDrawingCheck_ = new QCheckBox(QStringLiteral("作図モード以外でも表示"));
    gridOutsideDrawingCheck_->setToolTip(QStringLiteral(
        "通常、点グリッドは作図モード(作図面を選んでいるとき)だけ表示します。\n"
        "面・板材や出力モードでも表示したい場合にチェックしてください"));
    gridLayout->addRow(gridOutsideDrawingCheck_);
    dimOtherPlanesCheck_ = new QCheckBox(QStringLiteral("作図面以外の線を常に薄く表示"));
    dimOtherPlanesCheck_->setToolTip(QStringLiteral(
        "作図面上にない線・点を薄く表示します。\n"
        "作図ツールの使用中はチェックに関わらず自動で薄くなります"));
    gridLayout->addRow(dimOtherPlanesCheck_);
    gridLayout->addRow(QStringLiteral("副点"), gridSubdivision_);
    gridLayout->addRow(QStringLiteral("基準 X"), gridOrigin_[0]);
    gridLayout->addRow(QStringLiteral("基準 Y"), gridOrigin_[1]);
    gridLayout->addRow(moveGridOrigin);
    gridLayout->addRow(resetGridOrigin);
    layout->addWidget(gridBox);

    connect(snapStepField_, &QDoubleSpinBox::valueChanged, viewport_, &CadViewport::SetSnapStep);
    connect(gridPointsVisible_, &QCheckBox::toggled, this, [this] {
        ApplyGridVisibility();
        SaveDisplaySettings();
    });
    connect(gridOutsideDrawingCheck_, &QCheckBox::toggled, this, [this] {
        ApplyGridVisibility();
        SaveDisplaySettings();
    });
    connect(dimOtherPlanesCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        viewport_->SetOffPlaneDimmingAlways(checked);
        SaveDisplaySettings();
    });
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

    auto* resetButton = new QPushButton(QStringLiteral("表示設定を初期値に戻す"));
    layout->addWidget(resetButton);

    // 各ウィンドウの表示切り替え(オーナー指示、ADR 0025)。
    {
        auto* windowsBox = new QGroupBox(QStringLiteral("ウィンドウの表示"));
        auto* windowsLayout = new QVBoxLayout(windowsBox);
        windowsLayout->setSpacing(4);
        auto* modelCheck = new QCheckBox(QStringLiteral("モデルパネル（左）"));
        auto* toolsCheck = new QCheckBox(QStringLiteral("作図と編集パネル（右）"));
        auto* guideCheck = new QCheckBox(QStringLiteral("操作ガイド"));
        auto* measureCheck = new QCheckBox(QStringLiteral("測定結果ウィンドウ"));
        modelCheck->setChecked(true);
        toolsCheck->setChecked(true);
        guideCheck->setChecked(true);
        measureCheck->setChecked(false);
        connect(modelCheck, &QCheckBox::toggled, this, [this](bool visible) {
            if (modelDock_ != nullptr) modelDock_->setVisible(visible);
        });
        connect(toolsCheck, &QCheckBox::toggled, this, [this](bool visible) {
            if (toolsDock_ != nullptr) toolsDock_->setVisible(visible);
        });
        // ドック側の×やメニューで閉じ/開きしたときもチェック状態を追従させる。
        if (modelDock_ != nullptr) {
            connect(modelDock_->toggleViewAction(), &QAction::toggled,
                modelCheck, &QCheckBox::setChecked);
        }
        if (toolsDock_ != nullptr) {
            connect(toolsDock_->toggleViewAction(), &QAction::toggled,
                toolsCheck, &QCheckBox::setChecked);
        }
        connect(guideCheck, &QCheckBox::toggled, this, [this](bool visible) {
            if (guideSection_ != nullptr) guideSection_->setVisible(visible);
        });
        connect(measureCheck, &QCheckBox::toggled, this, [this](bool visible) {
            if (visible) {
                EnsureMeasurementWindow();
                UpdateMeasurementWindow();
                measurementWindow_->show();
                measurementWindow_->raise();
            } else if (measurementWindow_ != nullptr) {
                measurementWindow_->hide();
            }
        });
        windowsLayout->addWidget(modelCheck);
        windowsLayout->addWidget(toolsCheck);
        windowsLayout->addWidget(guideCheck);
        windowsLayout->addWidget(measureCheck);
        layout->addWidget(windowsBox);
    }
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
    gridOutsideDrawingCheck_->setChecked(
        settings.value("display/gridOutsideDrawing", false).toBool());
    dimOtherPlanesCheck_->setChecked(
        settings.value("display/dimOtherPlanes", false).toBool());
    viewport_->SetOffPlaneDimmingAlways(dimOtherPlanesCheck_->isChecked());
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
    settings.setValue("display/gridOutsideDrawing", gridOutsideDrawingCheck_->isChecked());
    settings.setValue("display/dimOtherPlanes",
        dimOtherPlanesCheck_ != nullptr && dimOtherPlanesCheck_->isChecked());
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
    measurementLayout->setSpacing(5);
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
