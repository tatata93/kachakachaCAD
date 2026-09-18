#pragma once

//! 作図の道具の「作り方」カード(正本 3 HTML 2026-09-18 の methods、指示書 D-01〜D-14)。
//!
//!   線:       2点 / 点＋長さ＋角度
//!   円:       中心＋半径 / 直径指定 / 3点(核に無い)
//!   円弧:     3点 / 始点・終点・半径 / 中心・始点・終点(核に無い) / その他: 始点接線
//!   ベジェ:   制御点で作成
//!   スプライン: 制御点 / 通過点(核に無い) / 近似・Fit(核に無い)
//!
//! 核に無い作り方は **押せない形で理由を出す**(「押せるが何も起きない」を作らない)。
//! 正本に無いが核にある作り方(円弧の始点接線)は「その他」として失わない。
//! 何を並べるかはここ(core)が決める。画面は並べて押すだけ。

#include "kachakacha/modeling/ToolController.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

struct DrawingMethodCard {
    std::string labelJa;
    //! 空でなければ核に無く、押せない。理由は日本語で。
    std::string blockedReasonJa;
    //! 円弧のカードは作り方(ArcMode)を決める。ほかの道具のカードは持たない。
    std::optional<modeling::ArcMode> arcMode;
    //! そのカードを選んだときの、次にすることの一文。空にはならない。
    std::string hintJa;
    //! 正本に無い(「その他」)。並びの後ろに出す。
    bool extra = false;

    [[nodiscard]] bool Blocked() const noexcept { return !blockedReasonJa.empty(); }
};

//! その道具のカード。作り方が1つしかない道具も1枚返す。決める意味の無い道具(選択など)は空。
[[nodiscard]] std::vector<DrawingMethodCard> DrawingMethodCardsFor(modeling::DrawingTool tool);

//! いまの設定(円弧の作り方)に当たるカードの位置。カードが無ければ -1。
//! 円弧以外は最初の押せるカード。
[[nodiscard]] int CurrentDrawingMethodIndex(modeling::DrawingTool tool,
    const modeling::ToolSettings& settings);

} // namespace kachakacha::v2::app
