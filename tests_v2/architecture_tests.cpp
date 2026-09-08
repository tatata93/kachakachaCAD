// AT-ARC-001 依存境界: V2 core は Qt と OCCT に依存してはならない。
// AT-ARC-005 コード衛生: 行数上限、禁止include、未完了マーカーを機械で見る。
//
// 目視ではなく、ソース木を実際に走査して数える。
// KACHACAD_V2_REPO_ROOT はCMakeが渡す。
#include "kachakacha/base/SourceScan.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
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
using kachakacha::v2::test::RequireEqual;

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

[[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
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

//! 試験の名前は C++ の識別子になる(KACHA_V2_TEST が名前を関数名に埋め込むため)。
//! 空白・記号・先頭の数字はコンパイルエラーになる。日本語はそのまま使える。
//! これを入れた理由: 空白入りの名前を書いて、PC でだけ組めない状態を2回作った。
//! 雲の側の試験は「登録された名前」しか見ないので、書いた時点では気づけない。
[[nodiscard]] bool IsIdentifierText(const std::string& text)
{
    if (text.empty()) {
        return false;
    }
    // 先頭が数字でもよい。名前は kacha_v2_case_<suite>_<name> の後ろへ入るので、
    // 識別子の先頭には来ない。
    for (const char raw : text) {
        const unsigned char value = static_cast<unsigned char>(raw);
        if (value >= 0x80) {
            continue;   // 多バイト文字(日本語など)は識別子に使える。
        }
        const bool letter = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
        const bool digit = value >= '0' && value <= '9';
        if (!letter && !digit && value != '_') {
            return false;
        }
    }
    return true;
}

//! KACHA_V2_TEST(suite, name) の name を取り出す。見つからなければ空。
[[nodiscard]] std::string TestNameIn(const std::string& line)
{
    const std::string marker = "KACHA_V2_TEST(";
    // 行の先頭にあるものだけを拾う。文字列の中に出てくる同じ並びは拾わない。
    const std::size_t at = line.rfind(marker, 0);
    if (at != 0) {
        return {};
    }
    const std::size_t comma = line.find(',', at);
    const std::size_t close = line.rfind(')');
    if (comma == std::string::npos || close == std::string::npos || close <= comma) {
        return {};
    }
    std::string name = line.substr(comma + 1, close - comma - 1);
    // 前後の空白だけを落とす。中の空白は落とさない(それが見たいものである)。
    while (!name.empty() && name.front() == ' ') {
        name.erase(name.begin());
    }
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name;
}

//! OCCT の型は、使うファイルで include されていなければならない。
//!
//! OCCT のヘッダは互いを深く include しているので、書き忘れても
//! たまたま通ることがある。通らなかったときは PC でだけ落ちる。
//! 雲には OCCT が入らないので、そこでは気づけない。
//! 「その型を変数の型か戻り値として書いているなら、その include も書く」
//! を機械で守る。名前を挙げるだけの行(コメント、文字列)は見ない。
[[nodiscard]] std::vector<std::string> OcctTypesDeclaredIn(const std::string& line)
{
    std::vector<std::string> found;
    if (line.find("#include") != std::string::npos) {
        return found;
    }
    // 大文字で始まり、下線を1つ挟む OCCT 風の名前。
    const auto isTypeChar = [](char value) {
        return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
    };
    for (std::size_t at = 0; at < line.size(); ++at) {
        if (!(line[at] >= 'A' && line[at] <= 'Z')) {
            continue;
        }
        if (at > 0 && isTypeChar(line[at - 1])) {
            continue;
        }
        std::size_t end = at;
        while (end < line.size() && isTypeChar(line[end])) {
            ++end;
        }
        const std::string word = line.substr(at, end - at);
        const std::size_t underscore = word.find('_');
        if (underscore == std::string::npos || underscore == 0
            || underscore + 1 >= word.size()) {
            continue;
        }
        // その語のすぐ後ろが「変数名 + 区切り」なら、型として書いている。
        std::size_t next = end;
        while (next < line.size() && line[next] == ' ') {
            ++next;
        }
        const bool spaced = next > end;
        std::size_t nameEnd = next;
        while (nameEnd < line.size() && isTypeChar(line[nameEnd])) {
            ++nameEnd;
        }
        const bool named = nameEnd > next;
        const bool closed = nameEnd < line.size()
            && (line[nameEnd] == ';' || line[nameEnd] == '=' || line[nameEnd] == '('
                || line[nameEnd] == '{' || line[nameEnd] == ',' || line[nameEnd] == ')');
        if (spaced && named && closed) {
            found.push_back(word);
        }
        // Result<Type> の形も型として書いている。
        if (at >= 7 && line.compare(at - 7, 7, "Result<") == 0 && end < line.size()
            && line[end] == '>') {
            found.push_back(word);
        }
        at = end;
    }
    return found;
}

KACHA_V2_TEST(architecture, occt_types_are_included_where_used)
{
    std::vector<std::string> offenders;
    for (const SourceFile& file : CollectSourceFiles(RepoRoot() / "src" / "next_occt")) {
        std::vector<std::string> includes;
        for (const std::string& line : file.lines) {
            const std::size_t open = line.find("#include <");
            if (open == std::string::npos) {
                continue;
            }
            const std::size_t close = line.find(".hxx>", open);
            if (close == std::string::npos) {
                continue;
            }
            includes.push_back(line.substr(open + 10, close - open - 10));
        }
        for (std::size_t index = 0; index < file.lines.size(); ++index) {
            for (const std::string& type : OcctTypesDeclaredIn(file.lines[index])) {
                if (std::find(includes.begin(), includes.end(), type) != includes.end()) {
                    continue;
                }
                // core 側の型(kachakacha の名前空間)は OCCT ではない。
                if (type.compare(0, 5, "TopoD") != 0 && type.compare(0, 4, "BRep") != 0
                    && type.compare(0, 5, "Geom_") != 0 && type.compare(0, 3, "gp_") != 0
                    && type.compare(0, 4, "Bnd_") != 0 && type.compare(0, 6, "GProp_") != 0
                    && type.compare(0, 7, "TopAbs_") != 0
                    && type.compare(0, 8, "TopTools") != 0
                    && type.compare(0, 9, "IFSelect_") != 0
                    && type.compare(0, 12, "STEPControl_") != 0) {
                    continue;
                }
                offenders.push_back(file.path.filename().string() + ":"
                    + std::to_string(index + 1) + " " + type);
            }
        }
    }
    Require(offenders.empty(),
        "every OCCT type used is included in that file: " + Join(offenders));
}

KACHA_V2_TEST(architecture, v2_test_names_are_valid_identifiers)
{
    std::vector<std::string> offenders;
    for (const SourceFile& file : CollectSourceFiles(RepoRoot() / "tests_v2")) {
        for (std::size_t index = 0; index < file.lines.size(); ++index) {
            const std::string name = TestNameIn(file.lines[index]);
            if (name.empty()) {
                continue;
            }
            if (!IsIdentifierText(name)) {
                offenders.push_back(file.path.filename().string() + ":"
                    + std::to_string(index + 1) + " \"" + name + "\"");
            }
        }
    }
    Require(offenders.empty(),
        "every test name is a valid C++ identifier: " + Join(offenders));
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

KACHA_V2_TEST(architecture, every_v2_target_gets_the_shared_compile_options)
{
    // V2 の実行ファイルは、どれも同じ規格と警告で組み立てる。
    // 忘れると、その1つだけが古い規格で組み立てられ、
    // 雲では通るのに Windows で落ちる。実際に一度そうなった。
    const std::string text = ReadFile(RepoRoot() / "CMakeLists.txt");
    std::vector<std::string> offenders;
    std::istringstream stream(text);
    std::string line;
    std::vector<std::string> declared;
    std::vector<std::string> configured;
    while (std::getline(stream, line)) {
        const std::string marker = "add_executable(kachakacha_v2_";
        const std::size_t at = line.find(marker);
        if (at != std::string::npos) {
            const std::size_t start = at + std::string("add_executable(").size();
            const std::size_t end = line.find_first_of(" )\t", start);
            if (end != std::string::npos) {
                declared.push_back(line.substr(start, end - start));
            }
        }
        const std::string applied = "kachakacha_apply_test_options(kachakacha_v2_";
        const std::size_t used = line.find(applied);
        if (used != std::string::npos) {
            const std::size_t start = used
                + std::string("kachakacha_apply_test_options(").size();
            const std::size_t end = line.find(')', start);
            if (end != std::string::npos) {
                configured.push_back(line.substr(start, end - start));
            }
        }
    }
    // 台帳から作る試験は関数の中で必ず options を当てているので、
    // ここで見るのは「直に add_executable したもの」だけである。
    for (const std::string& name : declared) {
        if (std::find(configured.begin(), configured.end(), name) == configured.end()) {
            offenders.push_back(name);
        }
    }
    Require(!declared.empty(), "V2 の実行ファイルが1つ以上ある");
    Require(offenders.empty(),
        "every directly declared V2 executable applies the shared options: "
            + Join(offenders));
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

    // 試験名の走査も、その場で試す。
    Require(IsIdentifierText("曲線を曲線のまま描ける"), "日本語だけの名前は通る");
    Require(IsIdentifierText("draw_a_line"), "英数字と下線も通る");
    Require(!IsIdentifierText("面のキーが core の決めたものと同じ"), "空白は落とす");
    Require(!IsIdentifierText("draw-a-line"), "ハイフンは落とす");
    Require(IsIdentifierText("2本目の線"), "先頭の数字は通る(識別子の途中に入るため)");
    Require(!IsIdentifierText(""), "空の名前は落とす");
    RequireEqual(TestNameIn("KACHA_V2_TEST(suite, 名前)"), "名前", "名前を取り出せる");
    RequireEqual(TestNameIn("KACHA_V2_TEST(suite,  空白つき 名前 )"), "空白つき 名前",
        "前後の空白だけを落とす");
    Require(TestNameIn("// ふつうの行").empty(), "関係ない行は拾わない");

    // OCCT の include の走査も、その場で試す。
    const auto declared = OcctTypesDeclaredIn("    TopoDS_Compound compound;");
    Require(std::find(declared.begin(), declared.end(), "TopoDS_Compound")
            != declared.end(),
        "変数の型として書いた OCCT の型を拾う");
    const auto returned = OcctTypesDeclaredIn("Result<TopoDS_Face> MakeFace()");
    Require(std::find(returned.begin(), returned.end(), "TopoDS_Face") != returned.end(),
        "戻り値として書いた OCCT の型も拾う");
    Require(OcctTypesDeclaredIn("#include <TopoDS_Compound.hxx>").empty(),
        "include の行そのものは拾わない");
    Require(OcctTypesDeclaredIn("// TopoDS_Compound を作る").empty(),
        "名前を挙げるだけの行は拾わない");
}

KACHA_V2_TEST_MAIN("architecture_tests")
