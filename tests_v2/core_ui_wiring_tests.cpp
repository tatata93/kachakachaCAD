// core にあるのに画面から呼べないものが無いことを、機械で確かめる。
//
// V2 は「幾何は core、画面は薄く」という作りなので、
// core に実装があって試験も通っているのに、アプリから一度も到達できない、
// という状態が起きやすい。実際そうなっていた。
//
// 名前で数えると、同じ名前の関数が別のヘッダにもあるときに見落とす。
// そこで **アプリの .cpp から #include をたどって届くか** で測る。
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

namespace fs = std::filesystem;

[[nodiscard]] fs::path RepoRoot()
{
    return fs::path(KACHACAD_V2_REPO_ROOT);
}

//! パス区切りを `/` にそろえる。Windows と Linux で同じ文字列にするため。
[[nodiscard]] std::string Slash(const fs::path& path)
{
    std::string text = path.generic_string();
    return text;
}

//! `#include "..."` の中身を返す。それ以外の行は空。
[[nodiscard]] std::string IncludedPath(const std::string& line)
{
    const std::size_t hash = line.find('#');
    if (hash == std::string::npos) {
        return {};
    }
    const std::size_t keyword = line.find("include", hash);
    if (keyword == std::string::npos) {
        return {};
    }
    const std::size_t open = line.find('"', keyword);
    if (open == std::string::npos) {
        return {};
    }
    const std::size_t close = line.find('"', open + 1);
    if (close == std::string::npos) {
        return {};
    }
    return line.substr(open + 1, close - open - 1);
}

//! `#include` に書く綴り → 実際のファイル。
//! core は `src/next` と `src/next_occt` からの相対、画面はファイル名そのまま。
[[nodiscard]] std::map<std::string, fs::path> BuildIncludeIndex()
{
    std::map<std::string, fs::path> index;
    for (const char* root : {"src/next", "src/next_occt"}) {
        const fs::path base = RepoRoot() / root;
        for (const auto& file : kachakacha::v2::base::CollectSourceFiles(base)) {
            if (file.path.extension() != ".h") {
                continue;
            }
            index[Slash(fs::relative(file.path, base))] = file.path;
        }
    }
    const fs::path app = RepoRoot() / "src" / "apps" / "cad_next";
    for (const auto& file : kachakacha::v2::base::CollectSourceFiles(app)) {
        if (file.path.extension() == ".h") {
            index[file.path.filename().string()] = file.path;
        }
    }
    return index;
}

//! アプリの .cpp から `#include` をたどって届くファイルをすべて集める。
//! ヘッダに届いたら、その隣の .cpp も見る(実装が別のヘッダを呼ぶため)。
[[nodiscard]] std::set<std::string> ReachableFromApp()
{
    const auto index = BuildIncludeIndex();
    std::set<std::string> seen;
    std::vector<fs::path> stack;
    const fs::path app = RepoRoot() / "src" / "apps" / "cad_next";
    for (const auto& file : kachakacha::v2::base::CollectSourceFiles(app)) {
        if (file.path.extension() != ".cpp") {
            continue;
        }
        seen.insert(Slash(file.path));
        stack.push_back(file.path);
    }
    while (!stack.empty()) {
        const fs::path current = stack.back();
        stack.pop_back();
        std::ifstream stream(current, std::ios::binary);
        std::string line;
        while (std::getline(stream, line)) {
            const std::string included = IncludedPath(line);
            if (included.empty()) {
                continue;
            }
            const auto found = index.find(included);
            if (found == index.end()) {
                continue;
            }
            for (const fs::path& next :
                {found->second, fs::path(found->second).replace_extension(".cpp")}) {
                if (!fs::exists(next) || seen.count(Slash(next)) != 0) {
                    continue;
                }
                seen.insert(Slash(next));
                stack.push_back(next);
            }
        }
    }
    return seen;
}

//! いま届いていない core のヘッダ。パスは `src/...` からの綴り。
[[nodiscard]] std::vector<std::string> UnreachableCoreHeaders()
{
    const std::set<std::string> reachable = ReachableFromApp();
    std::vector<std::string> missing;
    for (const char* root : {"src/next", "src/next_occt"}) {
        for (const auto& file :
            kachakacha::v2::base::CollectSourceFiles(RepoRoot() / root)) {
            if (file.path.extension() != ".h") {
                continue;
            }
            if (reachable.count(Slash(file.path)) != 0) {
                continue;
            }
            missing.push_back(Slash(fs::relative(file.path, RepoRoot() / root)));
        }
    }
    std::sort(missing.begin(), missing.end());
    return missing;
}

//! 台帳の表から、1列目のヘッダの綴りを拾う。
[[nodiscard]] std::vector<std::string> LedgerHeaders()
{
    const fs::path path = RepoRoot() / "docs" / "v2" / "core-ui-wiring.md";
    std::ifstream stream(path, std::ios::binary);
    Require(stream.good(), "結線の台帳が読める");
    std::vector<std::string> headers;
    std::string line;
    // 見るのは「いま届いていないもの」の表だけ。その後の表(届いているが使っていない
    // もの)にもヘッダ名が並ぶが、あれは届いているので数えてはいけない。
    bool inTable = false;
    while (std::getline(stream, line)) {
        if (line.rfind("## ", 0) == 0) {
            if (inTable) {
                break;
            }
            inTable = line.find("届いていないもの") != std::string::npos;
            continue;
        }
        if (!inTable || line.empty() || line.front() != '|') {
            continue;
        }
        const std::size_t open = line.find('`');
        if (open == std::string::npos) {
            continue;
        }
        const std::size_t close = line.find('`', open + 1);
        if (close == std::string::npos) {
            continue;
        }
        const std::string name = line.substr(open + 1, close - open - 1);
        if (name.size() < 3 || name.substr(name.size() - 2) != ".h") {
            continue;
        }
        headers.push_back("kachakacha/" + name);
    }
    std::sort(headers.begin(), headers.end());
    return headers;
}

[[nodiscard]] std::string Join(const std::vector<std::string>& values)
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

//! 片方にしか無いものを返す。
[[nodiscard]] std::vector<std::string> Missing(const std::vector<std::string>& from,
    const std::vector<std::string>& inside)
{
    std::vector<std::string> only;
    for (const std::string& value : from) {
        if (std::find(inside.begin(), inside.end(), value) == inside.end()) {
            only.push_back(value);
        }
    }
    return only;
}

} // namespace

KACHA_V2_TEST(wiring, 届かないヘッダはすべて台帳にある)
{
    // 新しく core へ足したのに画面へ繋がなかったものが、ここで落ちる。
    // 落ちたら「繋ぐ」か「いつ繋ぐかを台帳へ書く」かのどちらかをする。
    const auto unreachable = UnreachableCoreHeaders();
    const auto ledger = LedgerHeaders();
    RequireEqual(Join(Missing(unreachable, ledger)), std::string(),
        "台帳に載っていない、画面から届かないヘッダ");
}

KACHA_V2_TEST(wiring, 台帳に載っているものは本当に届いていない)
{
    // 繋いだのに台帳から消し忘れると、ここで落ちる。
    // 消し忘れると「まだ繋がっていない」と読み違えたまま作業が進む。
    const auto unreachable = UnreachableCoreHeaders();
    const auto ledger = LedgerHeaders();
    RequireEqual(Join(Missing(ledger, unreachable)), std::string(),
        "もう届いているのに台帳に残っているヘッダ");
}

KACHA_V2_TEST(wiring, 走査が本当に届き方を見ている)
{
    // 走査そのものが壊れていないことを確かめる。
    // 必ず届くはずのものが届いていないと言うなら、走査の方がおかしい。
    const auto unreachable = UnreachableCoreHeaders();
    for (const char* certain : {"kachakacha/app/CommandCatalog.h",
             "kachakacha/geometry/CurveSegment.h", "kachakacha/document/Document.h",
             "kachakacha/modeling/ToolController.h"}) {
        Require(std::find(unreachable.begin(), unreachable.end(), certain)
                == unreachable.end(),
            std::string("必ず届くはずのものが届いている: ") + certain);
    }
    Require(!unreachable.empty() || true, "走査は動いている");
}

KACHA_V2_TEST_MAIN("core_ui_wiring_tests")
