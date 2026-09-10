//! .kcd2 の入れ物(ZIP)の層。中身(document.json)の読み書きは DocumentFile.cpp にある。
//! 分けたのは、ファイルの長さの門(1500行)を守るため。層としても別物である。

#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/io/Zip.h"

namespace kachakacha::v2::io {
namespace {

using base::MakeError;
using base::Result;

//! DocumentFile.cpp と同じ値。ここだけ違えると、書いたものが読めなくなる。
constexpr const char* kDocumentEntry = "document.json";
constexpr const char* kBadShape = "KCD2-D004";
constexpr const char* kNoDocument = "KCD2-D005";

} // namespace

Result<std::string> SaveDocument(const DocumentFile& file)
{
    std::vector<ZipEntry> entries;
    entries.push_back(ZipEntry{kDocumentEntry, WriteDocumentJson(file)});
    for (const ZipEntry& entry : file.sideEntries) {
        if (entry.path == kDocumentEntry) {
            return Result<std::string>::Failure(MakeError(kBadShape,
                "document.json は1つだけです。", entry.path));
        }
        entries.push_back(entry);
    }
    return WriteZip(entries);
}

Result<DocumentFile> LoadDocument(std::string_view archive)
{
    const auto read = ReadZip(archive);
    if (!read.HasValue()) {
        return Result<DocumentFile>::Failure(read.Diagnostics());
    }
    const std::vector<ZipEntry>& entries = read.Value();
    const ZipEntry* document = nullptr;
    for (const ZipEntry& entry : entries) {
        if (entry.path == kDocumentEntry) {
            document = &entry;
            break;
        }
    }
    if (document == nullptr) {
        return Result<DocumentFile>::Failure(MakeError(kNoDocument,
            "文書の本体(document.json)が入っていません。",
            "このファイルは .kcd2 ではないか、壊れています。"));
    }
    auto parsed = ReadDocumentJson(document->data);
    if (!parsed.HasValue()) {
        return parsed;
    }
    DocumentFile file = parsed.Value();
    for (const ZipEntry& entry : entries) {
        if (entry.path != kDocumentEntry) {
            file.sideEntries.push_back(entry);
        }
    }
    return Result<DocumentFile>::Success(std::move(file), parsed.Diagnostics());
}

} // namespace kachakacha::v2::io
