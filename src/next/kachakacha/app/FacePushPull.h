#pragma once

//! 立体の面をつまんで押す・引く(EX-02)。
//!
//! 面を選んで矢印を引くと、外へ引けば材料が増え、中へ押せば材料が減る。
//! 作る人はそう思って引く。ところが押し出しの中身は
//! 「正の距離だけ」「足すか引くかは別の欄」で出来ている。
//! その食い違いをここで吸収する。
//!
//! **新しい押し出しは作らない。** 符号つきの距離を、いままでの押し出しが
//! 分かる形(正の距離・向きの反転・足す/引く)へ言い換えるだけである。
//! こうしておくと、体積と面数の突き合わせも今までのものがそのまま効く。

#include "kachakacha/modeling/ExtrudeInput.h"

#include <string>

namespace kachakacha::v2::app {

//! 面の押し引きを、いままでの押し出しの言葉へ言い換えたもの。
struct FacePushPullPlan {
    //! 押し出しへ渡す距離。必ず正。
    double distanceMm = 0.0;
    //! 面の外向き法線に対して反転するか。中へ押すときに真。
    bool reversed = false;
    modeling::ExtrudeBooleanMode booleanMode = modeling::ExtrudeBooleanMode::AddToPart;
    //! 押し出せる状態か。0mm では作れない。
    bool ready = false;
    //! 作る人へ出す言葉。ready でないときは、次に何をすればよいかを言う。
    std::string messageJa;
};

//! 符号つきの距離(矢印を引いた量)から、押し出しの指定を作る。
//!
//! 正(外向き)なら足す。負(内向き)なら向きを反転して引く。
//! 0 のときは作らない。0mm の押し出しは形を変えないので、
//! 「できた」と言ってはならない。
[[nodiscard]] FacePushPullPlan PlanFacePushPull(double signedDistanceMm,
    double minimumDistanceMm = 1.0e-6);

//! 押し引きの結果を作る人の言葉で言う。帯に出す。
[[nodiscard]] std::string DescribeFacePushPullJa(const FacePushPullPlan& plan);

} // namespace kachakacha::v2::app
