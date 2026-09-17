#pragma once

//! 「足す・引く」の入力の状態(引継ぎ 2026-09-17 の 4)。
//!
//! これまでの足す・引くは「部品を2つ選んでから押す」だけで、どちらが土台で
//! どちらが相手かは **選んだ順** に隠れていた。押した瞬間に文書が変わり、下見も無かった。
//!
//! ここでは 足す/引くを押す → 土台待ち → 3D で押すと土台に入り、自動で相手待ちへ →
//! 相手を押す → 下見 → Enter で確定(1つの取り消し単位)。
//! 土台・相手はそれぞれの欄に名前が出て、「ここへ選ぶ」で選び直せ、「解除」で空にできる。
//! 3D で入っているものを押し直すと外れる。
//!
//! 何がどの欄に入るかは、ここが決める。画面は映して押すだけ。

#include "kachakacha/base/Ids.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class BooleanSlot {
    Target,   //!< 土台。足される/引かれる側
    Tool,     //!< 相手。足す/引く側
};

struct BooleanInputState {
    //! 偽なら足す、真なら引く。
    bool cut = false;
    //! 空なら Nil。
    base::EntityId target;
    base::EntityId tool;
    //! 「ここへ選ぶ」で明示した欄。無ければ空いている欄へ順に入る。
    std::optional<BooleanSlot> activeSlot;
};

[[nodiscard]] std::string_view BooleanSlotKey(BooleanSlot slot) noexcept;      //!< TARGET / TOOL
[[nodiscard]] std::string_view BooleanSlotNameJa(BooleanSlot slot) noexcept;   //!< 土台 / 相手
[[nodiscard]] std::string_view BooleanOperationLabelJa(bool cut) noexcept;     //!< 足す / 引く

//! 次の 3D クリックが入る欄。明示した欄があればそれ、無ければ土台 → 相手の順に空いている欄。
//! 両方入っていれば相手(押し直しは相手を入れ替える)。
[[nodiscard]] BooleanSlot NextBooleanSlot(const BooleanInputState& state) noexcept;

//! 3D で部品を押した。入っているものを押せば外れる。空いている(または明示した)欄へ入れる。
//! 入れたら明示は解ける(その欄は満たされた)。同じものを両方の欄には入れない。
[[nodiscard]] BooleanInputState WithBooleanPick(const BooleanInputState& state,
    const base::EntityId& id);

//! 3D の選択から外れた(Ctrl+クリック等)。入っている欄を空にする。
[[nodiscard]] BooleanInputState WithoutBooleanEntries(const BooleanInputState& state,
    const std::vector<base::EntityId>& ids);

//! 「解除」。欄を空にし、次のクリックがその欄へ入るようにする。
[[nodiscard]] BooleanInputState WithBooleanSlotCleared(const BooleanInputState& state,
    BooleanSlot slot);

//! 「ここへ選ぶ」。次のクリックがその欄へ入る(入っていれば入れ替え)。
[[nodiscard]] BooleanInputState WithActiveBooleanSlot(const BooleanInputState& state,
    BooleanSlot slot);

//! 入っているものの並び(土台、相手の順)。3D の選択の印と同じにする。
[[nodiscard]] std::vector<base::EntityId> BooleanEntries(const BooleanInputState& state);

//! 両方入っていて、別のものか。
[[nodiscard]] bool BooleanReady(const BooleanInputState& state) noexcept;

//! 「次のクリック → 土台」のような、いま何を待っているかの一行。
[[nodiscard]] std::string BooleanHintJa(const BooleanInputState& state);

//! 実際に足し引きをした結果の要約。下見と確定は同じものを使う。
struct BooleanPreviewOutcome {
    bool evaluated = false;
    bool available = false;
    double previousVolumeMm3 = 0.0;
    double volumeMm3 = 0.0;
    std::string refusalJa;
};

//! 棚の「状態」に出す行。
[[nodiscard]] std::vector<std::string> BooleanStatusLinesJa(const BooleanInputState& state,
    const BooleanPreviewOutcome& outcome, bool previewShown);

//! 一番下の一行。「引く: TARGET=部品1 / TOOL=(なし) / NEXT=TOOL / no preview」
[[nodiscard]] std::string BooleanFooterLine(const BooleanInputState& state,
    const std::string& targetName, const std::string& toolName,
    const BooleanPreviewOutcome& outcome, bool previewShown);

} // namespace kachakacha::v2::app
