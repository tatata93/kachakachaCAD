#pragma once

//! 画面の一番下に出す、いまの道具の一行(UI の正本の footer、オーナー指示 §15)。
//!
//! 正本にはこう書いてある。
//!   押し出し: TARGET=Part001 / PROFILE=NoseProfile_01 / 18.0mm / Add / OUTPUT=Solid
//!   面を作る: METHOD=Loft / SECTIONS=3 / GUIDES=2 / Preview only
//!
//! 右の棚の日本語の帯とは役目が違う。**いまの入力が一目で全部並ぶ**行である。
//! 棚を閉じていても、いま何で押そうとしているのかが分かる。

#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/app/SurfaceInputState.h"

#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 押し出しの一行。名前は呼ぶ側が文書から引いて渡す。
[[nodiscard]] std::string ExtrudeFooterLine(const ExtrudeInputState& state,
    const std::string& targetName, const std::vector<std::string>& profileNames);

//! 「面を作る」の一行。
[[nodiscard]] std::string SurfaceFooterLine(const SurfaceInputState& state,
    bool previewShown);

//! 正本の footer の右側。**いつでも見えているようにする。**
//! これが出ていないと、Ctrl も Tab も知らない人には使えない
//! (オーナー指示「Ctrl を知らないと操作できない設計は禁止」)。
[[nodiscard]] std::string ToolKeyHintJa();

} // namespace kachakacha::v2::app
