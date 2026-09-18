#pragma once

//! 2段の帯(上段 = カテゴリ、下段 = 道具)。正本は 3 HTML(2026-09-18)。
//!
//! どのモードにどのカテゴリがあり、どのカテゴリにどの道具が並ぶかは **ここが決める**。
//! 画面(Qt)は並べて押すだけ。道具は台帳の命令 id で指す。
//!
//! backend に無い道具も、正本にあるものは並べる。ただし **押せるが何も起きない** は禁止なので、
//! `blockedReasonJa` を持つ道具は押せない形で出し、理由を日本語で見せる
//! (指示書 diagnostics_and_feedback)。見た目だけの「実装済み」を作らない。
//!
//! 正本に無い既存の道具(角の加工、投影、治具、ワイヤー群から部品、離した面 …)は
//! 「その他」として同じカテゴリに収容する。失わない(指示書 inventory_first)。

#include "kachakacha/app/UiMode.h"

#include <optional>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

struct RibbonTool {
    //! 帯に出す言葉。
    std::string_view labelJa;
    //! 押したときに走らせる台帳の命令。blocked のときは空でもよい。
    std::string_view commandId;
    //! 面作成の道具は同じ `surface.create` を作り方つきで呼ぶ(modeling::GuideSurfaceMethod の整数値)。
    std::optional<int> surfaceMethod;
    //! 測定の道具は同じ `measure.open` を測り方つきで呼ぶ(app::MeasureMode の整数値)。
    std::optional<int> measureMode;
    //! backend に無い。空でなければ押せない形で出し、この理由を見せる。
    std::string_view blockedReasonJa;
    //! 正本に無いが既存の道具(「その他」)。帯では後ろに寄せる。
    bool extra = false;

    [[nodiscard]] bool Blocked() const noexcept { return !blockedReasonJa.empty(); }
};

struct RibbonCategory {
    std::string_view key;
    std::string_view labelJa;
    std::vector<RibbonTool> tools;
};

//! そのモードのカテゴリ。並びは正本のとおり。
[[nodiscard]] const std::vector<RibbonCategory>& RibbonCategoriesFor(UiMode mode);

//! 命令 id がそのモードの帯のどこかにあるか(押せる形かは問わない)。
[[nodiscard]] bool RibbonHasCommand(UiMode mode, std::string_view commandId);

//! 帯の道具の中で、その命令 id を持つ最初のもの。無ければ nullptr。
[[nodiscard]] const RibbonTool* FindRibbonTool(UiMode mode, std::string_view commandId);

} // namespace kachakacha::v2::app
