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
        "view.align_selection", "view.align_selection_back", "view.align_workplane", "view.hide_selected", "view.show_all",
        "view.stage_all", "view.stage_no_grid", "view.stage_no_construction",
        "view.stage_selection_only",
        "entity.rename", "workplane.set_active", "group.set_active",
        "group.create", "group.dissolve", "group.rename", "snap.toggle",
        // 数の設定(板厚・面取り量・型紙の余白・縮尺)はどのモードでも要る。
        "view.number_settings",
        // 面の解析は、面を作る(作図)・厚み(部品)・近似(製作)のどれでも見る。
        "view.surface_analysis", "view.analysis_zebra",
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
        "help.copy_diagnostics",
        "wire.intersection_points", "wire.center_points", "wire.key_points",
        "wire.corner_chamfer", "wire.corner_fillet",
        "wire.array_linear", "wire.array_circular",
        "wire.set_datum", "wire.clear_datum", "edit.numeric",
        "wire.move", "wire.copy", "wire.mirror",
        "wire.rotate", "wire.project", "wire.project_surface", "wire.wrap_project", "workplane.create", "grid.edit",
        "grid.move_origin",
        // 3D の面は作図モードで作る(正本 2026-09-18: 作図 = Wire / Curve / WorkPlane / 3D Surface)。
        // 製作モードの近似の元になる。
        "surface.create",
        // 面の編集(合わせる・つなぐ・整える・対称・U/V 線)。面を作る段と同じ作図モード。
        "surface.match", "surface.bridge", "surface.refit", "surface.mirror",
        "surface.iso_curves",
        "guide.revolve", "guide.set_method", "guide.add_row", "guide.append_row",
        "guide.row_up", "guide.row_down", "guide.row_remove", "guide.row_reverse",
        "guide.build", "guide.clear",
    };
    static const std::vector<std::string_view> part{
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
        "fabrication.set_method", "fabrication.freeze_output", "fabrication.merge_parts", "fabrication.split_part",
        "fabrication.set_unfold_base",
        "fabrication.freeze_state", "fabrication.freeze_flat", "fabrication.freeze_target",
        "fabrication.freeze_wires", "fabrication.edit_part",
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
        // **面を作る入口は1つ**(オーナー指示 §10)。面は作図モードで作る(正本 2026-09-18)。
        "surface.create", "workplane.create", "grid.edit", "grid.move_origin",
        "wire.project", "wire.project_surface", "wire.wrap_project",
        "wire.array_linear", "edit.numeric",
    };
    static const std::vector<std::string_view> part{
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
