// 「足す・引く」の入力(app/BooleanInputState.h、引継ぎ 2026-09-17 の 4)。
#include "kachakacha/app/BooleanInputState.h"
#include "kachakacha/base/TestHarness.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using kachakacha::v2::app::BooleanEntries;
using kachakacha::v2::app::BooleanFooterLine;
using kachakacha::v2::app::BooleanHintJa;
using kachakacha::v2::app::BooleanInputState;
using kachakacha::v2::app::BooleanPreviewOutcome;
using kachakacha::v2::app::BooleanReady;
using kachakacha::v2::app::BooleanSlot;
using kachakacha::v2::app::BooleanStatusLinesJa;
using kachakacha::v2::app::NextBooleanSlot;
using kachakacha::v2::app::WithActiveBooleanSlot;
using kachakacha::v2::app::WithBooleanPick;
using kachakacha::v2::app::WithBooleanSlotCleared;
using kachakacha::v2::app::WithoutBooleanEntries;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(std::uint8_t value)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

} // namespace

KACHA_V2_TEST(boolean_input, 土台待ちから始まり_入れると相手待ちへ自動で移る)
{
    BooleanInputState state;
    Require(NextBooleanSlot(state) == BooleanSlot::Target, "最初は土台待ち");
    Require(BooleanHintJa(state) == "次のクリック → 土台", "案内は土台");
    state = WithBooleanPick(state, Id(1));
    Require(state.target == Id(1) && state.tools.empty(), "土台に入る");
    Require(NextBooleanSlot(state) == BooleanSlot::Tool, "自動で相手待ちへ");
    Require(BooleanHintJa(state) == "次のクリック → 相手", "案内は相手");
    Require(!BooleanReady(state), "まだ作れない");
    state = WithBooleanPick(state, Id(2));
    Require(state.tools.size() == 1 && state.tools[0] == Id(2) && BooleanReady(state),
        "相手に入って作れる");
    Require(BooleanHintJa(state).find("Enter") != std::string::npos, "案内は Enter");
    Require(BooleanEntries(state).size() == 2 && BooleanEntries(state)[0] == Id(1),
        "並びは土台、相手の順");
}

KACHA_V2_TEST(boolean_input, 入っているものを押し直すと外れ_同じものは両方に入らない)
{
    BooleanInputState state;
    state = WithBooleanPick(state, Id(1));
    state = WithBooleanPick(state, Id(2));
    state = WithBooleanPick(state, Id(1));
    Require(state.target.IsNil() && state.tools.size() == 1 && state.tools[0] == Id(2),
        "土台を押し直すと土台だけ外れる");
    Require(NextBooleanSlot(state) == BooleanSlot::Target, "次は空いた土台へ");
    state = WithBooleanPick(state, Id(2));
    Require(state.tools.empty() && state.target.IsNil(), "相手を押し直すと相手が外れる");
    state = WithBooleanPick(state, Id(3));
    state = WithBooleanPick(state, Id(3));
    Require(state.target.IsNil() && state.tools.empty(), "同じものは両方には入らず、外れる");
}

KACHA_V2_TEST(boolean_input, ここへ選ぶと解除で選び直せる)
{
    BooleanInputState state;
    state = WithBooleanPick(state, Id(1));
    state = WithBooleanPick(state, Id(2));
    state = WithActiveBooleanSlot(state, BooleanSlot::Target);
    Require(NextBooleanSlot(state) == BooleanSlot::Target, "明示した欄が次");
    Require(BooleanHintJa(state) == "次のクリック → 土台", "両方入っていても案内は明示した欄");
    state = WithBooleanPick(state, Id(3));
    Require(state.target == Id(3) && state.tools.size() == 1 && state.tools[0] == Id(2),
        "土台が入れ替わる");
    Require(!state.activeSlot.has_value(), "満たされたら明示は解ける");
    state = WithBooleanSlotCleared(state, BooleanSlot::Tool);
    Require(state.tools.empty() && NextBooleanSlot(state) == BooleanSlot::Tool,
        "解除した欄が次のクリックを受ける");
    state = WithoutBooleanEntries(state, {Id(3)});
    Require(state.target.IsNil(), "3D の選択から外れた分は欄からも消える");
}

KACHA_V2_TEST(boolean_input, 相手は何個でも足せて押し直した分だけ外れる)
{
    BooleanInputState state;
    state = WithBooleanPick(state, Id(1));
    state = WithBooleanPick(state, Id(2));
    state = WithBooleanPick(state, Id(3));
    state = WithBooleanPick(state, Id(4));
    Require(state.target == Id(1) && state.tools.size() == 3 && state.tools[2] == Id(4),
        "土台のあとの部品は全部相手に足される(相手は 1 個に限らない)");
    Require(BooleanReady(state) && BooleanEntries(state).size() == 4, "4 つとも使う");
    state = WithBooleanPick(state, Id(3));
    Require(state.tools.size() == 2 && state.tools[0] == Id(2) && state.tools[1] == Id(4),
        "押し直した相手だけが外れる");
    state = WithoutBooleanEntries(state, {Id(2)});
    Require(state.tools.size() == 1 && state.tools[0] == Id(4), "3D の選択から外れた分も外れる");
    const auto lines = BooleanStatusLinesJa(WithBooleanPick(state, Id(5)), BooleanPreviewOutcome{},
        false);
    bool sawCount = false;
    for (const auto& line : lines) {
        sawCount = sawCount || line.find("2 個") != std::string::npos;
    }
    Require(sawCount, "相手の数を言う");
}

KACHA_V2_TEST(boolean_input, 状態行と一番下の一行は同じことを言う)
{
    BooleanInputState state;
    state.cut = true;
    BooleanPreviewOutcome none;
    auto lines = BooleanStatusLinesJa(state, none, false);
    Require(!lines.empty() && lines.front() == "▶ 次のクリック → 土台", "先頭は案内");
    auto footer = BooleanFooterLine(state, "", "", none, false);
    Require(footer == "引く: TARGET=(なし) / TOOL=(なし) / NEXT=TARGET / no preview",
        "一行: " + footer);
    state = WithBooleanPick(state, Id(1));
    state = WithBooleanPick(state, Id(2));
    BooleanPreviewOutcome ok;
    ok.evaluated = true;
    ok.available = true;
    ok.previousVolumeMm3 = 1000.0;
    ok.volumeMm3 = 750.5;
    lines = BooleanStatusLinesJa(state, ok, true);
    bool sawVolume = false;
    for (const auto& line : lines) {
        sawVolume = sawVolume || line.find("750.5000 mm3") != std::string::npos;
    }
    Require(sawVolume, "体積の変わり方が出る");
    footer = BooleanFooterLine(state, "部品1", "部品2", ok, true);
    Require(footer == "引く: TARGET=部品1 / TOOL=部品2 / VOLUME=750.5000 mm3 / Preview only",
        "一行: " + footer);
    BooleanPreviewOutcome refused;
    refused.evaluated = true;
    refused.refusalJa = "触れていないので何も変わりません";
    lines = BooleanStatusLinesJa(state, refused, false);
    Require(lines.back().find("触れていない") != std::string::npos, "断る理由が出る");
}

KACHA_V2_TEST_MAIN("boolean_input_tests")
