#pragma once

#include <QString>

//! 中核(model/io/occt)が投げる英語の例外文を日本語へ言い換える。
//!
//! 中核はUIを持たない層なので例外文が英語のままで、それがそのまま
//! 画面へ出てしまっていた(実機スクリーンショットで発見:
//! 「この選択では作れません: Projected wire cannot be used as loft section.」)。
//! 使う人は模型を作りたい人であってプログラマではないので、
//! 画面へ出す直前にここで日本語へ置き換える。
//!
//! 「英語文: 名前」の形(名前を後ろに足す例外)にも対応する。
//! 表に無い文はそのまま返す(消してしまうより残したほうが調べられる)。
[[nodiscard]] QString TranslateCoreMessage(const QString& text);
