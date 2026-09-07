#pragma once

#include "kachakacha/model/Plate.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

//! 面と板材を、利用者には一つの「面部品」として扱わせる小さな編集パネル。
//! 幾何面(Surface)と製作条件(Plate)の分離は内部だけに留める。
class SheetPartPanel final : public QWidget {
public:
    explicit SheetPartPanel(QWidget* parent = nullptr);

    void ShowNoSelection(bool canCreateFromWires);
    void ShowSurface(const QString& name, int manufacturingVariantCount);
    void ShowPlate(
        const QString& name,
        double startThicknessMillimeters,
        double endThicknessMillimeters,
        kachakacha::model::PlateThicknessDirection direction,
        const QString& materialCode);

    [[nodiscard]] double StartThicknessMillimeters() const;
    [[nodiscard]] double EndThicknessMillimeters() const;
    [[nodiscard]] bool UsesVariableThickness() const;
    [[nodiscard]] kachakacha::model::PlateThicknessDirection Direction() const;
    [[nodiscard]] QString MaterialCode() const;

    //! MainWindow の自己診断から、通常操作と同じ入力欄を設定する。
    void SetManufacturingValuesForTest(
        double startThicknessMillimeters,
        double endThicknessMillimeters,
        bool variableThickness,
        kachakacha::model::PlateThicknessDirection direction,
        const QString& materialCode);

    std::function<void()> onApply;
    std::function<void()> onCreateFromWires;

private:
    void SetEditorEnabled(bool enabled);
    void SetValues(
        double startThicknessMillimeters,
        double endThicknessMillimeters,
        bool variableThickness,
        kachakacha::model::PlateThicknessDirection direction,
        const QString& materialCode);

    QLabel* selectionLabel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QDoubleSpinBox* startThickness_ = nullptr;
    QCheckBox* variableThickness_ = nullptr;
    QDoubleSpinBox* endThickness_ = nullptr;
    QComboBox* direction_ = nullptr;
    QComboBox* material_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* createFromWiresButton_ = nullptr;
};
