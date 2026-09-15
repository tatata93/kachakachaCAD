#pragma once

//! 道具の最中の Enter / Esc を、どう受け取るか(オーナー指示 2026-09-15 §14)。
//!
//! これまで Enter と Esc は 3D 画面の `keyPressEvent` にしか無かった。
//! 右の欄へ数字を打った直後は焦点が欄にあるので届かず、**3D を一度クリックして
//! 焦点を戻すしかなかった。そのクリックで選択が変わる。**
//!
//! 決め方はここ(core)が持つ。画面はここへ聞いて、そのとおりに動くだけにする。
//! 画面で決めると、確かめるのに画面を出さなければならなくなる。

namespace kachakacha::v2::app {

//! 合図を受けて、道具が何をするか。
enum class ToolKeyAction {
    //! 受け取らない。ふだんの欄や画面の扱いに任せる。
    Ignore,
    //! やめる。
    Cancel,
    //! 打った値を入れて、下見を作り直す。**確定はしない。**
    CommitValueAndWait,
    //! 確定する。
    Confirm,
};

//! 何が起きたか。画面が見たままを渡す。
struct ToolKeyContext {
    //! 下見を出している道具が動いているか。動いていなければ何も受け取らない。
    bool previewActive = false;
    //! 合図が来たのが、打ち込む欄の中か。
    bool inTypingField = false;
    //! その欄の値が、この合図で変わったか。
    bool valueChanged = false;
};

//! 決まりは1つだけ。
//!
//!   - 欄の中で Enter を押し、**値が変わった** → その値を入れて下見を作り直す
//!   - 欄の中で Enter を押し、**値が変わっていない** → 確定する
//!   - 欄の外で Enter → 確定する
//!   - Esc → やめる
//!
//! こうすると「打った値は必ず一度、下見で見える」。
//! 見ていない値で作らない(オーナー指示 §9)。
[[nodiscard]] ToolKeyAction ActionForConfirmKey(const ToolKeyContext& context) noexcept;
[[nodiscard]] ToolKeyAction ActionForCancelKey(const ToolKeyContext& context) noexcept;

} // namespace kachakacha::v2::app
