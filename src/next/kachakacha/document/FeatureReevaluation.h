#pragma once

//! Feature の編集と再評価(architecture-and-data.md §7、AT-EXT-008)。
//!
//! 輪郭の寸法、押し出しの距離、相手の位置を変えたとき、
//! **出力の EntityId は変わらず、形だけが計算し直される**。
//! これが守れないと、押し出しの距離を変えるたびに新しい部品が出来てしまい、
//! それを参照していた製作モデルや型紙の参照が切れる。
//!
//! ここには「どの順で、何を計算し直すか」と「core だけで計算できる分の計算」を置く。
//! 立体そのものは OCCT 側が作るが、その入力(輪郭・向き・距離)と
//! 出力の身元はここで決まる。
//!
//! V1 は編集のたびに作り直しで、名前も参照も付け替わった。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Feature.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kachakacha::v2::document {

using domain::Feature;
using domain::FeatureDefinition;

//! 計算し直す順番。上流から並ぶ。
struct ReevaluationPlan {
    //! 変えた Feature そのものを先頭に、下流が続く。
    std::vector<FeatureId> order;
    //! そのあいだに作り直される Entity。IDは変わらない。
    std::vector<EntityId> affectedEntityIds;
};

//! 変えた Feature から、計算し直す範囲を決める。
//! 循環していたら値を返さない(DOC-V004)。
[[nodiscard]] base::Result<ReevaluationPlan> PlanReevaluation(
    const DocumentSnapshot& snapshot, FeatureId changed);

//! 定義を差し替えてよいか。種類が変わる差し替えは断る。
//! 押し出しを「点を作る」に差し替えられては、出力の種類が合わなくなる。
[[nodiscard]] base::Result<std::monostate> CheckDefinitionSwap(const Feature& feature,
    const FeatureDefinition& definition);

//! core だけで計算し直せる分を計算する。
//!
//! 作図点とワイヤーは、ここで最後まで出る。
//! 押し出しは、輪郭・向き・距離・相手から「押し出しの要求」を組み立てるところまで。
//! 立体は OCCT 側が作る。ここで嘘の立体を作らない。
struct ReevaluatedOutput {
    EntityId entityId;
    std::string key;
    domain::EntityKind kind = domain::EntityKind::Point;
    //! 作図点のとき。
    geometry::Vector3 positionMm{};
    //! ワイヤーのとき。
    std::vector<geometry::CurveSegment> segments;
    //! 立体のとき、core では形を持たない。OCCT 側が作る印。
    bool needsKernel = false;
};

struct ReevaluationResult {
    std::vector<ReevaluatedOutput> outputs;
    std::uint64_t featureRevision = 0;
};

//! その Feature 1つを計算し直す。入力は snapshot から引く。
[[nodiscard]] base::Result<ReevaluationResult> ReevaluateFeature(
    const DocumentSnapshot& snapshot, FeatureId featureId);

} // namespace kachakacha::v2::document
