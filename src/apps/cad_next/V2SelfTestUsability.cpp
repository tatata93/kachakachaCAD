#include "V2SelfTest.h"
#include "V2QuickDisplayControls.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/app/SceneBuilder.h"
#include <QToolBar>
#include "V2Ribbon.h"
#include "V2ToolIcons.h"
#include <QToolButton>
#include <QGroupBox>
#include <QImage>
#include <QSize>
#include <QRect>
#include <QRegion>
#include "V2MainWindow.h"
#include "V2GpuRenderer.h"
#include <QPainter>
#include <QColor>
#include "kachakacha/view/SurfaceRaster.h"
#include "V2Viewport.h"
#include "V2DrawingDock.h"
#include "kachakacha/app/DirectWireEntry.h"
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QComboBox>
#include <set>
#include <cmath>
#include "V2OperationPanelHost.h"
#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QDir>
#include <QPixmap>
namespace kachakacha::v2::selftest {
std::vector<SelfTestCase> ToolContractCases();
namespace {
using T=modeling::DrawingTool;
QPointF PlanePixel(V2Viewport& v,geometry::Vector3 p) {
    const auto pixel=v.Mapping().Project(p);return pixel?QPointF(pixel->x,pixel->y):QPointF(-100,-100);
}
void PlaneClick(V2Viewport& v,geometry::Vector3 p,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    const auto at=PlanePixel(v,p);
    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
        QMouseEvent event(type,at,QPointF(v.mapToGlobal(at.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
        QApplication::sendEvent(&v,&event);
    }
}
void PlaneKey(V2Viewport& v,int key) {
    if(!v.window()->isActiveWindow()){v.window()->activateWindow();QApplication::processEvents();}
    v.setFocus();QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier);QApplication::sendEvent(&v,&event);
}
bool PlaneSetup(V2MainWindow& w) {
    w.RunCommand("file.new");w.resize(1440,900);QApplication::processEvents();
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(180);v.SetSnapSuppressed(true);
    auto* only=w.findChild<QToolButton*>("quickPlaneWiresOnly");if(!only)return false;
    only->setChecked(true);return only->isChecked();
}
std::set<base::EntityId> PlaneEntities(V2MainWindow& w) {
    std::set<base::EntityId> ids;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::Wire||e.kind==domain::EntityKind::Point)ids.insert(e.id);
    return ids;
}
bool PlaneNewVisible(V2MainWindow& w,const std::set<base::EntityId>& before) {
    const auto after=PlaneEntities(w);bool created=false;
    for(const auto id:after)if(!before.contains(id)){
        created=true;if(!Explain("new in-plane entity is immediately visible",w.Viewport().EntityShown(id)))return false;
    }
    return Explain("drawing actually created a document entity",created);
}
void PlaneShot(V2MainWindow& w,const char* name) {
    const auto root=qEnvironmentVariable("KACHACAD_PLANE_DRAW_SHOTS");if(root.isEmpty())return;
    QDir dir(root);dir.mkpath(".");w.grab().save(dir.filePath(QString::fromUtf8(name)+".png"));
}
bool PlaneMouseDrawing(V2MainWindow& w) {
    if(!PlaneSetup(w))return false;auto& v=w.Viewport();
    const auto draw=[&](T tool,std::vector<geometry::Vector3> points,bool finish=false){
        const auto before=PlaneEntities(w);w.SelectTool(tool);
        for(const auto& p:points)PlaneClick(v,p);
        if(finish)PlaneKey(v,Qt::Key_Return);
        return PlaneNewVisible(w,before);
    };
    if(!draw(T::Line,{{-60,-45,0},{-35,-45,0}}))return false;
    // The real rendered line must be present, not just a permissive visibility flag.
    v.CompletePresentationForTest();const auto shot=v.grab().toImage();const auto mid=PlanePixel(v,{-47.5,-45,0});
    const double scale=double(shot.width())/v.width();bool ink=false;
    for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){
        const auto pixel=shot.pixelColor(qRound(mid.x()*scale)+dx,qRound(mid.y()*scale)+dy);
        ink=ink||(pixel.red()<90&&pixel.green()<90&&pixel.blue()<90);
    }
    if(!Explain("new line is drawn in the real viewport",ink))return false;
    if(!draw(T::Rectangle,{{-60,-30,0},{-35,-20,0}})
        ||!draw(T::Circle,{{-48,-5,0},{-42,-5,0}})
        ||!draw(T::Arc,{{-60,15,0},{-50,24,0},{-40,15,0}})
        ||!draw(T::Bezier,{{-60,40,0},{-55,45,0},{-45,35,0},{-40,40,0}})
        ||!draw(T::Spline,{{-25,-45,0},{-15,-40,0},{-10,-20,0},{0,-30,0}},true)
        ||!draw(T::Point,{{-10,-5,0}}))return false;
    PlaneShot(w,"mouse-primitives");return true;
}
bool PlaneNumericAndContinuous(V2MainWindow& w) {
    if(!PlaneSetup(w))return false;auto& v=w.Viewport();w.SelectTool(T::Line);
    PlaneClick(v,{-50,-30,0});v.HoverAt(PlanePixel(v,{-20,-30,0}));
    if(!v.TypeIntoCursorField("12.5"))return false;
    auto before=PlaneEntities(w);PlaneKey(v,Qt::Key_Return);
    if(!PlaneNewVisible(w,before))return false;
    const auto& curve=w.Session().Scene().curves.back().segment;
    if(!Explain("cursor numeric line uses its actual requested length",std::abs((curve.EndPoint()-curve.StartPoint()).Length()-12.5)<1e-8))return false;
    for(double y:{-10.,10.,30.}){
        before=PlaneEntities(w);PlaneClick(v,{-50,y,0});PlaneClick(v,{-20,y,0});
        if(!PlaneNewVisible(w,before))return false;
    }
    before=PlaneEntities(w);w.SelectTool(T::Polyline);
    PlaneClick(v,{-10,-30,0});PlaneClick(v,{0,-20,0});PlaneClick(v,{10,-30,0});PlaneKey(v,Qt::Key_Return);
    return PlaneNewVisible(w,before);
}
bool PlaneConstructionPoints(V2MainWindow& w) {
    if(!PlaneSetup(w))return false;auto& v=w.Viewport();modeling::ToolSettings settings;
    settings.construction=true;settings.keepPoints=true;w.DrawingDock().SetSettings(settings);w.SelectTool(T::Line);
    auto before=PlaneEntities(w);PlaneClick(v,{-50,-20,0});PlaneClick(v,{-20,-20,0});
    if(!PlaneNewVisible(w,before)||PlaneEntities(w).size()!=3)return false;
    before=PlaneEntities(w);w.SelectTool(T::Bezier);
    for(const auto p:std::vector<geometry::Vector3>{{-50,10,0},{-40,25,0},{-30,-5,0},{-20,10,0}})PlaneClick(v,p);
    return PlaneNewVisible(w,before);
}
void PlaneFrame(V2MainWindow& w,const modeling::WorkPlaneFrame& plane) {
    w.Viewport().SetWorkPlane(plane);auto scene=w.Session().Scene();
    scene.grid.origin=plane.origin;scene.grid.uDirection=plane.uAxis;scene.grid.vDirection=plane.vAxis;
    w.Session().SetScene(std::move(scene));
}
bool PlaneTiltedDrawing(V2MainWindow& w) {
    if(!PlaneSetup(w))return false;auto& v=w.Viewport();const auto top=v.WorkPlane();
    for(bool tilted:{false,true}){
        auto plane=top;plane.origin={0,0,tilted?13.:7.};
        if(tilted){const double q=std::sqrt(.5);plane.normal={q,0,q};plane.uAxis={q,0,-q};plane.vAxis={0,1,0};}
        PlaneFrame(w,plane);const auto before=PlaneEntities(w);w.SelectTool(T::Line);
        PlaneClick(v,plane.origin+plane.uAxis*-45+plane.vAxis*-20);
        PlaneClick(v,plane.origin+plane.uAxis*-20+plane.vAxis*-20);
        if(!PlaneNewVisible(w,before))return false;
        v.SetSnapSuppressed(false);const auto c=w.Session().Scene().curves.back();
        const auto p=v.Mapping().Project(c.segment.StartPoint());if(!p)return false;
        const auto snap=w.Session().PeekHover(*p).snap;
        if(!Explain("new tilted-plane endpoint is an immediate snap target",snap&&snap->entityId==c.entityId))return false;
        v.SelectAt({p->x,p->y},Qt::NoModifier);
        if(!Explain("new tilted wire is immediately selectable",app::IsSelected(v.Selection(),c.entityId)))return false;
        v.SetSnapSuppressed(true);PlaneFrame(w,top);
        if(!Explain("true off-plane wire remains hidden",!v.EntityShown(c.entityId)))return false;
    }
    return true;
}
bool PlaneDrawingHistory(V2MainWindow& w) {
    if(!PlaneSetup(w))return false;auto& v=w.Viewport();w.SelectTool(T::Line);
    PlaneClick(v,{-50,-20,0});PlaneClick(v,{-20,-20,0});
    const auto id=w.Session().Scene().curves.back().entityId;if(!v.EntityShown(id))return false;
    PlaneClick(v,{-35,-20,0},Qt::ControlModifier);PlaneKey(v,Qt::Key_Delete);
    if(w.Session().GetDocument().FindEntity(id))return false;
    w.RunCommand("edit.undo");if(!v.EntityShown(id)||!w.Session().GetDocument().FindEntity(id))return false;
    w.RunCommand("edit.redo");if(w.Session().GetDocument().FindEntity(id))return false;
    w.RunCommand("edit.undo");
    if(!w.Session().GetDocument().Run(document::SetVisibilityCommand({id},domain::Visibility::Hidden)).committed)return false;
    base::RandomIdGenerator ids;w.Session().SetScene(app::RebuildSceneKeepingView(w.Session().Scene(),w.Session().GetDocument().Snapshot(),ids));
    auto* only=w.findChild<QToolButton*>("quickPlaneWiresOnly");only->click();only->click();
    if(!Explain("filter toggle does not unhide document-hidden wire",w.Session().GetDocument().FindEntity(id)->visibility==domain::Visibility::Hidden))return false;
    w.RunCommand("edit.undo");if(!v.EntityShown(id))return false;
    if(!w.SaveAndReopen("plane-wire-drawing.kcd2")||!only->isChecked()||!v.EntityShown(id))return false;
    v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(180);w.SelectTool(T::Line);
    const auto before=PlaneEntities(w);PlaneClick(v,{-50,10,0});PlaneClick(v,{-20,10,0});
    return PlaneNewVisible(w,before);
}
bool PlaneDirectAndSpatial(V2MainWindow& w) {
    if(!PlaneSetup(w))return false;auto& v=w.Viewport();app::DirectWireRequest request;
    request.points={{-50,-20,0},{-20,-20,0}};auto before=PlaneEntities(w);
    w.DrawingDock().SetDirectWire(request,"平面内数値線");w.DrawingDock().PressCreateWire();
    if(!PlaneNewVisible(w,before))return false;
    request.kind=app::DirectWireKind::SpatialLine;request.points={{-50,10,5},{-20,10,5}};
    before=PlaneEntities(w);
    w.DrawingDock().SetDirectWire(request,"面外数値線");w.DrawingDock().PressCreateWire();
    if(!Explain("planar drawing rejects off-plane coordinates",PlaneEntities(w)==before))return false;
    auto* scope=w.findChild<QComboBox*>("drawingScope");if(!scope)return false;scope->setCurrentIndex(1);
    w.DrawingDock().SetDirectWire(request,"面外数値線");w.DrawingDock().PressCreateWire();
    if(!Explain("3D drawing actually creates the off-plane wire",PlaneEntities(w).size()==before.size()+1))return false;
    const auto id=w.Session().Scene().curves.back().entityId;
    if(!Explain("spatial wire truly outside the plane is excluded",!v.EntityShown(id)))return false;
    auto* only=w.findChild<QToolButton*>("quickPlaneWiresOnly");const auto revision=w.Session().GetDocument().Revision();
    only->click();if(!v.EntityShown(id))return false;only->click();
    return Explain("display toggle remains outside document history",!v.EntityShown(id)&&w.Session().GetDocument().Revision()==revision);
}
base::EntityId Line(V2MainWindow& w,double y) {
    w.SelectTool(modeling::DrawingTool::Line);
    for(double x:{-20.,20.}){const auto p=w.Viewport().Mapping().Project({x,y,0});if(!p)return {};
        w.Viewport().ClickAt(QPointF(p->x,p->y));}
    w.SelectTool(modeling::DrawingTool::Select);base::EntityId id;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Wire)id=e.id;
    return id;
}
bool QuickDisplay(V2MainWindow& w) {
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);
    base::RandomIdGenerator ids;
    const auto add=[&](std::vector<geometry::CurveSegment> curves,const char* name){
        domain::Feature f;f.id=ids.NextTyped<base::IdKind::Feature>();f.type=domain::FeatureType::CreateWire;
        domain::CreateWireDefinition d;d.segments=std::move(curves);for(const auto& c:d.segments){(void)c;d.segmentIds.push_back(ids.NextTyped<base::IdKind::Segment>());}f.definition=d;
        domain::Entity e;e.id=ids.NextTyped<base::IdKind::Entity>();e.kind=domain::EntityKind::Wire;e.createdBy=f.id;e.displayName=name;
        f.outputs={{"wire",e.id,e.kind}};
        if(!w.Session().GetDocument().Run(document::AddFeatureCommand(f,{e},name)).committed)return base::EntityId{};
        return e.id;
    };
    const auto a=add({geometry::CurveSegment::MakeLine({-20,10,0},{20,10,0}).Value()},"平面内");
    const auto b=add({geometry::CurveSegment::MakeCircle({0,40,5},{0,0,1},{1,0,0},5).Value()},"平行別面");
    const auto c=add({geometry::CurveSegment::MakeCubicBezier({{-20,-20,0},{-10,-20,5},{10,-20,-5},{20,-20,0}}).Value()},"途中で面外");
    const auto d=add({geometry::CurveSegment::MakeLine({0,-20,0},{0,20,0}).Value()},"複数面共通");
    const auto mixed=add({geometry::CurveSegment::MakeLine({-10,0,0},{10,0,0}).Value(),geometry::CurveSegment::MakeLine({10,0,0},{10,0,5}).Value()},"立体ワイヤー");
    if(a.IsNil()||b.IsNil()||c.IsNil()||d.IsNil()||mixed.IsNil())return false;
    auto* visible=w.findChild<QToolButton*>("quickGridVisible");
    auto* grid=w.findChild<QToolButton*>("quickGridOnTop");auto* only=w.findChild<QToolButton*>("quickPlaneWiresOnly");
    auto* check=w.findChild<QCheckBox*>("gridOnTop");if(!grid||!only||!check||!visible)return false;
    w.Session().SetScene(app::RebuildSceneKeepingView(w.Session().Scene(),w.Session().GetDocument().Snapshot(),ids));
    const auto revision=w.Session().GetDocument().Revision();
    for(auto theme:{UiTheme::Normal,UiTheme::Windows95})for(int width:{1000,1280}){
        w.ApplyTheme(theme);w.resize(width,800);
        for(auto mode:app::AllUiModes()){w.SetMode(mode);QApplication::processEvents();
            if(!Explain("quick display buttons remain visible",grid->isVisible()&&only->isVisible()&&visible->isVisible()&&visible->visibleRegion().boundingRect()==visible->rect()&&grid->visibleRegion().boundingRect()==grid->rect()&&only->visibleRegion().boundingRect()==only->rect())){w.grab().save("out/quick-display-overflow.png");return false;}}
    }
    grid->click();if(!Explain("toolbar grid synchronizes dock",!check->isChecked()))return false;
    check->click();if(!Explain("dock grid synchronizes toolbar",grid->isChecked()))return false;
    w.SetMode(app::UiMode::Drawing);only->click();
    if(!Explain("geometric plane visibility excludes off-plane and mixed wire",v.EntityShown(a)&&v.EntityShown(d)&&!v.EntityShown(b)&&!v.EntityShown(c)&&!v.EntityShown(mixed)))return false;
    const auto point=v.Mapping().Project({0,45,5});if(!point)return false;
    const auto snap=w.Session().PeekHover(*point).snap;
    if(!Explain("hidden wire is not a snap target",!snap||snap->entityId!=b))return false;
    v.SelectAt({point->x,point->y},Qt::NoModifier);if(!Explain("hidden wire cannot be selected",!app::IsSelected(v.Selection(),b)))return false;
    for(const auto& entity:w.Session().GetDocument().Snapshot().entities)if(entity.kind!=domain::EntityKind::Wire&&entity.kind!=domain::EntityKind::Point&&v.EntityShown(entity.id))return false;
    auto plane=v.WorkPlane();plane.origin={0,0,5};v.SetWorkPlane(plane);
    if(!Explain("offset plane updates filter",v.EntityShown(b)&&!v.EntityShown(a)))return false;
    plane.origin={0,0,0};plane.normal={1,0,0};plane.uAxis={0,1,0};plane.vAxis={0,0,1};v.SetWorkPlane(plane);
    if(!Explain("rotated plane uses geometric containment",v.EntityShown(d)&&!v.EntityShown(a)&&!v.EntityShown(b)))return false;
    plane.normal={0,0,1};plane.uAxis={1,0,0};plane.vAxis={0,1,0};v.SetWorkPlane(plane);
    if(!w.Session().GetDocument().Run(document::SetVisibilityCommand({d},domain::Visibility::Hidden)).committed)return false;
    w.Session().SetScene(app::RebuildSceneKeepingView(w.Session().Scene(),w.Session().GetDocument().Snapshot(),ids));
    only->click();
    if(!Explain("toggle restores visible entities without changing hidden flag",v.EntityShown(a)&&v.EntityShown(b)&&w.Session().GetDocument().FindEntity(d)->visibility==domain::Visibility::Hidden))return false;
    const bool unchanged=w.Session().GetDocument().Revision()==revision+1;
    if(!w.Session().Undo())return false;only->click();if(!Explain("scene replacement updates cache after Undo",v.EntityShown(d)))return false;
    if(!w.Session().Redo())return false;only->click();
    if(!qEnvironmentVariable("KACHACAD_QUICK_DISPLAY_SHOTS").isEmpty()){
        QDir dir(qEnvironmentVariable("KACHACAD_QUICK_DISPLAY_SHOTS"));dir.mkpath(".");only->click();w.grab().save(dir.filePath("plane-wires.png"));only->click();w.grab().save(dir.filePath("all-geometry.png"));}
    return Explain("display controls never enter document undo history",unchanged);
}
bool Search(V2MainWindow& w) {
    w.Viewport().SetViewDirection(ViewDirection::Top);const auto id=Line(w,10);if(id.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{id}});const auto rev=w.Session().GetDocument().Revision();
    w.RunCommand("tools.search");QApplication::processEvents();
    auto* search=w.findChild<QLineEdit*>(QStringLiteral("toolSearch"));if(!search)return false;
    search->setText(QStringLiteral("押し出し"));QApplication::processEvents();
    QPushButton* target=nullptr;
    for(auto* button:w.findChildren<QPushButton*>(QStringLiteral("toolChoice")))
        if(button->isVisible()&&button->property("commandId").toString()==QStringLiteral("part.extrude"))target=button;
    w.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-tool-search.png"));
    if(!Explain("cross-mode search exposes extrude",target!=nullptr))return false;
    if(!Explain("search extrude is enabled",target->isEnabled()))return false;
    if(!Explain("search extrude has guidance",!target->toolTip().isEmpty()))return false;
    target->click();
    if(!Explain("search starts correct mode without changing model",w.Mode()==app::UiMode::Part&&w.Session().GetDocument().Revision()==rev))return false;
    return Explain("search retains selected input",app::IsSelected(w.Viewport().Selection(),id));
}
bool Isolation(V2MainWindow& w) {
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(200);
    const auto a=Line(w,10),b=Line(w,50);if(a.IsNil()||b.IsNil()||a==b)return false;
    v.SetSelection(app::SelectionSet{{a}});const auto revision=w.Session().GetDocument().Revision();
    w.RunCommand("view.isolate");v.SetSelection({});
    if(!Explain("local view stays after deselection",v.IsolationActive()&&v.EntityShown(a)&&!v.EntityShown(b)))return false;
    const auto p=v.Mapping().Project({-20,50,0});if(!p)return false;
    const auto snap=w.Session().PeekHover(*p).snap;
    if(!Explain("hidden line does not snap",!snap||snap->entityId!=b))return false;
    v.SelectAt(QPointF(p->x,p->y),Qt::NoModifier);
    if(!Explain("hidden line cannot be picked",!app::IsSelected(v.Selection(),b)))return false;
    v.SetSelection(app::SelectionSet{{a}});w.RunCommand("view.fit_selection");
    if(!Explain("selected fit excludes distant line",v.VisibleWidthMm()<70))return false;
    auto* restore=v.findChild<QPushButton*>(QStringLiteral("exitIsolation"));if(!restore||!restore->isVisible())return false;
    restore->click();
    return Explain("local restore is non-destructive",!v.IsolationActive()&&v.EntityShown(b)&&w.Session().GetDocument().Revision()==revision);
}
bool ShapeFocus(V2MainWindow& w) {
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);
    const auto a=Line(w,10),b=Line(w,50);if(a.IsNil()||b.IsNil())return false;
    V2Viewport::ShapeView shape;shape.entityId=b;
    modeling::MeshTriangle triangle;triangle.points={geometry::Vector3{100,0,0},geometry::Vector3{110,0,0},geometry::Vector3{100,10,0}};
    triangle.normal={0,0,1};shape.mesh.triangles.push_back(triangle);v.SetShapeViews({shape});
    v.SetSelection(app::SelectionSet{{b}});v.FitToDocument(true);
    if(!Explain("fit includes solid mesh bounds",v.VisibleWidthMm()>100))return false;
    auto display=v.DisplaySettingsNow();display.selectionOnly=true;v.SetDisplaySettings(display);
    v.SetSelection(app::SelectionSet{{a}});
    if(!Explain("selection-only filters shape entity",!v.EntityShown(b)&&v.EntityShown(a)))return false;
    v.SetSelection(app::SelectionSet{{b}});return Explain("selection-only follows new entity",v.EntityShown(b)&&!v.EntityShown(a));
}
bool Ergonomics(V2MainWindow& w) {
    bool ok=true;
    for(auto theme:{UiTheme::Normal,UiTheme::Windows95}){
        w.ApplyTheme(theme);
        for(int width:{1000,1280}){
            w.resize(width,800);QApplication::processEvents();
            for(auto mode:app::AllUiModes()){
                w.SetMode(mode);auto& ribbon=w.Ribbon();
                for(int category=0;category<ribbon.CategoryCount();++category){
                    ribbon.ShowCategory(category);QApplication::processEvents();
                    if(width==1000&&mode==app::UiMode::Drawing&&category==2)w.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-ergonomics-wrap-%1.png").arg(theme==UiTheme::Normal?"normal":"win95"));
                    ok=Explain("ribbon tools wrap inside width",ribbon.ToolsFitInWidth())&&ok;
                    for(auto* button:ribbon.findChildren<QToolButton*>(QStringLiteral("ribbonTool")))if(button->isVisible())
                        ok=Explain("illustration and label with usable target",!button->icon().isNull()&&!button->text().isEmpty()&&button->height()>=32&&button->visibleRegion().boundingRect()==button->rect())&&ok;
                }
            }
        }
        w.SetMode(app::UiMode::Drawing);w.Ribbon().ShowCategory(0);w.SelectTool(modeling::DrawingTool::Circle);QApplication::processEvents();
        for(auto* button:w.findChildren<QToolButton*>(QStringLiteral("drawingMethodCard")))if(button->isVisible())
            ok=Explain("drawing method illustrated",!button->icon().isNull()&&button->height()>=52)&&ok;
        w.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-ergonomics-%1.png").arg(theme==UiTheme::Normal?"normal":"win95"));
    }
    w.ApplyTheme(UiTheme::Normal);
    const auto circle=V2ToolIcon("draw.circle").pixmap(QSize(48,48)).toImage();
    const auto line=V2ToolIcon("draw.line").pixmap(QSize(48,48)).toImage();
    return Explain("line and circle have distinct illustrations",circle!=line)&&ok;
}
bool CompactSearch(V2MainWindow& w){
    w.RunCommand("tools.search");QApplication::processEvents();
    auto* search=w.findChild<QLineEdit*>(QStringLiteral("toolSearch"));if(!search)return false;
    search->setText(QStringLiteral("part.extrude"));QApplication::processEvents();
    int found=0;
    for(auto* button:w.findChildren<QPushButton*>(QStringLiteral("toolChoice")))if(button->isVisible()){
        ++found;auto* group=dynamic_cast<QGroupBox*>(button->parentWidget());
        if(!Explain("single search result has compact category",group&&group->height()<150))return false;
    }
    return Explain("precise command search found in its categories",found>=1);
}

bool GpuParity(V2MainWindow&) {
    V2GpuRenderer renderer;
    modeling::ShapeMesh mesh;
    modeling::MeshTriangle t;
    t.points={geometry::Vector3{-10,-10,0},geometry::Vector3{10,-10,0},geometry::Vector3{0,10,0}};
    t.normal={0,0,1};mesh.triangles.push_back(t);
    auto mapping=geometry::MakeOrthographicMapping({0,0,0},{0,0,-1},{0,1,0},40,100,100);
    std::vector<V2GpuRenderer::Item> items{{&mesh,0x91bed9,0xc9dfeb,true,false}};
    const auto first=renderer.Render(items,1,mapping,{0,0,-1},100,100);
    if(first.isNull()) { Note("GPU context unavailable: software path remains active; GPU parity not exercised");return true; }
    view::SurfaceRaster cpu(100,100);cpu.Draw(t,mapping,{0,0,-1},false,items[0].fill);
    const auto expected=cpu.Pixels()[50*100+50], actual=first.pixel(50,50);
    for(int shift:{0,8,16})if(!Explain("GPU normal lighting agrees with CPU",std::abs(int((actual>>shift)&255)-int((expected>>shift)&255))<=2))return false;
    if(!Explain("same frame cache",first==renderer.Render(items,1,mapping,{0,0,-1},100,100)))return false;
    items[0].fill=0xff0000;
    const auto selected=renderer.Render(items,1,mapping,{0,0,-1},100,100);
    if(!Explain("color change invalidates frame",selected!=first))return false;
    items[0].visible=false;
    if(!Explain("visibility change clears surface",renderer.Render(items,1,mapping,{0,0,-1},100,100).pixel(50,50)==0))return false;
    items[0].visible=true;
    for(auto& point:mesh.triangles[0].points)point.x+=100;
    if(!Explain("mesh replacement invalidates GPU buffers",renderer.Render(items,2,mapping,{0,0,-1},100,100).pixel(50,50)==0))return false;
    mapping=geometry::MakeOrthographicMapping({100,0,0},{0,0,-1},{0,1,0},40,100,100);
    if(!Explain("camera change restores visible geometry",renderer.Render(items,2,mapping,{0,0,-1},100,100).pixel(50,50)!=0))return false;
    for(auto& point:mesh.triangles[0].points)point.z=10000;
    if(!Explain("orthographic zoom does not clip distant surfaces",
        renderer.Render(items,3,mapping,{0,0,-1},100,100).pixel(50,50)!=0))return false;
    QImage composed(100,100,QImage::Format_ARGB32);composed.fill(Qt::transparent);
    QPainter destination(&composed);
    const bool painted=renderer.PaintFrame(destination,100,100,[&](QPainter& painter){
        painter.fillRect(0,0,100,100,QColor(10,20,30));
        painter.beginNativePainting();renderer.Render(items,3,mapping,{0,0,-1},100,100);painter.endNativePainting();
        painter.fillRect(45,45,10,10,QColor(0,255,0));
    });
    destination.end();
    return Explain("GPU frame preserves background and painter overlays",painted
        && composed.pixelColor(2,2)==QColor(10,20,30) && composed.pixelColor(50,50)==QColor(0,255,0));
}

}
std::vector<SelfTestCase> UsabilityCases(){auto cases=ToolContractCases(); const std::vector<SelfTestCase> existing{
    {"HP-PLANE-DRAW-01 mouse primitives visible without toggling filter",PlaneMouseDrawing},
    {"HP-PLANE-DRAW-02 cursor numeric and continuous drawing",PlaneNumericAndContinuous},
    {"HP-PLANE-DRAW-03 construction control polygon and kept points",PlaneConstructionPoints},
    {"HP-PLANE-DRAW-04 offset and tilted planes visibility picking snapping",PlaneTiltedDrawing},
    {"HP-PLANE-DRAW-05 delete Undo Redo hidden state save reopen",PlaneDrawingHistory},
    {"HP-PLANE-DRAW-06 direct numeric entry and actual off-plane exclusion",PlaneDirectAndSpatial},
    {"HP-UX-04 permanent grid and plane wire display controls",&QuickDisplay},{"HP-GPU display cache parity",&GpuParity},{"HP-ERGO-01 illustrated responsive controls",&Ergonomics},{"HP-ERGO-02 compact tool search",&CompactSearch},{"HP-UX-01 cross-mode tool search",&Search},{"HP-UX-02 persistent local view picking snapping restore",&Isolation},{"HP-UX-03 solid selection focus",&ShapeFocus}};cases.insert(cases.end(),existing.begin(),existing.end());return cases;}
}
