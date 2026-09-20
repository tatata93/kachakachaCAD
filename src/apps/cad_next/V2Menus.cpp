//! 上の献立(メニュー)。V2MainWindow.cpp が 1500 行の門に当たるので、ここへ分ける。
//! 並べるのは台帳の命令だけ。台帳の門(command_catalog_tests)が「献立に無い命令」を見張る。

#include "V2MainWindow.h"

#include "kachakacha/app/CommandCatalog.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QString>
#include <QWidget>

#include <initializer_list>
#include <string>
#include <string_view>

using kachakacha::v2::app::CommandDescriptor;
using kachakacha::v2::app::FindCommand;

//! 献立へ台帳の命令を並べる。台帳に無い id は黙って飛ばす(無い命令の入口を作らない)。
void V2MainWindow::AddMenuCommands(QMenu* menu, std::initializer_list<std::string_view> ids)
{
    for (const std::string_view id : ids) {
        const CommandDescriptor* command = FindCommand(id);
        if (command == nullptr) {
            continue;
        }
        QAction* action = menu->addAction(QString::fromUtf8(std::string(command->labelJa).c_str()));
        action->setToolTip(QString::fromUtf8(std::string(command->operationGuideJa).c_str()));
        if (!command->defaultShortcut.empty()) {
            action->setShortcut(
                QKeySequence(QString::fromUtf8(std::string(command->defaultShortcut).c_str())));
        }
        QObject::connect(action, &QAction::triggered, this, [this, id] { RunCommand(id); });
        commandActions_.emplace_back(id, action);
    }
}

void V2MainWindow::BuildMenus()
{
    // 1024px 幅でも文字を潰さないよう、上段は8分類だけにする。
    // 詳細な編集・基準・視点・見た目は、意味の近い分類のサブメニューへ入れる。
    const auto addCommands = [this](QMenu* menu, std::initializer_list<std::string_view> ids) {
        AddMenuCommands(menu, ids);
    };

    QMenu* file = menuBar()->addMenu(QStringLiteral("ファイル(&F)"));
    addCommands(file, {"file.new", "file.open", "file.save", "file.save_as"});
    file->addSeparator();
    file->addAction(QStringLiteral("終了(&X)"), this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(QStringLiteral("編集(&E)"));
    addCommands(edit, {"edit.undo", "edit.redo", "edit.delete", "entity.rename",
        "edit.numeric", "selection.activate", "snap.toggle"});
    QMenu* wire = edit->addMenu(QStringLiteral("ワイヤー編集(&W)"));
    addCommands(wire, {"wire.trim", "wire.extend", "wire.split", "wire.join",
        "wire.coincident", "wire.tangent", "wire.curvature", "wire.chamfer",
        "wire.fillet", "wire.offset", "wire.meet_lines", "wire.array_linear",
        "wire.array_circular", "wire.intersection_points", "wire.center_points",
        "wire.key_points", "wire.corner_chamfer", "wire.corner_fillet",
        "wire.set_datum", "wire.clear_datum", "wire.move", "wire.copy",
        "wire.mirror", "wire.rotate", "wire.project", "wire.project_surface",
        "wire.wrap_project"});
    QMenu* groups = edit->addMenu(QStringLiteral("グループ(&G)"));
    addCommands(groups, {"group.set_active", "group.create", "group.dissolve",
        "group.rename"});

    QMenu* drawing = menuBar()->addMenu(QStringLiteral("作図(&D)"));
    addCommands(drawing, {"draw.point", "draw.line", "draw.polyline", "draw.rectangle",
        "draw.circle", "draw.arc", "draw.bezier", "draw.spline"});
    QMenu* datum = drawing->addMenu(QStringLiteral("作図面とグリッド(&P)"));
    addCommands(datum, {"workplane.create", "workplane.set_active", "grid.edit",
        "grid.move_origin"});

    QMenu* shape = menuBar()->addMenu(QStringLiteral("形(&M)"));
    addCommands(shape, {"surface.create", "guide.revolve",
        "guide.set_method", "guide.add_row", "guide.append_row", "guide.row_up",
        "guide.row_down", "guide.row_remove", "guide.row_reverse", "guide.build",
        "guide.clear", "part.extrude", "part.thicken", "part.thickness_placement",
        "part.thicken_to_plane", "part.surface_jig", "part.from_wire_cage",
        "part.boolean_add", "part.boolean_cut", "derived.freeze"});

    QMenu* fabrication = menuBar()->addMenu(QStringLiteral("製作(&B)"));
    addCommands(fabrication, {"fabrication.create", "fabrication.assign_role",
        "fabrication.assign_relief_cut", "fabrication.preview_update", "fabrication.create_pattern",
        "fabrication.set_assembly", "fabrication.set_method", "fabrication.merge_parts",
        "fabrication.split_part", "fabrication.set_unfold_base", "fabrication.freeze_output",
        "fabrication.freeze_state", "fabrication.freeze_flat", "fabrication.freeze_target",
        "fabrication.freeze_wires", "fabrication.edit_part", "fabrication.set_connection_scope"});

    QMenu* output = menuBar()->addMenu(QStringLiteral("書き出し(&X)"));
    addCommands(output, {"export.validate", "export.stl", "export.step", "export.svg",
        "export.dxf", "export.pdf_1to1"});

    QMenu* view = menuBar()->addMenu(QStringLiteral("表示(&V)"));
    viewMenu_ = view;
    addCommands(view, {"view.fit_all", "view.align_selection", "view.align_selection_back",
        "view.align_workplane", "view.hide_selected", "view.show_all", "view.stage_all",
        "view.stage_no_grid", "view.stage_no_construction", "view.stage_selection_only",
        "view.display_settings", "view.number_settings", "measure.open"});

    QMenu* viewMenu = view->addMenu(QStringLiteral("視点(&C)"));
    const std::array<ViewDirection, 7> directions{ViewDirection::Top,
        ViewDirection::Bottom, ViewDirection::Front, ViewDirection::Back,
        ViewDirection::Left, ViewDirection::Right, ViewDirection::Isometric};
    for (const ViewDirection direction : directions) {
        viewMenu->addAction(QString::fromUtf8(ViewDirectionNameJa(direction)), this,
            [this, direction] {
                viewport_->SetViewDirection(direction);
                SetStatus(QStringLiteral("視点: %1")
                        .arg(QString::fromUtf8(ViewDirectionNameJa(direction))));
            });
    }

    QMenu* themeMenu = view->addMenu(QStringLiteral("見た目(&T)"));
    themeMenu->addAction(QStringLiteral("通常"), this,
        [this] { ApplyTheme(UiTheme::Normal); });
    themeMenu->addAction(QStringLiteral("Windows 95 風"), this,
        [this] { ApplyTheme(UiTheme::Windows95); });

    QMenu* help = menuBar()->addMenu(QStringLiteral("ヘルプ(&H)"));
    addCommands(help, {"help.copy_diagnostics"});
}
