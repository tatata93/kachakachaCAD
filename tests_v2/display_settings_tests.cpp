// 見え方の設定(AT-UIX-010)。形は変えない。
#include "kachakacha/app/DisplaySettings.h"
#include "kachakacha/base/TestHarness.h"

#include <set>
#include <string>

using kachakacha::v2::app::DisplaySettings;
using kachakacha::v2::app::DisplayStage;
using kachakacha::v2::app::DisplayStageNameJa;
using kachakacha::v2::app::NextDisplayStage;
using kachakacha::v2::app::SettingsForStage;
using kachakacha::v2::test::Require;

KACHA_V2_TEST(display, 4回押すと元へ戻る)
{
    // 押すたびにどこへ行くかが読めないと、目当ての見え方へ戻すのに何度も押す。
    DisplayStage stage = DisplayStage::All;
    stage = NextDisplayStage(stage);
    Require(stage == DisplayStage::NoGrid, "1回目でグリッドが消える");
    stage = NextDisplayStage(stage);
    Require(stage == DisplayStage::NoConstruction, "2回目で補助線も消える(完成形)");
    stage = NextDisplayStage(stage);
    Require(stage == DisplayStage::SelectionOnly, "3回目で選択だけ");
    stage = NextDisplayStage(stage);
    Require(stage == DisplayStage::All, "4回目で元へ戻る");
}

KACHA_V2_TEST(display, 段ごとに見え方が違う)
{
    const DisplaySettings all = SettingsForStage(DisplayStage::All);
    Require(all.gridVisible && all.constructionVisible && !all.selectionOnly, "はじめは両方出る");
    const DisplaySettings noGrid = SettingsForStage(DisplayStage::NoGrid);
    Require(!noGrid.gridVisible && noGrid.constructionVisible, "グリッドだけ消える");
    const DisplaySettings bare = SettingsForStage(DisplayStage::NoConstruction);
    Require(!bare.gridVisible && !bare.constructionVisible && !bare.selectionOnly, "どちらも消える");
    const DisplaySettings only = SettingsForStage(DisplayStage::SelectionOnly);
    Require(only.selectionOnly && !only.gridVisible, "選んだものだけ");
}

KACHA_V2_TEST(display, 段を当てても太さと様式は残る)
{
    // 段は「何を出すか」だけ。線の太さや様式は人が決めたものなので、段で戻さない。
    DisplaySettings settings;
    settings.wireWidthPx = 3.5;
    settings.wireStyle = kachakacha::v2::app::LineStyle::Dotted;
    settings.dimOffPlaneLines = false;
    settings.gridInAllModes = false;
    const DisplaySettings applied = kachakacha::v2::app::ApplyStage(settings,
        DisplayStage::NoConstruction);
    Require(!applied.gridVisible && !applied.constructionVisible, "段は効く");
    Require(applied.wireWidthPx == 3.5, "太さは残る");
    Require(applied.wireStyle == kachakacha::v2::app::LineStyle::Dotted, "様式は残る");
    Require(!applied.dimOffPlaneLines && !applied.gridInAllModes, "薄くする・グリッドの出し方も残る");
    Require(kachakacha::v2::app::ClampLineWidthPx(50.0) == 12.0, "太すぎる線は 12 に");
    Require(kachakacha::v2::app::ClampLineWidthPx(0.0) == 0.25, "細すぎる線は 0.25 に");
}

KACHA_V2_TEST(display, どの段にも違う一言がある)
{
    // 何が消えたかを言わないと、消えたのか壊れたのか分からない。
    std::set<std::string> seen;
    std::set<std::string> labels;
    DisplayStage stage = DisplayStage::All;
    for (int index = 0; index < 4; ++index) {
        const std::string text{DisplayStageNameJa(stage)};
        Require(!text.empty(), "一言がある");
        seen.insert(text);
        labels.insert(std::string(kachakacha::v2::app::DisplayStageLabelJa(stage)));
        stage = NextDisplayStage(stage);
    }
    Require(seen.size() == 4, "段ごとに違う");
    Require(labels.size() == 4, "短い名前も段ごとに違う");
}

KACHA_V2_TEST_MAIN("display_settings_tests")
