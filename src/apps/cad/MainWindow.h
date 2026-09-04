#pragma once

#include "CadViewport.h"
#include "kachakacha/model/Project.h"

#include <QColor>
#include <QList>
#include <QMainWindow>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class PartModelPanel;
class QCloseEvent;
class QAction;
class QDialog;
class QDockWidget;
class QToolBar;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace kachakacha::io {
struct PlateAssemblyGuide;
struct PlateAssemblyMotion;
struct PlateFlatPattern;
struct PlateFlatPatternOptions;
}

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool LoadProjectFile(const QString& path);
    bool SaveProjectFile(const QString& path);
    bool RunCreationSelfTest();
    bool ExportFirstBodyForAutomation(const QString& stlPath, const QString& stepPath);
    bool PrepareManualScreenshot(const QString& state);

protected:
    void closeEvent(QCloseEvent* event) override;
    //! アプリ全体のEsc捕捉(#1): どこにフォーカスがあっても1回で
    //! 「入力破棄→ツール=選択→選択解除」まで戻す。
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void PerformGlobalEscape();
    void BuildUi();
    void BuildDrawingActions();
    QWidget* BuildDrawingPanel();
    QWidget* BuildPlanePanel();
    QWidget* BuildWirePanel();
    QWidget* BuildEditPanel();
    QWidget* BuildMachiningPanel();
    QWidget* BuildSurfacePanel();
    QWidget* BuildOutputPanel();
    QWidget* BuildDisplayPanel();
    QWidget* BuildInfoPanel();
    QWidget* BuildPartModelPanelTab();
    void CreatePartModelFromPanel();
    void RecalculateSelectedPartModel();
    void RemoveSelectedPartModel();
    void ExtractSelectedPartModelBoundaries();
    void ShowSelectedPartPatterns();
    void SetApproximationSetsVisible(bool visible);
    void ChangeSelectedSetState(int state);
    void CreatePlateFromSelectedPart();
    void UpdatePartFoldPreview();
    void SetSelectedPartModelRailFold(int railIndex, double value);
    void PickPartBoundariesFromSelectedWires();
    void RealizePartFoldState();
    void ExportPartFoldMesh(bool step);
    void ExportPartFoldKcd();
    void ApplyPolylineCornerEdit();
    void CreateIntersectionPoints();
    void CreateLineBetweenSelectedPoints();
    void CreateLineBetweenPickedPoints(
        kachakacha::geometry::Vector3 first, kachakacha::geometry::Vector3 second);
    void KeepCurveDrawingPoints(
        const std::string& wireName,
        const std::vector<kachakacha::geometry::Vector3>& points,
        const std::string& planeName);
    void ShowViewportContextMenu(const QPoint& globalPosition);
    void PrepareMachiningForWires(int firstIndex, int secondIndex);
    void ExpandSketchSection(const QString& title);
    void BuildMenusAndToolbar();
    void ApplyDisplaySettings();
    void LoadDisplaySettings();
    void SaveDisplaySettings() const;
    void ChooseDisplayColor(QPushButton* button);
    static void SetDisplayColorButton(QPushButton* button, const QColor& color);
    [[nodiscard]] static QColor DisplayColor(const QPushButton* button);
    void NewProject();
    void OpenProject();
    void SaveProject();
    void SaveProjectAs();
    void ExportPlanar(bool dxf);
    void ExportSelectedPlate(bool dxf);
    void ExportSelectedPlatePdf();
    [[nodiscard]] int SelectedPlateIndexForExport() const;
    bool ConfirmPlateFlatPatternAccuracy(const kachakacha::io::PlateFlatPattern& pattern);
    void AddWorkPlane();
    [[nodiscard]] kachakacha::model::WorkPlane WorkPlaneFromInputs() const;
    void AlignViewportFromPlaneInputs();
    void AddWire();
    void ApplySelectedEdit();
    void ApplyViewportTranslation(kachakacha::geometry::Vector3 delta, bool copy);
    void ApplyViewportMirror(
        kachakacha::geometry::Vector3 linePoint,
        kachakacha::geometry::Vector3 lineDirection,
        kachakacha::geometry::Vector3 planeNormal);
    void ApplyViewportRotation(
        kachakacha::geometry::Vector3 axisPoint,
        kachakacha::geometry::Vector3 axisDirection,
        double angleRadians);
    void ApplySplitWire(int wireIndex, double parameter);
    void ApplyDirectLineTrim(int wireIndex, double parameter);
    void ApplyDirectLineExtend(int wireIndex, double parameter);
    void ApplyEndpointCoincidence(WireEndpointPick anchor, WireEndpointPick follower);
    void ApplyEndpointTangency(WireEndpointPick anchor, WireEndpointPick follower);
    void ApplyEndpointCurvature(WireEndpointPick anchor, WireEndpointPick follower);
    void ApplyEndpointContinuity(
        WireEndpointPick anchor,
        WireEndpointPick follower,
        kachakacha::model::WireContinuity continuity);
    void RemoveSelectedCoincidences();
    void RemoveSelectedTangencies();
    void UpdateMeasurement(const std::vector<MeasurementPick>& picks);
    void SaveCurrentMeasurement();
    void DeleteSelectedReferenceDimension();
    void RefreshReferenceDimensions();
    void JoinSelectedWires();
    void ApplyMeetSelectedLines();
    void UpdateWireOffsetPreview();
    void ApplyWireOffset();
    void SetReferenceFromSelection();
    void ClearReference();
    void RefreshReference();
    void UseReferenceForPlaneRotation();
    void ApplyLineChamfer();
    void ApplyLineFillet();
    //! 面取り/丸めのモーダルツール(#7): ペアを次々クリック→一括適用。
    void StartCornerTool(bool fillet);
    void HandleCornerPairPicked(
        int firstWire, double firstParameter, int secondWire, double secondParameter);
    void RefreshCornerToolPreview();
    void ApplyCornerToolPairs();
    enum class SurfaceInputRole {
        Boundary,
        Guide,
        Section,
    };
    struct SurfaceInputGroup {
        SurfaceInputRole role = SurfaceInputRole::Section;
        std::vector<std::string> wireNames;
    };
    void AddSelectedSurfaceInputGroup(SurfaceInputRole role);
    void SelectConnectedSurfaceWireChain();
    void AppendSelectedWiresToSurfaceInputGroup();
    void RemoveSelectedSurfaceInputGroup();
    void ClearSurfaceInputGroups();
    void RefreshSurfaceInputTable();
    [[nodiscard]] std::vector<std::string> SelectedSurfaceWireNames() const;
    void ValidateSurfaceInputGroup(
        const std::vector<std::string>& wireNames,
        SurfaceInputRole role,
        bool requireClosedBoundary = false) const;
    //! 面の作り方(#10): コンボの値(-1=自動, 0=平面, 1=ルールド, 2=ロフト, 3=ガイド付き)。
    [[nodiscard]] int ConfiguredSurfaceMode() const;
    //! 自動判定の結果。mode<0 は判定不能(reason に理由)。
    struct SurfaceModeResolution {
        int mode = -2;
        std::vector<int> orderedWireIndices; //!< ガイド付きのときの並べ替え済み選択
        QString label;                       //!< 「平面」「ロフト面」等の表示名
        QString reason;                      //!< 判定不能の理由
    };
    [[nodiscard]] SurfaceModeResolution ResolveAutomaticSurfaceMode(
        const std::vector<int>& wireIndices) const;
    //! 選択が変わるたびに面を試作して半透明プレビュー(#11)。
    void UpdateSurfaceCreationPreview();
    //! 面を法線方向へずらした近似面(断面ロフト)を candidate へ追加し、名前を返す(#12)。
    std::string AddOffsetSurfaceLoft(
        kachakacha::model::Project& candidate,
        const kachakacha::model::NamedSurface& source,
        double signedDistanceMillimeters) const;
    void AddSurfaceFromConfiguredInputs(
        kachakacha::model::Project& project,
        const std::string& surfaceName,
        int surfaceMode,
        const std::vector<int>& fallbackWireIndices) const;
    void CreateSurfaceFromSelection();
    void AddSelectedGordonGuides();
    void ClearGordonGuides();
    void RefreshGordonGuideLabel();
    void CreateGordonSurfaceFromSelection();
    void ProjectSelectedWiresToSurface();
    void CreateProtrudingLightCase();
    void CreatePlateFromSurface();
    void CreatePlateFromSelectedWires();
    void UpdateSelectedPlate();
    void CreatePlateOffsetWires();
    void CreateSurfaceJig();
    void UpdateSelectedBody();
    void ModifySelectedPlateWires(
        void (*applyToPlate)(kachakacha::model::Project& candidate, std::string_view plateName,
            const std::string& wireName),
        const char* onlyOnePlateMessage, const char* selectionRequiredMessage,
        const QString& successMessageTemplate);
    void AddSelectedPlateOpenings();
    void RemoveSelectedPlateOpenings();
    void AddSelectedPlateReliefCuts();
    void RemoveSelectedPlateReliefCuts();
    void AddSelectedPlateSplitLines();
    void RemoveSelectedPlateSplitLines();
    void SplitSelectedPlate();
    void AddSelectedSurfaceOpenings();
    void CreateExtrudedSurface();
    void CreateRevolvedSurface();
    void CreateOffsetSurfaceApproximation();
    void RemoveSelectedSurfaceOpenings();
    void AddLaminationToSelectedPlate();
    void LinkSelectedPlatesAsLaminate();
    void ClearSelectedPlateLaminate();
    void UpdatePlateSplitPreview();
    void ExportSelectedBody(bool step);
    void CreateSelectedPlateFlatPatternModel();
    void CreatePlateAssemblyStateModel();
    void ExportPlateAssemblyState(bool step);
    [[nodiscard]] std::optional<int> SelectedPlateAssemblyPiece() const;
    [[nodiscard]] kachakacha::io::PlateFlatPatternOptions PlateFlatPatternOptionsFromUi() const;
    [[nodiscard]] bool UsesFacetedPapercraft() const;
    [[nodiscard]] bool UsesBentSheetPapercraft() const;
    [[nodiscard]] bool UsesFabricationPanelPapercraft() const;
    [[nodiscard]] kachakacha::io::PlateFlatPattern BuildActivePapercraftPattern(
        const kachakacha::model::NamedPlate& plate,
        kachakacha::io::PlateFlatPatternOptions options) const;
    [[nodiscard]] kachakacha::io::PlateAssemblyGuide BuildActivePapercraftGuide(
        const kachakacha::model::NamedPlate& plate,
        kachakacha::io::PlateFlatPatternOptions options) const;
    [[nodiscard]] kachakacha::io::PlateAssemblyMotion BuildActivePapercraftMotion(
        const kachakacha::model::NamedPlate& plate,
        double progress,
        kachakacha::io::PlateFlatPatternOptions options) const;
    void UpdatePlateAssemblyGuidePreview();
    void SetViewportTool(ViewportTool tool);
    void UpdateDrawingPanel(ViewportTool tool, std::size_t pointCount);
    void RefreshBeginnerGuide();
    void OpenManual(const QString& anchor = {});
    void OpenLegalNotices();
    void CommitDrawingDimensions();
    void UpdateArcConfiguration();
    void RefreshActiveWorkPlane();
    void AddViewportPoint(kachakacha::geometry::Vector3 point);
    void AddViewportLine(kachakacha::geometry::Vector3 start, kachakacha::geometry::Vector3 end);
    void AddViewportPolyline(const std::vector<kachakacha::geometry::Vector3>& points);
    void AddViewportRectangle(const std::array<kachakacha::geometry::Vector3, 4>& corners);
    void AddViewportCircle(kachakacha::geometry::Vector3 center, double radius);
    void AddViewportArc(kachakacha::geometry::Vector3 start, kachakacha::geometry::Vector3 through, kachakacha::geometry::Vector3 end);
    void AddViewportArcWire(const kachakacha::model::Wire& arc);
    void AddViewportBezier(const std::array<kachakacha::geometry::Vector3, 4>& points);
    void AddViewportSpline(const std::vector<kachakacha::geometry::Vector3>& throughPoints);
    void ApplyViewportWireEdit(int wireIndex, const kachakacha::model::Wire& replacement);
    void DeleteSelection();
    void HideSelected();
    void ShowAllObjects();
    void Undo();
    void Redo();
    void RecordUndo();
    void UpdateHistoryActions();
    void RefreshModelViews(bool fitView = false);
    void RefreshPlaneChoices();
    void RefreshWireChoices();
    void RefreshSurfaceChoices();
    void RefreshExportSummary();
    void ApplyModelTreeFilter();
    //! 上部モード切替(ADR 0025)。作図/面・板材/近似モデル/出力。
    enum class WorkMode { Drawing, SurfacePlate, PartModel, Output };
    void SetWorkMode(WorkMode mode);
    void SyncWorkModeToTab(int tabIndex);
    //! 数値入力パネル(作業平面・ワイヤ)の内容をゴーストプレビューへ反映する。
    void UpdateNumericPreviews();
    [[nodiscard]] kachakacha::model::Wire BuildNumericWire(
        std::optional<std::string>* sourcePlaneName) const;
    //! 点グリッドの実効表示(作図モード時のみ。設定で常時表示可)。
    void ApplyGridVisibility();
    //! 右パネル(作図と編集ドック)を必ず表示して指定タブへ切り替える。
    void ShowRightPanel(int tabIndex);
    //! 面・板パネルの見出しへスクロールして表示する(空文字なら先頭)。
    void RevealSurfaceGroup(const QString& title);
    void ShowOutputTool(const QString& title);
    void ShowPartModelTool(int sectionIndex);
    //! 選択中ワイヤ同士のT字分岐(端点が他の線の途中に接する箇所)で自動分割し、
    //! チェーンに繋がる側だけを選択に残す。分割したら true。
    bool SplitSelectedWiresAtBranchPoints();
    void EnsureMeasurementWindow();
    void UpdateMeasurementWindow();
    void CreateCurveCenterPoint();
    void CreateCurveControlPoints();
    void ShowShortcutReference();
    void ShowModelTreeContextMenu(const QPoint& position);
    //! ツリー項目の名前変更(#2)。F2・右クリック「名前を変更…」から。
    void RenameModelTreeItem(QTreeWidgetItem* item);
    //! ツリーのドラッグ&ドロップ: オブジェクトは部材グループへ所属変更、
    //! グループはグループの入れ子へ移動。ドロップ先が未分類/空欄なら最上位・未所属へ。
    bool HandleModelTreeDrop(const QList<QTreeWidgetItem*>& dragged, QTreeWidgetItem* target);
    void ExportProjectExcludingSets();
    void SetDisplayMode(ViewportDisplayMode mode);
    void ResetDisplayMode();
    void UpdateSelection(CadSelection selection, bool updateTree);
    void UpdateSelections(std::vector<CadSelection> selections, bool updateTree);
    void SyncMachiningSelection(const std::vector<CadSelection>& selections);
    void BeginMachiningPick(int slot);
    void PopulateEditPanel(CadSelection selection);
    void PopulateWirePointTable(const kachakacha::model::NamedWire& wire);
    void MarkModified();
    void WriteAutosave();
    void OfferAutosaveRecovery();
    void RemoveAutosave();
    [[nodiscard]] QString AutosavePath() const;
    bool ConfirmDiscardChanges();
    QString SuggestedName(
        const QString& prefix, int startNumber, const std::function<bool(const QString& candidate)>& exists) const;
    QString SuggestedPlaneName() const;
    QString SuggestedWireName() const;
    QString SuggestedDirectGroupName(const QString& prefix) const;
    QString SuggestedChamferName() const;
    QString SuggestedFilletName() const;
    QString SuggestedSurfaceName() const;
    QString SuggestedPlateName() const;
    QString SuggestedBodyName() const;
    QString SuggestedDimensionName() const;

    kachakacha::model::Project project_;
    QString currentPath_;
    bool modified_ = false;
    std::vector<kachakacha::model::Project> undoStack_;
    std::vector<kachakacha::model::Project> redoStack_;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* hideSelectedAction_ = nullptr;
    QAction* showAllObjectsAction_ = nullptr;
    QAction* designDisplayAction_ = nullptr;
    QAction* finishedDisplayAction_ = nullptr;
    QAction* isolateDisplayAction_ = nullptr;
    QAction* selectToolAction_ = nullptr;
    QAction* pointToolAction_ = nullptr;
    QAction* lineToolAction_ = nullptr;
    QAction* polylineToolAction_ = nullptr;
    QAction* rectangleToolAction_ = nullptr;
    QAction* circleToolAction_ = nullptr;
    QAction* arcToolAction_ = nullptr;
    QAction* bezierToolAction_ = nullptr;
    QAction* splineToolAction_ = nullptr;
    QAction* moveToolAction_ = nullptr;
    QAction* copyToolAction_ = nullptr;
    QAction* mirrorToolAction_ = nullptr;
    QAction* rotateToolAction_ = nullptr;
    QAction* splitToolAction_ = nullptr;
    QAction* trimToolAction_ = nullptr;
    QAction* extendToolAction_ = nullptr;
    QAction* coincidentToolAction_ = nullptr;
    QAction* tangentToolAction_ = nullptr;
    QAction* curvatureToolAction_ = nullptr;
    QAction* removeCoincidentAction_ = nullptr;
    QAction* removeTangentAction_ = nullptr;
    QAction* measureToolAction_ = nullptr;
    QAction* joinWiresAction_ = nullptr;
    QAction* meetLinesAction_ = nullptr;
    QAction* chamferAction_ = nullptr;
    QAction* filletAction_ = nullptr;
    QAction* cornerEditAction_ = nullptr;
    QAction* intersectionPointsAction_ = nullptr;
    QAction* lineBetweenPointsAction_ = nullptr;
    QAction* offsetApplyAction_ = nullptr;
    QAction* setReferenceAction_ = nullptr;
    QAction* clearReferenceAction_ = nullptr;
    QAction* snapAction_ = nullptr;
    QAction* gridOriginToolAction_ = nullptr;
    QAction* alignPlaneAction_ = nullptr;
    QAction* finishDrawingAction_ = nullptr;
    QAction* cancelDrawingAction_ = nullptr;
    QComboBox* activePlaneCombo_ = nullptr;
    QDoubleSpinBox* snapStepField_ = nullptr;
    QCheckBox* gridPointsVisible_ = nullptr;
    QComboBox* gridSubdivision_ = nullptr;
    std::array<QDoubleSpinBox*, 2> gridOrigin_{};
    QCheckBox* drawingConstruction_ = nullptr;
    QCheckBox* drawingKeepCurvePoints_ = nullptr; //!< スプライン/ベジェの指定点を作図点に残す(#9)
    bool performingGlobalEscape_ = false; //!< Escの全体処理の再入防止(#1)
    //! 面取り/丸めツール(#7)の選択済みペア(クリック位置tは残す側の判定に使う)。
    struct CornerToolPair {
        int firstWire = -1;
        double firstParameter = 0.5;
        int secondWire = -1;
        double secondParameter = 0.5;
    };
    std::vector<CornerToolPair> cornerToolPairs_;
    bool cornerToolFillet_ = false;
    QLabel* drawingStateLabel_ = nullptr;
    QWidget* drawingDimensionSection_ = nullptr;
    QStackedWidget* drawingDimensionStack_ = nullptr;
    QDoubleSpinBox* drawingLengthField_ = nullptr;
    QDoubleSpinBox* drawingAngleField_ = nullptr;
    QDoubleSpinBox* drawingWidthField_ = nullptr;
    QDoubleSpinBox* drawingHeightField_ = nullptr;
    QDoubleSpinBox* drawingRadiusField_ = nullptr;
    QComboBox* arcDrawingMode_ = nullptr;
    QStackedWidget* arcParameterStack_ = nullptr;
    QLabel* arcRadiusLabel_ = nullptr;
    QDoubleSpinBox* arcRadiusField_ = nullptr;
    QComboBox* arcBulgeSide_ = nullptr;
    QComboBox* arcDirectionBasis_ = nullptr;
    QDoubleSpinBox* arcDirectionAngle_ = nullptr;
    QComboBox* arcExtentMode_ = nullptr;
    QLabel* arcExtentLabel_ = nullptr;
    QDoubleSpinBox* arcExtentValue_ = nullptr;
    QComboBox* arcTurnSide_ = nullptr;
    QPushButton* drawingDimensionCommitButton_ = nullptr;

    QPushButton* wireColor_ = nullptr;
    QDoubleSpinBox* wireWidth_ = nullptr;
    QComboBox* wireStyle_ = nullptr;
    QPushButton* constructionColor_ = nullptr;
    QDoubleSpinBox* constructionWidth_ = nullptr;
    QComboBox* constructionStyle_ = nullptr;
    QPushButton* surfaceFillColor_ = nullptr;
    QDoubleSpinBox* surfaceOpacity_ = nullptr;
    QPushButton* surfaceEdgeColor_ = nullptr;
    QDoubleSpinBox* surfaceEdgeWidth_ = nullptr;
    QComboBox* surfaceEdgeStyle_ = nullptr;
    QPushButton* plateFillColor_ = nullptr;
    QDoubleSpinBox* plateOpacity_ = nullptr;
    QPushButton* plateEdgeColor_ = nullptr;
    QDoubleSpinBox* plateEdgeWidth_ = nullptr;
    QComboBox* plateEdgeStyle_ = nullptr;
    QPushButton* backgroundColor_ = nullptr;
    QPushButton* majorGridColor_ = nullptr;
    QPushButton* minorGridColor_ = nullptr;
    bool loadingDisplaySettings_ = false;

    CadViewport* viewport_ = nullptr;
    QLineEdit* modelFilter_ = nullptr;
    QTreeWidget* modelTree_ = nullptr;
    PartModelPanel* partModelPanel_ = nullptr;
    QTabWidget* toolsTabs_ = nullptr;
    std::array<QPushButton*, 4> workflowButtons_{};
    QWidget* workflowPanel_ = nullptr;
    QAction* drawingModeAction_ = nullptr;
    QAction* surfaceModeAction_ = nullptr;
    QAction* partModelModeAction_ = nullptr;
    QAction* outputModeAction_ = nullptr;
    QToolBar* drawingToolbar_ = nullptr;
    QToolBar* transformToolbar_ = nullptr;
    QToolBar* machiningToolbar_ = nullptr;
    QToolBar* surfaceToolbar_ = nullptr;
    QDockWidget* modelDock_ = nullptr;
    QDockWidget* toolsDock_ = nullptr;
    QWidget* guideSection_ = nullptr;
    QDialog* measurementWindow_ = nullptr;
    QLabel* measurementWindowResult_ = nullptr;
    QLabel* measurementWindowCurve_ = nullptr;
    QPushButton* curveCenterPointButton_ = nullptr;
    QPushButton* curveControlPointsButton_ = nullptr;
    bool syncingWorkMode_ = false;
    WorkMode currentMode_ = WorkMode::Drawing;
    QWidget* planePanelWidget_ = nullptr;
    QWidget* wirePanelWidget_ = nullptr;
    QWidget* surfacePanelWidget_ = nullptr;
    std::vector<std::pair<QString, QWidget*>> surfaceSections_;
    std::vector<QAction*> surfaceToolActions_;
    std::vector<std::pair<QString, QWidget*>> outputSections_;
    std::vector<QAction*> outputToolActions_;
    QToolBar* outputToolbar_ = nullptr;
    QToolBar* partModelToolbar_ = nullptr;
    QCheckBox* gridOutsideDrawingCheck_ = nullptr;
    QCheckBox* dimOtherPlanesCheck_ = nullptr; //!< 作図面以外の線を常時薄く表示(#6)
    QLabel* beginnerGuideTitle_ = nullptr;
    QLabel* beginnerGuideNext_ = nullptr;
    QLabel* beginnerGuideSteps_ = nullptr;
    QLabel* beginnerGuideContext_ = nullptr;
    QPushButton* beginnerGuideNextButton_ = nullptr;
    QPushButton* beginnerGuideManualButton_ = nullptr;
    int beginnerGuideNextTab_ = -1;
    QString beginnerGuideManualAnchor_;
    QTimer* autosaveTimer_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QComboBox* measurementMode_ = nullptr;
    QLabel* measurementStateLabel_ = nullptr;
    QLabel* measurementResultLabel_ = nullptr;
    QPushButton* measurementClearButton_ = nullptr;
    QComboBox* measurementMetric_ = nullptr;
    QLineEdit* measurementName_ = nullptr;
    QPushButton* measurementSaveButton_ = nullptr;
    QListWidget* referenceDimensionList_ = nullptr;
    QPushButton* referenceDimensionDeleteButton_ = nullptr;
    std::vector<MeasurementPick> lastMeasurementPicks_;

    QLabel* editSelectionLabel_ = nullptr;
    QLabel* transformReferenceLabel_ = nullptr;
    QPushButton* editApplyButton_ = nullptr;
    QStackedWidget* editParameters_ = nullptr;
    std::array<QDoubleSpinBox*, 3> editPlaneOrigin_{};
    std::array<QDoubleSpinBox*, 3> editPlaneNormal_{};
    std::array<QDoubleSpinBox*, 3> editPlaneUAxis_{};
    QComboBox* editWireSourcePlane_ = nullptr;
    QComboBox* editWirePolicy_ = nullptr;
    QCheckBox* editWireConstruction_ = nullptr;
    QWidget* editWireConstraintPanel_ = nullptr;
    QCheckBox* editWireLockLength_ = nullptr;
    QDoubleSpinBox* editWireConstraintLength_ = nullptr;
    QCheckBox* editWireLockAngle_ = nullptr;
    QDoubleSpinBox* editWireConstraintAngle_ = nullptr;
    QStackedWidget* editWireGeometry_ = nullptr;
    QTableWidget* editWirePointTable_ = nullptr;
    std::array<QDoubleSpinBox*, 3> editArcCenter_{};
    std::array<QDoubleSpinBox*, 3> editArcUAxis_{};
    std::array<QDoubleSpinBox*, 3> editArcVAxis_{};
    QCheckBox* editWireLockRadius_ = nullptr;
    QDoubleSpinBox* editArcRadius_ = nullptr;
    QDoubleSpinBox* editArcStartAngle_ = nullptr;
    QDoubleSpinBox* editArcSweepAngle_ = nullptr;
    QLabel* wireOffsetSelectionLabel_ = nullptr;
    QDoubleSpinBox* wireOffsetDistance_ = nullptr;
    QComboBox* wireOffsetSide_ = nullptr;
    QPushButton* wireOffsetApplyButton_ = nullptr;

    QLineEdit* chamferName_ = nullptr;
    QComboBox* machiningType_ = nullptr;
    QStackedWidget* machiningValues_ = nullptr;
    QPushButton* machiningApplyButton_ = nullptr;
    QPushButton* machiningPickFirstButton_ = nullptr;
    QPushButton* machiningPickSecondButton_ = nullptr;
    QComboBox* chamferFirstWire_ = nullptr;
    QComboBox* chamferSecondWire_ = nullptr;
    QComboBox* polylineCornerWire_ = nullptr;
    QSpinBox* polylineCornerVertex_ = nullptr;
    QComboBox* chamferFirstBranch_ = nullptr;
    QComboBox* chamferSecondBranch_ = nullptr;
    QDoubleSpinBox* chamferFirstDistance_ = nullptr;
    QDoubleSpinBox* chamferSecondDistance_ = nullptr;
    QDoubleSpinBox* filletRadius_ = nullptr;
    int pendingMachiningPickSlot_ = -1;

    QLineEdit* surfaceName_ = nullptr;
    QComboBox* surfaceType_ = nullptr;
    QLabel* surfaceSelectionLabel_ = nullptr;
    QLabel* gordonGuideLabel_ = nullptr;
    std::vector<std::string> gordonGuideNames_;
    QTableWidget* surfaceInputTable_ = nullptr;
    QPushButton* surfaceSelectChainButton_ = nullptr;
    QPushButton* surfaceAddBoundaryOrGuideButton_ = nullptr;
    QPushButton* surfaceAddSectionButton_ = nullptr;
    QPushButton* surfaceAppendGroupButton_ = nullptr;
    QPushButton* surfaceCreateButton_ = nullptr;
    std::vector<SurfaceInputGroup> surfaceInputGroups_;
    QComboBox* projectionSurface_ = nullptr;
    QComboBox* projectionPlane_ = nullptr;
    QLabel* projectionSelectionLabel_ = nullptr;
    QLabel* lightCaseSelectionLabel_ = nullptr;
    QLabel* lightCaseReferenceLabel_ = nullptr;
    QLineEdit* lightCaseRootName_ = nullptr;
    QLineEdit* lightCaseSurfaceName_ = nullptr;
    QComboBox* lightCaseDirectionMode_ = nullptr;
    QWidget* lightCaseDirectionEditor_ = nullptr;
    std::array<QDoubleSpinBox*, 3> lightCaseDirection_{};
    QLineEdit* plateName_ = nullptr;
    QComboBox* plateSurface_ = nullptr;
    QDoubleSpinBox* plateThickness_ = nullptr;
    QCheckBox* plateVariableThickness_ = nullptr;
    QCheckBox* thicknessAlsoSurface_ = nullptr; //!< 板材化と同時に反対側表面の面も作る(#12)
    QCheckBox* thicknessAlsoWires_ = nullptr;   //!< 板材化と同時に縁ワイヤを厚み位置へ複製(#12)
    QCheckBox* surfaceKeepSectionWires_ = nullptr; //!< 面作成時に構成線もワイヤ化(#13)
    QDoubleSpinBox* plateEndThickness_ = nullptr;
    QComboBox* plateDirection_ = nullptr;
    QSpinBox* laminateCountSpin_ = nullptr;
    QComboBox* extrudeDirection_ = nullptr;
    QDoubleSpinBox* extrudeDistance_ = nullptr;
    QComboBox* revolveAxis_ = nullptr;
    QDoubleSpinBox* revolveAngle_ = nullptr;
    QSpinBox* revolveSections_ = nullptr;
    QDoubleSpinBox* offsetSurfaceDistance_ = nullptr;
    QDoubleSpinBox* laminateThicknessSpin_ = nullptr;
    QDoubleSpinBox* laminateTargetHeightSpin_ = nullptr;
    QLabel* laminateSuggestLabel_ = nullptr;
    QComboBox* plateMaterial_ = nullptr;
    QLineEdit* jigName_ = nullptr;
    QComboBox* jigSurface_ = nullptr;
    QComboBox* jigSide_ = nullptr;
    QDoubleSpinBox* jigClearance_ = nullptr;
    QDoubleSpinBox* jigThickness_ = nullptr;
    QDoubleSpinBox* jigMinimumWall_ = nullptr;
    QLabel* jigAnalysisLabel_ = nullptr;
    QLabel* plateOpeningSelectionLabel_ = nullptr;
    QLabel* plateReliefSelectionLabel_ = nullptr;
    QLabel* plateSplitLineSelectionLabel_ = nullptr;
    QLabel* plateOffsetSelectionLabel_ = nullptr;
    QComboBox* plateOffsetLayer_ = nullptr;
    QLabel* plateSplitSelectionLabel_ = nullptr;
    QComboBox* plateSplitAxis_ = nullptr;
    QSlider* plateSplitSlider_ = nullptr;
    QDoubleSpinBox* plateSplitPosition_ = nullptr;

    QComboBox* exportPlane_ = nullptr;
    QComboBox* exportScope_ = nullptr;
    QLabel* exportSummary_ = nullptr;
    QLabel* plateFlatPatternSummary_ = nullptr;
    QLineEdit* plateFlatPatternName_ = nullptr;
    QComboBox* plateFlatPatternPlane_ = nullptr;
    QCheckBox* plateFlatPatternAutoRelief_ = nullptr;
    QComboBox* plateFlatPatternAssemblyStrategy_ = nullptr;
    QComboBox* plateFlatPatternCutDirection_ = nullptr;
    QCheckBox* plateFlatPatternAllowNotches_ = nullptr;
    QComboBox* plateFlatPatternNotchStyle_ = nullptr;
    QSlider* plateFlatPatternFidelity_ = nullptr;
    QLabel* plateFlatPatternFidelityLabel_ = nullptr;
    QCheckBox* plateFlatPatternAdvancedSpacing_ = nullptr;
    QDoubleSpinBox* plateFlatPatternReliefSpacing_ = nullptr;
    QDoubleSpinBox* plateFlatPatternReliefDepth_ = nullptr;
    QDoubleSpinBox* plateFlatPatternNotchAngle_ = nullptr;
    QDoubleSpinBox* plateFlatPatternNotchCurveStrength_ = nullptr;
    QDoubleSpinBox* plateFlatPatternMinimumBendAngle_ = nullptr;
    QCheckBox* plateAssemblyGuidePreview_ = nullptr;
    QCheckBox* plateAssemblyApproximationPreview_ = nullptr;
    QSlider* plateAssemblyProgress_ = nullptr;
    QLabel* plateAssemblyProgressLabel_ = nullptr;
    QComboBox* plateAssemblyOutputPiece_ = nullptr;
    QTimer* plateAssemblyPreviewTimer_ = nullptr;
    QDoubleSpinBox* plateFlatPatternFoldSpacing_ = nullptr;
    QDoubleSpinBox* plateFlatPatternCutWidth_ = nullptr;
    QComboBox* platePdfPaper_ = nullptr;
    QDoubleSpinBox* platePdfOverlap_ = nullptr;
    QLabel* bodyExportSummary_ = nullptr;
    QComboBox* modelExportScope_ = nullptr;

    QLineEdit* planeName_ = nullptr;
    QComboBox* planeMethod_ = nullptr;
    QStackedWidget* planeParameters_ = nullptr;
    QComboBox* standardPlane_ = nullptr;
    std::array<QDoubleSpinBox*, 3> pointNormalOrigin_{};
    std::array<QDoubleSpinBox*, 3> pointNormalDirection_{};
    std::array<QDoubleSpinBox*, 3> pointNormalUAxis_{};
    std::array<QDoubleSpinBox*, 3> threePointA_{};
    std::array<QDoubleSpinBox*, 3> threePointB_{};
    std::array<QDoubleSpinBox*, 3> threePointC_{};
    QComboBox* offsetSourcePlane_ = nullptr;
    QComboBox* rotateSourcePlane_ = nullptr;
    std::array<QDoubleSpinBox*, 3> rotateAxisPoint_{};
    std::array<QDoubleSpinBox*, 3> rotateAxisDirection_{};
    QDoubleSpinBox* rotateAngle_ = nullptr;
    QDoubleSpinBox* pathPlanePosition_ = nullptr;
    QDoubleSpinBox* planeOffset_ = nullptr;
    QDoubleSpinBox* planeTilt_ = nullptr;
    QLabel* planeReferenceLabel_ = nullptr;
    std::optional<std::string> referenceWireName_;

    QLineEdit* wireName_ = nullptr;
    QComboBox* wireKind_ = nullptr;
    QStackedWidget* wireParameters_ = nullptr;
    QComboBox* wirePlane_ = nullptr;
    QComboBox* wirePolicy_ = nullptr;
    std::array<QDoubleSpinBox*, 3> lineStart_{};
    std::array<QDoubleSpinBox*, 3> lineEnd_{};
    std::array<QDoubleSpinBox*, 3> bezierStart_{};
    std::array<QDoubleSpinBox*, 3> bezierControl1_{};
    std::array<QDoubleSpinBox*, 3> bezierControl2_{};
    std::array<QDoubleSpinBox*, 3> bezierEnd_{};
    std::array<QDoubleSpinBox*, 2> sketchLineStart_{};
    std::array<QDoubleSpinBox*, 2> sketchLineEnd_{};
    std::array<QDoubleSpinBox*, 2> circleCenter_{};
    QDoubleSpinBox* circleRadius_ = nullptr;
    std::array<QDoubleSpinBox*, 2> arcCenter_{};
    QDoubleSpinBox* arcRadius_ = nullptr;
    QDoubleSpinBox* arcStartAngle_ = nullptr;
    QDoubleSpinBox* arcSweepAngle_ = nullptr;
    std::array<QDoubleSpinBox*, 2> sketchBezierStart_{};
    std::array<QDoubleSpinBox*, 2> sketchBezierControl1_{};
    std::array<QDoubleSpinBox*, 2> sketchBezierControl2_{};
    std::array<QDoubleSpinBox*, 2> sketchBezierEnd_{};
};
