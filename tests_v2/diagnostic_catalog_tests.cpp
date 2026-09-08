// 診断コードの台帳が、ソースと食い違っていないことを機械で確かめる(AT-ARC-004)。
//
// V1 は画面側で日本語の本文を見て分岐していた。文言を直すたびに挙動が変わり、
// 直したことに気づけなかった。V2 では code だけで識別し、
// その code の一覧をここで守る。
#include "kachakacha/base/SourceScan.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using kachakacha::v2::base::CollectSourceFiles;
using kachakacha::v2::base::SourceFile;
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

[[nodiscard]] std::vector<SourceFile> V2Sources()
{
    std::vector<SourceFile> files = CollectSourceFiles(RepoRoot() / "src" / "next");
    const std::vector<SourceFile> kernel =
        CollectSourceFiles(RepoRoot() / "src" / "next_occt");
    files.insert(files.end(), kernel.begin(), kernel.end());
    return files;
}

//! "XXX-000" の形の文字列リテラルを拾う。
//! 前後が引用符であることまで見て、説明文の中の似た並びを拾わない。
[[nodiscard]] bool LooksLikeCode(const std::string& text)
{
    const std::size_t dash = text.find('-');
    if (dash == std::string::npos || dash < 2 || dash > 5) {
        return false;
    }
    for (std::size_t index = 0; index < dash; ++index) {
        const char character = text[index];
        const bool upper = character >= 'A' && character <= 'Z';
        const bool digit = character >= '0' && character <= '9';
        if (!upper && !digit) {
            return false;
        }
    }
    std::size_t at = dash + 1;
    if (at < text.size() && text[at] >= 'A' && text[at] <= 'Z') {
        ++at;
    }
    std::size_t digits = 0;
    while (at < text.size()) {
        if (text[at] < '0' || text[at] > '9') {
            return false;
        }
        ++at;
        ++digits;
    }
    return digits == 3 || digits == 4;
}

//! ソースに出てくる診断コードを全部集める。
[[nodiscard]] std::set<std::string> CodesInSource()
{
    std::set<std::string> codes;
    for (const SourceFile& file : V2Sources()) {
        for (const std::string& line : file.lines) {
            std::size_t index = 0;
            while (true) {
                const std::size_t open = line.find('"', index);
                if (open == std::string::npos) {
                    break;
                }
                const std::size_t close = line.find('"', open + 1);
                if (close == std::string::npos) {
                    break;
                }
                const std::string inner = line.substr(open + 1, close - open - 1);
                if (LooksLikeCode(inner)) {
                    codes.insert(inner);
                }
                index = close + 1;
            }
        }
    }
    return codes;
}

//! そのコードが、どの重さで作られているか。MakeError / MakeWarning / MakeInformation
//! のどれで作られたかを、コードの直前の語で見る。
//! 台帳の「種別」と、実際に作っている重さが食い違うのを防ぐ。
//! これが無いと、台帳に「情報」と書いてあるのに画面へ赤で出る、ということが起きる。
[[nodiscard]] std::map<std::string, std::set<std::string>> SeveritiesInSource()
{
    static const std::vector<std::pair<std::string, std::string>> kMakers = {
        {"MakeError(", "エラー"}, {"MakeWarning(", "警告"}, {"MakeInformation(", "情報"}};
    std::map<std::string, std::set<std::string>> found;
    for (const SourceFile& file : V2Sources()) {
        for (std::size_t at = 0; at < file.lines.size(); ++at) {
            // 呼び出しは改行をまたぐので、その行と次の行をつないで見る。
            std::string window = file.lines[at];
            if (at + 1 < file.lines.size()) {
                window += " " + file.lines[at + 1];
            }
            for (const auto& maker : kMakers) {
                std::size_t index = 0;
                while (true) {
                    const std::size_t call = window.find(maker.first, index);
                    if (call == std::string::npos) {
                        break;
                    }
                    const std::size_t open = window.find('"', call);
                    const std::size_t close = open == std::string::npos
                        ? std::string::npos
                        : window.find('"', open + 1);
                    if (close != std::string::npos) {
                        const std::string inner =
                            window.substr(open + 1, close - open - 1);
                        if (LooksLikeCode(inner)) {
                            found[inner].insert(maker.second);
                        }
                    }
                    index = call + maker.first.size();
                }
            }
        }
    }
    return found;
}

struct CatalogRow {
    std::string code;
    std::string severity;
    std::string summary;
};

//! 台帳の表を読む。| コード | 種別 | 一文 | の並び。
[[nodiscard]] std::vector<CatalogRow> ReadCatalog()
{
    const std::string text = ReadFile(RepoRoot() / "docs" / "v2" / "diagnostic-catalog.md");
    std::vector<CatalogRow> rows;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < 5 || line.front() != '|') {
            continue;
        }
        std::vector<std::string> cells;
        std::size_t start = 1;
        while (start <= line.size()) {
            const std::size_t bar = line.find('|', start);
            if (bar == std::string::npos) {
                break;
            }
            std::string cell = line.substr(start, bar - start);
            const std::size_t first = cell.find_first_not_of(" \t");
            const std::size_t last = cell.find_last_not_of(" \t");
            cell = first == std::string::npos ? std::string()
                                              : cell.substr(first, last - first + 1);
            cells.push_back(cell);
            start = bar + 1;
        }
        if (cells.size() < 3) {
            continue;
        }
        if (!LooksLikeCode(cells[0])) {
            continue;   // 見出し行と区切り行
        }
        rows.push_back(CatalogRow{cells[0], cells[1], cells[2]});
    }
    return rows;
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

KACHA_V2_TEST(diagnostics, 台帳に重複したコードが無い)
{
    const std::vector<CatalogRow> rows = ReadCatalog();
    Require(!rows.empty(), "台帳が読めること");
    std::set<std::string> seen;
    for (const CatalogRow& row : rows) {
        Require(seen.insert(row.code).second, "重複: " + row.code);
    }
    RequireEqual(std::to_string(seen.size()), std::to_string(rows.size()), "行数");
}

KACHA_V2_TEST(diagnostics, ソースのコードはすべて台帳にある)
{
    const std::set<std::string> source = CodesInSource();
    Require(!source.empty(), "ソースからコードが拾えること");
    std::set<std::string> catalog;
    for (const CatalogRow& row : ReadCatalog()) {
        catalog.insert(row.code);
    }
    std::set<std::string> missing;
    for (const std::string& code : source) {
        if (catalog.count(code) == 0) {
            missing.insert(code);
        }
    }
    Require(missing.empty(), "台帳に無いコードを使っている: " + Joined(missing));
}

KACHA_V2_TEST(diagnostics, 台帳のコードはすべてソースに出てくる)
{
    const std::set<std::string> source = CodesInSource();
    std::set<std::string> dead;
    for (const CatalogRow& row : ReadCatalog()) {
        if (row.severity == "未実装") {
            continue;   // 契約にあるが、まだ出すところが無いもの
        }
        if (source.count(row.code) == 0) {
            dead.insert(row.code);
        }
    }
    Require(dead.empty(), "どこからも出ないコードが台帳に残っている: " + Joined(dead));
}

KACHA_V2_TEST(diagnostics, 未実装と書いたコードは本当にどこからも出ない)
{
    // 実装したのに 未実装 のままにしておくと、残りが見えなくなる。
    const std::set<std::string> source = CodesInSource();
    std::set<std::string> stale;
    for (const CatalogRow& row : ReadCatalog()) {
        if (row.severity == "未実装" && source.count(row.code) > 0) {
            stale.insert(row.code);
        }
    }
    Require(stale.empty(),
        "実装したのに 未実装 のままになっている: " + Joined(stale));
}

KACHA_V2_TEST(diagnostics, 未実装の行にはどの作業で入れるかが書いてある)
{
    for (const CatalogRow& row : ReadCatalog()) {
        if (row.severity != "未実装") {
            continue;
        }
        Require(row.summary.find("WP-") != std::string::npos,
            row.code + ": どの作業で入れるかが書いていない");
    }
}

KACHA_V2_TEST(diagnostics, どの行にも日本語の一文がある)
{
    for (const CatalogRow& row : ReadCatalog()) {
        Require(!row.summary.empty(), row.code + ": 一文が空");
        bool hasJapanese = false;
        for (const char character : row.summary) {
            if (static_cast<unsigned char>(character) > 127) {
                hasJapanese = true;
                break;
            }
        }
        Require(hasJapanese, row.code + ": 日本語が入っていない");
    }
}

KACHA_V2_TEST(diagnostics, 台帳の種別が実際に作っている重さと合っている)
{
    // 台帳に「情報」と書いてあるのに MakeError で作っていたら落とす。
    // 画面の色と、台帳の説明が食い違うのを防ぐ。
    const std::map<std::string, std::set<std::string>> actual = SeveritiesInSource();
    std::vector<std::string> offenders;
    for (const CatalogRow& row : ReadCatalog()) {
        if (row.severity == "未実装") {
            continue;
        }
        const auto found = actual.find(row.code);
        if (found == actual.end()) {
            continue;   // 別の道(コピーや転記)で作られているものは、ここでは見ない。
        }
        for (const std::string& made : found->second) {
            if (row.severity.find(made) == std::string::npos) {
                offenders.push_back(row.code + " は台帳が「" + row.severity
                    + "」だがソースは「" + made + "」で作っている");
            }
        }
    }
    std::string report;
    for (const std::string& line : offenders) {
        report += line + " / ";
    }
    Require(offenders.empty(), "台帳の種別とソースが合う: " + report);
}

KACHA_V2_TEST(diagnostics, その走査が仕込んだ食い違いを見つける)
{
    // 走査そのものが効いているかを、その場の文字列で確かめる。
    Require(LooksLikeCode("PER-001"), "コードだと分かる");
    Require(!LooksLikeCode("2026-09-08"), "日付はコードではない");
    const std::map<std::string, std::set<std::string>> actual = SeveritiesInSource();
    Require(!actual.empty(), "ソースから重さを拾えている");
    // 実在するコードの重さが取れていること。
    // 定数へ入れてから使うコードは拾えない(拾えないものは、この試験では見ない)。
    const auto found = actual.find("PER-002");
    Require(found != actual.end(), "PER-002 の重さが取れる");
    Require(found->second.count("エラー") == 1, "エラーで作っている");
    Require(actual.size() > 40, "多くのコードで重さが取れている: "
        + std::to_string(actual.size()));
}

KACHA_V2_TEST(diagnostics, 種別はエラーか警告か情報のどれか)
{
    for (const CatalogRow& row : ReadCatalog()) {
        const bool ok = row.severity == "エラー" || row.severity == "警告"
            || row.severity == "情報" || row.severity == "エラー / 警告"
            || row.severity == "エラー / 情報" || row.severity == "警告 / 情報"
            || row.severity == "エラー / 警告 / 情報" || row.severity == "未実装";
        Require(ok, row.code + ": 知らない種別 \"" + row.severity + "\"");
    }
}

KACHA_V2_TEST(diagnostics, コードは空でない)
{
    for (const SourceFile& file : V2Sources()) {
        for (std::size_t index = 0; index < file.lines.size(); ++index) {
            const std::string& line = file.lines[index];
            const bool emitsEmpty = line.find("MakeError(\"\"") != std::string::npos
                || line.find("MakeWarning(\"\"") != std::string::npos
                || line.find("MakeInformation(\"\"") != std::string::npos;
            Require(!emitsEmpty, file.path.string() + ":" + std::to_string(index + 1)
                    + " コードが空の診断がある");
        }
    }
}

KACHA_V2_TEST(diagnostics, 日本語の本文で分岐していない)
{
    // 本文は直す。コードは直さない。だから分岐はコードで書く。
    for (const SourceFile& file : V2Sources()) {
        for (std::size_t index = 0; index < file.lines.size(); ++index) {
            const std::string& line = file.lines[index];
            const bool comparesText = line.find("summaryJa ==") != std::string::npos
                || line.find("summaryJa !=") != std::string::npos
                || line.find("detailsJa ==") != std::string::npos
                || line.find("detailsJa !=") != std::string::npos
                || line.find("summaryJa.find(") != std::string::npos
                || line.find("detailsJa.find(") != std::string::npos;
            Require(!comparesText, file.path.string() + ":" + std::to_string(index + 1)
                    + " 日本語の本文で分岐している");
        }
    }
}

KACHA_V2_TEST(diagnostics, 契約が定めたコードが台帳にそろっている)
{
    // 各契約書が列挙しているコードは、実装が無くても台帳から消してはならない。
    // 消すと「まだ出来ていない」ことが見えなくなる。
    const std::vector<std::string> contracts{
        "geometry-contract.md",
        "fabrication-contract.md",
    };
    std::set<std::string> catalog;
    for (const CatalogRow& row : ReadCatalog()) {
        catalog.insert(row.code);
    }
    std::set<std::string> missing;
    for (const std::string& name : contracts) {
        const std::string text = ReadFile(RepoRoot() / "docs" / "v2" / name);
        std::size_t index = 0;
        while (true) {
            const std::size_t open = text.find('`', index);
            if (open == std::string::npos) {
                break;
            }
            const std::size_t close = text.find('`', open + 1);
            if (close == std::string::npos) {
                break;
            }
            std::string inner = text.substr(open + 1, close - open - 1);
            const std::size_t space = inner.find(' ');
            if (space != std::string::npos) {
                inner = inner.substr(0, space);
            }
            // 要件ID(FAB-001、MEA-001 など)は診断コードではない。
            // 診断コードは、区切りのあとに種別の英字が1文字入るか、
            // 押し出しの EXT- 系である。
            const std::size_t dash = inner.find('-');
            const bool hasKindLetter = dash != std::string::npos
                && dash + 1 < inner.size() && inner[dash + 1] >= 'A'
                && inner[dash + 1] <= 'Z';
            const bool isExtrude = inner.rfind("EXT-", 0) == 0;
            if (LooksLikeCode(inner) && (hasKindLetter || isExtrude)
                && catalog.count(inner) == 0) {
                missing.insert(inner);
            }
            index = close + 1;
        }
    }
    Require(missing.empty(),
        "契約書にあるのに台帳に無いコード(未実装なら台帳へ 未実装 として載せること): "
            + Joined(missing));
}

KACHA_V2_TEST(diagnostics, 台帳のコード数がソースと一致する)
{
    const std::set<std::string> source = CodesInSource();
    std::set<std::string> catalog;
    for (const CatalogRow& row : ReadCatalog()) {
        if (row.severity == "未実装") {
            continue;
        }
        catalog.insert(row.code);
    }
    RequireEqual(std::to_string(catalog.size()), std::to_string(source.size()),
        "台帳とソースのコード数");
}

KACHA_V2_TEST_MAIN("diagnostic_catalog_tests")
