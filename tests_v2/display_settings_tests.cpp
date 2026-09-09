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

KACHA_V2_TEST(display, 3回押すと元へ戻る)
{
    // 押すたびにどこへ行くかが読めないと、目当ての見え方へ戻すのに何度も押す。
    DisplayStage stage = DisplayStage::All;
    stage = NextDisplayStage(stage);
    Require(stage == DisplayStage::NoGrid, "1回目でグリッドが消える");
    stage = NextDisplayStage(stage);
    Require(stage == DisplayStage::NoConstruction, "2回目で補助線も消える");
    stage = NextDisplayStage(stage);
    Require(stage == DisplayStage::All, "3回目で元へ戻る");
}

KACHA_V2_TEST(display, 段ごとに見え方が違う)
{
    const DisplaySettings all = SettingsForStage(DisplayStage::All);
    Require(all.gridVisible && all.constructionVisible, "はじめは両方出る");
    const DisplaySettings noGrid = SettingsForStage(DisplayStage::NoGrid);
    Require(!noGrid.gridVisible && noGrid.constructionVisible, "グリッドだけ消える");
    const DisplaySettings bare = SettingsForStage(DisplayStage::NoConstruction);
    Require(!bare.gridVisible && !bare.constructionVisible, "どちらも消える");
}

KACHA_V2_TEST(display, どの段にも違う一言がある)
{
    // 何が消えたかを言わないと、消えたのか壊れたのか分からない。
    std::set<std::string> seen;
    DisplayStage stage = DisplayStage::All;
    for (int index = 0; index < 3; ++index) {
        const std::string text{DisplayStageNameJa(stage)};
        Require(!text.empty(), "一言がある");
        seen.insert(text);
        stage = NextDisplayStage(stage);
    }
    Require(seen.size() == 3, "段ごとに違う");
}

KACHA_V2_TEST_MAIN("display_settings_tests")
