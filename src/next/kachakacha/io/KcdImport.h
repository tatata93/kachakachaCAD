#pragma once

//! V1 の保存形式(.kcd、`src/core/kachakacha/io/ProjectScript.cpp` の命令の並び)を
//! V2 の文書へ読み込む。
//!
//! V1 の .kcd は「1行1命令」の文字列で、平面・線・面・板・製作モデルを命令で並べる。
//! ここでは V2 に同じ意味のものがある命令だけを Feature にし、無いものは
//! **名前を挙げて読み飛ばしたと知らせる**(KCD1-I002、警告)。黙って捨てない。
//! 開けないほど壊れた行(数が足りない、名前が見つからない)は KCD1-E001 で断る。
//!
//! 読めるもの: format_version, point3d, line3d, polyline3d, bezier3d, bspline3d,
//! bspline3d_knots(端を4重にした一様節だけ), circle3d, arc3d, plane_point_normal,
//! plane_offset, plane_rotate, plane_three, wire_meta(平面の紐づけ), visibility,
//! surface_loft(断面のロフト → 形状ガイド), plate(面に厚み → ThickenSurface)。
//! 読み飛ばすもの(V2 に無い/核が要る): wire_project(開いてから「曲面へ投影」で作り直す),
//! plate_range, plate_opening, body_surface_jig, part_model_*, object_set_*, など。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"

#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::io {

struct KcdImportResult {
    document::DocumentSnapshot snapshot;
    //! 読み飛ばした命令などの知らせ(警告)。画面へそのまま出す。
    std::vector<base::Diagnostic> notes;
    int readCommands = 0;
    int skippedCommands = 0;
};

//! .kcd の本文を読む。壊れていれば断る。
[[nodiscard]] base::Result<KcdImportResult> ImportKcdScript(std::string_view text,
    base::IdGenerator& ids);

//! 拡張子が .kcd か(大文字小文字は問わない)。
[[nodiscard]] bool LooksLikeKcdPath(std::string_view path) noexcept;

} // namespace kachakacha::v2::io
