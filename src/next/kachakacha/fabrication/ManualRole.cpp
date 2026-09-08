#include "kachakacha/fabrication/ManualRole.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::MakeWarning;
using base::Result;

namespace {

constexpr const char* kConflict = "FAB-R001";
constexpr const char* kBadInput = "FAB-R002";
constexpr const char* kBrokenReference = "FAB-R003";
constexpr const char* kUnknownAssignment = "FAB-R004";

//! 範囲が重なっているか。触れているだけ(端点が同じ)は重なりとみなさない。
[[nodiscard]] bool Overlaps(const ManualRoleAssignment& first,
    const ManualRoleAssignment& second, double& fromOut, double& toOut)
{
    if (first.wireEntityId != second.wireEntityId
        || first.segmentId != second.segmentId) {
        return false;
    }
    fromOut = std::max(first.fromParameter, second.fromParameter);
    toOut = std::min(first.toParameter, second.toParameter);
    return toOut - fromOut > 1.0e-9;
}

} // namespace

std::string_view ManualRoleNameJa(ManualRole value) noexcept
{
    switch (value) {
    case ManualRole::PanelBoundary: return "ここで分ける";
    case ManualRole::FoldLine:      return "ここで折る";
    case ManualRole::ReliefCut:     return "途中まで切る";
    case ManualRole::Opening:       return "開口";
    case ManualRole::KeepTogether:  return "つないだままにする";
    case ManualRole::NoCutZone:     return "切れ目を入れない";
    case ManualRole::BendDirection: return "曲げの向き";
    }
    return "不明";
}

std::string_view ManualRoleName(ManualRole value) noexcept
{
    switch (value) {
    case ManualRole::PanelBoundary: return "panel_boundary";
    case ManualRole::FoldLine:      return "fold_line";
    case ManualRole::ReliefCut:     return "relief_cut";
    case ManualRole::Opening:       return "opening";
    case ManualRole::KeepTogether:  return "keep_together";
    case ManualRole::NoCutZone:     return "no_cut_zone";
    case ManualRole::BendDirection: return "bend_direction";
    }
    return "unknown";
}

std::string_view ManualReferenceStateNameJa(ManualReferenceState value) noexcept
{
    switch (value) {
    case ManualReferenceState::Valid:         return "使えます";
    case ManualReferenceState::Broken:        return "指している線がありません";
    case ManualReferenceState::Unprojectable: return "いまの形へ投影できません";
    case ManualReferenceState::Disabled:      return "外してあります";
    }
    return "不明";
}

bool RolesCanCoexist(ManualRole first, ManualRole second) noexcept
{
    if (first == second) {
        // 同じ役割を同じ場所へ2度付けても害は無い。
        return true;
    }
    const auto pair = [&](ManualRole a, ManualRole b) {
        return (first == a && second == b) || (first == b && second == a);
    };
    // 分けると、つないだままにするは両立しない。
    if (pair(ManualRole::PanelBoundary, ManualRole::KeepTogether)) {
        return false;
    }
    // 折ると、分けるは両立しない。
    if (pair(ManualRole::PanelBoundary, ManualRole::FoldLine)) {
        return false;
    }
    // 切れ目を入れないところへ、切れ目や分割は置けない。
    if (pair(ManualRole::NoCutZone, ManualRole::ReliefCut)
        || pair(ManualRole::NoCutZone, ManualRole::PanelBoundary)) {
        return false;
    }
    // 開口と、その線を折る/切るは両立しない。開口は穴であって線ではない。
    if (pair(ManualRole::Opening, ManualRole::FoldLine)
        || pair(ManualRole::Opening, ManualRole::ReliefCut)
        || pair(ManualRole::Opening, ManualRole::PanelBoundary)) {
        return false;
    }
    // 折ると切れ目は、同じ線の同じ範囲では両立しない。
    if (pair(ManualRole::FoldLine, ManualRole::ReliefCut)) {
        return false;
    }
    return true;
}

std::vector<ManualRoleConflict> FindManualRoleConflicts(
    const std::vector<ManualRoleAssignment>& assignments)
{
    std::vector<ManualRoleConflict> conflicts;
    for (std::size_t first = 0; first < assignments.size(); ++first) {
        for (std::size_t second = first + 1; second < assignments.size(); ++second) {
            double from = 0.0;
            double to = 0.0;
            if (!Overlaps(assignments[first], assignments[second], from, to)) {
                continue;
            }
            if (RolesCanCoexist(assignments[first].role, assignments[second].role)) {
                continue;
            }
            ManualRoleConflict conflict;
            conflict.firstAssignmentId = assignments[first].assignmentId;
            conflict.secondAssignmentId = assignments[second].assignmentId;
            conflict.firstRole = assignments[first].role;
            conflict.secondRole = assignments[second].role;
            conflict.overlapFrom = from;
            conflict.overlapTo = to;
            conflict.reasonJa = std::string(ManualRoleNameJa(assignments[first].role))
                + " と " + std::string(ManualRoleNameJa(assignments[second].role))
                + " は同じ場所に置けません。";
            conflicts.push_back(std::move(conflict));
        }
    }
    return conflicts;
}

Result<ManualRoleSet> ValidateManualRoles(
    const std::vector<ManualRoleAssignment>& assignments)
{
    std::vector<Diagnostic> errors;
    std::set<std::string> names;
    for (const ManualRoleAssignment& assignment : assignments) {
        if (assignment.assignmentId.empty()) {
            errors.push_back(MakeError(kBadInput, "名前の無い割り当てがあります。", {}));
        } else if (!names.insert(assignment.assignmentId).second) {
            errors.push_back(MakeError(kBadInput, "同じ名前の割り当てが2つあります。",
                assignment.assignmentId));
        }
        if (!(assignment.fromParameter >= 0.0) || !(assignment.toParameter <= 1.0)
            || !(assignment.fromParameter < assignment.toParameter)) {
            errors.push_back(MakeError(kBadInput, "範囲が 0〜1 になっていません。",
                assignment.assignmentId));
        }
        if (assignment.role == ManualRole::BendDirection
            && !assignment.bendAngleRad.has_value()) {
            errors.push_back(MakeError(kBadInput, "曲げの向きが指定されていません。",
                assignment.assignmentId));
        }
        if (assignment.role != ManualRole::BendDirection
            && assignment.bendAngleRad.has_value()) {
            errors.push_back(MakeError(kBadInput,
                "曲げの向きは「曲げの向き」の役割にだけ付けられます。",
                assignment.assignmentId));
        }
    }
    for (const ManualRoleConflict& conflict : FindManualRoleConflicts(assignments)) {
        errors.push_back(MakeError(kConflict, conflict.reasonJa,
            conflict.firstAssignmentId + " と " + conflict.secondAssignmentId
                + "(重なり " + std::to_string(conflict.overlapFrom) + "〜"
                + std::to_string(conflict.overlapTo) + ")。"));
    }
    if (!errors.empty()) {
        return Result<ManualRoleSet>::Failure(std::move(errors));
    }

    ManualRoleSet set;
    set.assignments = assignments;
    std::vector<Diagnostic> warnings;
    for (const ManualRoleAssignment& assignment : assignments) {
        if (assignment.state == ManualReferenceState::Broken
            || assignment.state == ManualReferenceState::Unprojectable) {
            set.brokenAssignmentIds.push_back(assignment.assignmentId);
            warnings.push_back(MakeWarning(kBrokenReference,
                "手で付けた線の指し先が使えません。",
                assignment.assignmentId + ": "
                    + std::string(ManualReferenceStateNameJa(assignment.state))
                    + "。近い場所へ動かすことはしません。"
                    + "付け直す・無効にする・消すのどれかを選んでください。"));
        }
    }
    return Result<ManualRoleSet>::Success(std::move(set), std::move(warnings));
}

std::vector<std::string> FindBrokenReferences(
    const std::vector<ManualRoleAssignment>& assignments,
    const std::vector<EntityId>& existingWireIds)
{
    std::vector<std::string> broken;
    for (const ManualRoleAssignment& assignment : assignments) {
        const bool present = std::find(existingWireIds.begin(), existingWireIds.end(),
                                 assignment.wireEntityId)
            != existingWireIds.end();
        if (!present) {
            broken.push_back(assignment.assignmentId);
        }
    }
    return broken;
}

std::string_view BrokenReferenceActionNameJa(BrokenReferenceAction value) noexcept
{
    switch (value) {
    case BrokenReferenceAction::Reassign: return "付け直す";
    case BrokenReferenceAction::Disable:  return "無効にする";
    case BrokenReferenceAction::Remove:   return "消す";
    }
    return "不明";
}

Result<ManualRoleSet> ResolveBrokenReference(const ManualRoleSet& current,
    const std::string& assignmentId, BrokenReferenceAction action,
    std::optional<EntityId> newWireId)
{
    const auto found = std::find_if(current.assignments.begin(),
        current.assignments.end(), [&assignmentId](const ManualRoleAssignment& item) {
            return item.assignmentId == assignmentId;
        });
    if (found == current.assignments.end()) {
        return Result<ManualRoleSet>::Failure(MakeError(kUnknownAssignment,
            "その割り当てがありません。", assignmentId));
    }
    if (action == BrokenReferenceAction::Reassign && !newWireId.has_value()) {
        return Result<ManualRoleSet>::Failure(MakeError(kBadInput,
            "付け直す先の線が指定されていません。", assignmentId));
    }

    std::vector<ManualRoleAssignment> next;
    next.reserve(current.assignments.size());
    for (const ManualRoleAssignment& assignment : current.assignments) {
        if (assignment.assignmentId != assignmentId) {
            next.push_back(assignment);
            continue;
        }
        switch (action) {
        case BrokenReferenceAction::Reassign: {
            ManualRoleAssignment updated = assignment;
            updated.wireEntityId = *newWireId;
            updated.state = ManualReferenceState::Valid;
            next.push_back(std::move(updated));
            break;
        }
        case BrokenReferenceAction::Disable: {
            ManualRoleAssignment updated = assignment;
            updated.state = ManualReferenceState::Disabled;
            next.push_back(std::move(updated));
            break;
        }
        case BrokenReferenceAction::Remove:
            break;
        }
    }
    return ValidateManualRoles(next);
}

} // namespace kachakacha::v2::fabrication
