#pragma once

#include "CadViewport.h"
#include "kachakacha/model/Project.h"

#include <QMainWindow>

#include <array>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
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
    QWidget* BuildPlanePanel();
    QWidget* BuildWirePanel();
    QWidget* BuildInfoPanel();
    void BuildMenusAndToolbar();
    void NewProject();
    void OpenProject();
    void SaveProject();
    void SaveProjectAs();
    void AddWorkPlane();
    void AddWire();
    void DeleteSelection();
    void RefreshModelViews(bool fitView = false);
    void RefreshPlaneChoices();
    void UpdateSelection(CadSelection selection, bool updateTree);
    void MarkModified();
    bool ConfirmDiscardChanges();
    QString SuggestedPlaneName() const;
    QString SuggestedWireName() const;

    kachakacha::model::Project project_;
    QString currentPath_;
    bool modified_ = false;

    CadViewport* viewport_ = nullptr;
    QTreeWidget* modelTree_ = nullptr;
    QLabel* infoLabel_ = nullptr;

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
