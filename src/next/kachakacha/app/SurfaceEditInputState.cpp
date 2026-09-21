#include "kachakacha/app/SurfaceEditInputState.h"

#include <algorithm>
#include <cstdio>

namespace kachakacha::v2::app {

namespace {

using modeling::SurfaceContinuity;

[[nodiscard]] std::string Format(double value, int digits)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

[[nodiscard]] bool Contains(const std::vector<base::EntityId>& ids, const base::EntityId& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

//! 面の欄へ押した面を入れる/外す(最大を超えたら、いちばん古いものと入れ替える)。
void ToggleSurface(std::vector<base::EntityId>& surfaces, const base::EntityId& id,
    std::size_t maximum)
{
    const auto found = std::find(surfaces.begin(), surfaces.end(), id);
    if (found != surfaces.end()) {
        surfaces.erase(found);
        return;
    }
    if (maximum == 1) {
        surfaces.clear();
    } else if (surfaces.size() >= maximum) {
        surfaces.erase(surfaces.begin());
    }
    surfaces.push_back(id);
}

} // namespace

std::string_view SurfaceEditLabelJa(SurfaceEditOperation operation) noexcept
{
    switch (operation) {
    case SurfaceEditOperation::Match:          return "面を合わせる";
    case SurfaceEditOperation::Bridge:         return "面をつなぐ";
    case SurfaceEditOperation::Refit:          return "面を整える";
    case SurfaceEditOperation::Mirror:         return "対称に写す";
    case SurfaceEditOperation::IsoCurve:       return "U/V 線を取り出す";
    case SurfaceEditOperation::CurveOnSurface: return "面へ投影";
    }
    return "面の編集";
}

std::string_view SurfaceEditCommandId(SurfaceEditOperation operation) noexcept
{
    switch (operation) {
    case SurfaceEditOperation::Match:          return "surface.match";
    case SurfaceEditOperation::Bridge:         return "surface.bridge";
    case SurfaceEditOperation::Refit:          return "surface.refit";
    case SurfaceEditOperation::Mirror:         return "surface.mirror";
    case SurfaceEditOperation::IsoCurve:       return "surface.iso_curves";
    case SurfaceEditOperation::CurveOnSurface: return "wire.project_surface";
    }
    return "";
}

bool SurfaceEditOperationForCommand(std::string_view id, SurfaceEditOperation& operation) noexcept
{
    for (const SurfaceEditOperation candidate : SurfaceEditOperations()) {
        if (SurfaceEditCommandId(candidate) == id) {
            operation = candidate;
            return true;
        }
    }
    return false;
}

const std::vector<SurfaceEditOperation>& SurfaceEditOperations()
{
    static const std::vector<SurfaceEditOperation> operations{SurfaceEditOperation::Match,
        SurfaceEditOperation::Bridge, SurfaceEditOperation::Refit, SurfaceEditOperation::Mirror,
        SurfaceEditOperation::IsoCurve, SurfaceEditOperation::CurveOnSurface};
    return operations;
}

std::string_view SurfaceEditHintJa(SurfaceEditOperation operation) noexcept
{
    switch (operation) {
    case SurfaceEditOperation::Match:
        return "直す面の縁 → 合わせ先の縁(縁の近くを押す)";
    case SurfaceEditOperation::Bridge:
        return "縁 A → 縁 B のあいだを渡す面";
    case SurfaceEditOperation::Refit:
        return "形を許容の内側に保って制御点を減らす(何枚でも)";
    case SurfaceEditOperation::Mirror:
        return "対称面に写し、境目の滑らかさを測る(何枚でも)";
    case SurfaceEditOperation::IsoCurve:
        return "面の U/V の線を、ふつうの線として取り出す(何枚でも)";
    case SurfaceEditOperation::CurveOnSurface:
        return "線を作業平面の向きに面へ落とす(面の上の曲線)";
    }
    return "";
}

std::string_view MirrorPlaneLabelJa(MirrorPlaneChoice choice) noexcept
{
    switch (choice) {
    case MirrorPlaneChoice::CenterXZ:  return "車体の中心(Y = 0)";
    case MirrorPlaneChoice::CenterYZ:  return "X = 0 の面";
    case MirrorPlaneChoice::CenterXY:  return "Z = 0 の面";
    case MirrorPlaneChoice::WorkPlane: return "いまの作業平面";
    }
    return "";
}

SurfaceEditSlots SurfaceEditSlotsFor(SurfaceEditOperation operation) noexcept
{
    SurfaceEditSlots plan;
    constexpr std::size_t kAny = static_cast<std::size_t>(-1);
    switch (operation) {
    case SurfaceEditOperation::Match:
        plan.edges = 2;
        plan.continuityA = true;
        break;
    case SurfaceEditOperation::Bridge:
        plan.edges = 2;
        plan.continuityA = true;
        plan.continuityB = true;
        break;
    case SurfaceEditOperation::Refit:
    case SurfaceEditOperation::Mirror:
    case SurfaceEditOperation::IsoCurve:
        plan.surfacesMinimum = 1;
        plan.surfacesMaximum = kAny;
        plan.batchPerSurface = true;
        break;
    case SurfaceEditOperation::CurveOnSurface:
        plan.surfacesMinimum = 1;
        plan.surfacesMaximum = 1;
        plan.wiresMinimum = 1;
        plan.wiresMaximum = kAny;
        break;
    }
    return plan;
}

std::string SurfaceEdgeSlotNameJa(SurfaceEditOperation operation, std::size_t index)
{
    if (operation == SurfaceEditOperation::Match) {
        return index == 0 ? "直す面の縁" : "合わせ先の縁";
    }
    return index == 0 ? "縁 A" : "縁 B";
}

SurfaceEditInputState WithSurfaceEditSurfacePick(const SurfaceEditInputState& state,
    const base::EntityId& surface, const geometry::Vector3& hitPoint, int edgeIndex)
{
    SurfaceEditInputState next = state;
    if (surface.IsNil()) {
        return next;
    }
    const SurfaceEditSlots plan = SurfaceEditSlotsFor(state.operation);
    if (plan.edges > 0) {
        // 同じ面が入っていれば、その欄の縁を押した縁に替える(同じ縁なら外す)。
        for (std::size_t index = 0; index < next.edges.size(); ++index) {
            if (next.edges[index].surface != surface) {
                continue;
            }
            if (next.edges[index].edgeIndex == edgeIndex) {
                next.edges.erase(next.edges.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                next.edges[index].edgeIndex = edgeIndex;
                next.edges[index].hitPoint = hitPoint;
            }
            return next;
        }
        if (next.edges.size() >= plan.edges) {
            next.edges.back() = SurfaceEdgePick{surface, edgeIndex, hitPoint};
            return next;
        }
        next.edges.push_back(SurfaceEdgePick{surface, edgeIndex, hitPoint});
        return next;
    }
    if (plan.surfacesMaximum > 0) {
        ToggleSurface(next.surfaces, surface, plan.surfacesMaximum);
    }
    return next;
}

SurfaceEditInputState WithSurfaceEditWirePick(const SurfaceEditInputState& state,
    const base::EntityId& wire)
{
    SurfaceEditInputState next = state;
    if (wire.IsNil() || SurfaceEditSlotsFor(state.operation).wiresMaximum == 0) {
        return next;
    }
    const auto found = std::find(next.wires.begin(), next.wires.end(), wire);
    if (found != next.wires.end()) {
        next.wires.erase(found);
    } else {
        next.wires.push_back(wire);
    }
    return next;
}

SurfaceEditInputState WithoutSurfaceEditEntry(const SurfaceEditInputState& state,
    const base::EntityId& id)
{
    SurfaceEditInputState next = state;
    next.edges.erase(std::remove_if(next.edges.begin(), next.edges.end(),
                         [&id](const SurfaceEdgePick& pick) { return pick.surface == id; }),
        next.edges.end());
    next.surfaces.erase(std::remove(next.surfaces.begin(), next.surfaces.end(), id),
        next.surfaces.end());
    next.wires.erase(std::remove(next.wires.begin(), next.wires.end(), id), next.wires.end());
    return next;
}

SurfaceEditInputState WithSurfaceEditOperation(const SurfaceEditInputState& state,
    SurfaceEditOperation operation)
{
    SurfaceEditInputState next = state;
    next.operation = operation;
    const SurfaceEditSlots plan = SurfaceEditSlotsFor(operation);
    // 縁 ↔ 面。入れたものは消さない(使えるところへ移す)。
    std::vector<base::EntityId> surfaces = state.surfaces;
    for (const SurfaceEdgePick& pick : state.edges) {
        if (!Contains(surfaces, pick.surface)) {
            surfaces.push_back(pick.surface);
        }
    }
    if (plan.edges > 0) {
        next.surfaces.clear();
        if (next.edges.empty()) {
            for (const base::EntityId& id : state.surfaces) {
                if (next.edges.size() < plan.edges) {
                    next.edges.push_back(SurfaceEdgePick{id, -1, {}});
                }
            }
        }
        if (next.edges.size() > plan.edges) {
            next.edges.resize(plan.edges);
        }
    } else {
        next.edges.clear();
        next.surfaces = surfaces;
        if (next.surfaces.size() > plan.surfacesMaximum) {
            next.surfaces.resize(plan.surfacesMaximum);
        }
    }
    if (plan.wiresMaximum == 0) {
        next.wires.clear();
    }
    return next;
}

std::string SurfaceEditMissingJa(const SurfaceEditInputState& state)
{
    const SurfaceEditSlots plan = SurfaceEditSlotsFor(state.operation);
    if (plan.edges > 0) {
        for (std::size_t index = 0; index < plan.edges; ++index) {
            if (index >= state.edges.size()) {
                return SurfaceEdgeSlotNameJa(state.operation, index)
                    + "を、3D で面の縁の近くを押して選んでください";
            }
            if (state.edges[index].edgeIndex < 0) {
                return SurfaceEdgeSlotNameJa(state.operation, index)
                    + "の縁が決まっていません。面の縁の近くを押してください";
            }
        }
        if (state.edges[0].surface == state.edges[1].surface
            && state.edges[0].edgeIndex == state.edges[1].edgeIndex) {
            return "2 つの欄に同じ縁が入っています。別の縁を選んでください";
        }
    }
    if (state.surfaces.size() < plan.surfacesMinimum) {
        return state.operation == SurfaceEditOperation::CurveOnSurface
            ? "落とす先の面を、3D で押して選んでください"
            : "面を 1 枚以上、3D で押して選んでください";
    }
    if (state.wires.size() < plan.wiresMinimum) {
        return "面へ落とす線を、3D で押して選んでください";
    }
    if (state.operation == SurfaceEditOperation::Refit && !(state.toleranceMm > 0.0)) {
        return "許容は 0 より大きくしてください";
    }
    if (state.operation == SurfaceEditOperation::Bridge && !(state.tension > 0.0)) {
        return "張りの強さは 0 より大きくしてください";
    }
    if (state.operation == SurfaceEditOperation::IsoCurve
        && (state.isoCount < 1 || state.isoCount > 50)) {
        return "本数は 1〜50 本にしてください";
    }
    return {};
}

bool SurfaceEditReadyToBuild(const SurfaceEditInputState& state)
{
    return SurfaceEditMissingJa(state).empty();
}

std::vector<std::string> SurfaceEditStatusLinesJa(const SurfaceEditInputState& state,
    const SurfaceEditOutcome& outcome, bool previewShown)
{
    std::vector<std::string> lines;
    const std::string missing = SurfaceEditMissingJa(state);
    if (!missing.empty()) {
        lines.push_back("まだ作れません: " + missing + "。");
        return lines;
    }
    if (outcome.evaluated && !outcome.available) {
        lines.push_back("作れません: " + outcome.refusalJa);
        return lines;
    }
    if (outcome.available) {
        lines.push_back(std::string(previewShown ? "下見を出しています。" : "作れます。")
            + "Enter で確定、Esc でやめます(元の面はそのまま残ります)。");
        if (!outcome.noteJa.empty()) {
            lines.push_back(outcome.noteJa);
        }
        return lines;
    }
    lines.push_back("そろいました。下見を作っています。");
    return lines;
}

std::string SurfaceEditFooterLine(const SurfaceEditInputState& state,
    const SurfaceEditOutcome& outcome, bool previewShown)
{
    const SurfaceEditSlots plan = SurfaceEditSlotsFor(state.operation);
    std::string line = std::string(SurfaceEditLabelJa(state.operation)) + ": ";
    if (plan.edges > 0) {
        std::size_t decided = 0;
        for (const SurfaceEdgePick& pick : state.edges) {
            decided += pick.edgeIndex >= 0 ? 1 : 0;
        }
        line += "EDGES=" + std::to_string(decided) + "/" + std::to_string(plan.edges);
        line += " / " + std::string(modeling::SurfaceContinuityName(state.continuityA));
        if (plan.continuityB) {
            line += "-" + std::string(modeling::SurfaceContinuityName(state.continuityB));
        }
    } else {
        line += "SURFACES=" + std::to_string(state.surfaces.size());
        if (plan.wiresMaximum > 0) {
            line += " / WIRES=" + std::to_string(state.wires.size());
        }
    }
    if (state.operation == SurfaceEditOperation::Refit) {
        line += " / TOL=" + Format(state.toleranceMm, 3) + "mm";
    } else if (state.operation == SurfaceEditOperation::Bridge) {
        line += " / TENSION=" + Format(state.tension, 2);
    } else if (state.operation == SurfaceEditOperation::IsoCurve) {
        line += std::string(" / ") + (state.isoDirection == 0 ? "U" : state.isoDirection == 1 ? "V" : "U+V")
            + " x" + std::to_string(state.isoCount);
    }
    if (outcome.available) {
        line += " / OUTPUTS=" + std::to_string(outcome.outputs);
    }
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

MirrorPlane MirrorPlaneFor(MirrorPlaneChoice choice, const geometry::Vector3& workOrigin,
    const geometry::Vector3& workNormal) noexcept
{
    switch (choice) {
    case MirrorPlaneChoice::CenterXZ:  return MirrorPlane{{}, {0.0, 1.0, 0.0}};
    case MirrorPlaneChoice::CenterYZ:  return MirrorPlane{{}, {1.0, 0.0, 0.0}};
    case MirrorPlaneChoice::CenterXY:  return MirrorPlane{{}, {0.0, 0.0, 1.0}};
    case MirrorPlaneChoice::WorkPlane: return MirrorPlane{workOrigin, workNormal};
    }
    return MirrorPlane{};
}

} // namespace kachakacha::v2::app
