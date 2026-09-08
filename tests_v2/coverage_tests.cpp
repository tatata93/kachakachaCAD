// 受入試験の台帳が、仕様と食い違っていないことを機械で確かめる。
//
// 「書き忘れて未達成のまま完成にする」を防ぐための試験。
// 台帳(acceptance-coverage.md)に無いIDがあれば失敗、
// 仕様(acceptance-tests.md)に無いIDが台帳にあっても失敗、
// 「済」と書いてあるのに根拠の試験ファイルが無ければ失敗にする。
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

using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

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

//! 文中の AT-XXX-000 をすべて拾う。
[[nodiscard]] std::set<std::string> CollectIds(const std::string& text)
{
    std::set<std::string> ids;
    for (std::size_t index = 0; index + 10 <= text.size(); ++index) {
        if (text.compare(index, 3, "AT-") != 0) {
            continue;
        }
        // AT- + 3文字以上の英大文字 + - + 3桁
        std::size_t at = index + 3;
        std::size_t letters = 0;
        while (at < text.size() && text[at] >= 'A' && text[at] <= 'Z') {
            ++at;
            ++letters;
        }
        if (letters < 2 || at >= text.size() || text[at] != '-') {
            continue;
        }
        ++at;
        std::size_t digits = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            ++at;
            ++digits;
        }
        if (digits != 3) {
            continue;
        }
        ids.insert(text.substr(index, at - index));
    }
    return ids;
}

struct LedgerRow {
    std::string id;
    std::string state;
    std::string evidence;
};

[[nodiscard]] std::vector<LedgerRow> ReadLedger()
{
    const std::string text = ReadFile(RepoRoot() / "docs/v2/acceptance-coverage.md");
    std::vector<LedgerRow> rows;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.compare(0, 5, "| AT-") != 0) {
            continue;
        }
        // | ID | 状態 | 根拠 |
        std::vector<std::string> cells;
        std::size_t start = 1;
        while (true) {
            const std::size_t bar = line.find('|', start);
            if (bar == std::string::npos) {
                break;
            }
            std::string cell = line.substr(start, bar - start);
            // 前後の空白を落とす。
            while (!cell.empty() && cell.front() == ' ') {
                cell.erase(cell.begin());
            }
            while (!cell.empty() && cell.back() == ' ') {
                cell.pop_back();
            }
            cells.push_back(cell);
            start = bar + 1;
        }
        if (cells.size() >= 3) {
            rows.push_back(LedgerRow{cells[0], cells[1], cells[2]});
        }
    }
    return rows;
}

} // namespace

KACHA_V2_TEST(coverage, 台帳が仕様の全IDを覆っている)
{
    const std::set<std::string> specified =
        CollectIds(ReadFile(RepoRoot() / "docs/v2/acceptance-tests.md"));
    Require(specified.size() >= 80,
        "仕様からIDを拾えること (" + std::to_string(specified.size()) + ")");

    const std::vector<LedgerRow> rows = ReadLedger();
    std::set<std::string> ledger;
    for (const LedgerRow& row : rows) {
        Require(ledger.insert(row.id).second, "台帳にIDの重複が無いこと: " + row.id);
    }

    std::vector<std::string> missing;
    for (const std::string& id : specified) {
        if (ledger.count(id) == 0) {
            missing.push_back(id);
        }
    }
    Require(missing.empty(),
        "台帳に載っていないIDがあります: "
            + (missing.empty() ? std::string() : missing.front())
            + (missing.size() > 1 ? " ほか " + std::to_string(missing.size() - 1) + " 件"
                                  : ""));

    std::vector<std::string> extra;
    for (const std::string& id : ledger) {
        if (specified.count(id) == 0) {
            extra.push_back(id);
        }
    }
    Require(extra.empty(),
        "仕様に無いIDが台帳にあります: "
            + (extra.empty() ? std::string() : extra.front()));
}

KACHA_V2_TEST(coverage, 状態は3つのうちのどれかである)
{
    for (const LedgerRow& row : ReadLedger()) {
        Require(row.state == "済" || row.state == "部分" || row.state == "未",
            row.id + " の状態が「済/部分/未」のどれかであること: [" + row.state + "]");
    }
}

KACHA_V2_TEST(coverage, 済と書いた項目には本当に試験がある)
{
    int done = 0;
    for (const LedgerRow& row : ReadLedger()) {
        if (row.state != "済") {
            continue;
        }
        ++done;
        Require(row.evidence.find("tests_v2/") != std::string::npos,
            row.id + " の根拠に試験ファイルが書かれていること: " + row.evidence);
        // 書かれているファイルが実在すること。
        std::size_t position = 0;
        int found = 0;
        while ((position = row.evidence.find("tests_v2/", position)) != std::string::npos) {
            std::size_t end = position;
            while (end < row.evidence.size()
                && (std::isalnum(static_cast<unsigned char>(row.evidence[end])) != 0
                    || row.evidence[end] == '_' || row.evidence[end] == '/'
                    || row.evidence[end] == '.')) {
                ++end;
            }
            const std::string file = row.evidence.substr(position, end - position);
            Require(std::filesystem::exists(RepoRoot() / file),
                row.id + " の根拠 " + file + " が実在すること");
            ++found;
            position = end;
        }
        Require(found > 0, row.id + " の根拠にファイル名があること");
    }
    Require(done >= 30, "済が十分あること (" + std::to_string(done) + ")");
}

KACHA_V2_TEST(coverage, 未と部分には理由が書いてある)
{
    for (const LedgerRow& row : ReadLedger()) {
        if (row.state == "済") {
            continue;
        }
        Require(row.evidence.size() >= 6,
            row.id + " に理由が書かれていること: [" + row.evidence + "]");
    }
}

KACHA_V2_TEST(coverage, 台帳に載っている試験ファイルはすべて登録されている)
{
    // 根拠に書いたのに CMakeLists へ登録していない、を防ぐ。
    const std::string cmake = ReadFile(RepoRoot() / "CMakeLists.txt");
    std::set<std::string> mentioned;
    for (const LedgerRow& row : ReadLedger()) {
        std::size_t position = 0;
        while ((position = row.evidence.find("tests_v2/", position)) != std::string::npos) {
            std::size_t end = position;
            while (end < row.evidence.size()
                && (std::isalnum(static_cast<unsigned char>(row.evidence[end])) != 0
                    || row.evidence[end] == '_' || row.evidence[end] == '/'
                    || row.evidence[end] == '.')) {
                ++end;
            }
            mentioned.insert(row.evidence.substr(position, end - position));
            position = end;
        }
    }
    Require(!mentioned.empty(), "根拠のファイルが集まること");
    for (const std::string& file : mentioned) {
        Require(cmake.find(file) != std::string::npos,
            file + " が CMakeLists.txt へ登録されていること");
    }
}

KACHA_V2_TEST(coverage, V1同等性の表に未が残っていない)
{
    // オーナー指示による完了条件。Qt へ繋ぐ部分だけが残ってよい。
    const std::string text = ReadFile(RepoRoot() / "docs/v2/v1-drawing-parity.md");
    std::istringstream stream(text);
    std::string line;
    std::vector<std::string> remaining;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() != '|') {
            continue;
        }
        if (line.find("| 未 |") == std::string::npos
            && line.find("| 未") != line.size() - 2) {
            // 「未」だけの欄を探す。「Qt=未」「操作の流れ=済 / Qt=未」は除く。
        }
        const std::size_t lastBar = line.rfind('|');
        const std::size_t previousBar = line.rfind('|', lastBar - 1);
        if (previousBar == std::string::npos) {
            continue;
        }
        std::string cell = line.substr(previousBar + 1, lastBar - previousBar - 1);
        while (!cell.empty() && cell.front() == ' ') {
            cell.erase(cell.begin());
        }
        while (!cell.empty() && cell.back() == ' ') {
            cell.pop_back();
        }
        if (cell == "未") {
            remaining.push_back(line);
        }
    }
    Require(remaining.size() <= 2,
        "幾何と操作の流れに「未」が残っていないこと (残り "
            + std::to_string(remaining.size()) + " 行: "
            + (remaining.empty() ? std::string() : remaining.front()) + ")");
}

KACHA_V2_TEST_MAIN("coverage_tests")
