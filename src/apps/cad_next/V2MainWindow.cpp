#include "V2MainWindow.h"

#include "Win95Style.h"

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
#include <QMenuBar>
#include <QStatusBar>
#include <QStyleFactory>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <array>

using kachakacha::v2::app::DrawingSession;
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
    BuildToolPalette();
    BuildPanels();

    viewport_->SetStatusCallback([this](const std::string& text) {
        SetStatus(QString::fromUtf8(text.c_str()));
    });
    viewport_->SetDocumentChangedCallback([this] { RefreshEntityList(); });

    SelectTool(DrawingTool::Line);
    ApplyTheme(UiTheme::Normal);
    setWindowTitle(QStringLiteral("kachakachaCAD %1")
            .arg(QString::fromStdString(kachakacha::v2::base::ProductVersionString())));
    resize(1180, 760);
}

V2MainWindow::~V2MainWindow() = default;

void V2MainWindow::BuildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("ファイル(&F)"));
    fileMenu->addAction(QStringLiteral("終了(&X)"), this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("編集(&E)"));
    editMenu->addAction(QStringLiteral("元に戻す(&U)"), this, [this] {
        if (session_->Undo()) {
            RefreshEntityList();
            SetStatus(QStringLiteral("元に戻しました。"));
            viewport_->update();
        } else {
            SetStatus(QStringLiteral("戻せる操作がありません。"));
        }
    });
    editMenu->addAction(QStringLiteral("やり直す(&R)"), this, [this] {
        if (session_->Redo()) {
            RefreshEntityList();
            SetStatus(QStringLiteral("やり直しました。"));
            viewport_->update();
        } else {
            SetStatus(QStringLiteral("やり直せる操作がありません。"));
        }
    });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("表示(&V)"));
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
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("全体を表示"), this, [this] {
        viewport_->FitToDocument();
        SetStatus(QStringLiteral("全体を表示しました。"));
    });

    QMenu* themeMenu = menuBar()->addMenu(QStringLiteral("見た目(&T)"));
    themeMenu->addAction(QStringLiteral("通常"), this,
        [this] { ApplyTheme(UiTheme::Normal); });
    themeMenu->addAction(QStringLiteral("Windows 95 風"), this,
        [this] { ApplyTheme(UiTheme::Windows95); });
}

void V2MainWindow::BuildToolPalette()
{
    toolPalette_ = addToolBar(QStringLiteral("道具"));
    toolPalette_->setObjectName(QStringLiteral("toolPalette"));
    toolPalette_->setMovable(false);
    for (const DrawingTool tool : kToolOrder) {
        QAction* action = toolPalette_->addAction(ToolLabel(tool));
        action->setCheckable(true);
        action->setToolTip(ToolLabel(tool));
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
    const auto hover = session_->Hover(kachakacha::v2::geometry::ScreenPoint{
        viewport_->width() * 0.5, viewport_->height() * 0.5});
    SetStatus(QString::fromUtf8(hover.messageJa.c_str()));
    viewport_->update();
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

bool V2MainWindow::ApplyManualState(const QString& name)
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
    return false;
}
