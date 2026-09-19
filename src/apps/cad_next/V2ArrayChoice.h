#pragma once

//! 並べ方(配列)の入力。棚(V2ArrayDock、指示書 D-23)と自己試験の差し替え口が使う。
//!
//! 判断(2個以上か、多すぎないか、一周なら最後を重ねないか)は core(app/ArrayPlan)にある。
//! ここで数えると、画面を出さないと確かめられなくなる。
//! 以前あった窓(V2ArrayDialog)は棚に置き換わったので消した(2026-09-19)。

#include "kachakacha/geometry/Vector3.h"

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
    //! 円の並べ方を選んでいるか(D-23、右ペインの棚 V2ArrayDock)。
    //! 直線と円で欄が違うので、いま作り方カードのどちらを押しているかを持たせておく。
    bool circular = false;
};
