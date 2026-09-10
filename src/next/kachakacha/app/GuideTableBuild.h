#pragma once

//! 形状ガイドの役割表を、文書と場面から組み立てる。
//!
//! 表(modeling::GuideTable)は「どの線を、どの役割で、どの向きに」を持つ。
//! ここは、その表を **選択から自動で** 作る道と、**保存した作り方から** 作り直す道を置く。
//! 2つの道を画面側に別々に書くと、開き直したときだけ違う面が出来る。
//! 実際に、開き直しは断面だけを拾っていて、外形Uや外形Vを持つ面は戻らなかった。
//!
//! ここは OCCT も Qt も呼ばない。表を作るところまでで、面を作るのは画面側が kernel を呼ぶ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 1本のワイヤーを、表へ入れる束にする。ワイヤーでない、または線が無いときは空。
[[nodiscard]] std::optional<modeling::GuideTableSelection> GuideSelectionOf(
    const document::Document& document, const modeling::SnapScene& scene,
    const base::EntityId& wireId);

//! 「おまかせ」の表。選んだ線を選んだ順に断面として並べる。
//! 2本なら渡すだけ(ルールド)、3本以上ならなめらかに通す(ロフト)。
struct GuideTableDraft {
    modeling::GuideTable table;
    int sections = 0;
    //! 空でなければ表は使えない。理由がここに入る。
    std::vector<base::Diagnostic> diagnostics;
};

[[nodiscard]] GuideTableDraft AutoSectionTable(const document::Document& document,
    const modeling::SnapScene& scene, const std::vector<base::EntityId>& wireIds);

//! 保存した作り方から表を作り直す。役割・向き・作り方・離す距離をそのまま戻す。
//! 元の線が文書から消えていれば、その行を理由付きで断る(黙って飛ばさない)。
[[nodiscard]] base::Result<modeling::GuideTable> GuideTableFromDefinition(
    const document::Document& document, const modeling::SnapScene& scene,
    const domain::CreateGuideSurfaceDefinition& definition);

//! 表を、保存する作り方へ写す。AdoptGuideSurface と開き直しが同じ写し方を使う。
[[nodiscard]] domain::CreateGuideSurfaceDefinition DefinitionFromGuideTable(
    const modeling::GuideTable& table);

//! 表に入っている元の id を、行の順に並べる(Feature の入力に使う)。
[[nodiscard]] std::vector<base::EntityId> GuideTableInputIds(
    const modeling::GuideTable& table);

//! 「選択を既存行へ追加」。端につながらなければ、線を逆向きにしてもう一度だけ試す。
//! それでもつながらなければ、最初の理由(UI-R005)をそのまま返す。
[[nodiscard]] base::Result<modeling::GuideTable> AppendSelectionToRow(
    const modeling::GuideTable& table, std::size_t rowIndex,
    const modeling::GuideTableSelection& selection,
    const geometry::GeometryTolerance& tolerance);

//! 作り方の日本語。「ロフト(断面をなめらかに通す)」など。画面の一覧に出す。
[[nodiscard]] std::string_view GuideSurfaceMethodLabelJa(
    modeling::GuideSurfaceMethod method) noexcept;

//! 画面に並べる順の作り方7通り。
[[nodiscard]] const std::vector<modeling::GuideSurfaceMethod>& GuideSurfaceMethods();

} // namespace kachakacha::v2::app
