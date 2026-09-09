#pragma once

//! Esc を押したとき何が起きるか(V1同等)。
//!
//! V1 の Esc は **必ず「選択道具・何も選んでいない」で終わる。**
//! ただし、いきなりそこへ飛ぶのではない。
//! **やりかけを1つだけ取り消してから** 選択へ戻る。
//!
//! なぜ1つだけか。1回のEscで全部消すと、線を1本引きかけただけのつもりが
//! 選択もモードも消えて、どこまで戻ったのか分からなくなる。
//! 逆に「取り消すだけ」で選択へ戻らないと、Escを押したのに
//! まだ道具を持ったままで、次のクリックで意図しない線が出る。
//! V2 はこの後者だった。
//!
//! 判断をここへ置いたのは、画面を出さずに確かめるためである。

#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! Esc を押した時点で、何が起きているか。
struct EscapeContext {
    //! 視点の操作板(輪・矢印)を引きずっている。
    bool draggingGadget = false;
    //! ビューキューブを引きずっている。
    bool draggingCube = false;
    //! 「押す場所を1回押してください」と待っている(トリム・延長・グリッド原点)。
    bool waitingForPick = false;
    //! カーソル横の数値入力が開いている。
    bool cursorInputOpen = false;
    //! 作図の途中で、点を1つ以上置いている。
    bool toolHasPoints = false;
    //! 何かを選んでいる。
    bool hasSelection = false;
    //! いま選択道具である。
    bool toolIsSelect = false;
};

//! Esc がすること。順に実行する。
enum class EscapeStep {
    CancelGadgetDrag,   //!< 視点の引きずりを中断する(姿勢は押す前へ戻さない)
    CancelCubeDrag,     //!< キューブの引きずりを中断する
    CancelPick,         //!< 押す場所を待つのをやめる
    CloseCursorInput,   //!< 数値入力を閉じる
    CancelDrawing,      //!< 作図の途中を捨てる
    ClearSelection,     //!< 選択を空にする
    BackToSelectTool,   //!< 選択道具へ戻る
};

//! いまの状況から、Esc がすることを組み立てる。
//!
//! **やりかけの取り消しは、上から1つだけ。**
//! そのあとに、選択を空にして選択道具へ戻る手順が必ず付く。
[[nodiscard]] std::vector<EscapeStep> PlanEscape(const EscapeContext& context);

//! 帯に出す一言。何を取り消したのかを言う。
//! 言わないと、Escが効いたのかどうかが分からない。
[[nodiscard]] std::string_view EscapeMessageJa(const std::vector<EscapeStep>& steps);

} // namespace kachakacha::v2::app
