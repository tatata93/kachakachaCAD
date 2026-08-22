#pragma once

#include "CadViewport.h"
#include "kachakacha/model/Project.h"

#include <QMainWindow>

#include <array>
#include <vector>

class QCloseEvent;
class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool LoadProjectFile(const QString& path);
    bool SaveProjectFile(const QString& path);
    bool RunCreationSelfTest();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void BuildUi();
    void BuildDrawingActions();
    QWidget* BuildDrawingPanel();
    QWidget* BuildPlanePanel();
    QWidget* BuildWirePanel();
    QWidget* BuildEditPanel();
    QWidget* BuildMachiningPanel();
    QWidget* BuildInfoPanel();
    void BuildMenusAndToolbar();
    void NewProject();
    void OpenProject();
    void SaveProject();
    void SaveProjectAs();
    void AddWorkPlane();
    void AddWire();
    void ApplySelectedEdit();
    void ApplyLineChamfer();
    void ApplyLineFillet();
    void SetViewportTool(ViewportTool tool);
    void UpdateDrawingPanel(ViewportTool tool, std::size_t pointCount);
    void RefreshActiveWorkPlane();
    void AddViewportLine(kachakacha::geometry::Vector3 start, kachakacha::geometry::Vector3 end);
    void AddViewportPolyline(const std::vector<kachakacha::geometry::Vector3>& points);
    void AddViewportRectangle(const std::array<kachakacha::geometry::Vector3, 4>& corners);
    void AddViewportCircle(kachakacha::geometry::Vector3 center, double radius);
    void AddViewportArc(kachakacha::geometry::Vector3 start, kachakacha::geometry::Vector3 through, kachakacha::geometry::Vector3 end);
    void AddViewportBezier(const std::array<kachakacha::geometry::Vector3, 4>& points);
    void DeleteSelection();
    void Undo();
    void Redo();
    void RecordUndo();
    void UpdateHistoryActions();
    void RefreshModelViews(bool fitView = false);
    void RefreshPlaneChoices();
    void RefreshWireChoices();
    void UpdateSelection(CadSelection selection, bool updateTree);
    void UpdateSelections(std::vector<CadSelection> selections, bool updateTree);
    void SyncMachiningSelection(const std::vector<CadSelection>& selections);
    void BeginMachiningPick(int slot);
    void PopulateEditPanel(CadSelection selection);
    void PopulateWirePointTable(const kachakacha::model::NamedWire& wire);
    void MarkModified();
    bool ConfirmDiscardChanges();
    QString SuggestedPlaneName() const;
    QString SuggestedWireName() const;
    QString SuggestedDirectGroupName(const QString& prefix) const;
    QString SuggestedChamferName() const;
    QString SuggestedFilletName() const;

    kachakacha::model::Project project_;
    QString currentPath_;
    bool modified_ = false;
    std::vector<kachakacha::model::Project> undoStack_;
    std::vector<kachakacha::model::Project> redoStack_;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* selectToolAction_ = nullptr;
    QAction* lineToolAction_ = nullptr;
    QAction* polylineToolAction_ = nullptr;
    QAction* rectangleToolAction_ = nullptr;
    QAction* circleToolAction_ = nullptr;
    QAction* arcToolAction_ = nullptr;
    QAction* bezierToolAction_ = nullptr;
    QAction* snapAction_ = nullptr;
    QAction* alignPlaneAction_ = nullptr;
    QAction* finishDrawingAction_ = nullptr;
    QAction* cancelDrawingAction_ = nullptr;
    QComboBox* activePlaneCombo_ = nullptr;
    QDoubleSpinBox* snapStepField_ = nullptr;
    QLabel* drawingStateLabel_ = nullptr;

    CadViewport* viewport_ = nullptr;
    QTreeWidget* modelTree_ = nullptr;
    QTabWidget* toolsTabs_ = nullptr;
    QLabel* infoLabel_ = nullptr;

    QLabel* editSelectionLabel_ = nullptr;
    QStackedWidget* editParameters_ = nullptr;
    std::array<QDoubleSpinBox*, 3> editPlaneOrigin_{};
    std::array<QDoubleSpinBox*, 3> editPlaneNormal_{};
    std::array<QDoubleSpinBox*, 3> editPlaneUAxis_{};
    QComboBox* editWireSourcePlane_ = nullptr;
    QComboBox* editWirePolicy_ = nullptr;
    QStackedWidget* editWireGeometry_ = nullptr;
    QTableWidget* editWirePointTable_ = nullptr;
    std::array<QDoubleSpinBox*, 3> editArcCenter_{};
    std::array<QDoubleSpinBox*, 3> editArcUAxis_{};
    std::array<QDoubleSpinBox*, 3> editArcVAxis_{};
    QDoubleSpinBox* editArcRadius_ = nullptr;
    QDoubleSpinBox* editArcStartAngle_ = nullptr;
    QDoubleSpinBox* editArcSweepAngle_ = nullptr;

    QLineEdit* chamferName_ = nullptr;
    QComboBox* machiningType_ = nullptr;
    QStackedWidget* machiningValues_ = nullptr;
    QPushButton* machiningApplyButton_ = nullptr;
    QPushButton* machiningPickFirstButton_ = nullptr;
    QPushButton* machiningPickSecondButton_ = nullptr;
    QComboBox* chamferFirstWire_ = nullptr;
    QComboBox* chamferSecondWire_ = nullptr;
    QComboBox* chamferFirstBranch_ = nullptr;
    QComboBox* chamferSecondBranch_ = nullptr;
    QDoubleSpinBox* chamferFirstDistance_ = nullptr;
    QDoubleSpinBox* chamferSecondDistance_ = nullptr;
    QDoubleSpinBox* filletRadius_ = nullptr;
    int pendingMachiningPickSlot_ = -1;

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
    QDoubleSpinBox* planeOffset_ = nullptr;
    QDoubleSpinBox* planeTilt_ = nullptr;

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
