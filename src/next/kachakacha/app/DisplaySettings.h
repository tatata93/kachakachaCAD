#pragma once

//! 見え方の設定(AT-UIX-010)。
//!
//! 「形は変わらない」ことが大事である。ここで変えるのは見え方だけで、
//! 文書には何も書かない。書くと、見え方を変えただけで保存が要る文書になる。
//!
//! 段は決まった順に回る。押すたびにどこへ行くかが読めないと、
//! 目当ての見え方へ戻すのに何度も押すことになる。
//! 段は V1 の 設計(Ctrl+1)/ 完成形(Ctrl+2)/ 選択だけ(Ctrl+3)に、グリッドだけ消す段を足したもの。

#include <string_view>

namespace kachakacha::v2::app {

//! 見え方の段。押すたびにこの順で回る。
enum class DisplayStage {
    All,            //!< 設計: グリッドも補助線も出す
    NoGrid,         //!< グリッドを消す。線だけを見たいとき
    NoConstruction, //!< 完成形: 補助線も消す。出来上がりの形だけを見たいとき
    SelectionOnly,  //!< 選択だけ: 選んでいるものだけを出す
};

//! 線の様式(V1 の表示設定と同じ3つ)。
enum class LineStyle {
    Solid,
    Dashed,
    Dotted,
};

struct DisplaySettings {
    bool gridVisible = true;
    bool constructionVisible = true;
    //! 選んでいるものだけを出す(V1 の「選択だけ」)。
    bool selectionOnly = false;
    //! 線の太さ(px)と様式。V1 の既定と同じ。
    double wireWidthPx = 2.0;
    LineStyle wireStyle = LineStyle::Solid;
    double constructionWidthPx = 1.7;
    LineStyle constructionStyle = LineStyle::Dashed;
    //! 作図中、作図面の上にない線を薄くする(V1 の「作図面以外の線を常に薄く」)。
    bool dimOffPlaneLines = true;
    //! 作図モード以外でもグリッドを出す(V1 の「作図モード以外でも表示」)。
    bool gridInAllModes = true;
};

//! 次の段へ。All → NoGrid → NoConstruction → SelectionOnly → All と回る。
[[nodiscard]] DisplayStage NextDisplayStage(DisplayStage stage) noexcept;

//! その段の設定(太さや様式は既定のまま)。
[[nodiscard]] DisplaySettings SettingsForStage(DisplayStage stage) noexcept;

//! いまの設定に段だけを当てる。太さ・様式・薄くする・グリッドの出し方は残す。
[[nodiscard]] DisplaySettings ApplyStage(DisplaySettings settings, DisplayStage stage) noexcept;

//! 帯へ出す一言。何が消えているかを言う。言わないと、消えたのか壊れたのか分からない。
[[nodiscard]] std::string_view DisplayStageNameJa(DisplayStage stage) noexcept;

//! 段の短い名前(ボタンに出す)。
[[nodiscard]] std::string_view DisplayStageLabelJa(DisplayStage stage) noexcept;

[[nodiscard]] std::string_view LineStyleNameJa(LineStyle style) noexcept;

//! 太さは 0.25〜12 px に収める(V1 と同じ範囲)。範囲外は端へ寄せる。
[[nodiscard]] double ClampLineWidthPx(double width) noexcept;

} // namespace kachakacha::v2::app
