#pragma once

//! .kcd2 の入れ物になる ZIP。kcd2-format.md §2 の規則をそのまま実装する。
//!
//! 書き出しは無圧縮格納(stored)だけを使う。理由は2つ。
//! 1. 依存を増やさない(ADR 0027。ビルドできる唯一のPCを止めない)。
//! 2. 同じ文書からは必ず同じバイト列が出る(圧縮器の版で結果が変わらない)。
//! 読み込みは deflate のエントリも見分け、対応していないことを明示して断る。
//! 黙って壊れたデータを返さない。

#include "kachakacha/base/Diagnostic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kachakacha::v2::io {

struct ZipEntry {
    std::string path;
    std::string data;
};

//! 上限。おかしなファイルでメモリを食い潰さないための歯止め。
struct ZipLimits {
    std::uint64_t maximumEntryBytes = 512ull * 1024 * 1024;   //!< 1エントリ 512MiB
    std::uint64_t maximumTotalBytes = 2ull * 1024 * 1024 * 1024; //!< 合計 2GiB
    std::size_t maximumEntryCount = 10000;
    //! 展開後 / 圧縮後 がこの倍率を超えるものは拒否する(zip bomb 対策)。
    double maximumExpansionRatio = 1000.0;
};

//! エントリ名の検査。絶対path、ドライブ文字、`..`、逆スラッシュ、NUL を拒否する。
[[nodiscard]] bool IsSafeZipEntryPath(std::string_view path);

//! CRC-32 (IEEE)。ZIPが要求するもの。
[[nodiscard]] std::uint32_t Crc32(std::string_view data);

//! 書き出し。エントリの並びは追加順で固定する。
[[nodiscard]] base::Result<std::string> WriteZip(const std::vector<ZipEntry>& entries);

//! 読み込み。中央ディレクトリを正として読み、局所ヘッダの名前と突き合わせる。
//! 診断: Z001 名前 / Z002 重複 / Z003 途切れ / Z004 未対応の圧縮 /
//!       Z005 検査値 / Z006 大きすぎ / Z007 ZIPでない / Z008 名前の食い違い。
[[nodiscard]] base::Result<std::vector<ZipEntry>> ReadZip(std::string_view archive,
    const ZipLimits& limits = {});

} // namespace kachakacha::v2::io
