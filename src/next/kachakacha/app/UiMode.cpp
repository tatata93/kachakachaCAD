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
        "view.align_selection", "view.align_workplane", "view.hide_selected", "view.show_all",
        "view.stage_all", "view.stage_no_grid", "view.stage_no_construction",
        "view.stage_selection_only",
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
        "wire.chamfer", "wire.fillet", "wire.offset", "wire.meet_lines",
        "wire.intersection_points", "wire.corner_chamfer", "wire.corner_fillet",
        "wire.array_linear", "wire.array_circular",
        "wire.set_datum", "wire.clear_datum", "edit.numeric",
        "wire.move", "wire.copy", "wire.mirror",
        "wire.rotate", "wire.project", "wire.project_surface", "wire.wrap_project", "workplane.create", "grid.edit",
        "grid.move_origin",
    };
    static const std::vector<std::string_view> part{
        "guide.create", "guide.revolve", "guide.set_method", "guide.add_row", "guide.append_row",
        "guide.row_up", "guide.row_down", "guide.row_remove", "guide.row_reverse",
        "guide.build", "guide.clear",
        "part.extrude", "part.thicken", "part.thickness_placement", "part.thicken_to_plane",
        "part.surface_jig",
        "part.from_wire_cage",
        "part.boolean_add",
        "part.boolean_cut", "derived.freeze",
    };
    static const std::vector<std::string_view> fabrication{
        "fabrication.create", "fabrication.assign_role", "fabrication.assign_relief_cut",
        "fabrication.preview_update",
        "fabrication.create_pattern", "fabrication.set_assembly",
        "fabrication.set_method", "fabrication.freeze_output", "fabrication.freeze_state",
        "fabrication.set_connection_scope",
    };
    static const std::vector<std::string_view> output{
        "export.validate", "export.stl", "export.step", "export.svg", "export.dxf",
        "export.pdf_1to1",
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

const std::vector<std::string_view>& TopBarCommandIdsForMode(UiMode mode)
{
    // 上の帯に残すのは「そこから始める」ものだけ。
    // 表の行を動かす・板厚を当てる、といったものはその欄の隣(右の棚)にある。
    static const std::vector<std::string_view> drawing{
        "workplane.create", "grid.edit", "grid.move_origin",
        "wire.project", "wire.project_surface", "wire.wrap_project",
        "wire.array_linear", "wire.array_circular", "edit.numeric",
    };
    static const std::vector<std::string_view> part{
        // 形状ガイドを作る入口と、立体にする入口。細かい欄は「部品」の棚。
        "guide.create", "guide.add_row", "guide.build",
        "part.extrude", "part.thicken", "part.boolean_add", "part.boolean_cut",
    };
    static const std::vector<std::string_view> fabrication{
        "fabrication.create", "fabrication.create_pattern", "fabrication.assign_role",
    };
    static const std::vector<std::string_view> output{
        "export.validate", "export.stl", "export.step", "export.svg", "export.dxf",
        "export.pdf_1to1",
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
