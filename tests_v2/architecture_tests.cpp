// AT-ARC-001 依存境界: V2 core は Qt と OCCT に依存してはならない。
// AT-ARC-005 コード衛生: 行数上限、禁止include、未完了マーカーを機械で見る。
//
// 目視ではなく、ソース木を実際に走査して数える。
// KACHACAD_V2_REPO_ROOT はCMakeが渡す。
#include "kachakacha/base/SourceScan.h"
#include "kachakacha/base/TestHarness.h"

#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using kachakacha::v2::base::CollectSourceFiles;
using kachakacha::v2::base::IsIncludeOf;
using kachakacha::v2::base::LongestFunctionLength;
using kachakacha::v2::base::SourceFile;
using kachakacha::v2::test::Require;

namespace {

//! 1ファイルあたりの行数上限。これを超える前に責務で分ける(実装作業規則 §3.1)。
constexpr int kMaximumFileLines = 1500;
//! 1関数あたりの行数上限(同 §3.1)。
constexpr int kMaximumFunctionLines = 100;

[[nodiscard]] std::filesystem::path RepoRoot()
{
#ifdef KACHACAD_V2_REPO_ROOT
    return std::filesystem::path(KACHACAD_V2_REPO_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] std::string Join(const std::vector<std::string>& items)
{
    std::ostringstream text;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            text << ", ";
        }
        text << items[index];
    }
    return text.str();
}

//! V2 core が include してはならないもの。
const std::vector<std::string> kForbiddenInCore = {
    "<Q", "QtCore", "QtGui", "QtWidgets", "qt/", // Qt
    "TopoDS_", "BRep", "Standard_", "gp_", "Geom_", "TopExp", "TopAbs", "ShapeFix", // OCCT
};

//! すべてのV2コードで、必須経路へ残してはならない語。
const std::vector<std::string> kUnfinishedMarkers = {"TODO", "FIXME", "XXX", "HACK"};

//! Qt と Windows SDK がマクロにしている語。変数名や引数名に使うと、
//! Qt を入れて組んだときだけ壊れる。雲の側は Qt が無いので気づけない。
//! ここで名前として書けないようにしておく。
//!   Qt(qobjectdefs.h): signals / slots / emit / foreach
//!   Windows(windef.h): near / far / min / max / small
const std::vector<std::string> kMacroReservedNames = {
    "signals", "slots", "emit", "foreach", "near", "far", "small"};

//! その語が、識別子として書かれているか。文字列や注釈や別語の一部は数えない。
[[nodiscard]] bool DeclaresIdentifier(const std::string& line, const std::string& name)
{
    const auto isWordChar = [](unsigned char value) {
        return std::isalnum(value) != 0 || value == '_';
    };
    for (std::size_t at = line.find(name); at != std::string::npos;
        at = line.find(name, at + 1)) {
        const std::size_t end = at + name.size();
        if (at > 0 && isWordChar(static_cast<unsigned char>(line[at - 1]))) {
            continue;
        }
        if (end < line.size() && isWordChar(static_cast<unsigned char>(line[end]))) {
            continue;
        }
        // 直後が「宣言や仮引数の終わり」に見える形だけを拾う。
        std::size_t next = end;
        while (next < line.size() && line[next] == ' ') {
            ++next;
        }
        if (next >= line.size()) {
            continue;
        }
        const char following = line[next];
        if (following != '{' && following != '=' && following != ';' && following != ','
            && following != ')' && following != '[') {
            continue;
        }
        // 直前が型らしい語であること(先頭が空白だけの行は宣言ではない)。
        std::size_t before = at;
        while (before > 0 && line[before - 1] == ' ') {
            --before;
        }
        if (before == 0) {
            continue;
        }
        const char previous = line[before - 1];
        if (!isWordChar(static_cast<unsigned char>(previous)) && previous != '>'
            && previous != '*' && previous != '&') {
            continue;
        }
        return true;
    }
    return false;
}

} // namespace

KACHA_V2_TEST(architecture, repo_root_is_reachable)
{
    Require(std::filesystem::exists(RepoRoot() / "CMakeLists.txt"),
        "the repository root passed by CMake contains CMakeLists.txt");
}

KACHA_V2_TEST(architecture, v2_core_has_no_qt_or_occt_include)
{
    const auto files = CollectSourceFiles(RepoRoot() / "src" / "next");
    Require(!files.empty(), "V2 core has source files to inspect");
    std::vector<std::string> offenders;
    for (const SourceFile& file : files) {
        for (const std::string& line : file.lines) {
            for (const std::string& forbidden : kForbiddenInCore) {
                if (IsIncludeOf(line, forbidden)) {
                    offenders.push_back(file.path.filename().string() + ": " + line);
                }
            }
        }
    }
    Require(offenders.empty(),
        "V2 core includes neither Qt nor OCCT: " + Join(offenders));
}

KACHA_V2_TEST(architecture, v2_sources_stay_under_the_file_line_limit)
{
    std::vector<std::string> offenders;
    for (const char* area : {"src/next", "src/next_occt", "src/apps/cad_next", "tests_v2"}) {
        for (const SourceFile& file : CollectSourceFiles(RepoRoot() / area)) {
            if (static_cast<int>(file.lines.size()) > kMaximumFileLines) {
                offenders.push_back(file.path.filename().string() + "="
                    + std::to_string(file.lines.size()));
            }
        }
    }
    Require(offenders.empty(),
        "no V2 source file exceeds 1500 lines: " + Join(offenders));
}

KACHA_V2_TEST(architecture, v2_functions_stay_under_the_line_limit)
{
    std::vector<std::string> offenders;
    for (const char* area : {"src/next", "src/next_occt", "src/apps/cad_next"}) {
        for (const SourceFile& file : CollectSourceFiles(RepoRoot() / area)) {
            const int longest = LongestFunctionLength(file.lines);
            if (longest > kMaximumFunctionLines) {
                offenders.push_back(
                    file.path.filename().string() + "=" + std::to_string(longest));
            }
        }
    }
    Require(offenders.empty(),
        "no V2 function exceeds 100 lines: " + Join(offenders));
}

KACHA_V2_TEST(architecture, v2_sources_carry_no_unfinished_marker)
{
    std::vector<std::string> offenders;
    for (const char* area : {"src/next", "src/next_occt", "src/apps/cad_next"}) {
        for (const SourceFile& file : CollectSourceFiles(RepoRoot() / area)) {
            for (std::size_t index = 0; index < file.lines.size(); ++index) {
                for (const std::string& marker : kUnfinishedMarkers) {
                    if (file.lines[index].find(marker) != std::string::npos) {
                        offenders.push_back(file.path.filename().string() + ":"
                            + std::to_string(index + 1) + " " + marker);
                    }
                }
            }
        }
    }
    Require(offenders.empty(),
        "no V2 source carries TODO/FIXME/XXX/HACK: " + Join(offenders));
}

KACHA_V2_TEST(architecture, v2_sources_avoid_names_that_are_macros_elsewhere)
{
    std::vector<std::string> offenders;
    for (const char* area : {"src/next", "src/next_occt", "src/apps/cad_next"}) {
        for (const SourceFile& file : CollectSourceFiles(RepoRoot() / area)) {
            for (std::size_t index = 0; index < file.lines.size(); ++index) {
                for (const std::string& name : kMacroReservedNames) {
                    if (DeclaresIdentifier(file.lines[index], name)) {
                        offenders.push_back(file.path.filename().string() + ":"
                            + std::to_string(index + 1) + " " + name);
                    }
                }
            }
        }
    }
    Require(offenders.empty(),
        "no V2 source names a variable after a Qt or Windows macro: " + Join(offenders));
}

KACHA_V2_TEST(architecture, the_scanner_itself_detects_a_planted_violation)
{
    // 走査が本当に効いているかを、その場で作った文字列で確かめる。
    // 検査そのものが空振りしていると、上の3件が常に通ってしまう。
    Require(IsIncludeOf("#include <QWidget>", "<Q"),
        "the include scanner sees a Qt include");
    Require(!IsIncludeOf("// #include <QWidget> is not allowed", "<Q") == false,
        "a commented include is still reported (conservative by design)");
    const std::vector<std::string> longFunction = [] {
        std::vector<std::string> lines;
        lines.push_back("void Example()");
        lines.push_back("{");
        for (int index = 0; index < 120; ++index) {
            lines.push_back("    int value = 0;");
        }
        lines.push_back("}");
        return lines;
    }();
    Require(LongestFunctionLength(longFunction) > kMaximumFunctionLines,
        "the function length scanner sees a 122 line function");
    const std::vector<std::string> shortFunction = {"void Small()", "{", "    return;", "}"};
    Require(LongestFunctionLength(shortFunction) <= kMaximumFunctionLines,
        "the function length scanner accepts a short function");

    // マクロと同じ名前を見つける走査も、その場で試す。
    // これを入れた理由: Qt の slots マクロに当たって、PC でだけ組めない状態を作ってしまった。
    Require(DeclaresIdentifier("    std::array<double*, 3> slots{&a, &b, &c};", "slots"),
        "the macro name scanner sees a variable called slots");
    Require(DeclaresIdentifier("    double near = 1.0;", "near"),
        "the macro name scanner sees a variable called near");
    Require(DeclaresIdentifier("void Take(int far, int other)", "far"),
        "the macro name scanner sees a parameter called far");
    Require(!DeclaresIdentifier("    // 近くの点(near)を探す", "near"),
        "a word inside a comment is not a declaration");
    Require(!DeclaresIdentifier("    nearest = 3;", "near"),
        "a longer word that merely starts with the name is not a declaration");
    Require(!DeclaresIdentifier("    value = far + 1;", "far"),
        "reading a name is not declaring it");
}

KACHA_V2_TEST_MAIN("architecture_tests")
