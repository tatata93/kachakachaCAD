// 取扱説明書(docs/manual/README.md)が、いまの実装と食い違わないことを見る。
//
// 説明書は人が読むものなので、直し忘れても誰も気づかない。
// だから、機械で確かめられるところは全部ここで確かめる。
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/ExportPanel.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/base/TestHarness.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using kachakacha::v2::app::AllowedFormatsFor;
using kachakacha::v2::app::ExportFormat;
using kachakacha::v2::app::ExportFormatNameJa;
using kachakacha::v2::app::ExportTarget;
using kachakacha::v2::app::ExportTargetNameJa;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] std::filesystem::path RepoRoot()
{
    return std::filesystem::path(KACHACAD_V2_REPO_ROOT);
}

[[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "読める: " + path.string());
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string Manual()
{
    return ReadFile(RepoRoot() / "docs/manual/README.md");
}

//! 作り方の手順書。押すボタンと入れる数字を順に書いたもの。
//!
//! 名前に日本語が入っている。Windows で
//! std::filesystem::path("作り方.md") と書くと、UTF-8 の並びを
//! CP932 として読まれて開けない。UTF-8 と分かっている道を通す。
[[nodiscard]] std::string HowTo()
{
    const std::string root = kachakacha::v2::io::FromPath(RepoRoot());
    return ReadFile(kachakacha::v2::io::MakePath(root + "/docs/manual/作り方.md"));
}

//! 説明書が指している図の名前を集める。![...](images/xxx.png) の形。
[[nodiscard]] std::set<std::string> ReferencedImages(const std::string& text)
{
    std::set<std::string> names;
    const std::string prefix = "](images/";
    std::size_t at = 0;
    while ((at = text.find(prefix, at)) != std::string::npos) {
        const std::size_t start = at + prefix.size();
        const std::size_t end = text.find(')', start);
        if (end == std::string::npos) {
            break;
        }
        names.insert(text.substr(start, end - start));
        at = end;
    }
    return names;
}

//! 表の1行を「|」で割る。前後の空白は落とす。
[[nodiscard]] std::vector<std::string> Cells(const std::string& line)
{
    std::vector<std::string> cells;
    std::size_t start = 1;
    while (true) {
        const std::size_t bar = line.find('|', start);
        if (bar == std::string::npos) {
            break;
        }
        std::string cell = line.substr(start, bar - start);
        const std::size_t first = cell.find_first_not_of(" \t\r");
        const std::size_t last = cell.find_last_not_of(" \t\r");
        cells.push_back(first == std::string::npos
                ? std::string()
                : cell.substr(first, last - first + 1));
        start = bar + 1;
    }
    return cells;
}

//! 「STL / STEP」を1つずつに割る。
[[nodiscard]] std::vector<std::string> SplitFormats(const std::string& text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t at = text.find(" / ", start);
        const std::string part = text.substr(start,
            at == std::string::npos ? std::string::npos : at - start);
        if (!part.empty()) {
            parts.push_back(part);
        }
        if (at == std::string::npos) {
            break;
        }
        start = at + 3;
    }
    return parts;
}

} // namespace

KACHA_V2_TEST(manual, 説明書がある)
{
    const std::string text = Manual();
    Require(text.size() > 4000, "中身がある");
    Require(text.find("できないことを、できたことにしません") != std::string::npos,
        "この道具の約束が書いてある");
}

KACHA_V2_TEST(manual, 指している図がすべてある)
{
    const auto names = ReferencedImages(Manual());
    Require(names.size() >= 15, "図が15枚以上ある");
    for (const std::string& name : names) {
        const std::filesystem::path path = RepoRoot() / "docs/manual/images" / name;
        std::error_code code;
        Require(std::filesystem::exists(path, code), "図がある: " + name);
        Require(std::filesystem::file_size(path, code) > 0, "図が空でない: " + name);
    }
}

KACHA_V2_TEST(manual, 置いてある図はすべて使われている)
{
    // 使わない図を置いたままにすると、古い画面が残っていても気づけない。
    const auto names = ReferencedImages(Manual());
    std::error_code code;
    for (const auto& entry :
        std::filesystem::directory_iterator(RepoRoot() / "docs/manual/images", code)) {
        const std::string name = entry.path().filename().string();
        Require(names.count(name) == 1, "説明書から指されている: " + name);
    }
}

KACHA_V2_TEST(manual, 書き出しの表が実装と同じ)
{
    // 説明書の「何を出すか」の表と、ExportFormatAllowed が食い違わないこと。
    const std::string text = Manual();
    std::istringstream stream(text);
    std::string line;
    int checked = 0;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() != '|') {
            continue;
        }
        const auto cells = Cells(line);
        if (cells.size() != 2) {
            continue;
        }
        for (ExportTarget target : {ExportTarget::VisibleParts, ExportTarget::SelectedParts,
                 ExportTarget::SelectedFabricationPanels, ExportTarget::CurrentPattern,
                 ExportTarget::SelectedWires, ExportTarget::Project}) {
            if (cells[0] != std::string(ExportTargetNameJa(target))) {
                continue;
            }
            std::string expected;
            for (ExportFormat format : AllowedFormatsFor(target)) {
                if (!expected.empty()) {
                    expected += " / ";
                }
                expected += std::string(ExportFormatNameJa(format));
            }
            RequireEqual(cells[1], expected,
                "説明書の形式が実装と同じ: " + cells[0]);
            ++checked;
        }
    }
    RequireEqual(std::to_string(checked), std::string("6"), "6つの対象を全部見た");
}

KACHA_V2_TEST(manual, 挙げている番号が台帳にある)
{
    // 説明書が、台帳に無い番号を書いていないこと。
    const std::string manual = Manual();
    const std::string catalog = ReadFile(RepoRoot() / "docs/v2/diagnostic-catalog.md");
    int found = 0;
    for (std::size_t index = 0; index + 8 <= manual.size(); ++index) {
        if (manual.compare(index, 1, "`") != 0) {
            continue;
        }
        const std::size_t end = manual.find('`', index + 1);
        if (end == std::string::npos) {
            continue;
        }
        const std::string token = manual.substr(index + 1, end - index - 1);
        // GEO-W001 のような形だけを見る。
        if (token.size() < 7 || token.size() > 10 || token.find('-') == std::string::npos) {
            continue;
        }
        bool looksLikeCode = true;
        for (const char letter : token) {
            const bool upper = letter >= 'A' && letter <= 'Z';
            const bool digit = letter >= '0' && letter <= '9';
            if (!upper && !digit && letter != '-') {
                looksLikeCode = false;
                break;
            }
        }
        if (!looksLikeCode) {
            continue;
        }
        Require(catalog.find("| " + token + " |") != std::string::npos,
            "台帳にある番号: " + token);
        ++found;
    }
    Require(found >= 8, "番号を8つ以上挙げている");
}

KACHA_V2_TEST(manual, 図を撮る名前をすべて挙げている)
{
    // 図の名前と、置いてある図のファイル名が対応していること。
    const std::string text = Manual();
    for (const std::string& name : ReferencedImages(text)) {
        // v2-<名前>.png の <名前> が、17章の一覧に載っていること。
        const std::string stem = name.substr(3, name.size() - 3 - 4);
        Require(text.find("`" + stem + "`") != std::string::npos,
            "17章に載っている名前: " + stem);
    }
}

KACHA_V2_TEST(manual, 作り方の手順書がある)
{
    const std::string text = HowTo();
    Require(text.size() > 6000, "中身がある");
    // 工程は10章。抜けたら気づけるように、見出しを数える。
    int chapters = 0;
    std::size_t at = 0;
    while ((at = text.find("\n## ", at)) != std::string::npos) {
        ++chapters;
        at += 4;
    }
    Require(chapters >= 10, "章が10以上ある");
}

KACHA_V2_TEST(manual, 手順書が各工程の思想を書いている)
{
    // 「なぜそうするか」を飛ばすと、断られたときに何を直せばよいか分からなくなる。
    const std::string text = HowTo();
    int philosophy = 0;
    std::size_t at = 0;
    while ((at = text.find("### 思想", at)) != std::string::npos) {
        ++philosophy;
        at += 4;
    }
    Require(philosophy >= 7, "思想の節が7つ以上ある");
}

KACHA_V2_TEST(manual, 手順書の書き出しの表が実装と同じ)
{
    // README と同じ検査を手順書にもかける。片方だけ古くなるのを防ぐ。
    const std::string text = HowTo();
    std::istringstream stream(text);
    std::string line;
    int checked = 0;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() != '|') {
            continue;
        }
        const auto cells = Cells(line);
        if (cells.size() != 2) {
            continue;
        }
        for (ExportTarget target : {ExportTarget::VisibleParts, ExportTarget::SelectedParts,
                 ExportTarget::SelectedFabricationPanels, ExportTarget::CurrentPattern,
                 ExportTarget::SelectedWires, ExportTarget::Project}) {
            if (cells[0] != std::string(ExportTargetNameJa(target))) {
                continue;
            }
            std::string expected;
            for (ExportFormat format : AllowedFormatsFor(target)) {
                if (!expected.empty()) {
                    expected += " / ";
                }
                expected += std::string(ExportFormatNameJa(format));
            }
            RequireEqual(cells[1], expected, "手順書の形式が実装と同じ: " + cells[0]);
            ++checked;
        }
    }
    RequireEqual(std::to_string(checked), std::string("6"), "6つの対象を全部見た");
}

KACHA_V2_TEST(manual, 手順書が挙げている番号が台帳にある)
{
    const std::string text = HowTo();
    const std::string catalog = ReadFile(RepoRoot() / "docs/v2/diagnostic-catalog.md");
    int found = 0;
    for (std::size_t index = 0; index + 8 <= text.size(); ++index) {
        if (text.compare(index, 1, "`") != 0) {
            continue;
        }
        const std::size_t end = text.find('`', index + 1);
        if (end == std::string::npos) {
            continue;
        }
        const std::string token = text.substr(index + 1, end - index - 1);
        if (token.size() < 7 || token.size() > 10 || token.find('-') == std::string::npos) {
            continue;
        }
        bool looksLikeCode = true;
        for (const char letter : token) {
            const bool upper = letter >= 'A' && letter <= 'Z';
            const bool digit = letter >= '0' && letter <= '9';
            if (!upper && !digit && letter != '-') {
                looksLikeCode = false;
                break;
            }
        }
        if (!looksLikeCode) {
            continue;
        }
        Require(catalog.find("| " + token + " |") != std::string::npos,
            "台帳にある番号: " + token);
        ++found;
    }
    Require(found >= 10, "番号を10以上挙げている");
}

KACHA_V2_TEST(manual, 手順書がショートカットを台帳どおりに書いている)
{
    // 押すキーが実装と違うと、そのとおりに押しても動かない。
    const std::string text = HowTo();
    const std::pair<const char*, const char*> keys[] = {
        {"file.new", "Ctrl+N"}, {"file.open", "Ctrl+O"}, {"file.save", "Ctrl+S"},
        {"file.save_as", "Ctrl+Shift+S"}, {"edit.undo", "Ctrl+Z"},
        {"edit.redo", "Ctrl+Y"}, {"draw.line", "L"}, {"draw.circle", "C"},
        {"draw.arc", "A"}, {"view.fit_all", "F"}, {"part.extrude", "X"},
    };
    for (const auto& entry : keys) {
        bool foundInCatalog = false;
        for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
            if (command.id != entry.first) {
                continue;
            }
            foundInCatalog = true;
            RequireEqual(std::string(command.defaultShortcut), std::string(entry.second),
                std::string("台帳のショートカット: ") + entry.first);
        }
        Require(foundInCatalog, std::string("台帳にある: ") + entry.first);
        Require(text.find(std::string("`") + entry.second + "`") != std::string::npos,
            std::string("手順書に書いてある: ") + entry.second);
    }
}

KACHA_V2_TEST_MAIN("manual_tests")
