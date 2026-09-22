// 2段の帯(app/Ribbon.h、正本 3 HTML 2026-09-18)。
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/Ribbon.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/base/TestHarness.h"

#include <set>
#include <string>

using kachakacha::v2::app::AllUiModes;
using kachakacha::v2::app::CommandVisibleInMode;
using kachakacha::v2::app::FindCommand;
using kachakacha::v2::app::FindRibbonTool;
using kachakacha::v2::app::RibbonCategoriesFor;
using kachakacha::v2::app::RibbonHasCommand;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::test::Require;

KACHA_V2_TEST(ribbon, 正本のカテゴリが正本の順に並ぶ)
{
    const auto& drawing = RibbonCategoriesFor(UiMode::Drawing);
    Require(drawing.size() == 8, "作図は8カテゴリ");
    Require(drawing[0].labelJa == "基本作図" && drawing[1].labelJa == "曲線"
            && drawing[2].labelJa == "編集" && drawing[3].labelJa == "変形"
            && drawing[4].labelJa == "作業面" && drawing[5].labelJa == "面作成"
            && drawing[6].labelJa == "注記" && drawing[7].labelJa == "測定",
        "作図の並びは正本どおり");
    const auto& part = RibbonCategoriesFor(UiMode::Part);
    Require(part.size() == 5 && part[0].labelJa == "作成" && part[1].labelJa == "形状編集"
            && part[2].labelJa == "面編集" && part[3].labelJa == "ブール演算"
            && part[4].labelJa == "配置",
        "部品の並びは正本どおり");
    const auto& fabrication = RibbonCategoriesFor(UiMode::Fabrication);
    Require(fabrication.size() == 4 && fabrication[0].labelJa == "近似"
            && fabrication[1].labelJa == "部材編集" && fabrication[2].labelJa == "曲げ・展開"
            && fabrication[3].labelJa == "生成",
        "製作の並びは正本どおり");
}

KACHA_V2_TEST(ribbon, 押せる道具は台帳にあり_そのモードで見える)
{
    // 「押せるが何も起きない」を作らない。押せる道具は台帳の命令で、そのモードで見えるもの。
    for (const UiMode mode : AllUiModes()) {
        for (const auto& category : RibbonCategoriesFor(mode)) {
            Require(!category.tools.empty(), "空のカテゴリは無い");
            for (const auto& tool : category.tools) {
                if (tool.Blocked()) {
                    Require(tool.blockedReasonJa.size() > 6, "断る理由は日本語の文");
                    continue;
                }
                Require(FindCommand(tool.commandId) != nullptr,
                    "台帳にある: " + std::string(tool.commandId));
                Require(CommandVisibleInMode(tool.commandId, mode),
                    "そのモードで見える: " + std::string(tool.commandId));
            }
        }
    }
}

KACHA_V2_TEST(ribbon, 既存の道具を失わない)
{
    // 正本に無い既存の道具は「その他」として同じカテゴリに残す(指示書 inventory_first)。
    for (const char* id : {"wire.corner_chamfer", "wire.project", "wire.array_linear",
             "wire.set_datum", "edit.numeric", "grid.edit", "guide.revolve"}) {
        Require(RibbonHasCommand(UiMode::Drawing, id), std::string("作図に残る: ") + id);
    }
    for (const char* id : {"part.thicken_to_plane", "part.from_wire_cage", "part.surface_jig",
             "derived.freeze", "part.boolean_cut"}) {
        Require(RibbonHasCommand(UiMode::Part, id), std::string("部品に残る: ") + id);
    }
    for (const char* id : {"fabrication.assign_role", "fabrication.set_connection_scope",
             "fabrication.set_unfold_base", "fabrication.create_pattern"}) {
        Require(RibbonHasCommand(UiMode::Fabrication, id), std::string("製作に残る: ") + id);
    }
    // 面作成は作り方つきで同じ命令を呼ぶ。8方式が全部ある。
    std::set<int> methods;
    for (const auto& tool : RibbonCategoriesFor(UiMode::Drawing)[5].tools) {
        if (tool.surfaceMethod.has_value()) {
            methods.insert(*tool.surfaceMethod);
        }
    }
    Require(methods.size() == 7 && RibbonHasCommand(UiMode::Drawing, "guide.revolve"),
        "面作成は7方式 + 回転面");
    const auto* planar = FindRibbonTool(UiMode::Drawing, "surface.create");
    Require(planar != nullptr && planar->surfaceMethod == 0, "先頭は平面");
}

KACHA_V2_TEST(ribbon, 正本にあって核に無いものは理由つきで押せない形)
{
    // まだ作れない道具は、名前ごとに「押せない形」で残っていること(黙って消さない)。
    std::set<std::string> blocked;
    for (const UiMode mode : AllUiModes()) {
        for (const auto& category : RibbonCategoriesFor(mode)) {
            for (const auto& tool : category.tools) {
                if (tool.Blocked()) {
                    blocked.insert(std::string(tool.labelJa));
                }
            }
        }
    }
    for (const char* label : {"楕円", "テキスト", "面オフセット", "面削除", "面置換", "表裏反転"}) {
        Require(blocked.count(label) == 1, std::string("まだ作れないので押せない形: ") + label);
    }
    Require(blocked.size() == 6, "押せない道具は 6 個(作れるようになったものは本物の道具へ)");
    // 面積は測定の棚を「面積」で開く(C-15)。
    bool areaFound = false;
    for (const auto& category : RibbonCategoriesFor(UiMode::Drawing)) {
        for (const auto& tool : category.tools) {
            if (tool.labelJa == std::string_view("面積")) {
                areaFound = !tool.Blocked() && tool.measureMode.has_value() && *tool.measureMode == 4;
            }
        }
    }
    Require(areaFound, "作図の測定で面積が押せ、測定の棚を面積で開く");
    const auto* scale = FindRibbonTool(UiMode::Drawing, "wire.scale");
    Require(scale != nullptr && !scale->Blocked(), "作図の変形でスケールが押せる(D-22)");
    // 作れるようになったものは本物の命令を呼ぶ(P-08/P-09: 回転体・ロフト立体・スイープ、
    // P-17: 交差、P-18: 部品の配置)。
    for (const char* id : {"part.fillet", "part.chamfer", "part.shell", "part.split",
             "part.revolve", "part.loft_solid",
             "part.sweep", "part.boolean_intersect",
             "part.move", "part.rotate", "part.mirror", "part.copy"}) {
        const auto* tool = FindRibbonTool(UiMode::Part, id);
        Require(tool != nullptr && !tool->Blocked(), std::string("部品の帯で押せる: ") + id);
    }
}

KACHA_V2_TEST_MAIN("ribbon_tests")
