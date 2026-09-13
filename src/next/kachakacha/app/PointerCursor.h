#pragma once

//! いまカーソルをどの形にするか(ui-ux-integrated-spec §5.1、UI-P1-008)。
//!
//! §5.1 は「クリック可能な通常形状に指カーソルを使わない」と書いている。
//! V2 はそれと逆で、選択道具で線や形の上へ来ると指になっていた。
//! 指は本来「押すと別の場所へ行く」印であって、図形の印ではない。
//! 図形の上に来たことは Hover の強調と短い言葉で伝える(§5.1 の但し書き)。
//!
//! 形の決め方を core に置くのは、画面を出さずに確かめるためである。
//! Qt の enum へ写すのは画面側の仕事で、ここは Qt を知らない。

#include <string_view>

namespace kachakacha::v2::app {

//! 出しうるカーソルの形。§5.1 の並びと同じ。
enum class CursorShape {
    //! 通常選択、形状Hover。
    Arrow,
    //! パンできる(まだ押していない)。
    OpenHand,
    //! パン中、キューブや操作板を掴んでいる。
    ClosedHand,
    //! 軌道回転中。
    Rotate,
    //! 作図点を置く。
    Cross,
    //! 掴んで動かしている。
    Move,
    //! そこには置けない、拾えない。
    Forbidden,
};

[[nodiscard]] std::string_view CursorShapeNameJa(CursorShape shape) noexcept;

//! カーソルの形を決めるのに要る、画面のいまの様子。
//!
//! 真偽値だけで持つ。Qt の型も、画面の寸法も持ち込まない。
struct PointerCursorContext {
    //! 中ボタンで画面を平行移動している。
    bool panning = false;
    //! 軌道回転している。
    bool orbiting = false;
    //! 選んだ物を掴んで動かしている。
    bool draggingBody = false;
    //! 制御点を掴んで動かしている。
    bool draggingControlPoint = false;
    //! 視点キューブや操作板を掴んでいる。
    bool draggingViewGadget = false;
    //! 視点キューブや操作板の上にいる(まだ押していない)。
    bool overViewGadget = false;
    //! いる場所のものが拾えない(面の外で薄く出ているなど)。
    bool overForbidden = false;
    //! いまの道具は、押すと作図点を置く。
    bool placingPoints = false;
};

//! §5.1 の割り当て。上にあるものほど強い。
//!
//! 強い順にするのは、たとえば掴んで動かしている最中に
//! 形の上を通っても、カーソルが移動から矢印へ戻らないようにするためである。
[[nodiscard]] CursorShape ChooseCursorShape(const PointerCursorContext& context) noexcept;

} // namespace kachakacha::v2::app
