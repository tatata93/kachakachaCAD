#include "kachakacha/document/BrokenReference.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::document {
namespace {

using base::MakeError;
using base::Result;

[[nodiscard]] bool HasEntity(const DocumentSnapshot& snapshot, const EntityId& id)
{
    return std::any_of(snapshot.entities.begin(), snapshot.entities.end(),
        [&id](const Entity& entity) { return entity.id == id; });
}

[[nodiscard]] std::string NameOfFeature(const DocumentSnapshot& snapshot,
    const FeatureId& id)
{
    for (const Feature& feature : snapshot.features) {
        if (feature.id == id) {
            return feature.displayName.empty() ? feature.id.ToString()
                                               : feature.displayName;
        }
    }
    return id.ToString();
}

} // namespace

std::string_view RepairChoiceNameJa(RepairChoice choice) noexcept
{
    switch (choice) {
    case RepairChoice::Reassign: return "指し直す";
    case RepairChoice::Disable:  return "効かなくする";
    case RepairChoice::Remove:   return "消す";
    }
    return "不明";
}

std::vector<BrokenReference> FindBrokenReferences(const DocumentSnapshot& snapshot)
{
    std::vector<BrokenReference> broken;
    for (const Feature& feature : snapshot.features) {
        for (const EntityId& input : feature.inputEntityIds) {
            if (HasEntity(snapshot, input)) {
                continue;
            }
            BrokenReference item;
            item.featureId = feature.id;
            item.missingEntityId = input;
            item.summaryJa = NameOfFeature(snapshot, feature.id)
                + " が指しているものが見つかりません。";
            broken.push_back(std::move(item));
        }
    }
    // 並びは決まっている。同じ文書なら毎回同じ順で出る。
    std::sort(broken.begin(), broken.end(),
        [](const BrokenReference& first, const BrokenReference& second) {
            if (first.featureId.ToString() != second.featureId.ToString()) {
                return first.featureId.ToString() < second.featureId.ToString();
            }
            return first.missingEntityId.ToString() < second.missingEntityId.ToString();
        });
    return broken;
}

std::vector<RepairCandidate> RepairCandidatesFor(const DocumentSnapshot& snapshot,
    const BrokenReference& broken, const geometry::Vector3& nearPoint,
    std::size_t maximumCandidates)
{
    std::vector<RepairCandidate> candidates;
    for (const Feature& feature : snapshot.features) {
        if (feature.id == broken.featureId) {
            continue;   // 自分自身は候補にしない。
        }
        const auto* wire = std::get_if<domain::CreateWireDefinition>(&feature.definition);
        if (wire == nullptr) {
            continue;
        }
        for (const auto& output : feature.outputs) {
            if (!HasEntity(snapshot, output.entityId)) {
                continue;
            }
            double best = 1.0e300;
            for (const geometry::CurveSegment& segment : wire->segments) {
                best = std::min(best, segment.ClosestPoint(nearPoint).distance);
            }
            if (best > 1.0e299) {
                continue;
            }
            RepairCandidate candidate;
            candidate.entityId = output.entityId;
            candidate.distanceMm = best;
            for (const Entity& entity : snapshot.entities) {
                if (entity.id == output.entityId) {
                    candidate.displayNameJa = entity.displayName;
                }
            }
            candidates.push_back(std::move(candidate));
        }
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const RepairCandidate& first, const RepairCandidate& second) {
            if (first.distanceMm != second.distanceMm) {
                return first.distanceMm < second.distanceMm;
            }
            return first.entityId.ToString() < second.entityId.ToString();
        });
    if (candidates.size() > maximumCandidates) {
        candidates.resize(maximumCandidates);
    }
    return candidates;
}

Result<DocumentSnapshot> RepairBrokenReference(const DocumentSnapshot& snapshot,
    const BrokenReference& broken, RepairChoice choice,
    const std::optional<RepairCandidate>& chosen)
{
    using Out = Result<DocumentSnapshot>;
    Feature* target = nullptr;
    DocumentSnapshot next = snapshot;
    for (Feature& feature : next.features) {
        if (feature.id == broken.featureId) {
            target = &feature;
            break;
        }
    }
    if (target == nullptr) {
        return Out::Failure(MakeError("DOC-C001",
            "入力に指定されたものが見つかりません。", broken.featureId.ToString()));
    }
    switch (choice) {
    case RepairChoice::Reassign: {
        if (!chosen.has_value()) {
            // 候補が1つしかなくても、選ばれていなければ書き換えない。
            return Out::Failure(MakeError("DOC-C008",
                "指し直す相手が選ばれていません。",
                "近いものへ勝手に付け替えることはしません。候補から選んでください。"));
        }
        if (!HasEntity(next, chosen->entityId)) {
            return Out::Failure(MakeError("DOC-C001",
                "入力に指定されたものが見つかりません。",
                chosen->entityId.ToString()));
        }
        for (EntityId& input : target->inputEntityIds) {
            if (input == broken.missingEntityId) {
                input = chosen->entityId;
            }
        }
        ++target->revision;
        return Out::Success(std::move(next));
    }
    case RepairChoice::Disable:
        // 消さずに残す。あとで指し直せるようにするためである。
        target->enabled = false;
        ++target->revision;
        return Out::Success(std::move(next));
    case RepairChoice::Remove: {
        const FeatureId removing = target->id;
        std::vector<EntityId> outputs;
        for (const auto& output : target->outputs) {
            outputs.push_back(output.entityId);
        }
        next.features.erase(std::remove_if(next.features.begin(), next.features.end(),
                                [&removing](const Feature& feature) {
                                    return feature.id == removing;
                                }),
            next.features.end());
        next.entities.erase(std::remove_if(next.entities.begin(), next.entities.end(),
                                [&outputs](const Entity& entity) {
                                    return std::find(outputs.begin(), outputs.end(),
                                               entity.id)
                                        != outputs.end();
                                }),
            next.entities.end());
        return Out::Success(std::move(next));
    }
    }
    return Out::Failure(MakeError("DOC-C008",
        "指し直す相手が選ばれていません。", "知らない直し方です。"));
}

} // namespace kachakacha::v2::document
