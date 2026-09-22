#include "V2MainWindow.h"
#include "V2SurfaceAnalysisTool.h"
#include "V2SurfaceEditTool.h"

#include "V2EntityTree.h"

#include "kachakacha/app/ExportContent.h"
#include "kachakacha/exporters/PdfWriter.h"
#include "kachakacha/app/CommandAvailability.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/ToolTargeting.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ToolFooter.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/io/KcdImport.h"

#include "Win95Style.h"

#include "kachakacha/app/OperationGuide.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <map>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPalette>
#include <QPoint>
#include <QPushButton>
#include <QSizePolicy>
#include <QStatusBar>
#include <QString>
#include <QStyleFactory>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <initializer_list>

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

constexpr std::array<ToolBinding, 21> kToolBindings{{
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
    // 部品の配置(P-18)。線と同じ道具・同じ点の置き方で、選んだ部品を動かす。
    {"part.move", DrawingTool::Move},
    {"part.copy", DrawingTool::Copy},
    {"part.mirror", DrawingTool::Mirror},
    {"part.rotate", DrawingTool::Rotate},
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
    // 正本の一番下の帯。3D の下に置く(UI の正本の footer)。
    // 状態の帯へ相乗りさせると、日本語の案内に押されて右が切れる。
    auto* central = new QWidget(this);
    auto* column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(viewport_, 1);
    auto* footer = new QWidget(central);
    auto* footerRow = new QHBoxLayout(footer);
    footerRow->setContentsMargins(8, 2, 8, 2);
    toolFooterLabel_ = new QLabel(footer);
    keyHintLabel_ = new QLabel(
        QString::fromUtf8(kachakacha::v2::app::ToolKeyHintJa().c_str()), footer);
    footerRow->addWidget(toolFooterLabel_, 1);
    footerRow->addWidget(keyHintLabel_, 0);
    column->addWidget(footer, 0);
    setCentralWidget(central);

    BuildMenus();
    BuildModeBar();
    BuildToolPalette();
    BuildPanels();

    WireViewportCallbacks();

    // Enter / Esc は窓が先に受ける。右の欄に焦点があっても効くようにする
    // (オーナー指示 2026-09-15 §14)。3D を一度クリックして焦点を戻す、を無くす。
    qApp->installEventFilter(this);

    // 空の文書にも原点の3面(top_XY / front_XZ / side_YZ)を置き、上面 XY を作業中にする。
    // V1 と同じく、開いた直後から「平面から離す」の相手が選べる。
    AdoptDocument(kachakacha::v2::document::DocumentSnapshot{});
    // 起動は「選択」。線の道具で始めると、画面を押した瞬間に線が引けてしまい、
    // 選ぶことができない(オーナー指摘 2026-09-11)。
    SelectTool(kachakacha::v2::app::ToolAfterModeChange());
    SetMode(UiMode::Drawing);
    ApplyTheme(UiTheme::Normal);
    setWindowTitle(QStringLiteral("kachakachaCAD %1")
            .arg(QString::fromStdString(kachakacha::v2::base::ProductVersionString())));
    resize(1180, 760);
}

//! 画面(V2Viewport)から窓へ戻ってくる知らせを、まとめて繋ぐ。
//! 繋ぎ先はどれも窓の役目(文書を変える・棚を書き直す)なので、ここに集める。
void V2MainWindow::WireViewportCallbacks()
{
    viewport_->SetStatusCallback([this](const std::string& text) {
        SetStatus(QString::fromUtf8(text.c_str()));
    });
    viewport_->SetDocumentChangedCallback([this] {
        viewport_->PruneSelection();
        RefreshEntityList();
    });
    // 押し出しの矢印を引いたら、右の欄と破線を合わせる。
    viewport_->SetExtrudeDistanceCallback([this](double distanceMm) {
        UpdateExtrudePreview(distanceMm);
    });
    // Enter で確定、Esc でやめる。中身は窓が持っている。
    viewport_->SetExtrudeCallbacks([this] { ConfirmExtrude(); },
        [this] {
            EndExtrudePreview();
            SetStatus(QStringLiteral("押し出し: やめました。"));
        });
    // Esc で選択道具へ戻す(V1同等)。道具は窓が持っているので、窓が引き受ける。
    // 測定を重ねていたなら元の道具へ(C-16)。そうでなければ選択道具へ。
    viewport_->SetBackToSelectCallback([this] { BackToSelectOrResume(); });
    viewport_->SetMeasureResumeAvailable([this] { return toolBeforeMeasure_.has_value(); });
    // カーソルが動いたら状態行の座標を書き直す。
    viewport_->SetHoverChangedCallback([this] { OnViewportHoverChanged(); });
    // 移動・複製・鏡映・回転。点がそろったら、選んでいる線へ当てる。
    viewport_->SetTransformCallback(
        [this](const kachakacha::v2::modeling::TransformPlan& plan) {
            ApplyTransformPlan(plan);
        });
    // 押し出しは右の棚で決める(オーナー指示 2026-09-14 §7)。
    // ここで窓を据え付けない。据え付けると、押すたびに窓が出て、
    // 「見ながら決める」ができなくなる。窓は「詳細...」を押したときだけ出す。
    // 作業平面は窓ではなく右の棚(V1 の「平面を作る」タブ)で作る。
    // 12通りの作り方と数の欄を持ち、「平面を作る」で文書へ入れる。
    // 組立率も窓で聞かない。右の棚の組立率の欄(スライダ・基準値と同じ道)へ案内する
    // (V2FabricationCommands.cpp の fabrication.set_assembly)。窓と棚で別々に値を持たない。
    // 並べ方は右の棚(V2ArrayDock、指示書 D-23)で聞く。ここで窓(V2ArrayDialog)を
    // 差し込むと棚が出ず、自己試験では窓が閉じられないまま止まる(PC 検証 2026-09-19、
    // HP-AR-01 が 900 秒で打ち切られた)。窓は自己試験の差し替え口としてだけ残す。
    // 制御点を掴んで動かした結果。文書を変えるのは窓の役目。
    viewport_->SetControlPointCallback(
        [this](kachakacha::v2::base::EntityId entityId,
            kachakacha::v2::base::SegmentId segmentId,
            const kachakacha::v2::geometry::CurveSegment& replacement) {
            ReplaceWireSegment(entityId, segmentId, replacement);
        });
    // 選択道具での右クリック。V1と同じで、ここだけメニューを出す。
    // 窓は「出して、選ばれた候補の番号を返す」だけ。候補を集めるのも、
    // 選択へ入れるのも画面(V2Viewport)の役目である。
    viewport_->SetContextMenuCallback(
        [this](const QPoint& at, const std::vector<QString>& candidates) {
            return ShowSelectMenuWithCandidates(at, candidates);
        });
    viewport_->SetPendingCommandCallbacks([this] { ConfirmPendingCommand(); },
        [this] { ClearPendingCommand(); });
    viewport_->SetSelectionChangedCallback([this] { HandleSelectionChanged(); });
    // 操作板の「選択に正対」は、台帳のコマンドと同じ道を通す。入口を分けない。
    viewport_->SetAlignSelectionCallback([this] { RunCommand("view.align_selection"); });
}

//! 3D か左の一覧で選択が変わるたびに、関わる棚を持ち直す(WireViewportCallbacks の続き)。
void V2MainWindow::HandleSelectionChanged()
{
    // 3D 画面で選んだものを、左の一覧でも光らせる(V1 と同じ。逆も同じ)。
    HighlightTreeForSelection();
    // 構えている命令があれば、そろったかを見る。
    RefreshPendingCommand(false);
    // 面作成中の素のクリックは、**いまの欄**へ入る(もう一度押すと外れる)。
    // 「選んでから右棚の追加ボタンを押す」を基本操作にしない。
    RefreshSurfaceForSelectionChange();
    // 近似中の素のクリックは対象へ入る(面と立体だけ)。押すたびに候補を作り直す。
    RefreshApproxForSelectionChange();
    // 足す引く中の素のクリックは土台 → 相手の順に入る(押し直すと外れる)。
    RefreshBooleanForSelectionChange();
    // 厚み中の素のクリックは面の欄へ入る(押し直すと外れる)。
    RefreshThickenForSelectionChange();
    // 面の編集中の素のクリックは縁・面・線の欄へ入る(押し直すと外れる)。
    if (surfaceEdit_ != nullptr) {
        surfaceEdit_->HandleSelectionChanged();
    }
    // 製作モードで部材を押したら「対象部材」欄をその番号にする(F-05/06/07)。
    RefreshFabricationPartPickForSelectionChange();
    // 下見を出している最中なら、写しと下見を選択に合わせる(§9)。
    RefreshExtrudeForSelectionChange();
    RefreshExportCounts();
    RefreshProcessContextFromSelection();
    RefreshMeasurements();
    RefreshEditDock();
    RefreshCornerDock();
    RefreshFabricationDock();
    RefreshPartDock();
    // 作業平面の棚は「いま何を選んでいるか」で作れるかが変わる。
    RefreshWorkPlaneDock();
    // 面の解析は選んだ面を塗る(選んでいなければ全部の面)。
    if (surfaceAnalysis_ != nullptr) {
        surfaceAnalysis_->Refresh();
    }
    // 選択が変われば押せるものも変わる。押せる形を選択に付いてこさせる。
    RefreshCommandVisibility();
}

V2MainWindow::~V2MainWindow() = default;

void V2MainWindow::SetMode(UiMode mode)
{
    // モードを変えても、選んでいるものも、作った形も、一切触らない。
    // 変わるのは「どのコマンドが出ているか」だけである(UIX-001 / 003)。
    mode_ = mode;
    for (auto& entry : modeActions_) {
        entry.second->setChecked(entry.first == mode);
    }
    // 道具は白紙へ戻す。前の道具が残っていると、部品モードへ移った直後に
    // 画面を押して線が引ける。モードを変えるのは「何を相手にするか」を
    // 変えることなので、道具も戻すのが素直である(オーナー指摘 2026-09-11)。
    if (session_->CurrentTool() != kachakacha::v2::app::ToolAfterModeChange()) {
        SelectTool(kachakacha::v2::app::ToolAfterModeChange());
    }
    // 構えていた命令も捨てる。別のモードへ移ったなら、その命令はもう関係ない。
    ClearPendingCommand();
    RefreshCommandVisibility();
    if (ribbon_ != nullptr) {
        ribbon_->ShowMode(mode);   // 帯のカテゴリと道具はモードで入れ替わる
    }
    // 右に出す棚は「いまの道具とモード」で決まる(core の ShelfLayout)。
    // 全部出しっぱなしにすると、1枚あたりが 80px まで潰れて見出しだけが並ぶ。
    RefreshRightShelves();
    RefreshGridSuppression();
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
        const bool ready = CommandEnabled(entry.first, &reason);
        const CommandDescriptor* command = FindCommand(entry.first);
        const bool canSelectTarget = command != nullptr
            && kachakacha::v2::app::PredicateCanBeSatisfiedBySelection(
                command->predicate);
        const bool enabled = ready || canSelectTarget;
        entry.second->setEnabled(enabled);
        if (!ready && !reason.isEmpty()) {
            // 対象不足なら入口は有効のまま、選ぶ対象をツールチップで示す。
            const QString suffix = canSelectTarget
                ? QStringLiteral(" 道具を開始してから対象を選べます。")
                : QString();
            entry.second->setToolTip(reason + suffix);
        }
    }
    // 固有の作図道具は作図モードだけ。ほかのモードは台帳 QAction の専用列を使う。
    for (QAction* action : toolActions_) {
        action->setVisible(mode_ == UiMode::Drawing);
    }
}

bool V2MainWindow::IsToolBoundCommand(std::string_view id)
{
    for (const ToolBinding& binding : kToolBindings) {
        if (binding.commandId == id) {
            return true;
        }
    }
    return false;
}

void V2MainWindow::BuildToolPalette()
{
    toolPalette_ = addToolBar(QStringLiteral("道具"));
    toolPalette_->setObjectName(QStringLiteral("toolPalette"));
    toolPalette_->setMovable(false);
    toolPalette_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    std::map<DrawingTool, QAction*> actions;
    for (const DrawingTool tool : kToolOrder) {
        auto* action = new QAction(ToolLabel(tool), this);
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
        actions.emplace(tool, action);
        QObject::connect(action, &QAction::triggered, this,
            [this, tool] { SelectTool(tool); });
    }

    // 2段の帯(カテゴリ → 道具)。並びは core の app/Ribbon が決める(正本 3 HTML)。
    // 道具の QAction は近道(L / C / A …)とメニューのために窓へも足しておく。
    std::map<std::string, QAction*> byCommand;
    for (QAction* action : toolActions_) {
        addAction(action);
    }
    for (const ToolBinding& binding : kToolBindings) {
        if (const auto found = actions.find(binding.tool); found != actions.end()) {
            byCommand.emplace(std::string(binding.commandId), found->second);
        }
    }
    BuildRibbon(byCommand);
}

//! 左の一覧(V1 のモデルツリー)を組み立てる。
//! 絞り込みの欄・名前の書き換え・右クリック・3D 画面との往復をここで繋ぐ。
QDockWidget* V2MainWindow::BuildEntityTreeDock()
{
    auto* treeDock = new QDockWidget(QStringLiteral("作ったもの"), this);
    treeDock->setObjectName(QStringLiteral("entityDock"));
    entityTree_ = new V2EntityTree(treeDock);
    entityTree_->setColumnCount(2);
    entityTree_->setHeaderLabels(
        {QStringLiteral("名前"), QStringLiteral("種類")});
    entityTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    entityTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    // 種類の列は隠す(正本の一覧は名前と印だけ)。列の字は絞り込みと試験が読むので残す。
    // 出しておくと、入れ子の深い行で名前の列が潰れて名前が消えた(PC 画面 2026-09-19)。
    entityTree_->setColumnHidden(1, true);
    entityTree_->setIndentation(14);
    // V1 と同じく、まとめて選べる。左の一覧で選んだものは 3D 画面でも選ばれる。
    entityTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QObject::connect(entityTree_, &QTreeWidget::itemSelectionChanged, this,
        [this] { AdoptTreeSelection(); });
    // 名前を書き換えたら文書へ入れる。判断(空か、変わったか)は core にある。
    // 行の変更を処理すると文書から木を作り直す。DirectConnection のままでは、
    // QTreeWidgetItem::setData の途中でその行自身を削除してしまう。
    QObject::connect(entityTree_, &QTreeWidget::itemChanged, this,
        [this](QTreeWidgetItem* item, int column) {
            if (column != 0) {
                return;
            }
            // 軸の行はチェックで表示を切り替える。名前の書き換えではない。
            for (int axis = 0; axis < 3; ++axis) {
                if (axisItems_[static_cast<std::size_t>(axis)] == item) {
                    viewport_->SetAxisVisible(axis, item->checkState(0) == Qt::Checked);
                    return;
                }
            }
            // グループの行。チェックは出し隠し、文字は名前の書き換え。
            if (RenameOrToggleGroupFromItem(item)) {
                return;
            }
            // ものの行の ◉。1つずつの出し隠し。
            if (ToggleEntityVisibilityFromItem(item)) {
                return;
            }
            RenameEntityFromItem(item);
        }, Qt::QueuedConnection);
    // 引きずって移す(オーナー指示 §9)。木の中だけで動かす。
    // Qt に行を動かさせず、**落ちた先を聞いて文書のほうを変える。**
    // 木は文書から作り直すので、木だけ動かしても次の作り直しで元へ戻る。
    entityTree_->SetDropHandler(
        [this](const std::vector<QTreeWidgetItem*>& moved, QTreeWidgetItem* onto) {
            DropTreeItemsOnto(moved, onto);
        });
    // 右クリックは一覧の献立(名前変更/表示・非表示/正対/グループへ移動/複製/削除/プロパティ)。
    // 並ぶのは台帳のコマンドだけ。別の入口を作ると、押せるかどうかの判断も文言も二重になる。
    entityTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(entityTree_, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& at) { ShowExplorerMenu(at); });

    // 絞り込みの欄(V1 の「名前・種類で絞り込み」)。
    // 物が増えると一覧は数十行になり、目で探すのはすぐに無理になる。
    auto* treeBody = new QWidget(treeDock);
    auto* treeLayout = new QVBoxLayout(treeBody);
    treeLayout->setContentsMargins(4, 4, 4, 4);
    treeLayout->setSpacing(4);
    entityFilter_ = new QLineEdit(treeBody);
    entityFilter_->setClearButtonEnabled(true);
    entityFilter_->setPlaceholderText(QStringLiteral("名前・種類で絞り込み"));
    entityFilter_->setToolTip(QStringLiteral(
        "ワイヤー、作業平面などの種類名か、付けた名前を打つと絞り込みます。"));
    QObject::connect(entityFilter_, &QLineEdit::textChanged, this,
        [this] { ApplyEntityTreeFilter(); });
    treeLayout->addWidget(entityFilter_);
    treeLayout->addWidget(entityTree_, 1);
    treeDock->setWidget(treeBody);
    treeDock->setMinimumWidth(220);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);
    return treeDock;
}

void V2MainWindow::BuildPanels()
{
    QDockWidget* treeDock = BuildEntityTreeDock();

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
    guideDock->setWidget(BuildGuideTableBody(guideDock));
    guideDock_ = guideDock;

    BuildRemainingPanels(treeDock);
}

//! 形状ガイドの役割の表と、表を動かすボタン。
//! ボタンは表の隣に置く(オーナー指摘 2026-09-11)。上の帯に並べていたので、
//! どの行に効くのかが見た目から読めなかった。
QWidget* V2MainWindow::BuildGuideTableBody(QWidget* parent)
{
    auto* guideDock = parent;
    auto* guideBody = new QWidget(guideDock);
    auto* guideLayout = new QVBoxLayout(guideBody);
    guideLayout->setContentsMargins(6, 6, 6, 6);
    guideLayout->setSpacing(4);
    guideLayout->addWidget(guideTableView_, 1);
    auto* guideButtons = new QWidget(guideBody);
    auto* guideButtonLayout = new QVBoxLayout(guideButtons);
    guideButtonLayout->setContentsMargins(0, 0, 0, 0);
    guideButtonLayout->setSpacing(2);
    for (const auto& entry : {
             std::pair<const char*, const char*>{"面の作り方", "guide.set_method"},
             {"選択を表へ", "guide.add_row"},
             {"選択を既存行へ追加", "guide.append_row"},
             {"行を上へ", "guide.row_up"},
             {"行を下へ", "guide.row_down"},
             {"行を削除", "guide.row_remove"},
             {"向きを反転", "guide.row_reverse"},
             {"表から面を作る", "guide.build"},
             {"表を空にする", "guide.clear"},
             {"回転体を作る", "guide.revolve"}}) {
        auto* button = new QPushButton(QString::fromUtf8(entry.first), guideButtons);
        const std::string command = entry.second;
        QObject::connect(button, &QPushButton::clicked, this,
            [this, command] { RunCommand(command); });
        guideButtonLayout->addWidget(button);
    }
    guideLayout->addWidget(guideButtons);
    return guideBody;
}

//! 手順・書き出し・右の棚・知らせ。BuildPanels の続き。
//! 1つの関数が 100 行に届いたので分けた。並べる順は変えていない。
void V2MainWindow::BuildRemainingPanels(QDockWidget* treeDock)
{
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
    // 手順は右ではなく左の下へ。右は「いま選んでいる道具の設定」だけにする。
    // 右に置くと、道具の設定が手順に押されて見えなくなる(オーナー指摘 2026-09-11)。
    addDockWidget(Qt::LeftDockWidgetArea, processDock);
    splitDockWidget(treeDock, processDock, Qt::Vertical);
    processDock_ = processDock;
    // 正本(3 HTML 2026-09-18)の左は一覧だけ。手順は V1 の名残で、案内は状態行と HUD が
    // 持つようになったので、はじめは畳んでおく(表示メニューから出せる)。
    // 出したままだと 1280x720 で一覧が半分に潰れる(PC 検証 2026-09-19)。
    processDock->hide();
    if (viewMenu_ != nullptr) {
        QAction* toggle = processDock->toggleViewAction();
        toggle->setText(QStringLiteral("手順の一覧(&P)"));
        viewMenu_->addAction(toggle);
    }

    BuildExportDock();
    BuildRightShelves();

    auto* diagnosticDock = new QDockWidget(QStringLiteral("知らせ"), this);
    diagnosticDock->setObjectName(QStringLiteral("diagnosticDock"));
    diagnosticList_ = new QListWidget(diagnosticDock);
    diagnosticDock->setWidget(diagnosticList_);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticDock);
    diagnosticDock_ = diagnosticDock;
    diagnosticDock_->hide();

    // 棚の広さを決める。決めないと、部品モードで右が 120px まで狭まり、
    // 見出しが切れ、手順が2行しか見えなくなる。
    // 横幅を先に決めてから、縦の割り当てを決める。
    resizeDocks({treeDock, operationDock_}, {240, 380}, Qt::Horizontal);
    // 左は一覧が主で、手順はその下。一覧を潰さない割り当てにする。
    resizeDocks({processDock_}, {180}, Qt::Vertical);
    BuildStatusBar();
}

//! 一番下の一行を書き直す。道具が動いていなければ空にする。
void V2MainWindow::ShowToolFooter(const QString& line)
{
    if (toolFooterLabel_ == nullptr) {
        return;
    }
    toolFooterLabel_->setText(line);
}

QString V2MainWindow::ToolFooterTextJa() const
{
    return toolFooterLabel_ == nullptr ? QString() : toolFooterLabel_->text();
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
        return QStringLiteral("グループ: (なし)");
    }
    for (const auto& group : session_->GetDocument().Snapshot().groups) {
        if (group.id == *settings.activeGroupId) {
            return QStringLiteral("グループ: %1")
                .arg(QString::fromStdString(group.displayName));
        }
    }
    return QStringLiteral("グループ: (なし)");
}

void V2MainWindow::SetAxisShown(int axis, bool shown)
{
    if (axis < 0 || axis >= 3 || axisItems_[static_cast<std::size_t>(axis)] == nullptr) {
        return;
    }
    // チェックを変えると itemChanged が走り、画面の軸が切り替わる。試験も同じ道を通す。
    axisItems_[static_cast<std::size_t>(axis)]->setCheckState(0,
        shown ? Qt::Checked : Qt::Unchecked);
    QApplication::processEvents();
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
    // 原点の基準平面(top_XY / front_XZ / side_YZ)が無ければ足す。V1 と同じく
    // 一覧の最上部に固定で出し、「平面から離す」などの相手として最初から選べるようにする。
    kachakacha::v2::app::EnsureOriginPlanes(session_->GetDocument(), *ids_);
    // 原点の3面は文書の土台であって操作ではない。開いた直後に「元に戻す」で
    // 消えてしまわないよう、ここで履歴の境界にする(開く・新規と同じ扱い)。
    session_->GetDocument().MarkHistoryBoundary();
    // 文書が入れ替わった。構えていた道具はやめる。古い入力を新しい文書へ持ち越さない。
    if (surfaceShelfShown_) { EndSurfacePreview(); }
    if (approxShelfShown_) { EndApprox(); }
    if (booleanShelfShown_) { EndBoolean(); }
    if (thickenShelfShown_) { EndThicken(); }
    // 線を場面へ並べ直す。見ている場所は変えない。
    session_->SetScene(kachakacha::v2::app::RebuildSceneKeepingView(session_->Scene(),
        session_->GetDocument().Snapshot(), *ids_));
    viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
    // 作業中の平面が文書に無ければ、上面 XY を作業中にする。
    if (session_->GetDocument().FindEntity(activeWorkPlaneId_) == nullptr) {
        const auto top = kachakacha::v2::app::OriginPlaneId(
            session_->GetDocument().Snapshot(), kachakacha::v2::modeling::StandardPlaneKind::XY);
        if (top.has_value()) {
            ApplyWorkPlane(kachakacha::v2::modeling::StandardPlane(
                               kachakacha::v2::modeling::StandardPlaneKind::XY),
                *top);
        }
    }
    // 立体と面を作り方から作り直す。作り直さないと、線だけが残って
    // 立体が消えたことに気づかないまま、出そうとしたときに初めて分かる。
    RebuildKernelShapes();
    RefreshEntityList();
    RefreshExportCounts();
    RefreshWorkPlaneDock();
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
    // 開くときは V1 の .kcd も選べる(読み込んで V2 の文書にする)。保存は kcd2 だけ。
    const QString filter = forSave
        ? QStringLiteral("kachakachaCAD の文書 (*.kcd2)")
        : QStringLiteral("kachakachaCAD の文書 (*.kcd2 *.kcd);;V1 の文書 (*.kcd)");
    return forSave
        ? QFileDialog::getSaveFileName(this, QStringLiteral("保存する"), QString(), filter)
        : QFileDialog::getOpenFileName(this, QStringLiteral("開く"), QString(), filter);
}

bool V2MainWindow::ImportKcdFile(const QString& path)
{
    // V1 の .kcd。読めるものは Feature にし、読めないものは名前を挙げて知らせる。
    // 開いたあとの保存先は決めない(kcd2 で名前を付けて保存する)。V1 のファイルは触らない。
    const auto text = kachakacha::v2::io::ReadWholeFile(path.toStdString());
    if (!text.HasValue()) {
        ReportDiagnostics(text.Diagnostics());
        return false;
    }
    const auto imported = kachakacha::v2::io::ImportKcdScript(text.Value(), *ids_);
    if (!imported.HasValue()) {
        ReportDiagnostics(imported.Diagnostics());
        return false;
    }
    AdoptDocument(imported.Value().snapshot);
    if (session_->GetDocument().Snapshot().entities.size() < 3
        && !imported.Value().snapshot.entities.empty()) {
        return false;   // ResetTo が断った(原点の3面すら無い)。
    }
    documentPath_.clear();
    for (const auto& note : imported.Value().notes) {
        AddDiagnostic(QString::fromStdString(note.code + " " + note.summaryJa + " "
            + note.detailsJa));
    }
    SetStatus(QStringLiteral("%1 を V1 の文書として読みました(%2 命令、読み飛ばし %3)。"
                             "保存は kcd2 で名前を付けてください。")
            .arg(path)
            .arg(imported.Value().readCommands)
            .arg(imported.Value().skippedCommands));
    return true;
}

bool V2MainWindow::OpenDocumentFile(const QString& path)
{
    if (kachakacha::v2::io::LooksLikeKcdPath(path.toStdString())) {
        return ImportKcdFile(path);
    }
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

//! 手順の「選んでいる数」は画面が数え直さず、選択の側から取る(RefreshExportCounts と同じ)。
//! 数えないままだと、線を引いても「線を1本以上描いてください」が消えなかった
//! (PC の絵 2026-09-19)。
void V2MainWindow::RefreshProcessContextFromSelection()
{
    if (viewport_ == nullptr) {
        return;
    }
    const auto& snapshot = session_->GetDocument().Snapshot();
    const auto& selection = viewport_->Selection();
    processContext_.selectedWireCount = kachakacha::v2::app::SelectedCountOfKind(selection,
        snapshot, kachakacha::v2::domain::EntityKind::Wire);
    processContext_.selectedPartCount = kachakacha::v2::app::SelectedCountOfKind(selection,
        snapshot, kachakacha::v2::domain::EntityKind::Part);
    processContext_.selectedEntityCount = static_cast<int>(selection.entityIds.size());
    RefreshProcessSteps();
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
    // 帯に見えている道具の数(押せるものだけ)。
    int count = 0;
    if (ribbon_ != nullptr) {
        for (const QString& label : ribbon_->ToolLabels()) {
            count += ribbon_->ToolEnabled(label) ? 1 : 0;
        }
    }
    return count;
}

int V2MainWindow::CurrentProcessStep() const
{
    return kachakacha::v2::app::CurrentStepNumber(
        kachakacha::v2::app::BuildProcessSteps(mode_, processContext_));
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
    RememberToolForMeasure(tool);   // 測定へ持ち替えるなら、いまの道具を戻り先に(C-16)
    session_->SelectTool(tool);
    // 道具を替えたら、見せているだけの案は捨てる。
    // 文書は触っていないので、捨てるだけで元どおりである。
    ForgetPendingPartition();
    viewport_->OnToolChanged();
    for (std::size_t index = 0; index < toolActions_.size(); ++index) {
        toolActions_[index]->setChecked(kToolOrder[index] == tool);
    }
    // 案内文は core が持っている。画面で作らない。
    // 道具の名前・いまの手順・次の手順・決め方・やめ方・選択数の6つを必ず出す。
    RefreshGuide();
    // その道具の設定だけを右に出す。道具を選んだのに欄が出てこない、をなくす。
    RefreshRightShelves();
    RefreshCornerPreview();   // 面取りの道具を持った/離したときに下見を出す/片づける
    RefreshRibbonState();
    // 近道やメニューで持った道具も、帯ではそのカテゴリが前に出る(帯と道具を食い違わせない)。
    for (const ToolBinding& binding : kToolBindings) {
        if (binding.tool == tool && ribbon_ != nullptr && tool != DrawingTool::Select) {
            ribbon_->RevealCommand(binding.commandId);
        }
    }
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
    // 画面の中央はポインタの位置ではないので、吸着の持ち越しを変えない PeekHover で見る。
    const auto hover = session_->PeekHover(kachakacha::v2::geometry::ScreenPoint{
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
        QString text = QStringLiteral("%1 %2")
            .arg(QString::fromStdString(diagnostic.code),
                QString::fromStdString(diagnostic.summaryJa));
        // 手当ての手がかりは細かいほうに書いてある。
        // 「出来た面が線を通っていません」だけでは、どれだけ外れたのか分からない。
        if (!diagnostic.detailsJa.empty()) {
            text += QStringLiteral(" ") + QString::fromStdString(diagnostic.detailsJa);
        }
        AddDiagnostic(text);
        SetStatus(text);
    }
}

void V2MainWindow::SetStatus(const QString& text)
{
    if (statusLabel_) {
        statusLabel_->setText(text);
    }
    // 案内が変われば HUD と左右の札も同じ状態を映す(C-11 / C-12)。
    RefreshStatusLine();
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
    if (diagnosticDock_ != nullptr) {
        diagnosticDock_->show();
        diagnosticDock_->raise();
        resizeDocks({diagnosticDock_}, {96}, Qt::Vertical);
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
    const CommandDescriptor* command = FindCommand(id);
    if (command == nullptr) {
        // 台帳に無い id を押した。黙って何も起きないのがいちばん困るので、そう言う
        // (人には出ないはずの道。献立や帯の id を直し忘れたときに、ここで気づく)。
        SetStatus(QStringLiteral("「%1」という操作は台帳にありません(画面と台帳が食い違っています)。")
                .arg(QString::fromUtf8(std::string(id).c_str())));
        return;
    }
    // 道具に結びついた命令は、まず道具を構える。相手はそのあと選ぶ。
    if (EnterToolFor(*command)) {
        return;
    }
    // 面を作る・近似・足す引くは、棚を構えてから 3D で相手を選ぶ(構えて待つ道は通らない)。
    if (BeginToolFirstCommand(id)) {
        return;
    }
    // まだ使えない命令は、断って終わりにせず **構えて待つ**。
    // 「道具を選ぶ → 相手を選ぶ」の順で使えるようにする(オーナー指摘 2026-09-11)。
    // 選んでも直らないもの(戻せる履歴が無い等)は、ここで理由を出して終わる。
    if (ArmCommandIfUnsatisfied(id)) {
        return;
    }
    // ここまで来たら走らせる。走る前に構えを解く(別の命令を押したとき用)。
    ClearPendingCommand();
    if (id == "edit.undo" || id == "edit.redo") {
        RunHistoryCommand(id == "edit.undo");
        return;
    }
    if (id == "help.copy_diagnostics") {
        CopyDiagnostics();
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
    if (IsArrayCommand(id)) {
        RunArrayCommand(id);
        return;
    }
    if (IsGroupCommand(id)) {
        RunGroupCommand(id);
        return;
    }
    if (IsViewCommand(id)) {
        RunViewCommand(id);
        return;
    }
    if (IsShelfCommand(id)) {
        RunShelfCommand(id);
        return;
    }
    if (id == "snap.toggle") {
        ToggleSnap();
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
        // Windows 95 風で設定したビットマップ向けフォントを残さない。
        // スタイルだけ戻しても QApplication のフォントは自動では戻らず、
        // 通常表示のメニューまで狭く・ぎざぎざに見えていた。
        QApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        Win95Style::RestoreApplicationStyleSheets();
        viewport_->SetPalette(ViewportPalette::Dark());
    }
    // 見た目を変えたら、棚に出している色も同じにする。
    if (gridDock_ != nullptr) {
        gridDock_->SetChoice(CurrentGridChoice());
    }
    if (displayDock_ != nullptr) {
        displayDock_->SetChoice(CurrentDisplayChoice(), displayStage_);
    }
    update();
}
