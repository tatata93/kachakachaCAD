#include "kachakacha/app/UiMode.h"

#include <algorithm>

namespace kachakacha::v2::app {

std::string_view UiModeNameJa(UiMode value) noexcept
{
    switch (value) {
    case UiMode::Drawing:     return "作図";
    case UiMode::Part:        return "部品";
    case UiMode::Fabrication: return "製作";
    case UiMode::Output:      return "出力";
    }
    return "不明";
}

std::string_view UiModeName(UiMode value) noexcept
{
    switch (value) {
    case UiMode::Drawing:     return "drawing";
    case UiMode::Part:        return "part";
    case UiMode::Fabrication: return "fabrication";
    case UiMode::Output:      return "output";
    }
    return "unknown";
}

const std::vector<UiMode>& AllUiModes()
{
    static const std::vector<UiMode> modes{UiMode::Drawing, UiMode::Part,
        UiMode::Fabrication, UiMode::Output};
    return modes;
}

const std::vector<std::string_view>& CommonCommandIds()
{
    // ui-workflows.md §2 の「共通操作」。モードを切り替えても常に出す。
    static const std::vector<std::string_view> ids{
        "file.new", "file.open", "file.save", "file.save_as", "edit.undo", "edit.redo",
        "edit.delete", "selection.activate", "measure.open", "view.fit_all",
        "view.align_selection", "view.hide_selected", "view.show_all",
        "view.stage_all", "view.stage_no_grid", "view.stage_no_construction",
        "entity.rename", "workplane.set_active", "group.set_active", "snap.toggle",
    };
    return ids;
}

const std::vector<std::string_view>& CommandIdsForMode(UiMode mode)
{
    static const std::vector<std::string_view> drawing{
        "draw.point", "draw.line", "draw.polyline", "draw.rectangle", "draw.circle",
        "draw.arc", "draw.bezier", "draw.spline", "wire.trim", "wire.extend",
        "wire.split", "wire.join", "wire.coincident", "wire.tangent", "wire.curvature",
        "wire.chamfer", "wire.fillet", "wire.move", "wire.copy", "wire.mirror",
        "wire.rotate", "wire.project", "workplane.create", "grid.edit",
        "grid.move_origin",
    };
    static const std::vector<std::string_view> part{
        "guide.create", "part.extrude", "part.thicken", "part.from_wire_cage",
        "part.boolean_add",
        "part.boolean_cut", "derived.freeze",
    };
    static const std::vector<std::string_view> fabrication{
        "fabrication.create", "fabrication.assign_role", "fabrication.preview_update",
        "fabrication.create_pattern", "fabrication.set_assembly",
        "fabrication.freeze_state",
    };
    static const std::vector<std::string_view> output{
        "export.validate", "export.stl", "export.step", "export.svg", "export.dxf",
        "view.display_settings",
    };
    switch (mode) {
    case UiMode::Drawing:     return drawing;
    case UiMode::Part:        return part;
    case UiMode::Fabrication: return fabrication;
    case UiMode::Output:      return output;
    }
    return drawing;
}

bool CommandVisibleInMode(std::string_view commandId, UiMode mode)
{
    const auto& common = CommonCommandIds();
    if (std::find(common.begin(), common.end(), commandId) != common.end()) {
        return true;
    }
    const auto& list = CommandIdsForMode(mode);
    return std::find(list.begin(), list.end(), commandId) != list.end();
}

} // namespace kachakacha::v2::app
