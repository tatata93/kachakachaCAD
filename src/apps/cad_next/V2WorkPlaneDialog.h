#pragma once

//! 作業平面の作り方11通りを選ぶ窓。
//!
//! これまでは押すたびに XY→YZ→ZX を順ぐりに切り替えるだけで、
//! **原点を通らない平面が作れなかった**。船体や車体の station ごとの断面を
//! 描くには、平面から離した面や、曲線に直角な面が要る。
//!
//! 何を選べばよいかは core(app/WorkPlaneOptions.h)が決める。
//! この窓は、作り方を並べて、いまの選択で足りるかをその場で出すだけにする。

#include "kachakacha/app/WorkPlaneOptions.h"

#include <QDialog>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;

//! 窓で決めたこと。
struct WorkPlaneChoice {
    kachakacha::v2::modeling::WorkPlaneMethod method =
        kachakacha::v2::modeling::WorkPlaneMethod::Standard;
    kachakacha::v2::modeling::StandardPlaneKind standard =
        kachakacha::v2::modeling::StandardPlaneKind::XY;
    double offsetMm = 0.0;
    double angleDeg = 0.0;
};

class V2WorkPlaneDialog final : public QDialog {
public:
    V2WorkPlaneDialog(const WorkPlaneChoice& initial,
        const kachakacha::v2::app::WorkPlaneFacts& facts, QWidget* parent);

    [[nodiscard]] WorkPlaneChoice Choice() const;
    //! 試験から呼ぶ。窓を出さずに同じ道を通す。
    void SetMethodIndex(int index);
    void Refresh();
    [[nodiscard]] bool CurrentChoiceIsValid(QString* reasonOut) const;

private:
    kachakacha::v2::app::WorkPlaneFacts facts_;
    QComboBox* method_ = nullptr;
    QComboBox* standard_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QLabel* needs_ = nullptr;
};
