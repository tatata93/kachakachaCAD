// 画面の一番下の一行(app/ToolFooter.h)。UI の正本の footer。
//
// 右の棚の日本語の帯とは役目が違う。いまの入力が一目で全部並ぶ行である。
#include "kachakacha/app/ToolFooter.h"
#include "kachakacha/base/TestHarness.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using kachakacha::v2::app::ExtrudeFooterLine;
using kachakacha::v2::app::ExtrudeInputState;
using kachakacha::v2::app::ExtrudeOutputPreset;
using kachakacha::v2::app::OutputsForPreset;
using kachakacha::v2::app::SurfaceFooterLine;
using kachakacha::v2::app::SurfaceInputState;
using kachakacha::v2::app::ToolKeyHintJa;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::ExtrudeBooleanMode;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(std::uint8_t value)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

} // namespace

KACHA_V2_TEST(tool_footer, 押し出しの一行は正本の並びで出る)
{
    ExtrudeInputState state;
    state.target = Id(1);
    state.profiles = {Id(2)};
    state.distanceMm = 18.0;
    state.operation = ExtrudeBooleanMode::AddToPart;
    state.outputs = OutputsForPreset(ExtrudeOutputPreset::SolidOnly);
    const auto line = ExtrudeFooterLine(state, "Part001", {"NoseProfile_01"});
    Require(line
            == std::string(
                "押し出し: TARGET=Part001 / PROFILE=NoseProfile_01 / 18.0mm / Add"
                " / OUTPUT=Solid"),
        std::string("正本のとおり: ") + line);
}

KACHA_V2_TEST(tool_footer, 選んでいないところはなしと出る)
{
    // 空欄にすると、**入っているのに出ていない**のか、入っていないのかが
    // 分からない。
    ExtrudeInputState state;
    state.distanceMm = 0.5;
    const auto line = ExtrudeFooterLine(state, "", {});
    Require(line.find("TARGET=(なし)") != std::string::npos, "対象なし");
    Require(line.find("PROFILE=(なし)") != std::string::npos, "輪郭なし");
    Require(line.find("0.5mm") != std::string::npos, "距離");
    Require(line.find("/ New /") != std::string::npos, "新しい部品");
}

KACHA_V2_TEST(tool_footer, 面を作るの一行は方式と本数を出す)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::LoftSections;
    state.sections = {Id(1), Id(2), Id(3)};
    state.guides = {Id(5), Id(6)};
    const auto line = SurfaceFooterLine(state, true);
    Require(line.find("METHOD=Loft") != std::string::npos, "方式");
    Require(line.find("SECTIONS=3") != std::string::npos, "断面3本");
    Require(line.find("GUIDES=2") != std::string::npos, "ガイド2本");
    // **まだ文書に入っていないこと**を、ここでも言う。
    Require(line.find("Preview only") != std::string::npos, "下見だけ");
    Require(SurfaceFooterLine(state, false).find("no preview") != std::string::npos,
        "下見が無ければ、そう言う");
}

KACHA_V2_TEST(tool_footer, 合図の説明がいつでも出ている)
{
    // Ctrl を知らないと操作できない設計は禁止(オーナー指示)。
    const auto hint = ToolKeyHintJa();
    Require(hint.find("Ctrl") != std::string::npos, "Ctrl");
    Require(hint.find("Esc") != std::string::npos, "Esc");
    Require(hint.find("Enter") != std::string::npos, "Enter");
    Require(hint.find("Tab") != std::string::npos, "Tab");
}

KACHA_V2_TEST_MAIN("tool_footer_tests")
