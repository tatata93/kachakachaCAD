#pragma once

//! 途中で失敗しても、元のファイルを壊さない書き込み(AT-EXP-004)。
//!
//! 手順は3段。
//!   1. 一時ファイルへ全部書く
//!   2. 書けた中身を読み直して、書こうとした中身と一致するか確かめる
//!   3. 既存を .bak へ改名してから、一時ファイルを本名へ改名する
//!
//! どの段で失敗しても、既存のファイルはそのまま読める状態で残る。
//! V1 は本名を開いて上書きしていたので、途中で電源が落ちると
//! 半分だけ書かれたファイルが残り、開けなくなった。
//!
//! 試験で「途中で失敗する」を再現できるように、書き込みそのものを差し替えられる。

#include "kachakacha/base/Diagnostic.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace kachakacha::v2::io {

//! 書き込みの診断コード。
inline constexpr const char* kAtomicWriteFailed = "IO-A001";
inline constexpr const char* kAtomicRenameFailed = "IO-A002";
inline constexpr const char* kAtomicVerifyFailed = "IO-A003";
inline constexpr const char* kAtomicBadPath = "IO-A004";

struct AtomicWriteOptions {
    //! 既存のファイルを .bak として残すか。
    bool keepBackup = true;
    //! 一時ファイルの語尾。
    std::string temporarySuffix = ".tmp";
    //! .bak の語尾。
    std::string backupSuffix = ".bak";
    //! 書いた中身を読み直して確かめるか。既定は確かめる。
    bool verifyAfterWrite = true;
};

struct AtomicWriteReport {
    std::string writtenPath;
    std::string backupPath;      //!< 残さなかったときは空
    std::size_t bytesWritten = 0;
    bool replacedExisting = false;
};

//! 一時ファイルへ実際に書く処理。試験では差し替えて途中失敗を作る。
//! 戻り値が false なら、書き込みが失敗したものとして扱う。
using ByteWriter = std::function<bool(const std::string& path, std::string_view content,
    std::string& errorOut)>;

//! 既定の書き込み。ふつうはこれを使う。
[[nodiscard]] bool WriteWholeFile(const std::string& path, std::string_view content,
    std::string& errorOut);

//! 中身を丸ごと読む。読めなければ値を返さない。
[[nodiscard]] base::Result<std::string> ReadWholeFile(const std::string& path);

//! 原子的に書く。失敗したら、既存のファイルは触らずに残す。
[[nodiscard]] base::Result<AtomicWriteReport> WriteFileAtomically(const std::string& path,
    std::string_view content, const AtomicWriteOptions& options = {},
    const ByteWriter& writer = {});

} // namespace kachakacha::v2::io
