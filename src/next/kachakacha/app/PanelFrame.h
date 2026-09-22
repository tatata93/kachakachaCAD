#pragma once

//! 道具の棚の共通の枠(C-10、UI 正本の各モードの右ペイン)。
//!
//!   見出し + 案内 → 作り方 → 入力(対象) → 設定・オプション → 共通 → 状態 → キャンセル・確定
//!
//! 正本(作図・部品・製作の 3 つの HTML)は、どの道具の棚もこの並びで描いている。
//! 棚ごとに中身は違ってよいが、**節の並び順** と **下のキャンセル・確定** は揃える。
//! 並びが道具ごとに違うと(厚みは「入力 → 作り方」、面を作るは「作り方 → 入力」だった)、
//! 次に目をやる場所が道具ごとに変わる。
//!
//! 画面の見出しの字(「2. 入力」「作り方」など)から節の種類を決め、並びを確かめるのはここ。
//! 自己試験「道具の棚は共通の枠」が、実際の棚の見出しをこれに通す(門)。

#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class PanelSectionKind {
    Method,     //!< 作り方(方式のカード)。正本では見出しのすぐ下
    Input,      //!< 入力・対象(3D で拾うもの)
    Settings,   //!< 設定・オプション・結果・範囲・演算・出力・断面順・厚み など
    Common,     //!< 共通(スナップなど、どの作り方でも同じもの)
    State,      //!< 状態・プレビュー
    Other,      //!< 並びを決めないもの(表示・候補・読み方など)
};

//! 見出しの字から節の種類を決める。先頭の「1. 」のような番号は読み飛ばす。
[[nodiscard]] PanelSectionKind PanelSectionKindOf(std::string_view titleJa);

[[nodiscard]] std::string_view PanelSectionKindNameJa(PanelSectionKind kind) noexcept;

//! 見出しの並びが約束どおりか。外れていれば、どの 2 つが逆かを日本語で返す
//! (空の文字列なら約束どおり)。Other は並びを決めないので飛ばす。
[[nodiscard]] std::string PanelSectionOrderProblemJa(const std::vector<std::string>& titlesJa);

} // namespace kachakacha::v2::app
