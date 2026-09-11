// コマンド台帳が仕様と食い違っていないことを機械で確かめる(AT-UIX-011)。
//
// V1 は同じ操作がメニューとボタンで別々に書かれていて、
// 片方だけ直したせいで挙動が食い違った。V2 では入口を1つにし、
// その一覧を仕様と突き合わせる。
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/base/TestHarness.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using kachakacha::v2::app::CommandCatalog;
using kachakacha::v2::app::CommandDescriptor;
using kachakacha::v2::app::CommandMode;
using kachakacha::v2::app::FindCommand;
using kachakacha::v2::app::SelectionPredicate;
using kachakacha::v2::app::SelectionPredicateNameJa;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] std::string ReadCatalogDocument()
{
    const std::filesystem::path path = std::filesystem::path(KACHACAD_V2_REPO_ROOT)
        / "docs" / "v2" / "command-catalog.md";
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "台帳が読める");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

//! 台帳の表から、行頭の `id` を拾う。
[[nodiscard]] std::vector<std::string> IdsInDocument()
{
    const std::string text = ReadCatalogDocument();
    std::vector<std::string> ids;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() < 4 || line.front() != '|') {
            continue;
        }
        const std::size_t open = line.find('`');
        if (open == std::string::npos || open > 3) {
            continue;
        }
        const std::size_t close = line.find('`', open + 1);
        if (close == std::string::npos) {
            continue;
        }
        const std::string id = line.substr(open + 1, close - open - 1);
        if (id.find('.') == std::string::npos) {
            continue;
        }
        bool onlyAllowed = true;
        for (const char character : id) {
            const bool lower = character >= 'a' && character <= 'z';
            // 数字も許す。許していなかったので `export.pdf_1to1` が読み飛ばされ、
            // 台帳にあるのに実装が無いことを、この門が何か月も見逃していた。
            const bool digit = character >= '0' && character <= '9';
            if (!lower && !digit && character != '.' && character != '_') {
                onlyAllowed = false;
                break;
            }
        }
        if (onlyAllowed) {
            ids.push_back(id);
        }
    }
    return ids;
}

[[nodiscard]] std::string Joined(const std::set<std::string>& values)
{
    std::string text;
    for (const std::string& value : values) {
        if (!text.empty()) {
            text += ", ";
        }
        text += value;
    }
    return text;
}

} // namespace

KACHA_V2_TEST(commands, 実装の一覧に重複が無い)
{
    std::set<std::string> seen;
    for (const CommandDescriptor& item : CommandCatalog()) {
        Require(seen.insert(std::string(item.id)).second,
            "重複: " + std::string(item.id));
    }
    RequireEqual(std::to_string(seen.size()),
        std::to_string(CommandCatalog().size()), "件数");
}

KACHA_V2_TEST(commands, 仕様のIDがすべて実装にある)
{
    std::set<std::string> implemented;
    for (const CommandDescriptor& item : CommandCatalog()) {
        implemented.insert(std::string(item.id));
    }
    std::set<std::string> missing;
    for (const std::string& id : IdsInDocument()) {
        if (implemented.count(id) == 0) {
            missing.insert(id);
        }
    }
    Require(missing.empty(), "実装に無いコマンド: " + Joined(missing));
}

KACHA_V2_TEST(commands, 実装のIDがすべて仕様にある)
{
    std::set<std::string> documented;
    for (const std::string& id : IdsInDocument()) {
        documented.insert(id);
    }
    std::set<std::string> extra;
    for (const CommandDescriptor& item : CommandCatalog()) {
        if (documented.count(std::string(item.id)) == 0) {
            extra.insert(std::string(item.id));
        }
    }
    Require(extra.empty(), "仕様に無いコマンド: " + Joined(extra));
}

KACHA_V2_TEST(commands, 件数が仕様と一致する)
{
    RequireEqual(std::to_string(CommandCatalog().size()),
        std::to_string(IdsInDocument().size()), "コマンドの件数");
}

KACHA_V2_TEST(commands, どのコマンドにも日本語の表示名がある)
{
    for (const CommandDescriptor& item : CommandCatalog()) {
        Require(!item.labelJa.empty(), std::string(item.id) + ": 表示名が空");
        bool japanese = false;
        for (const char character : item.labelJa) {
            if (static_cast<unsigned char>(character) > 127) {
                japanese = true;
                break;
            }
        }
        Require(japanese, std::string(item.id) + ": 表示名が日本語でない");
    }
}

KACHA_V2_TEST(commands, どのコマンドにも記号がある)
{
    for (const CommandDescriptor& item : CommandCatalog()) {
        Require(!item.icon.empty(), std::string(item.id) + ": 記号が空");
    }
}

KACHA_V2_TEST(commands, どのコマンドにも操作の案内がある)
{
    for (const CommandDescriptor& item : CommandCatalog()) {
        Require(item.operationGuideJa.size() >= 8,
            std::string(item.id) + ": 案内が短すぎる");
    }
}

KACHA_V2_TEST(commands, どのコマンドにも受入試験が結びついている)
{
    for (const CommandDescriptor& item : CommandCatalog()) {
        Require(!item.acceptanceIds.empty(),
            std::string(item.id) + ": 受入試験が無い");
        for (const std::string_view id : item.acceptanceIds) {
            Require(id.rfind("AT-", 0) == 0,
                std::string(item.id) + ": 受入IDの形が違う " + std::string(id));
            // 「AT-XXX-001から003」の省略記法は、1件ずつ展開して登録すること。
            Require(id.find("から") == std::string_view::npos,
                std::string(item.id) + ": 省略記法のまま登録されている");
        }
    }
}

KACHA_V2_TEST(commands, 受入試験のIDが仕様に実在する)
{
    const std::filesystem::path path = std::filesystem::path(KACHACAD_V2_REPO_ROOT)
        / "docs" / "v2" / "acceptance-tests.md";
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "受入仕様が読める");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string text = buffer.str();
    std::set<std::string> missing;
    for (const CommandDescriptor& item : CommandCatalog()) {
        for (const std::string_view id : item.acceptanceIds) {
            if (text.find(std::string(id)) == std::string::npos) {
                missing.insert(std::string(id));
            }
        }
    }
    Require(missing.empty(), "仕様に無い受入ID: " + Joined(missing));
}

KACHA_V2_TEST(commands, 条件の不成立理由が日本語で書いてある)
{
    for (const CommandDescriptor& item : CommandCatalog()) {
        if (item.predicate == SelectionPredicate::Always) {
            continue;
        }
        Require(!item.predicateFailureJa.empty(),
            std::string(item.id) + ": 使えない理由が空");
    }
}

KACHA_V2_TEST(commands, 視点と表示の操作は文書を変えない)
{
    // camera操作と表示設定を Undo へ積まないこと(command-catalog.md §1)。
    for (const std::string_view id : {"view.fit_all", "view.align_selection",
             "view.align_workplane", "view.display_settings", "snap.toggle",
             "selection.activate",
             "measure.open", "fabrication.set_assembly"}) {
        const CommandDescriptor* item = FindCommand(id);
        Require(item != nullptr, std::string(id) + ": 見つからない");
        Require(!item->changesDocument, std::string(id) + ": 文書を変えてはならない");
    }
}

KACHA_V2_TEST(commands, 作図の操作は文書を変える)
{
    for (const std::string_view id : {"draw.point", "draw.line", "draw.polyline",
             "draw.rectangle", "draw.circle", "draw.arc", "draw.bezier",
             "draw.spline", "part.extrude"}) {
        const CommandDescriptor* item = FindCommand(id);
        Require(item != nullptr, std::string(id) + ": 見つからない");
        Require(item->changesDocument, std::string(id) + ": 文書を変えるはず");
    }
}

KACHA_V2_TEST(commands, ショートカットが重なっていない)
{
    std::set<std::string> used;
    for (const CommandDescriptor& item : CommandCatalog()) {
        if (item.defaultShortcut.empty()) {
            continue;
        }
        Require(used.insert(std::string(item.defaultShortcut)).second,
            "ショートカットが重なっている: " + std::string(item.defaultShortcut)
                + " (" + std::string(item.id) + ")");
    }
}

KACHA_V2_TEST(commands, IDで引ける)
{
    for (const CommandDescriptor& item : CommandCatalog()) {
        const CommandDescriptor* found = FindCommand(item.id);
        Require(found != nullptr, std::string(item.id) + ": 引けない");
        RequireEqual(std::string(found->labelJa), std::string(item.labelJa), "表示名");
    }
    Require(FindCommand("no.such.command") == nullptr, "知らないIDは見つからない");
}

KACHA_V2_TEST(commands, 道具のコマンドは道具として出る)
{
    for (const std::string_view id : {"draw.line", "draw.polyline", "wire.trim",
             "selection.activate"}) {
        const CommandDescriptor* item = FindCommand(id);
        Require(item != nullptr, std::string(id) + ": 見つからない");
        Require(item->mode == CommandMode::Tool,
            std::string(id) + ": 道具であるはず");
    }
}

KACHA_V2_TEST(commands, 条件の名前がすべて言葉になっている)
{
    for (const CommandDescriptor& item : CommandCatalog()) {
        Require(!SelectionPredicateNameJa(item.predicate).empty(),
            std::string(item.id) + ": 条件の言葉が無い");
    }
}

KACHA_V2_TEST(commands, 並びが決まっている)
{
    // 2回呼んでも同じ並びであること。画面の順が呼ぶたびに変わってはならない。
    const auto& first = CommandCatalog();
    const auto& second = CommandCatalog();
    RequireEqual(std::to_string(first.size()), std::to_string(second.size()), "件数");
    for (std::size_t index = 0; index < first.size(); ++index) {
        RequireEqual(std::string(first[index].id), std::string(second[index].id),
            "並び");
    }
}

KACHA_V2_TEST(commands, どのコマンドにもメニューの入口がある)
{
    // 台帳へ足したのにメニューへ足し忘れる、が何度も起きた。
    // 起きても、画面を組み立てられる機械でしか気づけなかった。
    // メニューの並びは V2MainWindow.cpp の1か所にあるので、
    // そこに id が書いてあるかどうかを、ここで先に見る。
    const std::filesystem::path path = std::filesystem::path(KACHACAD_V2_REPO_ROOT)
        / "src" / "apps" / "cad_next" / "V2MainWindow.cpp";
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "画面の組み立てが読める");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    // ファイル全体ではなく、メニューの並びだけを見る。
    // 全体を見ると、道具の割り当て表にも同じ id があるので、
    // メニューから抜け落ちても見つからない。
    const std::string all = buffer.str();
    const std::string begin = "const std::vector<MenuGroup> groups{";
    const std::size_t from = all.find(begin);
    Require(from != std::string::npos, "メニューの並びが見つかる");
    const std::size_t to = all.find("\n    };", from);
    Require(to != std::string::npos, "メニューの並びの終わりが見つかる");
    const std::string text = all.substr(from, to - from);

    std::string missing;
    for (const auto& command : CommandCatalog()) {
        const std::string quoted = "\"" + std::string(command.id) + "\"";
        if (text.find(quoted) != std::string::npos) {
            continue;
        }
        if (!missing.empty()) {
            missing += ", ";
        }
        missing += std::string(command.id);
    }
    RequireEqual(missing, std::string(), "メニューに出ていないコマンド");
}

KACHA_V2_TEST_MAIN("command_catalog_tests")
