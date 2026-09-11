// 上位モード(AT-UIX-001)。
#include "kachakacha/app/UiMode.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>

using kachakacha::v2::app::AllUiModes;
using kachakacha::v2::app::CommandCatalog;
using kachakacha::v2::app::CommandIdsForMode;
using kachakacha::v2::app::TopBarCommandIdsForMode;
using kachakacha::v2::app::CommandVisibleInMode;
using kachakacha::v2::app::CommonCommandIds;
using kachakacha::v2::app::FindCommand;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::app::UiModeName;
using kachakacha::v2::app::UiModeNameJa;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(ui_mode, モードは4つだけ)
{
    RequireEqual(std::to_string(AllUiModes().size()), "4", "モードの数");
}

KACHA_V2_TEST(ui_mode, 面や板材というモードが無い)
{
    // V1 の内部の作りがそのまま出た区分は置かない(PRD §3)。
    for (const UiMode mode : AllUiModes()) {
        const std::string name(UiModeNameJa(mode));
        Require(name != "面", "面というモードは無い");
        Require(name != "板材", "板材というモードは無い");
    }
}

KACHA_V2_TEST(ui_mode, 4つとも名前がある)
{
    for (const UiMode mode : AllUiModes()) {
        Require(UiModeNameJa(mode) != "不明", "日本語名");
        Require(UiModeName(mode) != "unknown", "英語名");
    }
}

KACHA_V2_TEST(ui_mode, 共通操作はどのモードでも出る)
{
    for (const std::string_view id : CommonCommandIds()) {
        for (const UiMode mode : AllUiModes()) {
            Require(CommandVisibleInMode(id, mode),
                std::string(id) + ": どのモードでも出るはず");
        }
    }
}

KACHA_V2_TEST(ui_mode, 共通操作はすべて台帳にある)
{
    for (const std::string_view id : CommonCommandIds()) {
        Require(FindCommand(id) != nullptr, std::string(id) + ": 台帳に無い");
    }
}

KACHA_V2_TEST(ui_mode, モードごとのコマンドもすべて台帳にある)
{
    for (const UiMode mode : AllUiModes()) {
        for (const std::string_view id : CommandIdsForMode(mode)) {
            Require(FindCommand(id) != nullptr, std::string(id) + ": 台帳に無い");
        }
    }
}

KACHA_V2_TEST(ui_mode, 台帳の全コマンドがどこかのモードに出る)
{
    // 台帳にあるのに、どのモードからも押せないコマンドがあってはならない。
    for (const auto& command : CommandCatalog()) {
        bool visible = false;
        for (const UiMode mode : AllUiModes()) {
            if (CommandVisibleInMode(command.id, mode)) {
                visible = true;
                break;
            }
        }
        Require(visible, std::string(command.id) + ": どのモードにも出ない");
    }
}

KACHA_V2_TEST(ui_mode, 同じコマンドが2つのモードに重複して並ばない)
{
    // 共通操作を除き、モード固有のコマンドは1つのモードにだけ属する。
    std::set<std::string> seen;
    for (const UiMode mode : AllUiModes()) {
        for (const std::string_view id : CommandIdsForMode(mode)) {
            Require(seen.insert(std::string(id)).second,
                std::string(id) + ": 2つのモードに出ている");
        }
    }
}

KACHA_V2_TEST(ui_mode, 共通操作とモード固有が重ならない)
{
    std::set<std::string> common;
    for (const std::string_view id : CommonCommandIds()) {
        common.insert(std::string(id));
    }
    for (const UiMode mode : AllUiModes()) {
        for (const std::string_view id : CommandIdsForMode(mode)) {
            Require(common.count(std::string(id)) == 0,
                std::string(id) + ": 共通操作と重なっている");
        }
    }
}

KACHA_V2_TEST(ui_mode, 作図モードに作図の道具がそろっている)
{
    const auto& drawing = CommandIdsForMode(UiMode::Drawing);
    for (const std::string_view id : {"draw.point", "draw.line", "draw.polyline",
             "draw.rectangle", "draw.circle", "draw.arc", "draw.bezier",
             "draw.spline"}) {
        bool found = false;
        for (const std::string_view item : drawing) {
            if (item == id) {
                found = true;
            }
        }
        Require(found, std::string(id) + ": 作図モードに無い");
    }
}

KACHA_V2_TEST(ui_mode, 出力モードに書き出しがそろっている)
{
    for (const std::string_view id : {"export.stl", "export.step", "export.svg",
             "export.dxf", "export.validate"}) {
        Require(CommandVisibleInMode(id, UiMode::Output),
            std::string(id) + ": 出力モードに無い");
        Require(!CommandVisibleInMode(id, UiMode::Drawing),
            std::string(id) + ": 作図モードには出ないはず");
    }
}

KACHA_V2_TEST(ui_mode, 並びが決まっている)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto& first = CommandIdsForMode(UiMode::Part);
        const auto& second = CommandIdsForMode(UiMode::Part);
        RequireEqual(std::to_string(first.size()), std::to_string(second.size()), "件数");
        for (std::size_t index = 0; index < first.size(); ++index) {
            RequireEqual(std::string(first[index]), std::string(second[index]), "並び");
        }
    }
}

KACHA_V2_TEST(ui_mode, 上の帯の命令はモードの命令に入っている)
{
    // 上の帯に別の一覧を持つと、台帳に無いものが並びうる。必ず部分集合にする。
    for (const UiMode mode : AllUiModes()) {
        const auto& all = CommandIdsForMode(mode);
        for (const std::string_view id : TopBarCommandIdsForMode(mode)) {
            const bool found = std::find(all.begin(), all.end(), id) != all.end();
            Require(found, "上の帯の " + std::string(id) + " がモードの台帳に無い");
        }
    }
}

KACHA_V2_TEST(ui_mode, 上の帯は短く保つ)
{
    // 部品モードには 19 個が横一列に並び、何から押すのか読めなかった
    // (オーナー指摘 2026-09-11)。入口だけに絞る。
    for (const UiMode mode : AllUiModes()) {
        const std::size_t count = TopBarCommandIdsForMode(mode).size();
        Require(count >= 1, "1つ以上ある");
        Require(count <= 10, "10個まで");
    }
}

KACHA_V2_TEST(ui_mode, 上の帯に同じものを2度並べない)
{
    for (const UiMode mode : AllUiModes()) {
        const auto& row = TopBarCommandIdsForMode(mode);
        std::set<std::string> unique;
        for (const std::string_view id : row) {
            unique.insert(std::string(id));
        }
        RequireEqual(std::to_string(unique.size()), std::to_string(row.size()), "重なりなし");
    }
}

KACHA_V2_TEST(ui_mode, 上の帯の命令は台帳にある)
{
    for (const UiMode mode : AllUiModes()) {
        for (const std::string_view id : TopBarCommandIdsForMode(mode)) {
            Require(FindCommand(id) != nullptr, "台帳に " + std::string(id) + " がある");
        }
    }
}

KACHA_V2_TEST_MAIN("ui_mode_tests")
