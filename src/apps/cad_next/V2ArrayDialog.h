#pragma once

//! 並べ方を聞く窓(棚卸し B-2)。
//!
//! 聞くのは「何個・どれだけ離して」だけである。判断(2個以上か、多すぎないか、
//! 一周なら最後を重ねないか)は core(app/ArrayPlan)にある。
//! ここで数えると、画面を出さないと確かめられなくなる。

#include "kachakacha/geometry/Vector3.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

//! 並べ方。直線と円で使う欄が違うので、両方を1つに持つ。
struct V2ArrayChoice {
    //! 元のものを含めた数。
    int count = 5;
    //! 直線のとき: 1つ分の間隔、または端から端まで(spanIsTotal で決まる)。
    kachakacha::v2::geometry::Vector3 step{20.0, 0.0, 0.0};
    //! step が「端から端まで」なら true。
    bool spanIsTotal = false;
    //! 円のとき: 端から端までの角(度)。360 なら一周。
    double totalAngleDeg = 360.0;
    //! 円のとき: 回す中心。軸は作業平面の法線を使う。
    kachakacha::v2::geometry::Vector3 center{};
};

class V2ArrayDialog final : public QDialog {
public:
    //! circular が真なら円の欄を、偽なら直線の欄を出す。
    V2ArrayDialog(const V2ArrayChoice& initial, bool circular, QWidget* parent);

    [[nodiscard]] V2ArrayChoice Choice() const;

private:
    bool circular_ = false;
    QSpinBox* count_ = nullptr;
    QDoubleSpinBox* stepX_ = nullptr;
    QDoubleSpinBox* stepY_ = nullptr;
    QDoubleSpinBox* stepZ_ = nullptr;
    QComboBox* spanKind_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QDoubleSpinBox* centerX_ = nullptr;
    QDoubleSpinBox* centerY_ = nullptr;
    QDoubleSpinBox* centerZ_ = nullptr;
};
