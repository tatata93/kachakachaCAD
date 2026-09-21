#include "kachakacha/app/ToolRoleLabels.h"

namespace kachakacha::v2::app {
namespace {

//! `名前` か `名前 n`。1本しか無いなら番号を出さない。番号があると探してしまう。
[[nodiscard]] std::string Numbered(const std::string& name, std::size_t index,
    std::size_t count)
{
    if (count <= 1) {
        return name;
    }
    return name + " " + std::to_string(index + 1);
}

} // namespace

std::vector<ToolRoleLabel> ExtrudeRoleLabels(const ExtrudeInputState& state)
{
    std::vector<ToolRoleLabel> labels;
    if (state.target.has_value()) {
        labels.push_back(ToolRoleLabel{*state.target, "TARGET"});
    }
    for (std::size_t index = 0; index < state.profiles.size(); ++index) {
        const base::EntityId& id = state.profiles[index];
        if (state.target.has_value() && id == *state.target) {
            // 面を押しているとき、輪郭の番号は対象と同じ立体を指す。
            // 同じところに2枚重ねない。対象の札のほうを残す。
            continue;
        }
        labels.push_back(
            ToolRoleLabel{id, Numbered("PROFILE", index, state.profiles.size())});
    }
    return labels;
}

std::vector<ToolRoleLabel> SurfaceRoleLabels(const SurfaceInputState& state)
{
    const bool network = state.method == modeling::GuideSurfaceMethod::GordonNetwork
        || state.method == modeling::GuideSurfaceMethod::CurveNetworkExact;
    std::vector<ToolRoleLabel> labels;

    // 断面。**画面の「3. 断面順」と同じ並び・同じ番号にする。**
    // 別の番号を出すと、どちらが本当の生成順か分からなくなる。
    const auto sections = SurfaceSectionOrder(state);
    modeling::ChainRole role = modeling::ChainRole::Section;
    if (RoleForSurfaceSlot(state.method, modeling::ChainRole::Section, role)) {
        for (std::size_t index = 0; index < sections.size(); ++index) {
            labels.push_back(ToolRoleLabel{sections[index],
                Numbered(network ? "U" : "Section", index, sections.size())});
        }
    }

    if (RoleForSurfaceSlot(state.method, modeling::ChainRole::GuideU, role)) {
        const std::size_t count = state.guides.size();
        for (std::size_t index = 0; index < count; ++index) {
            std::string text;
            const bool passThrough = state.method == modeling::GuideSurfaceMethod::BoundaryFill
                || state.method == modeling::GuideSurfaceMethod::FourEdgePatch;
            if (network) {
                text = Numbered("V", index, count);
            } else if (passThrough) {
                text = Numbered("Through", index, count);   // 面が必ず通る線
            } else if (count == 2) {
                text = index == 0 ? "Guide L" : "Guide R";
            } else {
                text = Numbered("Guide", index, count);
            }
            labels.push_back(ToolRoleLabel{state.guides[index], std::move(text)});
        }
    }

    if (RoleForSurfaceSlot(state.method, modeling::ChainRole::Centerline, role)) {
        for (std::size_t index = 0; index < state.centerlines.size(); ++index) {
            labels.push_back(ToolRoleLabel{state.centerlines[index],
                Numbered("Centerline", index, state.centerlines.size())});
        }
    }

    if (RoleForSurfaceSlot(state.method, modeling::ChainRole::BoundarySide, role)) {
        const bool fourEdge = state.method == modeling::GuideSurfaceMethod::FourEdgePatch;
        for (std::size_t index = 0; index < state.boundaries.size(); ++index) {
            labels.push_back(ToolRoleLabel{state.boundaries[index],
                Numbered(fourEdge ? "Edge" : "Boundary", index, state.boundaries.size())});
        }
    }
    return labels;
}

} // namespace kachakacha::v2::app
