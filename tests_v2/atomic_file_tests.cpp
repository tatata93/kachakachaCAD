// 原子的保存(AT-EXP-004)。
//
// 途中で失敗しても、既存のファイルが読める状態で残ること。
// V1 は本名を開いて上書きしたので、途中で落ちると開けないファイルが残った。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using kachakacha::v2::io::AtomicWriteOptions;
using kachakacha::v2::io::FromPath;
using kachakacha::v2::io::MakePath;
using kachakacha::v2::io::PathExists;
using kachakacha::v2::io::ReadWholeFile;
using kachakacha::v2::io::WriteFileAtomically;
using kachakacha::v2::io::WriteWholeFile;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

//! 試験用の作業場所。ケースごとに別のフォルダを使う。
[[nodiscard]] std::string Workspace(const std::string& name)
{
    static int counter = 0;
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / "kachakacha_v2_atomic";
    const std::filesystem::path directory =
        base / (name + "_" + std::to_string(++counter));
    std::error_code code;
    std::filesystem::remove_all(directory, code);
    std::filesystem::create_directories(directory, code);
    Require(!code, "作業場所が作れること");
    // パスは UTF-8 の文字列で持つ。path::string() は Windows で ANSI を返す。
    return FromPath(directory);
}

[[nodiscard]] std::string Join(const std::string& directory, const std::string& name)
{
    // 文字列のまま繋ぐ。path を経由すると Windows で日本語が壊れる。
    return directory + "/" + name;
}

void WriteExisting(const std::string& path, const std::string& content)
{
    std::string error;
    Require(WriteWholeFile(path, content, error), "下準備の書き込みが通ること");
}

[[nodiscard]] bool Exists(const std::string& path)
{
    return PathExists(path);
}

[[nodiscard]] std::string Read(const std::string& path)
{
    auto content = ReadWholeFile(path);
    Require(content.HasValue(), "読めること");
    return content.Value();
}

[[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

} // namespace

KACHA_V2_TEST(atomic, 新しいファイルを作れる)
{
    const std::string directory = Workspace("new");
    const std::string path = Join(directory, "a.kcd2");
    const auto report = WriteFileAtomically(path, "hello");
    Require(report.HasValue(), "書けること");
    Require(Exists(path), "ファイルがある");
    RequireEqual(Read(path), "hello", "中身");
    Require(report.Value().backupPath.empty(), "控えは作らない");
    Require(!report.Value().replacedExisting, "置き換えではない");
}

KACHA_V2_TEST(atomic, 一時ファイルを残さない)
{
    const std::string directory = Workspace("tmp");
    const std::string path = Join(directory, "a.kcd2");
    const auto report = WriteFileAtomically(path, "hello");
    Require(report.HasValue(), "書けること");
    Require(!Exists(path + ".tmp"), "一時ファイルが残っていない");
}

KACHA_V2_TEST(atomic, 既存を置き換えると控えが残る)
{
    const std::string directory = Workspace("replace");
    const std::string path = Join(directory, "a.kcd2");
    WriteExisting(path, "古い中身");
    const auto report = WriteFileAtomically(path, "新しい中身");
    Require(report.HasValue(), "書けること");
    RequireEqual(Read(path), "新しい中身", "本体");
    Require(Exists(path + ".bak"), "控えがある");
    RequireEqual(Read(path + ".bak"), "古い中身", "控えの中身");
    Require(report.Value().replacedExisting, "置き換えたと言う");
}

KACHA_V2_TEST(atomic, 控えを残さない設定もできる)
{
    const std::string directory = Workspace("nobak");
    const std::string path = Join(directory, "a.kcd2");
    WriteExisting(path, "古い中身");
    AtomicWriteOptions options;
    options.keepBackup = false;
    const auto report = WriteFileAtomically(path, "新しい中身", options);
    Require(report.HasValue(), "書けること");
    RequireEqual(Read(path), "新しい中身", "本体");
    Require(!Exists(path + ".bak"), "控えは無い");
}

KACHA_V2_TEST(atomic, 2回書くと控えが最新の1つ前になる)
{
    const std::string directory = Workspace("twice");
    const std::string path = Join(directory, "a.kcd2");
    WriteExisting(path, "1回目");
    Require(WriteFileAtomically(path, "2回目").HasValue(), "2回目");
    Require(WriteFileAtomically(path, "3回目").HasValue(), "3回目");
    RequireEqual(Read(path), "3回目", "本体");
    RequireEqual(Read(path + ".bak"), "2回目", "控え");
}

// ---- 途中で失敗させる ----

KACHA_V2_TEST(atomic, 書き込みが失敗しても既存はそのまま読める)
{
    const std::string directory = Workspace("failwrite");
    const std::string path = Join(directory, "a.kcd2");
    WriteExisting(path, "大事な中身");
    const auto report = WriteFileAtomically(path, "新しい中身", {},
        [](const std::string&, std::string_view, std::string& errorOut) {
            errorOut = "わざと失敗させた";
            return false;
        });
    Require(!report.HasValue(), "書けたことにしない");
    RequireEqual(FirstCode(report.Diagnostics()), "IO-A001", "診断コード");
    Require(Exists(path), "既存が残っている");
    RequireEqual(Read(path), "大事な中身", "既存の中身が変わっていない");
    Require(!Exists(path + ".tmp"), "一時ファイルが残っていない");
}

KACHA_V2_TEST(atomic, 途中まで書いて落ちても既存はそのまま読める)
{
    const std::string directory = Workspace("halfwrite");
    const std::string path = Join(directory, "a.kcd2");
    WriteExisting(path, "大事な中身");
    const auto report = WriteFileAtomically(path,
        "0123456789012345678901234567890123456789", {},
        [](const std::string& target, std::string_view content, std::string& errorOut) {
            // 半分だけ書いてから失敗する。実際の電源断に近い。
            std::string partial;
            (void)WriteWholeFile(target, content.substr(0, content.size() / 2), partial);
            errorOut = "途中で落ちた";
            return false;
        });
    Require(!report.HasValue(), "書けたことにしない");
    RequireEqual(Read(path), "大事な中身", "既存の中身が変わっていない");
    Require(!Exists(path + ".tmp"), "半分書けた一時ファイルは片づける");
}

KACHA_V2_TEST(atomic, 書けた中身が違えば置き換えない)
{
    const std::string directory = Workspace("verify");
    const std::string path = Join(directory, "a.kcd2");
    WriteExisting(path, "大事な中身");
    const auto report = WriteFileAtomically(path, "本来の中身", {},
        [](const std::string& target, std::string_view, std::string& errorOut) {
            // 「書けた」と言いながら、違う中身を書く。
            std::string ignored;
            const bool ok = WriteWholeFile(target, "違う中身", ignored);
            errorOut.clear();
            return ok;
        });
    Require(!report.HasValue(), "書けたことにしない");
    RequireEqual(FirstCode(report.Diagnostics()), "IO-A003", "確認に失敗した診断コード");
    RequireEqual(Read(path), "大事な中身", "既存の中身が変わっていない");
}

KACHA_V2_TEST(atomic, 確認を切ることもできる)
{
    const std::string directory = Workspace("noverify");
    const std::string path = Join(directory, "a.kcd2");
    AtomicWriteOptions options;
    options.verifyAfterWrite = false;
    const auto report = WriteFileAtomically(path, "中身", options);
    Require(report.HasValue(), "書けること");
    RequireEqual(Read(path), "中身", "中身");
}

KACHA_V2_TEST(atomic, 保存先が空なら断る)
{
    const auto report = WriteFileAtomically("", "中身");
    Require(!report.HasValue(), "書けたことにしない");
    RequireEqual(FirstCode(report.Diagnostics()), "IO-A004", "診断コード");
}

KACHA_V2_TEST(atomic, 無い場所へは書けないと言う)
{
    const std::string path = "/no/such/place/at/all/a.kcd2";
    const auto report = WriteFileAtomically(path, "中身");
    Require(!report.HasValue(), "書けたことにしない");
    RequireEqual(FirstCode(report.Diagnostics()), "IO-A001", "診断コード");
}

KACHA_V2_TEST(atomic, 空のファイルも書ける)
{
    const std::string directory = Workspace("empty");
    const std::string path = Join(directory, "a.kcd2");
    const auto report = WriteFileAtomically(path, "");
    Require(report.HasValue(), "書けること");
    Require(Exists(path), "ファイルがある");
    RequireEqual(Read(path), "", "中身は空");
}

KACHA_V2_TEST(atomic, 大きいファイルもそのまま戻る)
{
    const std::string directory = Workspace("big");
    const std::string path = Join(directory, "a.kcd2");
    std::string content;
    content.reserve(2 * 1024 * 1024);
    for (int index = 0; index < 2 * 1024 * 1024; ++index) {
        content.push_back(static_cast<char>(index % 251));
    }
    const auto report = WriteFileAtomically(path, content);
    Require(report.HasValue(), "書けること");
    Require(Read(path) == content, "1バイトも変わらない");
}

KACHA_V2_TEST(atomic, 0バイトを含む中身も壊れない)
{
    const std::string directory = Workspace("zeros");
    const std::string path = Join(directory, "a.kcd2");
    std::string content("ab\0cd\0\0ef", 9);
    const auto report = WriteFileAtomically(path, content);
    Require(report.HasValue(), "書けること");
    Require(Read(path) == content, "中身が同じ");
    RequireEqual(std::to_string(Read(path).size()), "9", "長さ");
}

KACHA_V2_TEST(atomic, 前回の一時ファイルが残っていても書ける)
{
    const std::string directory = Workspace("stale");
    const std::string path = Join(directory, "a.kcd2");
    WriteExisting(path, "本体");
    WriteExisting(path + ".tmp", "前回の残骸");
    const auto report = WriteFileAtomically(path, "新しい中身");
    Require(report.HasValue(), "書けること");
    RequireEqual(Read(path), "新しい中身", "中身");
    Require(!Exists(path + ".tmp"), "残骸は片づく");
}

KACHA_V2_TEST(atomic, 日本語のファイル名でも書ける)
{
    const std::string directory = Workspace("japanese");
    const std::string path = Join(directory, "車体_側面.kcd2");
    const auto report = WriteFileAtomically(path, "中身");
    Require(report.HasValue(), "書けること");
    Require(Exists(path), "ファイルがある");
    RequireEqual(Read(path), "中身", "中身");
}

// ---- 文書として保存し、読み直す ----

KACHA_V2_TEST(atomic, 保存した文書を読み直せる)
{
    using kachakacha::v2::io::DocumentFile;
    using kachakacha::v2::io::LoadDocument;
    using kachakacha::v2::io::SaveDocument;

    const std::string directory = Workspace("document");
    const std::string path = Join(directory, "a.kcd2");
    DocumentFile file;
    auto archive = SaveDocument(file);
    Require(archive.HasValue(), "書き出せること");
    const auto report = WriteFileAtomically(path, archive.Value());
    Require(report.HasValue(), "保存できること");

    auto content = ReadWholeFile(path);
    Require(content.HasValue(), "読めること");
    auto loaded = LoadDocument(content.Value());
    Require(loaded.HasValue(), "読み直せること");
}

KACHA_V2_TEST(atomic, 保存に失敗しても前の文書が読み直せる)
{
    using kachakacha::v2::io::DocumentFile;
    using kachakacha::v2::io::LoadDocument;
    using kachakacha::v2::io::SaveDocument;

    const std::string directory = Workspace("document_fail");
    const std::string path = Join(directory, "a.kcd2");
    DocumentFile first;
    auto archive = SaveDocument(first);
    Require(archive.HasValue(), "書き出せること");
    Require(WriteFileAtomically(path, archive.Value()).HasValue(), "1回目の保存");

    const auto failed = WriteFileAtomically(path, "こわれた中身", {},
        [](const std::string&, std::string_view, std::string& errorOut) {
            errorOut = "わざと失敗";
            return false;
        });
    Require(!failed.HasValue(), "2回目は失敗する");

    auto content = ReadWholeFile(path);
    Require(content.HasValue(), "読めること");
    auto loaded = LoadDocument(content.Value());
    Require(loaded.HasValue(), "前の文書がそのまま読み直せる");
}

KACHA_V2_TEST(atomic, 控えからも読み直せる)
{
    using kachakacha::v2::io::DocumentFile;
    using kachakacha::v2::io::LoadDocument;
    using kachakacha::v2::io::SaveDocument;

    const std::string directory = Workspace("document_bak");
    const std::string path = Join(directory, "a.kcd2");
    DocumentFile file;
    auto archive = SaveDocument(file);
    Require(archive.HasValue(), "書き出せること");
    Require(WriteFileAtomically(path, archive.Value()).HasValue(), "1回目");
    Require(WriteFileAtomically(path, archive.Value()).HasValue(), "2回目");

    auto backup = ReadWholeFile(path + ".bak");
    Require(backup.HasValue(), "控えが読めること");
    Require(LoadDocument(backup.Value()).HasValue(), "控えも文書として読める");
}


KACHA_V2_TEST(atomic, 日本語のフォルダ名でも書ける)
{
    // Windows では path(std::string) が ANSI になるため、
    // ここを文字列のまま扱えていないと日本語のフォルダで保存できない。
    const std::string directory = Workspace("nested");
    const std::string nested = directory + "/車両_下回り";
    std::error_code code;
    std::filesystem::create_directories(MakePath(nested), code);
    Require(!code, "フォルダが作れること");
    const std::string path = Join(nested, "側板.kcd2");
    const auto report = WriteFileAtomically(path, "中身");
    Require(report.HasValue(), "書けること");
    Require(Exists(path), "ファイルがある");
    RequireEqual(Read(path), "中身", "中身");
}

KACHA_V2_TEST(atomic, 日本語のファイル名でも控えが残る)
{
    const std::string directory = Workspace("jp_backup");
    const std::string path = Join(directory, "屋根板.kcd2");
    WriteExisting(path, "古い中身");
    const auto report = WriteFileAtomically(path, "新しい中身");
    Require(report.HasValue(), "書けること");
    RequireEqual(Read(path), "新しい中身", "本体");
    Require(Exists(path + ".bak"), "控えがある");
    RequireEqual(Read(path + ".bak"), "古い中身", "控えの中身");
}

KACHA_V2_TEST(atomic, パスの往復で文字が変わらない)
{
    // 区切り文字は環境で変わるが、名前の文字は1つも変わってはならない。
    const std::string original = "/tmp/車両/側板 A-1.kcd2";
    std::string roundTrip = FromPath(MakePath(original));
    for (char& character : roundTrip) {
        if (character == '\\') {
            character = '/';
        }
    }
    RequireEqual(roundTrip, original, "往復しても文字が変わらない");
}

KACHA_V2_TEST_MAIN("atomic_file_tests")
