#include "V2SelfTest.h"
#include "V2MainWindow.h"
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
namespace {
base::EntityId Line(V2MainWindow& w,double y) {
    w.SelectTool(modeling::DrawingTool::Line);
    for(double x:{-20.,20.}){const auto p=w.Viewport().Mapping().Project({x,y,0});if(!p)return {};
        w.Viewport().ClickAt(QPointF(p->x,p->y));}
    w.SelectTool(modeling::DrawingTool::Select);base::EntityId id;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Wire)id=e.id;
    return id;
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
}
std::vector<SelfTestCase> UsabilityCases(){return {{"HP-UX-01 cross-mode tool search",&Search},{"HP-UX-02 persistent local view picking snapping restore",&Isolation},{"HP-UX-03 solid selection focus",&ShapeFocus}};}
}
