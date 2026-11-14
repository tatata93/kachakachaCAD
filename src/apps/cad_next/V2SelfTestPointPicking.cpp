#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"
#include "V2MeasureDock.h"
#include "V2GridDock.h"
#include "kachakacha/modeling/WorkPlane.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QKeyEvent>
#include <QLineEdit>
#include <QString>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QTreeWidgetItem>
#include <QEvent>
#include <QPointF>
#include <QWidget>
#include <algorithm>
#include <array>
#include <optional>
#include <cmath>

namespace kachakacha::v2::selftest {
namespace {
using geometry::Vector3;
const std::array<Vector3,3> points{{{-25,-12,6},{16,-8,12},{-6,19,21}}};
void Click(QWidget& widget,QPointF at,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QMouseEvent move(QEvent::MouseMove,at,widget.mapToGlobal(at.toPoint()),Qt::NoButton,Qt::NoButton,modifiers);
    QMouseEvent press(QEvent::MouseButtonPress,at,widget.mapToGlobal(at.toPoint()),Qt::LeftButton,Qt::LeftButton,modifiers);
    QMouseEvent release(QEvent::MouseButtonRelease,at,widget.mapToGlobal(at.toPoint()),Qt::LeftButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(&widget,&move);QApplication::sendEvent(&widget,&press);QApplication::sendEvent(&widget,&release);QApplication::processEvents();
}
bool ClickWorld(V2Viewport& view,Vector3 point,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    const auto screen=view.Mapping().Project(point);if(!screen)return false;
    const QPointF at(screen->x,screen->y);if(!view.rect().contains(at.toPoint())||view.ViewButtonAt(at))return false;
    Click(view,at,modifiers);return true;
}
bool Setup(V2MainWindow& w) {
    w.resize(1600,1000);QApplication::processEvents();w.RunCommand("file.new");
    for(const auto p:points)if(w.AddPlainWire({geometry::CurveSegment::MakeLine(p,p+Vector3{7,3,2}).Value()},"点指定の入力",false).IsNil())return false;
    if(!w.SaveAndReopen("point-input-source.kcd2"))return false;
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Isometric);v.SetViewCenter({});v.SetVisibleWidthMm(150);v.SetSnapSuppressed(false);
    w.RunCommand("selection.activate");return true;
}
bool WorkPlaneRoles(V2MainWindow& w) {
    if(!Setup(w))return false;w.SetWorkPlaneChooser({});w.RunCommand("workplane.create");
    auto* dock=w.WorkPlaneDock();if(!dock)return false;
    WorkPlaneChoice choice;choice.method=modeling::WorkPlaneMethod::ThreePoints;dock->SetChoice(choice);
    const auto source=w.Session().Scene().curves;
    for(int i=0;i<3;++i){
        auto* button=w.findChild<QPushButton*>(QStringLiteral("workPlanePickRole%1").arg(i));
        if(!Explain("point role button is visible",button&&button->isVisible()))return false;Click(*button,button->rect().center());
        if(!Explain("point role is armed by its visible button",w.Viewport().PickPending()))return false;
        if(!Explain("point is within the viewport",ClickWorld(w.Viewport(),points[i])))return false;
        if(!Explain("actual viewport click completes the requested point",!w.Viewport().PickPending()))return false;
        if(!Explain("3D reference point retains its height off the active plane",geometry::Distance(dock->Choice().threePoints[i],points[i])<1e-7))return false;
    }
    const auto before=dock->Choice();auto* retake=w.findChild<QPushButton*>("workPlanePickRole1");
    Click(*retake,retake->rect().center());const auto replacement=points[1]+Vector3{7,3,2};
    if(!ClickWorld(w.Viewport(),replacement))return false;
    const auto after=dock->Choice();
    if(!Explain("retaking one point preserves the other two points",after.threePoints[0]==before.threePoints[0]
        &&after.threePoints[2]==before.threePoints[2]&&geometry::Distance(after.threePoints[1],replacement)<1e-7))return false;
    QPushButton* create=nullptr;for(auto* b:w.findChildren<QPushButton*>("panelConfirm"))
        if(b->isVisible()&&b->text()==QStringLiteral("平面を作る"))create=b;
    if(!Explain(("plane create button is enabled: "+dock->NeedsText().toStdString()).c_str(),create&&create->isEnabled()))return false;
    Click(*create,create->rect().center());
    if(!Explain("three actual point clicks create one plane",CountOfKind(w,domain::EntityKind::WorkPlane)==4))return false;
    for(const auto& c:source){const auto at=std::find_if(w.Session().Scene().curves.begin(),w.Session().Scene().curves.end(),[&](const auto& x){return x.segmentId==c.segmentId;});
        if(!Explain("point selection does not change source wire geometry",at!=w.Session().Scene().curves.end()&&at->segment.StartPoint()==c.segment.StartPoint()&&at->segment.EndPoint()==c.segment.EndPoint()))return false;}
    w.RunCommand("edit.undo");if(CountOfKind(w,domain::EntityKind::WorkPlane)!=3)return false;w.RunCommand("edit.redo");
    return w.SaveAndReopen("three-picked-points.kcd2")&&CountOfKind(w,domain::EntityKind::WorkPlane)==4;
}
bool GenericPointInputs(V2MainWindow& w) {
    if(!Setup(w))return false;auto& v=w.Viewport();
    for(const auto tool:{modeling::DrawingTool::Select,modeling::DrawingTool::Measure,modeling::DrawingTool::Polyline}) {
        w.SelectTool(tool);const auto placed=w.Session().PlacedPointCount();
        std::optional<Vector3> picked;v.BeginPointPick([&](const auto& p){picked=p.point;},"参照点を指定");
        if(!ClickWorld(v,points[1],Qt::ControlModifier))return false;
        if(!Explain("point request takes precedence over selection measurement and drawing",picked&&geometry::Distance(*picked,points[1])<1e-7&&!v.PickPending()
            &&w.Session().PlacedPointCount()==placed&&w.Session().CurrentTool()==tool))return false;
    }
    w.RunCommand("selection.activate");v.SetViewDirection(ViewDirection::Front);
    std::optional<Vector3> edgeOn;v.BeginPointPick([&](const auto& p){edgeOn=p.point;},"点を指定");
    if(!ClickWorld(v,points[1])||!Explain("an edge-on workplane still allows real 3D reference points",edgeOn&&geometry::Distance(*edgeOn,points[1])<1e-7))return false;
    bool accepted=false;v.BeginPointPick([&](const auto&){accepted=true;},"点を指定");
    if(!ClickWorld(v,{30,0,-25}))return false;
    if(!Explain("edge-on empty space never silently supplies the world origin",!accepted&&v.PickPending()))return false;
    w.RunCommand("draw.line");
    return Explain("switching tools cancels the old point request",!v.PickPending());
}
bool SnapAndTransitions(V2MainWindow& w) {
    if(!Setup(w))return false;auto& v=w.Viewport();
    const Vector3 intersection{0,0,30};
    const std::array<std::array<Vector3,2>,2> pairs{
        std::array<Vector3,2>{Vector3{-12,0,30},Vector3{12,0,30}},
        std::array<Vector3,2>{Vector3{0,-12,30},Vector3{0,12,30}}};
    for(const auto& ends:pairs)
        if(w.AddPlainWire({geometry::CurveSegment::MakeLine(ends[0],ends[1]).Value()},"交点の入力",false).IsNil())return false;
    if(!w.SaveAndReopen("point-intersection.kcd2"))return false;
    v.SetViewDirection(ViewDirection::Isometric);v.SetViewCenter({});v.SetVisibleWidthMm(150);
    const auto source=w.Session().Scene().curves;
    for(const auto target:{intersection,points[0]+Vector3{3.5,1.5,1}}) {
        std::optional<Vector3> picked;v.BeginPointPick([&](const auto& p){picked=p.point;},"点を指定");
        const auto at=v.Mapping().Project(target);if(!at)return false;
        v.HoverAt({at->x,at->y});
        if(!Explain("point hover shows the same 3D intersection or midpoint as click",v.Hover().position&&geometry::Distance(*v.Hover().position,target)<1e-7))return false;
        if(!ClickWorld(v,target)||!picked||geometry::Distance(*picked,target)>1e-7)return false;
    }
    bool cancelled=false;
    v.BeginElementPick([](const auto&){return true;},[&](const auto& p){cancelled=!p;});
    v.BeginPointPick([](const auto&){},"点を指定");
    if(!Explain("a point request cancels the previous element request",cancelled&&!v.ReferencePickPending()&&v.PickPending()))return false;
    v.BeginReferencePick({source.front().entityId},[](const auto&){});
    if(!Explain("a reference request cancels the previous point request",!v.PickPending()&&v.ReferencePickPending()))return false;
    v.CancelReferencePick();v.BeginPointPick([](const auto&){},"点を指定");
    w.activateWindow();v.setFocus();QApplication::processEvents();
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&v,&escape);
    return Explain("Escape releases point input without modifying the document",!v.PickPending()&&w.Session().Scene().curves.size()==source.size());
}
bool OtherPointTools(V2MainWindow& w) {
    if(!Setup(w))return false;auto& v=w.Viewport();
    w.RunCommand("grid.move_origin");
    if(!Explain("grid origin click is armed",v.PickPending())||!ClickWorld(v,points[1]))return false;
    const auto expected=v.WorkPlane().PointAt(v.WorkPlane().CoordinateU(points[1]),v.WorkPlane().CoordinateV(points[1]));
    if(!Explain("grid origin accepts a click while selection tool is active",!v.PickPending()&&geometry::Distance(w.Session().Scene().grid.origin,expected)<1e-7))return false;
    w.RunCommand("measure.open");
    for(const auto mode:{app::MeasureMode::TwoPoints,app::MeasureMode::ThreePointAngle}) {
        w.MeasureDock().SetMode(mode);
        const int count=app::MeasurePointCount(mode);
        for(int i=0;i<count;++i)if(!ClickWorld(v,points[i]))return false;
        if(!Explain("measurement collects each successive 3D point",v.MeasurePicks().size()==static_cast<std::size_t>(count)))return false;
        for(int i=0;i<count;++i)if(geometry::Distance(v.MeasurePicks()[i].point,points[i])>1e-7)return false;
    }
    w.RunCommand("selection.activate");app::SelectionSet selection;selection.entityIds.push_back(w.Session().Scene().curves.front().entityId);v.SetSelection(selection);
    w.RunCommand("export.selection");auto* dialog=w.findChild<QDialog*>("selectionOutput");
    if(!Explain("selection output is available",dialog&&dialog->isVisible()))return false;
    auto* placement=dialog->findChild<QComboBox*>("outputPlacement");if(!placement)return false;placement->setCurrentIndex(1);QApplication::processEvents();
    QPushButton* pick=nullptr;for(auto* b:dialog->findChildren<QPushButton*>())if(b->text()==QStringLiteral("基準点を3Dで指定"))pick=b;
    if(!pick)return false;Click(*pick,pick->rect().center());
    if(!Explain("output origin click is armed",v.PickPending())||!ClickWorld(v,points[2]))return false;
    const double coordinates[]{points[2].x,points[2].y,points[2].z};
    for(int i=0;i<3;++i){const auto* value=dialog->findChild<QDoubleSpinBox*>(QStringLiteral("outputSourcePoint_%1").arg(i));
        if(!Explain("output origin preserves the selected 3D coordinate",value&&std::abs(value->value()-coordinates[i])<1e-7))return false;}
    dialog->close();return Explain("output point pick finishes",!v.PickPending());
}
bool PointVisibility(V2MainWindow& w) {
    if(!Setup(w))return false;auto& v=w.Viewport();
    const auto hidden=w.Session().Scene().curves.front().entityId;
    const auto* row=w.ItemOfEntity(hidden);if(!row)return false;
    w.ItemOfEntity(hidden)->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    std::optional<Vector3> picked;v.BeginPointPick([&](const auto& p){picked=p.point;},"点を指定");
    if(!ClickWorld(v,points[0])||!Explain("hidden geometry cannot supply a reference point",picked&&std::abs(picked->z)<1e-7))return false;
    const Vector3 standalone{30,15,0};
    w.SelectTool(modeling::DrawingTool::Point);v.SetSnapSuppressed(true);
    if(!ClickWorld(v,standalone))return false;
    w.SelectTool(modeling::DrawingTool::Select);v.SetSnapSuppressed(false);
    v.BeginPointPick([&](const auto& p){picked=p.point;},"点を指定");
    const auto at=v.Mapping().Project(standalone);if(!at)return false;v.HoverAt({at->x,at->y});
    if(!Explain("standalone drawing point appears as the point snap",v.Hover().snap&&v.Hover().snap->kind==modeling::SnapKind::DrawingPoint))return false;
    return ClickWorld(v,standalone)&&Explain("standalone drawing point is accepted",picked&&geometry::Distance(*picked,standalone)<1e-7);
}
bool SelectedWireExtension(V2MainWindow& w) {
    if(!Setup(w))return false;auto& v=w.Viewport();
    const auto target=w.AddPlainWire({geometry::CurveSegment::MakeLine({-10,-25,8},{0,-25,8}).Value()},"延長する線",false);
    const auto boundary=w.AddPlainWire({geometry::CurveSegment::MakeLine({12,-40,8},{12,-10,8}).Value()},"境界線",false);
    if(target.IsNil()||boundary.IsNil()||!w.SaveAndReopen("point-extend-input.kcd2"))return false;
    v.SetViewDirection(ViewDirection::Isometric);v.SetViewCenter({});v.SetVisibleWidthMm(150);
    app::SelectionSet selection;selection.entityIds={target,boundary};v.SetSelection(selection);
    w.RunCommand("wire.extend");
    if(!Explain("two selected wires request the extension end",v.PickPending())||!ClickWorld(v,{0,-25,8}))return false;
    bool extended=false,retained=false;
    for(const auto& curve:w.Session().Scene().curves) {
        if(curve.entityId==boundary)retained=curve.segment.StartPoint()==Vector3{12,-40,8}&&curve.segment.EndPoint()==Vector3{12,-10,8};
        else if(geometry::Distance(curve.segment.EndPoint(),{12,-25,8})<1e-7)extended=true;
    }
    return Explain("actual end click extends only the target and preserves the boundary",!v.PickPending()&&extended&&retained);
}
bool GridAngleInputs(V2MainWindow& w) {
    if (!Setup(w)) return false;
    auto& view = w.Viewport();
    const auto frame = view.WorkPlane();
    const auto revision = w.Session().GetDocument().Revision();
    const auto original = w.Session().Scene().grid;
    w.RunCommand("grid.edit");
    QApplication::processEvents();
    auto* angle = w.GridDock().findChild<QLineEdit*>("gridAngleDegrees");
    auto* pick = w.GridDock().findChild<QPushButton*>("gridPickAngle");
    if (!Explain("grid angle controls are visible", angle && angle->isVisible()
            && pick && pick->isVisible())) return false;
    const auto enterAngle = [&](const QString& text) {
        angle->setFocus();
        angle->selectAll();
        QKeyEvent input(QEvent::KeyPress, 0, Qt::NoModifier, text);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(angle, &input);
        QApplication::sendEvent(angle, &enter);
        QApplication::processEvents();
    };
    enterAngle("-30");
    const auto numeric = w.Session().Scene().grid;
    if (!Explain("numeric entry rotates around the existing origin",
            geometry::Distance(numeric.uDirection, {std::sqrt(3.0)/2, -0.5, 0}) < 1e-10
            && numeric.origin == original.origin)) return false;
    enterAngle("10 mm");
    if (!Explain("invalid angle leaves the previous orientation in place",
            w.Session().Scene().grid.uDirection == numeric.uDirection
            && !w.GridDock().MessageText().isEmpty())) return false;
    enterAngle("0");
    if (!Explain("zero degrees restores the work plane direction",
            geometry::Distance(w.Session().Scene().grid.uDirection, frame.uAxis) < 1e-10)) return false;
    Click(*pick, pick->rect().center());
    if (!view.PickPending() || !ClickWorld(view, points[0])) return false;
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &cancel);
    if (!Explain("Escape after the first click keeps the old grid",
            !view.PickPending() && w.Session().Scene().grid.uDirection == frame.uAxis)) return false;
    Click(*pick, pick->rect().center());
    if (!ClickWorld(view, points[0]) || !ClickWorld(view, points[0])) return false;
    if (!Explain("the same point twice cannot change the grid",
            !view.PickPending() && w.Session().Scene().grid.uDirection == frame.uAxis)) return false;
    Click(*pick, pick->rect().center());
    if (!ClickWorld(view, points[0]) || !ClickWorld(view, points[1])) return false;
    const auto fitted = w.Session().Scene().grid;
    const auto direction = geometry::Normalized(Vector3{41,4,0});
    if (!Explain("two real endpoint clicks align to the projected direction without moving the origin",
            !view.PickPending() && geometry::Distance(fitted.uDirection, direction) < 1e-10
            && fitted.origin == original.origin && fitted.majorSpacingMm == original.majorSpacingMm
            && view.WorkPlane().uAxis == frame.uAxis
            && w.Session().GetDocument().Revision() == revision)) return false;
    view.SetViewDirection(ViewDirection::Top);
    const auto shot = qEnvironmentVariable("KACHACAD_GRID_ANGLE_SCREENSHOT");
    if (!shot.isEmpty()) {
        QApplication::processEvents();
        if (!Explain("grid angle screenshot saved", w.grab().save(shot))) return false;
    }
    const auto target = fitted.origin + fitted.uDirection * fitted.majorSpacingMm;
    const auto screen = view.Mapping().Project(target);
    if (!screen) return false;
    view.HoverAt({screen->x,screen->y});
    if (!Explain("normal drawing snaps to the rotated grid",
        view.Hover().snap && view.Hover().snap->kind == modeling::SnapKind::GridMajor
        && geometry::Distance(view.Hover().snap->position, target) < 1e-10)) return false;

    // A free direction near the old lattice must not be rounded to it.
    const Vector3 freeStart{0.3,0.6,0};
    const Vector3 freeEnd{10.2,10.6,0};
    Click(*pick, pick->rect().center());
    if (!ClickWorld(view, freeStart) || !ClickWorld(view, freeEnd)) return false;
    const auto freeDirection = geometry::Normalized(freeEnd - freeStart);
    if (!Explain("two free points are not pulled onto the old grid",
            geometry::Distance(w.Session().Scene().grid.uDirection, freeDirection) < 1e-10)) return false;

    w.SelectTool(modeling::DrawingTool::Rectangle);
    const auto first = w.Session().PlacePoint({0,0,0});
    const auto second = w.Session().PlacePoint({10,6,0});
    if (!Explain("rectangle still uses the work plane axes", first.placedPoint && second.committed
            && !second.createdEntityIds.empty())) return false;
    int edges = 0;
    for (const auto& curve : w.Session().Scene().curves) {
        if (curve.entityId != second.createdEntityIds.front()) continue;
        const auto delta = curve.segment.EndPoint() - curve.segment.StartPoint();
        if (!Explain("new rectangle edges remain parallel to the work plane axes",
                std::abs(delta.x) < 1e-10 || std::abs(delta.y) < 1e-10)) return false;
        ++edges;
    }
    return Explain("all four rectangle edges were checked", edges == 4);
}
}
std::vector<SelfTestCase> PointPickingCases(){return {
    {"HP-POINT-WP actual three-point role clicks preserve 3D source geometry",WorkPlaneRoles},
    {"HP-POINT-INPUT point requests override tools and reject undefined plane hits",GenericPointInputs},
    {"HP-POINT-SNAP 3D intersection midpoint hover and request transitions",SnapAndTransitions},
    {"HP-POINT-TOOLS grid origin measurement and output origin actual clicks",OtherPointTools},
    {"HP-POINT-VISIBLE hidden geometry is ignored and standalone points are picked",PointVisibility},
    {"HP-POINT-EXTEND two selected wires accept the requested extension end",SelectedWireExtension},
    {"HP-GRID-ANGLE numeric and two-point rotation share the drawing snap grid",GridAngleInputs}};}
}
