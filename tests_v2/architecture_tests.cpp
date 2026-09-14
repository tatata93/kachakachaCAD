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
#include <set>
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

namespace {

//! 注釈と文字列を落とした本文。Qt の型を数えるのに使う。
//! 落とさないと、注釈に書いた型名や、案内文の中の語まで数えてしまう。
[[nodiscard]] std::string BodyWithoutCommentsOrStrings(const std::vector<std::string>& lines)
{
    std::string body;
    bool inBlockComment = false;
    for (const std::string& line : lines) {
        bool inString = false;
        for (std::size_t index = 0; index < line.size(); ++index) {
            if (inBlockComment) {
                if (line[index] == '*' && index + 1 < line.size() && line[index + 1] == '/') {
                    inBlockComment = false;
                    ++index;
                }
                continue;
            }
            if (inString) {
                if (line[index] == '\\') {
                    ++index;   // 逃がし文字。次の1文字は読み飛ばす。
                } else if (line[index] == '"') {
                    inString = false;
                }
                continue;
            }
            if (line[index] == '/' && index + 1 < line.size()) {
                if (line[index + 1] == '/') {
                    break;   // 行末まで注釈。
                }
                if (line[index + 1] == '*') {
                    inBlockComment = true;
                    ++index;
                    continue;
                }
            }
            if (line[index] == '"') {
                inString = true;
                continue;
            }
            body.push_back(line[index]);
        }
        body.push_back('\n');
    }
    return body;
}

//! その本文が名前を出している Qt の型。`Qt::` の名前空間は数えない。
[[nodiscard]] std::set<std::string> QtTypesNamedIn(const std::string& body)
{
    const auto isWordChar = [](unsigned char value) {
        return std::isalnum(value) != 0 || value == '_';
    };
    std::set<std::string> found;
    for (std::size_t index = 0; index + 1 < body.size(); ++index) {
        if (body[index] != 'Q') {
            continue;
        }
        if (index > 0 && isWordChar(static_cast<unsigned char>(body[index - 1]))) {
            continue;   // 語の途中。
        }
        if (std::isupper(static_cast<unsigned char>(body[index + 1])) == 0) {
            continue;   // Qt:: など。型の名前ではない。
        }
        std::size_t end = index + 1;
        while (end < body.size() && isWordChar(static_cast<unsigned char>(body[end]))) {
            ++end;
        }
        std::string name = body.substr(index, end - index);
        index = end - 1;
        if (name == "QStringLiteral") {
            continue;   // 巨大マクロ。頭書きは QString のもの。
        }
        found.insert(std::move(name));
    }
    return found;
}

//! そのファイルが自分で書いている Qt の頭書き。
[[nodiscard]] std::set<std::string> QtIncludesIn(const std::vector<std::string>& lines)
{
    std::set<std::string> found;
    for (const std::string& line : lines) {
        const std::size_t open = line.find('<');
        const std::size_t close = line.find('>');
        if (line.find("#include") == std::string::npos || open == std::string::npos
            || close == std::string::npos || close < open + 2) {
            continue;
        }
        const std::string name = line.substr(open + 1, close - open - 1);
        if (name.size() >= 2 && name[0] == 'Q'
            && std::isupper(static_cast<unsigned char>(name[1])) != 0) {
            found.insert(name);
        }
    }
    return found;
}

} // namespace

KACHA_V2_TEST(architecture, screen_sources_include_the_qt_headers_they_use)
{
    // **当て木では捕まえられない失敗を、ここで捕まえる。**
    //
    // tools/qtstub は1枚の頭書きで全部の型を出すので、`#include <QMouseEvent>`
    // を書き忘れても雲の型検査は通る。本物の Qt では通らない。
    // この抜けで PC の組み立てを何度も落とした。関数を別のファイルへ移したとき、
    // 移した先に頭書きが無い、が典型である。
    //
    // 決まりは単純にする。**.cpp が名前を出している Qt の型は、
    // その .cpp が自分で include する。** 他の頭書き経由で通っていても書く。
    // 余分な include は害が無く、抜けは PC でしか分からない。
    std::vector<std::string> offenders;
    for (const SourceFile& file : CollectSourceFiles(RepoRoot() / "src/apps/cad_next")) {
        if (file.path.extension() != ".cpp") {
            continue;   // 頭書き(.h)は、実装の側が include するので見ない。
        }
        const std::set<std::string> used =
            QtTypesNamedIn(BodyWithoutCommentsOrStrings(file.lines));
        const std::set<std::string> included = QtIncludesIn(file.lines);
        for (const std::string& name : used) {
            if (included.count(name) == 0) {
                offenders.push_back(file.path.filename().string() + ": " + name);
            }
        }
    }
    Require(offenders.empty(),
        "every cad_next .cpp includes the Qt headers for the types it names: "
            + Join(offenders));
    // 走査そのものが効いているかを、その場で確かめる。
    const std::vector<std::string> planted{"#include <QString>", "QMouseEvent event;"};
    const auto plantedUsed = QtTypesNamedIn(BodyWithoutCommentsOrStrings(planted));
    Require(plantedUsed.count("QMouseEvent") == 1, "the scanner sees a used type");
    Require(QtIncludesIn(planted).count("QString") == 1, "the scanner sees an include");
    // 注釈と文字列の中の型名は数えない。
    const std::vector<std::string> quiet{"// QMouseEvent はここでは使わない",
        "const char* text = \"QMouseEvent\";"};
    Require(QtTypesNamedIn(BodyWithoutCommentsOrStrings(quiet)).empty(),
        "the scanner ignores comments and strings");
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

KACHA_V2_TEST(architecture, every_screen_source_is_built_and_type_checked)
{
    // 画面のファイルを足したのに CMake へ書き忘れると、
    // 雲では何も起きず、PC のビルドで初めて分かる。実際に一度そうなった。
    // 雲の側で「並べ忘れ」を捕まえる。
    const std::filesystem::path dir = RepoRoot() / "src" / "apps" / "cad_next";
    const std::string cmake = ReadFile(RepoRoot() / "CMakeLists.txt");
    const std::string typecheck = ReadFile(RepoRoot() / "tools" / "qtstub" / "typecheck.sh");
    std::vector<std::string> missing;
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
            continue;
        }
        const std::string name = entry.path().filename().string();
        names.push_back(name);
        if (cmake.find("src/apps/cad_next/" + name) == std::string::npos) {
            missing.push_back(name);
        }
    }
    Require(!names.empty(), "画面のファイルが1つ以上ある");
    Require(missing.empty(),
        "every cad_next source is listed in CMakeLists.txt: " + Join(missing));
    // 型検査は並べずに拾う。並べると、また漏れる。
    Require(typecheck.find("src/apps/cad_next/*.cpp") != std::string::npos,
        "typecheck.sh collects cad_next sources with a glob, not a hand-written list");
}

//! `V2MainWindow.h` が名乗っている関数のうち、中身がどこにも無いものを探す。
//!
//! 型検査(`-fsyntax-only`)は中身の有無を見ない。CMake も、ファイルが
//! 並んでいれば通す。だから **宣言だけ残して中身を消す** と、
//! 雲では何も起きず、PC の link で初めて分かる。実際に一度そうなった
//! (`ApplyBandBoundaries`、2026-09-14)。往復が1回まるごと無駄になる。
[[nodiscard]] std::vector<std::string> ScreenMethodsWithoutBodies()
{
    const std::filesystem::path dir = RepoRoot() / "src" / "apps" / "cad_next";
    const std::string header = ReadFile(dir / "V2MainWindow.h");
    std::string sources;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
            sources += ReadFile(entry.path());
            sources += '\n';
        }
    }
    std::vector<std::string> missing;
    std::istringstream lines(header);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t open = line.find('(');
        if (open == std::string::npos) {
            continue;
        }
        // 宣言だけの行。中身が同じ行にあるもの(`{ ... }`)は見ない。
        if (line.find('{') != std::string::npos || line.find(';') == std::string::npos) {
            continue;
        }
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 2, "//") == 0) {
            continue;
        }
        // `(` の直前の語を、行にある分だけ全部拾う。
        // `std::function<QString(bool)> Foo() const;` のように括弧が2つある行では、
        // どちらが関数の名前かは形だけでは決まらない。**1つでも中身があれば良し**
        // にすれば、名乗ったのに中身が無い行だけが残る。
        std::vector<std::string> candidates;
        for (std::size_t at = line.find('('); at != std::string::npos;
            at = line.find('(', at + 1)) {
            std::size_t end = at;
            while (end > 0 && (std::isalnum(static_cast<unsigned char>(line[end - 1])) != 0
                       || line[end - 1] == '_')) {
                --end;
            }
            if (end == at) {
                continue;
            }
            const std::string name = line.substr(end, at - end);
            // 大文字で始まる関数だけを見る。変数や型は見ない。
            if (name.empty() || std::isupper(static_cast<unsigned char>(name.front())) == 0
                || name == "QString" || name == "QStringLiteral") {
                continue;
            }
            candidates.push_back(name);
        }
        if (candidates.empty()) {
            continue;
        }
        bool found = false;
        for (const std::string& name : candidates) {
            if (sources.find("V2MainWindow::" + name) != std::string::npos) {
                found = true;
            }
        }
        if (!found) {
            missing.push_back(candidates.front());
        }
    }
    return missing;
}

KACHA_V2_TEST(architecture, every_screen_method_has_a_body)
{
    const auto missing = ScreenMethodsWithoutBodies();
    Require(missing.empty(),
        "V2MainWindow.h で名乗っている関数の中身がどこにも無い: " + Join(missing));
}

//! `/ "…"` の形で path をつなぐとき、その文字列に非ASCIIが入っているか。
[[nodiscard]] bool JoinsPathWithNonAsciiLiteral(const std::string& line)
{
    // 注釈は見ない。説明のために書いた例まで叱ると、説明が書けなくなる。
    const std::size_t first = line.find_first_not_of(" \t");
    if (first != std::string::npos && line.compare(first, 2, "//") == 0) {
        return false;
    }
    const std::string marker = "/ \"";
    std::size_t at = line.find(marker);
    while (at != std::string::npos) {
        const std::size_t start = at + marker.size();
        const std::size_t end = line.find('"', start);
        if (end == std::string::npos) {
            return false;
        }
        for (std::size_t index = start; index < end; ++index) {
            if (static_cast<unsigned char>(line[index]) >= 0x80U) {
                return true;
            }
        }
        at = line.find(marker, end);
    }
    return false;
}

KACHA_V2_TEST(architecture, paths_with_japanese_names_do_not_go_through_narrow_literals)
{
    // Windows の既定コードページは 932 なので、
    // std::filesystem::path / "作り方.md" と書くと UTF-8 の並びが
    // CP932 として読まれ、そのファイルは開けない。雲では開けるので気づけない。
    // 日本語の名前は io::MakePath(UTF-8) を通す。
    std::vector<std::string> offenders;
    for (const char* area : {"src/next", "src/next_occt", "src/apps/cad_next", "tests_v2"}) {
        for (const SourceFile& file : CollectSourceFiles(RepoRoot() / area)) {
            for (std::size_t index = 0; index < file.lines.size(); ++index) {
                if (JoinsPathWithNonAsciiLiteral(file.lines[index])) {
                    offenders.push_back(file.path.filename().string() + ":"
                        + std::to_string(index + 1));
                }
            }
        }
    }
    Require(offenders.empty(),
        "no path is joined with a narrow literal holding Japanese: " + Join(offenders));
    // 走査そのものが効いているかを、その場で確かめる。
    Require(JoinsPathWithNonAsciiLiteral("ReadFile(Root() / \"docs/manual/作り方.md\")"),
        "the scanner sees a Japanese path literal");
    Require(!JoinsPathWithNonAsciiLiteral("ReadFile(Root() / \"docs/manual/README.md\")"),
        "the scanner leaves plain ASCII paths alone");
}

//! 行から、注釈と文字列の中身を落とす。
//! `#` は**引用符の外**にあるときだけ注釈の始まりである。
//! Codex の指摘(AI-REVIEW-PIPELINE-TESTS-R5 B2)。文字列の中の `#` を注釈と
//! 見なしていたため、`$x="#"; $y=$a ?? $b` が素通りしていた。
[[nodiscard]] std::string PowerShellCodeOutsideStrings(const std::string& line)
{
    // 二重引用符の中でも `$(...)` の中は**コードである**。
    // Codex の指摘(AI-REVIEW-PIPELINE-TESTS-R6 B1)。中身をまるごと落としていたので
    // `"$($a ?? $b)"` が素通りしていた。入れ子も数える。
    //
    // `$(...)` の中の括弧だけを数えていたら、その中の文字列に入っている括弧まで
    // 数えてしまい、`"$(')'; $x = $a ?? $b)"` で途中から数が合わなくなって
    // 残りを文字列と読み違えていた(Codex AI-REVIEW-PIPELINE-TESTS-R7 B1)。
    // 引用符・逃がし・注釈を、どの深さでも同じように追う。
    struct Context {
        char kind;         //!< 'C' コード / 'S' 単引用 / 'D' 二重引用
        int parenDepth;    //!< 'C' のとき、その `$(` から数えた括弧の深さ
    };
    std::vector<Context> stack;
    stack.push_back(Context{'C', 0});
    std::string out;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char c = line[index];
        const char kind = stack.back().kind;
        if (kind == 'S') {
            // 単引用の中では、逃がしは `''` だけ。
            if (c == '\'') {
                if (index + 1 < line.size() && line[index + 1] == '\'') { ++index; continue; }
                stack.pop_back();
            }
            continue;
        }
        if (kind == 'D') {
            if (c == '`') { ++index; continue; }
            if (c == '"') {
                if (index + 1 < line.size() && line[index + 1] == '"') { ++index; continue; }
                stack.pop_back();
                continue;
            }
            if (c == '$' && index + 1 < line.size() && line[index + 1] == '(') {
                ++index;
                stack.push_back(Context{'C', 1});
                out.push_back(' ');
                continue;
            }
            continue;   // 文字列の中身は落とす
        }
        // ここからコード。
        if (c == '#') { break; }   // ここから先は注釈
        if (c == '\'') { stack.push_back(Context{'S', 0}); continue; }
        if (c == '"') { stack.push_back(Context{'D', 0}); continue; }
        if (stack.size() > 1) {
            if (c == '(') { ++stack.back().parenDepth; }
            else if (c == ')') {
                --stack.back().parenDepth;
                if (stack.back().parenDepth <= 0) {
                    stack.pop_back();
                    out.push_back(' ');
                    continue;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

//! PowerShell の二重引用符の中で `\"` はエスケープではない。
//! バックスラッシュはそのままの文字で、引用符はそこで文字列を**閉じる**。
//! 続きは次の引数として解釈され、型が合わずにスクリプトごと止まる。
//! 自己試験が丸ごと止まった原因がこれだった。雲で先に落とす。
[[nodiscard]] bool HasBackslashEscapedQuote(const std::string& line)
{
    char quote = '\0';
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char c = line[index];
        if (quote == '\0') {
            if (c == '\'' || c == '"') { quote = c; }
            else if (c == '#') { break; }
            continue;
        }
        if (quote == '"') {
            if (c == '`') { ++index; continue; }
            if (c == '\\' && index + 1 < line.size() && line[index + 1] == '"') { return true; }
        }
        if (c == quote) { quote = '\0'; }
    }
    return false;
}

//! レビュー基盤の PowerShell に、Windows PowerShell 5.1 で動かない書き方が
//! 混じっていないかを見る。PC でしか動かせない道具なので、雲の側で先に落とす。
[[nodiscard]] std::vector<std::string> PowerShell7OnlyTokensIn(const std::string& line)
{
    std::vector<std::string> hits;
    const std::string body = PowerShellCodeOutsideStrings(line);
    static const char* const tokens[] = {
        "??", "?.", "&&", "||", "-AsHashtable", "-Parallel", ".ArgumentList",
        "$IsWindows", "$IsLinux",
    };
    for (const char* token : tokens) {
        if (body.find(token) != std::string::npos) { hits.push_back(token); }
    }
    // 三項演算子 `<条件> ? <A> : <B>` も 7 だけのもの。
    // 引用符の外に ` ? ` と ` : ` が両方あるときだけ数える。
    if (body.find(" ? ") != std::string::npos && body.find(" : ") != std::string::npos) {
        hits.push_back("ternary ? :");
    }
    return hits;
}

KACHA_V2_TEST(architecture, the_local_review_pipeline_is_present_and_runs_on_windows_powershell)
{
    // レビュー基盤は PC の上でしか動かない。動かないものを動くことにしないために、
    // 「ファイルがあること」と「5.1 で死ぬ書き方が無いこと」だけは雲で毎回見る。
    const std::vector<std::string> required = {
        "tools/ai-local/review-common.ps1",
        "tools/ai-local/review-precheck.ps1",
        "tools/ai-local/review-enqueue.ps1",
        "tools/ai-local/review-dispatcher.ps1",
        "tools/ai-local/review-runner.ps1",
        "tools/ai-local/review-ledger.ps1",
        "tools/ai-local/review-profile.ps1",
        "tools/ai-local/review-recover.ps1",
        "tools/ai-local/queue-status.ps1",
        "tools/ai-local/stop-stale-dispatcher.ps1",
        "tools/ai-local/clear-hold.ps1",
        "tools/ai-local/review-selftest.ps1",
        "tools/ai-local/start-dispatcher.cmd",
        "docs/ai/CODEX_REVIEW_POLICY.md",
        "docs/ai/LOCAL_REVIEW_PIPELINE.md",
        "docs/ai/review-schemas.md",
    };
    std::vector<std::string> missing;
    for (const std::string& relative : required) {
        if (!std::filesystem::exists(RepoRoot() / relative)) { missing.push_back(relative); }
    }
    Require(missing.empty(), "the review pipeline is complete: " + Join(missing));

    std::vector<std::string> offenders;
    for (const std::string& relative : required) {
        if (relative.size() < 4 || relative.substr(relative.size() - 4) != ".ps1") { continue; }
        std::istringstream stream(ReadFile(RepoRoot() / relative));
        std::string line;
        int number = 0;
        while (std::getline(stream, line)) {
            ++number;
            for (const std::string& token : PowerShell7OnlyTokensIn(line)) {
                offenders.push_back(relative + ":" + std::to_string(number) + " " + token);
            }
        }
    }
    Require(offenders.empty(),
        "nothing in the review pipeline needs PowerShell 7: " + Join(offenders));

    // Windows PowerShell 5.1 reads a file with no byte order mark as the machine's
    // ANSI code page, which is CP932 here. A single Japanese character written
    // straight into a .ps1 therefore breaks the parser on the PC while looking
    // perfectly fine in the cloud. It happened once; the scripts stay pure ASCII
    // and build any Japanese they need from code points at run time.
    std::vector<std::string> nonAscii;
    for (const std::string& relative : required) {
        if (relative.size() < 4 || relative.substr(relative.size() - 4) != ".ps1") { continue; }
        const std::string text = ReadFile(RepoRoot() / relative);
        int number = 1;
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '\n') { ++number; continue; }
            if (static_cast<unsigned char>(text[index]) > 127u) {
                nonAscii.push_back(relative + ":" + std::to_string(number));
                break;
            }
        }
    }
    Require(nonAscii.empty(),
        "every review script is pure ASCII, so PowerShell 5.1 can parse it: " + Join(nonAscii));

    std::vector<std::string> escaped;
    for (const std::string& relative : required) {
        if (relative.size() < 4 || relative.substr(relative.size() - 4) != ".ps1") { continue; }
        std::istringstream stream(ReadFile(RepoRoot() / relative));
        std::string line;
        int number = 0;
        while (std::getline(stream, line)) {
            ++number;
            if (HasBackslashEscapedQuote(line)) {
                escaped.push_back(relative + ":" + std::to_string(number));
            }
        }
    }
    Require(escaped.empty(),
        "no review script tries to escape a quote with a backslash: " + Join(escaped));
    // 走査そのものが効いているかを、その場で確かめる。
    Require(HasBackslashEscapedQuote("$x = \"say \\\"hi\\\"\""),
        "the scanner sees a backslash before a quote inside a double quoted string");
    Require(!HasBackslashEscapedQuote("$x = 'a \\\" inside single quotes'"),
        "a single quoted string may hold a backslash and a quote");
    Require(!HasBackslashEscapedQuote("$x = \"a backtick escape `\" is fine\""),
        "the backtick is the escape character PowerShell actually uses");

    // 実行時領域は git に入れない。入れると PC ごとの事情が共有されてしまう。
    const std::string ignore = ReadFile(RepoRoot() / ".gitignore");
    Require(ignore.find(".ai-runtime/") != std::string::npos,
        ".ai-runtime/ is ignored by git");

    // 走査そのものが効いているかを、その場で確かめる。
    Require(!PowerShell7OnlyTokensIn("$x = $a ?? $b").empty(),
        "the scanner sees a PowerShell 7 only operator");
    Require(PowerShell7OnlyTokensIn("# ?? is not available in 5.1").empty(),
        "the scanner leaves an explanation in a comment alone");
    Require(PowerShell7OnlyTokensIn("$psi.Arguments = $line").empty(),
        "the scanner leaves the 5.1 way alone");
    // 植えた違反で確かめる(Codex の指摘そのもの)。
    Require(!PowerShell7OnlyTokensIn("$x = \"#\"; $y = $a ?? $b").empty(),
        "a hash inside a string does not hide what comes after it");
    Require(!PowerShell7OnlyTokensIn("$y = $ok ? 1 : 0").empty(),
        "the scanner sees the PowerShell 7 ternary");
    Require(PowerShell7OnlyTokensIn("$text = 'use ? : when you have 7'").empty(),
        "a ternary written inside a string is not a ternary");
    Require(PowerShell7OnlyTokensIn("$map = @{ 'a' = 1 }  # ?? and && are 7 only").empty(),
        "the list of forbidden operators may be written in a comment");
    Require(!PowerShell7OnlyTokensIn("Write-Host \"$($a ?? $b)\"").empty(),
        "code inside an expanding string is still code");
    Require(!PowerShell7OnlyTokensIn("Write-Host \"x $( $y = $( $a ?? $b ) ) z\"").empty(),
        "a nested subexpression is still code");
    Require(PowerShell7OnlyTokensIn("Write-Host \"a plain ?? in text\"").empty(),
        "text inside an expanding string is still text");
    // Codex の指摘(AI-REVIEW-PIPELINE-TESTS-R7 B1)。
    // `$(...)` の中の文字列に括弧が入っていても、数を見失わないこと。
    Require(!PowerShell7OnlyTokensIn("Write-Host \"$(')'; $x = $a ?? $b)\"").empty(),
        "a bracket inside a string inside a subexpression does not hide the code after it");
    Require(!PowerShell7OnlyTokensIn("Write-Host \"$(\"\")\" ; $y = $a ?? $b").empty(),
        "code after a subexpression is still code");
    Require(PowerShell7OnlyTokensIn("Write-Host \"$('a ?? b')\"").empty(),
        "text inside a string inside a subexpression is still text");
    Require(PowerShell7OnlyTokensIn("$x = 'it''s ?? fine'").empty(),
        "a doubled quote does not end a single quoted string");
}

//! 台帳へ書く出来事の名前を、スクリプトから拾う。
//! `event = 'name'` のように、その場に名前が書いてあるものだけを拾う。
//! `event = $Variable` は下の走査が引き受ける。
[[nodiscard]] std::vector<std::string> LedgerEventNamesIn(const std::string& line)
{
    // `event=` に続く文字列そのものを拾う。空白の有無も引用符の種類も問わない。
    // 注釈の中は拾わない。Codex の指摘(AI-REVIEW-PIPELINE-TESTS-R6 B3)。
    std::vector<std::string> names;
    std::size_t at = line.find("event");
    while (at != std::string::npos) {
        // 注釈より後ろは見ない。引用符の外にある `#` だけが注釈の始まり。
        const std::string before = line.substr(0, at);
        const std::string beforeCode = PowerShellCodeOutsideStrings(before);
        if (beforeCode.size() != before.size()) { break; }
        std::size_t cursor = at + 5;
        while (cursor < line.size() && line[cursor] == ' ') { ++cursor; }
        if (cursor >= line.size() || line[cursor] != '=') { at = line.find("event", at + 5); continue; }
        ++cursor;
        while (cursor < line.size() && line[cursor] == ' ') { ++cursor; }
        if (cursor >= line.size()) { break; }
        const char quote = line[cursor];
        if (quote != '\'' && quote != '"') { at = line.find("event", cursor); continue; }
        const std::size_t start = cursor + 1;
        const std::size_t end = line.find(quote, start);
        if (end == std::string::npos) { break; }
        names.push_back(line.substr(start, end - start));
        at = line.find("event", end);
    }
    return names;
}

//! `$` で始まる名前を読み取る。読めなければ空。
[[nodiscard]] std::string PowerShellVariableAt(const std::string& line, std::size_t at)
{
    if (at >= line.size() || line[at] != '$') { return {}; }
    std::size_t end = at + 1;
    while (end < line.size()
        && (std::isalnum(static_cast<unsigned char>(line[end])) != 0 || line[end] == '_')) {
        ++end;
    }
    if (end == at + 1) { return {}; }
    return line.substr(at + 1, end - at - 1);
}

//! `key` のすぐ後ろにある値を読む。`'名前'` なら名前を、`$変数` なら変数名を返す。
//! 見つけたものが名前なのか変数なのかを、呼ぶ側が区別できるようにして返す。
struct LedgerEventUse {
    std::string literal;    //!< その場に書いてある名前
    std::string variable;   //!< 変数で渡している名前
};

[[nodiscard]] std::vector<LedgerEventUse> LedgerEventUsesIn(
    const std::string& line, const std::string& key, bool requireEquals)
{
    std::vector<LedgerEventUse> uses;
    std::size_t at = line.find(key);
    while (at != std::string::npos) {
        // 注釈より後ろは見ない。
        const std::string before = line.substr(0, at);
        if (PowerShellCodeOutsideStrings(before).size() != before.size()) { break; }
        std::size_t cursor = at + key.size();
        while (cursor < line.size() && line[cursor] == ' ') { ++cursor; }
        if (requireEquals) {
            if (cursor >= line.size() || line[cursor] != '=') {
                at = line.find(key, at + key.size());
                continue;
            }
            ++cursor;
            while (cursor < line.size() && line[cursor] == ' ') { ++cursor; }
        }
        if (cursor >= line.size()) { break; }
        const char c = line[cursor];
        if (c == '\'' || c == '"') {
            const std::size_t start = cursor + 1;
            const std::size_t end = line.find(c, start);
            if (end == std::string::npos) { break; }
            uses.push_back(LedgerEventUse{line.substr(start, end - start), std::string()});
            at = line.find(key, end);
            continue;
        }
        const std::string variable = PowerShellVariableAt(line, cursor);
        if (!variable.empty()) {
            uses.push_back(LedgerEventUse{std::string(), variable});
        }
        at = line.find(key, cursor);
    }
    return uses;
}

//! `$name = '値'` と `[string]$name = '値'` から、その変数に入り得る名前を拾う。
[[nodiscard]] std::vector<std::string> StringsAssignedTo(
    const std::string& line, const std::string& variable)
{
    std::vector<std::string> values;
    const std::string needle = "$" + variable;
    std::size_t at = line.find(needle);
    while (at != std::string::npos) {
        const std::string before = line.substr(0, at);
        if (PowerShellCodeOutsideStrings(before).size() != before.size()) { break; }
        const std::size_t after = at + needle.size();
        // 名前の切れ目でなければ、別の変数である。
        const bool wholeName = after >= line.size()
            || (std::isalnum(static_cast<unsigned char>(line[after])) == 0
                && line[after] != '_');
        if (wholeName) {
            std::size_t cursor = after;
            while (cursor < line.size() && line[cursor] == ' ') { ++cursor; }
            if (cursor < line.size() && line[cursor] == '=') {
                ++cursor;
                while (cursor < line.size() && line[cursor] == ' ') { ++cursor; }
                if (cursor < line.size() && (line[cursor] == '\'' || line[cursor] == '"')) {
                    const char quote = line[cursor];
                    const std::size_t start = cursor + 1;
                    const std::size_t end = line.find(quote, start);
                    if (end != std::string::npos) {
                        values.push_back(line.substr(start, end - start));
                    }
                }
            }
        }
        at = line.find(needle, at + needle.size());
    }
    return values;
}

KACHA_V2_TEST(architecture, every_ledger_event_the_scripts_write_is_written_down)
{
    // Codex の指摘(AI-REVIEW-PIPELINE-DOCS-R5 B2)。
    // 文書に載っていない出来事を台帳へ書くと、読む側は意味を推測するしかない。
    // 書ける名前は、必ず review-schemas.md に説明がある状態を保つ。
    const std::filesystem::path scripts = RepoRoot() / "tools/ai-local";
    const std::string schemas = ReadFile(RepoRoot() / "docs/ai/review-schemas.md");
    Require(!schemas.empty(), "docs/ai/review-schemas.md is readable");

    // まず全部の行を手元に集める。変数で渡している出来事は、
    // その変数に何が入るかを別の行から探さなければ名前が分からない。
    std::vector<std::string> allLines;
    for (const auto& entry : std::filesystem::directory_iterator(scripts)) {
        if (!entry.is_regular_file()) { continue; }
        if (entry.path().extension() != ".ps1") { continue; }
        std::istringstream stream(ReadFile(entry.path()));
        std::string line;
        while (std::getline(stream, line)) { allLines.push_back(line); }
    }

    std::set<std::string> written;
    std::set<std::string> variables;
    for (const std::string& line : allLines) {
        for (const std::string& name : LedgerEventNamesIn(line)) { written.insert(name); }
        // 変数で渡している分。`event = $x` と `-LedgerEvent $x` の両方を見る。
        // Codex の指摘(AI-REVIEW-PIPELINE-TESTS-R7 B2)。変数を黙って見逃していたので、
        // 文書に無い名前を変数越しに書いても、この関所は通っていた。
        for (const auto& use : LedgerEventUsesIn(line, "event", true)) {
            if (!use.variable.empty()) { variables.insert(use.variable); }
        }
        for (const auto& use : LedgerEventUsesIn(line, "-LedgerEvent", false)) {
            if (!use.literal.empty()) { written.insert(use.literal); }
            if (!use.variable.empty()) { variables.insert(use.variable); }
        }
    }
    // 変数に入る名前を、同じ道具箱の中から集める。
    std::vector<std::string> unresolved;
    for (const std::string& variable : variables) {
        std::size_t found = 0;
        for (const std::string& line : allLines) {
            for (const std::string& value : StringsAssignedTo(line, variable)) {
                written.insert(value);
                ++found;
            }
        }
        // 何が入るのか分からない変数を、分からないまま通さない。
        if (found == 0) { unresolved.push_back(variable); }
    }
    Require(unresolved.empty(),
        "every ledger event passed through a variable can be traced to a name: "
            + Join(unresolved));
    Require(written.size() >= 8, "the scan found the ledger events at all");

    std::vector<std::string> undocumented;
    for (const std::string& name : written) {
        if (schemas.find("`" + name + "`") == std::string::npos) { undocumented.push_back(name); }
    }
    Require(undocumented.empty(),
        "every ledger event is described in review-schemas.md: " + Join(undocumented));

    // 走査そのものが効いているかを、その場で確かめる。
    const auto found = LedgerEventNamesIn("        event = 'review_timeout'; request_id = $x");
    Require(found.size() == 1 && found.front() == "review_timeout", "the scanner reads a literal event name");
    Require(LedgerEventNamesIn("        event = $LedgerEvent; request_id = $x").empty(),
        "the scanner leaves a variable alone");
    const auto tight = LedgerEventNamesIn("        event='review_timeout'");
    Require(tight.size() == 1 && tight.front() == "review_timeout", "spacing does not hide an event name");
    const auto doubled = LedgerEventNamesIn("        event = \"review_timeout\"");
    Require(doubled.size() == 1 && doubled.front() == "review_timeout", "either quote character works");
    Require(LedgerEventNamesIn("        # event = 'not_a_real_event'").empty(),
        "an event name written in a comment is not an event");

    // 変数で渡す出来事も追えること。追えなければ、文書に無い名前を
    // 変数越しに書くだけでこの関所を抜けられる(Codex TESTS-R7 B2)。
    const auto viaVariable = LedgerEventUsesIn("        event = $LedgerEvent; x = 1", "event", true);
    Require(viaVariable.size() == 1 && viaVariable.front().variable == "LedgerEvent",
        "the scanner sees which variable carries the event name");
    const auto viaArgument = LedgerEventUsesIn("    -LedgerEvent 'review_timeout' -Outcome 'X'",
        "-LedgerEvent", false);
    Require(viaArgument.size() == 1 && viaArgument.front().literal == "review_timeout",
        "the scanner reads an event name passed as an argument");
    const auto assigned = StringsAssignedTo("$ledgerEvent = 'undocumented_event'", "ledgerEvent");
    Require(assigned.size() == 1 && assigned.front() == "undocumented_event",
        "the scanner reads what a variable is set to");
    Require(StringsAssignedTo("$ledgerEventOther = 'x'", "ledgerEvent").empty(),
        "a longer variable name is a different variable");
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
