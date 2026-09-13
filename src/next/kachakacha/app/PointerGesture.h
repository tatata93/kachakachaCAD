#pragma once

//! 押したのか、引きずったのか(ui-ux-integrated-spec §5.2、UI-P1-008)。
//!
//! ここが無かったころ、同じことを3か所が別々に決めていた。
//! 掴んで動かすのは 4px、矩形選択は 5px、右ボタンはまた 4px。
//! おまけに矩形選択だけは **離した場所** で決めていたので、
//! 100px 引きずってから元の場所へ戻して離すと、引きずらなかったことになった。
//!
//! 決め方はここ1か所に置く。
//!
//! - 押した場所からの隔たりが 5 logical px 未満ならクリック、以上ならドラッグ。
//! - **一度ドラッグになったら、指を戻してもクリックには戻らない。**
//!   戻ると、引きずり終わりが選び直しに化ける。
//! - 隔たりは画面の px で測る。ズーム倍率では変えない。
//!   変えると、拡大するほど手が震えたことになる。

#include "kachakacha/geometry/ScreenMapping.h"

namespace kachakacha::v2::app {

//! これ未満はクリック、これ以上はドラッグ(logical px)。
//! ここだけが持つ。機能ごとに書き写さない。
inline constexpr double kClickDragThresholdPx = 5.0;

//! 押してから離すまでに、指が何をしたか。
enum class PointerGesture {
    //! まだ門を越えていない。押しただけ。
    Click,
    //! 門を越えた。以後クリックへは戻らない。
    Drag,
};

//! 押した場所からの隔たりが門を越えたか。向きは関係ない。
[[nodiscard]] bool ExceedsDragThreshold(double dxPx, double dyPx) noexcept;

//! 押した1回ぶんの成り行きを覚えておく小さな入れ物。
//!
//! 画面側で `moved` の真偽値を各所に散らすと、
//! 「どこで false へ戻したか」を追えなくなる。ここへ集める。
class PointerGestureTracker {
public:
    //! 押した。ここから測り始める。
    void Begin(const geometry::ScreenPoint& origin) noexcept;

    //! 動いた。ドラッグへ **入った瞬間だけ** true を返す。
    //! すでにドラッグなら false(入り直しではない)。
    bool Update(const geometry::ScreenPoint& now) noexcept;

    //! 離した。いま何だったかを返す。位置は Update と同じ扱いをする。
    [[nodiscard]] PointerGesture Release(const geometry::ScreenPoint& now) noexcept;

    //! 押している最中か。
    [[nodiscard]] bool Active() const noexcept { return active_; }

    //! いまの見立て。押していないときは Click を返す。
    [[nodiscard]] PointerGesture Kind() const noexcept
    {
        return drag_ ? PointerGesture::Drag : PointerGesture::Click;
    }

    //! ドラッグになったか。掴んだ物を実際に動かしてよいかの判断はこれで行う。
    [[nodiscard]] bool IsDrag() const noexcept { return drag_; }

    //! 押した場所。
    [[nodiscard]] geometry::ScreenPoint Origin() const noexcept { return origin_; }

    //! 押していないことにする。道具を変えたときや Esc で呼ぶ。
    void Reset() noexcept;

private:
    geometry::ScreenPoint origin_{};
    bool active_ = false;
    bool drag_ = false;
};

} // namespace kachakacha::v2::app
