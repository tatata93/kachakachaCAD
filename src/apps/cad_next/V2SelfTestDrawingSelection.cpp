#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2SelfTestDeleteDialog.h"
#include "kachakacha/document/Commands.h"
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QAction>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QTemporaryDir>

namespace kachakacha::v2::selftest {
namespace {
using namespace kachakacha::v2;
using T = modeling::DrawingTool;

QPointF Pixel(V2Viewport& v, geometry::Vector3 point)
{
    const auto p=v.Mapping().Project(point);return p?QPointF(p->x,p->y):QPointF(-100,-100);
}
void Mouse(V2Viewport& v,QEvent::Type kind,QPointF p,Qt::MouseButton button,
    Qt::MouseButtons held,Qt::KeyboardModifiers modifiers)
{
    QMouseEvent event(kind,p,QPointF(v.mapToGlobal(p.toPoint())),button,held,modifiers);
    QApplication::sendEvent(&v,&event);
}
void Click(V2Viewport& v,geometry::Vector3 point,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    const auto p=Pixel(v,point);
    Mouse(v,QEvent::MouseMove,p,Qt::NoButton,Qt::NoButton,modifiers);
    Mouse(v,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,modifiers);
    Mouse(v,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,modifiers);
}
void Key(V2Viewport& v,int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier,QString text={})
{
    if(!v.window()->isActiveWindow()){v.window()->activateWindow();QApplication::processEvents();}
    v.setFocus();QKeyEvent press(QEvent::KeyPress,key,modifiers,text);
    QApplication::sendEvent(&v,&press);
    QKeyEvent release(QEvent::KeyRelease,key,modifiers);QApplication::sendEvent(&v,&release);
}
bool Setup(V2MainWindow& w)
{
    w.RunCommand("file.new");w.resize(1440,900);QApplication::processEvents();
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);
    v.SetVisibleWidthMm(180);w.SelectTool(T::Polyline);
    Click(v,{-40,20,0});Click(v,{0,20,0});Click(v,{0,50,0});v.FinishTool();
    Note(("fixture curves="+std::to_string(w.Session().Scene().curves.size())+" "+w.StatusText().toStdString()).c_str());
    w.SelectTool(T::Line);v.SetSelection({});w.Session().GetDocument().MarkHistoryBoundary();
    return Explain("fixture is one wire with two segments",w.Session().Scene().curves.size()==2
        && w.Session().Scene().curves.front().entityId==w.Session().Scene().curves.back().entityId);
}
void Shot(V2MainWindow& w,const char* name)
{
    const auto root=qEnvironmentVariable("KACHACAD_DSEL_SCREENSHOT_DIR");if(root.isEmpty())return;
    QDir dir(root);dir.mkpath(".");w.grab().save(dir.filePath(QString::fromUtf8(name)+".png"));
}
bool Inputs(V2MainWindow& w,std::size_t count,std::size_t curves)
{
    const auto detail="draft and geometry agree (points="+std::to_string(w.Session().PlacedPointCount())+"/"+std::to_string(count)
        +", curves="+std::to_string(w.Session().Scene().curves.size())+"/"+std::to_string(curves)
        +", tool="+std::to_string(static_cast<int>(w.Session().CurrentTool()))+")";
    return Explain(detail.c_str(),w.Session().CurrentTool()==T::Line
        &&w.Session().PlacedPointCount()==count&&w.Session().Scene().curves.size()==curves);
}
bool SelectDeleteContinue(V2MainWindow& w)
{
    if(!Setup(w))return false;auto& v=w.Viewport();Click(v,{-40,-30,0});
    if(!v.TypeIntoCursorField("12.5"))return false;
    const auto text=v.CursorPanel().states[v.CursorPanel().focusedIndex].text;
    Click(v,{-20,20,0},Qt::ControlModifier);
    if(!Explain("Ctrl click selects an exact segment without adding a point",v.Selection().ordered.size()==1
        &&v.Selection().ordered.front().segmentId.has_value()&&w.Session().PlacedPointCount()==1))return false;
    Shot(w,"01-selected-during-line");
    Click(v,{-20,20,0},Qt::ControlModifier);if(!v.Selection().entityIds.empty())return false;
    Click(v,{-20,20,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    if(!Inputs(w,1,1)||v.CursorPanel().states[v.CursorPanel().focusedIndex].text!=text)return false;
    Shot(w,"02-deleted-keeping-input");Key(v,Qt::Key_Z,Qt::ControlModifier);
    if(!Inputs(w,1,2))return false;Key(v,Qt::Key_Y,Qt::ControlModifier);
    if(!Inputs(w,1,1)||v.CursorPanel().states[v.CursorPanel().focusedIndex].text!=text)return false;
    Click(v,{-10,-30,0});Shot(w,"03-continued-line");
    return Inputs(w,0,2);
}
bool ToolsAndEmptyDelete(V2MainWindow& w)
{
    const T tools[]={T::Line,T::Polyline,T::Rectangle,T::Circle,T::Arc,T::Bezier,T::Spline,T::Point,T::ConnectTwoPoints};
    for(const auto tool:tools){
        if(!Setup(w))return false;auto& v=w.Viewport();w.SelectTool(tool);
        if(tool!=T::Point)Click(v,{-40,-30,0});
        const auto count=w.Session().PlacedPointCount();
        Click(v,{-20,20,0},Qt::ControlModifier);Click(v,{0,35,0},Qt::ControlModifier);
        if(!Explain("two sub-elements can be toggled during every drawing tool",v.Selection().ordered.size()==2))return false;
        Key(v,Qt::Key_Delete);
        if(w.Session().CurrentTool()!=tool||w.Session().PlacedPointCount()!=count||!w.Session().Scene().curves.empty())return false;
        w.RunCommand("edit.undo");if(w.Session().PlacedPointCount()!=count||w.Session().Scene().curves.size()!=2)return false;
        v.SetSelection({});w.RunCommand("edit.delete");
        if(w.Session().CurrentTool()!=tool||w.Session().PlacedPointCount()!=count)return false;
    }
    return true;
}
bool HistoryOrder(V2MainWindow& w)
{
    if(!Setup(w))return false;auto& v=w.Viewport();w.SelectTool(T::Polyline);
    Click(v,{-40,-30,0});Click(v,{-20,20,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    Click(v,{-20,-30,0}); // Q after D; undo Q, then D, then P
    w.RunCommand("edit.undo");if(w.Session().PlacedPointCount()!=1||w.Session().Scene().curves.size()!=1)return false;
    w.RunCommand("edit.undo");if(w.Session().PlacedPointCount()!=1||w.Session().Scene().curves.size()!=2)return false;
    w.RunCommand("edit.undo");if(w.Session().PlacedPointCount()!=0||w.Session().Scene().curves.size()!=2)return false;
    Click(v,{-50,-30,0});w.RunCommand("edit.redo");
    if(w.Session().PlacedPointCount()!=1||w.Session().Scene().curves.size()!=1)return false;
    w.RunCommand("edit.undo");if(w.Session().PlacedPointCount()!=1||w.Session().Scene().curves.size()!=2)return false;
    // Two successive deletions at the same input prefix, then a new history boundary.
    w.SelectTool(T::Line);Click(v,{-40,-30,0});
    Click(v,{-20,20,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    Click(v,{0,35,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    if(!Inputs(w,1,0))return false;w.RunCommand("edit.undo");if(!Inputs(w,1,1))return false;
    w.RunCommand("edit.undo");if(!Inputs(w,1,2))return false;
    w.Session().GetDocument().MarkHistoryBoundary();w.RunCommand("edit.undo");if(!Inputs(w,0,2))return false;
    Click(v,{-40,-30,0});Click(v,{-20,20,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    w.RunCommand("edit.undo");return Inputs(w,1,2);
}
bool UnknownHistory(V2MainWindow& w)
{
    if(!Setup(w))return false;auto& v=w.Viewport();auto& doc=w.Session().GetDocument();
    const auto id=w.Session().Scene().curves.front().entityId;
    if(!doc.Run(document::SetConstructionCommand({id},true)).committed)return false;
    Click(v,{-40,-30,0});Click(v,{-20,20,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    w.RunCommand("edit.undo");if(!Inputs(w,1,2))return false;Key(v,Qt::Key_Backspace);
    w.RunCommand("edit.undo");if(doc.FindEntity(id)->construction)return false;
    w.RunCommand("edit.redo");if(!doc.FindEntity(id)->construction)return false;
    Click(v,{-50,-30,0});w.RunCommand("edit.redo");if(!Inputs(w,1,1))return false;
    w.RunCommand("edit.undo");return Inputs(w,1,2);
}
bool CancelAndReopen(V2MainWindow& w)
{
    if(!Setup(w))return false;auto& v=w.Viewport();Click(v,{-40,-30,0});
    Click(v,{-20,20,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    v.CancelTool();Click(v,{-50,-30,0});w.RunCommand("edit.undo");
    if(!Inputs(w,0,1))return false;w.RunCommand("edit.undo");if(!Inputs(w,0,2))return false;
    // Use the real Save command, whose successful completion establishes a history boundary.
    QTemporaryDir temporary;const auto path=temporary.filePath("drawing-selection-save.kcd2");
    const auto id=w.Session().Scene().curves.front().entityId;
    if(!w.Session().GetDocument().Run(document::SetConstructionCommand({id},true)).committed
        ||!w.Session().GetDocument().CanUndo())return false;
    w.SetPathChooser([&](bool){return path;});w.RunCommand("file.save_as");
    if(!QFileInfo::exists(path)||QFileInfo(path).size()==0||w.Session().GetDocument().CanUndo())return false;
    Click(v,{-40,-30,0});Click(v,{-20,20,0},Qt::ControlModifier);Key(v,Qt::Key_Delete);
    w.RunCommand("edit.undo");if(!Inputs(w,1,2))return false;
    v.CancelTool();if(!w.Session().GetDocument().CanRedo())return false;
    w.RunCommand("file.open");if(w.Session().GetDocument().CanRedo())return false;
    w.SelectTool(T::Line);v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(180);
    Click(v,{-40,-30,0});Click(v,{-20,20,0},Qt::ControlModifier);
    Key(v,Qt::Key_Delete);
    w.RunCommand("edit.undo");return Inputs(w,1,2);
}
bool BoxSelection(V2MainWindow& w,bool crossing)
{
    if(!Setup(w))return false;auto& v=w.Viewport();Click(v,{-40,-30,0});
    // A left-to-right box contains the whole wire; the reverse crossing touches one segment.
    const auto from=Pixel(v,crossing?geometry::Vector3{-10,25,0}:geometry::Vector3{-45,55,0});
    const auto to=Pixel(v,crossing?geometry::Vector3{-30,15,0}:geometry::Vector3{5,15,0});
    Mouse(v,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton,Qt::ControlModifier);
    Mouse(v,QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton,Qt::ControlModifier);
    Mouse(v,QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton,Qt::ControlModifier);
    if(!Explain("box selects the whole wire",v.Selection().ordered.size()==1
        &&v.Selection().ordered.front().kind==app::SelectionElementKind::Object))return false;
    Key(v,Qt::Key_Delete);if(!Inputs(w,1,0))return false;
    w.RunCommand("edit.undo");return Inputs(w,1,2);
}
bool NumericAndPickGuards(V2MainWindow& w)
{
    if(!Setup(w))return false;auto& v=w.Viewport();Click(v,{-40,-30,0});
    Click(v,{-20,20,0},Qt::ControlModifier);if(!v.TypeIntoCursorField("12.5"))return false;
    Key(v,Qt::Key_Delete); // typing resumed: clear the value, not the selected line
    if(!Inputs(w,1,2)||!v.CursorPanel().states[v.CursorPanel().focusedIndex].text.empty())return false;
    bool picked=false;v.BeginPointPick([&](const auto&){picked=true;},"点を選択");
    Click(v,{0,35,0},Qt::ControlModifier);if(!picked||!Inputs(w,1,2))return false;
    v.BeginElementPick([](const auto&){return true;},[&](const auto&){picked=false;});
    Click(v,{0,35,0},Qt::ControlModifier);if(picked||!Inputs(w,1,2))return false;
    // Native text editing must keep Delete, even if a sketch selection exists.
    QLineEdit edit(&w);edit.setText("123");edit.show();edit.setFocus();edit.setCursorPosition(1);
    QKeyEvent key(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(&edit,&key);
    if(edit.text()!="13"||!Inputs(w,1,2))return false;
    v.setFocus();Key(v,Qt::Key_Escape);return Inputs(w,0,2);
}
bool ContextDelete(V2MainWindow& w,bool dependent)
{
    if(!Setup(w))return false;auto& v=w.Viewport();const auto id=w.Session().Scene().curves.front().entityId;
    if(dependent){
        base::DeterministicIdGenerator ids(76430);domain::Feature f;f.id=ids.NextTyped<base::IdKind::Feature>();
        f.type=domain::FeatureType::CreatePoint;f.displayName="依存点";f.inputEntityIds={id};f.definition=domain::CreatePointDefinition{};
        domain::Entity e;e.id=ids.NextTyped<base::IdKind::Entity>();e.createdBy=f.id;e.kind=domain::EntityKind::Point;e.displayName="依存点";
        f.outputs={{"point",e.id,e.kind}};if(!w.Session().GetDocument().Run(document::AddFeatureCommand(f,{e},"依存点")).committed)return false;
    }
    Click(v,{-40,-30,0});if(!v.TypeIntoCursorField("8.5"))return false;
    bool actionSeen=false,dialogSeen=false;QTimer timer,confirmation;timer.setInterval(5);confirmation.setInterval(5);
    QObject::connect(&timer,&QTimer::timeout,&w,[&]{
        if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){
            if(actionSeen)return;
            for(auto* a:menu->actions())if(a->objectName()=="deletePickedSegment"){
                actionSeen=true;a->trigger();menu->close();break;
            }
        }
    });
    QObject::connect(&confirmation,&QTimer::timeout,&w,[&]{
        if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){
            if(dialog->objectName()=="confirmDependentDelete"){dialogSeen=true;dialog->reject();}
        }
    });
    timer.start();confirmation.start();v.PressRightWithoutMoving(Pixel(v,{-20,20,0}));timer.stop();confirmation.stop();
    if(!actionSeen||dialogSeen!=dependent||!Inputs(w,1,dependent?2:1))return false;
    return Explain("context action keeps numeric draft",v.CursorPanel().states[v.CursorPanel().focusedIndex].text=="8.5");
}
}
std::vector<SelfTestCase> DrawingSelectionCases()
{
    return {
        {"HP-DSEL-01 Ctrl segment Delete numeric draft Undo Redo and continue",SelectDeleteContinue},
        {"HP-DSEL-02 drawing tools multiple segments empty deletion",ToolsAndEmptyDelete},
        {"HP-DSEL-03 input and deletion history order save boundary",HistoryOrder},
        {"HP-DSEL-04 unrelated document Undo Redo cannot impersonate deletion",UnknownHistory},
        {"HP-DSEL-05 Ctrl window selects whole wire",[](auto& w){return BoxSelection(w,false);}},
        {"HP-DSEL-06 Ctrl crossing selects whole wire",[](auto& w){return BoxSelection(w,true);}},
        {"HP-DSEL-07 numeric input point pick reference pick native text and Escape",NumericAndPickGuards},
        {"HP-DSEL-08 right click segment Delete keeps drawing",[](auto& w){return ContextDelete(w,false);}},
        {"HP-DSEL-09 dependent context deletion cancel keeps drawing",[](auto& w){return ContextDelete(w,true);}},
        {"HP-DSEL-10 explicit cancel save and reopen invalidate old input prefixes",CancelAndReopen},
    };
}
}
