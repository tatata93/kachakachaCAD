#pragma once

//! 配列(直線/円形)の棚(指示書 D-23、右ペインに欄)。
//!
//! 今までは `wire.array_linear` / `wire.array_circular` を押すとダイアログ(窓)が
//! 開いて聞いていた(V2ArrayDialog)。正本のモックは右ペインに欄を置くので、
//! ここは同じ値(V2ArrayChoice)を **窓を出さずに右の棚で** 集める。
//!
//! 作り方カードは「直線」「円形」の2枚。選んだほうの欄だけを出す。
//!   直線: 個数 / 間隔(mm)/ 方向(作業平面の X or Y)
//!   円形: 個数 / 中心(作業平面の u, v)/ 全体角度
//!
//! 中心と方向は作業平面の上の値として持つ(u, v)。世界座標への変換は
//! `kachakacha::v2::modeling::WorkPlaneFrame::PointAt` を呼ぶ側(V2MainWindow)が行う。
//! ここは「欄に入っている値をそのまま返す」だけにして、作業平面を知らなくてもよいようにする。

#include "V2ArrayChoice.h"

#include <QDockWidget>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;
class QWidget;

class V2ArrayDock final : public QDockWidget {
public:
    explicit V2ArrayDock(QWidget* parent);

    //! いまの並べ方を棚へ映す。circular で作り方カードも合わせる。
    void ShowChoice(const V2ArrayChoice& choice, bool circular);
    //! いま欄に入っている値。center/step は作業平面の (u, v) をそのまま持つので、
    //! 世界座標にするのは呼ぶ側の仕事(RunLinearArray/RunCircularArray と同じ形に合わせるため、
    //! ここでは du/dv を step.x/step.y、u/v を center.x/center.y に仮置きする)。
    [[nodiscard]] V2ArrayChoice Choice() const;
    [[nodiscard]] bool Circular() const noexcept { return circular_; }
    //! 方向カード(直線のときだけ)。0 なら作業平面の X、1 なら Y。
    [[nodiscard]] int DirectionIndex() const;

    //! 確定 / キャンセル。
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);
    //! **見えているボタンを実際に押す。** 人の道の試験はこちらを使う。
    [[nodiscard]] bool ClickConfirm();
    [[nodiscard]] bool ClickCancel();
    //! 作り方カードを実際に押す(試験用)。
    [[nodiscard]] bool ClickMethod(bool circular);
    //! 個数の欄に値を打ち込む(自己試験の「打ち込む」相当。setValue で valueChanged も動く)。
    void SetCountTyped(int value);
    [[nodiscard]] int Count() const;

private:
    void ApplyMethodVisibility();

    QToolButton* methodLinear_ = nullptr;
    QToolButton* methodCircular_ = nullptr;
    QSpinBox* count_ = nullptr;
    QWidget* linearRow_ = nullptr;
    QDoubleSpinBox* interval_ = nullptr;
    QComboBox* direction_ = nullptr;
    QWidget* circularRow_ = nullptr;
    QDoubleSpinBox* centerU_ = nullptr;
    QDoubleSpinBox* centerV_ = nullptr;
    QDoubleSpinBox* totalAngle_ = nullptr;
    QPushButton* confirm_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QLabel* hint_ = nullptr;
    bool circular_ = false;
    bool loading_ = false;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
