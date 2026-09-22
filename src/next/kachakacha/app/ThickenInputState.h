#pragma once

//! 「厚み」の入力の状態(指示書 matrix P-10)。
//!
//! これまでの厚みは「形状ガイドの面を選んでから押す」窓(Dialog)だった。
//! ここでは足す・引くと同じ「道具から始める」形にする。
//!
//! 厚みを押す → 面待ち → 3D で形状ガイドの面を押すと入る(押し直すと外れる) →
//! 作り方(外側/中央/内側/平面まで)を選ぶ → 「平面まで」だけは相手の作業平面も選ぶ →
//! 下見 → Enter で確定。
//!
//! 何がどの欄に入るかは、ここが決める。画面は映して押すだけ
//! (app/BooleanInputState と同じ役目分け)。

#include "kachakacha/base/Ids.h"
#include "kachakacha/fabrication/FabricationSettings.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

struct ThickenInputState {
    //! 厚みを付ける形状ガイドの面(何枚でも。1 枚ずつ別の部品にし、1 回の取り消しで戻る)。
    std::vector<base::EntityId> surfaces;
    //! 作り方(外側/中央/内側)。「平面まで」を選んでいる間も値は残す
    //! (平面までをやめたら、直前の作り方へそのまま戻れるようにするため)。
    fabrication::ThicknessPlacement placement = fabrication::ThicknessPlacement::Outside;
    //! 4枚目のカード「平面まで」を選んでいるか。
    bool toPlane = false;
    //! 「平面まで」の相手の作業平面。toPlane のときだけ使う。
    base::EntityId targetPlane;
    //! 厚み(mm)。ParameterId::ExtrudeDistance(板厚)と同じ値を持つ。
    //! 「平面まで」のときは使わない(相手との距離で厚みが決まる)。
    double thicknessMm = 0.0;
};

//! 3D で形状ガイドの面を押した。入っていれば外れる。入っていなければ足す(何枚でも)。
[[nodiscard]] ThickenInputState WithThickenPick(const ThickenInputState& state,
    const base::EntityId& id);

//! 3D の選択からその面が外れた(Ctrl+クリック等)。欄も空にする。
[[nodiscard]] ThickenInputState WithoutThickenSurface(const ThickenInputState& state,
    const base::EntityId& id);

//! 面が入っていて、作り方が決まっていること。
//! 「平面まで」なら相手の作業平面も要る。それ以外は厚みが正であること。
[[nodiscard]] bool ThickenReadyToBuild(const ThickenInputState& state) noexcept;

//! 実際に厚みを付けた結果の要約。下見と確定は同じものを使う。
struct ThickenPreviewOutcome {
    bool evaluated = false;
    bool available = false;
    double volumeMm3 = 0.0;
    //! 実際に付いた厚み。「平面まで」のときは指定でなく計算した値(何枚かなら最も厚いもの)。
    double thicknessMm = 0.0;
    //! 作った部品の数(面の数と同じ)。
    std::size_t count = 0;
    std::string refusalJa;
};

//! 棚の「状態」に出す行。
[[nodiscard]] std::vector<std::string> ThickenStatusLinesJa(const ThickenInputState& state,
    const ThickenPreviewOutcome& outcome, bool previewShown);

//! 一番下の一行。「厚み: SURFACE=名前 / 2.0mm / 外側 / Preview only」
//! (「平面まで」のときは厚みと作り方の代わりに PLANE=名前 / 平面まで を出す)。
[[nodiscard]] std::string ThickenFooterLine(const ThickenInputState& state,
    const std::string& surfaceName, const std::string& planeName,
    const ThickenPreviewOutcome& outcome, bool previewShown);

} // namespace kachakacha::v2::app
