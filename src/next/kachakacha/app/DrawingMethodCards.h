#pragma once

//! 作図の道具の「作り方」カード(正本 3 HTML 2026-09-18 の methods、指示書 D-01〜D-14)。
//!
//!   線:       2点 / 点＋長さ＋角度
//!   円:       中心＋半径 / 直径指定 / 3点(核に無い)
//!   円弧:     3点 / 始点・終点・半径 / 中心・始点・終点(核に無い) / その他: 始点接線
//!   ベジェ:   制御点で作成
//!   スプライン: 制御点 / 通過点(核に無い) / 近似・Fit(核に無い)
//!   移動/コピー: 2点     ミラー: 鏡の線2点     回転: 中心+2方向
//!   分割:     押した場所で   トリム: 消したい側を押す   延長: 伸ばす端を押す
//!   結合:     端点2つ / 接線 / 曲率(3つの道具それぞれ1枚)
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
    //! そのカードを選んだら、カーソル横の入力欄のどれに焦点を移すか(CursorInput の欄の id)。
    //! 空なら移さない。**カードを押しても何も起きない、を作らないための欄である**
    //! (例: 円の「直径指定」は 直径 の欄へ移す。Tab で探させない)。
    std::string cursorFieldId;

    [[nodiscard]] bool Blocked() const noexcept { return !blockedReasonJa.empty(); }
};

//! その道具のカード。作り方が1つしかない道具も1枚返す。
//! 編集・変形の道具(移動・コピー・ミラー・回転・分割・トリム・延長・結合)は
//! いまのところ作り方が1つなので1枚だけ返す。一文は DrawingToolHintJa と同じにする
//! (指示書 D-15/D-21、一道具一枚 = 道具のページ)。決める意味の無い道具(選択など)は空。
[[nodiscard]] std::vector<DrawingMethodCard> DrawingMethodCardsFor(modeling::DrawingTool tool);

//! いまの設定(円弧の作り方)に当たるカードの位置。カードが無ければ -1。
//! 円弧以外は最初の押せるカード。
[[nodiscard]] int CurrentDrawingMethodIndex(modeling::DrawingTool tool,
    const modeling::ToolSettings& settings);

} // namespace kachakacha::v2::app
