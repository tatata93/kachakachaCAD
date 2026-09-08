// .kcd2 の入れ物になる ZIP。壊れたファイルを掴まされても黙って通さないことを、多数の入力で押さえる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/Zip.h"

#include <algorithm>
#include <string>
#include <vector>

using kachakacha::v2::io::Crc32;
using kachakacha::v2::io::IsSafeZipEntryPath;
using kachakacha::v2::io::ReadZip;
using kachakacha::v2::io::WriteZip;
using kachakacha::v2::io::ZipEntry;
using kachakacha::v2::io::ZipLimits;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

//! 数の比較。土台の RequireEqual は文字列だけなので、ここで文字列にして渡す。
void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

void RequireNumber(long long actual, long long expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

std::string MakeArchive(const std::vector<ZipEntry>& entries, const std::string& why)
{
    const auto written = WriteZip(entries);
    Require(written.HasValue(), "書き出せること: " + why);
    return written.Value();
}

void RequireReadFails(std::string_view archive, const std::string& expectedCode,
    const std::string& why)
{
    const auto read = ReadZip(archive);
    Require(!read.HasValue(), "読み込みが断られること: " + why);
    Require(!read.Diagnostics().empty(), "断り方に診断が付くこと: " + why);
    RequireEqual(read.Diagnostics().front().code, expectedCode, "診断コード: " + why);
}

//! 終端レコード(EOCD)の位置。書き出したものは必ずコメント無しなので末尾22バイト。
std::size_t EndOfCentralOffset(const std::string& archive)
{
    Require(archive.size() >= 22, "終端レコードが入る長さがあること");
    return archive.size() - 22;
}

std::uint32_t ReadU32(const std::string& data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     static_cast<unsigned char>(data[offset + static_cast<std::size_t>(index)]))
            << (8 * index);
    }
    return value;
}

std::uint16_t ReadU16(const std::string& data, std::size_t offset)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset])
        | (static_cast<unsigned char>(data[offset + 1]) << 8));
}

void WriteU32(std::string& data, std::size_t offset, std::uint32_t value)
{
    for (int index = 0; index < 4; ++index) {
        data[offset + static_cast<std::size_t>(index)] =
            static_cast<char>((value >> (8 * index)) & 0xFF);
    }
}

void WriteU16(std::string& data, std::size_t offset, std::uint16_t value)
{
    data[offset] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

//! 中央ディレクトリの先頭位置(EOCD が持っている値をそのまま読む)。
std::size_t CentralStart(const std::string& archive)
{
    return ReadU32(archive, EndOfCentralOffset(archive) + 16);
}

const ZipEntry kDocument{"document.json", "{\"schema\":\"kcd2/1\"}"};

} // namespace

// ---------------------------------------------------------------- 名前の検査

KACHA_V2_TEST(zip, 使える名前を受け入れる)
{
    const char* accepted[] = {
        "document.json",
        "thumbnail.png",
        "parts/part-1.json",
        "a/b/c/d/e/f.bin",
        "名前に日本語.json",
        "with space.txt",
        "dot.in.name.json",
        "..hidden-but-not-dotdot.json",
        "a..b/c.json",
        "x",
    };
    for (const char* path : accepted) {
        Require(IsSafeZipEntryPath(path), std::string("使えるはず: ") + path);
    }
}

KACHA_V2_TEST(zip, 危ない名前を全部断る)
{
    // ZIP Slip とその近所。1つでも通ると、開いただけで外のファイルを壊せてしまう。
    const std::vector<std::string> rejected{
        "",                       // 空
        "/etc/passwd",            // 絶対path
        "/",                      // 絶対pathの根
        "C:/windows/system.ini",  // ドライブ文字
        "c:relative.txt",         // ドライブ文字(相対形)
        "..",                     // 親そのもの
        "../outside.json",        // 1つ上へ
        "a/../../outside.json",   // 潜ってから上へ
        "a/..",                   // 末尾が親
        "./document.json",        // カレント指定
        "a/./b.json",             // 途中のカレント
        ".",                      // カレントそのもの
        "dir\\file.json",         // 逆スラッシュ
        "..\\..\\outside.json",   // Windows流の親
        "a//b.json",              // 空の区間
        "/leading.json",          // 先頭スラッシュ
        "trailing/",              // ディレクトリentry
        "a/",                     // ディレクトリentry(短い)
        std::string("nul\0inside.json", 15), // NUL埋め込み
        "tab\there.json",         // 制御文字
        "line\nbreak.json",       // 改行
        std::string(1025, 'x'),   // 長すぎる
    };
    for (const std::string& path : rejected) {
        Require(!IsSafeZipEntryPath(path), "断るはず: [" + path + "]");
    }
}

KACHA_V2_TEST(zip, 名前の検査は読み書きの両方に効く)
{
    const auto written = WriteZip({ZipEntry{"../outside.json", "x"}});
    Require(!written.HasValue(), "危ない名前では書き出さない");
    RequireEqual(written.Diagnostics().front().code, "KCD2-Z001", "書き出し側の診断コード");

    // 読み込み側は、他のソフトが作った書庫を掴まされる場面。名前を後から差し替えて試す。
    std::string archive = MakeArchive({ZipEntry{"aaaaaaaaaaaaaa.json", "x"}}, "差し替えの土台");
    const std::string bad = "../outside.jsonxxxx";
    RequireCount(bad.size(), std::size_t{19}, "差し替え前後で長さが同じであること");
    const std::size_t position = archive.find("aaaaaaaaaaaaaa.json");
    Require(position != std::string::npos, "名前が見つかること");
    // ローカルヘッダと中央ディレクトリの両方を、同じ長さの別名で置き換える。
    std::string replaced;
    std::size_t cursor = 0;
    while (true) {
        const std::size_t found = archive.find("aaaaaaaaaaaaaa.json", cursor);
        if (found == std::string::npos) {
            replaced += archive.substr(cursor);
            break;
        }
        replaced += archive.substr(cursor, found - cursor);
        replaced += bad; // 19文字。元と同じ長さ
        cursor = found + 19;
    }
    RequireCount(replaced.size(), archive.size(), "書庫の長さが変わっていないこと");
    const auto read = ReadZip(replaced);
    Require(!read.HasValue(), "危ない名前の書庫は開かない");
    RequireEqual(read.Diagnostics().front().code, "KCD2-Z001", "読み込み側の診断コード");
}

// ---------------------------------------------------------------- 往復

KACHA_V2_TEST(zip, 1件を往復できる)
{
    const std::string archive = MakeArchive({kDocument}, "1件");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().size(), std::size_t{1}, "件数");
    RequireEqual(read.Value()[0].path, kDocument.path, "名前");
    RequireEqual(read.Value()[0].data, kDocument.data, "中身");
}

KACHA_V2_TEST(zip, 複数件を順番どおりに往復できる)
{
    const std::vector<ZipEntry> entries{
        kDocument,
        ZipEntry{"parts/part-1.json", "{\"id\":1}"},
        ZipEntry{"parts/part-2.json", "{\"id\":2}"},
        ZipEntry{"thumbnail.png", std::string("\x89PNG\r\n\x1a\n", 8)},
        ZipEntry{"名前に日本語.json", "{\"note\":\"日本語の中身\"}"},
    };
    const std::string archive = MakeArchive(entries, "5件");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().size(), entries.size(), "件数");
    for (std::size_t index = 0; index < entries.size(); ++index) {
        RequireEqual(read.Value()[index].path, entries[index].path,
            "名前 " + std::to_string(index));
        RequireEqual(read.Value()[index].data, entries[index].data,
            "中身 " + std::to_string(index));
    }
}

KACHA_V2_TEST(zip, 空の中身も往復できる)
{
    const std::vector<ZipEntry> entries{
        ZipEntry{"empty.json", ""},
        ZipEntry{"after-empty.json", "x"},
        ZipEntry{"also-empty.bin", ""},
    };
    const std::string archive = MakeArchive(entries, "空の中身");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().size(), std::size_t{3}, "件数");
    RequireEqual(read.Value()[0].data, std::string(), "1件目は空");
    RequireEqual(read.Value()[2].data, std::string(), "3件目は空");
}

KACHA_V2_TEST(zip, 書庫そのものが空でも往復できる)
{
    const std::string archive = MakeArchive({}, "0件");
    RequireCount(archive.size(), std::size_t{22}, "終端レコードだけの長さ");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    Require(read.Value().empty(), "0件であること");
}

KACHA_V2_TEST(zip, 生のバイト列を壊さずに往復できる)
{
    // 0x00 から 0xFF まで全部入れる。テキスト扱いされていたらここで落ちる。
    std::string all;
    all.reserve(256 * 4);
    for (int repeat = 0; repeat < 4; ++repeat) {
        for (int value = 0; value < 256; ++value) {
            all.push_back(static_cast<char>(value));
        }
    }
    const std::string archive = MakeArchive({ZipEntry{"raw.bin", all}}, "全バイト");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value()[0].data.size(), all.size(), "長さ");
    Require(read.Value()[0].data == all, "1バイトも変わっていないこと");
}

KACHA_V2_TEST(zip, 大きめの中身も往復できる)
{
    std::string big;
    big.reserve(300000);
    for (int index = 0; index < 300000; ++index) {
        big.push_back(static_cast<char>('a' + (index % 26)));
    }
    const std::string archive = MakeArchive(
        {kDocument, ZipEntry{"big.bin", big}, ZipEntry{"tail.json", "{}"}}, "300KB");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().size(), std::size_t{3}, "件数");
    Require(read.Value()[1].data == big, "大きい中身が一致すること");
    RequireEqual(read.Value()[2].data, std::string("{}"), "後ろのentryも読めること");
}

KACHA_V2_TEST(zip, 深い階層の名前も往復できる)
{
    std::vector<ZipEntry> entries;
    std::string path = "a";
    for (int depth = 0; depth < 20; ++depth) {
        path += "/b";
        entries.push_back(ZipEntry{path + ".json", "{\"depth\":" + std::to_string(depth) + "}"});
    }
    const std::string archive = MakeArchive(entries, "深い階層");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().size(), entries.size(), "件数");
    RequireEqual(read.Value().back().path, entries.back().path, "一番深い名前");
}

KACHA_V2_TEST(zip, 件数が多くても往復できる)
{
    std::vector<ZipEntry> entries;
    entries.reserve(500);
    for (int index = 0; index < 500; ++index) {
        entries.push_back(ZipEntry{"parts/part-" + std::to_string(index) + ".json",
            "{\"id\":" + std::to_string(index) + "}"});
    }
    const std::string archive = MakeArchive(entries, "500件");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().size(), std::size_t{500}, "件数");
    RequireEqual(read.Value()[499].data, std::string("{\"id\":499}"), "最後のentry");
}

// ---------------------------------------------------------------- 決定性

KACHA_V2_TEST(zip, 同じ入力からは必ず同じバイト列が出る)
{
    const std::vector<ZipEntry> entries{
        kDocument,
        ZipEntry{"parts/part-1.json", "{\"id\":1}"},
        ZipEntry{"名前に日本語.json", "日本語"},
    };
    const std::string first = MakeArchive(entries, "1回目");
    const std::string second = MakeArchive(entries, "2回目");
    const std::string third = MakeArchive(entries, "3回目");
    Require(first == second, "2回目が一致すること");
    Require(second == third, "3回目が一致すること");
    // 時刻が混ざっていないことを、値そのもので押さえる。
    RequireNumber(static_cast<int>(ReadU16(first, 10)), 0, "時刻は固定値0");
    RequireNumber(static_cast<int>(ReadU16(first, 12)), 0x21, "日付は固定値0x21");
}

KACHA_V2_TEST(zip, 往復してもう一度書くと同じバイト列になる)
{
    const std::vector<ZipEntry> entries{
        kDocument,
        ZipEntry{"parts/part-1.json", "{\"id\":1}"},
        ZipEntry{"thumbnail.png", std::string("\x89PNG\r\n\x1a\n\0\0", 10)},
    };
    const std::string first = MakeArchive(entries, "1回目");
    const auto read = ReadZip(first);
    Require(read.HasValue(), "読めること");
    const std::string again = MakeArchive(read.Value(), "読んだものを書き直す");
    Require(first == again, "バイト単位で一致すること");
}

KACHA_V2_TEST(zip, 中身が1バイト違えば書庫も違う)
{
    const std::string first = MakeArchive({ZipEntry{"a.json", "{\"v\":1}"}}, "元");
    const std::string second = MakeArchive({ZipEntry{"a.json", "{\"v\":2}"}}, "1バイト違い");
    Require(first != second, "違うバイト列になること");
    RequireCount(first.size(), second.size(), "長さは同じであること");
}

// ---------------------------------------------------------------- 重複

KACHA_V2_TEST(zip, 同じ名前を2つ入れようとすると断る)
{
    const auto written = WriteZip({kDocument, ZipEntry{"document.json", "{}"}});
    Require(!written.HasValue(), "書き出さないこと");
    RequireEqual(written.Diagnostics().front().code, "KCD2-Z002", "診断コード");
    Require(written.Diagnostics().front().detailsJa.find("document.json") != std::string::npos,
        "どの名前かを言うこと");
}

KACHA_V2_TEST(zip, 同じ名前が2つ入った書庫は開かない)
{
    // 2件の書庫を作り、2件目の名前を1件目と同じにする。長さは合わせる。
    std::string archive = MakeArchive(
        {ZipEntry{"aaa.json", "1"}, ZipEntry{"bbb.json", "2"}}, "土台");
    std::string replaced;
    std::size_t cursor = 0;
    while (true) {
        const std::size_t found = archive.find("bbb.json", cursor);
        if (found == std::string::npos) {
            replaced += archive.substr(cursor);
            break;
        }
        replaced += archive.substr(cursor, found - cursor);
        replaced += "aaa.json";
        cursor = found + 8;
    }
    RequireReadFails(replaced, "KCD2-Z002", "同名2件");
}

// ---------------------------------------------------------------- 壊れた入力

KACHA_V2_TEST(zip, ZIPでないものを断る)
{
    RequireReadFails("", "KCD2-Z007", "空のファイル");
    RequireReadFails("x", "KCD2-Z007", "1バイト");
    RequireReadFails("not a zip file at all, just text", "KCD2-Z007", "ただの文章");
    RequireReadFails(std::string(200, '\0'), "KCD2-Z007", "NULだけ200バイト");
    RequireReadFails(std::string("\x89PNG\r\n\x1a\n", 8) + std::string(100, 'x'),
        "KCD2-Z007", "PNGを渡された");
}

KACHA_V2_TEST(zip, 終端レコードを壊すと断る)
{
    std::string archive = MakeArchive({kDocument}, "土台");
    const std::size_t end = EndOfCentralOffset(archive);
    archive[end] = 'X';
    RequireReadFails(archive, "KCD2-Z007", "終端の署名が壊れている");
}

KACHA_V2_TEST(zip, 後ろが切れた書庫を断る)
{
    const std::string archive = MakeArchive(
        {kDocument, ZipEntry{"parts/part-1.json", std::string(500, 'x')}}, "土台");
    // 末尾から少しずつ削る。どこで切っても、黙って通してはいけない。
    for (std::size_t cut = 1; cut < 60; ++cut) {
        const std::string broken = archive.substr(0, archive.size() - cut);
        const auto read = ReadZip(broken);
        Require(!read.HasValue(), "末尾 " + std::to_string(cut) + " バイト欠けを断ること");
    }
}

KACHA_V2_TEST(zip, 前が切れた書庫を断る)
{
    const std::string archive = MakeArchive(
        {kDocument, ZipEntry{"parts/part-1.json", std::string(500, 'x')}}, "土台");
    for (std::size_t cut : {std::size_t{1}, std::size_t{16}, std::size_t{64},
             std::size_t{200}}) {
        const std::string broken = archive.substr(cut);
        const auto read = ReadZip(broken);
        Require(!read.HasValue(), "先頭 " + std::to_string(cut) + " バイト欠けを断ること");
    }
}

KACHA_V2_TEST(zip, 目録の位置が書庫の外を指していたら断る)
{
    std::string archive = MakeArchive({kDocument}, "土台");
    WriteU32(archive, EndOfCentralOffset(archive) + 16, 0xFFFFFF00U);
    RequireReadFails(archive, "KCD2-Z003", "目録の位置が外");
}

KACHA_V2_TEST(zip, 目録の大きさが嘘なら断る)
{
    std::string archive = MakeArchive({kDocument}, "土台");
    WriteU32(archive, EndOfCentralOffset(archive) + 12, 0xFFFF0000U);
    RequireReadFails(archive, "KCD2-Z003", "目録の大きさが外");
}

KACHA_V2_TEST(zip, 目録の件数が実際より多ければ断る)
{
    std::string archive = MakeArchive({kDocument}, "土台");
    const std::size_t end = EndOfCentralOffset(archive);
    WriteU16(archive, end + 8, 5);
    WriteU16(archive, end + 10, 5);
    RequireReadFails(archive, "KCD2-Z003", "件数が多すぎる");
}

KACHA_V2_TEST(zip, 目録の署名が壊れていたら断る)
{
    std::string archive = MakeArchive({kDocument}, "土台");
    archive[CentralStart(archive)] = 'X';
    RequireReadFails(archive, "KCD2-Z003", "目録の署名");
}

KACHA_V2_TEST(zip, 中身の位置が正しくなければ断る)
{
    std::string archive = MakeArchive({kDocument}, "土台");
    // 中央ディレクトリの「ローカルヘッダの位置」を書庫の外へ向ける。
    WriteU32(archive, CentralStart(archive) + 42, 0xFFFFFF00U);
    RequireReadFails(archive, "KCD2-Z003", "位置が外");

    std::string other = MakeArchive({kDocument}, "土台2");
    // 書庫の中だが、そこにローカルヘッダは無い位置。
    WriteU32(other, CentralStart(other) + 42, 4);
    RequireReadFails(other, "KCD2-Z003", "位置がずれている");
}

KACHA_V2_TEST(zip, 中身が途中で切れていたら断る)
{
    std::string archive = MakeArchive({ZipEntry{"a.json", "0123456789"}}, "土台");
    // 目録が言う長さを、実際より大きくする(CRCも合わせておかないと別の理由で落ちる)。
    const std::size_t central = CentralStart(archive);
    WriteU32(archive, central + 20, 100000);
    WriteU32(archive, central + 24, 100000);
    RequireReadFails(archive, "KCD2-Z003", "中身が足りない");
}

KACHA_V2_TEST(zip, 検査値が合わなければ断る)
{
    std::string archive = MakeArchive({ZipEntry{"a.json", "0123456789"}}, "土台");
    const std::size_t position = archive.find("0123456789");
    Require(position != std::string::npos, "中身が見つかること");
    archive[position + 3] = 'X'; // 1バイトだけ書き換える
    RequireReadFails(archive, "KCD2-Z005", "中身が1バイト化けた");
}

KACHA_V2_TEST(zip, 検査値の側を書き換えても断る)
{
    std::string archive = MakeArchive({ZipEntry{"a.json", "0123456789"}}, "土台");
    WriteU32(archive, CentralStart(archive) + 16, 0xDEADBEEFU);
    RequireReadFails(archive, "KCD2-Z005", "検査値が違う");
}

KACHA_V2_TEST(zip, 途中のentryが壊れていても最後まで見て断る)
{
    std::string archive = MakeArchive(
        {ZipEntry{"a.json", "aaaa"}, ZipEntry{"b.json", "bbbb"}, ZipEntry{"c.json", "cccc"}},
        "3件");
    const std::size_t position = archive.find("bbbb");
    Require(position != std::string::npos, "2件目の中身が見つかること");
    archive[position] = 'X';
    const auto read = ReadZip(archive);
    Require(!read.HasValue(), "1件でも壊れていれば全体を断ること");
    RequireEqual(read.Diagnostics().front().code, "KCD2-Z005", "診断コード");
    Require(read.Diagnostics().front().detailsJa.find("b.json") != std::string::npos,
        "どのファイルが壊れているかを言うこと");
}

KACHA_V2_TEST(zip, 圧縮されたentryは対応していないと言って断る)
{
    std::string archive = MakeArchive({ZipEntry{"a.json", "0123456789"}}, "土台");
    WriteU16(archive, CentralStart(archive) + 10, 8); // deflate
    const auto read = ReadZip(archive);
    Require(!read.HasValue(), "断ること");
    RequireEqual(read.Diagnostics().front().code, "KCD2-Z004", "診断コード");
    Require(read.Diagnostics().front().detailsJa.find("a.json") != std::string::npos,
        "どのファイルかを言うこと");

    // 他の方式も同じ扱い。黙って中身らしきものを返してはいけない。
    for (std::uint16_t method : {std::uint16_t{1}, std::uint16_t{9}, std::uint16_t{12},
             std::uint16_t{14}, std::uint16_t{93}, std::uint16_t{99}}) {
        std::string other = MakeArchive({ZipEntry{"a.json", "0123456789"}}, "土台");
        WriteU16(other, CentralStart(other) + 10, method);
        RequireReadFails(other, "KCD2-Z004", "方式 " + std::to_string(method));
    }
}

KACHA_V2_TEST(zip, 1バイト書き換えを総当たりしても素通りしない)
{
    // 小さな書庫を作り、全バイトを1つずつ壊して、どこを壊しても
    // 「読めた上に中身が違う」が起きないことを確かめる。
    const std::vector<ZipEntry> entries{
        ZipEntry{"a.json", "{\"v\":1}"}, ZipEntry{"b.json", "{\"v\":2}"}};
    const std::string archive = MakeArchive(entries, "土台");
    int silentlyWrong = 0;
    for (std::size_t index = 0; index < archive.size(); ++index) {
        std::string broken = archive;
        broken[index] = static_cast<char>(broken[index] ^ 0x5A);
        const auto read = ReadZip(broken);
        if (!read.HasValue()) {
            continue;
        }
        const auto& got = read.Value();
        bool same = got.size() == entries.size();
        for (std::size_t item = 0; same && item < got.size(); ++item) {
            same = got[item].path == entries[item].path && got[item].data == entries[item].data;
        }
        if (!same) {
            ++silentlyWrong;
        }
    }
    RequireNumber(silentlyWrong, 0, "壊れた書庫を黙って別物として返さないこと");
}

KACHA_V2_TEST(zip, 目録と中身で名前が食い違えば断る)
{
    // ZIPは名前に検査値を持たない。ここを見ていないと、化けた名前がそのまま通る。
    std::string archive = MakeArchive({ZipEntry{"a.json", "x"}, ZipEntry{"b.json", "y"}}, "土台");
    // 目録側だけを書き換える(中央ディレクトリは後ろにあるので、後ろの出現を狙う)。
    const std::size_t central = CentralStart(archive);
    const std::size_t position = archive.find("a.json", central);
    Require(position != std::string::npos, "目録側の名前が見つかること");
    archive[position] = 'z';
    RequireReadFails(archive, "KCD2-Z008", "目録側の名前が化けた");

    // 中身側だけを書き換えても同じ。
    std::string other = MakeArchive({ZipEntry{"a.json", "x"}, ZipEntry{"b.json", "y"}}, "土台2");
    const std::size_t localPosition = other.find("a.json");
    Require(localPosition < CentralStart(other), "中身側の名前が先に来ること");
    other[localPosition] = 'z';
    RequireReadFails(other, "KCD2-Z008", "中身側の名前が化けた");
}

// ---------------------------------------------------------------- 上限

KACHA_V2_TEST(zip, 件数の上限を超えたら断る)
{
    std::vector<ZipEntry> entries;
    for (int index = 0; index < 40; ++index) {
        entries.push_back(ZipEntry{"f" + std::to_string(index) + ".json", "{}"});
    }
    const std::string archive = MakeArchive(entries, "40件");
    ZipLimits limits;
    limits.maximumEntryCount = 10;
    const auto read = ReadZip(archive, limits);
    Require(!read.HasValue(), "断ること");
    RequireEqual(read.Diagnostics().front().code, "KCD2-Z006", "診断コード");

    limits.maximumEntryCount = 40;
    Require(ReadZip(archive, limits).HasValue(), "ちょうど上限なら読めること");
}

KACHA_V2_TEST(zip, 1件が大きすぎたら断る)
{
    const std::string archive = MakeArchive({ZipEntry{"big.bin", std::string(5000, 'x')}}, "5000B");
    ZipLimits limits;
    limits.maximumEntryBytes = 1000;
    const auto read = ReadZip(archive, limits);
    Require(!read.HasValue(), "断ること");
    RequireEqual(read.Diagnostics().front().code, "KCD2-Z006", "診断コード");
    Require(read.Diagnostics().front().detailsJa.find("big.bin") != std::string::npos,
        "どのファイルかを言うこと");

    limits.maximumEntryBytes = 5000;
    Require(ReadZip(archive, limits).HasValue(), "ちょうど上限なら読めること");
}

KACHA_V2_TEST(zip, 合計が大きすぎたら断る)
{
    std::vector<ZipEntry> entries;
    for (int index = 0; index < 10; ++index) {
        entries.push_back(ZipEntry{"f" + std::to_string(index) + ".bin", std::string(1000, 'x')});
    }
    const std::string archive = MakeArchive(entries, "合計10000B");
    ZipLimits limits;
    limits.maximumEntryBytes = 100000;
    limits.maximumTotalBytes = 5000;
    const auto read = ReadZip(archive, limits);
    Require(!read.HasValue(), "断ること");
    RequireEqual(read.Diagnostics().front().code, "KCD2-Z006", "診断コード");

    limits.maximumTotalBytes = 10000;
    Require(ReadZip(archive, limits).HasValue(), "ちょうど上限なら読めること");
}

KACHA_V2_TEST(zip, 展開すると膨れる細工を断る)
{
    // 「圧縮後は小さいが展開すると巨大」と名乗る細工(zip bomb)。
    std::string archive = MakeArchive({ZipEntry{"a.bin", std::string(10, 'x')}}, "土台");
    const std::size_t central = CentralStart(archive);
    WriteU32(archive, central + 20, 10);        // 圧縮後 10 バイト
    WriteU32(archive, central + 24, 10000000);  // 展開後 1000万バイトと名乗る
    ZipLimits limits;
    limits.maximumEntryBytes = 100000000;
    limits.maximumTotalBytes = 100000000;
    limits.maximumExpansionRatio = 1000.0;
    const auto read = ReadZip(archive, limits);
    Require(!read.HasValue(), "断ること");
    RequireEqual(read.Diagnostics().front().code, "KCD2-Z006", "診断コード");
}

KACHA_V2_TEST(zip, 既定の上限では普通の文書が通る)
{
    std::vector<ZipEntry> entries{kDocument};
    for (int index = 0; index < 200; ++index) {
        entries.push_back(ZipEntry{"parts/part-" + std::to_string(index) + ".json",
            std::string(2000, 'p')});
    }
    const std::string archive = MakeArchive(entries, "普通の大きさ");
    Require(ReadZip(archive).HasValue(), "既定の上限で読めること");
}

// ---------------------------------------------------------------- CRC32

KACHA_V2_TEST(zip, 検査値が既知の値と一致する)
{
    // ZIPが使う CRC-32(IEEE)。他のソフトと同じ値でなければ相互に読めない。
    RequireNumber(static_cast<long long>(Crc32("")), 0LL, "空文字列");
    RequireNumber(static_cast<long long>(Crc32("a")), 0xE8B7BE43LL, "\"a\"");
    RequireNumber(static_cast<long long>(Crc32("abc")), 0x352441C2LL, "\"abc\"");
    RequireNumber(static_cast<long long>(Crc32("123456789")), 0xCBF43926LL, "\"123456789\"");
    RequireNumber(static_cast<long long>(Crc32("The quick brown fox jumps over the lazy dog")),
        0x414FA339LL, "定番の文");
    RequireNumber(static_cast<long long>(Crc32(std::string(32, '\0'))), 0x190A55ADLL,
        "NUL 32個");
}

KACHA_V2_TEST(zip, 検査値は1バイトの違いを見逃さない)
{
    const std::string base(64, 'x');
    const std::uint32_t expected = Crc32(base);
    for (std::size_t index = 0; index < base.size(); ++index) {
        std::string changed = base;
        changed[index] = 'y';
        Require(Crc32(changed) != expected,
            std::to_string(index) + " バイト目の違いを見分けること");
    }
}

// ---------------------------------------------------------------- 実際の使われ方

KACHA_V2_TEST(zip, kcd2の中身らしい構成で往復できる)
{
    const std::vector<ZipEntry> entries{
        ZipEntry{"mimetype", "application/vnd.kachakacha.kcd2"},
        ZipEntry{"document.json", "{\"schema\":\"kcd2/1\",\"entities\":[]}"},
        ZipEntry{"history.json", "{\"commands\":[]}"},
        ZipEntry{"thumbnail.png", std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, '\0')},
        ZipEntry{"parts/part-0001.json", "{\"name\":\"側板\"}"},
        ZipEntry{"parts/part-0002.json", "{\"name\":\"屋根\"}"},
    };
    const std::string archive = MakeArchive(entries, "kcd2らしい構成");
    const auto read = ReadZip(archive);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().size(), entries.size(), "件数");
    // 最初のentryが mimetype であること。他のソフトが種類を判別できるように。
    RequireEqual(read.Value()[0].path, std::string("mimetype"), "先頭は mimetype");
    for (std::size_t index = 0; index < entries.size(); ++index) {
        RequireEqual(read.Value()[index].data, entries[index].data,
            "中身 " + entries[index].path);
    }
}

KACHA_V2_TEST(zip, 保存し直しても中身が育たない)
{
    // 開いて保存を繰り返しても、ファイルが太らないこと。
    std::vector<ZipEntry> entries{kDocument, ZipEntry{"parts/part-1.json", "{\"id\":1}"}};
    std::string archive = MakeArchive(entries, "1回目");
    const std::size_t firstSize = archive.size();
    for (int round = 0; round < 5; ++round) {
        const auto read = ReadZip(archive);
        Require(read.HasValue(), "読めること " + std::to_string(round));
        archive = MakeArchive(read.Value(), "書き直し " + std::to_string(round));
        RequireCount(archive.size(), firstSize, "大きさが変わらないこと " + std::to_string(round));
    }
}

KACHA_V2_TEST_MAIN("zip_tests")
