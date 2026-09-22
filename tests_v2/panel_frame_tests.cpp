// 道具の棚の共通の枠(C-10、app/PanelFrame.h)。見出しの字から節を決め、並びを確かめる。
#include "kachakacha/app/PanelFrame.h"
#include "kachakacha/base/TestHarness.h"

#include <string>
#include <vector>

using kachakacha::v2::app::PanelSectionKind;
using kachakacha::v2::app::PanelSectionKindOf;
using kachakacha::v2::app::PanelSectionOrderProblemJa;
using kachakacha::v2::test::Require;

KACHA_V2_TEST(panel_frame, 見出しの字から節の種類が決まり番号は読み飛ばす)
{
    Require(PanelSectionKindOf("作り方") == PanelSectionKind::Method, "作り方");
    Require(PanelSectionKindOf("1. 作り方") == PanelSectionKind::Method, "番号つきの作り方");
    Require(PanelSectionKindOf("2. 入力(3D で押すと入り、押し直すと外れます)")
            == PanelSectionKind::Input, "説明つきの入力");
    Require(PanelSectionKindOf("1. 対象") == PanelSectionKind::Input, "対象は入力");
    Require(PanelSectionKindOf("2. 結果") == PanelSectionKind::Settings, "結果は設定");
    Require(PanelSectionKindOf("3. 断面順") == PanelSectionKind::Settings, "断面順は設定");
    Require(PanelSectionKindOf("オプション") == PanelSectionKind::Settings, "オプションは設定");
    Require(PanelSectionKindOf("共通") == PanelSectionKind::Common, "共通");
    Require(PanelSectionKindOf("4. 状態") == PanelSectionKind::State, "状態");
    Require(PanelSectionKindOf("3. 候補") == PanelSectionKind::Other, "候補は並びを決めない");
    Require(PanelSectionKindOf("10. 入力") == PanelSectionKind::Input, "2 桁の番号も読み飛ばす");
}

KACHA_V2_TEST(panel_frame, 正本の並びは通り逆の並びは理由を言って断る)
{
    // 正本(面を作る・部品・作図)の並び。
    Require(PanelSectionOrderProblemJa({"1. 作り方", "2. 入力", "3. 断面順", "4. 状態"}).empty(),
        "面を作るの並び");
    Require(PanelSectionOrderProblemJa({"1. 入力", "2. 結果", "3. 状態"}).empty(), "押し出しの並び");
    Require(PanelSectionOrderProblemJa({"作り方", "入力", "オプション", "共通"}).empty(), "作図の並び");
    Require(PanelSectionOrderProblemJa({"1. 対象", "2. 近似条件", "3. 候補", "4. 表示"}).empty(),
        "製作の並び(候補と表示は並びを決めない)");
    Require(PanelSectionOrderProblemJa({}).empty(), "見出しが無いのは約束の外ではない");
    // 厚みの前の並び(入力 → 作り方)は約束の外。どれが逆かを言う。
    const std::string problem = PanelSectionOrderProblemJa({"1. 入力", "2. 作り方", "3. 厚み", "4. 状態"});
    Require(!problem.empty() && problem.find("2. 作り方") != std::string::npos
            && problem.find("1. 入力") != std::string::npos,
        "入力の後ろの作り方を言う: " + problem);
    Require(!PanelSectionOrderProblemJa({"状態", "入力"}).empty(), "状態は最後");
}

KACHA_V2_TEST_MAIN("panel_frame_tests")
