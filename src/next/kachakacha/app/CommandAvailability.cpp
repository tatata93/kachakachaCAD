#include "kachakacha/app/CommandAvailability.h"

#include "kachakacha/geometry/WireChain.h"

#include <vector>

namespace kachakacha::v2::app {

bool SelectionSatisfies(SelectionPredicate predicate, const SelectionFacts& facts) noexcept
{
    switch (predicate) {
    case SelectionPredicate::Always:
        return true;
    case SelectionPredicate::HasDocument:
        return facts.hasDocument;
    case SelectionPredicate::HasUndo:
        return facts.canUndo;
    case SelectionPredicate::HasRedo:
        return facts.canRedo;
    case SelectionPredicate::HasVisibleGeometry:
        return facts.hasVisibleGeometry;
    case SelectionPredicate::OneWorkPlane:
        return facts.workPlanes == 1;
    case SelectionPredicate::OnePlanarFaceOrWorkPlane:
        // どちらか片方が1つ。両方選んでいたら、どちらの上に置くのか決まらない。
        return facts.workPlanes + facts.planarFaces == 1;
    case SelectionPredicate::ZeroOrOneGroup:
        return facts.groups <= 1;
    case SelectionPredicate::OneOrMoreWires:
        return facts.wires >= 1;
    case SelectionPredicate::TwoWireChains:
        return facts.wireChains == 2;
    case SelectionPredicate::OneClosedProfile:
        return facts.closedProfiles == 1;
    case SelectionPredicate::OneOrMoreClosedProfiles:
        return facts.closedProfiles >= 1;
    case SelectionPredicate::OnePart:
        return facts.parts == 1;
    case SelectionPredicate::TwoParts:
        return facts.parts == 2;
    case SelectionPredicate::OneDerivedEntity:
        return facts.derivedEntities == 1;
    case SelectionPredicate::OneFabricationModel:
        return facts.fabricationModels == 1;
    case SelectionPredicate::OneFabricationPanel:
        return facts.fabricationPanels == 1;
    case SelectionPredicate::OneOrMorePatterns:
        return facts.patterns >= 1;
    case SelectionPredicate::OneOrMoreSelectedCurves:
        return facts.curves >= 1;
    case SelectionPredicate::OnePartOrSurface:
        // どちらか片方が1つ。両方選んでいたら、どちらから作るのか決まらない。
        return facts.parts + facts.guideSurfaces == 1;
    case SelectionPredicate::OneOrMoreGuideSurfaces:
        // 面に厚みを付ける。面が1つも無ければ、付ける相手がいない。
        return facts.guideSurfaces >= 1;
    case SelectionPredicate::OneOrMoreWiresOrGuideSurfaces:
        return facts.wires + facts.guideSurfaces >= 1;
    case SelectionPredicate::OneGuideRow:
        return facts.selectedGuideRows == 1;
    case SelectionPredicate::OneOrMoreGuideRows:
        return facts.guideRows >= 1;
    case SelectionPredicate::WiresAndOneGuideSurface:
        // 落とす先はちょうど1枚。2枚選んでいたら、どちらへ落とすのか決まらない。
        return facts.wires >= 1 && facts.guideSurfaces == 1;
    }
    return false;
}

std::string_view SelectionBlockReasonJa(SelectionPredicate predicate) noexcept
{
    return SelectionPredicateNameJa(predicate);
}

namespace {

//! 1つのワイヤーの線を集める。場面の並び順のまま渡す。
[[nodiscard]] std::vector<geometry::ChainInput> ChainInputsOf(const base::EntityId& id,
    const modeling::SnapScene& scene)
{
    std::vector<geometry::ChainInput> inputs;
    for (const auto& curve : scene.curves) {
        if (curve.entityId == id) {
            inputs.push_back(geometry::ChainInput{curve.entityId, curve.segmentId,
                curve.segment});
        }
    }
    return inputs;
}

} // namespace

SelectionFacts BuildSelectionFacts(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot, const modeling::SnapScene& scene,
    const geometry::GeometryTolerance& tolerance, const ExternalCounts& external,
    bool canUndo, bool canRedo)
{
    SelectionFacts facts;
    facts.hasDocument = true;
    facts.canUndo = canUndo;
    facts.canRedo = canRedo;
    facts.hasVisibleGeometry = !scene.curves.empty() || !scene.points.empty();
    facts.fabricationModels = external.fabricationModels;
    facts.fabricationPanels = external.fabricationPanels;
    facts.patterns = external.patterns;
    facts.selectedGuideRows = external.selectedGuideRows;
    facts.guideRows = external.guideRows;

    std::vector<base::GroupId> groups;
    for (const auto& id : selection.entityIds) {
        const domain::Entity* entity = nullptr;
        for (const auto& candidate : snapshot.entities) {
            if (candidate.id == id) {
                entity = &candidate;
                break;
            }
        }
        if (entity == nullptr) {
            continue;
        }
        if (entity->groupId.has_value()) {
            bool seen = false;
            for (const auto& known : groups) {
                if (known == *entity->groupId) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                groups.push_back(*entity->groupId);
            }
        }
        switch (entity->kind) {
        case domain::EntityKind::WorkPlane:
            // 平らな面は数えない。面はまだ選べない。
            // ここで作業平面を面としても数えると「どちらか1つ」の条件が
            // 作業平面1つで2つぶんになり、いつまでも押せなくなる。
            ++facts.workPlanes;
            break;
        case domain::EntityKind::Part:
            ++facts.parts;
            break;
        case domain::EntityKind::GuideSurface:
            ++facts.guideSurfaces;
            break;
        case domain::EntityKind::Wire: {
            ++facts.wires;
            const auto inputs = ChainInputsOf(id, scene);
            facts.curves += static_cast<int>(inputs.size());
            if (inputs.empty()) {
                break;
            }
            // 1つのワイヤーは1つの鎖として数える。閉じているかは幾何に聞く。
            // 押し出しの側と同じ関数へ聞く。別々に判断すると食い違う。
            ++facts.wireChains;
            std::vector<geometry::CurveSegment> segments;
            segments.reserve(inputs.size());
            for (const auto& input : inputs) {
                segments.push_back(input.segment);
            }
            if (geometry::SegmentsFormClosedLoop(segments, tolerance)) {
                ++facts.closedProfiles;
            }
            break;
        }
        default:
            break;
        }
        if (!entity->createdBy.IsNil()) {
            ++facts.derivedEntities;
        }
    }
    facts.groups = static_cast<int>(groups.size());
    return facts;
}

} // namespace kachakacha::v2::app
