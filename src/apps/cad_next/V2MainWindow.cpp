#include "V2MainWindow.h"

#include "Win95Style.h"

#include "kachakacha/app/OperationGuide.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
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

constexpr std::array<ToolBinding, 13> kToolBindings{{
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
    {"measure.open", DrawingTool::Measure},
}};

[[nodiscard]] QString ToolLabel(DrawingTool tool)
{
    return QString::fromUtf8(std::string(DrawingToolNameJa(tool)).c_str());
}

[[nodiscard]] CurveSegment MakeLine(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    return made.Value();
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
    viewport_->SetDocumentChangedCallback([this] { RefreshEntityList(); });

    SelectTool(DrawingTool::Line);
    RefreshCommandVisibility();
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
        {"編集(&E)", {"edit.undo", "edit.redo", "selection.activate", "snap.toggle",
                       "group.set_active"}},
        {"作図(&D)", {"draw.point", "draw.line", "draw.polyline", "draw.rectangle",
                       "draw.circle", "draw.arc", "draw.bezier", "draw.spline"}},
        {"編集操作(&W)", {"wire.trim", "wire.extend", "wire.split", "wire.join",
                            "wire.coincident", "wire.tangent", "wire.curvature",
                            "wire.chamfer", "wire.fillet", "wire.project"}},
        {"基準(&P)", {"workplane.create", "workplane.set_active", "grid.edit",
                       "grid.move_origin"}},
        {"形(&M)", {"guide.create", "part.extrude", "part.from_wire_cage",
                     "part.boolean_add", "part.boolean_cut", "derived.freeze"}},
        {"製作(&B)", {"fabrication.create", "fabrication.assign_role",
                       "fabrication.preview_update", "fabrication.create_pattern",
                       "fabrication.set_assembly", "fabrication.freeze_state"}},
        {"書き出し(&X)", {"export.validate", "export.stl", "export.step", "export.svg",
                            "export.dxf"}},
        {"表示(&V)", {"view.fit_all", "view.align_selection", "view.display_settings",
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
    guideDock->setWidget(guideTableView_);
    addDockWidget(Qt::RightDockWidgetArea, guideDock);

    auto* diagnosticDock = new QDockWidget(QStringLiteral("知らせ"), this);
    diagnosticDock->setObjectName(QStringLiteral("diagnosticDock"));
    diagnosticList_ = new QListWidget(diagnosticDock);
    diagnosticDock->setWidget(diagnosticList_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticDock);

    toolLabel_ = new QLabel(this);
    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(toolLabel_);
    statusBar()->addWidget(statusLabel_, 1);
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
    entityTree_->clear();
    const auto& snapshot = session_->GetDocument().Snapshot();
    for (const auto& entity : snapshot.entities) {
        auto* item = new QTreeWidgetItem(entityTree_);
        const QString name = entity.displayName.empty()
            ? QStringLiteral("(名前なし)")
            : QString::fromUtf8(entity.displayName.c_str());
        item->setText(0, name);
        item->setText(1, QString::fromUtf8(
            std::string(kachakacha::v2::domain::EntityKindNameJa(entity.kind)).c_str()));
    }
    entityTree_->expandAll();
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
    bool ok = true;
    switch (command->predicate) {
    case SelectionPredicate::Always:
    case SelectionPredicate::HasDocument:
        ok = true;
        break;
    case SelectionPredicate::HasUndo:
        ok = session_->GetDocument().CanUndo();
        break;
    case SelectionPredicate::HasRedo:
        ok = session_->GetDocument().CanRedo();
        break;
    case SelectionPredicate::HasVisibleGeometry:
        ok = !session_->Scene().curves.empty() || !session_->Scene().points.empty();
        break;
    default:
        // 選択に依る条件は、選択の仕組みが入るまで押せないままにする。
        // 隠さずに、理由を出す(command-catalog.md §1)。
        ok = false;
        break;
    }
    if (!ok && reasonOut != nullptr) {
        *reasonOut = QString::fromUtf8(
            std::string(command->predicateFailureJa).c_str());
    }
    return ok;
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
    // 作図の道具は道具へ入る。
    for (const ToolBinding& binding : kToolBindings) {
        if (binding.commandId == id) {
            SelectTool(binding.tool);
            SetStatus(QString::fromUtf8(
                std::string(command->operationGuideJa).c_str()));
            return;
        }
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
    if (id == "snap.toggle") {
        snapEnabled_ = !snapEnabled_;
        kachakacha::v2::modeling::SnapSettings settings;
        settings.suppressed = !snapEnabled_;
        session_->SetSnapSettings(settings);
        SetStatus(snapEnabled_ ? QStringLiteral("吸着を入れました。")
                               : QStringLiteral("吸着を切りました。"));
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

bool V2MainWindow::ApplyGuideTableState()
{
    // 形状ガイドの役割テーブル。外形U2本と断面2枚を入れた形。
    using kachakacha::v2::modeling::AddSelectionAsNewRow;
    using kachakacha::v2::modeling::ChainRole;
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    using kachakacha::v2::modeling::GuideTableSelection;
    guideTable_ = kachakacha::v2::modeling::GuideTable{};
    guideTable_.method = GuideSurfaceMethod::GuidedLoft;
    const auto make = [&](const char* label, Vector3 from, Vector3 to) {
        GuideTableSelection selection;
        selection.sourceWireId = kachakacha::v2::base::EntityId(ids_->Next());
        selection.label = label;
        selection.segments.push_back(CurveSegment::MakeLine(from, to).Value());
        return selection;
    };
    struct Entry {
        ChainRole role;
        const char* label;
        Vector3 from;
        Vector3 to;
    };
    const Entry entries[] = {
        {ChainRole::GuideU, "guide_lower", {0, 0, 0}, {100, 0, 0}},
        {ChainRole::GuideU, "guide_upper", {0, 0, 40}, {100, 0, 40}},
        {ChainRole::Section, "sec_left", {0, 0, 0}, {0, 0, 40}},
        {ChainRole::Section, "sec_right", {100, 0, 0}, {100, 0, 40}},
    };
    for (const Entry& entry : entries) {
        if (!SetGuideTable(AddSelectionAsNewRow(guideTable_, entry.role,
                make(entry.label, entry.from, entry.to)))) {
            return false;
        }
    }
    viewport_->SetViewDirection(ViewDirection::Isometric);
    return true;
}

bool V2MainWindow::ApplyStaticState(const QString& name)
{
    ClearDiagnostics();
    if (name == QStringLiteral("empty")) {
        return true;
    }
    if (name == QStringLiteral("grid")) {
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->SetVisibleWidthMm(120.0);
        return true;
    }
    if (name == QStringLiteral("tools")) {
        SelectTool(DrawingTool::Arc);
        return true;
    }
    if (name == QStringLiteral("curves") || name == QStringLiteral("curves-win95")) {
        // 直線・円弧・円・ベジェ・B-spline を1つずつ置く。
        // 曲線が曲線のまま描けているかを、画面で確かめるための状態。
        SnapScene scene = session_->Scene();
        // SnapCurve は既定で作れない(CurveSegment を必ず伴うため)。
        // その場で全部そろえて作る。
        const auto add = [&](const CurveSegment& segment, bool construction) {
            scene.curves.push_back(SnapCurve{kachakacha::v2::base::EntityId{},
                kachakacha::v2::base::SegmentId{}, segment, construction});
        };
        add(MakeLine({-60, -30, 0}, {-20, -30, 0}), false);
        add(MakeLine({-60, -30, 0}, {-60, 10, 0}), true);
        add(CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 25.0, 0.0,
                3.14159265358979323846).Value(),
            false);
        add(CurveSegment::MakeCircle({50, 0, 0}, {0, 0, 1}, {1, 0, 0}, 18.0).Value(),
            false);
        add(CurveSegment::MakeCubicBezier(
                {{-60, 30, 0}, {-40, 60, 0}, {0, 60, 0}, {20, 30, 0}})
                .Value(),
            false);
        add(CurveSegment::MakeCubicBSpline(
                {{30, 40, 0}, {45, 65, 0}, {65, 20, 0}, {85, 55, 0}, {100, 35, 0}})
                .Value(),
            false);
        session_->SetScene(std::move(scene));
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->FitToDocument();
        if (name.endsWith(QStringLiteral("win95"))) {
            ApplyTheme(UiTheme::Windows95);
        }
        return true;
    }
    return false;
}

bool V2MainWindow::ApplyManualState(const QString& name)
{
    if (ApplyStaticState(name)) {
        return true;
    }
    if (name == QStringLiteral("draw-line")) {
        // 道具を選んで2点置く。文書が変わり、一覧に出ることを確かめる。
        SelectTool(DrawingTool::Line);
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->SetVisibleWidthMm(200.0);
        viewport_->ClickAt(QPointF(viewport_->width() * 0.3, viewport_->height() * 0.6));
        viewport_->HoverAt(QPointF(viewport_->width() * 0.7, viewport_->height() * 0.4));
        viewport_->ClickAt(QPointF(viewport_->width() * 0.7, viewport_->height() * 0.4));
        RefreshEntityList();
        return true;
    }
    if (name == QStringLiteral("snap")) {
        // 既にある線の端点へ吸着させる。吸着の印と名前が出る。
        (void)ApplyManualState(QStringLiteral("curves"));
        SelectTool(DrawingTool::Line);
        const auto screen = viewport_->Mapping().Project(Vector3{-20, -30, 0});
        if (screen.has_value()) {
            viewport_->HoverAt(QPointF(screen->x, screen->y));
        }
        return true;
    }
    if (name == QStringLiteral("isometric")) {
        (void)ApplyManualState(QStringLiteral("curves"));
        viewport_->SetViewDirection(ViewDirection::Isometric);
        viewport_->FitToDocument();
        return true;
    }
    if (name == QStringLiteral("win95")) {
        ApplyTheme(UiTheme::Windows95);
        return true;
    }
    if (name == QStringLiteral("guide-table")) {
        return ApplyGuideTableState();
    }
    if (name == QStringLiteral("cursor-input")) {
        // カーソル連動の数値入力。長さをロックし、角度の欄へ式を入れた形。
        SelectTool(DrawingTool::Line);
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->SetVisibleWidthMm(200.0);
        viewport_->ClickAt(QPointF(viewport_->width() * 0.35, viewport_->height() * 0.6));
        viewport_->HoverAt(QPointF(viewport_->width() * 0.65, viewport_->height() * 0.4));
        if (!viewport_->OpenCursorInput()) {
            return false;
        }
        (void)viewport_->TypeIntoCursorField(QStringLiteral("(180/2)*3"));
        (void)viewport_->CommitCursorField();
        (void)viewport_->FocusNextCursorField(false);
        (void)viewport_->TypeIntoCursorField(QStringLiteral("30deg"));
        return true;
    }
    if (name == QStringLiteral("view-cube")) {
        // ビューキューブをドラッグした後の画面。90度へ吸着していないことを目で見る。
        (void)ApplyManualState(QStringLiteral("curves"));
        viewport_->SetViewDirection(ViewDirection::Isometric);
        viewport_->FitToDocument();
        const QRectF box = viewport_->ViewCubeRect();
        const QPointF press = box.center();
        (void)viewport_->PressViewCube(press);
        viewport_->DragViewCube(press + QPointF(37.0, -13.0));
        viewport_->ReleaseViewCube(press + QPointF(37.0, -13.0));
        return true;
    }
    if (name == QStringLiteral("mode-part")) {
        (void)ApplyManualState(QStringLiteral("curves"));
        SetMode(kachakacha::v2::app::UiMode::Part);
        return true;
    }
    if (name == QStringLiteral("mode-fabrication")) {
        (void)ApplyManualState(QStringLiteral("curves"));
        SetMode(kachakacha::v2::app::UiMode::Fabrication);
        return true;
    }
    if (name == QStringLiteral("mode-output")) {
        (void)ApplyManualState(QStringLiteral("curves"));
        SetMode(kachakacha::v2::app::UiMode::Output);
        return true;
    }
    if (name == QStringLiteral("guide")) {
        // 案内の6つがそろって出ている画面。
        (void)ApplyManualState(QStringLiteral("curves"));
        SelectTool(DrawingTool::Arc);
        return true;
    }
    return false;
}
