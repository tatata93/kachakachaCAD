#pragma once
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/app/Selection.h"
#include <QWidget>
#include <vector>
class V2MainWindow;
class V2RoleInput;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class V2CurvePointsTool final : public QWidget {
public:
    static void Open(V2MainWindow& window);
private:
    explicit V2CurvePointsTool(V2MainWindow& window);
    bool eventFilter(QObject*,QEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void Refresh();
    void Confirm();
    void QueueRefresh();
    V2MainWindow& window_;
    V2RoleInput* role_=nullptr;
    QComboBox* mode_=nullptr;
    QDoubleSpinBox* pitch_=nullptr;
    QSpinBox* divisions_=nullptr;
    QCheckBox* endpoints_=nullptr;
    QCheckBox* reverse_=nullptr;
    QLabel* status_=nullptr;
    QPushButton* confirm_=nullptr;
    QTimer* timer_=nullptr;
    std::vector<kachakacha::v2::geometry::Vector3> points_;
    std::vector<kachakacha::v2::app::SelectionRef> inputs_;
};
