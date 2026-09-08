#pragma once

//! 部品の面や辺を指す意味的なキー(architecture-and-data.md §6)。
//!
//! OCCTの一時的なFace番号を保存してはならない。番号は再計算のたびに変わるので、
//! 保存した参照が別の面へ黙って移る。V1の「開口が別の面に出る」はこれが原因。
//! ここでは「どのFeatureが、どの入力から作った面か」を文字列で持つ。
//!
//! 意味的キーが消えたら、近い面へ付け替えず BrokenReference にする。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::modeling {

using base::EntityId;
using base::SegmentId;

enum class SubshapeKind {
    ExtrudeCapStart,
    ExtrudeCapEnd,
    ExtrudeSide,
    LoftSpan,
    CagePatch,
    BooleanProvenance,
};

//! 分解したキー。作るときも読むときもこの型を通す。
struct SubshapeKey {
    SubshapeKind kind = SubshapeKind::ExtrudeCapStart;
    //! ExtrudeSide / CagePatch。
    SegmentId sourceSegmentId;
    //! LoftSpan。
    SegmentId firstSectionSegmentId;
    SegmentId secondSectionSegmentId;
    //! BooleanProvenance。
    EntityId sourcePartId;
    std::string nestedKey;

    [[nodiscard]] std::string ToString() const;
};

//! 文字列からキーへ戻す。読めない文字列は値を返さない。
[[nodiscard]] base::Result<SubshapeKey> ParseSubshapeKey(std::string_view text);

// ---- 組み立ての入口。文字列を手で書かない ----

[[nodiscard]] SubshapeKey MakeExtrudeCapStart();
[[nodiscard]] SubshapeKey MakeExtrudeCapEnd();
[[nodiscard]] SubshapeKey MakeExtrudeSide(SegmentId sourceSegmentId);
[[nodiscard]] SubshapeKey MakeLoftSpan(SegmentId first, SegmentId second);
[[nodiscard]] SubshapeKey MakeCagePatch(SegmentId representativeSegmentId);
[[nodiscard]] SubshapeKey MakeBooleanProvenance(EntityId sourcePartId,
    const std::string& nestedKey);

} // namespace kachakacha::v2::modeling
