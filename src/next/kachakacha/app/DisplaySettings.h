#pragma once

//! 見え方の設定(AT-UIX-010)。
//!
//! 「形は変わらない」ことが大事である。ここで変えるのは見え方だけで、
//! 文書には何も書かない。書くと、見え方を変えただけで保存が要る文書になる。
//!
//! 段は決まった順に回る。押すたびにどこへ行くかが読めないと、
//! 目当ての見え方へ戻すのに何度も押すことになる。

#include <string_view>

namespace kachakacha::v2::app {

//! 見え方の段。押すたびにこの順で回る。
enum class DisplayStage {
    All,          //!< グリッドも補助線も出す
    NoGrid,       //!< グリッドを消す。線だけを見たいとき
    NoConstruction, //!< 補助線も消す。出来上がりの形だけを見たいとき
};

struct DisplaySettings {
    bool gridVisible = true;
    bool constructionVisible = true;
};

//! 次の段へ。All → NoGrid → NoConstruction → All と回る。
[[nodiscard]] DisplayStage NextDisplayStage(DisplayStage stage) noexcept;

//! その段の設定。
[[nodiscard]] DisplaySettings SettingsForStage(DisplayStage stage) noexcept;

//! 帯へ出す一言。何が消えているかを言う。言わないと、消えたのか壊れたのか分からない。
[[nodiscard]] std::string_view DisplayStageNameJa(DisplayStage stage) noexcept;

} // namespace kachakacha::v2::app
