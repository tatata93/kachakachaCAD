#pragma once
#include "kachakacha/modeling/ExtrudeEndCap.h"
#include <QWidget>
#include <functional>
class V2MainWindow;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class V2EndCapInput final : public QWidget {
public:
    V2EndCapInput(V2MainWindow* window,QWidget* parent);
    void SetExtentVisible(bool visible);
    kachakacha::v2::modeling::ExtrudeEndCap Options() const;
    void SetOptions(const kachakacha::v2::modeling::ExtrudeEndCap& options);
    std::function<void()> changed;
private:
    void Refresh();
    void Pick();
    V2MainWindow* window_;
    class QCheckBox* extend_;
    QComboBox* mode_;
    QDoubleSpinBox *tilt_,*heading_;
    QPushButton* pick_;
    QLabel* reference_;
    kachakacha::v2::geometry::Vector3 normal_{};
    bool restoring_=false;
};
