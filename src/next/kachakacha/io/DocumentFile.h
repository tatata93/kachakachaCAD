#pragma once

//! `.kcd2` の読み書き(kcd2-format.md)。
//!
//! 中身は ZIP + JSON。正本は `document.json` ひとつで、幾何はすべて Feature が持つ。
//! Entity 側へ幾何を書かない(同じものを2箇所へ書くと、必ずどこかで食い違う)。
//!
//! 読み込みは「黙って直さない」。必須キーの欠落、型違い、知らない enum、
//! 参照切れは、どれも値を返さずに診断で断る。
//! 知らないentryは捨てずに持ち帰り、保存し直したときに落とさない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/io/Json.h"
#include "kachakacha/io/Zip.h"

#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::io {

//! 文書の見出し。幾何ではないので Document 本体から分けて持つ。
struct DocumentMetadata {
    std::string title;
    std::string author;
    std::string description;
};

//! ファイル1つ分。
struct DocumentFile {
    document::DocumentSnapshot snapshot;
    DocumentMetadata metadata;
    //! 画面の状態。幾何へ影響させない(kcd2-format.md §17)。
    //! WP-08 で中身を決めるまでは、読んだものをそのまま持ち帰る。
    JsonValue uiState = JsonValue::Object({});
    //! document.json 以外のentry(サムネイルなど)。書き戻すときに落とさない。
    std::vector<ZipEntry> sideEntries;
};

// ---- document.json 単体 ----

[[nodiscard]] std::string WriteDocumentJson(const DocumentFile& file);
[[nodiscard]] base::Result<DocumentFile> ReadDocumentJson(std::string_view text);

// ---- .kcd2 書庫 ----

[[nodiscard]] base::Result<std::string> SaveDocument(const DocumentFile& file);
[[nodiscard]] base::Result<DocumentFile> LoadDocument(std::string_view archive);

} // namespace kachakacha::v2::io
