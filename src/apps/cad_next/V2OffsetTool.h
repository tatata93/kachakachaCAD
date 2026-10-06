#pragma once
#include <QWidget>
#include "kachakacha/app/PlaneFocus.h"
class V2MainWindow;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QCloseEvent;
class V2OffsetTool final:public QWidget {
public:
    static void Open(V2MainWindow&);
    static void Refresh(V2MainWindow&);
    static bool Key(V2MainWindow&,int);
    static kachakacha::v2::base::Result<kachakacha::v2::modeling::WorkPlaneFrame> PlaneFor(
        V2MainWindow&,const std::vector<kachakacha::v2::geometry::CurveSegment>&);
protected:
    void closeEvent(QCloseEvent*) override;
private:
    explicit V2OffsetTool(V2MainWindow&);
    void Preview();
    void Confirm();
    V2MainWindow& window_;
    QComboBox* plane_=nullptr;
    QDoubleSpinBox* distance_=nullptr;
    QLabel* status_=nullptr;
};
