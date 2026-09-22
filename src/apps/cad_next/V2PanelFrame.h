#pragma once

//! 道具の棚の共通の枠(C-10)を、画面の部品として作る小道具。
//!
//! 並び順の約束と見出しの字の読み方は core(app/PanelFrame.h)。ここは見た目を揃えるだけ。
//!   - 節の見出しは太字、objectName は "panelSectionTitle"(自己試験がこれで並びを読む)
//!   - 下のボタンは「キャンセル Esc」「確定 Enter」、objectName は "panelCancel" / "panelConfirm"

#include <QString>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QWidget;

//! 節の見出し。字は正本どおり(「1. 作り方」「2. 入力」「共通」など)。
[[nodiscard]] QLabel* MakePanelSectionTitle(QWidget* parent, const QString& text);

//! 既にある見出しの字を、節の見出しとして見た目と名前だけ揃える(字を差し替える見出し用)。
void StylePanelSectionTitle(QLabel* label);

//! 下の「キャンセル Esc」「確定 Enter」の行。作った 2 つのボタンを返す(つなぎは呼ぶ側)。
[[nodiscard]] QHBoxLayout* MakeCancelConfirmRow(QWidget* parent, QPushButton** cancel,
    QPushButton** confirm);

//! 既にあるボタンを、共通の枠のキャンセル・確定として名前を付ける(字も揃える)。
void MarkCancelConfirm(QPushButton* cancel, QPushButton* confirm);
