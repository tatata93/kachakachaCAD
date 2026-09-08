#include "kachakacha/io/Zip.h"

#include <array>
#include <cstring>
#include <set>

namespace kachakacha::v2::io {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kBadPath = "KCD2-Z001";
constexpr const char* kDuplicate = "KCD2-Z002";
constexpr const char* kTruncated = "KCD2-Z003";
constexpr const char* kUnsupported = "KCD2-Z004";
constexpr const char* kChecksum = "KCD2-Z005";
constexpr const char* kTooLarge = "KCD2-Z006";
constexpr const char* kNotZip = "KCD2-Z007";
constexpr const char* kNameMismatch = "KCD2-Z008";

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50;
constexpr std::uint32_t kEndOfCentralSignature = 0x06054b50;

void PutU16(std::string& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void PutU32(std::string& out, std::uint32_t value)
{
    for (int index = 0; index < 4; ++index) {
        out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
    }
}

[[nodiscard]] std::uint16_t GetU16(std::string_view data, std::size_t offset)
{
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(data[offset])
        | (static_cast<unsigned char>(data[offset + 1]) << 8));
}

[[nodiscard]] std::uint32_t GetU32(std::string_view data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     static_cast<unsigned char>(data[offset + static_cast<std::size_t>(index)]))
            << (8 * index);
    }
    return value;
}

[[nodiscard]] const std::array<std::uint32_t, 256>& CrcTable()
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> made{};
        for (std::uint32_t index = 0; index < 256; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1U) ? (0xEDB88320U ^ (value >> 1)) : (value >> 1);
            }
            made[index] = value;
        }
        return made;
    }();
    return table;
}

} // namespace

bool IsSafeZipEntryPath(std::string_view path)
{
    if (path.empty() || path.size() > 1024) {
        return false;
    }
    if (path.front() == '/' ) {
        return false; // 絶対path
    }
    if (path.size() >= 2 && path[1] == ':') {
        return false; // ドライブ文字
    }
    if (path.back() == '/') {
        return false; // ディレクトリentryは使わない
    }
    for (const char character : path) {
        if (character == '\\' || character == '\0') {
            return false;
        }
        if (static_cast<unsigned char>(character) < 0x20) {
            return false;
        }
    }
    // `..` を含む区間を拒否する。`//` のような空の区間も。
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view part = path.substr(start,
            slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

std::uint32_t Crc32(std::string_view data)
{
    const auto& table = CrcTable();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const char raw : data) {
        crc = table[(crc ^ static_cast<unsigned char>(raw)) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

Result<std::string> WriteZip(const std::vector<ZipEntry>& entries)
{
    std::set<std::string> seen;
    for (const ZipEntry& entry : entries) {
        if (!IsSafeZipEntryPath(entry.path)) {
            return Result<std::string>::Failure(MakeError(kBadPath,
                "ファイル名として使えないものが混ざっています。", entry.path));
        }
        if (!seen.insert(entry.path).second) {
            return Result<std::string>::Failure(MakeError(kDuplicate,
                "同じ名前のファイルが2つあります。", entry.path));
        }
        if (entry.data.size() > 0xFFFFFFFFull) {
            return Result<std::string>::Failure(MakeError(kTooLarge,
                "ファイルが大きすぎます(4GiB以上)。", entry.path));
        }
    }

    std::string archive;
    struct Placed {
        std::string path;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };
    std::vector<Placed> placed;
    placed.reserve(entries.size());

    for (const ZipEntry& entry : entries) {
        Placed record;
        record.path = entry.path;
        record.crc = Crc32(entry.data);
        record.size = static_cast<std::uint32_t>(entry.data.size());
        record.offset = static_cast<std::uint32_t>(archive.size());

        PutU32(archive, kLocalHeaderSignature);
        PutU16(archive, 20);   // 必要な版
        PutU16(archive, 1 << 11); // UTF-8のファイル名
        PutU16(archive, 0);    // 格納(無圧縮)
        PutU16(archive, 0);    // 時刻。決定的にするため固定
        PutU16(archive, 0x21); // 日付。1980-01-01 固定
        PutU32(archive, record.crc);
        PutU32(archive, record.size);
        PutU32(archive, record.size);
        PutU16(archive, static_cast<std::uint16_t>(entry.path.size()));
        PutU16(archive, 0);
        archive += entry.path;
        archive += entry.data;
        placed.push_back(std::move(record));
    }

    const std::uint32_t centralStart = static_cast<std::uint32_t>(archive.size());
    for (const Placed& record : placed) {
        PutU32(archive, kCentralHeaderSignature);
        PutU16(archive, 20);   // 作った版
        PutU16(archive, 20);   // 必要な版
        PutU16(archive, 1 << 11);
        PutU16(archive, 0);
        PutU16(archive, 0);
        PutU16(archive, 0x21);
        PutU32(archive, record.crc);
        PutU32(archive, record.size);
        PutU32(archive, record.size);
        PutU16(archive, static_cast<std::uint16_t>(record.path.size()));
        PutU16(archive, 0);    // extra
        PutU16(archive, 0);    // comment
        PutU16(archive, 0);    // disk
        PutU16(archive, 0);    // internal attributes
        PutU32(archive, 0);    // external attributes
        PutU32(archive, record.offset);
        archive += record.path;
    }
    const std::uint32_t centralSize =
        static_cast<std::uint32_t>(archive.size()) - centralStart;

    PutU32(archive, kEndOfCentralSignature);
    PutU16(archive, 0);
    PutU16(archive, 0);
    PutU16(archive, static_cast<std::uint16_t>(placed.size()));
    PutU16(archive, static_cast<std::uint16_t>(placed.size()));
    PutU32(archive, centralSize);
    PutU32(archive, centralStart);
    PutU16(archive, 0);
    return Result<std::string>::Success(std::move(archive));
}

Result<std::vector<ZipEntry>> ReadZip(std::string_view archive, const ZipLimits& limits)
{
    using Entries = std::vector<ZipEntry>;
    if (archive.size() < 22) {
        return Result<Entries>::Failure(MakeError(kNotZip,
            "ZIPとして短すぎます。", "ファイルが壊れているか、別の形式です。"));
    }
    // 終端レコードを後ろから探す(コメントが付いていても見つけられるように)。
    std::size_t end = std::string_view::npos;
    const std::size_t lowest = archive.size() >= 66000 ? archive.size() - 66000 : 0;
    for (std::size_t index = archive.size() - 22 + 1; index-- > lowest;) {
        if (GetU32(archive, index) == kEndOfCentralSignature) {
            end = index;
            break;
        }
    }
    if (end == std::string_view::npos) {
        return Result<Entries>::Failure(MakeError(kNotZip,
            "ZIPの終端が見つかりません。", "ファイルが壊れています。"));
    }
    const std::uint16_t count = GetU16(archive, end + 10);
    const std::uint32_t centralSize = GetU32(archive, end + 12);
    const std::uint32_t centralStart = GetU32(archive, end + 16);
    if (count > limits.maximumEntryCount) {
        return Result<Entries>::Failure(MakeError(kTooLarge,
            "ファイルの数が多すぎます。", std::to_string(count) + " 個"));
    }
    if (static_cast<std::uint64_t>(centralStart) + centralSize > archive.size()) {
        return Result<Entries>::Failure(MakeError(kTruncated,
            "ZIPの目録が壊れています。", {}));
    }

    Entries entries;
    std::set<std::string> seen;
    std::uint64_t total = 0;
    std::size_t cursor = centralStart;
    for (std::uint16_t index = 0; index < count; ++index) {
        if (cursor + 46 > archive.size() || GetU32(archive, cursor) != kCentralHeaderSignature) {
            return Result<Entries>::Failure(MakeError(kTruncated,
                "ZIPの目録が途中で切れています。", std::to_string(index) + " 件目"));
        }
        const std::uint16_t method = GetU16(archive, cursor + 10);
        const std::uint32_t crc = GetU32(archive, cursor + 16);
        const std::uint32_t compressedSize = GetU32(archive, cursor + 20);
        const std::uint32_t uncompressedSize = GetU32(archive, cursor + 24);
        const std::uint16_t nameLength = GetU16(archive, cursor + 28);
        const std::uint16_t extraLength = GetU16(archive, cursor + 30);
        const std::uint16_t commentLength = GetU16(archive, cursor + 32);
        const std::uint32_t localOffset = GetU32(archive, cursor + 42);
        if (cursor + 46 + nameLength > archive.size()) {
            return Result<Entries>::Failure(MakeError(kTruncated,
                "ZIPの目録が途中で切れています。", {}));
        }
        const std::string name(archive.substr(cursor + 46, nameLength));
        cursor += 46ull + nameLength + extraLength + commentLength;

        if (!IsSafeZipEntryPath(name)) {
            return Result<Entries>::Failure(MakeError(kBadPath,
                "安全でないファイル名が入っています。", name));
        }
        if (!seen.insert(name).second) {
            return Result<Entries>::Failure(MakeError(kDuplicate,
                "同じ名前のファイルが2つ入っています。", name));
        }
        if (method != 0) {
            return Result<Entries>::Failure(MakeError(kUnsupported,
                "圧縮されたファイルには対応していません。",
                name + " (方式 " + std::to_string(method) + ")。"
                "このソフトが書いた .kcd2 は圧縮しません。"));
        }
        if (uncompressedSize > limits.maximumEntryBytes) {
            return Result<Entries>::Failure(MakeError(kTooLarge,
                "中のファイルが大きすぎます。", name));
        }
        if (compressedSize > 0
            && static_cast<double>(uncompressedSize) / compressedSize
                > limits.maximumExpansionRatio) {
            return Result<Entries>::Failure(MakeError(kTooLarge,
                "展開すると異常に大きくなるファイルが入っています。", name));
        }
        total += uncompressedSize;
        if (total > limits.maximumTotalBytes) {
            return Result<Entries>::Failure(MakeError(kTooLarge,
                "中身の合計が大きすぎます。", {}));
        }

        // ローカルヘッダを見て、実データの位置を決める。
        if (localOffset + 30ull > archive.size()
            || GetU32(archive, localOffset) != kLocalHeaderSignature) {
            return Result<Entries>::Failure(MakeError(kTruncated,
                "ZIPの中身の位置が正しくありません。", name));
        }
        const std::uint16_t localNameLength = GetU16(archive, localOffset + 26);
        const std::uint16_t localExtraLength = GetU16(archive, localOffset + 28);
        // ZIPは名前に検査値を持たない。名前が1バイト化けても、そのまま別名として
        // 通ってしまう。目録側と中身側に同じ名前が2つ書かれているので、突き合わせる。
        if (localOffset + 30ull + localNameLength > archive.size()) {
            return Result<Entries>::Failure(MakeError(kTruncated,
                "ZIPの中身の見出しが途中で切れています。", name));
        }
        if (localNameLength != nameLength
            || archive.substr(localOffset + 30ull, localNameLength) != name) {
            return Result<Entries>::Failure(MakeError(kNameMismatch,
                "目録と中身でファイル名が食い違っています。",
                name + " / "
                    + std::string(archive.substr(localOffset + 30ull, localNameLength))));
        }
        const std::size_t dataStart =
            localOffset + 30ull + localNameLength + localExtraLength;
        if (dataStart + uncompressedSize > archive.size()) {
            return Result<Entries>::Failure(MakeError(kTruncated,
                "ZIPの中身が途中で切れています。", name));
        }
        ZipEntry entry;
        entry.path = name;
        entry.data = std::string(archive.substr(dataStart, uncompressedSize));
        if (Crc32(entry.data) != crc) {
            return Result<Entries>::Failure(MakeError(kChecksum,
                "中身が壊れています(検査値が合いません)。", name));
        }
        entries.push_back(std::move(entry));
    }
    return Result<Entries>::Success(std::move(entries));
}

} // namespace kachakacha::v2::io
