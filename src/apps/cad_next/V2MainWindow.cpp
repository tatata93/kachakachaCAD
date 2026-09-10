#include "V2MainWindow.h"
#include "V2NumberDialog.h"

#include "kachakacha/app/ExportContent.h"
#include "kachakacha/exporters/PdfWriter.h"
#include "kachakacha/app/CommandAvailability.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"

#include "Win95Style.h"

#include "kachakacha/app/OperationGuide.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <map>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QKeySequence>
#include <QMenuBar>
#include <QStatusBar>
#include <QStyleFactory>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <array>

using kachakacha::v2::app::CommandCatalog;
using kachakacha::v2::app::CommandDescriptor;
using kachakacha::v2::app::CommandMode;
using kachakacha::v2::app::DrawingSession;
using kachakacha::v2::app::FindCommand;
using kachakacha::v2::app::CommandVisibleInMode;
using kachakacha::v2::app::SelectionPredicate;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::app::UiModeNameJa;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::DocumentId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::DrawingToolNameJa;
using kachakacha::v2::modeling::SnapCurve;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneFrame;

namespace {

//! 道具の並び。V1の道具箱と同じ順にする(V1同等性)。
constexpr std::array<DrawingTool, 23> kToolOrder{
    DrawingTool::Select,
    DrawingTool::SetGridOrigin,
    DrawingTool::Point,
    DrawingTool::Line,
    DrawingTool::Polyline,
    DrawingTool::Rectangle,
    DrawingTool::Circle,
    DrawingTool::Arc,
    DrawingTool::Bezier,
    DrawingTool::Spline,
    DrawingTool::Move,
    DrawingTool::Copy,
    DrawingTool::Mirror,
    DrawingTool::Rotate,
    DrawingTool::Split,
    DrawingTool::Trim,
    DrawingTool::Extend,
    DrawingTool::JoinEndpoints,
    DrawingTool::TangentJoin,
    DrawingTool::CurvatureJoin,
    DrawingTool::Measure,
    DrawingTool::ConnectTwoPoints,
    DrawingTool::ChamferOrFilletPair,
};

//! 台帳のコマンドIDと、作図の道具の対応。
//! 道具の入口も台帳を通す。メニューと道具箱で別の道を作らない。
struct ToolBinding {
    std::string_view commandId;
    DrawingTool tool;
};

constexpr std::array<ToolBinding, 17> kToolBindings{{
    {"selection.activate", DrawingTool::Select},
    {"grid.move_origin", DrawingTool::SetGridOrigin},
    {"draw.point", DrawingTool::Point},
    {"draw.line", DrawingTool::Line},
    {"draw.polyline", DrawingTool::Polyline},
    {"draw.rectangle", DrawingTool::Rectangle},
    {"draw.circle", DrawingTool::Circle},
    {"draw.arc", DrawingTool::Arc},
    {"draw.bezier", DrawingTool::Bezier},
    {"draw.spline", DrawingTool::Spline},
    {"wire.trim", DrawingTool::Trim},
    {"wire.extend", DrawingTool::Extend},
    {"wire.move", DrawingTool::Move},
    {"wire.copy", DrawingTool::Copy},
    {"wire.mirror", DrawingTool::Mirror},
    {"wire.rotate", DrawingTool::Rotate},
    {"measure.open", DrawingTool::Measure},
}};

[[nodiscard]] QString ToolLabel(DrawingTool tool)
{
    return QString::fromUtf8(std::string(DrawingToolNameJa(tool)).c_str());
}

} // namespace

V2MainWindow::V2MainWindow()
{
    ids_ = std::make_unique<DeterministicIdGenerator>(1);
    session_ = std::make_unique<DrawingSession>(DocumentId(ids_->Next()), *ids_);

    // 既定の作業平面は XY。グリッドを出す。
    SnapScene scene = session_->Scene();
    const WorkPlaneFrame plane = StandardPlane(StandardPlaneKind::XY);
    scene.workPlane.active = true;
    scene.workPlane.origin = plane.origin;
    scene.workPlane.normal = plane.normal;
    scene.grid.visible = true;
    scene.grid.majorSpacingMm = 10.0;
    scene.grid.subdivision = 2;
    scene.grid.origin = plane.origin;
    scene.grid.uDirection = plane.uAxis;
    scene.grid.vDirection = plane.vAxis;
    session_->SetScene(std::move(scene));

    viewport_ = new V2Viewport(*session_);
    viewport_->SetWorkPlane(plane);
    viewport_->SetViewDirection(ViewDirection::Top);
    setCentralWidget(viewport_);

    BuildMenus();
    BuildModeBar();
    BuildToolPalette();
    BuildPanels();

    viewport_->SetStatusCallback([this](const std::string& text) {
        SetStatus(QString::fromUtf8(text.c_str()));
    });
    viewport_->SetDocumentChangedCallback([this] {
        viewport_->PruneSelection();
        RefreshEntityList();
    });
    // Esc で選択道具へ戻す(V1同等)。道具は窓が持っているので、窓が引き受ける。
    viewport_->SetBackToSelectCallback([this] {
        SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    });
    // 移動・複製・鏡映・回転。点がそろったら、選んでいる線へ当てる。
    viewport_->SetTransformCallback(
        [this](const kachakacha::v2::modeling::TransformPlan& plan) {
            ApplyTransformPlan(plan);
        });
    // 押し出しの選択肢は窓で聞く。判断は core が持っているので、
    // ここは窓を出して答えを渡すだけにする。
    SetExtrudeChooser([this](const kachakacha::v2::app::ExtrudeChoice& initial,
                          const kachakacha::v2::app::ExtrudeFacts& facts)
                          -> std::optional<kachakacha::v2::app::ExtrudeChoice> {
        V2ExtrudeDialog dialog(initial, facts, ExtrudeTargets(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        return dialog.Choice();
    });
    // 作業平面の作り方も窓で聞く。11通りあるのに標準面しか作れなかった。
    SetWorkPlaneChooser([this](const WorkPlaneChoice& initial,
                            const kachakacha::v2::app::WorkPlaneFacts& facts)
                            -> std::optional<WorkPlaneChoice> {
        V2WorkPlaneDialog dialog(initial, facts, this);
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        return dialog.Choice();
    });
    // 組立率は窓で聞く。数値1つなので、押し出しのような大きな窓は要らない。
    SetAssemblyChooser([this](double current) -> std::optional<double> {
        V2NumberDialog dialog(QStringLiteral("組立状態"),
            QStringLiteral("組立率(0 = 平ら、100 = 完成形)"), current, 0.0, 100.0,
            QStringLiteral(" %"), this);
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        return dialog.Value();
    });
    // 制御点を掴んで動かした結果。文書を変えるのは窓の役目。
    viewport_->SetControlPointCallback(
        [this](kachakacha::v2::base::EntityId entityId,
            kachakacha::v2::base::SegmentId segmentId,
            const kachakacha::v2::geometry::CurveSegment& replacement) {
            ReplaceWireSegment(entityId, segmentId, replacement);
        });
    // 選択道具での右クリック。V1と同じで、ここだけメニューを出す。
    viewport_->SetContextMenuCallback([this](const QPoint& at) { ShowSelectMenu(at); });
    viewport_->SetSelectionChangedCallback([this] {
        RefreshExportCounts();
        RefreshMeasurements();
        // 選択が変われば押せるものも変わる。押せる形を選択に付いてこさせる。
        RefreshCommandVisibility();
    });
    // 操作板の「選択に正対」は、台帳のコマンドと同じ道を通す。入口を分けない。
    viewport_->SetAlignSelectionCallback([this] { RunCommand("view.align_selection"); });

    SelectTool(DrawingTool::Line);
    SetMode(UiMode::Drawing);
    ApplyTheme(UiTheme::Normal);
    setWindowTitle(QStringLiteral("kachakachaCAD %1")
            .arg(QString::fromStdString(kachakacha::v2::base::ProductVersionString())));
    resize(1180, 760);
}

V2MainWindow::~V2MainWindow() = default;

void V2MainWindow::BuildMenus()
{
    // メニューは台帳から作る。台帳に無い項目をここで足さない。
    struct MenuGroup {
        const char* titleJa;
        std::vector<std::string_view> ids;
    };
    const std::vector<MenuGroup> groups{
        {"ファイル(&F)", {"file.new", "file.open", "file.save", "file.save_as"}},
        {"編集(&E)", {"edit.undo", "edit.redo", "edit.delete", "entity.rename",
                       "selection.activate", "snap.toggle", "group.set_active"}},
        {"作図(&D)", {"draw.point", "draw.line", "draw.polyline", "draw.rectangle",
                       "draw.circle", "draw.arc", "draw.bezier", "draw.spline"}},
        {"編集操作(&W)", {"wire.trim", "wire.extend", "wire.split", "wire.join",
                            "wire.coincident", "wire.tangent", "wire.curvature",
                            "wire.chamfer", "wire.fillet", "wire.move", "wire.copy",
                            "wire.mirror", "wire.rotate", "wire.project",
                            "wire.project_surface"}},
        {"基準(&P)", {"workplane.create", "workplane.set_active", "grid.edit",
                       "grid.move_origin"}},
        {"形(&M)", {"guide.create", "guide.set_method", "guide.add_row",
                     "guide.append_row", "guide.row_up", "guide.row_down",
                     "guide.row_remove", "guide.row_reverse", "guide.build",
                     "guide.clear", "part.extrude", "part.thicken",
                     "part.thickness_placement", "part.thicken_to_plane",
                     "part.from_wire_cage",
                     "part.boolean_add", "part.boolean_cut", "derived.freeze"}},
        {"製作(&B)", {"fabrication.create", "fabrication.assign_role",
                       "fabrication.preview_update", "fabrication.create_pattern",
                       "fabrication.set_assembly", "fabrication.set_method",
                       "fabrication.freeze_output", "fabrication.freeze_state",
                       "fabrication.set_connection_scope"}},
        {"書き出し(&X)", {"export.validate", "export.stl", "export.step", "export.svg",
                            "export.dxf"}},
        {"表示(&V)", {"view.fit_all", "view.align_selection", "view.hide_selected",
                       "view.show_all", "view.stage_all", "view.stage_no_grid",
                       "view.stage_no_construction", "view.display_settings",
                       "measure.open"}},
    };
    for (const MenuGroup& group : groups) {
        QMenu* menu = menuBar()->addMenu(QString::fromUtf8(group.titleJa));
        for (const std::string_view id : group.ids) {
            const CommandDescriptor* command = FindCommand(id);
            if (command == nullptr) {
                continue;
            }
            QAction* action = menu->addAction(
                QString::fromUtf8(std::string(command->labelJa).c_str()));
            action->setToolTip(
                QString::fromUtf8(std::string(command->operationGuideJa).c_str()));
            if (!command->defaultShortcut.empty()) {
                action->setShortcut(QKeySequence(
                    QString::fromUtf8(std::string(command->defaultShortcut).c_str())));
            }
            QObject::connect(action, &QAction::triggered, this,
                [this, id] { RunCommand(id); });
            commandActions_.emplace_back(id, action);
        }
    }

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("視点(&C)"));
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

    QMenu* themeMenu = menuBar()->addMenu(QStringLiteral("見た目(&T)"));
    themeMenu->addAction(QStringLiteral("通常"), this,
        [this] { ApplyTheme(UiTheme::Normal); });
    themeMenu->addAction(QStringLiteral("Windows 95 風"), this,
        [this] { ApplyTheme(UiTheme::Windows95); });
    themeMenu->addSeparator();
    themeMenu->addAction(QStringLiteral("終了(&X)"), this, &QWidget::close);
}

void V2MainWindow::BuildModeBar()
{
    modeBar_ = addToolBar(QStringLiteral("モード"));
    modeBar_->setObjectName(QStringLiteral("modeBar"));
    modeBar_->setMovable(false);
    for (const UiMode mode : kachakacha::v2::app::AllUiModes()) {
        QAction* action = modeBar_->addAction(
            QString::fromUtf8(std::string(UiModeNameJa(mode)).c_str()));
        action->setCheckable(true);
        action->setChecked(mode == mode_);
        modeActions_.emplace_back(mode, action);
        QObject::connect(action, &QAction::triggered, this,
            [this, mode] { SetMode(mode); });
    }
    addToolBarBreak();
}

void V2MainWindow::SetMode(UiMode mode)
{
    // モードを変えても、選んでいるものも、作った形も、一切触らない。
    // 変わるのは「どのコマンドが出ているか」だけである(UIX-001 / 003)。
    mode_ = mode;
    for (auto& entry : modeActions_) {
        entry.second->setChecked(entry.first == mode);
    }
    RefreshCommandVisibility();
    // 形状ガイドの役割テーブルは部品モードの道具なので、そのときだけ出す。
    // 出しっぱなしにすると、作図モードで空の表が場所を取る。
    if (guideDock_ != nullptr) {
        guideDock_->setVisible(mode == UiMode::Part);
    }
    // 手順はどのモードでも出す。中身がモードで変わる。
    RefreshProcessSteps();
    SetStatus(QStringLiteral("%1モードにしました。選んでいるものはそのままです。")
            .arg(QString::fromUtf8(std::string(UiModeNameJa(mode)).c_str())));
}

int V2MainWindow::VisibleCommandCount() const
{
    int count = 0;
    for (const auto& entry : commandActions_) {
        if (entry.second->isVisible()) {
            ++count;
        }
    }
    return count;
}

void V2MainWindow::RefreshCommandVisibility()
{
    for (const auto& entry : commandActions_) {
        const bool visible = CommandVisibleInMode(entry.first, mode_);
        entry.second->setVisible(visible);
        QString reason;
        const bool enabled = CommandEnabled(entry.first, &reason);
        entry.second->setEnabled(enabled);
        if (!enabled && !reason.isEmpty()) {
            // 使えない理由はツールチップへ出す(UIX-002)。隠さない。
            entry.second->setToolTip(reason);
        }
    }
    // 道具箱もモードに従う。作図の道具が製作モードに並んでいると、
    // そのモードで何ができるのかが読めなくなる。
    for (std::size_t index = 0; index < toolActions_.size(); ++index) {
        const DrawingTool tool = kToolOrder[index];
        std::string_view commandId;
        for (const ToolBinding& binding : kToolBindings) {
            if (binding.tool == tool) {
                commandId = binding.commandId;
            }
        }
        const bool visible = commandId.empty()
            ? (mode_ == UiMode::Drawing)
            : CommandVisibleInMode(commandId, mode_);
        toolActions_[index]->setVisible(visible);
    }
    if (toolPalette_ != nullptr) {
        // 出る道具が1つも無いモードでは、道具箱ごと隠す。
        bool anyVisible = false;
        for (QAction* action : toolActions_) {
            anyVisible = anyVisible || action->isVisible();
        }
        toolPalette_->setVisible(anyVisible);
    }
}

void V2MainWindow::BuildToolPalette()
{
    toolPalette_ = addToolBar(QStringLiteral("道具"));
    toolPalette_->setObjectName(QStringLiteral("toolPalette"));
    toolPalette_->setMovable(false);
    for (const DrawingTool tool : kToolOrder) {
        QAction* action = toolPalette_->addAction(ToolLabel(tool));
        action->setCheckable(true);
        // 案内は台帳から取る。道具箱で別の文言を作らない。
        for (const ToolBinding& binding : kToolBindings) {
            if (binding.tool != tool) {
                continue;
            }
            const CommandDescriptor* command = FindCommand(binding.commandId);
            if (command != nullptr) {
                action->setToolTip(QString::fromUtf8(
                    std::string(command->operationGuideJa).c_str()));
                if (!command->defaultShortcut.empty()) {
                    action->setShortcut(QKeySequence(QString::fromUtf8(
                        std::string(command->defaultShortcut).c_str())));
                }
            }
        }
        if (action->toolTip().isEmpty()) {
            action->setToolTip(ToolLabel(tool));
        }
        toolActions_.push_back(action);
        QObject::connect(action, &QAction::triggered, this,
            [this, tool] { SelectTool(tool); });
    }
}

void V2MainWindow::BuildPanels()
{
    auto* treeDock = new QDockWidget(QStringLiteral("作ったもの"), this);
    treeDock->setObjectName(QStringLiteral("entityDock"));
    entityTree_ = new QTreeWidget(treeDock);
    entityTree_->setColumnCount(2);
    entityTree_->setHeaderLabels(
        {QStringLiteral("名前"), QStringLiteral("種類")});
    // 名前を書き換えたら文書へ入れる。判断(空か、変わったか)は core にある。
    QObject::connect(entityTree_, &QTreeWidget::itemChanged, this,
        [this](QTreeWidgetItem* item, int column) {
            if (column == 0) {
                RenameEntityFromItem(item);
            }
        });
    treeDock->setWidget(entityTree_);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);

    auto* guideDock = new QDockWidget(QStringLiteral("形状ガイドの役割"), this);
    guideDock->setObjectName(QStringLiteral("guideTableDock"));
    guideTableView_ = new QTreeWidget(guideDock);
    guideTableView_->setColumnCount(6);
    guideTableView_->setHeaderLabels({QStringLiteral("役割"), QStringLiteral("番号"),
        QStringLiteral("線数"), QStringLiteral("接続"), QStringLiteral("方向"),
        QStringLiteral("元ワイヤー")});
    guideTableView_->setRootIsDecorated(false);
    // 行を選ぶと、行に効くコマンド(上下・削除・反転・既存行へ追加)が押せるようになる。
    QObject::connect(guideTableView_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem*, int) { RefreshCommandVisibility(); });
    guideDock->setWidget(guideTableView_);
    addDockWidget(Qt::RightDockWidgetArea, guideDock);
    guideDock_ = guideDock;

    auto* processDock = new QDockWidget(QStringLiteral("手順"), this);
    processDock->setObjectName(QStringLiteral("processDock"));
    processView_ = new QTreeWidget(processDock);
    processView_->setColumnCount(3);
    processView_->setHeaderLabels({QStringLiteral("番号"), QStringLiteral("すること"),
        QStringLiteral("様子")});
    processView_->setRootIsDecorated(false);
    // 手順は「いま何段目か」を読むためのものなので、5段は見えていてほしい。
    // 棚の割り当てだけでは、書き出しの棚に押されて2段まで潰れた。
    processView_->setMinimumHeight(130);
    processDock->setWidget(processView_);
    addDockWidget(Qt::RightDockWidgetArea, processDock);
    processDock_ = processDock;

    BuildExportDock();

    // 測る棚。はじめは畳んでおく。使うときに「測る」で出す。
    measureDock_ = new V2MeasureDock(this);
    addDockWidget(Qt::RightDockWidgetArea, measureDock_);
    measureDock_->hide();

    // 数の棚。板厚などは、変えられないと使えない。はじめから出しておく。
    parameterDock_ = new V2ParameterDock(this);
    addDockWidget(Qt::RightDockWidgetArea, parameterDock_);
    parameterDock_->SetDiagnosticSink([this](const QString& text) {
        AddDiagnostic(text);
        SetStatus(text);
    });

    // 右側の棚を重ねて札にする。縦に並べると、1180x760 では
    // 「手順」が2行しか見えず、いま何段目かが読めなくなる。
    // 手順だけは常に見えるように残し、残りは札で切り替える。
    tabifyDockWidget(exportDock_, parameterDock_);
    tabifyDockWidget(parameterDock_, measureDock_);
    exportDock_->raise();


    auto* diagnosticDock = new QDockWidget(QStringLiteral("知らせ"), this);
    diagnosticDock->setObjectName(QStringLiteral("diagnosticDock"));
    diagnosticList_ = new QListWidget(diagnosticDock);
    diagnosticDock->setWidget(diagnosticList_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticDock);
    diagnosticDock_ = diagnosticDock;

    // 棚の広さを決める。決めないと、部品モードで右が 120px まで狭まり、
    // 見出しが切れ、手順が2行しか見えなくなる。
    // 横幅を先に決めてから、縦の割り当てを決める。
    resizeDocks({exportDock_}, {300}, Qt::Horizontal);
    resizeDocks({guideDock_, processDock_, exportDock_}, {110, 200, 330}, Qt::Vertical);
    resizeDocks({diagnosticDock_}, {90}, Qt::Vertical);

    toolLabel_ = new QLabel(this);
    groupLabel_ = new QLabel(this);
    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(toolLabel_);
    // 作業中グループは常に見えるところに置く(ui-workflows §1 の上の帯)。
    statusBar()->addWidget(groupLabel_);
    statusBar()->addWidget(statusLabel_, 1);
}

bool V2MainWindow::SetActiveGroup(
    const std::optional<kachakacha::v2::base::GroupId>& groupId)
{
    const auto result = session_->GetDocument().Run(
        kachakacha::v2::document::SetActiveGroupCommand(groupId));
    if (!result.committed) {
        for (const auto& diagnostic : result.diagnostics) {
            AddDiagnostic(QStringLiteral("%1 %2")
                    .arg(QString::fromStdString(diagnostic.code),
                        QString::fromStdString(diagnostic.summaryJa)));
        }
        return false;
    }
    RefreshEntityList();
    if (groupLabel_ != nullptr) {
        groupLabel_->setText(ActiveGroupText());
    }
    return true;
}

QString V2MainWindow::ActiveGroupText() const
{
    const auto& settings = session_->GetDocument().Snapshot().settings;
    if (!settings.activeGroupId.has_value()) {
        return QStringLiteral("まとまり: (なし)");
    }
    for (const auto& group : session_->GetDocument().Snapshot().groups) {
        if (group.id == *settings.activeGroupId) {
            return QStringLiteral("まとまり: %1")
                .arg(QString::fromStdString(group.displayName));
        }
    }
    return QStringLiteral("まとまり: (なし)");
}

int V2MainWindow::GroupRowCount() const
{
    return entityTree_ == nullptr ? 0 : entityTree_->topLevelItemCount();
}

QString V2MainWindow::GroupRowText(int row) const
{
    if (entityTree_ == nullptr || row < 0 || row >= entityTree_->topLevelItemCount()) {
        return QString();
    }
    return entityTree_->topLevelItem(row)->text(0);
}

void V2MainWindow::RefreshProcessSteps()
{
    if (processView_ == nullptr) {
        return;
    }
    processView_->clear();
    const auto steps = kachakacha::v2::app::BuildProcessSteps(mode_, processContext_);
    for (const auto& step : steps) {
        auto* item = new QTreeWidgetItem(processView_);
        item->setText(0, QString::number(step.number));
        item->setText(1, QString::fromStdString(step.titleJa));
        item->setText(2, QString::fromStdString(
            std::string(kachakacha::v2::app::StepStateNameJa(step.state))));
        // 進めない段には理由を出す。列に押し込むと切れて読めないので、
        // その段の下へ1行ぶら下げる。理由の無い灰色を作らない。
        if (!step.blockedReasonJa.empty()) {
            auto* why = new QTreeWidgetItem(item);
            why->setText(1, QString::fromStdString(step.blockedReasonJa));
            why->setForeground(1, QColor(0xC0, 0xA0, 0x50));
        }
        if (step.state == kachakacha::v2::app::StepState::Done) {
            item->setForeground(1, QColor(0x40, 0xA0, 0x60));
        } else if (step.state == kachakacha::v2::app::StepState::Blocked) {
            item->setForeground(1, QColor(0x90, 0x90, 0x98));
        }
    }
    processView_->expandAll();
    for (int column = 0; column < processView_->columnCount(); ++column) {
        processView_->resizeColumnToContents(column);
    }
}

void V2MainWindow::AdoptDocument(kachakacha::v2::document::DocumentSnapshot snapshot)
{
    const auto problems = session_->GetDocument().ResetTo(std::move(snapshot));
    bool refused = false;
    for (const auto& diagnostic : problems) {
        AddDiagnostic(QStringLiteral("%1 %2")
                .arg(QString::fromStdString(diagnostic.code),
                    QString::fromStdString(diagnostic.summaryJa)));
        refused = refused || diagnostic.IsError();
    }
    if (refused) {
        SetStatus(QStringLiteral("開けませんでした。いまの文書はそのままです。"));
        return;
    }
    // 線を場面へ並べ直す。見ている場所は変えない。
    session_->SetScene(kachakacha::v2::app::RebuildSceneKeepingView(session_->Scene(),
        session_->GetDocument().Snapshot(), *ids_));
    viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
    // 立体と面を作り方から作り直す。作り直さないと、線だけが残って
    // 立体が消えたことに気づかないまま、出そうとしたときに初めて分かる。
    RebuildKernelShapes();
    RefreshEntityList();
    RefreshExportCounts();
    viewport_->update();
}

void V2MainWindow::SetPathChooser(std::function<QString(bool forSave)> chooser)
{
    pathChooser_ = std::move(chooser);
}

QString V2MainWindow::AskForPath(bool forSave)
{
    if (pathChooser_) {
        return pathChooser_(forSave);
    }
    const QString filter = QStringLiteral("kachakachaCAD の文書 (*.kcd2)");
    return forSave
        ? QFileDialog::getSaveFileName(this, QStringLiteral("保存する"), QString(), filter)
        : QFileDialog::getOpenFileName(this, QStringLiteral("開く"), QString(), filter);
}

bool V2MainWindow::OpenDocumentFile(const QString& path)
{
    const auto text = kachakacha::v2::io::ReadWholeFile(path.toStdString());
    if (!text.HasValue()) {
        ReportDiagnostics(text.Diagnostics());
        return false;
    }
    const auto loaded = kachakacha::v2::io::LoadDocument(text.Value());
    if (!loaded.HasValue()) {
        ReportDiagnostics(loaded.Diagnostics());
        return false;
    }
    const std::uint64_t before = session_->GetDocument().Revision();
    AdoptDocument(loaded.Value().snapshot);
    if (session_->GetDocument().Snapshot().entities.empty()
        && !loaded.Value().snapshot.entities.empty()) {
        // ResetTo が断った。いまの文書はそのままなので、開けたことにしない。
        (void)before;
        return false;
    }
    documentPath_ = path;
    SetStatus(QStringLiteral("%1 を開きました。").arg(path));
    return true;
}

void V2MainWindow::RunFileCommand(std::string_view id)
{
    using kachakacha::v2::io::DocumentFile;
    if (id == "file.new") {
        AdoptDocument(kachakacha::v2::document::DocumentSnapshot{});
        documentPath_.clear();
        SetStatus(QStringLiteral("新しい文書にしました。"));
        return;
    }
    if (id == "file.open") {
        const QString chosen = AskForPath(false);
        if (!chosen.isEmpty()) {
            (void)OpenDocumentFile(chosen);
        } else {
            SetStatus(QStringLiteral("開くのをやめました。"));
        }
        return;
    }
    QString path = documentPath_;
    if (id == "file.save_as" || path.isEmpty()) {
        path = AskForPath(true);
        if (path.isEmpty()) {
            SetStatus(QStringLiteral("保存するのをやめました。"));
            return;
        }
        if (!path.endsWith(QStringLiteral(".kcd2"))) {
            path += QStringLiteral(".kcd2");
        }
    }
    DocumentFile file;
    file.snapshot = session_->GetDocument().Snapshot();
    file.metadata.title = path.toStdString();
    const auto archive = kachakacha::v2::io::SaveDocument(file);
    if (!archive.HasValue()) {
        ReportDiagnostics(archive.Diagnostics());
        return;
    }
    const auto written = kachakacha::v2::io::WriteFileAtomically(path.toStdString(),
        archive.Value());
    if (!written.HasValue()) {
        ReportDiagnostics(written.Diagnostics());
        return;
    }
    documentPath_ = path;
    session_->GetDocument().MarkHistoryBoundary();
    SetStatus(QStringLiteral("%1 へ保存しました。").arg(path));
}


double V2MainWindow::ExtrudeDistanceMm() const
{
    return kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(),
        kachakacha::v2::app::ParameterId::ExtrudeDistance);
}

double V2MainWindow::CornerSizeMm() const
{
    return kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(),
        kachakacha::v2::app::ParameterId::CornerSize);
}

kachakacha::v2::app::SelectionFacts V2MainWindow::BuildFactsForCommands() const
{
    kachakacha::v2::app::ExternalCounts external;
    // 部材も型紙も文書には入らない。画面が覚えているので、そこから渡す。
    external.fabricationModels = static_cast<int>(fabricationModels_.size());
    external.fabricationPanels = static_cast<int>(fabricationPanels_.size());
    external.patterns = static_cast<int>(patternPages_.size());
    // 役割表も画面が持つ。行を選んでいるかで、行に効くコマンドが押せるかが決まる。
    external.guideRows = static_cast<int>(guideTable_.rows.size());
    external.selectedGuideRows = CurrentGuideRow().has_value() ? 1 : 0;
    const kachakacha::v2::app::SelectionSet empty;
    return kachakacha::v2::app::BuildSelectionFacts(
        viewport_ == nullptr ? empty : viewport_->Selection(),
        session_->GetDocument().Snapshot(), session_->Scene(),
        session_->GetDocument().Snapshot().settings.tolerance, external,
        session_->GetDocument().CanUndo(), session_->GetDocument().CanRedo());
}

void V2MainWindow::RefreshMeasurements()
{
    if (measureDock_ == nullptr || viewport_ == nullptr) {
        return;
    }
    kachakacha::v2::app::MeasureRequest request;
    // 選んだものだけを測る。見えているだけのものを勝手に足さない。
    request.curves = kachakacha::v2::app::SelectedCurves(viewport_->Selection(),
        session_->Scene());
    request.toleranceMm =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    measureDock_->SetRequest(request);
}

void V2MainWindow::SetProcessContext(const kachakacha::v2::app::ProcessContext& context)
{
    processContext_ = context;
    RefreshProcessSteps();
    RefreshExportCounts();
    // 文書や部材が変われば押せるものも変わる。
    RefreshCommandVisibility();
}

int V2MainWindow::ProcessStepCount() const
{
    return processView_ == nullptr ? 0 : processView_->topLevelItemCount();
}

QString V2MainWindow::ProcessStepText(int row) const
{
    if (processView_ == nullptr || row < 0 || row >= processView_->topLevelItemCount()) {
        return QString();
    }
    auto* item = processView_->topLevelItem(row);
    return item->text(0) + QStringLiteral(" ") + item->text(1) + QStringLiteral(" ")
        + item->text(2);
}

QString V2MainWindow::ProcessStepReason(int row) const
{
    if (processView_ == nullptr || row < 0 || row >= processView_->topLevelItemCount()) {
        return QString();
    }
    auto* item = processView_->topLevelItem(row);
    if (item->childCount() == 0) {
        return QString();
    }
    return item->child(0)->text(1);
}

int V2MainWindow::VisibleToolCount() const
{
    int count = 0;
    for (QAction* action : toolActions_) {
        count += action->isVisible() ? 1 : 0;
    }
    return count;
}

int V2MainWindow::CurrentProcessStep() const
{
    return kachakacha::v2::app::CurrentStepNumber(
        kachakacha::v2::app::BuildProcessSteps(mode_, processContext_));
}

void V2MainWindow::RefreshGuideTable()
{
    if (guideTableView_ == nullptr) {
        return;
    }
    guideTableView_->clear();
    const auto views = kachakacha::v2::modeling::BuildGuideTableView(guideTable_,
        session_->GetDocument().Snapshot().settings.tolerance);
    for (const auto& view : views) {
        auto* item = new QTreeWidgetItem(guideTableView_);
        item->setText(0, QString::fromStdString(view.roleLabelJa));
        item->setText(1, QString::number(view.number));
        item->setText(2, QString::number(static_cast<int>(view.segmentCount)));
        item->setText(3, QString::fromStdString(view.connectionLabelJa));
        item->setText(4, QString::fromStdString(view.directionLabelJa));
        item->setText(5, QString::fromStdString(view.sourceLabelJa));
        // 色は core の式が決める。画面で作らないので、3Dと必ず同じ色になる。
        const QColor color(view.color.red, view.color.green, view.color.blue);
        item->setForeground(0, color);
        item->setData(0, Qt::UserRole, color);
    }
    for (int column = 0; column < guideTableView_->columnCount(); ++column) {
        guideTableView_->resizeColumnToContents(column);
    }
    // 3Dへ同じ色で出す。色は core の式が決めるので、表と3Dがずれようがない。
    viewport_->SetGuideTableRows(views);
    // 足りない役割の案内は、そのつど出し直す。前の案内を残すと、
    // 入れ終わったあとも「入っていません」が並んだままになる。
    ClearGuideGuidance();
    for (const std::string& line : kachakacha::v2::modeling::MissingRoleGuidanceJa(
             guideTable_)) {
        AddGuideGuidance(QStringLiteral("UI-R009 %1").arg(QString::fromStdString(line)));
    }
}

void V2MainWindow::ClearGuideGuidance()
{
    if (diagnosticList_ == nullptr) {
        return;
    }
    for (int row = diagnosticList_->count() - 1; row >= 0; --row) {
        if (diagnosticList_->item(row)->text().startsWith(QStringLiteral("UI-R009"))) {
            delete diagnosticList_->takeItem(row);
        }
    }
}

void V2MainWindow::AddGuideGuidance(const QString& text)
{
    if (diagnosticList_ == nullptr) {
        return;
    }
    diagnosticList_->addItem(text);
}

bool V2MainWindow::SetGuideTable(
    const kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>& result)
{
    if (!result.HasValue()) {
        for (const auto& diagnostic : result.Diagnostics()) {
            AddDiagnostic(QStringLiteral("%1 %2")
                    .arg(QString::fromStdString(diagnostic.code),
                        QString::fromStdString(diagnostic.summaryJa)));
        }
        return false;   // 断られたら表は変えない。
    }
    guideTable_ = result.Value();
    RefreshGuideTable();
    // 行の数が変われば「表から面を作る」などの押せる・押せないも変わる。
    RefreshCommandVisibility();
    return true;
}

int V2MainWindow::GuideRowCount() const
{
    return guideTableView_ == nullptr ? 0 : guideTableView_->topLevelItemCount();
}

QColor V2MainWindow::GuideRowColor(int row) const
{
    if (guideTableView_ == nullptr || row < 0 || row >= guideTableView_->topLevelItemCount()) {
        return QColor();
    }
    return guideTableView_->topLevelItem(row)->data(0, Qt::UserRole).value<QColor>();
}

QString V2MainWindow::GuideRowText(int row, int column) const
{
    if (guideTableView_ == nullptr || row < 0 || row >= guideTableView_->topLevelItemCount()) {
        return QString();
    }
    return guideTableView_->topLevelItem(row)->text(column);
}

void V2MainWindow::SelectTool(DrawingTool tool)
{
    session_->SelectTool(tool);
    for (std::size_t index = 0; index < toolActions_.size(); ++index) {
        toolActions_[index]->setChecked(kToolOrder[index] == tool);
    }
    if (toolLabel_) {
        toolLabel_->setText(QStringLiteral("道具: %1").arg(ToolLabel(tool)));
    }
    // 案内文は core が持っている。画面で作らない。
    // 道具の名前・いまの手順・次の手順・決め方・やめ方・選択数の6つを必ず出す。
    RefreshGuide();
    viewport_->update();
}

void V2MainWindow::RefreshGuide()
{
    const DrawingTool tool = session_->CurrentTool();
    std::string_view commandId = "selection.activate";
    for (const ToolBinding& binding : kToolBindings) {
        if (binding.tool == tool) {
            commandId = binding.commandId;
            break;
        }
    }
    const CommandDescriptor* command = FindCommand(commandId);
    if (command == nullptr) {
        return;
    }
    // いまの進み具合はツールが持っている。画面で数え直さない。
    const auto hover = session_->Hover(kachakacha::v2::geometry::ScreenPoint{
        viewport_->width() * 0.5, viewport_->height() * 0.5});
    kachakacha::v2::modeling::ToolPrompt prompt;
    prompt.messageJa = hover.messageJa;
    prompt.canFinish = true;
    const auto guide = kachakacha::v2::app::BuildGuide(*command, prompt,
        selectionCount_, true);
    SetStatus(QString::fromUtf8(guide.ToStatusLine().c_str()));
}

void V2MainWindow::ReportDiagnostics(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    for (const auto& diagnostic : diagnostics) {
        const QString text = QStringLiteral("%1 %2")
            .arg(QString::fromStdString(diagnostic.code),
                QString::fromStdString(diagnostic.summaryJa));
        AddDiagnostic(text);
        SetStatus(text);
    }
}

void V2MainWindow::SetStatus(const QString& text)
{
    if (statusLabel_) {
        statusLabel_->setText(text);
    }
}

QString V2MainWindow::StatusText() const
{
    return statusLabel_ ? statusLabel_->text() : QString();
}

void V2MainWindow::AddDiagnostic(const QString& codeAndText)
{
    if (diagnosticList_) {
        diagnosticList_->addItem(codeAndText);
    }
}

void V2MainWindow::ClearDiagnostics()
{
    if (diagnosticList_) {
        diagnosticList_->clear();
    }
}

int V2MainWindow::EntityRowCount() const
{
    return entityTree_ ? entityTree_->topLevelItemCount() : 0;
}

int V2MainWindow::DiagnosticRowCount() const
{
    return diagnosticList_ ? diagnosticList_->count() : 0;
}

void V2MainWindow::RefreshEntityList()
{
    if (!entityTree_) {
        return;
    }
    // 書き換えの便りを止めてから作り直す。止めないと、作り直しの途中で
    // 「名前が変わった」と誤って伝わり、名前が入れ替わる。
    const bool blocked = entityTree_->blockSignals(true);
    entityTree_->clear();
    entityItems_.clear();
    const auto& snapshot = session_->GetDocument().Snapshot();
    // まとまりごとに束ねる。いま作業中のまとまりは名前の後ろに印を付ける。
    std::map<std::string, QTreeWidgetItem*> byGroup;
    const auto groupItem = [&](const std::optional<kachakacha::v2::base::GroupId>& id)
        -> QTreeWidgetItem* {
        std::string name = "(まとまりなし)";
        bool active = false;
        if (id.has_value()) {
            for (const auto& group : snapshot.groups) {
                if (group.id == *id) {
                    name = group.displayName;
                }
            }
            active = snapshot.settings.activeGroupId.has_value()
                && *snapshot.settings.activeGroupId == *id;
        }
        const std::string key = name + (active ? " ←作業中" : "");
        const auto found = byGroup.find(key);
        if (found != byGroup.end()) {
            return found->second;
        }
        auto* made = new QTreeWidgetItem(entityTree_);
        made->setText(0, QString::fromStdString(key));
        made->setText(1, QStringLiteral("まとまり"));
        byGroup.emplace(key, made);
        return made;
    };
    entityItems_.clear();
    for (const auto& entity : snapshot.entities) {
        auto* item = new QTreeWidgetItem(groupItem(entity.groupId));
        const QString name = entity.displayName.empty()
            ? QStringLiteral("(名前なし)")
            : QString::fromUtf8(entity.displayName.c_str());
        item->setText(0, name);
        // F2 で名前を書き換えられるようにする。まとまりの行は変えられない。
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        entityItems_.emplace_back(item, entity.id);
        item->setText(1, QString::fromUtf8(
            std::string(kachakacha::v2::domain::EntityKindNameJa(entity.kind)).c_str()));
    }
    entityTree_->expandAll();
    for (int column = 0; column < entityTree_->columnCount(); ++column) {
        entityTree_->resizeColumnToContents(column);
    }
    if (groupLabel_ != nullptr) {
        groupLabel_->setText(ActiveGroupText());
    }
    entityTree_->blockSignals(blocked);
}

QAction* V2MainWindow::ActionFor(std::string_view id) const
{
    for (const auto& entry : commandActions_) {
        if (entry.first == id) {
            return entry.second;
        }
    }
    return nullptr;
}

bool V2MainWindow::CommandEnabled(std::string_view id, QString* reasonOut) const
{
    const CommandDescriptor* command = FindCommand(id);
    if (command == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = QStringLiteral("知らないコマンドです。");
        }
        return false;
    }
    // 判断は core にある。ここで数えると、画面を出さないと確かめられなくなる。
    const bool ok = kachakacha::v2::app::SelectionSatisfies(command->predicate,
        BuildFactsForCommands());
    if (!ok && reasonOut != nullptr) {
        *reasonOut = QString::fromUtf8(
            std::string(command->predicateFailureJa).c_str());
    }
    return ok;
}

bool V2MainWindow::EnterToolFor(const CommandDescriptor& command)
{
    // 作図の道具は道具へ入る。
    //
    // ただし、道具へ入るだけでは終わらないものがある。
    // トリム・延長・グリッド原点は「押す場所」を1回聞く必要があり、
    // 測定は棚を出す必要がある。これらは道具を選んだあと、受け口へ続ける。
    // ここで終わりにしてしまい、押しても案内文が出るだけになっていた。
    const bool continuesAfterTool = command.id == "wire.trim"
        || command.id == "wire.extend" || command.id == "grid.move_origin"
        || command.id == "measure.open";
    for (const ToolBinding& binding : kToolBindings) {
        if (binding.commandId != command.id) {
            continue;
        }
        SelectTool(binding.tool);
        if (continuesAfterTool) {
            return false;
        }
        SetStatus(QString::fromUtf8(
            std::string(command.operationGuideJa).c_str()));
        return true;
    }
    return false;
}

void V2MainWindow::RunCommand(std::string_view id)
{
    QString reason;
    if (!CommandEnabled(id, &reason)) {
        SetStatus(reason);
        return;
    }
    const CommandDescriptor* command = FindCommand(id);
    if (command == nullptr) {
        return;
    }
    if (EnterToolFor(*command)) {
        return;
    }
    if (id == "edit.undo") {
        SetStatus(session_->Undo() ? QStringLiteral("元に戻しました。")
                                   : QStringLiteral("戻せる操作がありません。"));
        RefreshEntityList();
        viewport_->update();
        return;
    }
    if (id == "edit.redo") {
        SetStatus(session_->Redo() ? QStringLiteral("やり直しました。")
                                   : QStringLiteral("やり直せる操作がありません。"));
        RefreshEntityList();
        viewport_->update();
        return;
    }
    if (id == "view.fit_all") {
        viewport_->FitToDocument();
        SetStatus(QStringLiteral("全体を表示しました。"));
        return;
    }
    if (id.rfind("file.", 0) == 0) {
        RunFileCommand(id);
        return;
    }
    if (IsFabricationCommand(id)) {
        RunFabricationCommand(id);
        return;
    }
    if (IsPartCommand(id)) {
        RunPartCommand(id);
        return;
    }
    if (IsPlaneCommand(id)) {
        RunPlaneCommand(id);
        return;
    }
    if (IsWireEditCommand(id)) {
        RunWireEditCommand(id);
        return;
    }
    if (id.rfind("export.", 0) == 0) {
        RunExportCommand(id);
        return;
    }
    if (IsFreezeCommand(id)) {
        RunFreezeCommand(id);
        return;
    }
    if (IsGuideCommand(id)) {
        RunGuideCommand(id);
        return;
    }
    if (IsViewCommand(id)) {
        RunViewCommand(id);
        return;
    }
    if (id == "measure.open") {
        // 選んでいるものを測って出す。何も選んでいなければ、何を選ぶかを言う。
        RefreshMeasurements();
        measureDock_->show();
        measureDock_->raise();
        SetStatus(measureDock_->SummaryText());
        return;
    }
    if (id == "snap.toggle") {
        snapEnabled_ = !snapEnabled_;
        // 吸着は「道具として切る」と「Ctrl で一時的に止める」の2つがある。
        // 画面がその両方をまとめて持つ。片方だけ見ると、Ctrl を離した瞬間に
        // 切ってあったはずの吸着が戻る。
        viewport_->SetSnapSuppressed(!snapEnabled_);
        SetStatus(snapEnabled_ ? QStringLiteral("吸着を入れました。")
                               : QStringLiteral("吸着を切りました(Ctrlでも一時的に止められます)。"));
        return;
    }
    // まだ入っていないものは、案内を出して何もしない。
    // 「押せるのに何も起きない」より、いま何ができないかを言う。
    SetStatus(QStringLiteral("%1: %2 (この操作はまだ入っていません)")
            .arg(QString::fromUtf8(std::string(command->labelJa).c_str()),
                QString::fromUtf8(std::string(command->operationGuideJa).c_str())));
}

void V2MainWindow::ApplyTheme(UiTheme theme)
{
    theme_ = theme;
    if (theme == UiTheme::Windows95) {
        Win95Style::SuspendApplicationStyleSheets();
        QApplication::setStyle(new Win95Style());
        QApplication::setPalette(Win95Style::Win95Palette());
        QApplication::setFont(Win95Style::Win95Font());
        viewport_->SetPalette(ViewportPalette::Win95());
    } else {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(QPalette());
        Win95Style::RestoreApplicationStyleSheets();
        viewport_->SetPalette(ViewportPalette::Dark());
    }
    update();
}
