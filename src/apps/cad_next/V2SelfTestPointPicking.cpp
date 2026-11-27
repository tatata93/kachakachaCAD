#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"
#include "V2MeasureDock.h"
#include "kachakacha/modeling/WorkPlane.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QKeyEvent>
#include <QComboBox>
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QDoubleSpinBox>
#include <QTreeWidgetItem>
#include <QEvent>
#include <QPointF>
#include <QString>
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
bool Setup(V2MainWindow& w,const std::array<Vector3,3>& input=points) {
    w.resize(1600,1000);QApplication::processEvents();w.RunCommand("file.new");
    for(const auto p:input)if(w.AddPlainWire({geometry::CurveSegment::MakeLine(p,p+Vector3{7,3,2}).Value()},"点指定の入力",false).IsNil())return false;
    if(!w.SaveAndReopen("point-input-source.kcd2"))return false;
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Isometric);v.SetViewCenter({});v.SetVisibleWidthMm(150);v.SetSnapSuppressed(false);
    w.RunCommand("selection.activate");return true;
}
bool WorkPlaneRoles(V2MainWindow& w,bool fractional=false) {
    const std::array<Vector3,3> input=fractional?std::array<Vector3,3>{{
        {-12,3.984861993178182,16.5},{-0.5,0.0062320222362686195,16.5},
        {-11.5,6.797437494192961,25}}}:points;
    if(!Setup(w,input))return false;w.SetWorkPlaneChooser({});w.RunCommand("workplane.create");
    auto* dock=w.WorkPlaneDock();if(!dock)return false;
    WorkPlaneChoice choice;choice.method=modeling::WorkPlaneMethod::ThreePoints;dock->SetChoice(choice);
    const auto source=w.Session().Scene().curves;
    for(int i=0;i<3;++i){
        auto* button=w.findChild<QPushButton*>(QStringLiteral("workPlanePickRole%1").arg(i));
        if(!Explain("point role button is visible",button&&button->isVisible()))return false;Click(*button,button->rect().center());
        if(!Explain("point role is armed by its visible button",w.Viewport().PickPending()))return false;
        if(!Explain("point is within the viewport",ClickWorld(w.Viewport(),input[i])))return false;
        if(!Explain("actual viewport click completes the requested point",!w.Viewport().PickPending()))return false;
        if(!Explain("3D reference point retains its full picked precision",geometry::Distance(dock->Choice().threePoints[i],input[i])<1e-10))return false;
    }
    const auto before=dock->Choice();auto* retake=w.findChild<QPushButton*>("workPlanePickRole1");
    Click(*retake,retake->rect().center());const auto replacement=input[1]+Vector3{7,3,2};
    if(!ClickWorld(w.Viewport(),replacement))return false;
    const auto after=dock->Choice();
    if(!Explain("retaking one point preserves the other two points",after.threePoints[0]==before.threePoints[0]
        &&after.threePoints[2]==before.threePoints[2]&&geometry::Distance(after.threePoints[1],replacement)<1e-7))return false;
    QPushButton* create=nullptr;for(auto* b:w.findChildren<QPushButton*>("panelConfirm"))
        if(b->isVisible()&&b->text()==QStringLiteral("平面を作る"))create=b;
    if(!Explain(("plane create button is enabled: "+dock->NeedsText().toStdString()).c_str(),create&&create->isEnabled()))return false;
    Click(*create,create->rect().center());
    if(!Explain("three actual point clicks create one plane",CountOfKind(w,domain::EntityKind::WorkPlane)==4))return false;
    const auto& plane=w.Viewport().WorkPlane();
    for(const auto p:after.threePoints)if(!Explain("the constructed plane passes through every picked point",
        std::abs(geometry::Dot(p-plane.origin,plane.normal))<1e-9))return false;
    for(const auto& c:source){const auto at=std::find_if(w.Session().Scene().curves.begin(),w.Session().Scene().curves.end(),[&](const auto& x){return x.segmentId==c.segmentId;});
        if(!Explain("point selection does not change source wire geometry",at!=w.Session().Scene().curves.end()&&at->segment.StartPoint()==c.segment.StartPoint()&&at->segment.EndPoint()==c.segment.EndPoint()))return false;}
    const auto planeId=w.ActiveWorkPlaneId();
    w.RunCommand("edit.undo");if(CountOfKind(w,domain::EntityKind::WorkPlane)!=3)return false;w.RunCommand("edit.redo");
    if(!w.SaveAndReopen("three-picked-points.kcd2")||CountOfKind(w,domain::EntityKind::WorkPlane)!=4)return false;
    const auto restored=w.WorkPlaneFrameOf(planeId);
    if(!restored)return false;
    return Explain("reopened plane still contains the original exact picked points",
        std::all_of(after.threePoints.begin(),after.threePoints.end(),[&](const auto p){
            return std::abs(geometry::Dot(p-restored->origin,restored->normal))<1e-9;
        }));
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

std::vector<base::EntityId> OverlappingSources(V2MainWindow& w,bool coincident=false)
{
    w.resize(1600,1000);QApplication::processEvents();w.RunCommand("file.new");
    std::vector<base::EntityId> ids;
    for (double z:{0.,coincident?0.:7.}) {
        std::vector<geometry::CurveSegment> segments;
        if (coincident) segments={geometry::CurveSegment::MakeCubicBezier({{-20,0,z},{-16,0,z},{-12,0,z},{-8,0,z}}).Value(),
            geometry::CurveSegment::MakeCubicBezier({{-8,0,z},{-4,0,z},{0,0,z},{4,0,z}}).Value()};
        else segments={geometry::CurveSegment::MakeLine({-20,0,z},{20,0,z}).Value()};
        ids.push_back(w.AddPlainWire(std::move(segments),"重なる線",false));
    }
    if (!w.SaveAndReopen("point-overlap-source.kcd2")) return {};
    auto* scope=w.findChild<QComboBox*>("drawingScope");if(!scope)return {};
    scope->setCurrentIndex(1);
    auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Top);view.SetViewCenter({});
    view.SetVisibleWidthMm(80);view.SetSnapSuppressed(false);
    return ids;
}

bool TabToPointSource(V2Viewport& view,base::EntityId id)
{
    for (int i=0;i<view.CandidateCount()+1;++i) {
        if (view.CurrentCandidate() && view.CurrentCandidate()->entityId==id && view.ExplicitPointTarget()) return true;
        QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(&view,&tab);
    }
    return false;
}

bool OverlapDrawingPoint(V2MainWindow& w)
{
    const auto ids=OverlappingSources(w);if(ids.size()!=2)return false;
    w.RunCommand("draw.line");auto& view=w.Viewport();
    const auto at=view.Mapping().Project({-3,0,7});if(!at)return false;
    const QPointF pixel(at->x,at->y);view.HoverAt(pixel);
    if(!Explain("actual Tab selects the other depth during drawing",TabToPointSource(view,ids[1])))return false;
    if(!Explain("highlight snap source and preview point use the explicitly chosen curve",view.CurrentCandidate()->entityId==ids[1]
        &&view.Hover().snap&&view.Hover().snap->entityId==ids[1]
        &&view.Hover().position&&std::abs(view.Hover().position->z-7)<1e-10))return false;
    QKeyEvent suppress(QEvent::KeyPress,Qt::Key_S,Qt::NoModifier);QApplication::sendEvent(&view,&suppress);
    QKeyEvent release(QEvent::KeyRelease,Qt::Key_S,Qt::NoModifier);QApplication::sendEvent(&view,&release);
    if(!Explain("S release immediately synchronizes normal hover and snap without pointer motion",!view.ExplicitPointTarget()
        &&view.Hover().snap&&view.CurrentCandidate()&&view.Hover().snap->entityId==view.CurrentCandidate()->entityId))return false;
    if(!TabToPointSource(view,ids[1]))return false;
    const auto preview=*view.Hover().position;
    Click(view,pixel);
    if(!Explain("the actual click places the same 3D point",w.Session().PlacedPointCount()==1
        &&geometry::Distance(w.Session().ConstraintAnchor(),preview)<1e-9))return false;
    const auto away=view.Mapping().Project({10,20,0});if(!away)return false;
    view.HoverAt({away->x,away->y});
    if(!Explain("moving away releases the explicit source",!view.ExplicitPointTarget()))return false;
    w.RunCommand("draw.line");
    return Explain("changing the tool never retains the previous point source",!view.ExplicitPointTarget()
        &&CountOfKind(w,domain::EntityKind::Wire)==2);
}


bool OverlapPointTransition(V2MainWindow& w,int mode)
{
    const auto ids=OverlappingSources(w);if(ids.size()!=2)return false;
    auto& view=w.Viewport();w.RunCommand("draw.line");
    if(mode && !ClickWorld(view,{-15,0,0}))return false;
    if(mode)view.CloseCursorInput(); // Tab keeps its field-navigation role while the numeric panel is open.
    const auto at=view.Mapping().Project({-3,0,7});if(!at)return false;
    const QPointF pixel(at->x,at->y);view.HoverAt(pixel);
    if(!view.Hover().position||!view.Hover().snap)return false;
    const auto normal=*view.Hover().position;const auto normalSource=view.Hover().snap->entityId;
    if(!TabToPointSource(view,ids[1]))return false;
    if(mode==0) {
        Click(view,pixel,Qt::ControlModifier);
        if(!Explain("Ctrl selects the highlighted source without placing a point",
            w.Session().PlacedPointCount()==0&&app::IsSelected(view.Selection(),ids[1])))return false;
        if(!Explain("releasing Ctrl selection immediately refreshes the snap without pointer movement",
            !view.ExplicitPointTarget()&&view.Hover().snap&&view.CurrentCandidate()
            &&view.Hover().snap->entityId==normalSource&&view.CurrentCandidate()->entityId==normalSource))return false;
    } else {
        if(mode==2&&!view.OpenCursorInput())return false;
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&view,&escape);
        if(!Explain("Esc cancels pending points and the cursor input panel",
            w.Session().PlacedPointCount()==0&&!view.CursorPanel().active))return false;
    }
    view.HoverAt(pixel);
    if(!Explain("returning to point input releases the old target and synchronizes normal snap and highlight",
        !view.ExplicitPointTarget()&&view.Hover().position&&view.Hover().snap&&view.CurrentCandidate()
        &&view.Hover().snap->entityId==normalSource&&view.CurrentCandidate()->entityId==normalSource
        &&geometry::Distance(*view.Hover().position,normal)<1e-9))return false;
    Click(view,pixel);
    if(!Explain("the next real click uses the refreshed normal target",
        w.Session().PlacedPointCount()==1&&geometry::Distance(w.Session().ConstraintAnchor(),normal)<1e-9))return false;
    w.RunCommand("selection.activate");
    return CountOfKind(w,domain::EntityKind::Wire)==2;
}


bool IncompleteFinishKeepsPointTarget(V2MainWindow& w)
{
    const auto ids=OverlappingSources(w);if(ids.size()!=2)return false;
    auto& view=w.Viewport();w.RunCommand("draw.line");
    if(!ClickWorld(view,{-15,0,0}))return false;
    view.CloseCursorInput();
    const auto at=view.Mapping().Project({-3,0,7});if(!at)return false;
    const QPointF pixel(at->x,at->y);view.HoverAt(pixel);
    if(!TabToPointSource(view,ids[1])||!view.Hover().position)return false;
    const auto expected=*view.Hover().position;
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(&view,&enter);
    if(!Explain("an incomplete finish retains the pending point and explicitly chosen source",
        w.Session().PlacedPointCount()==1&&view.ExplicitPointTarget()&&view.Hover().position
        &&geometry::Distance(*view.Hover().position,expected)<1e-9))return false;
    Click(view,pixel);
    if(!Explain("continuing after a refused finish commits to that same source",CountOfKind(w,domain::EntityKind::Wire)==3
        &&std::any_of(w.Session().Scene().curves.begin(),w.Session().Scene().curves.end(),[&](const auto& curve){
            return curve.entityId!=ids[0]&&curve.entityId!=ids[1]&&geometry::Distance(curve.segment.EndPoint(),expected)<1e-9;})))return false;
    w.RunCommand("selection.activate");w.RunCommand("edit.undo");
    return CountOfKind(w,domain::EntityKind::Wire)==2;
}

bool OverlapReferencePoint(V2MainWindow& w)
{
    const auto ids=OverlappingSources(w,true);if(ids.size()!=2)return false;
    auto& view=w.Viewport();w.RunCommand("selection.activate");
    base::SegmentId secondSegment;
    for(const auto& source:w.Session().Scene().curves)if(source.entityId==ids[1]&&source.segment.StartPoint().x==-8)secondSegment=source.segmentId;
    std::optional<V2Viewport::PickedPoint> picked;
    view.BeginPointPick([&](const auto& value){picked=value;},"参照点を選ぶ");
    const auto at=view.Mapping().Project({-3,0,0});if(!at)return false;
    const QPointF pixel(at->x,at->y);view.HoverAt(pixel);
    if(!Explain("point requests expose Tab candidates even over the selection tool",TabToPointSource(view,ids[1])))return false;
    Click(view,pixel);
    if(!Explain("coincident coordinates still return the selected curve UUID",picked&&picked->curve
        &&picked->curve->entityId==ids[1]&&picked->curve->segmentId==secondSegment&&!view.PickPending()))return false;
    view.BeginPointPick([&](const auto& value){picked=value;},"参照点を選ぶ");view.HoverAt(pixel);
    std::optional<int> wanted;
    for(int i=0;i<view.CandidateCount();++i) {view.PreviewCandidate(i);
        if(view.CurrentCandidate()->entityId==ids[1])wanted=i;}
    if(!wanted)return false;
    view.SetContextMenuCallback([&](const auto&,const auto& labels){
        if(labels.empty())return std::optional<int>{};
        QEvent leave(QEvent::Leave);QApplication::sendEvent(&view,&leave);
        view.PreviewCandidate(*wanted);return wanted;
    });
    view.PressRightWithoutMoving(pixel);
    if(!Explain("right candidate menu chooses the point source without placing a point",view.PickPending()
        &&view.ExplicitPointTarget()&&view.ExplicitPointTarget()->entityId==ids[1]))return false;
    picked.reset();Click(view,pixel);
    if(!picked||!picked->curve||picked->curve->entityId!=ids[1])return false;
    return Explain("requested point completion releases the source and preserves wires",!view.ExplicitPointTarget()
        &&CountOfKind(w,domain::EntityKind::Wire)==2);
}

bool CurvePlanePointInput(V2MainWindow& w)
{
    w.RunCommand("file.new");w.resize(1600,1000);QApplication::processEvents();
    const auto line=geometry::CurveSegment::MakeLine({-20,-5,-3},{20,5,3}).Value();
    const auto source=w.AddPlainWire({line},"平面を横切る曲線",false);
    if(source.IsNil()||!w.SaveAndReopen("curve-plane-pick-source.kcd2"))return false;
    auto* scope=w.findChild<QComboBox*>("drawingScope");if(!scope)return false;scope->setCurrentIndex(0);
    w.RunCommand("draw.line");auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Top);
    view.SetViewCenter({});view.SetVisibleWidthMm(80);view.SetSnapSuppressed(false);
    const auto at=view.Mapping().Project({});if(!at)return false;view.HoverAt({at->x,at->y});
    if(!Explain("plane drawing snaps to the actual curve-plane crossing without flattening",view.Hover().snap
        &&view.Hover().snap->kind==modeling::SnapKind::WorkPlaneIntersection
        &&geometry::Distance(view.Hover().snap->position,{})<1e-10))return false;
    if(!ClickWorld(view,{}))return false;
    view.SetSnapSuppressed(true);const bool second=ClickWorld(view,{2,0,0});view.SetSnapSuppressed(false);
    if(!second||CountOfKind(w,domain::EntityKind::Wire)!=2)return false;
    const auto& made=w.Session().Scene().curves.back().segment;
    if(!Explain("generated line starts exactly at the finite crossing",geometry::Distance(made.StartPoint(),{})<1e-10))return false;
    w.RunCommand("edit.undo");w.RunCommand("edit.redo");
    if(!w.SaveAndReopen("curve-plane-drawn.kcd2"))return false;
    const auto& curves=w.Session().Scene().curves;
    const auto found=std::find_if(curves.begin(),curves.end(),[&](const auto& curve){return curve.entityId==source;});
    return Explain("history and reopen preserve the original spatial curve",found!=curves.end()
        &&found->segment.StartPoint()==line.StartPoint()&&found->segment.EndPoint()==line.EndPoint()
        &&CountOfKind(w,domain::EntityKind::Wire)==2);
}


bool CrossingPointSourceIsNotAnOffPlaneSelection(V2MainWindow& w)
{
    w.RunCommand("file.new");w.resize(1600,1000);QApplication::processEvents();
    const auto a=geometry::CurveSegment::MakeLine({-20,-5,-3},{20,5,3}).Value();
    const auto b=geometry::CurveSegment::MakeLine({-20,5,-4},{20,-5,4}).Value();
    const auto first=w.AddPlainWire({a},"平面を横切る線",false),second=w.AddPlainWire({b},"平面を横切る別の線",false);
    if(first.IsNil()||second.IsNil()||!w.SaveAndReopen("crossing-point-sources.kcd2"))return false;
    auto* scope=w.findChild<QComboBox*>("drawingScope");if(!scope)return false;scope->setCurrentIndex(0);
    w.RunCommand("draw.line");auto& view=w.Viewport();view.SetSelection({});
    view.SetViewDirection(ViewDirection::Top);view.SetViewCenter({});view.SetVisibleWidthMm(80);view.SetSnapSuppressed(false);
    const auto root=view.Mapping().Project({});if(!root)return false;
    const QPointF crossing(root->x,root->y);view.HoverAt(crossing);
    if(!Explain("Tab can choose the source of a real finite workplane crossing",TabToPointSource(view,second)
        &&view.Hover().position&&geometry::Distance(*view.Hover().position,{})<1e-9))return false;
    if(!Explain("the source candidate stores the actual root rather than an off-plane closest point",
        view.CurrentCandidate()&&geometry::Distance(view.CurrentCandidate()->hitPoint,{})<1e-9
        &&view.CurrentCandidate()->curveParameter&&std::abs(*view.CurrentCandidate()->curveParameter-.5)<1e-9))return false;
    Click(view,crossing,Qt::ControlModifier);
    if(!Explain("Ctrl target selection does not select the entire off-plane curves at their crossing",
        view.Selection().entityIds.empty()&&w.Session().PlacedPointCount()==0))return false;
    const auto away=a.Evaluate(.8);const auto screen=view.Mapping().Project(away);if(!screen)return false;
    const QPointF pixel(screen->x,screen->y);view.HoverAt(pixel);
    if(!Explain("away from the real root an off-plane or projected line is not a drawing source",
        view.HoveredEntityId().IsNil()&&!view.ExplicitPointTarget()))return false;
    scope->setCurrentIndex(1);view.HoverAt(pixel);
    if(!Explain("3D drawing still permits the spatial source curve",view.HoveredEntityId()==first))return false;
    scope->setCurrentIndex(0);w.RunCommand("selection.activate");
    const auto revision=w.Session().GetDocument().Revision();std::optional<Vector3> picked;
    view.BeginPointPick([&](const auto& point){picked=point.point;},"空間上の点を指定");view.HoverAt(pixel);Click(view,pixel);
    return Explain("reference point requests still accept actual 3D points without modifying sources",
        picked&&geometry::Distance(*picked,away)<1e-8&&w.Session().GetDocument().Revision()==revision
        &&CountOfKind(w,domain::EntityKind::Wire)==2);
}


bool WorkPlaneSourceOrderUsesActualRoots(V2MainWindow& w)
{
    w.RunCommand("file.new");w.resize(1600,1000);QApplication::processEvents();
    const Vector3 center{5,5,0};
    const auto a=geometry::CurveSegment::MakeLine(center+Vector3{-1,0,2},center+Vector3{1,0,-.5}).Value();
    const auto b=geometry::CurveSegment::MakeLine(center+Vector3{-1,.08,1.08},center+Vector3{1,.08,-.92}).Value();
    if(w.AddPlainWire({a},"投影上は近い線",false).IsNil())return false;
    const auto closerRoot=w.AddPlainWire({b},"実交点が近い線",false);if(closerRoot.IsNil())return false;
    if(!w.SaveAndReopen("workplane-source-order.kcd2"))return false;
    auto* scope=w.findChild<QComboBox*>("drawingScope");if(!scope)return false;scope->setCurrentIndex(0);
    w.RunCommand("draw.line");QApplication::processEvents();auto& view=w.Viewport();
    view.SetViewDirection(ViewDirection::Top);view.SetViewCenter(center);view.SetVisibleWidthMm(80);view.SetSnapSuppressed(false);
    const auto screen=view.Mapping().Project(center);if(!screen)return false;
    view.HoverAt({screen->x,screen->y});
    if(!Explain(("the plane roots have source candidates; count="+std::to_string(view.CandidateCount())).c_str(),
        view.PreviewCandidate(0)))return false;
    return Explain("the first source is ordered by its actual plane intersection rather than the projected line distance",
        view.CurrentCandidate()&&view.CurrentCandidate()->entityId==closerRoot
        &&geometry::Distance(view.CurrentCandidate()->hitPoint,center+Vector3{.08,.08,0})<1e-8);
}

bool SharedExplicitSource(V2MainWindow& w)
{
    const auto path=qEnvironmentVariable("KACHACAD_ZOOM_OWNER_FILE");
    if(!Explain("open the preserved supplied source",!path.isEmpty()&&w.OpenDocumentFile(path)))return false;
    w.resize(1600,1000);QApplication::processEvents();
    const auto sources=w.Session().Scene().curves;
    const auto revision=w.Session().GetDocument().Revision();
    const auto body=std::find_if(sources.begin(),sources.end(),[](const auto& curve){return curve.entityId.ToString().ends_with("0095");});
    if(!Explain(("the source scene contains formal body arc 0095; curve count="+std::to_string(sources.size())).c_str(),body!=sources.end()))return false;
    const auto source=*body;
    const auto expected=source.segment.Evaluate(.55);
    auto* scope=w.findChild<QComboBox*>("drawingScope");if(!Explain("the source model exposes the drawing scope control",scope!=nullptr))return false;scope->setCurrentIndex(1);
    auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Top);view.SetViewCenter(expected);
    view.SetVisibleWidthMm(140);view.SetSnapSuppressed(false);
    const auto screen=view.Mapping().Project(expected);if(!Explain("the source arc point can be projected into the viewport",screen.has_value()))return false;
    const QPointF pixel(screen->x,screen->y);
    w.RunCommand("selection.activate");
    std::optional<V2Viewport::PickedPoint> picked;
    view.BeginPointPick([&](const auto& value){picked=value;},"元の曲線から点を指定");view.HoverAt(pixel);
    if(!Explain("supplied model exposes its formal source arc through actual Tab",TabToPointSource(view,source.entityId)))return false;
    Click(view,pixel);
    if(!Explain("supplied model reference takes its point and segment from that arc",picked&&picked->curve
        &&picked->curve->entityId==source.entityId&&picked->curve->segmentId==source.segmentId
        &&source.segment.ClosestPoint(picked->point).distance<1e-8))return false;
    w.RunCommand("draw.line");QApplication::processEvents();
    const auto drawingScreen=view.Mapping().Project(expected);
    if(!Explain("reobserve the source point after changing the tool panel",drawingScreen.has_value()))return false;
    const QPointF drawingPixel(drawingScreen->x,drawingScreen->y);view.HoverAt(drawingPixel);
    if(!Explain(("actual drawing can target the formal arc; candidates="+std::to_string(view.CandidateCount())
        +" status="+w.StatusText().toStdString()).c_str(),TabToPointSource(view,source.entityId)&&view.Hover().position))return false;
    const auto point=*view.Hover().position;Click(view,drawingPixel);
    if(!Explain("actual drawing uses the highlighted original body curve",w.Session().PlacedPointCount()==1
        &&geometry::Distance(w.Session().ConstraintAnchor(),point)<1e-9
        &&source.segment.ClosestPoint(point).distance<1e-8))return false;
    w.RunCommand("selection.activate");
    return Explain("source model geometry and segment identity remain untouched",w.Session().GetDocument().Revision()==revision
        &&w.Session().Scene().curves.size()==sources.size()
        &&std::any_of(w.Session().Scene().curves.begin(),w.Session().Scene().curves.end(),[&](const auto& curve){
            return curve.entityId==source.entityId&&curve.segmentId==source.segmentId
                &&curve.segment.StartPoint()==source.segment.StartPoint()&&curve.segment.EndPoint()==source.segment.EndPoint();}));
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

bool ArcCrossingDrawing(V2MainWindow& w)
{
    using geometry::CurveSegment;
    using modeling::SnapKind;
    constexpr double radius = 20.06081081081081;
    const Vector3 crossing{-12, radius - std::sqrt(radius * radius - 144), 16.5};
    const Vector3 nearby{-11.99604993837049, 3.9819141986901467, 16.5};
    const Vector3 chordStart{-0.5000000161227988, 0.006232022702284679, 16.5};
    w.resize(1600, 1000);
    QApplication::processEvents();
    auto& view = w.Viewport();
    const auto fixture = qEnvironmentVariable("KACHACAD_ARC_CROSSING_FIXTURE");
    base::EntityId arcId, verticalId, chordId;
    if (!fixture.isEmpty()) {
        if (!w.OpenDocumentFile(fixture)) return false;
        for (int i = 0; i < w.PlaneComboCount(); ++i)
            if (w.PlaneComboText(i) == QStringLiteral("作業面4")) w.SelectPlaneCombo(i);
        for (const auto& curve : w.Session().Scene().curves) {
            const auto& segment = curve.segment;
            if (segment.Kind() == geometry::CurveKind::CircularArc
                && geometry::Distance(segment.Center(), {0, radius, 16.5}) < 1e-8) arcId = curve.entityId;
            if (segment.StartPoint() == Vector3{-12, 2, 16.5}
                && segment.EndPoint() == Vector3{-12, 5, 16.5}) verticalId = curve.entityId;
            if (segment.StartPoint() == chordStart && segment.EndPoint() == nearby) chordId = curve.entityId;
        }
    } else {
        const auto arc = CurveSegment::MakeCircularArc({0, radius, 16.5}, {0, 0, 1},
            {-0.996968676321994, -0.0778039744021556, 0}, radius, 0, 2.985827281201636).Value();
        arcId = w.AddPlainWire({arc}, "境界の円弧", false);
        verticalId = w.AddPlainWire({CurveSegment::MakeLine({-12, 2, 16.5}, {-12, 5, 16.5}).Value()}, "交差する線", false);
        chordId = w.AddPlainWire({CurveSegment::MakeLine(chordStart, nearby).Value()}, "近くに端点のある線", false);
        app::SelectionSet plane;
        plane.entityIds = {w.ActiveWorkPlaneId()};
        view.SetSelection(plane);
        w.SetWorkPlaneChooser([&](const WorkPlaneChoice&, const app::WorkPlaneFacts&) {
            WorkPlaneChoice choice;
            choice.method = modeling::WorkPlaneMethod::OffsetFromPlane;
            choice.offsetMm = crossing.z;
            return std::optional{choice};
        });
        w.RunCommand("workplane.create");
    }
    if (!Explain("the three source wires and the offset work plane are loaded",
            !arcId.IsNil() && !verticalId.IsNil() && !chordId.IsNil()
            && std::abs(view.WorkPlane().origin.z - crossing.z) < 1e-8)) return false;

    w.RunCommand("draw.line");
    view.SetViewDirection(ViewDirection::Top);
    view.SetViewCenter(crossing);
    view.SetVisibleWidthMm(view.width() / 1300.0);
    view.SetSnapSuppressed(false);
    const auto hover = [&](const Vector3& world) {
        const auto pixel = view.Mapping().Project(world);
        if (pixel) view.HoverAt(QPointF(pixel->x, pixel->y).toPoint());
        return pixel.has_value();
    };
    if (!hover(nearby)
        || !Explain(("the adjacent endpoint or drawing point is still reachable: " + view.Hover().messageJa).c_str(), view.Hover().snap
            && (view.Hover().snap->kind == SnapKind::Endpoint || view.Hover().snap->kind == SnapKind::DrawingPoint)
            && geometry::Distance(view.Hover().snap->position, nearby) < 1e-8)) return false;
    if (!hover(crossing)
        || !Explain("the cursor changes to the real arc intersection", view.Hover().snap
            && view.Hover().snap->kind == SnapKind::Intersection
            && geometry::Distance(view.Hover().snap->position, crossing) < 1e-8)) return false;
    const auto screenshot = qEnvironmentVariable("KACHACAD_ARC_CROSSING_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        QApplication::processEvents();
        w.grab().save(screenshot);
    }
    const auto sourceCount = CountOfKind(w, domain::EntityKind::Wire);
    const auto pointCount = CountOfKind(w, domain::EntityKind::Point);
    const auto pixel = view.Mapping().Project(crossing);
    if (!pixel) return false;
    Click(view, QPointF(pixel->x, pixel->y).toPoint());
    view.SetSnapSuppressed(true);
    const bool endClicked = ClickWorld(view, crossing + Vector3{0.12, 0.08, 0});
    view.SetSnapSuppressed(false);
    if (!endClicked || !Explain("the clicked intersection becomes the exact start of the new wire",
            CountOfKind(w, domain::EntityKind::Wire) == sourceCount + 1
            && geometry::Distance(w.Session().Scene().curves.back().segment.StartPoint(), crossing) < 1e-8)) return false;
    w.RunCommand("edit.undo");
    w.RunCommand("selection.activate");
    app::SelectionSet selected;
    selected.entityIds = {chordId, verticalId, arcId};
    view.SetSelection(selected);
    w.RunCommand("wire.intersection_points");
    const auto hasPoint = [&](const Vector3& point) {
        return std::any_of(w.Session().Scene().points.begin(), w.Session().Scene().points.end(), [&](const auto& item) {
            return geometry::Distance(item.position, point) < 1e-8;
        });
    };
    if (!Explain("intersection points are generated for all selected pairs",
            CountOfKind(w, domain::EntityKind::Wire) == sourceCount
            && CountOfKind(w, domain::EntityKind::Point) == pointCount + 3
            && hasPoint(crossing) && hasPoint(nearby) && hasPoint(chordStart))) return false;
    w.RunCommand("edit.undo");
    return Explain("one undo removes the points and preserves the source wires",
        CountOfKind(w, domain::EntityKind::Point) == pointCount
        && CountOfKind(w, domain::EntityKind::Wire) == sourceCount);
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

bool ZoomedCircularDisplay(V2MainWindow& w, bool circle)
{
    using geometry::CurveSegment;
    w.RunCommand("file.new");
    w.resize(1600,1000);QApplication::processEvents();
    auto& view=w.Viewport();
    constexpr double radius=20.06081081081081;
    constexpr double angle=0.27;
    const Vector3 center{0,radius,0};
    const Vector3 at=center+Vector3{radius*std::cos(angle),radius*std::sin(angle),0};
    const auto arc=CurveSegment::MakeCircularArc(center,{0,0,1},{1,0,0},radius,-0.4,2.8).Value();
    const auto source=circle?CurveSegment::MakeCircle(center,{0,0,1},{1,0,0},radius).Value():arc;
    const auto first=w.AddPlainWire({source},"円形の線",false);
    if(first.IsNil()||!w.SaveAndReopen("zoomed-circle-input.kcd2"))return false;
    view.SetViewDirection(ViewDirection::Top);view.SetViewCenter(at);
    auto display=view.DisplaySettingsNow();display.gridVisible=false;display.workPlaneVisible=false;
    view.SetDisplaySettings(display);for(int i=0;i<3;++i)view.SetAxisVisible(i,false);
    view.SetSelection({});view.HoverAt({5,view.height()-40.0});
    const auto frame=[&]{QImage result(view.size(),QImage::Format_ARGB32);view.render(&result);return result;};
    const auto nearColor=[](const QImage& image,const geometry::ScreenPoint& pixel,QColor color){
        for(int y=int(pixel.y)-2;y<=int(pixel.y)+2;++y)for(int x=int(pixel.x)-2;x<=int(pixel.x)+2;++x){
            if(!image.rect().contains(x,y))continue;
            const auto c=image.pixelColor(x,y);
            if(std::abs(c.red()-color.red())+std::abs(c.green()-color.green())+std::abs(c.blue()-color.blue())<40)return true;
        }
        return false;
    };
    for(const auto direction:{ViewDirection::Top,ViewDirection::Isometric})for(const double pixelsPerMm:{100.0,5000.0,50000.0}){
        view.SetViewDirection(direction);
        view.SetVisibleWidthMm(view.width()/pixelsPerMm);
        const auto image=frame();
        const auto output=qEnvironmentVariable("KACHACAD_CURVE_ZOOM_PROOF_DIR");
        if(!output.isEmpty())image.save(output+QString("/base-%1-%2.png").arg(circle).arg(pixelsPerMm));
        for(int offset:{-180,0,180}){
            const double a=angle+offset/(radius*pixelsPerMm);
            const auto p=view.Mapping().Project(center+Vector3{radius*std::cos(a),radius*std::sin(a),0});
            const auto reason="zoomed curve ink follows analytic circle rather than a coarse cubic: scale="
                +std::to_string(pixelsPerMm)+" offset="+std::to_string(offset);
            if(!Explain(reason.c_str(),p
                &&nearColor(image,*p,view.SemanticColor(app::SemanticState::Default))))return false;
        }
    }
    // A differently trimmed, reversed arc on exactly the same circle must cover it.
    const auto second=w.AddPlainWire({CurveSegment::MakeCircularArc(center,{0,0,1},{1,0,0},radius,0.7,-0.9).Value()},"同じ円の部分",false);
    if(second.IsNil()||!w.SaveAndReopen("zoomed-circle-overlap.kcd2"))return false;
    view.SetViewDirection(ViewDirection::Top);view.SetViewCenter(at);view.SetVisibleWidthMm(view.width()/50000.0);
    view.SetDisplaySettings(display);for(int i=0;i<3;++i)view.SetAxisVisible(i,false);
    app::SelectionSet selected;selected.entityIds={second};view.SetSelection(selected);
    const auto image=frame();
    const auto p=view.Mapping().Project(at);if(!p)return false;
    if(!Explain("selection of a reversed partial arc stays on the original circle",nearColor(image,*p,
            view.SemanticColor(app::SemanticState::Selected))))return false;
    for(int y=int(p->y)-100;y<=int(p->y)+100;++y)for(int x=int(p->x)-100;x<=int(p->x)+100;++x){
        const auto ink=image.pixelColor(x,y);
        if(ink.red()>45||ink.green()>45||ink.blue()>45)continue;
        const auto world=view.Mapping().UnprojectOntoPlane({x+0.5,y+0.5},{},{0,0,1});
        if(!Explain("coincident source arcs do not leave a second dark stroke away from the selected curve",world
            &&std::abs(geometry::Distance(*world,center)-radius)*view.Mapping().PixelsPerMillimeterAt(at)<4.0))return false;
    }
    view.SetSelection({});view.HoverAt({p->x,p->y});
    if(!Explain("hover ink uses the same exact curve position",nearColor(frame(),*p,
        view.SemanticColor(app::SemanticState::Hover))))return false;
    const auto output=qEnvironmentVariable("KACHACAD_CURVE_ZOOM_PROOF_DIR");
    if(!output.isEmpty())image.save(output+(circle?"/circle.png":"/arc.png"));
    const auto before=w.Session().Scene().curves;
    if(!w.SaveAndReopen("zoomed-circle-display.kcd2"))return false;
    return Explain("rendering zoom and selection preserve exact saved curve definitions",
        w.Session().Scene().curves.size()==before.size()&&std::all_of(before.begin(),before.end(),[&](const auto& old){
            return std::any_of(w.Session().Scene().curves.begin(),w.Session().Scene().curves.end(),[&](const auto& now){
                return now.entityId==old.entityId&&now.segmentId==old.segmentId
                    &&now.segment.Kind()==old.segment.Kind()&&now.segment.Center()==old.segment.Center()
                    &&now.segment.Normal()==old.segment.Normal()&&now.segment.ReferenceDirection()==old.segment.ReferenceDirection()
                    &&now.segment.Radius()==old.segment.Radius()&&now.segment.StartAngleRad()==old.segment.StartAngleRad()
                    &&now.segment.SweepAngleRad()==old.segment.SweepAngleRad();
            });
        }));
}

bool ZoomedArcEndpoint(V2MainWindow& w)
{
    using geometry::CurveSegment;using modeling::SnapKind;
    constexpr double radius=20.06081081081081;
    const Vector3 corner{12,radius-std::sqrt(radius*radius-144),16.5};
    const Vector3 crossing{18,radius-std::sqrt(radius*radius-324),16.5};
    w.RunCommand("file.new");w.resize(1600,1000);QApplication::processEvents();
    if(w.AddPlainWire({CurveSegment::MakeCircularArc({0,radius,16.5},{0,0,1},
        {-0.996968676321994,-0.0778039744021556,0},radius,0,2.985827281201636).Value()},"曲線",false).IsNil()
        ||w.AddPlainWire({CurveSegment::MakeLine(corner,crossing).Value()},"曲線までの線",false).IsNil())return false;
    app::SelectionSet selected;selected.entityIds={w.ActiveWorkPlaneId()};w.Viewport().SetSelection(selected);
    w.SetWorkPlaneChooser([&](const WorkPlaneChoice&,const app::WorkPlaneFacts&){
        WorkPlaneChoice c;c.method=modeling::WorkPlaneMethod::OffsetFromPlane;c.offsetMm=16.5;return std::optional{c};});
    w.RunCommand("workplane.create");
    auto& v=w.Viewport();v.SetSnapSuppressed(false);
    const auto count=CountOfKind(w,domain::EntityKind::Wire);
    for(const auto direction:{ViewDirection::Top,ViewDirection::Isometric})for(const double scale:{100.0,3000.0,20000.0}){
        w.RunCommand("draw.line");v.SetViewDirection(direction);v.SetViewCenter(corner);v.SetVisibleWidthMm(35);
        if(!ClickWorld(v,corner))return false;
        v.SetViewCenter(crossing);v.SetVisibleWidthMm(v.width()/scale);
        const auto pixel=v.Mapping().Project(crossing);if(!pixel)return false;
        v.HoverAt({pixel->x,pixel->y});
        if(!Explain("a line meeting a curve remains an exact point snap while drawing at every zoom",v.Hover().snap
            &&(v.Hover().snap->kind==SnapKind::Endpoint||v.Hover().snap->kind==SnapKind::Intersection)
            &&geometry::Distance(v.Hover().snap->position,crossing)<1e-8))return false;
        if(!ClickWorld(v,crossing)||!Explain("click commits the actual curve intersection",
            CountOfKind(w,domain::EntityKind::Wire)==count+1
            &&geometry::Distance(w.Session().Scene().curves.back().segment.EndPoint(),crossing)<1e-8))return false;
        w.RunCommand("edit.undo");
    }
    return Explain("repeated intersection drawings leave source wires intact",CountOfKind(w,domain::EntityKind::Wire)==count);
}

bool SharedZoomedCrossings(V2MainWindow& w)
{
    const auto file=qEnvironmentVariable("KACHACAD_ZOOM_OWNER_FILE");
    if(file.isEmpty()||!w.OpenDocumentFile(file))return false;
    w.resize(1600,1000);QApplication::processEvents();
    auto& view=w.Viewport();
    constexpr double radius=20.06081081081081;
    const auto selectPlane=[&](const QString& name){
        for(int i=0;i<w.PlaneComboCount();++i)if(w.PlaneComboText(i)==name){w.SelectPlaneCombo(i);return true;}
        return false;
    };
    const auto original=CountOfKind(w,domain::EntityKind::Wire);
    for(const double x:{-12.0,18.0})for(const auto direction:{ViewDirection::Top,ViewDirection::Isometric}){
        if(!selectPlane(QStringLiteral("作業面4")))return false;
        const Vector3 hit{x,radius-std::sqrt(radius*radius-x*x),16.5};
        w.RunCommand("draw.line");view.SetViewDirection(direction);view.SetViewCenter(hit);
        view.SetVisibleWidthMm(view.width()/20000.0);view.SetSnapSuppressed(false);
        const auto pixel=view.Mapping().Project(hit);if(!pixel)return false;
        view.HoverAt({pixel->x,pixel->y});
        if(!Explain("shared model gives the actual circle crossing on its own drawing plane",view.Hover().snap
            &&(view.Hover().snap->kind==modeling::SnapKind::Endpoint||view.Hover().snap->kind==modeling::SnapKind::Intersection)
            &&geometry::Distance(view.Hover().snap->position,hit)<1e-8))return false;
        const auto output=qEnvironmentVariable("KACHACAD_CURVE_ZOOM_PROOF_DIR");
        if(!output.isEmpty()&&direction==ViewDirection::Top)w.grab().save(output+QString("/owner-%1.png").arg(x));
        if(!ClickWorld(view,hit))return false;
        view.SetSnapSuppressed(true);const bool clicked=ClickWorld(view,hit+Vector3{0.002,0.002,0});view.SetSnapSuppressed(false);
        if(!clicked||!Explain("shared model drawing starts at the exact selected crossing",
            CountOfKind(w,domain::EntityKind::Wire)==original+1
            &&geometry::Distance(w.Session().Scene().curves.back().segment.StartPoint(),hit)<1e-8))return false;
        w.RunCommand("edit.undo");
    }
    if(!selectPlane(QStringLiteral("作業面6")))return false;
    w.RunCommand("draw.line");view.SetViewDirection(ViewDirection::Top);
    const Vector3 offPlane{18,radius-std::sqrt(radius*radius-324),16.5};
    view.SetViewCenter(offPlane);view.SetVisibleWidthMm(view.width()/20000.0);
    const auto pixel=view.Mapping().Project(offPlane);if(!pixel)return false;view.HoverAt({pixel->x,pixel->y});
    return Explain("a crossing 3.5mm away from work plane 6 is not mislabeled as an in-plane intersection",
        std::abs(view.WorkPlane().origin.z-20)<1e-8
        &&(!view.Hover().snap||view.Hover().snap->kind!=modeling::SnapKind::Intersection
            ||geometry::Distance(view.Hover().snap->position,offPlane)>1e-6)
        &&CountOfKind(w,domain::EntityKind::Wire)==original);
}
}
std::vector<SelfTestCase> PointPickingCases(){std::vector<SelfTestCase> cases{
    {"HP-POINT-TARGET Tab makes actual drawing follow the chosen depth",OverlapDrawingPoint},
    {"HP-POINT-TARGET refused finish keeps the chosen curve and pending point",IncompleteFinishKeepsPointTarget},
    {"HP-POINT-TARGET Ctrl selection releases the previous point target",[](auto& w){return OverlapPointTransition(w,0);}},
    {"HP-POINT-TARGET Esc cancels drawing and releases the previous target",[](auto& w){return OverlapPointTransition(w,1);}},
    {"HP-POINT-TARGET Esc closes cursor input and releases the previous target",[](auto& w){return OverlapPointTransition(w,2);}},
    {"HP-POINT-TARGET reference Tab and right menu return the chosen UUID",OverlapReferencePoint},
    {"HP-POINT-PLANE draw from a finite curve plane intersection with source preservation",CurvePlanePointInput},
    {"HP-POINT-PLANE finite roots remain point sources while off-plane element selection stays restricted",CrossingPointSourceIsNotAnOffPlaneSelection},
    {"HP-POINT-PLANE source menu order follows actual root distance",WorkPlaneSourceOrderUsesActualRoots},
    {"HP-POINT-WP actual three-point role clicks preserve 3D source geometry",[](auto& w){return WorkPlaneRoles(w);}},
    {"HP-POINT-WP fractional curve endpoints keep full precision through plane creation",[](auto& w){return WorkPlaneRoles(w,true);}},
    {"HP-POINT-INPUT point requests override tools and reject undefined plane hits",GenericPointInputs},
    {"HP-POINT-SNAP 3D intersection midpoint hover and request transitions",SnapAndTransitions},
    {"HP-POINT-ARC draw from a nearby arc crossing on an offset work plane",ArcCrossingDrawing},
    {"HP-POINT-ZOOM circular arc ink matches analytic geometry at strong zoom",[](auto& w){return ZoomedCircularDisplay(w,false);}},
    {"HP-POINT-ZOOM circle and reversed partial arc stay coincident at strong zoom",[](auto& w){return ZoomedCircularDisplay(w,true);}},
    {"HP-POINT-ZOOM draw to an arc endpoint at high zoom and oblique view",ZoomedArcEndpoint},
    {"HP-POINT-TOOLS grid origin measurement and output origin actual clicks",OtherPointTools},
    {"HP-POINT-VISIBLE hidden geometry is ignored and standalone points are picked",PointVisibility},
    {"HP-POINT-EXTEND two selected wires accept the requested extension end",SelectedWireExtension}};
    if(!qEnvironmentVariableIsEmpty("KACHACAD_ZOOM_OWNER_FILE")) {
        cases.push_back({"HP-POINT-ZOOM-OWNER supplied current model exact and off-plane crossings",SharedZoomedCrossings});
        cases.push_back({"HP-POINT-TARGET-OWNER supplied original body arc explicit drawing and reference picks",SharedExplicitSource});
    }
    return cases;}
}
