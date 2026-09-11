#pragma once

//! 道具と相手の順番(オーナー指摘 2026-09-11)。
//!
//! 「すべての道具は **道具を選ぶ → 相手を選ぶ** の順で使う」。
//! V2 はそうなっていなかった。
//!
//!   - 移動・複製・鏡映・回転は、道具を選んで2点を押すと
//!     「先に動かす線を選んでください」と断られた。**押した後に言われる。**
//!   - トリム・延長・面取りなどは、押した瞬間に「線を2本選んでください」で終わった。
//!     道具を構えたまま待ってくれない。
//!   - そのうえ、起動直後の道具が「直線」で、モードを変えても道具が変わらないので、
//!     **画面を押すと線が引けてしまい、選ぶことができない。**
//!
//! ここが決めるのは3つ。
//!   1. モードを変えたら、どの道具へ戻すか
//!   2. その押しは「相手を選ぶ押し」か「点を置く押し」か
//!   3. 構えた命令を、いつ実行するか
//!
//! 3 を分けるのは、条件が「ちょうど2本」なら2本目で走ってよいが、
//! 「1本以上」なら1本目で走らせてはいけないためである。
//! 走らせると、2本目を選ぶ前に終わってしまう。

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"

namespace kachakacha::v2::app {

//! モードを変えたときに戻る道具。
//!
//! **必ず「選択」へ戻す。** 前の道具が残っていると、部品モードへ移った直後に
//! 画面を押して線が引ける。モードを変えるのは「何を相手にするか」を
//! 変えることなので、道具は白紙に戻すのが素直である。
[[nodiscard]] constexpr modeling::DrawingTool ToolAfterModeChange() noexcept
{
    return modeling::DrawingTool::Select;
}

//! その道具は、点を置く前に「相手の線」が要るか。
//!
//! 移動・複製・鏡映・回転は、動かす相手が決まっていないと点に意味がない。
[[nodiscard]] bool ToolNeedsTargetWire(modeling::DrawingTool tool) noexcept;

//! いまの押しは「相手を選ぶ押し」か。
//!
//! 相手が要る道具で、まだ何も選んでおらず、点も置いていないときだけ真。
//! 1点でも置いた後に選び直させると、置いた点が無駄になる。
[[nodiscard]] bool ClickPicksTarget(modeling::DrawingTool tool, bool hasSelection,
    int pointsPlaced) noexcept;

//! その条件は「ちょうど何個」と決まっているか。
//! 決まっていれば、そろった時点で走ってよい(足す余地がない)。
[[nodiscard]] bool PredicateIsExact(SelectionPredicate predicate) noexcept;

//! 選択または役割表の行を追加すれば満たせる条件か。
//!
//! これが true のコマンドは、まだ対象が無くてもUI入口を無効にしない。
//! コマンドを開始し、必要な対象を選ぶ状態へ入る。
[[nodiscard]] bool PredicateCanBeSatisfiedBySelection(
    SelectionPredicate predicate) noexcept;

//! 構えた命令をどうするか。
enum class PendingAction {
    //! まだ足りない。待つ。
    Wait,
    //! そろった。すぐ走ってよい。
    RunNow,
    //! そろったが、まだ足せる。人が「これで」と言うまで待つ。
    NeedsConfirm,
};

[[nodiscard]] PendingAction PendingCommandAction(SelectionPredicate predicate, bool satisfied,
    bool confirmed) noexcept;

} // namespace kachakacha::v2::app
