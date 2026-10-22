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
std::vector<SelfTestCase> UsabilityCases(){auto cases=ToolContractCases(); const std::vector<SelfTestCase> existing{{"HP-UX-04 permanent grid and plane wire display controls",&QuickDisplay},{"HP-GPU display cache parity",&GpuParity},{"HP-ERGO-01 illustrated responsive controls",&Ergonomics},{"HP-ERGO-02 compact tool search",&CompactSearch},{"HP-UX-01 cross-mode tool search",&Search},{"HP-UX-02 persistent local view picking snapping restore",&Isolation},{"HP-UX-03 solid selection focus",&ShapeFocus}};cases.insert(cases.end(),existing.begin(),existing.end());return cases;}
}
