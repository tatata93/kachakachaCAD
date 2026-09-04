// 出力タブと各種書き出し(1:1図面・ペーパークラフト・STL/STEP・組立ガイド)。
// MainWindow.cpp から逐語移動(ADR 0018/0022)。

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

QWidget* MainWindow::BuildOutputPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ADR 0020 第1段: 長い縦一列フォームを折りたたみセクションへ分ける。
    const auto beginSection = [] {
        auto* content = new QWidget;
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(4, 2, 4, 6);
        contentLayout->setSpacing(6);
        return std::pair<QWidget*, QVBoxLayout*>{content, contentLayout};
    };
    auto [planarContent, planarLayout] = beginSection();
    auto [plateContent, plateLayout] = beginSection();
    auto [modelContent, modelLayout] = beginSection();

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    exportPlane_ = new QComboBox;
    exportScope_ = new QComboBox;
    exportScope_->addItems({QStringLiteral("出力面上の全ワイヤー"), QStringLiteral("選択したワイヤーのみ")});
    form->addRow(QStringLiteral("出力面"), exportPlane_);
    form->addRow(QStringLiteral("対象"), exportScope_);
    form->addRow(QStringLiteral("寸法"), new QLabel(QStringLiteral("mm / 1:1")));
    planarLayout->addLayout(form);

    exportSummary_ = new QLabel(QStringLiteral("0本"));
    exportSummary_->setStyleSheet("color: #5c6670;");
    planarLayout->addWidget(exportSummary_);

    auto* svgButton = new QPushButton(QStringLiteral("SVGを保存"));
    svgButton->setObjectName("primaryButton");
    svgButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    auto* dxfButton = new QPushButton(QStringLiteral("DXFを保存"));
    dxfButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(svgButton, &QPushButton::clicked, this, [this] { ExportPlanar(false); });
    connect(dxfButton, &QPushButton::clicked, this, [this] { ExportPlanar(true); });
    connect(exportPlane_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    connect(exportScope_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    planarLayout->addWidget(svgButton);
    planarLayout->addWidget(dxfButton);

    plateFlatPatternSummary_ = new QLabel(QStringLiteral("選択板材: なし"));
    plateFlatPatternSummary_->setWordWrap(true);
    plateFlatPatternSummary_->setStyleSheet("color: #5c6670;");
    plateLayout->addWidget(plateFlatPatternSummary_);

    auto* flatModelForm = new QFormLayout;
    flatModelForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    plateFlatPatternName_ = new QLineEdit(QStringLiteral("developed_1"));
    plateFlatPatternPlane_ = new QComboBox;
    plateFlatPatternAutoRelief_ = new QCheckBox(
        QStringLiteral("ペーパークラフトとして分割・切れ目を生成"));
    plateFlatPatternAutoRelief_->setObjectName(QStringLiteral("plateFlatPatternAutoRelief"));
    plateFlatPatternAutoRelief_->setChecked(true);
    plateFlatPatternAutoRelief_->setToolTip(
        QStringLiteral("二方向に曲がる面へ、選んだ再現度の分割・折り目・切れ込みを生成"));
    plateFlatPatternAssemblyStrategy_ = new QComboBox;
    plateFlatPatternAssemblyStrategy_->addItem(
        QStringLiteral("一枚を優先（無理な箇所は分割）"),
        static_cast<int>(PlateAssemblyStrategy::SingleSheet));
    plateFlatPatternAssemblyStrategy_->addItem(
        QStringLiteral("部品へ分割して接着"),
        static_cast<int>(PlateAssemblyStrategy::SplitPieces));
    plateFlatPatternAssemblyStrategy_->setCurrentIndex(0);
    plateFlatPatternAssemblyStrategy_->setToolTip(
        QStringLiteral("二方向曲面は伸ばさず一枚にできない場合があります。その場合は組立可能な部品へ自動分割します"));
    plateFlatPatternCutDirection_ = new QComboBox;
    plateFlatPatternCutDirection_->addItem(
        QStringLiteral("縦だけ（上下へ走る切れ目）"),
        static_cast<int>(PapercraftCutDirection::Vertical));
    plateFlatPatternCutDirection_->addItem(
        QStringLiteral("横だけ（左右へ走る切れ目）"),
        static_cast<int>(PapercraftCutDirection::Horizontal));
    plateFlatPatternCutDirection_->addItem(
        QStringLiteral("縦横を併用"),
        static_cast<int>(PapercraftCutDirection::Both));
    plateFlatPatternCutDirection_->setCurrentIndex(2);
    plateFlatPatternCutDirection_->setToolTip(
        QStringLiteral("板を分ける方向。縦横併用は小片が増えますが二方向の丸みを最も忠実に再現します"));
    plateFlatPatternAllowNotches_ = new QCheckBox(QStringLiteral("必要箇所のV字切れ込みを許可"));
    plateFlatPatternAllowNotches_->setChecked(true);
    plateFlatPatternAllowNotches_->setToolTip(
        QStringLiteral("丸い角や収束部だけにV字を加えます。分割部品にも適用できます"));
    plateFlatPatternNotchStyle_ = new QComboBox;
    plateFlatPatternNotchStyle_->addItem(
        QStringLiteral("形状追従の曲線切れ込み"),
        static_cast<int>(ReliefNotchStyle::CurvedV));
    plateFlatPatternNotchStyle_->addItem(
        QStringLiteral("鋭いV字"),
        static_cast<int>(ReliefNotchStyle::SharpV));
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
    plateFlatPatternAdvancedSpacing_ = new QCheckBox(
        QStringLiteral("折り目・切れ込み間隔を個別指定"));
    plateFlatPatternAdvancedSpacing_->setChecked(false);
    plateFlatPatternAdvancedSpacing_->setToolTip(
        QStringLiteral("通常は立体再現度から自動決定します。加工条件を固定したい場合だけ使用"));
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
    plateFlatPatternNotchCurveStrength_ = MakeNumberField(100.0);
    plateFlatPatternNotchCurveStrength_->setRange(0.0, 100.0);
    plateFlatPatternNotchCurveStrength_->setDecimals(0);
    plateFlatPatternNotchCurveStrength_->setSingleStep(5.0);
    plateFlatPatternNotchCurveStrength_->setSuffix(QStringLiteral(" %"));
    plateFlatPatternNotchCurveStrength_->setToolTip(
        QStringLiteral("元曲面を展開した曲がりを切断辺へ反映する強さ。100%で形状に追従"));
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
    plateAssemblyApproximationPreview_ = new QCheckBox(
        QStringLiteral("展開から組立まで3D表示"));
    plateAssemblyApproximationPreview_->setObjectName(
        QStringLiteral("plateAssemblyApproximationPreview"));
    plateAssemblyApproximationPreview_->setChecked(true);
    plateAssemblyApproximationPreview_->setToolTip(
        QStringLiteral("平らな展開片が折り線を軸に回転し、完成形になる過程を表示"));
    plateAssemblyProgress_ = new QSlider(Qt::Horizontal);
    plateAssemblyProgress_->setRange(0, 100);
    plateAssemblyProgress_->setValue(100);
    plateAssemblyProgress_->setTickPosition(QSlider::TicksBelow);
    plateAssemblyProgress_->setTickInterval(10);
    plateAssemblyProgressLabel_ = new QLabel(QStringLiteral("100%（完成形）"));
    plateAssemblyPreviewTimer_ = new QTimer(this);
    plateAssemblyPreviewTimer_->setSingleShot(true);
    plateAssemblyPreviewTimer_->setInterval(100);
    connect(plateAssemblyPreviewTimer_, &QTimer::timeout,
        this, &MainWindow::UpdatePlateAssemblyGuidePreview);
    auto* assemblyProgressControl = new QWidget;
    auto* assemblyProgressLayout = new QHBoxLayout(assemblyProgressControl);
    assemblyProgressLayout->setContentsMargins(0, 0, 0, 0);
    assemblyProgressLayout->setSpacing(7);
    assemblyProgressLayout->addWidget(plateAssemblyProgress_, 1);
    assemblyProgressLayout->addWidget(plateAssemblyProgressLabel_);
    plateAssemblyOutputPiece_ = new QComboBox;
    plateAssemblyOutputPiece_->addItem(QStringLiteral("全ての分割部品"), -1);
    plateAssemblyOutputPiece_->setToolTip(
        QStringLiteral("曲げ途中の3DモデルやSTL/STEPへ含める部品を選択"));
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
    flatModelForm->addRow(QStringLiteral("組み立て方"), plateFlatPatternAssemblyStrategy_);
    flatModelForm->addRow(QStringLiteral("切れ目の方向"), plateFlatPatternCutDirection_);
    flatModelForm->addRow(QStringLiteral("立体再現度"), fidelityControl);
    flatModelForm->addRow(plateFlatPatternAllowNotches_);
    flatModelForm->addRow(QStringLiteral("切れ込み形状"), plateFlatPatternNotchStyle_);
    flatModelForm->addRow(plateFlatPatternAdvancedSpacing_);
    flatModelForm->addRow(QStringLiteral("切れ込み間隔"), plateFlatPatternReliefSpacing_);
    flatModelForm->addRow(QStringLiteral("切れ込み深さ"), plateFlatPatternReliefDepth_);
    flatModelForm->addRow(QStringLiteral("V字の開き角"), plateFlatPatternNotchAngle_);
    flatModelForm->addRow(QStringLiteral("切れ込み曲線追従"), plateFlatPatternNotchCurveStrength_);
    flatModelForm->addRow(QStringLiteral("反応する最小曲がり"), plateFlatPatternMinimumBendAngle_);
    flatModelForm->addRow(plateAssemblyGuidePreview_);
    flatModelForm->addRow(plateAssemblyApproximationPreview_);
    flatModelForm->addRow(QStringLiteral("組立過程"), assemblyProgressControl);
    flatModelForm->addRow(QStringLiteral("3D出力する部品"), plateAssemblyOutputPiece_);
    flatModelForm->addRow(QStringLiteral("折り線間隔"), plateFlatPatternFoldSpacing_);
    flatModelForm->addRow(QStringLiteral("3D切り幅"), plateFlatPatternCutWidth_);
    plateLayout->addLayout(flatModelForm);

    auto* createFlatModelButton = new QPushButton(
        QStringLiteral("ペーパークラフト部品を作成"));
    createFlatModelButton->setObjectName("primaryButton");
    createFlatModelButton->setProperty("plateFlatPatternModelAction", true);
    createFlatModelButton->setToolTip(QStringLiteral("選択板材を配置平面へ展開し、編集可能な線と厚み付き板を作成"));
    connect(createFlatModelButton, &QPushButton::clicked,
        this, &MainWindow::CreateSelectedPlateFlatPatternModel);
    plateLayout->addWidget(createFlatModelButton);

    auto* assemblyModelButton = new QPushButton(QStringLiteral("この曲げ状態を3Dモデル化"));
    assemblyModelButton->setObjectName("primaryButton");
    assemblyModelButton->setProperty("manualAnchor", QStringLiteral("plateAssemblyOutput"));
    assemblyModelButton->setToolTip(
        QStringLiteral("スライダー位置のパネルを厚み付き板としてプロジェクトへ追加"));
    connect(assemblyModelButton, &QPushButton::clicked,
        this, &MainWindow::CreatePlateAssemblyStateModel);
    plateLayout->addWidget(assemblyModelButton);

    auto* assemblyExportRow = new QWidget;
    auto* assemblyExportLayout = new QHBoxLayout(assemblyExportRow);
    assemblyExportLayout->setContentsMargins(0, 0, 0, 0);
    assemblyExportLayout->setSpacing(7);
    auto* assemblyStlButton = new QPushButton(QStringLiteral("曲げ状態をSTL保存"));
    assemblyStlButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    auto* assemblyStepButton = new QPushButton(QStringLiteral("曲げ状態をSTEP保存"));
    assemblyStepButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(assemblyStlButton, &QPushButton::clicked,
        this, [this] { ExportPlateAssemblyState(false); });
    connect(assemblyStepButton, &QPushButton::clicked,
        this, [this] { ExportPlateAssemblyState(true); });
    assemblyExportLayout->addWidget(assemblyStlButton);
    assemblyExportLayout->addWidget(assemblyStepButton);
    plateLayout->addWidget(assemblyExportRow);

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
    plateLayout->addLayout(pdfForm);

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
    const auto updateReliefControls = [this, fidelityControl, assemblyProgressControl,
                                       flatModelForm] {
        const bool enabled = plateFlatPatternAutoRelief_->isChecked();
        const bool allowNotches = enabled && plateFlatPatternAllowNotches_->isChecked();
        const ReliefNotchStyle notchStyle = static_cast<ReliefNotchStyle>(
            plateFlatPatternNotchStyle_->currentData().toInt());
        const bool curved = notchStyle == ReliefNotchStyle::CurvedV;
        const bool advancedSpacing = enabled && plateFlatPatternAdvancedSpacing_->isChecked();
        plateFlatPatternAssemblyStrategy_->setEnabled(enabled);
        plateFlatPatternCutDirection_->setEnabled(enabled);
        fidelityControl->setEnabled(enabled);
        plateFlatPatternAllowNotches_->setEnabled(enabled);
        plateFlatPatternNotchStyle_->setEnabled(allowNotches);
        plateFlatPatternAdvancedSpacing_->setEnabled(enabled);
        plateFlatPatternReliefSpacing_->setEnabled(allowNotches && advancedSpacing);
        plateFlatPatternReliefDepth_->setEnabled(allowNotches);
        plateFlatPatternNotchAngle_->setEnabled(allowNotches);
        plateFlatPatternNotchCurveStrength_->setEnabled(allowNotches && curved);
        plateFlatPatternMinimumBendAngle_->setEnabled(enabled);
        plateFlatPatternFoldSpacing_->setEnabled(advancedSpacing);
        assemblyProgressControl->setEnabled(enabled);
        plateAssemblyOutputPiece_->setEnabled(enabled);
        flatModelForm->setRowVisible(fidelityControl, enabled);
        flatModelForm->setRowVisible(plateFlatPatternAllowNotches_, enabled);
        flatModelForm->setRowVisible(plateFlatPatternNotchStyle_, allowNotches);
        flatModelForm->setRowVisible(plateFlatPatternAdvancedSpacing_, enabled);
        flatModelForm->setRowVisible(plateFlatPatternReliefSpacing_, allowNotches && advancedSpacing);
        flatModelForm->setRowVisible(plateFlatPatternReliefDepth_, allowNotches);
        flatModelForm->setRowVisible(plateFlatPatternNotchAngle_, allowNotches);
        flatModelForm->setRowVisible(plateFlatPatternNotchCurveStrength_, allowNotches && curved);
        flatModelForm->setRowVisible(plateFlatPatternMinimumBendAngle_, enabled);
        flatModelForm->setRowVisible(plateFlatPatternFoldSpacing_, advancedSpacing);
        RefreshExportSummary();
    };
    connect(plateFlatPatternAutoRelief_, &QCheckBox::toggled, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternAssemblyStrategy_, &QComboBox::currentIndexChanged, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternCutDirection_, &QComboBox::currentIndexChanged, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternAllowNotches_, &QCheckBox::toggled, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternNotchStyle_, &QComboBox::currentIndexChanged, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternAdvancedSpacing_, &QCheckBox::toggled, this,
        [updateReliefControls] { updateReliefControls(); });
    connect(plateFlatPatternFidelity_, &QSlider::valueChanged, this, [this](int value) {
        const QString level = value <= 3
            ? QStringLiteral("粗い")
            : value <= 7 ? QStringLiteral("標準") : QStringLiteral("細かい");
        plateFlatPatternFidelityLabel_->setText(QStringLiteral("%1 / 10（%2）").arg(value).arg(level));
        RefreshExportSummary();
    });
    connect(plateAssemblyGuidePreview_, &QCheckBox::toggled, this, [this] { RefreshExportSummary(); });
    connect(plateAssemblyApproximationPreview_, &QCheckBox::toggled,
        this, [updateReliefControls] { updateReliefControls(); });
    connect(plateAssemblyProgress_, &QSlider::valueChanged, this, [this](int value) {
        const QString state = value == 0
            ? QStringLiteral("平面")
            : value == 100 ? QStringLiteral("完成形") : QStringLiteral("組立中");
        plateAssemblyProgressLabel_->setText(
            QStringLiteral("%1%（%2）").arg(value).arg(state));
        if (plateAssemblyPreviewTimer_ != nullptr) {
            plateAssemblyPreviewTimer_->start();
        }
    });
    connect(plateAssemblyOutputPiece_, &QComboBox::currentIndexChanged,
        this, [this] { UpdatePlateAssemblyGuidePreview(); });
    connect(plateFlatPatternFoldSpacing_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternReliefSpacing_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternReliefDepth_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternNotchAngle_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternNotchCurveStrength_, &QDoubleSpinBox::valueChanged,
        this, [this] { RefreshExportSummary(); });
    connect(plateFlatPatternMinimumBendAngle_, &QDoubleSpinBox::valueChanged, this, [this] { RefreshExportSummary(); });
    updateReliefControls();
    plateLayout->addWidget(platePdfButton);
    plateLayout->addWidget(plateSvgButton);
    plateLayout->addWidget(plateDxfButton);

    modelExportScope_ = new QComboBox;
    modelExportScope_->addItems({
        QStringLiteral("3D画面で選択した部分"),
        QStringLiteral("表示中の3Dモデル全体"),
    });
    modelLayout->addWidget(modelExportScope_);
    bodyExportSummary_ = new QLabel(QStringLiteral("選択3D部品: なし"));
    bodyExportSummary_->setWordWrap(true);
    bodyExportSummary_->setStyleSheet("color: #5c6670;");
    modelLayout->addWidget(bodyExportSummary_);

    auto* bodyStlButton = new QPushButton(QStringLiteral("STLを保存"));
    bodyStlButton->setObjectName("primaryButton");
    bodyStlButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    auto* bodyStepButton = new QPushButton(QStringLiteral("STEPを保存"));
    bodyStepButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(bodyStlButton, &QPushButton::clicked, this, [this] { ExportSelectedBody(false); });
    connect(bodyStepButton, &QPushButton::clicked, this, [this] { ExportSelectedBody(true); });
    connect(modelExportScope_, &QComboBox::currentIndexChanged, this, [this] { RefreshExportSummary(); });
    modelLayout->addWidget(bodyStlButton);
    modelLayout->addWidget(bodyStepButton);

    // 部材グループ単位の .kcd 書き出し(モデルツリーの右クリックで除外を設定)。
    auto* filteredKcdLabel = new QLabel(QStringLiteral(
        "「出力しない」に設定した部材グループを除いて .kcd を書き出します。\n"
        "除外はモデルツリーの部材グループを右クリックして設定します。"));
    filteredKcdLabel->setWordWrap(true);
    filteredKcdLabel->setStyleSheet("color: #5c6670;");
    modelLayout->addWidget(filteredKcdLabel);
    auto* filteredKcdButton = new QPushButton(QStringLiteral("出力対象のみで .kcd 書き出し"));
    filteredKcdButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(filteredKcdButton, &QPushButton::clicked, this, [this] { ExportProjectExcludingSets(); });
    modelLayout->addWidget(filteredKcdButton);

    auto* planarSection = new CollapsibleSection(
        QStringLiteral("作業平面の1:1図面"), planarContent, true);
    planarSection->setProperty("manualAnchor", QStringLiteral("planarOutput"));
    layout->addWidget(planarSection);
    auto* plateSection = new CollapsibleSection(
        QStringLiteral("ペーパークラフト展開（1:1）"), plateContent, false);
    plateSection->setProperty("manualAnchor", QStringLiteral("plateFlatPattern"));
    layout->addWidget(plateSection);
    auto* modelSection = new CollapsibleSection(
        QStringLiteral("3DモデルのSTL / STEP出力"), modelContent, true);
    modelSection->setObjectName(QStringLiteral("modelOutputSection"));
    modelSection->setProperty("manualAnchor", QStringLiteral("modelOutput"));
    layout->addWidget(modelSection);
    // 出力ツール(上部)で選んだ1セクションだけ表示する(ADR 0025)。
    outputSections_ = {
        {QStringLiteral("作業平面の1:1図面"), planarSection},
        {QStringLiteral("ペーパークラフト展開（1:1）"), plateSection},
        {QStringLiteral("3DモデルのSTL / STEP出力"), modelSection},
    };
    layout->addStretch(1);
    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(panel);
    return scrollArea;
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
        toolsTabs_->setCurrentIndex(3);
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

// 旧ペーパークラフト3方式(多面体/曲げ紙/製作用大部材)は部材近似モデルへ置き換えて撤去した。
// 常に基本展開のみを使う。
bool MainWindow::UsesFacetedPapercraft() const
{
    return false;
}

bool MainWindow::UsesBentSheetPapercraft() const
{
    return false;
}

bool MainWindow::UsesFabricationPanelPapercraft() const
{
    return false;
}

kachakacha::io::PlateFlatPattern MainWindow::BuildActivePapercraftPattern(
    const kachakacha::model::NamedPlate& plate,
    PlateFlatPatternOptions options) const
{
    return BuildPlateFlatPattern(project_, plate, std::move(options));
}

kachakacha::io::PlateAssemblyGuide MainWindow::BuildActivePapercraftGuide(
    const kachakacha::model::NamedPlate& plate,
    PlateFlatPatternOptions options) const
{
    return BuildPlateAssemblyGuide(project_, plate, std::move(options));
}

kachakacha::io::PlateAssemblyMotion MainWindow::BuildActivePapercraftMotion(
    const kachakacha::model::NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options) const
{
    return BuildPlateAssemblyMotion(
        project_, plate, progress, std::move(options));
}

PlateFlatPatternOptions MainWindow::PlateFlatPatternOptionsFromUi() const
{
    PlateFlatPatternOptions options;
    if (plateFlatPatternAutoRelief_ != nullptr) {
        options.includeAutomaticReliefCuts = plateFlatPatternAutoRelief_->isChecked();
    }
    if (plateFlatPatternAssemblyStrategy_ != nullptr) {
        options.assemblyStrategy = static_cast<PlateAssemblyStrategy>(
            plateFlatPatternAssemblyStrategy_->currentData().toInt());
    }
    if (plateFlatPatternCutDirection_ != nullptr) {
        options.cutDirection = static_cast<PapercraftCutDirection>(
            plateFlatPatternCutDirection_->currentData().toInt());
    }
    if (plateFlatPatternAllowNotches_ != nullptr) {
        options.allowAutomaticNotches = plateFlatPatternAllowNotches_->isChecked();
    }
    if (plateFlatPatternNotchStyle_ != nullptr) {
        options.notchStyle = static_cast<ReliefNotchStyle>(
            plateFlatPatternNotchStyle_->currentData().toInt());
    }
    if (plateFlatPatternAdvancedSpacing_ != nullptr) {
        options.fidelityControlsFeatureSpacing
            = !plateFlatPatternAdvancedSpacing_->isChecked();
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
    if (plateFlatPatternNotchCurveStrength_ != nullptr) {
        options.reliefNotchCurveStrength
            = plateFlatPatternNotchCurveStrength_->value() / 100.0;
    }
    if (plateFlatPatternMinimumBendAngle_ != nullptr) {
        options.minimumFoldAngleDegrees = plateFlatPatternMinimumBendAngle_->value();
    }
    return options;
}

void MainWindow::UpdatePlateAssemblyGuidePreview()
{
    const bool guideEnabled = plateAssemblyGuidePreview_ != nullptr
        && plateAssemblyGuidePreview_->isChecked();
    const bool approximationEnabled = plateAssemblyApproximationPreview_ != nullptr
        && plateAssemblyApproximationPreview_->isChecked();
    if (viewport_ == nullptr || toolsTabs_ == nullptr || toolsTabs_->currentIndex() != 3
        || (!guideEnabled && !approximationEnabled)) {
        if (viewport_ != nullptr) {
            viewport_->SetPlateAssemblyGuidePreview(std::nullopt, {}, {});
            viewport_->SetPlateAssemblyApproximationPreview(
                std::nullopt, {}, {}, {}, 0.0);
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
        viewport_->SetPlateAssemblyApproximationPreview(
            std::nullopt, {}, {}, {}, 0.0);
        return;
    }

    try {
        PlateFlatPatternOptions options = PlateFlatPatternOptionsFromUi();
        options.uSegments = 96;
        options.vSegments = 48;
        options.openingSamples = 96;
        const double assemblyProgress = plateAssemblyProgress_ != nullptr
            ? static_cast<double>(plateAssemblyProgress_->value()) / 100.0
            : 1.0;
        kachakacha::io::PlateAssemblyGuide guide;
        std::optional<kachakacha::io::PlateAssemblyMotion> motion;
        guide = BuildActivePapercraftGuide(
            project_.Plates()[plateIndices.front()], options);
        if (approximationEnabled) {
            motion = BuildActivePapercraftMotion(
                project_.Plates()[plateIndices.front()], assemblyProgress, options);
        }
        std::vector<std::vector<Vector3>> foldLines;
        std::vector<std::vector<Vector3>> reliefCuts;
        if (guideEnabled) {
            foldLines.reserve(guide.foldLines.size());
            reliefCuts.reserve(guide.reliefCuts.size() + guide.splitLines.size());
            for (const auto& path : guide.foldLines) {
                foldLines.push_back(path.points);
            }
            for (const auto& path : guide.reliefCuts) {
                reliefCuts.push_back(path.points);
            }
            for (const auto& path : guide.splitLines) {
                reliefCuts.push_back(path.points);
            }
        }
        viewport_->SetPlateAssemblyGuidePreview(
            guideEnabled ? std::optional(plateIndices.front()) : std::nullopt,
            std::move(foldLines),
            std::move(reliefCuts));
        if (motion.has_value()) {
            const std::optional<int> selectedPiece = SelectedPlateAssemblyPiece();
            std::vector<std::array<Vector3, 3>> visiblePanels;
            std::vector<int> visiblePieceIndices;
            std::vector<double> visibleDeviations;
            visiblePanels.reserve(motion->panels.size());
            visiblePieceIndices.reserve(motion->pieceIndices.size());
            visibleDeviations.reserve(motion->panelDeviationMillimeters.size());
            for (std::size_t panel = 0; panel < motion->panels.size(); ++panel) {
                if (selectedPiece.has_value()
                    && motion->pieceIndices[panel] != *selectedPiece) {
                    continue;
                }
                visiblePanels.push_back(motion->panels[panel]);
                visiblePieceIndices.push_back(motion->pieceIndices[panel]);
                visibleDeviations.push_back(
                    motion->panelDeviationMillimeters[panel]);
            }
            viewport_->SetPlateAssemblyApproximationPreview(
                plateIndices.front(),
                std::move(visiblePanels),
                std::move(visiblePieceIndices),
                std::move(visibleDeviations),
                motion->maximumPanelDeviationMillimeters,
                UsesBentSheetPapercraft() || UsesFabricationPanelPapercraft());
        } else {
            viewport_->SetPlateAssemblyApproximationPreview(
                std::nullopt, {}, {}, {}, 0.0);
        }
    } catch (const std::exception&) {
        viewport_->SetPlateAssemblyGuidePreview(std::nullopt, {}, {});
        viewport_->SetPlateAssemblyApproximationPreview(
            std::nullopt, {}, {}, {}, 0.0);
    }
}

void MainWindow::ExportSelectedPlate(bool dxf)
{
    try {
        const auto& plate = project_.Plates()[SelectedPlateIndexForExport()];
        const PlateFlatPatternOptions flatOptions = PlateFlatPatternOptionsFromUi();
        const auto pattern = BuildActivePapercraftPattern(plate, flatOptions);
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
                .arg(UsesFacetedPapercraft()
                        ? plate.openingWireNames.size()
                        : pattern.openings.size())
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
        const auto pattern = BuildActivePapercraftPattern(
            plate, PlateFlatPatternOptionsFromUi());
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
                .arg(UsesFacetedPapercraft()
                        ? plate.openingWireNames.size()
                        : pattern.openings.size())
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
        const auto pattern = BuildActivePapercraftPattern(sourcePlate, options);
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
        std::vector<CadSelection> createdPlateSelections;
        for (const std::string& createdName : result.plateNames) {
            const auto platePosition = std::find_if(
                project_.Plates().begin(), project_.Plates().end(), [&](const auto& plate) {
                    return plate.name == createdName;
                });
            if (platePosition != project_.Plates().end()) {
                createdPlateSelections.push_back({
                    CadSelectionKind::Plate,
                    static_cast<int>(std::distance(
                        project_.Plates().begin(), platePosition)),
                });
            }
        }
        if (!createdPlateSelections.empty()) {
            UpdateSelections(std::move(createdPlateSelections), true);
        }
        plateFlatPatternName_->setText(SuggestedDirectGroupName(QStringLiteral("developed")));
        plateFlatPatternSummary_->setStyleSheet("color: #35664a;");
        plateFlatPatternSummary_->setText(
            QStringLiteral("%1 | 展開板%2枚を個別作成・全選択 | 開口%3 | 折り線%4 | 切れ目%5 | 3D板あり")
                .arg(ToQString(sourcePlate.name))
                .arg(result.plateNames.size())
                .arg(UsesFacetedPapercraft()
                        ? sourcePlate.openingWireNames.size()
                        : pattern.openings.size())
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

std::optional<int> MainWindow::SelectedPlateAssemblyPiece() const
{
    if (plateAssemblyOutputPiece_ == nullptr
        || plateAssemblyOutputPiece_->currentIndex() < 0) {
        return std::nullopt;
    }
    const int piece = plateAssemblyOutputPiece_->currentData().toInt();
    return piece >= 0 ? std::optional(piece) : std::nullopt;
}

void MainWindow::CreatePlateAssemblyStateModel()
{
    try {
        const auto sourcePlate = project_.Plates()[SelectedPlateIndexForExport()];
        const int progressPercent = plateAssemblyProgress_ != nullptr
            ? plateAssemblyProgress_->value()
            : 100;
        const double progress = static_cast<double>(progressPercent) / 100.0;
        const PlateFlatPatternOptions options = PlateFlatPatternOptionsFromUi();
        const auto motion = BuildActivePapercraftMotion(
            sourcePlate, progress, options);
        const std::optional<int> selectedPiece = SelectedPlateAssemblyPiece();
        const QString baseName = plateFlatPatternName_ != nullptr
                && !plateFlatPatternName_->text().trimmed().isEmpty()
            ? plateFlatPatternName_->text().trimmed()
            : ToQString(sourcePlate.name);
        const std::string prefix = ToName(SuggestedDirectGroupName(
            QStringLiteral("%1_fold_%2").arg(baseName).arg(progressPercent)));

        Project candidate = project_;
        const auto result = AddPlateAssemblyMotionModel(
            candidate, sourcePlate, motion, prefix, selectedPiece);
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(true);

        std::vector<CadSelection> createdSelections;
        createdSelections.reserve(result.plateNames.size());
        for (const std::string& name : result.plateNames) {
            const auto position = std::find_if(
                project_.Plates().begin(), project_.Plates().end(), [&](const auto& plate) {
                    return plate.name == name;
                });
            if (position != project_.Plates().end()) {
                createdSelections.push_back({
                    CadSelectionKind::Plate,
                    static_cast<int>(std::distance(project_.Plates().begin(), position)),
                });
            }
        }
        UpdateSelections(std::move(createdSelections), true);
        const QString scope = selectedPiece.has_value()
            ? QStringLiteral("部品%1").arg(*selectedPiece + 1)
            : QStringLiteral("全分割部品");
        plateFlatPatternSummary_->setStyleSheet("color: #35664a;");
        plateFlatPatternSummary_->setText(
            QStringLiteral("曲げ%1% | %2 | 厚み付きパネル%3枚を3Dモデル化")
                .arg(progressPercent)
                .arg(scope)
                .arg(result.plateNames.size()));
        statusBar()->showMessage(
            QStringLiteral("現在の曲げ状態を3Dモデルへ追加しました"), 5000);
    } catch (const std::exception& error) {
        plateFlatPatternSummary_->setStyleSheet("color: #a32734;");
        plateFlatPatternSummary_->setText(
            QStringLiteral("曲げ状態を3Dモデル化できません: %1")
                .arg(QString::fromUtf8(error.what())));
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ExportPlateAssemblyState(bool step)
{
    try {
        const auto sourcePlate = project_.Plates()[SelectedPlateIndexForExport()];
        const int progressPercent = plateAssemblyProgress_ != nullptr
            ? plateAssemblyProgress_->value()
            : 100;
        const double progress = static_cast<double>(progressPercent) / 100.0;
        const auto motion = BuildActivePapercraftMotion(
            sourcePlate, progress, PlateFlatPatternOptionsFromUi());
        const std::optional<int> selectedPiece = SelectedPlateAssemblyPiece();

        Project exportProject = project_;
        const std::string prefix = ToName(SuggestedDirectGroupName(
            QStringLiteral("assembly_export_%1").arg(progressPercent)));
        const auto result = AddPlateAssemblyMotionModel(
            exportProject, sourcePlate, motion, prefix, selectedPiece);
        kachakacha::occt::ModelShapeSelection selection;
        selection.plateNames = result.plateNames;

        statusBar()->showMessage(QStringLiteral("曲げ状態の閉形状を検査しています..."));
        QApplication::processEvents();
        const auto analysis = kachakacha::occt::AnalyzeModelShape(
            exportProject, selection, 0.01);
        if (!analysis.validBRep || !analysis.closedSolid) {
            throw std::runtime_error("曲げ状態から閉じた3D形状を作成できませんでした。");
        }

        const QString extension = step ? QStringLiteral(".step") : QStringLiteral(".stl");
        const QString filter = step ? QStringLiteral("STEP CAD形状 (*.step *.stp)")
                                    : QStringLiteral("STL 3Dプリント形状 (*.stl)");
        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        const QString pieceSuffix = selectedPiece.has_value()
            ? QStringLiteral("_piece_%1").arg(*selectedPiece + 1)
            : QStringLiteral("_all_parts");
        QString path = QFileDialog::getSaveFileName(
            this,
            step ? QStringLiteral("現在の曲げ状態をSTEP保存")
                 : QStringLiteral("現在の曲げ状態をSTL保存"),
            suggestedDirectory + ToQString(sourcePlate.name)
                + QStringLiteral("_fold_%1").arg(progressPercent)
                + pieceSuffix + extension,
            filter);
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(extension, Qt::CaseInsensitive)
            && !(step && path.endsWith(QStringLiteral(".stp"), Qt::CaseInsensitive))) {
            path += extension;
        }

        QApplication::processEvents();
        const std::filesystem::path outputPath(path.toStdWString());
        if (step) {
            kachakacha::occt::WriteModelStep(outputPath, exportProject, selection);
        } else {
            kachakacha::occt::WriteModelStl(outputPath, exportProject, selection);
        }
        plateFlatPatternSummary_->setStyleSheet("color: #35664a;");
        plateFlatPatternSummary_->setText(
            QStringLiteral("曲げ%1% | %2パネル | 保存済み: %3")
                .arg(progressPercent)
                .arg(result.plateNames.size())
                .arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(
            QStringLiteral("現在の曲げ状態を保存しました: %1").arg(path), 5000);
    } catch (const std::exception& error) {
        plateFlatPatternSummary_->setStyleSheet("color: #a32734;");
        plateFlatPatternSummary_->setText(
            QStringLiteral("曲げ状態を出力できません: %1")
                .arg(QString::fromUtf8(error.what())));
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
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
    const auto refreshAssemblyPieceChoices = [this](int pieceCount) {
        if (plateAssemblyOutputPiece_ == nullptr) {
            return;
        }
        const int previousPiece = plateAssemblyOutputPiece_->currentData().toInt();
        const QSignalBlocker blocker(plateAssemblyOutputPiece_);
        plateAssemblyOutputPiece_->clear();
        plateAssemblyOutputPiece_->addItem(QStringLiteral("全ての分割部品"), -1);
        for (int piece = 0; piece < pieceCount; ++piece) {
            plateAssemblyOutputPiece_->addItem(
                QStringLiteral("部品 %1 だけ").arg(piece + 1), piece);
        }
        const int previousIndex = plateAssemblyOutputPiece_->findData(previousPiece);
        plateAssemblyOutputPiece_->setCurrentIndex(
            previousIndex >= 0 ? previousIndex : 0);
    };
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
        refreshAssemblyPieceChoices(0);
        UpdatePlateAssemblyGuidePreview();
        plateFlatPatternSummary_->setStyleSheet("color: #5c6670;");
        plateFlatPatternSummary_->setText(QStringLiteral("選択板材: なし"));
        return;
    }
    if (plateIndices.size() > 1) {
        refreshAssemblyPieceChoices(0);
        UpdatePlateAssemblyGuidePreview();
        plateFlatPatternSummary_->setStyleSheet("color: #5c6670;");
        plateFlatPatternSummary_->setText(QStringLiteral("選択板材: %1枚（1枚に絞って出力）").arg(plateIndices.size()));
        return;
    }

    try {
        PlateFlatPatternOptions previewOptions = PlateFlatPatternOptionsFromUi();
        previewOptions.uSegments = 48;
        previewOptions.vSegments = 16;
        previewOptions.openingSamples = 48;
        previewOptions.includeOpenings = true;
        const auto& namedPlate = project_.Plates()[plateIndices.front()];
        const auto pattern = BuildActivePapercraftPattern(namedPlate, previewOptions);
        refreshAssemblyPieceChoices(pattern.analysis.pieceCount);
        UpdatePlateAssemblyGuidePreview();
        PlatePdfOptions pdfOptions;
        pdfOptions.pageSize = static_cast<QPageSize::PageSizeId>(platePdfPaper_->currentData().toInt());
        pdfOptions.overlapMillimeters = platePdfOverlap_->value();
        const auto pdfLayout = CalculatePlatePdfLayout(pattern, pdfOptions);
        const QString notchLabel = (UsesBentSheetPapercraft()
                || UsesFabricationPanelPapercraft())
                && !previewOptions.allowAutomaticNotches
            ? QStringLiteral("ブリッジ付きスリット")
            : previewOptions.notchStyle == ReliefNotchStyle::CurvedV
            ? QStringLiteral("曲線V＋スリット")
            : QStringLiteral("直線V＋スリット");
        const QString directionLabel = (UsesBentSheetPapercraft()
                || UsesFabricationPanelPapercraft())
            ? (previewOptions.cutDirection == PapercraftCutDirection::Vertical
                ? QStringLiteral("U")
                : previewOptions.cutDirection == PapercraftCutDirection::Horizontal
                ? QStringLiteral("V")
                : QStringLiteral("曲率自動"))
            : previewOptions.cutDirection
                == PapercraftCutDirection::Vertical
            ? QStringLiteral("縦")
            : previewOptions.cutDirection == PapercraftCutDirection::Horizontal
            ? QStringLiteral("横")
            : QStringLiteral("縦横");
        const QString shape = UsesFabricationPanelPapercraft()
            ? QStringLiteral("製作用大部材 %1枚・%2方向へ長い帯")
                .arg(pattern.analysis.pieceCount)
                .arg(previewOptions.fabricationPanelDirection
                        == FabricationPanelDirection::LongAlongU
                    ? QStringLiteral("U")
                    : previewOptions.fabricationPanelDirection
                        == FabricationPanelDirection::LongAlongV
                    ? QStringLiteral("V")
                    : QStringLiteral("曲率自動"))
            : UsesBentSheetPapercraft()
            ? QStringLiteral("曲げ紙・連続部材1枚・主曲げ%1＋%2 %3本")
                .arg(directionLabel, notchLabel)
                .arg(pattern.analysis.automaticNotchCount)
            : UsesFacetedPapercraft()
            ? QStringLiteral("新方式・%1方向の角面紙片 %2枚%3")
                .arg(directionLabel)
                .arg(pattern.analysis.pieceCount)
                .arg(pattern.analysis.automaticNotchCount > 0
                    ? QStringLiteral("＋%1 %2本")
                        .arg(notchLabel)
                        .arg(pattern.analysis.automaticNotchCount)
                    : QString())
            : pattern.analysis.pieceCount > 1
                && pattern.analysis.automaticNotchCount > 0
            ? QStringLiteral("%1分割 %2片＋%3 %4本")
                .arg(directionLabel)
                .arg(pattern.analysis.pieceCount)
                .arg(notchLabel)
                .arg(pattern.analysis.automaticNotchCount)
            : pattern.analysis.automaticNotchCount > 0
            ? QStringLiteral("一体板・%1 %2本")
                .arg(notchLabel)
                .arg(pattern.analysis.automaticNotchCount)
            : pattern.analysis.pieceCount > 1
            ? (previewOptions.assemblyStrategy == PlateAssemblyStrategy::SingleSheet
                ? QStringLiteral("一枚では成立不可 → %1分割 %2片")
                    .arg(directionLabel)
                    .arg(pattern.analysis.pieceCount)
                : QStringLiteral("%1分割 %2片")
                    .arg(directionLabel)
                    .arg(pattern.analysis.pieceCount))
            : pattern.analysis.classification == PlateDevelopability::Planar
            ? QStringLiteral("平面板")
            : pattern.analysis.classification == PlateDevelopability::Developable
            ? QStringLiteral("一方向曲げ")
            : QStringLiteral("二方向曲面・要確認");
        if (UsesBentSheetPapercraft() || UsesFabricationPanelPapercraft()) {
            const bool warning
                = pattern.analysis.maximumReconstructedDeviationMillimeters
                    > previewOptions.maximumShapeErrorMillimeters
                || pattern.analysis.maximumMaterialEdgeErrorMillimeters > 0.1
                || pattern.analysis.maximumSeamMismatchMillimeters > 0.1
                || pattern.analysis.maximumPanelConnectionMismatchMillimeters > 0.05;
            plateFlatPatternSummary_->setStyleSheet(
                warning ? "color: #a32734;" : "color: #35664a;");
            plateFlatPatternSummary_->setText(
                QStringLiteral("%1 | %2 | 部材 %3 / 完全分割線 %4 / 歪み逃がし %5 / 開口 %6 | 最大3D偏差 %7 mm（目標 %8）/ RMS %9 mm | 寸法誤差 %10 mm | 継ぎ目 %11 mm | relief長 %12 mm | PDF %13ページ")
                    .arg(ToQString(namedPlate.name), shape)
                    .arg(pattern.analysis.pieceCount)
                    .arg(pattern.analysis.separatingSeamCount)
                    .arg(pattern.analysis.nonSeparatingReliefCutCount)
                    .arg(pattern.openings.size())
                    .arg(pattern.analysis.maximumReconstructedDeviationMillimeters, 0, 'f', 3)
                    .arg(previewOptions.maximumShapeErrorMillimeters, 0, 'f', 3)
                    .arg(pattern.analysis.rootMeanSquareReconstructedDeviationMillimeters, 0, 'f', 3)
                    .arg(pattern.analysis.maximumMaterialEdgeErrorMillimeters, 0, 'f', 3)
                    .arg(pattern.analysis.maximumSeamMismatchMillimeters, 0, 'f', 3)
                    .arg(pattern.analysis.reliefCutLengthMillimeters, 0, 'f', 1)
                    .arg(pdfLayout.PageCount()));
            return;
        }
        const bool warning = !UsesFacetedPapercraft()
            && ((pattern.analysis.classification == PlateDevelopability::DoubleCurved
                    && pattern.analysis.pieceCount <= 1)
                || pattern.analysis.MaximumEstimatedErrorMillimeters() > 0.1);
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
        refreshAssemblyPieceChoices(0);
        UpdatePlateAssemblyGuidePreview();
        plateFlatPatternSummary_->setStyleSheet("color: #a32734;");
        plateFlatPatternSummary_->setText(QStringLiteral("展開不可: %1").arg(QString::fromUtf8(error.what())));
    }
}

void MainWindow::ExportProjectExcludingSets()
{
    using kachakacha::model::ObjectSet;
    using kachakacha::model::ObjectSetMember;
    using kachakacha::model::ProjectObjectKind;
    try {
        std::vector<std::string> excludedSets;
        for (const ObjectSet& set : project_.ObjectSets()) {
            if (!set.exportEnabled) {
                excludedSets.push_back(set.name);
            }
        }
        if (excludedSets.empty()) {
            QMessageBox::information(this, QStringLiteral(".kcd書き出し"),
                QStringLiteral("出力から除外された部材グループがありません。\n"
                               "モデルツリーで部材グループを右クリックし、"
                               "「.kcd書き出しに含める」のチェックを外してください。"));
            return;
        }

        // コピーへ複製し、除外部材のメンバーを依存順に削除する。
        // 依存で消せない場合は理由を集めて中断する(部材の所属は自分で管理する方針)。
        Project exportProject = project_;
        std::vector<ObjectSetMember> pending;
        for (const std::string& setName : excludedSets) {
            for (const ObjectSet& set : exportProject.ObjectSets()) {
                if (set.name == setName) {
                    pending.insert(pending.end(), set.members.begin(), set.members.end());
                }
            }
        }
        const auto kindRank = [](ProjectObjectKind kind) {
            switch (kind) {
            case ProjectObjectKind::PartModel: return 0;
            case ProjectObjectKind::Body: return 1;
            case ProjectObjectKind::Plate: return 2;
            case ProjectObjectKind::Surface: return 3;
            case ProjectObjectKind::Wire: return 4;
            case ProjectObjectKind::Point: return 5;
            case ProjectObjectKind::WorkPlane:
            default: return 6;
            }
        };
        std::stable_sort(pending.begin(), pending.end(), [&](const auto& a, const auto& b) {
            return kindRank(a.kind) < kindRank(b.kind);
        });
        const auto removeOne = [&exportProject](const ObjectSetMember& member) {
            switch (member.kind) {
            case ProjectObjectKind::PartModel: exportProject.RemovePartModel(member.name); return;
            case ProjectObjectKind::Body: exportProject.RemoveBody(member.name); return;
            case ProjectObjectKind::Plate: exportProject.RemovePlate(member.name); return;
            case ProjectObjectKind::Surface: exportProject.RemoveSurface(member.name); return;
            case ProjectObjectKind::Wire: exportProject.RemoveWire(member.name); return;
            case ProjectObjectKind::Point: exportProject.RemovePoint(member.name); return;
            case ProjectObjectKind::WorkPlane: exportProject.RemoveWorkPlane(member.name); return;
            }
        };
        std::vector<QString> blockers;
        bool progress = true;
        while (progress && !pending.empty()) {
            progress = false;
            blockers.clear();
            for (auto member = pending.begin(); member != pending.end();) {
                try {
                    removeOne(*member);
                    member = pending.erase(member);
                    progress = true;
                } catch (const std::exception& error) {
                    blockers.push_back(QStringLiteral("%1: %2")
                            .arg(ToQString(member->name), QString::fromUtf8(error.what())));
                    ++member;
                }
            }
        }
        if (!pending.empty()) {
            throw std::runtime_error(ToName(
                QStringLiteral("出力対象の部材が除外部材を参照しているため書き出せません。\n%1")
                    .arg(QStringList(blockers.begin(), blockers.end())
                            .join(QStringLiteral("\n")))));
        }
        for (const std::string& setName : excludedSets) {
            exportProject.RemoveObjectSet(setName);
        }

        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        QString suggestedName = QStringLiteral("出力対象のみ.kcd");
        if (!currentPath_.isEmpty()) {
            suggestedName = QFileInfo(currentPath_).completeBaseName()
                + QStringLiteral("_出力対象のみ.kcd");
        }
        QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("出力対象の部材のみで書き出し"),
            suggestedDirectory + suggestedName,
            QStringLiteral("kachakachaCAD (*.kcd)"));
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(QStringLiteral(".kcd"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".kcd");
        }
        const std::filesystem::path nativePath(path.toStdWString());
        std::ofstream output(nativePath, std::ios::binary);
        if (!output) {
            throw std::runtime_error("出力ファイルを開けませんでした。");
        }
        WriteProjectScript(output, exportProject);
        statusBar()->showMessage(
            QStringLiteral("出力対象のみで書き出しました: %1（除外部材 %2 件）")
                .arg(path)
                .arg(excludedSets.size()),
            5000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral(".kcd書き出し"),
            QString::fromUtf8(error.what()));
    }
}
