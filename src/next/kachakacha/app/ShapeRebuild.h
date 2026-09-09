#pragma once

//! 開き直したときに、立体と面を作り直すための段取り。
//!
//! 実形状(OCCT の形)は文書に持たない。持つと、入力を直したのに形が古いまま、
//! という食い違いが起きる。そのかわり **開いたときに作り方から作り直す** 必要がある。
//! これをしていなかったので、保存して開き直すと立体と面が消えていた。
//! 一覧には名前が残るので、消えたことに気づきにくい。
//!
//! ここは「どれを、どの順で作り直すか」だけを決める。OCCT は呼ばない。
//! 実際に作り直すのは画面側(V2RebuildCommands.cpp)である。
//! 順は文書の評価順に従う。足し算・引き算は、材料の立体が出来た後でないと作れない。

#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 作り直しの種類。文書の FeatureType のうち、実形状を作るものだけ。
enum class ShapeRebuildKind {
    Extrude,
    WireCage,
    Boolean,
    GuideSurface,
};

[[nodiscard]] std::string_view ShapeRebuildKindNameJa(ShapeRebuildKind kind) noexcept;

//! 作り直し1つぶん。
struct ShapeRebuildStep {
    ShapeRebuildKind kind = ShapeRebuildKind::Extrude;
    base::FeatureId featureId;
    //! 出来上がりのもの。画面はこの id を鍵にして形を覚える。
    base::EntityId outputEntityId;
    std::string displayName;
};

//! 作り直す順に並べて返す。評価順に従う。
//!
//! 切ってあるもの(enabled=false)は飛ばす。隠してあるものは **飛ばさない** ──
//! 隠れていても、足し算の材料になっていることがある。
[[nodiscard]] std::vector<ShapeRebuildStep> PlanShapeRebuild(
    const document::DocumentSnapshot& snapshot);

//! 作り直しが要る文書かどうか。1つも無ければ、画面は何もしなくてよい。
[[nodiscard]] bool NeedsShapeRebuild(const document::DocumentSnapshot& snapshot);

} // namespace kachakacha::v2::app
