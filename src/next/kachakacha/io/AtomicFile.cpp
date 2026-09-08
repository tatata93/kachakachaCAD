#include "kachakacha/io/AtomicFile.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace kachakacha::v2::io {

using base::MakeError;
using base::Result;

namespace {

//! UTF-8 の文字列を、その環境のパスへ直す。
//!
//! Windows で std::filesystem::path(std::string) をそのまま使うと、
//! ANSI コードページとして解釈されるので、日本語を含むパスが壊れる。
//! u8path は C++20 で非推奨になったため、自前で UTF-16 へ直して渡す。
[[nodiscard]] std::filesystem::path ToPath(const std::string& utf8)
{
#ifdef _WIN32
    std::wstring wide;
    wide.reserve(utf8.size());
    std::size_t index = 0;
    while (index < utf8.size()) {
        const unsigned char lead = static_cast<unsigned char>(utf8[index]);
        char32_t code = 0;
        std::size_t extra = 0;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            code = lead & 0x1Fu;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            code = lead & 0x0Fu;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            code = lead & 0x07u;
            extra = 3;
        } else {
            // 壊れた並び。置換文字にして進む。
            wide.push_back(L'\uFFFD');
            ++index;
            continue;
        }
        if (index + extra >= utf8.size()) {
            wide.push_back(L'\uFFFD');
            break;
        }
        for (std::size_t at = 1; at <= extra; ++at) {
            const unsigned char next = static_cast<unsigned char>(utf8[index + at]);
            if ((next & 0xC0) != 0x80) {
                code = 0xFFFD;
                break;
            }
            code = (code << 6) | (next & 0x3Fu);
        }
        index += extra + 1;
        if (code >= 0x10000) {
            code -= 0x10000;
            wide.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
            wide.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
        } else {
            wide.push_back(static_cast<wchar_t>(code));
        }
    }
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(utf8);
#endif
}

[[nodiscard]] bool Exists(const std::string& path)
{
    std::error_code code;
    return std::filesystem::exists(ToPath(path), code) && !code;
}

[[nodiscard]] bool Remove(const std::string& path)
{
    std::error_code code;
    std::filesystem::remove(ToPath(path), code);
    return !code;
}

[[nodiscard]] bool Rename(const std::string& from, const std::string& to,
    std::string& errorOut)
{
    std::error_code code;
    std::filesystem::rename(ToPath(from), ToPath(to),
        code);
    if (code) {
        errorOut = code.message();
        return false;
    }
    return true;
}

} // namespace

bool WriteWholeFile(const std::string& path, std::string_view content,
    std::string& errorOut)
{
    std::ofstream stream(ToPath(path),
        std::ios::binary | std::ios::trunc);
    if (!stream) {
        errorOut = "ファイルを開けませんでした。";
        return false;
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream) {
        errorOut = "書き込みの途中で失敗しました。";
        return false;
    }
    stream.flush();
    if (!stream) {
        errorOut = "書き込みを流し込めませんでした。";
        return false;
    }
    stream.close();
    if (!stream) {
        errorOut = "ファイルを閉じられませんでした。";
        return false;
    }
    return true;
}

Result<std::string> ReadWholeFile(const std::string& path)
{
    std::ifstream stream(ToPath(path), std::ios::binary);
    if (!stream) {
        return Result<std::string>::Failure(MakeError(kAtomicBadPath,
            "ファイルを開けませんでした。", path));
    }
    std::string content((std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    if (stream.bad()) {
        return Result<std::string>::Failure(MakeError(kAtomicBadPath,
            "ファイルを読めませんでした。", path));
    }
    return Result<std::string>::Success(std::move(content));
}

Result<AtomicWriteReport> WriteFileAtomically(const std::string& path,
    std::string_view content, const AtomicWriteOptions& options, const ByteWriter& writer)
{
    using Out = Result<AtomicWriteReport>;
    if (path.empty()) {
        return Out::Failure(MakeError(kAtomicBadPath, "保存先が空です。", {}));
    }
    if (options.temporarySuffix.empty()) {
        return Out::Failure(MakeError(kAtomicBadPath,
            "一時ファイルの語尾が空です。", {}));
    }

    const std::string temporaryPath = path + options.temporarySuffix;
    const std::string backupPath = path + options.backupSuffix;
    const bool hadExisting = Exists(path);

    // 前回の残骸があれば片づける。残っていても既存の本体には触らない。
    if (Exists(temporaryPath)) {
        (void)Remove(temporaryPath);
    }

    // 1. 一時ファイルへ書く。
    std::string error;
    const bool wrote = writer ? writer(temporaryPath, content, error)
                              : WriteWholeFile(temporaryPath, content, error);
    if (!wrote) {
        (void)Remove(temporaryPath);
        return Out::Failure(MakeError(kAtomicWriteFailed,
            "保存できませんでした。元のファイルはそのまま残っています。", error));
    }

    // 2. 書けた中身を読み直して確かめる。
    if (options.verifyAfterWrite) {
        auto readBack = ReadWholeFile(temporaryPath);
        if (!readBack.HasValue()) {
            (void)Remove(temporaryPath);
            return Out::Failure(MakeError(kAtomicVerifyFailed,
                "書いた中身を読み直せませんでした。元のファイルはそのまま残っています。",
                {}));
        }
        if (readBack.Value() != content) {
            (void)Remove(temporaryPath);
            return Out::Failure(MakeError(kAtomicVerifyFailed,
                "書いた中身が、書こうとした中身と違います。"
                "元のファイルはそのまま残っています。",
                "書こうとした " + std::to_string(content.size()) + " バイト / 実際 "
                    + std::to_string(readBack.Value().size()) + " バイト。"));
        }
    }

    // 3. 既存を .bak へ移してから、一時ファイルを本名にする。
    AtomicWriteReport report;
    report.writtenPath = path;
    report.bytesWritten = content.size();
    report.replacedExisting = hadExisting;

    if (hadExisting && options.keepBackup) {
        if (Exists(backupPath) && !Remove(backupPath)) {
            (void)Remove(temporaryPath);
            return Out::Failure(MakeError(kAtomicRenameFailed,
                "前回の控えを片づけられませんでした。"
                "元のファイルはそのまま残っています。",
                backupPath));
        }
        if (!Rename(path, backupPath, error)) {
            (void)Remove(temporaryPath);
            return Out::Failure(MakeError(kAtomicRenameFailed,
                "控えを作れませんでした。元のファイルはそのまま残っています。", error));
        }
        report.backupPath = backupPath;
    } else if (hadExisting && !Remove(path)) {
        (void)Remove(temporaryPath);
        return Out::Failure(MakeError(kAtomicRenameFailed,
            "元のファイルを置き換えられませんでした。", path));
    }

    if (!Rename(temporaryPath, path, error)) {
        // 本名への改名に失敗した。控えを戻して、元の状態へ返す。
        if (!report.backupPath.empty()) {
            std::string restoreError;
            (void)Rename(backupPath, path, restoreError);
        }
        (void)Remove(temporaryPath);
        return Out::Failure(MakeError(kAtomicRenameFailed,
            "保存先へ置き換えられませんでした。元のファイルはそのまま残っています。",
            error));
    }
    return Out::Success(std::move(report));
}

} // namespace kachakacha::v2::io
