#pragma once

//! 壊れた参照(architecture-and-data.md §6、AT-GEO-007)。
//!
//! 参照していた線や面が消えたとき、**近くの別のものへ黙って付け替えない**。
//! 付け替えると、利用者の知らないところで形が変わる。しかも、たいてい
//! 「なんとなく似た形」になるので、間違いに気づかない。
//!
//! ここでやるのは3つだけ。
//!   1. どの参照が壊れているかを見つける。
//!   2. 直す候補を挙げる(挙げるだけ。勝手に選ばない)。
//!   3. 利用者が選んだときだけ書き換える。
//!
//! V1 は最寄りへ自動で付け替えていた。曲線を1本消しただけで、
//! 別の場所の面が静かに歪んだ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <string>
#include <vector>

namespace kachakacha::v2::document {

using base::SegmentId;

//! 壊れた参照1つ。
struct BrokenReference {
    FeatureId featureId;
    //! 消えた相手。Entity ごと消えたのか、その中の線が消えたのか。
    EntityId missingEntityId;
    std::optional<SegmentId> missingSegmentId;
    //! 画面へ出す一文。赤で出す。
    std::string summaryJa;
};

//! 壊れた参照の直し方。利用者が選ぶ。
enum class RepairChoice {
    Reassign,   //!< 別のものを指し直す(どれにするかは利用者が選ぶ)
    Disable,    //!< その操作を効かなくする(消さずに残す)
    Remove,     //!< その操作ごと消す
};

[[nodiscard]] std::string_view RepairChoiceNameJa(RepairChoice choice) noexcept;

//! 指し直しの候補1つ。近い順に並ぶが、**選ぶのは利用者である**。
struct RepairCandidate {
    EntityId entityId;
    std::optional<SegmentId> segmentId;
    double distanceMm = 0.0;
    std::string displayNameJa;
};

//! 壊れた参照を全部見つける。
[[nodiscard]] std::vector<BrokenReference> FindBrokenReferences(
    const DocumentSnapshot& snapshot);

//! 指し直しの候補を挙げる。挙げるだけで、書き換えはしない。
//!
//! `nearPoint` は壊れた参照が指していたあたり。候補はそこからの距離順に並ぶ。
//! 候補が1つしかなくても、自動では選ばない。
[[nodiscard]] std::vector<RepairCandidate> RepairCandidatesFor(
    const DocumentSnapshot& snapshot, const BrokenReference& broken,
    const geometry::Vector3& nearPoint, std::size_t maximumCandidates);

//! 利用者が選んだ直し方を実行する。
//!
//! `Reassign` のときは `chosen` が要る。渡されなければ断る。
//! 「候補が1つだから」で勝手に選ばない。
[[nodiscard]] base::Result<DocumentSnapshot> RepairBrokenReference(
    const DocumentSnapshot& snapshot, const BrokenReference& broken,
    RepairChoice choice, const std::optional<RepairCandidate>& chosen);

} // namespace kachakacha::v2::document
