#pragma once
#include <QWidget>
#include "kachakacha/document/Dimension.h"
#include "kachakacha/app/Selection.h"
class V2MainWindow;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QCheckBox;
class V2DimensionTool final : public QWidget {
public:
    static V2DimensionTool* Open(V2MainWindow&);
    explicit V2DimensionTool(V2MainWindow&);
    bool AddTarget(const kachakacha::v2::app::SelectionRef&);
    bool Commit();
    void Reset();
    void RefreshPreview();
    void Load(kachakacha::v2::base::DimensionId);
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    void BuildUi();
    void TakeSelection();
    void RefreshList();
    void UpdateKind();
    V2MainWindow& window_;
    kachakacha::v2::base::DocumentId documentId_;
    kachakacha::v2::document::ReferenceDimension dimension_;
    QComboBox *kind_, *saved_;
    QDoubleSpinBox* value_;
    QCheckBox* driving_;
    QLineEdit* name_;
    QLabel* status_;
    bool placing_=false, loading_=false, fresh_=true;
};
