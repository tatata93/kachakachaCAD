#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ToolSelection.h"
#include "V2EntityTree.h"
#include "V2QuickDisplayControls.h"
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QEvent>
#include <QIODevice>
#include <QPointF>
#include <QString>
#include <QWidget>
#include <QDir>
#include <QPixmap>
#include <QPushButton>
#include <QKeyEvent>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace kachakacha::v2::selftest {
namespace {
using namespace kachakacha::v2;
QPointF At(V2Viewport& v,geometry::Vector3 p){auto q=v.Mapping().Project(p);return q?QPointF(q->x,q->y):QPointF();}
void Shot(V2MainWindow& w,const char* name){const auto path=qEnvironmentVariable("KACHACAD_DRAWING_GUIDE_DIR");if(path.isEmpty())return;
    QDir folder(path);folder.mkpath(".");QApplication::processEvents();w.grab().save(folder.filePath(QString::fromUtf8(name)+".png"));}
void Click(V2Viewport& v,geometry::Vector3 p,Qt::KeyboardModifiers m=Qt::NoModifier){
    auto q=At(v,p);QMouseEvent a(QEvent::MouseButtonPress,q,v.mapToGlobal(q.toPoint()),Qt::LeftButton,Qt::LeftButton,m);
    QMouseEvent b(QEvent::MouseButtonRelease,q,v.mapToGlobal(q.toPoint()),Qt::LeftButton,Qt::NoButton,m);
    QApplication::sendEvent(&v,&a);QApplication::sendEvent(&v,&b);
}
std::vector<base::EntityId> Prepare(V2MainWindow& w,int count=2){
    w.RunCommand("file.new");auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(200);
    if(qEnvironmentVariableIsSet("KACHACAD_DRAWING_GUIDE_DIR")){w.resize(1500,950);QApplication::processEvents();}
    v.SetSnapSuppressed(true);std::vector<base::EntityId> ids;
    for(int i=0;i<count;++i){auto line=geometry::CurveSegment::MakeLine({-30.,-10.+10*i,0},{-10.,-10.+10*i,0});
        auto made=w.Session().AddWire({line.Value()},false,"copy target");if(made.committed)ids.push_back(made.createdEntityIds.front());}
    if(!w.SaveAndReopen(QStringLiteral("copy-fixture.kcd2")))return {};
    v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(200);v.SetSnapSuppressed(true);return ids;
}
bool CopyTargets(V2MainWindow& w){
    auto ids=Prepare(w);if(ids.size()!=2)return false;auto& v=w.Viewport();w.SelectTool(modeling::DrawingTool::Copy);
    Click(v,{-20,-10,0});Click(v,{-20,0,0},Qt::ControlModifier);
    if(!Explain("Copy Ctrl adds targets without temporary delete mode or basepoint",v.Selection().entityIds.size()==2
        &&!V2ToolSelection::Active(v)&&w.Session().PlacedPointCount()==0))return false;
    Click(v,{-20,0,0},Qt::ControlModifier);
    return Explain("Copy Ctrl toggles only the clicked target",v.Selection().entityIds.size()==1&&v.Selection().entityIds.front()==ids.front());
}
bool CopyGhost(V2MainWindow& w){
    auto ids=Prepare(w);auto& v=w.Viewport();v.SetSelection(app::SelectionSet{ids});w.SelectTool(modeling::DrawingTool::Copy);
    Click(v,{0,-20,0});const auto revision=w.Session().GetDocument().Revision();
    for(const auto p:std::vector<geometry::Vector3>{{10,-5,0},{20,-5,0}}){
        v.HoverAt(At(v,p));
        if(!Explain("basepoint Copy ghost follows pointer without document edits",v.HasPreview()&&v.Hover().preview.size()==2
            &&w.Session().GetDocument().Revision()==revision))return false;
        if(!Explain("ghost uses the exact translated whole wire",geometry::Distance(v.Hover().preview.front().StartPoint(),
            geometry::Vector3{-30,-10,0}+p-geometry::Vector3{0,-20,0})<1e-8))return false;
    }
    Shot(w,"copy-cursor-ghost");Click(v,{20,-5,0});auto count=[&]{return w.Session().Scene().curves.size();};
    if(!Explain("one copy creates exactly two new wires",count()==4))return false;
    w.RunCommand("edit.undo");if(!Explain("one Undo removes the whole multi-copy",count()==2))return false;
    w.RunCommand("edit.redo");return Explain("Redo restores the complete multi-copy",count()==4);
}
void Key(QWidget& widget,int key,const QString& text={}){
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier,text),release(QEvent::KeyRelease,key,Qt::NoModifier,text);
    QApplication::sendEvent(&widget,&press);QApplication::sendEvent(&widget,&release);
    QEventLoop settle;QTimer::singleShot(15,&settle,&QEventLoop::quit);settle.exec();
}
void Mouse(QWidget& widget,QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons held){
    QMouseEvent event(type,p,widget.mapToGlobal(p.toPoint()),button,held,Qt::NoModifier);QApplication::sendEvent(&widget,&event);
}
bool CopyRoleBox(V2MainWindow& w){
    auto ids=Prepare(w);auto& v=w.Viewport();w.SelectTool(modeling::DrawingTool::Copy);QApplication::processEvents();
    auto* roles=w.findChild<QWidget*>("transformSelectionRoles");auto* arm=roles?roles->findChild<QPushButton*>("selectionRolePick0"):nullptr;
    auto* done=roles?roles->findChild<QPushButton*>("transformTargetsDone"):nullptr;if(!arm||!done)return false;
    arm->click();auto a=At(v,{-35,-15,0}),b=At(v,{-5,5,0});
    Mouse(v,QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton);Mouse(v,QEvent::MouseMove,b,Qt::NoButton,Qt::LeftButton);
    Mouse(v,QEvent::MouseButtonRelease,b,Qt::LeftButton,Qt::NoButton);
    if(!Explain("role accepts a rectangle of two wires without basepoints",v.Selection().entityIds.size()==2&&w.Session().PlacedPointCount()==0))return false;
    QApplication::processEvents();
    if(!Explain("target controls retain usable button height",arm->height()>=arm->minimumSizeHint().height()&&done->height()>=done->minimumSizeHint().height()))return false;
    Shot(w,"copy-target-selection");
    Mouse(v,QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton);Mouse(v,QEvent::MouseMove,b,Qt::NoButton,Qt::LeftButton);
    Key(v,Qt::Key_Escape);Mouse(v,QEvent::MouseButtonRelease,b,Qt::LeftButton,Qt::NoButton);
    if(!Explain("Escape cancels an in-flight target rectangle",!v.property("transformTargetPicking").toBool()&&v.Selection().entityIds.size()==2&&w.Session().PlacedPointCount()==0))return false;
    arm->click();w.SelectTool(modeling::DrawingTool::Move);Click(v,{0,-20,0});
    return Explain("changing transform tools ends target-pick mode",!v.property("transformTargetPicking").toBool()&&w.Session().PlacedPointCount()==1);
}
bool CopyNumeric(V2MainWindow& w){
    auto ids=Prepare(w);auto& v=w.Viewport();v.SetSelection(app::SelectionSet{ids});w.SelectTool(modeling::DrawingTool::Copy);
    Click(v,{0,-20,0});v.HoverAt(At(v,{10,-20,0}));
    if(!Explain("Copy offers a distance field after the basepoint",v.CursorPanel().active))return false;
    Key(v,Qt::Key_2,"2");Key(v,Qt::Key_5,"5");Key(v,Qt::Key_Tab);
    if(!Explain("numeric ghost updates without pointer motion",v.Hover().preview.size()==2
        &&geometry::Distance(v.Hover().preview.front().StartPoint(),{-5,-10,0})<1e-7))return false;
    Key(v,Qt::Key_Return);
    bool found=false;for(const auto& curve:w.Session().Scene().curves)if(geometry::Distance(curve.segment.StartPoint(),{-5,-10,0})<1e-7)found=true;
    if(!Explain("Enter creates the same geometry shown by the ghost",found&&w.Session().Scene().curves.size()==4))return false;
    w.SelectTool(modeling::DrawingTool::Line);return Explain("tool change discards the Copy ghost",!v.HasPreview()&&!v.CursorPanel().active);
}
bool DrawingAppendRows(V2MainWindow& w){
    auto ids=Prepare(w);auto& v=w.Viewport();auto* tree=w.EntityTree();auto* original=w.ItemOfEntity(ids.front());auto* category=original->parent();
    category->setExpanded(false);const auto builds=w.property("explorerBuilds").toInt();
    w.SelectTool(modeling::DrawingTool::Line);Click(v,{10,-30,0});Click(v,{30,-30,0});
    const auto id=w.Session().GetDocument().Snapshot().entities.back().id;auto* row=w.ItemOfEntity(id);
    if(!Explain("direct drawing preserves row identity and collapsed categories",row&&original==w.ItemOfEntity(ids.front())
        &&!category->isExpanded()&&w.property("explorerBuilds").toInt()==builds&&tree->FindKey(V2EntityTree::KeyOf(row))==row))return false;
    category->setExpanded(true);tree->scrollToItem(row);QApplication::processEvents();
    QStyleOptionViewItem option;option.initFrom(tree);option.rect=tree->visualItemRect(row);
    option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=row->checkState(0);
    const auto hit=tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,tree).center();
    for(int i=0;i<2;++i){Mouse(*tree->viewport(),QEvent::MouseButtonPress,hit,Qt::LeftButton,Qt::LeftButton);
        Mouse(*tree->viewport(),QEvent::MouseButtonRelease,hit,Qt::LeftButton,Qt::NoButton);QApplication::processEvents();
        if(!Explain("new row checkbox changes the actual entity",w.Session().GetDocument().FindEntity(id)->visibility==(i?domain::Visibility::Visible:domain::Visibility::Hidden)))return false;}
    tree->setCurrentItem(w.ItemOfEntity(id));tree->setFocus();Key(*tree,Qt::Key_F2);
    auto* editor=tree->findChild<QLineEdit*>();if(!editor||!editor->isVisible())return false;
    editor->selectAll();QInputMethodEvent text;text.setCommitString("new drawn wire");QApplication::sendEvent(editor,&text);Key(*editor,Qt::Key_Return);
    Note(("appended name before save: "+w.Session().GetDocument().FindEntity(id)->displayName).c_str());
    const auto saved=w.SaveAndReopen("appended-rows.kcd2");Note(("save result: "+w.StatusText().toStdString()).c_str());
    return Explain("new row rename survives native save/reopen",saved
        &&w.Session().GetDocument().FindEntity(id)->displayName=="new drawn wire");
}
bool DrawingBenchmark(V2MainWindow& w){
    const auto output=qEnvironmentVariable("KACHACAD_DRAWING_PERF_OUTPUT");if(output.isEmpty())return true;
    w.RunCommand("file.new");auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(250);
    const int count=std::clamp(qEnvironmentVariableIntValue("KACHACAD_DRAWING_PERF_WIRES"),100,10000);
    for(int i=0;i<count;++i){const double x=20+(i%100)*.6,y=20+(i/100)*.6;
        auto line=geometry::CurveSegment::MakeLine({x,y,0},{x+.4,y+.2,0});
        if(!w.Session().AddWire({line.Value()},false,"benchmark wire").committed)return false;}
    if(!w.SaveAndReopen(QStringLiteral("drawing-performance.kcd2")))return false;
    v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(250);
    QApplication::processEvents();w.SelectTool(modeling::DrawingTool::Line);
    QJsonArray rows;
    auto measure=[&](const char* name,int n,auto action){std::vector<double> ms;
        const auto builds=w.property("explorerBuilds").toInt();
        for(int i=0;i<n;++i){QElapsedTimer timer;timer.start();action(i);ms.push_back(timer.nsecsElapsed()/1e6);}
        std::sort(ms.begin(),ms.end());rows.push_back(QJsonObject{{"operation",name},{"samples",n},
            {"median_ms",ms[ms.size()/2]},{"p95_ms",ms[std::min(ms.size()-1,std::size_t(.95*ms.size()))]},
            {"maximum_ms",ms.back()},{"explorer_rebuilds",w.property("explorerBuilds").toInt()-builds}});};
    const auto revision=w.Session().GetDocument().Revision();
    measure("line-hover",48,[&](int i){v.HoverAt(At(v,{-50.+i*.1,-25,0}));});
    if(revision!=w.Session().GetDocument().Revision())return false;
    measure("line-commit",12,[&](int i){Click(v,{-80.,-40.+i,0});Click(v,{-70.,-40.+i,0});});
    QFile file(output);if(!file.open(QIODevice::WriteOnly))return false;
    file.write(QJsonDocument(QJsonObject{{"wire_count",count},{"viewport_width",v.width()},{"viewport_height",v.height()},
        {"section_view",v.SectionViewActive()},{"isolation",v.IsolationActive()},{"plane_wire_only",PlaneWireDisplayActive(v)},
        {"measure","synchronous real Qt mouse input, no screenshot or GPU wait"},{"results",rows}}).toJson());return true;
}
}
std::vector<SelfTestCase> CopyDrawingCases(){
    std::vector<SelfTestCase> cases{{"HP-COPY-01 Ctrl target selection keeps transform inputs",CopyTargets},
        {"HP-COPY-02 basepoint cursor ghost and atomic Undo Redo",CopyGhost},
        {"HP-COPY-03 role rectangle Escape and tool switch",CopyRoleBox},
        {"HP-COPY-04 numeric ghost matches Enter",CopyNumeric},
        {"HP-DRAW-01 appended rows visibility rename and fold state",DrawingAppendRows}};
    if(qEnvironmentVariableIsSet("KACHACAD_DRAWING_PERF_OUTPUT"))cases.push_back({"HP-DRAW-PERF line hover and commit latency",DrawingBenchmark});
    return cases;
}
}
