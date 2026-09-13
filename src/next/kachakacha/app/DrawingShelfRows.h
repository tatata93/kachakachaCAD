#pragma once

//! 作図の棚に、いまの道具で意味のある欄はどれか(オーナー指摘 2026-09-13)。
//!
//! 棚の中身が、道具を替えても変わらなかった。
//! 直線や円弧を引いたあとベジェ曲線に持ち替えても、右にはまだ
//! 「円弧の作り方」が出ていて、ベジェの画面にならない。
//! 出ている欄が、いま押しても何にも効かない欄である。
//!
//! 棚そのもの(どの棚を前に出すか)は ShelfLayout が決める。
//! ここが決めるのは、**その棚の中のどの欄を出すか** である。
//!
//! 決め方を core に置くのは、画面を出さずに確かめられるようにするためである。

#include "kachakacha/modeling/ToolController.h"

#include <string>
#include <string_view>

namespace kachakacha::v2::app {

//! 作図の棚の欄。出すか出さないかだけを持つ。
struct DrawingShelfRows {
    //! 円弧の作り方・半径・中心角。円弧の道具のときだけ。
    bool arc = false;
    //! 補助線として作図する。線を引く道具すべて。
    bool construction = false;
    //! 指定した点を作図点として残す。点を置く道具すべて。
    bool keepPoints = false;
    //! 数値で線を作る。道具に関係なくいつでも使える(別の区画)。
    bool directWire = true;

    //! 道具に応じて変わる欄が1つも無いか。
    //! 空のまま出すと「壊れた」ように見えるので、代わりに使い方を出す。
    [[nodiscard]] bool ToolSectionEmpty() const noexcept
    {
        return !arc && !construction && !keepPoints;
    }
};

[[nodiscard]] DrawingShelfRows DrawingShelfRowsFor(modeling::DrawingTool tool) noexcept;

//! 棚の見出し。いまの道具の名前を入れる。
//! 「作図」だけだと、どの道具の欄なのかが読めない。
[[nodiscard]] std::string DrawingShelfTitleJa(modeling::DrawingTool tool);

//! その道具の使い方の一文。決める欄が無い道具でも、次にすることが分かるようにする。
//! 空は返さない。
[[nodiscard]] std::string_view DrawingToolHintJa(modeling::DrawingTool tool) noexcept;

} // namespace kachakacha::v2::app
