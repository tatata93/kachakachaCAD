// 「厚み」の入力(app/ThickenInputState.h、指示書 matrix P-10)。
#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/base/TestHarness.h"

#include <array>
#include <cstdint>
#include <string>

using kachakacha::v2::app::ThickenFooterLine;
using kachakacha::v2::app::ThickenInputState;
using kachakacha::v2::app::ThickenPreviewOutcome;
using kachakacha::v2::app::ThickenReadyToBuild;
using kachakacha::v2::app::ThickenStatusLinesJa;
using kachakacha::v2::app::WithoutThickenSurface;
using kachakacha::v2::app::WithThickenPick;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::fabrication::ThicknessPlacement;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(std::uint8_t value)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

} // namespace

KACHA_V2_TEST(thicken_input, 面を押すと入り_押し直すと外れる)
{
    ThickenInputState state;
    Require(state.surface.IsNil(), "最初は空");
    state = WithThickenPick(state, Id(1));
    Require(state.surface == Id(1), "面が入る");
    state = WithThickenPick(state, Id(1));
    Require(state.surface.IsNil(), "押し直すと外れる");
    state = WithThickenPick(state, Id(1));
    state = WithThickenPick(state, Id(2));
    Require(state.surface == Id(2), "別のものを押せば入れ替わる");
    state = WithoutThickenSurface(state, Id(2));
    Require(state.surface.IsNil(), "3D の選択から外れた分は欄からも消える");
    // 選択から外れたのが違うものなら、いま入っているものはそのまま。
    state = WithThickenPick(state, Id(3));
    state = WithoutThickenSurface(state, Id(9));
    Require(state.surface == Id(3), "関係ない外れでは消えない");
}

KACHA_V2_TEST(thicken_input, 面と厚みがそろって初めて作れる)
{
    ThickenInputState state;
    Require(!ThickenReadyToBuild(state), "面が無ければ作れない");
    state.surface = Id(1);
    Require(!ThickenReadyToBuild(state), "厚みが 0 では作れない");
    state.thicknessMm = 0.5;
    Require(ThickenReadyToBuild(state), "面と正の厚みがそろえば作れる");
}

KACHA_V2_TEST(thicken_input, 平面までは相手の作業平面が要る)
{
    ThickenInputState state;
    state.surface = Id(1);
    state.toPlane = true;
    // 「平面まで」は厚みの数を見ない。相手の作業平面だけを見る。
    Require(!ThickenReadyToBuild(state), "相手が無ければ作れない");
    state.targetPlane = Id(2);
    Require(ThickenReadyToBuild(state), "相手が入れば作れる(厚みは 0 のままでよい)");
}

KACHA_V2_TEST(thicken_input, 状態行は入力に合わせて変わる)
{
    ThickenInputState state;
    ThickenPreviewOutcome none;
    auto lines = ThickenStatusLinesJa(state, none, false);
    Require(!lines.empty() && lines.front() == "▶ 次のクリック → 面", "先頭は案内");
    Require(lines.back().find("面を押してください") != std::string::npos, "面が無い理由");

    state.surface = Id(1);
    lines = ThickenStatusLinesJa(state, none, false);
    Require(lines.back().find("厚みは") != std::string::npos, "厚み 0 の理由");

    state.thicknessMm = 0.5;
    ThickenPreviewOutcome ok;
    ok.evaluated = true;
    ok.available = true;
    ok.volumeMm3 = 123.4;
    ok.thicknessMm = 0.5;
    lines = ThickenStatusLinesJa(state, ok, true);
    bool sawVolume = false;
    bool sawPreview = false;
    for (const auto& line : lines) {
        sawVolume = sawVolume || line.find("123.4 mm3") != std::string::npos;
        sawPreview = sawPreview || line.find("下見を表示中") != std::string::npos;
    }
    Require(sawVolume, "体積が出る: " + (lines.empty() ? std::string() : lines.back()));
    Require(sawPreview, "下見の様子が出る");

    ThickenPreviewOutcome refused;
    refused.evaluated = true;
    refused.refusalJa = "厚みが大きすぎます";
    lines = ThickenStatusLinesJa(state, refused, false);
    Require(lines.back().find("厚みが大きすぎます") != std::string::npos, "断る理由が出る");
}

KACHA_V2_TEST(thicken_input, 一番下の一行は仕様どおりの形になる)
{
    ThickenInputState state;
    state.surface = Id(1);
    state.thicknessMm = 2.0;
    state.placement = ThicknessPlacement::Outside;
    ThickenPreviewOutcome none;
    const auto footer = ThickenFooterLine(state, "部品1", "", none, true);
    Require(footer == "厚み: SURFACE=部品1 / 2.0mm / 外側 / Preview only",
        "一行: " + footer);
}

KACHA_V2_TEST(thicken_input, 平面までの一行は相手の平面を出す)
{
    ThickenInputState state;
    state.surface = Id(1);
    state.toPlane = true;
    state.targetPlane = Id(2);
    ThickenPreviewOutcome none;
    const auto footer = ThickenFooterLine(state, "部品1", "作業平面1", none, false);
    Require(footer == "厚み: SURFACE=部品1 / PLANE=作業平面1 / 平面まで / no preview",
        "一行: " + footer);
}

KACHA_V2_TEST_MAIN("thicken_input_state_tests")
