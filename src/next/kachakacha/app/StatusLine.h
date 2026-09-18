#pragma once

//! 画面の一番下の状態行と、3D 左上の HUD の文言(正本 3 HTML 2026-09-18、指示書 C-11 / C-12)。
//!
//!   状態行: モード ｜ 道具 ｜ 案内 ‖ 座標 ｜ Grid ｜ Snap ｜ Enter/Esc
//!   HUD:    1行目 = モード › 道具、2行目 = 案内(次に何をするか)
//!
//! 文言はここ(core)が組む。画面は並べるだけ。画面で組むと、確かめるのに画面が要る。
//! 測定を重ねているときは、HUD に「測定中(Esc で <元の道具> へ戻る)」が付く。

#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

struct StatusLineParts {
    UiMode mode = UiMode::Drawing;
    std::string toolJa;             //!< 道具の名前(空なら「選択」)
    std::string hintJa;             //!< いまの案内(空なら出さない)
    std::optional<double> cursorU;  //!< 作業平面上のカーソル位置(無ければ座標を出さない)
    std::optional<double> cursorV;
    double gridMm = 0.0;            //!< グリッドの間隔(0 以下なら「Grid なし」)
    bool gridShown = true;
    bool snapOn = true;
    std::string resumeToolJa;       //!< 測定を重ねている元の道具(空なら重ねていない)
};

//! 状態行の左側(モード ｜ 道具 ｜ 案内)。
[[nodiscard]] std::string StatusLeftText(const StatusLineParts& parts);
//! 状態行の右側(座標 ｜ Grid ｜ Snap ｜ Enter/Esc)。
[[nodiscard]] std::string StatusRightText(const StatusLineParts& parts);
//! 座標だけ(「U 12.0  V -3.5」)。無ければ「—」。
[[nodiscard]] std::string CursorText(const StatusLineParts& parts);
//! HUD の行(左上に上から順に出す)。空にはならない。
[[nodiscard]] std::vector<std::string> HudLines(const StatusLineParts& parts);

//! 測定を重ねる: 測定へ持ち替えるとき、戻り先にする道具。
//! 選択道具・測定そのものからは戻り先を作らない(戻る意味がない)。
[[nodiscard]] std::optional<modeling::DrawingTool> ToolToResumeAfterMeasure(
    modeling::DrawingTool before, modeling::DrawingTool next) noexcept;

} // namespace kachakacha::v2::app
