#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2EntityTree.h"
#include "V2SurfaceDock.h"
#include "V2WorkPlaneDock.h"
#include "V2ExtrudeDock.h"
#include "V2ProjectionTool.h"
#include "V2ParameterDock.h"
#include "V2SelfTestPrimitiveUi.h"
#include "V2SelfTestDeleteDialog.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/document/Commands.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <algorithm>
#include <cmath>

namespace kachakacha::v2::selftest {
namespace {
using geometry::Vector3;
using base::EntityId;
using domain::EntityKind;

void Click(QWidget& widget, QPointF at)
{
    QMouseEvent move(QEvent::MouseMove, at, widget.mapToGlobal(at.toPoint()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);
    for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, at, widget.mapToGlobal(at.toPoint()), Qt::LeftButton,
            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &event);
    }
    QApplication::processEvents();
}

bool ClickWorld(V2MainWindow& w, Vector3 point)
{
    QApplication::processEvents();
    auto& viewport = w.Viewport();
    const auto screen = viewport.Mapping().Project(point);
    if (!screen) return false;
    const QPointF at(screen->x, screen->y);
    if (!viewport.rect().contains(at.toPoint()) || viewport.ViewButtonAt(at)) return false;
    Click(viewport, at);
    return true;
}

bool StartFaceTool(V2MainWindow& w)
{
    for (auto* button : w.findChildren<QToolButton*>()) {
        if (button->defaultAction() == w.ActionFor("workplane.from_face") && button->isVisible()) {
            Click(*button, button->rect().center());
            return true;
        }
    }
    return Explain("面で作図の入口が画面にある", false);
}

void Shot(V2MainWindow& w, const char* name)
{
    const auto path = qEnvironmentVariable("KACHACAD_FACE_DRAWING_PROOF_DIR");
    if (path.isEmpty()) return;
    QDir dir(path);
    dir.mkpath(".");
    w.grab().save(dir.filePath(QString::fromUtf8(name) + ".png"));
}

EntityId MakeGuide(V2MainWindow& w, double height = 12, bool tilted = false, bool holes = false)
{
    const auto initialPlane = w.ActiveWorkPlaneId();
    w.SetWorkPlaneChooser([height, tilted](app::WorkPlaneChoice choice, const app::WorkPlaneFacts&) {
        choice.method = modeling::WorkPlaneMethod::PointNormal;
        choice.origin = {0, 0, height};
        choice.normal = tilted ? Vector3{-.2, -.3, 1} : Vector3{0, 0, 1};
        choice.uAxis = tilted ? Vector3{1, 0, .2} : Vector3{1, 0, 0};
        return std::optional(choice);
    });
    w.RunCommand("workplane.create");
    const auto point = [height, tilted](double x, double y) {
        return Vector3{x, y, height + (tilted ? 0.2 * x + 0.3 * y : 0.0)};
    };
    const Vector3 corners[] = {point(-25, -20), point(25, -20), point(25, 20), point(-25, 20)};
    std::vector<geometry::CurveSegment> edges;
    for (int i = 0; i < 4; ++i) edges.push_back(geometry::CurveSegment::MakeLine(corners[i], corners[(i + 1) % 4]).Value());
    const auto wire = w.AddPlainWire(edges, "作図元の輪郭", false);
    if (wire.IsNil()) return {};
    app::SelectionSet boundary{{wire}};
    if (holes) for (const auto x : {-12.0, 12.0}) {
        const auto hole = w.AddPlainWire({geometry::CurveSegment::MakeCircle(point(x, 0),
            {0, 0, 1}, {1, 0, 0}, 4).Value()}, "内側の輪郭", false);
        if (hole.IsNil()) return {};
        boundary.entityIds.push_back(hole);
    }
    if (!w.SaveAndReopen("face-grid-source.kcd2")) return {};
    w.SelectTool(modeling::DrawingTool::Select);
    w.Viewport().SetSelection(boundary);
    w.RunCommand("surface.create");
    w.ChooseSurfaceMethod(modeling::GuideSurfaceMethod::PlanarBoundary);
    if (!Explain(("元の面の下見: " + w.StatusText().toStdString()).c_str(),
            w.SurfacePreviewShown() && w.SurfaceDock().CanConfirm())) return {};
    w.SurfaceDock().PressConfirm();
    EntityId newest;
    for (const auto& entity : w.Session().GetDocument().Snapshot().entities)
        if (entity.kind == EntityKind::GuideSurface) newest = entity.id;
    w.RunCommand("selection.activate");
    w.Viewport().SetSelection(app::SelectionSet{{initialPlane}});
    w.RunCommand("workplane.set_active");
    w.Viewport().SetSelection({});
    return newest;
}

void Setup(V2MainWindow& w)
{
    w.resize(1500, 950);
    QApplication::processEvents();
    w.RunCommand("file.new");
    w.Viewport().SetViewDirection(ViewDirection::Top);
    w.Viewport().SetViewCenter({0, 0, 12});
    w.Viewport().SetVisibleWidthMm(90);
}

bool DrawOnGrid(V2MainWindow& w)
{
    auto& v = w.Viewport();
    if (!Explain("面のグリッド作図が開始する", w.Session().CurrentTool() == modeling::DrawingTool::Line
            && w.Session().DrawingPlaneOnly() && v.DisplaySettingsNow().gridVisible
            && !v.ReferencePickPending())) return false;
    const auto grid = w.Session().Scene().grid;
    // Keep the grid exercise away from the guide's upper boundary: native edge
    // snaps intentionally outrank a grid point within the same pick radius.
    const auto a = grid.origin - grid.vDirection * grid.majorSpacingMm;
    const auto b = a + grid.uDirection * grid.majorSpacingMm;
    const auto before = w.Session().GetDocument().Revision();
    if (!ClickWorld(w, a) || !ClickWorld(w, b)) return false;
    if (!Explain("グリッドの交点2つから線ができる", w.Session().GetDocument().Revision() == before + 1)) return false;
    const auto segment = w.Session().Scene().curves.back().segment;
    const bool exact = (segment.StartPoint() - a).Length() < 1e-6 && (segment.EndPoint() - b).Length() < 1e-6;
    w.RunCommand("selection.activate");
    const auto detail = QStringLiteral("線の端点が表示した格子と一致: actual (%1,%2,%3)-(%4,%5,%6), expected (%7,%8,%9)-(%10,%11,%12)")
        .arg(segment.StartPoint().x).arg(segment.StartPoint().y).arg(segment.StartPoint().z)
        .arg(segment.EndPoint().x).arg(segment.EndPoint().y).arg(segment.EndPoint().z)
        .arg(a.x).arg(a.y).arg(a.z).arg(b.x).arg(b.y).arg(b.z);
    return Explain(detail.toUtf8().constData(), exact);
}

bool GuideToolFirst(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w);
    if (!Explain("普通の面を作成", !guide.IsNil())) return false;
    auto grid = w.CurrentGridChoice();
    grid.grid.visible = false;
    w.ApplyGridChoice(grid);
    w.RunCommand("snap.toggle");
    if (w.AddPlainWire({geometry::CurveSegment::MakeLine({-20, 0, 12}, {20, 0, 12}).Value()}, "面上の線", false).IsNil()) return false;
    if (!w.SaveAndReopen("face-grid-overlaid-wire.kcd2")) return false;
    w.Viewport().SetSelection({});
    if (!StartFaceTool(w) || !w.Viewport().ReferencePickPending()) return false;
    const auto before = w.Session().GetDocument().Revision();
    const int planes = CountOfKind(w, EntityKind::WorkPlane);
    const auto screen = w.Viewport().Mapping().Project({3, 0, 12});
    if (!screen) return false;
    w.Viewport().HoverAt({screen->x, screen->y});
    const auto candidate = w.Viewport().CurrentCandidate();
    Shot(w, "face-only-hover");
    if (!Explain("重なる線を避けて面だけを拾う", candidate && candidate->entityId == guide
            && candidate->kind == app::SelectionElementKind::Face)) return false;
    if (!ClickWorld(w, {3, 0, 12})) return false;
    if (!Explain("面を直接作図面にし、別の作業平面や履歴を増やさない", w.ActiveWorkPlaneId() == guide
            && w.Session().GetDocument().Revision() == before && CountOfKind(w, EntityKind::WorkPlane) == planes)) return false;
    if (!Explain("面に一致する作図基準", std::abs(w.Viewport().WorkPlane().origin.z - 12.0) < 1e-7)) return false;
    Shot(w, "guide-grid-drawing");
    return DrawOnGrid(w);
}

bool TiltedGuideAndReuse(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w, 12, true);
    if (guide.IsNil()) return false;
    if (!ClickWorld(w, {3, 2, 13.2}) || !StartFaceTool(w) || !DrawOnGrid(w)) return false;
    const auto plane = w.ActiveWorkPlaneId();
    const int count = CountOfKind(w, EntityKind::WorkPlane);
    const auto revision = w.Session().GetDocument().Revision();
    w.Viewport().SetSelection(app::SelectionSet{{guide}});
    if (!StartFaceTool(w)) return false;
    return Explain("同じ平面への再作図では基準面が増えない", !w.Viewport().ReferencePickPending()
        && w.ActiveWorkPlaneId() == plane && CountOfKind(w, EntityKind::WorkPlane) == count
        && w.Session().GetDocument().Revision() == revision);
}

bool SelectTree(V2MainWindow& w, EntityId id)
{
    auto* tree = w.EntityTree();
    auto* row = w.ItemOfEntity(id);
    if (!tree || !row) return false;
    for (auto* parent = row->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
    tree->scrollToItem(row);
    tree->setFocus();
    QApplication::processEvents();
    const auto at = tree->visualItemRect(row).intersected(tree->viewport()->rect()).center();
    if (tree->itemAt(at) != row) return false;
    Click(*tree->viewport(), at);
    return true;
}

bool SelectDrawingPlane(V2MainWindow& w, const QString& text)
{
    auto* combo = w.findChild<QComboBox*>("drawingPlane");
    if (!combo) return false;
    const int index = combo->findText(text);
    if (index < 0) return false;
    combo->setFocus();
    combo->showPopup();
    QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
    QApplication::sendEvent(combo, &home);
    for (int i = 0; i < index; ++i) {
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(combo, &down);
    }
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(combo, &enter);
    QApplication::processEvents();
    return true;
}

bool CheckIntersectionAndDraw(V2MainWindow& w, Vector3 target)
{
    const auto at = w.Viewport().Mapping().Project(target);
    if (!at) return false;
    w.Viewport().HoverAt({at->x, at->y});
    const auto hover = w.Viewport().Hover();
    const auto detail = hover.snap ? QStringLiteral("面の実境界との3D交点へ吸着: kind=%1 delta=%2 actual=(%3,%4,%5)")
        .arg(int(hover.snap->kind)).arg((hover.snap->position - target).Length(), 0, 'g', 12)
        .arg(hover.snap->position.x).arg(hover.snap->position.y).arg(hover.snap->position.z) : QStringLiteral("面の実境界との3D交点へ吸着: 候補なし");
    if (!Explain(detail.toUtf8().constData(), hover.snap && hover.snap->kind == modeling::SnapKind::Intersection
            && (hover.snap->position - target).Length() < 1e-6)) return false;
    const auto revision = w.Session().GetDocument().Revision();
    const auto end = target + w.Viewport().WorkPlane().vAxis * -3.0 + w.Viewport().WorkPlane().uAxis * -2.0;
    if (!ClickWorld(w, target) || !ClickWorld(w, end)) return false;
    if (!Explain("交点から線を確定", w.Session().GetDocument().Revision() == revision + 1)) return false;
    const auto& snapshot = w.Session().GetDocument().Snapshot();
    for (auto it = snapshot.features.rbegin(); it != snapshot.features.rend(); ++it)
        if (const auto* wire = std::get_if<domain::CreateWireDefinition>(&it->definition))
            return Explain("確定線の始点が実交点の座標に一致", !wire->segments.empty()
                && (wire->segments.front().StartPoint() - target).Length() < 1e-6);
    return false;
}

bool SupportBoundaryCrossing(V2MainWindow& w, bool hole, bool tilted)
{
    Setup(w);
    const auto guide = MakeGuide(w, 12, tilted, hole);
    if (guide.IsNil()) return false;
    std::vector<EntityId> sources;
    for (const auto& entity : w.Session().GetDocument().Snapshot().entities)
        if (entity.kind == EntityKind::Wire) sources.push_back(entity.id);
    const auto point = [tilted](double x, double y) { return Vector3{x, y, 12 + (tilted ? .2 * x + .3 * y : 0)}; };
    const double y = hole ? 1.3 : 7.3;
    const double x = hole ? 12 + std::sqrt(16 - y * y) : 25;
    const auto crossing = point(x, y);
    const auto ends = hole ? std::pair{point(6, y), point(19, y)} : std::pair{point(20, y), point(30, y)};
    const auto line = w.AddPlainWire({geometry::CurveSegment::MakeLine(ends.first, ends.second).Value()}, "境界を横切る線", false);
    // Hide the generating wires so only the actual native face boundary can supply the snap.
    if (!w.Session().GetDocument().Run(document::SetVisibilityCommand(sources, domain::Visibility::Hidden)).committed
        || !w.SaveAndReopen("direct-face-native-boundary.kcd2")) return false;
    if (!SelectTree(w, guide) || !StartFaceTool(w)) return false;
    auto* only = w.findChild<QToolButton*>("quickPlaneWiresOnly");
    if (!only) return false;
    if (!only->isChecked()) Click(*only, only->rect().center());
    if (!Explain("平面ワイヤのみでも作図する面と交差ワイヤを表示", w.Viewport().EntityShown(guide) && w.Viewport().EntityShown(line))) return false;
    Shot(w, hole ? "direct-face-hole-boundary" : "direct-face-outer-boundary");
    if (!CheckIntersectionAndDraw(w, crossing)) return false;
    w.RunCommand("edit.undo");
    if (!Explain("線のUndoで面を消さず基準を維持", w.Session().GetDocument().FindEntity(guide) && w.ActiveWorkPlaneId() == guide)) return false;
    for (const auto id : sources) if (w.Session().GetDocument().FindEntity(id)->visibility != domain::Visibility::Hidden) return false;
    return w.SaveAndReopen("direct-face-native-result.kcd2") && w.RebuildProblems().isEmpty();
}

bool SupportSwitchAndCancel(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w, 12, true);
    if (guide.IsNil() || !SelectTree(w, guide) || !StartFaceTool(w)) return false;
    auto* combo = w.findChild<QComboBox*>("drawingPlane");
    if (!combo || !combo->currentText().startsWith(QStringLiteral("面: "))) return false;
    const auto label = combo->currentText();
    const auto before = w.Session().GetDocument().Revision();
    const auto frame = w.Viewport().WorkPlane();
    if (!ClickWorld(w, frame.PointAt(2, 3)) || !w.Session().HasPlacedPoints()) return false;
    if (!SelectDrawingPlane(w, QStringLiteral("top_XY"))) return false;
    if (!Explain("作図面の変更で未確定の点だけ取り消す", !w.Session().HasPlacedPoints()
            && w.Session().GetDocument().Revision() == before && std::abs(w.Viewport().WorkPlane().origin.z) < 1e-9)) return false;
    if (!SelectDrawingPlane(w, label) || !Explain("作図面の欄から面へ戻れる", w.ActiveWorkPlaneId() == guide
            && (w.Viewport().WorkPlane().normal - frame.normal).Length() < 1e-10)) return false;
    if (!SelectDrawingPlane(w, QStringLiteral("3Dビューで面を指定…")) || !w.Viewport().ReferencePickPending()) return false;
    w.activateWindow(); w.Viewport().setFocus(); QApplication::processEvents();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&w.Viewport(), &escape);
    if (!Explain("面指定の取消で基準と文書を保持", !w.Viewport().ReferencePickPending() && w.ActiveWorkPlaneId() == guide
            && combo->currentText() == label && w.Session().GetDocument().Revision() == before)) return false;
    if (!w.SaveAndReopen("direct-face-switch.kcd2")) return false;
    return Explain("再読込で一時的なフェイス番号を使い回さず標準面に戻す", w.ActiveWorkPlaneId() != guide
        && CountOfKind(w, EntityKind::GuideSurface) == 1 && w.RebuildProblems().isEmpty());
}

bool SolidBoundaryCrossing(V2MainWindow& w)
{
    Setup(w);
    const auto part = MakeBoxByHand(w, .3, .35, .65, .65);
    if (part.IsNil()) return false;
    Vector3 lower, upper;
    for (const auto& shape : w.Viewport().ShapeViews()) if (shape.entityId == part) {
        lower = shape.mesh.minimum; upper = shape.mesh.maximum;
    }
    const Vector3 center{(lower.x + upper.x) * .5, (lower.y + upper.y) * .5, upper.z};
    const Vector3 crossing{upper.x, lower.y + (upper.y - lower.y) * .213, upper.z};
    if (w.AddPlainWire({geometry::CurveSegment::MakeLine(crossing + Vector3{-4, 0, 0},
            crossing + Vector3{5, 0, 0}).Value()}, "表面の境界を横切る線", false).IsNil()
        || !w.SaveAndReopen("direct-solid-face-boundary.kcd2")) return false;
    w.Viewport().SetSelection({});
    if (!StartFaceTool(w) || !ClickWorld(w, center)) return false;
    if (!Explain("立体のフェイスを直接作図面にする", w.ActiveWorkPlaneId() == part)) return false;
    auto* only = w.findChild<QToolButton*>("quickPlaneWiresOnly");
    if (!only) return false;
    if (!only->isChecked()) Click(*only, only->rect().center());
    if (!CheckIntersectionAndDraw(w, crossing)) return false;
    Shot(w, "direct-solid-face-intersection");
    w.RunCommand("edit.undo"); w.RunCommand("edit.redo");
    return Explain("フェイス上の線のUndo/Redoでも立体を保持", CountOfKind(w, EntityKind::Part) == 1
        && w.Session().GetDocument().FindEntity(part));
}

bool DeletedSupport(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w);
    if (guide.IsNil() || !SelectTree(w, guide) || !StartFaceTool(w)) return false;
    w.RunCommand("selection.activate");
    if (!SelectTree(w, guide)) return false;
    const int wires = CountOfKind(w, EntityKind::Wire);
    DeleteWithConfirmation(w, true);
    if (!Explain("作図基準の面を削除すると標準面へ戻る", !w.Session().GetDocument().FindEntity(guide)
            && w.ActiveWorkPlaneId() != guide && CountOfKind(w, EntityKind::Wire) == wires)) return false;
    w.RunCommand("edit.undo");
    if (!Explain("削除のUndoで面を戻し、一時的なフェイス番号は復活させない", w.Session().GetDocument().FindEntity(guide)
            && w.ActiveWorkPlaneId() != guide)) return false;
    if (!SelectTree(w, guide) || !StartFaceTool(w) || !DrawOnGrid(w)) return false;
    w.RunCommand("file.new");
    return Explain("新規文書に古い作図基準を持ち越さない", CountOfKind(w, EntityKind::GuideSurface) == 0
        && w.ActiveWorkPlaneId() != guide && std::abs(w.Viewport().WorkPlane().origin.z) < 1e-9);
}

bool HiddenSupportBoundaries(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w, 12, false, true);
    if (guide.IsNil() || !SelectTree(w, guide) || !StartFaceTool(w)) return false;
    auto* tree = w.EntityTree();
    auto* row = w.ItemOfEntity(guide);
    if (!tree || !row) return false;
    for (const auto hidden : {true, false}) {
        tree->scrollToItem(row); QApplication::processEvents();
        QStyleOptionViewItem option; option.initFrom(tree);
        option.rect = tree->visualItemRect(row);
        option.features |= QStyleOptionViewItem::HasCheckIndicator;
        option.checkState = row->checkState(0);
        const auto checkbox = tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, tree).center();
        Click(*tree->viewport(), checkbox);
        const auto* entity = w.Session().GetDocument().FindEntity(guide);
        if (!Explain("左一覧のチェックで面の表示を変更", entity && entity->visibility
                == (hidden ? domain::Visibility::Hidden : domain::Visibility::Visible))) return false;
        const auto& curves = w.Session().Scene().curves;
        const bool edges = std::any_of(curves.begin(), curves.end(), [&](const auto& c) { return c.entityId == guide; });
        if (!Explain("非表示の面の補助境界を復活させず、再表示で戻す", edges != hidden)) return false;
        if (!Explain("表示切替で作図基準や元の面を変更しない", w.ActiveWorkPlaneId() == guide
                && CountOfKind(w, EntityKind::GuideSurface) == 1)) return false;
    }
    return true;
}

bool TreeBeforeAndAfter(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w);
    if (guide.IsNil() || !SelectTree(w, guide) || !StartFaceTool(w) || !DrawOnGrid(w)) return false;
    w.Viewport().SetSelection({});
    if (!StartFaceTool(w) || !w.Viewport().ReferencePickPending() || !SelectTree(w, guide)) return false;
    return DrawOnGrid(w);
}

bool OverlapAndCancel(V2MainWindow& w)
{
    Setup(w);
    const auto bottom = MakeGuide(w, 12), top = MakeGuide(w, 16);
    if (bottom.IsNil() || top.IsNil() || !StartFaceTool(w)) return false;
    const auto before = w.Session().GetDocument().Revision();
    const auto screen = w.Viewport().Mapping().Project({3, 2, 16});
    if (!screen) return false;
    w.Viewport().HoverAt({screen->x, screen->y});
    if (!Explain("表裏の面だけが重なり候補になる", w.Viewport().CandidateCount() == 2)) return false;
    const auto first = w.Viewport().CurrentCandidate();
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&w.Viewport(), &tab);
    const auto second = w.Viewport().CurrentCandidate();
    if (!Explain("Tabで重なる面を切り替える", first && second && first->entityId == top && second->entityId == bottom)) return false;
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&w.Viewport(), &escape);
    if (!Explain("取消で面も基準も作成しない", !w.Viewport().ReferencePickPending()
            && w.Session().GetDocument().Revision() == before)) return false;
    if (!StartFaceTool(w)) return false;
    w.RunCommand("draw.circle");
    return Explain("別の道具へ切り替えて参照選択を終了", !w.Viewport().ReferencePickPending()
        && w.Session().CurrentTool() == modeling::DrawingTool::Circle);
}

bool SolidAndContextMenu(V2MainWindow& w)
{
    Setup(w);
    const auto part = MakeBoxByHand(w, .3, .35, .65, .65);
    if (part.IsNil()) return false;
    w.RunCommand("selection.activate");
    Vector3 center;
    for (const auto& shape : w.Viewport().ShapeViews()) if (shape.entityId == part)
        center = {(shape.mesh.minimum.x + shape.mesh.maximum.x) * .5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * .5, shape.mesh.maximum.z};
    if (!ClickWorld(w, center)) return false;
    bool activated = false;
    QTimer::singleShot(0, &w, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) return;
        auto* action = w.ActionFor("workplane.from_face");
        if (menu->actions().contains(action)) { activated = true; action->trigger(); }
        menu->close();
    });
    w.ShowSelectMenu(w.Viewport().mapToGlobal(QPoint(30, 30)));
    if (!Explain("右クリックの面で作図から開始", activated)) return false;
    return DrawOnGrid(w) && w.SaveAndReopen("face-grid-solid.kcd2") && w.RebuildProblems().isEmpty();
}

bool WorkPlaneFromGuide(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w, 12, true);
    if (guide.IsNil()) return false;
    w.SetWorkPlaneChooser({});
    w.RunCommand("workplane.create");
    auto* pick = w.findChild<QPushButton*>("workPlanePickFace");
    if (!Explain("作業面作成の棚に面指定ボタンがある", pick && pick->isVisible())) return false;
    const auto before = w.Session().GetDocument().Revision();
    Click(*pick, pick->rect().center());
    if (!ClickWorld(w, {3, 2, 13.2})) return false;
    Shot(w, "workplane-face-preview");
    if (!Explain("面指定だけでは文書を変更せず下見する", w.Session().GetDocument().Revision() == before
            && !w.Viewport().ReferencePickPending() && !w.Viewport().ToolPreview().empty()
            && w.WorkPlaneDock()->CanCreate())) return false;
    const auto choice = w.WorkPlaneDock()->Choice();
    const auto expected = Vector3{-.2, -.3, 1} * (1.0 / Vector3{-.2, -.3, 1}.Length());
    if (!Explain("欄の表示桁に丸めず面の向きを保持", (choice.normal - expected).Length() < 1e-10)) return false;
    auto* field = w.findChild<QDoubleSpinBox*>("workPlaneOrigin_0");
    if (!field) return false;
    field->setValue(field->value() + 1);
    if (!Explain("座標欄を編集すれば指定値を使用", std::abs(w.WorkPlaneDock()->Choice().origin.x - field->value()) < 1e-12)) return false;
    const auto exact = w.WorkPlaneDock()->Choice();
    w.WorkPlaneDock()->PressCreate();
    if (!Explain("確定で斜めの面に一致する作業面を作成", w.Session().GetDocument().Revision() == before + 1
            && (w.Viewport().WorkPlane().normal - expected).Length() < 1e-10
            && (w.Viewport().WorkPlane().origin - exact.origin).Length() < 1e-10)) return false;
    w.RunCommand("edit.undo");
    if (!Explain("作業面作成のUndoで元のサーフェスを維持", CountOfKind(w, EntityKind::GuideSurface) == 1)) return false;
    w.RunCommand("edit.redo");
    return w.SaveAndReopen("workplane-from-face.kcd2") && w.RebuildProblems().isEmpty();
}

bool CurvedFace(V2MainWindow& w)
{
    Setup(w);
    w.RunCommand("draw.circle");
    if (!ClickWorld(w, {0, 0, 0}) || !ClickWorld(w, {10, 0, 0})) return false;
    const auto wire = w.Session().Scene().curves.back().entityId;
    w.RunCommand("selection.activate");
    if (!ClickOnCurveOf(w, wire)) return false;
    w.RunCommand("part.extrude");
    w.ExtrudeDock().TypeDistanceMm(10);
    if (!w.HandleToolKey(Qt::Key_Return, nullptr)) return false;
    w.RunCommand("selection.activate");
    w.Viewport().SetSelection({});
    w.Viewport().SetViewDirection(ViewDirection::Right);
    w.Viewport().SetViewCenter({0, 0, 5});
    w.Viewport().SetVisibleWidthMm(40);
    if (!StartFaceTool(w) || !ClickWorld(w, {10 * std::cos(.4), 10 * std::sin(.4), 5})) return false;
    const auto& frame = w.Viewport().WorkPlane();
    if (!Explain("曲面上の選択位置に接する作図面", w.StatusText().contains(QStringLiteral("接平面"))
            && std::abs(frame.origin.x * frame.origin.x + frame.origin.y * frame.origin.y - 100) < 1e-6
            && std::abs(geometry::Dot(frame.normal, {frame.origin.x / 10, frame.origin.y / 10, 0})) > 1 - 1e-10)) return false;
    Shot(w, "curved-face-tangent-grid");
    if (!DrawOnGrid(w)) return false;
    w.SetWorkPlaneChooser({});
    w.Viewport().SetViewDirection(ViewDirection::Right);
    w.Viewport().SetSelection({});
    w.RunCommand("workplane.create");
    auto* pick = w.findChild<QPushButton*>("workPlanePickFace");
    if (!pick || !pick->isVisible()) return false;
    Click(*pick, pick->rect().center());
    if (!ClickWorld(w, {10 * std::cos(.7), 10 * std::sin(.7), 5})) return false;
    const auto choice = w.WorkPlaneDock()->Choice();
    return Explain("作業面の棚でも曲面の接平面を下見", w.StatusText().contains(QStringLiteral("接平面"))
        && !w.Viewport().ReferencePickPending() && w.WorkPlaneDock()->CanCreate()
        && std::abs(geometry::Dot(choice.normal, {choice.origin.x / 10, choice.origin.y / 10, 0})) > 1 - 1e-10);
}

bool HolesCircleAndRectangle(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w, 12, false, true);
    if (guide.IsNil()) return false;
    const auto source = w.Session().GetDocument().Snapshot();
    if (!StartFaceTool(w) || !ClickWorld(w, {3, 9, 12}) || !DrawOnGrid(w)) return false;
    const auto plane = w.Viewport().WorkPlane();
    const auto origin = w.Session().Scene().grid.origin;
    const auto pitch = w.Session().Scene().grid.majorSpacingMm;
    const auto center = origin - plane.uAxis * pitch - plane.vAxis * (pitch * 2);
    w.RunCommand("draw.circle");
    if (!ClickWorld(w, center) || !ClickWorld(w, center + plane.uAxis * (pitch * .5))) return false;
    const auto circle = w.Session().Scene().curves.back().segment;
    if (!Explain("面の格子上に円を作図", circle.Kind() == geometry::CurveKind::Circle
            && (circle.Center() - center).Length() < 1e-6 && std::abs(circle.Radius() - pitch * .5) < 1e-6)) return false;
    w.RunCommand("draw.rectangle");
    const auto before = w.Session().GetDocument().Revision();
    if (!ClickWorld(w, center + plane.uAxis * (pitch * 2)) || !ClickWorld(w, center + plane.uAxis * (pitch * 3) + plane.vAxis * pitch)) return false;
    if (!Explain("同じ面に矩形を作図", w.Session().GetDocument().Revision() == before + 1)) return false;
    for (const auto& entity : source.entities) {
        const auto* current = w.Session().GetDocument().FindEntity(entity.id);
        if (!Explain("元の面と輪郭のID・表示・参照を保全", current && current->kind == entity.kind
                && current->visibility == entity.visibility && current->createdBy == entity.createdBy)) return false;
    }
    w.RunCommand("selection.activate");
    Shot(w, "holed-guide-drawing");
    return CountOfKind(w, EntityKind::GuideSurface) == 1 && w.SaveAndReopen("face-grid-holes.kcd2")
        && w.RebuildProblems().isEmpty();
}

bool WorkPlaneTreeCancelAndSmallScreen(V2MainWindow& w)
{
    Setup(w);
    const auto guide = MakeGuide(w);
    if (guide.IsNil() || !SelectTree(w, guide)) return false;
    w.SetWorkPlaneChooser({});
    w.resize(1000, 700); QApplication::processEvents();
    w.RunCommand("workplane.create");
    auto* pick = w.findChild<QPushButton*>("workPlanePickFace");
    if (!Explain("小画面でも面指定ボタンを直接押せる", pick && pick->isVisible()
            && pick->visibleRegion().contains(pick->rect().center()))) return false;
    const auto before = w.Session().GetDocument().Revision();
    Click(*pick, pick->rect().center());
    if (!Explain("事前に左一覧で選んだ面を下見に使う", !w.Viewport().ReferencePickPending()
            && w.WorkPlaneDock()->Choice().method == modeling::WorkPlaneMethod::PointNormal
            && std::abs(w.WorkPlaneDock()->Choice().origin.z - 12) < 1e-10
            && w.Session().GetDocument().Revision() == before)) return false;
    Shot(w, "small-screen-workplane-face");
    w.Viewport().SetSelection({});
    Click(*pick, pick->rect().center());
    if (!w.Viewport().ReferencePickPending()) return false;
    w.RunCommand("draw.line");
    return Explain("作業面の面指定を別ツールへの切替で解除", !w.Viewport().ReferencePickPending()
        && w.Session().CurrentTool() == modeling::DrawingTool::Line
        && w.Session().GetDocument().Revision() == before);
}

bool ProjectToWorkPlane(V2MainWindow& w, int mode, bool tree, bool tilted, bool faceOutput, bool mixed = false)
{
    Setup(w);
    const auto originalPlane = w.ActiveWorkPlaneId();
    const auto guide = mixed ? MakeGuide(w, 4) : EntityId{};
    if (mixed && guide.IsNil()) return false;
    w.Viewport().SetViewDirection(ViewDirection::Top);
    if (auto* scope = w.findChild<QComboBox*>("drawingScope")) scope->setCurrentIndex(0);
    w.RunCommand("draw.circle");
    w.Viewport().SetSnapSuppressed(true);
    if (!ClickWorld(w, {0, 0, 0}) || !ClickWorld(w, {3, 0, 0})) return false;
    w.Viewport().SetSnapSuppressed(false);
    const auto source = w.Session().Scene().curves.back();
    if (!Explain("投影試験の元の円はXY平面上", source.segment.Kind() == geometry::CurveKind::Circle
            && std::abs(source.segment.Normal().z) > 1 - 1e-10 && std::abs(source.segment.Center().z) < 1e-10)) return false;
    w.SetWorkPlaneChooser([tree, tilted](app::WorkPlaneChoice choice, const app::WorkPlaneFacts&) {
        choice.method = modeling::WorkPlaneMethod::PointNormal;
        choice.name = "投影する作業面";
        choice.origin = tree ? Vector3{300, -200, 10} : Vector3{0, 0, 10};
        choice.normal = tilted ? Vector3{-.2, -.3, 1} : Vector3{0, 0, 1};
        choice.uAxis = {1, 0, tilted ? .2 : 0};
        return std::optional(choice);
    });
    w.RunCommand("workplane.create");
    const auto target = w.ActiveWorkPlaneId();
    const auto plane = w.Viewport().WorkPlane();
    w.SetWorkPlaneChooser({});
    w.Viewport().SetSelection(app::SelectionSet{{originalPlane}});
    w.RunCommand("workplane.set_active");
    w.Viewport().SetSelection({});
    const int wireCount = CountOfKind(w, EntityKind::Wire), planeCount = CountOfKind(w, EntityKind::WorkPlane);
    std::vector<EntityId> originalWires;
    for (const auto& entity : w.Session().GetDocument().Snapshot().entities)
        if (entity.kind == EntityKind::Wire) originalWires.push_back(entity.id);
    w.RunCommand("wire.project_surface");
    auto* tool = dynamic_cast<V2ProjectionTool*>(w.findChild<QWidget*>("projectionPanel"));
    if (!tool) return false;
    auto* method = tool->findChild<QComboBox*>("projectionMode");
    method->setCurrentIndex(mode);
    if (!ClickWorld(w, source.segment.Evaluate(.4)) || tool->WireCount() != 1) return false;
    auto* kind = tool->findChild<QComboBox*>("projectionTargetKind");
    kind->setCurrentIndex(1);
    if (tree) { if (!SelectTree(w, target)) return false; }
    else {
        const auto at = w.Viewport().Mapping().Project(plane.origin); if (!at) return false;
        w.Viewport().HoverAt({at->x, at->y});
        bool found = false;
        for (int i = 0; i < w.Viewport().CandidateCount(); ++i) {
            if (const auto candidate = w.Viewport().CurrentCandidate(); candidate && candidate->entityId == target) { found = true; break; }
            QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier); QApplication::sendEvent(&w.Viewport(), &tab);
        }
        if (!Explain("作業面だけの候補をTabで選べる", found) || !ClickWorld(w, plane.origin)) return false;
    }
    if (!Explain("作業面を投影先へ追加", tool->TargetCount() == 1)) return false;
    if (mixed) {
        kind->setCurrentIndex(0);
        if (!SelectTree(w, guide) || tool->TargetCount() != 2) return false;
        tool->findChild<QComboBox*>("projectionHits")->setCurrentIndex(2);
    }
    if (faceOutput) tool->findChild<QComboBox*>("projectionOutput")->setCurrentIndex(1);
    const auto before = w.Session().GetDocument().Revision();
    const bool preview = tool->Preview();
    if (!Explain(tool->findChild<QLabel*>("projectionStatus")->text().toUtf8().constData(), preview)) return false;
    Shot(w, faceOutput ? "workplane-projection-face-preview" : "workplane-projection-wire-preview");
    auto* confirm = tool->findChild<QPushButton*>("panelConfirm");
    if (!Explain("投影の確定ボタンを押せる", confirm && confirm->isEnabled())) return false;
    Click(*confirm, confirm->rect().center());
    const auto committed = QStringLiteral("投影を1回で確定し元ワイヤーを残す: revision %1->%2, wires %3->%4, guides %5 / %6")
        .arg(before).arg(w.Session().GetDocument().Revision()).arg(wireCount).arg(CountOfKind(w, EntityKind::Wire))
        .arg(CountOfKind(w, EntityKind::GuideSurface)).arg(tool->findChild<QLabel*>("projectionStatus")->text());
    if (!Explain(committed.toUtf8().constData(), w.Session().GetDocument().Revision() == before + (mixed ? 2 : 1) + (faceOutput ? 1 : 0)
            && CountOfKind(w, EntityKind::Wire) == wireCount + (mixed ? 2 : 1)
            && CountOfKind(w, EntityKind::GuideSurface) == (faceOutput ? 1 : mixed ? 1 : 0))) return false;
    bool checked = false;
    for (const auto& curve : w.Session().Scene().curves) {
        const auto* entity = w.Session().GetDocument().FindEntity(curve.entityId);
        if (!entity || entity->kind != EntityKind::Wire || std::find(originalWires.begin(), originalWires.end(), curve.entityId) != originalWires.end()) continue;
        checked = true;
        for (const auto t : {0., .2, .5, .8, 1.}) if (!Explain("投影結果が指定した面上にある",
            std::abs(plane.SignedDistance(curve.segment.Evaluate(t))) < 1e-6
            || (mixed && std::abs(curve.segment.Evaluate(t).z - 4) < 1e-6))) return false;
        if (mixed) for (const auto t : {0., .1, .2, .3, .4, .5, .6, .7, .8, .9, 1.}) {
            const auto point = curve.segment.Evaluate(t) - source.segment.Center();
            if (!Explain("複数面でも曲線の形と半径を保持", std::abs(std::hypot(point.x, point.y) - source.segment.Radius()) < .001)) return false;
        }
        if (mode != 1 && !tilted && !Explain("円を分割せず円として投影", curve.segment.Kind() == geometry::CurveKind::Circle)) return false;
    }
    if (!checked) return false;
    w.RunCommand("edit.undo");
    if (!Explain("Undoでは元ワイヤーと作業面を残す", CountOfKind(w, EntityKind::Wire) == wireCount
            && CountOfKind(w, EntityKind::WorkPlane) == planeCount)) return false;
    w.RunCommand("edit.redo");
    return w.SaveAndReopen("workplane-projection.kcd2") && w.RebuildProblems().isEmpty();
}

bool PickDrawingFace(V2MainWindow& w, EntityId id)
{
    w.RunCommand("selection.activate"); w.Viewport().SetSelection({});
    if (!StartFaceTool(w)) return false;
    for (const auto& shape : w.Viewport().ShapeViews()) if (shape.entityId == id)
        for (const auto& triangle : shape.mesh.triangles) {
            const auto screen = w.Viewport().Mapping().Project(triangle.Center()); if (!screen) continue;
            const QPointF at(screen->x, screen->y);
            if (!w.Viewport().rect().contains(at.toPoint()) || w.Viewport().ViewButtonAt(at)) continue;
            w.Viewport().HoverAt(at);
            for (int i = 0; i < w.Viewport().CandidateCount(); ++i) {
                const auto candidate = w.Viewport().CurrentCandidate();
                if (candidate && candidate->entityId == id && candidate->pickedFaceIndex == triangle.faceIndex)
                    return ClickWorld(w, triangle.Center()) && !w.Viewport().ReferencePickPending();
                QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier); QApplication::sendEvent(&w.Viewport(), &tab);
            }
        }
    return Explain("面を3Dビューから指定できる", false);
}

bool ClickNamed(V2MainWindow& w, const char* name)
{
    auto* button = w.findChild<QWidget*>(QString::fromUtf8(name));
    if (!Explain((std::string("表示されたボタン: ") + name).c_str(), button && button->isVisible() && button->isEnabled())) return false;
    const auto point = button->mapToGlobal(button->rect().center());
    for (auto* parent = button->parentWidget(); parent; parent = parent->parentWidget())
        if (!Explain((std::string("画面内で押せるボタン: ") + name).c_str(), parent->rect().contains(parent->mapFromGlobal(point)))) return false;
    Click(*button, button->rect().center()); return true;
}

EntityId WarpedGuide(V2MainWindow& w)
{
    const Vector3 corners[] = {{-20,-12,12.15},{20,-12,11.85},{20,12,12.15},{-20,12,11.85}};
    app::SelectionSet selected;
    for (int i = 0; i < 4; ++i) selected.entityIds.push_back(w.AddPlainWire(
        {geometry::CurveSegment::MakeLine(corners[i], corners[(i+1)%4]).Value()}, "形状の辺", false));
    if (!w.SaveAndReopen("outline-warp-source.kcd2")) return {};
    w.Viewport().SetSelection(selected); w.RunCommand("surface.create");
    w.ChooseSurfaceMethod(modeling::GuideSurfaceMethod::FourEdgePatch);
    if (!w.SurfaceDock().ChooseFourEdgeStyle(modeling::FourEdgeStyle::Curved)) return {};
    if (!Explain(("ねじれた四辺面の下見: " + w.StatusText().toStdString()).c_str(), w.SurfaceDock().CanConfirm())) return {};
    w.SurfaceDock().PressConfirm();
    for (const auto& entity : w.Session().GetDocument().Snapshot().entities)
        if (entity.kind == EntityKind::GuideSurface) return entity.id;
    return {};
}

bool CopiedOutline(V2MainWindow& w, EntityId source, bool holes, bool project, bool offset)
{
    if (!PickDrawingFace(w, source)) return false;
    const auto sourceCurves = w.Session().Scene().curves;
    const int wires = CountOfKind(w, EntityKind::Wire), surfaces = CountOfKind(w, EntityKind::GuideSurface);
    const auto revision = w.Session().GetDocument().Revision();
    if (!ClickNamed(w, "drawingFaceOutline")) return false;
    auto* check = w.findChild<QCheckBox*>("faceOutlineProject");
    if (!check || !check->isChecked()) return false;
    if (!project) Click(*check, QPointF(10, check->height() * .5));
    if (!Explain("投影チェックが指定通り切り替わる", check->isChecked() == project)) return false;
    if (!Explain("下見で文書を変更しない", w.Session().GetDocument().Revision() == revision)) return false;
    Shot(w, project ? "outline-projected-preview" : "outline-original-3d-preview");
    if (!ClickNamed(w, offset ? "faceOutlineOffset" : "faceOutlineConfirm")) return false;
    const auto selection = w.Viewport().Selection();
    if (!Explain("輪郭ごとに1本、元の面を保持", selection.entityIds.size() == (holes ? 3 : 1)
            && CountOfKind(w, EntityKind::Wire) == wires + (holes ? 3 : 1)
            && CountOfKind(w, EntityKind::GuideSurface) == surfaces && w.Session().GetDocument().FindEntity(source))) return false;
    const auto curves = app::SelectedCurves(selection, w.Session().Scene());
    if (!Explain("閉じた輪郭を写す", curves.size() == (holes ? 6 : 4))) return false;
    if (project) for (const auto& curve : curves) for (const double t : {0., .25, .5, .75, 1.})
        if (!Explain("投影した輪郭が作図面上", std::abs(w.Viewport().WorkPlane().SignedDistance(curve.Evaluate(t))) < 1e-7)) return false;
    if (!project && offset == false && !holes) {
        double maximum = 0;
        for (const auto& curve : curves) for (const double t : {0., .5, 1.})
            maximum = std::max(maximum, std::abs(w.Viewport().WorkPlane().SignedDistance(curve.Evaluate(t))));
        if (!Explain("投影しない選択では元の非平面の輪郭を保持", maximum > .001)) return false;
    }
    for (const auto& old : sourceCurves) {
        const auto* entity = w.Session().GetDocument().FindEntity(old.entityId);
        if (!entity || entity->kind != EntityKind::Wire) continue;
        const auto found = std::find_if(w.Session().Scene().curves.begin(), w.Session().Scene().curves.end(), [&](const auto& current) {
            return current.entityId == old.entityId && current.segmentId == old.segmentId;
        });
        if (!Explain("元ワイヤーのUUIDを保持", found != w.Session().Scene().curves.end())) return false;
        for (const double t : {0., .25, .5, .75, 1.}) if (!Explain("元ワイヤーの形を変更しない", (old.segment.Evaluate(t)-found->segment.Evaluate(t)).Length() < 1e-9)) return false;
    }
    if (offset) {
        auto* distance = w.findChild<QDoubleSpinBox*>("offsetDistance");
        auto* plane = w.findChild<QComboBox*>("offsetPlaneChoice");
        if (!distance || !plane) return false;
        distance->setValue(.2); plane->setCurrentIndex(1);
        Shot(w, "outline-offset-preview");
        const auto before = w.Session().GetDocument().Revision();
        if (!ClickNamed(w, "offsetConfirm") || !Explain(("写した線をオフセット: " + w.StatusText().toStdString()).c_str(),
                w.Session().GetDocument().Revision() == before + 1)) return false;
        w.RunCommand("edit.undo");
        if (!Explain("オフセットのUndoで輪郭の写しを保持", CountOfKind(w, EntityKind::Wire) == wires + 1)) return false;
        w.RunCommand("edit.undo");
        if (!Explain("次のUndoで写した輪郭だけ戻る", CountOfKind(w, EntityKind::Wire) == wires && w.Session().GetDocument().FindEntity(source))) return false;
        w.RunCommand("edit.redo");
        if (!Explain("輪郭の写しを一度で戻せる", CountOfKind(w, EntityKind::Wire) == wires + 1)) return false;
        w.RunCommand("edit.redo");
        w.RunCommand("draw.line");
        const auto frame = w.Viewport().WorkPlane();
        const auto crossing = curves.front().Evaluate(.37);
        const auto perpendicular = geometry::Normalized(geometry::Cross(frame.normal, curves.front().EndPoint()-curves.front().StartPoint()));
        w.Viewport().SetSnapSuppressed(true);
        const bool drawn = ClickWorld(w, crossing-perpendicular*2) && ClickWorld(w, crossing+perpendicular*2);
        w.Viewport().SetSnapSuppressed(false);
        if (!drawn || !CheckIntersectionAndDraw(w, crossing)) return false;
        Shot(w, "outline-real-intersection-drawing");
    }
    w.RunCommand("selection.activate");
    return w.SaveAndReopen("outline-result.kcd2") && w.RebuildProblems().isEmpty();
}

bool OutlineFixture(V2MainWindow& w, int mode)
{
    Setup(w);
    const auto source = mode == 1 ? MakeGuide(w, 12, false, true) : WarpedGuide(w);
    if (source.IsNil()) return false;
    return CopiedOutline(w, source, mode == 1, mode != 2, mode == 0);
}

bool OutlineCancel(V2MainWindow& w)
{
    Setup(w); const auto source = MakeGuide(w);
    if (source.IsNil() || !PickDrawingFace(w, source)) return false;
    const auto revision = w.Session().GetDocument().Revision();
    if (!ClickNamed(w, "drawingFaceOutline")) return false;
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier); QApplication::sendEvent(&w.Viewport(), &escape);
    if (!Explain("取消でコピーも元の変更もない", w.Session().GetDocument().Revision() == revision
            && !w.findChild<QWidget*>("faceOutlinePanel")->isVisible())) return false;
    w.RunCommand("draw.line");
    w.Viewport().SetSnapSuppressed(true);
    const auto frame = w.Viewport().WorkPlane();
    if (!ClickWorld(w, frame.PointAt(-3, 1)) || !ClickWorld(w, frame.PointAt(3, 1))) return false;
    w.Viewport().SetSnapSuppressed(false);
    return Explain("取消後も線を作図できる", w.Session().GetDocument().Revision() == revision + 1);
}

bool OutlineUnsupportedProjection(V2MainWindow& w)
{
    Setup(w); const auto source = MakeGuide(w, 12, false, true);
    if (source.IsNil() || !PickDrawingFace(w, source) || !ClickNamed(w, "drawingFaceOutline")) return false;
    const auto revision = w.Session().GetDocument().Revision();
    if (!SelectDrawingPlane(w, QStringLiteral("front_XZ"))) return false;
    auto* confirm = w.findChild<QPushButton*>("faceOutlineConfirm");
    auto* offset = w.findChild<QPushButton*>("faceOutlineOffset");
    auto* status = w.findChild<QLabel*>("faceOutlineStatus");
    if (!Explain("変換できない曲線で部分的な写しを確定しない", confirm && offset && status
            && confirm->isVisible() && !confirm->isEnabled() && !offset->isEnabled() && !status->text().isEmpty()
            && w.Session().GetDocument().Revision() == revision)) return false;
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier); QApplication::sendEvent(&w.Viewport(), &enter);
    return Explain("Enterでも部分生成しない", w.Session().GetDocument().Revision() == revision);
}

bool CircleAxisRevolve(V2MainWindow& w, bool arc, bool tilted, bool pointed = false)
{
    Setup(w);
    const auto p = [tilted](Vector3 a) { return tilted ? Vector3{a.x + 10, -a.z + 5, a.y + 12} : a; };
    const auto normal = tilted ? Vector3{0,-1,0} : Vector3{0,0,1};
    const auto axisCurve = arc ? geometry::CurveSegment::MakeCircularArc(p({0,0,0}), normal, {1,0,0}, 3, 0, 1.5)
        : geometry::CurveSegment::MakeCircle(p({0,0,0}), normal, {1,0,0}, 3);
    if (!axisCurve.HasValue()) return false;
    const auto axis = w.AddPlainWire({axisCurve.Value()}, "中心と向き", false);
    const double endRadius = pointed ? 0 : 2;
    const auto profileCurve = geometry::CurveSegment::MakeCubicBezier({p({3,0,0}),p({4,0,2}),p({pointed ? 2. : 4.,0,pointed ? 6. : 4.}),p({endRadius,0,6})}).Value();
    const auto profile = w.AddPlainWire({profileCurve}, "回す形状線", false);
    if (!w.SaveAndReopen("circle-axis-source.kcd2")) return false;
    w.Viewport().SetViewDirection(ViewDirection::Isometric); w.Viewport().FitToDocument();
    w.Viewport().SetSelection({}); w.RunCommand("guide.revolve");
    if (!w.SurfaceDock().ClickActivate(modeling::ChainRole::Section) || !SelectTree(w, profile)) return false;
    if (!w.SurfaceDock().ClickActivate(modeling::ChainRole::GuideU) || !ClickWorld(w, axisCurve.Value().Evaluate(.4))) return false;
    auto* angle = w.findChild<QDoubleSpinBox*>("surfaceRevolveAngle");
    if (!Explain("円の軸入力と角度欄を表示", angle && angle->isVisible())) return false;
    angle->setValue(180);
    if (!Explain("表示中の角度欄が入力値へ反映", std::abs(app::ParameterValueOf(w.ParameterDock().Values(), app::ParameterId::RevolveAngleDeg)-180) < 1e-8)) return false;
    if (!ClickNamed(w, "surfaceRevolveFullTurn")) return false;
    Shot(w, "circle-axis-revolve-preview");
    if (!Explain(("円から軸を決めた下見: " + w.StatusText().toStdString()).c_str(), w.SurfaceDock().CanConfirm() && w.SurfacePreviewShown())) return false;
    w.SurfaceDock().PressConfirm();
    const auto& table = w.GuideRoleTable();
    if (!Explain("一周する面を生成、軸ワイヤーは増やさない", CountOfKind(w, EntityKind::GuideSurface) == 1
            && CountOfKind(w, EntityKind::Wire) == 2 && (table.revolveAxisPoint-p({0,0,0})).Length() < 1e-7
            && (table.revolveAxisDirection-normal).Length() < 1e-7 && std::abs(table.revolveAngleRad-2*3.141592653589793) < 1e-8)) return false;
    bool opposite = false, rear = false;
    for (const auto& shape : w.Viewport().ShapeViews()) for (const auto& triangle : shape.mesh.triangles)
        for (const auto& point : triangle.points) {
            if ((point-p({-3,0,0})).Length() < .1) opposite = true;
            if ((point-p({-endRadius,0,6})).Length() < .1) rear = true;
        }
    if (!Explain("反対側と奥側の輪郭まで曲面がある", opposite && rear)) return false;
    w.RunCommand("selection.activate"); w.Viewport().SetSelection({});
    w.Viewport().SetViewCenter(p({0,0,3})); w.Viewport().SetVisibleWidthMm(18);
    Shot(w, pointed ? "circle-axis-pointed-result" : "circle-axis-revolve-result");
    w.RunCommand("edit.undo");
    if (!Explain("一度のUndoで面だけ戻る", CountOfKind(w, EntityKind::GuideSurface) == 0 && CountOfKind(w, EntityKind::Wire) == 2)) return false;
    w.RunCommand("edit.redo");
    return w.SaveAndReopen("circle-axis-result.kcd2") && w.RebuildProblems().isEmpty();
}

bool OwnerOutline(V2MainWindow& w)
{
    Setup(w); const auto file = qEnvironmentVariable("KACHACAD_FACE_SKETCH_OWNER_FILE");
    if (file.isEmpty() || !w.OpenDocumentFile(file)) return false;
    EntityId source;
    for (const auto& entity : w.Session().GetDocument().Snapshot().entities)
        if (entity.kind == EntityKind::GuideSurface && entity.displayName == "非運転席側中心窓") source = entity.id;
    if (source.IsNil()) return false;
    if (!w.Session().GetDocument().Run(document::SetVisibilityCommand({source}, domain::Visibility::Visible)).committed
        || !w.SaveAndReopen("owner-outline-source.kcd2")) return false;
    w.Viewport().SetViewDirection(ViewDirection::Isometric); w.Viewport().SetViewCenter({-6,3.5,21});
    w.Viewport().SetVisibleWidthMm(35);
    return CopiedOutline(w, source, false, true, true);
}

bool OwnerFaceDrawing(V2MainWindow& w)
{
    Setup(w);
    const auto file = qEnvironmentVariable("KACHACAD_FACE_GRID_OWNER_FILE");
    if (file.isEmpty() || !w.OpenDocumentFile(file)) return false;
    w.Viewport().SetViewDirection(ViewDirection::Isometric);
    w.Viewport().FitToDocument();
    w.RunCommand("selection.activate");
    w.Viewport().SetSelection({});
    const int surfaces = CountOfKind(w, EntityKind::GuideSurface), parts = CountOfKind(w, EntityKind::Part);
    const int wires = CountOfKind(w, EntityKind::Wire);
    if (!StartFaceTool(w)) return false;
    bool picked = false;
    Vector3 selectedPoint;
    for (const auto& shape : w.Viewport().ShapeViews()) {
        if (!w.Viewport().EntityShown(shape.entityId)) continue;
        for (const auto& triangle : shape.mesh.triangles) {
            const auto point = triangle.Center();
            const auto screen = w.Viewport().Mapping().Project(point); if (!screen) continue;
            const QPointF at(screen->x, screen->y);
            if (!w.Viewport().rect().contains(at.toPoint()) || w.Viewport().ViewButtonAt(at)) continue;
            w.Viewport().HoverAt(at);
            for (int i = 0; i < w.Viewport().CandidateCount(); ++i) {
                const auto candidate = w.Viewport().CurrentCandidate();
                if (candidate && candidate->entityId == shape.entityId && candidate->pickedFaceIndex == triangle.faceIndex) {
                    selectedPoint = candidate->hitPoint;
                    picked = ClickWorld(w, point); break;
                }
                QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier); QApplication::sendEvent(&w.Viewport(), &tab);
            }
            if (picked) break;
        }
        if (picked) break;
    }
    if (!Explain("実モデルの面を3Dビューで選んでグリッド作図へ進む", picked
            && !w.Viewport().ReferencePickPending() && w.Viewport().DisplaySettingsNow().gridVisible
            && w.Session().CurrentTool() == modeling::DrawingTool::Line)) return false;
    const auto plane = w.Viewport().WorkPlane();
    const auto focus = w.Viewport().Mapping().Project(w.Viewport().ViewCenter());
    if (!Explain("作図する位置を画面の中央に表示", focus && (w.Viewport().ViewCenter() - selectedPoint).Length() < .1
            && std::abs(focus->x - w.Viewport().width() * .5) < 1e-6
            && std::abs(focus->y - w.Viewport().height() * .5) < 1e-6)) return false;
    w.Viewport().SetSnapSuppressed(true);
    if (!ClickWorld(w, plane.PointAt(2, 2)) || !ClickWorld(w, plane.PointAt(3, 2))) return false;
    w.Viewport().SetSnapSuppressed(false);
    if (!Explain("元の面・ソリッドを保ち1本だけ作図", CountOfKind(w, EntityKind::GuideSurface) == surfaces
            && CountOfKind(w, EntityKind::Part) == parts && CountOfKind(w, EntityKind::Wire) == wires + 1)) return false;
    Shot(w, "owner-model-face-grid");
    w.RunCommand("edit.undo");
    if (!Explain("実モデルでもUndoで作図した線だけを除去", CountOfKind(w, EntityKind::Wire) == wires
            && CountOfKind(w, EntityKind::GuideSurface) == surfaces && CountOfKind(w, EntityKind::Part) == parts)) return false;
    w.RunCommand("edit.redo");
    return w.SaveAndReopen("owner-face-grid-copy.kcd2") && w.RebuildProblems().isEmpty();
}
}

std::vector<SelfTestCase> FaceDrawingCases()
{
    auto cases = std::vector<SelfTestCase>{{"FACE-GRID tool first ignores overlapping wires", GuideToolFirst},
        {"FACE-SUPPORT native outer boundary intersection", [](auto& w) { return SupportBoundaryCrossing(w, false, false); }},
        {"FACE-SUPPORT native inclined boundary intersection", [](auto& w) { return SupportBoundaryCrossing(w, false, true); }},
        {"FACE-SUPPORT native inner curved boundary intersection", [](auto& w) { return SupportBoundaryCrossing(w, true, false); }},
        {"FACE-SUPPORT combo switch cancel and reopen", SupportSwitchAndCancel},
        {"FACE-SUPPORT native solid face boundary intersection", SolidBoundaryCrossing},
        {"FACE-SUPPORT delete undo new document", DeletedSupport},
        {"FACE-SUPPORT hidden native boundaries stay hidden", HiddenSupportBoundaries},
        {"FACE-GRID inclined surface and repeated drawing reuse", TiltedGuideAndReuse},
        {"FACE-GRID explorer before and after tool", TreeBeforeAndAfter},
        {"FACE-GRID overlapping faces cancel and tool switch", OverlapAndCancel},
        {"FACE-GRID solid surface context menu and save", SolidAndContextMenu},
        {"FACE-GRID workplane shelf face preview confirm undo", WorkPlaneFromGuide},
        {"FACE-GRID curved face tangent drawing and workplane", CurvedFace},
        {"FACE-GRID holed surface circle rectangle preserve", HolesCircleAndRectangle},
        {"FACE-GRID workplane explorer cancel small screen", WorkPlaneTreeCancelAndSmallScreen},
        {"FACE-PROJ direction to workplane through explorer", [](auto& w) { return ProjectToWorkPlane(w, 0, true, false, false); }},
        {"FACE-PROJ direction to tilted workplane through viewport", [](auto& w) { return ProjectToWorkPlane(w, 0, false, true, false); }},
        {"FACE-PROJ nearest point to workplane", [](auto& w) { return ProjectToWorkPlane(w, 2, true, false, false); }},
        {"FACE-PROJ wrap to workplane", [](auto& w) { return ProjectToWorkPlane(w, 1, true, false, false); }},
        {"FACE-PROJ closed region onto workplane", [](auto& w) { return ProjectToWorkPlane(w, 0, true, false, true); }},
        {"FACE-PROJ mixed surface and workplane all hits", [](auto& w) { return ProjectToWorkPlane(w, 0, true, false, false, true); }}};
    cases.push_back({"FACE-OUTLINE warped face project then offset", [](auto& w) { return OutlineFixture(w, 0); }});
    cases.push_back({"FACE-OUTLINE multiple closed contours preserve holes", [](auto& w) { return OutlineFixture(w, 1); }});
    cases.push_back({"FACE-OUTLINE exact copy without projection", [](auto& w) { return OutlineFixture(w, 2); }});
    cases.push_back({"FACE-OUTLINE cancel then draw", OutlineCancel});
    cases.push_back({"FACE-OUTLINE unsupported projection never commits partial contours", OutlineUnsupportedProjection});
    cases.push_back({"FACE-REVOLVE circle axis profile full turn undo reopen", [](auto& w) { return CircleAxisRevolve(w, false, false); }});
    cases.push_back({"FACE-REVOLVE arc axis tilted profile full turn", [](auto& w) { return CircleAxisRevolve(w, true, true); }});
    cases.push_back({"FACE-REVOLVE circle axis rounded nose closes on axis", [](auto& w) { return CircleAxisRevolve(w, false, false, true); }});
    if (!qEnvironmentVariableIsEmpty("KACHACAD_FACE_SKETCH_OWNER_FILE"))
        cases.push_back({"FACE-OUTLINE supplied ER1 skew face project offset", OwnerOutline});
    if (!qEnvironmentVariableIsEmpty("KACHACAD_FACE_GRID_OWNER_FILE"))
        cases.push_back({"FACE-OWNER supplied model face grid drawing undo save", OwnerFaceDrawing});
    return cases;
}
}
