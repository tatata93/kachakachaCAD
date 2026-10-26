#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2SelfTestDeleteDialog.h"
#include "V2ToolSelection.h"
#include "V2LoopFacesTool.h"
#include "V2LoopFacesDock.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/modeling/MeshPick.h"
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
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>
#include <algorithm>

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
void PressButton(QPushButton& button)
{
    const QPointF point=button.rect().center();
    for(const auto kind:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
        QMouseEvent event(kind,point,QPointF(button.mapToGlobal(point.toPoint())),Qt::LeftButton,
            kind==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&button,&event);
    }
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
bool SelectDuringModelTools(V2MainWindow& w){
    const char* commands[]={"surface.from_lines","surface.create","part.extrude","part.thicken","wire.project_surface","wire.offset","part.curved_emboss","dimension.open","image.place"};
    for(const auto* command:commands){
        if(!Setup(w))return false;auto& v=w.Viewport();w.RunCommand("selection.activate");v.SetSelection({});w.RunCommand(command);
        const auto tool=w.Session().CurrentTool();const auto pending=w.PendingCommandLabel();
        auto* button=w.findChild<QToolButton*>("toolEditSelection");if(!button||!button->isVisible())return false;
        button->click();
        Mouse(v,QEvent::MouseButtonPress,Pixel(v,{-20,20,0}),Qt::LeftButton,Qt::LeftButton,Qt::ControlModifier);
        Mouse(v,QEvent::MouseButtonRelease,Pixel(v,{-20,20,0}),Qt::LeftButton,Qt::NoButton,Qt::ControlModifier);
        if(!Explain("Ctrl selects an exact edge without feeding the modeling tool",V2ToolSelection::Active(v)&&v.Selection().ordered.size()==1
            &&v.Selection().ordered.front().segmentId.has_value()&&w.PendingCommandLabel()==pending))return false;
        if(std::string_view(command)=="wire.project_surface")Shot(w,"tool-selection");
        Key(v,Qt::Key_Delete);
        if(!Explain("deletion keeps the armed modeling tool",w.Session().Scene().curves.size()==1
            &&w.Session().CurrentTool()==tool&&w.PendingCommandLabel()==pending))return false;
        Key(v,Qt::Key_Z,Qt::ControlModifier);if(w.Session().Scene().curves.size()!=2||!V2ToolSelection::Active(v))return false;
        Key(v,Qt::Key_Y,Qt::ControlModifier);if(w.Session().Scene().curves.size()!=1)return false;
        button->click();
        if(!Explain("returning restores the tool input selection",!V2ToolSelection::Active(v)&&v.Selection().entityIds.empty()
            &&w.Session().CurrentTool()==tool&&w.PendingCommandLabel()==pending))return false;
    }
    return true;
}
bool CtrlAddsFaceInputs(V2MainWindow& w){
    if(!Setup(w))return false;
    w.RunCommand("selection.activate");w.RunCommand("file.new");
    const geometry::Vector3 points[]={{-30,-20,0},{30,-20,0},{0,30,0}};
    for(int i=0;i<3;++i)
        if(!w.Session().AddWire({geometry::CurveSegment::MakeLine(points[i],points[(i+1)%3]).Value()},false,"輪郭").committed)return false;
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(160);
    v.SetSelection({});w.RunCommand("surface.from_lines");QApplication::processEvents();
    for(int i=0;i<3;++i)Click(v,(points[i]+points[(i+1)%3])*.5,Qt::ControlModifier);
    if(!Explain("Ctrl adds to the face tool instead of entering deletion selection",!V2ToolSelection::Active(v)
        &&v.Selection().entityIds.size()==3&&w.LoopFacesTool().Plan()&&w.LoopFacesTool().Plan()->faces.size()==1
        &&w.LoopFacesTool().Dock()->FaceRowCount()==1))return false;
    Key(v,Qt::Key_Return);
    return Explain("Ctrl-selected contour commits a face",CountOfKind(w,domain::EntityKind::GuideSurface)==1);
}
bool ScratchSelectionFaceInput(V2MainWindow& w){
    if(!Setup(w))return false;w.RunCommand("selection.activate");w.RunCommand("file.new");
    const geometry::Vector3 points[]={{-30,-20,0},{30,-20,0},{0,30,0}};
    app::SelectionSet input;
    for(int i=0;i<3;++i){
        const auto added=w.Session().AddWire({geometry::CurveSegment::MakeLine(points[i],points[(i+1)%3]).Value()},false,"輪郭");
        if(!added.committed)return false;input.entityIds.push_back(added.createdEntityIds.front());
    }
    auto& v=w.Viewport();v.SetSelection({});w.RunCommand("surface.from_lines");
    auto* toggle=w.findChild<QToolButton*>("toolEditSelection");toggle->click();
    auto* use=w.findChild<QPushButton*>("toolSelectionUseInput");
    auto* resume=w.findChild<QPushButton*>("toolSelectionResume");
    auto* notice=w.findChild<QLabel*>("toolSelectionMessage");
    auto* pages=w.findChild<QWidget*>("operationPages");
    if(!Explain("scratch input is clearly paused and empty transfer is disabled",use&&resume&&notice&&notice->isVisible()
        &&notice->text().contains(QStringLiteral("一時停止"))&&!use->isEnabled()&&pages&&!pages->isEnabled()))return false;
    v.SetSelection(input);if(!use->isEnabled())return false;PressButton(*use);
    if(!Explain("explicitly transferred wires rebuild the face rows",!V2ToolSelection::Active(v)&&!notice->isVisible()
        &&w.LoopFacesTool().Plan()&&w.LoopFacesTool().Plan()->faces.size()==1&&w.LoopFacesTool().Dock()->FaceRowCount()==1))return false;
    for(const int key:{Qt::Key_Escape,Qt::Key_Return,Qt::Key_Enter}){
        toggle->click();v.SetSelection({});Key(v,key);
        if(V2ToolSelection::Active(v)||v.Selection().entityIds!=input.entityIds||!w.LoopFacesTool().Plan()
            ||w.LoopFacesTool().Dock()->FaceRowCount()!=1)return false;
    }
    toggle->click();v.SetSelection({});PressButton(*resume);
    if(!w.LoopFacesTool().Plan()||w.LoopFacesTool().Dock()->FaceRowCount()!=1)return false;
    Key(v,Qt::Key_Return);
    if(!Explain("restored nonempty face inputs still commit",CountOfKind(w,domain::EntityKind::GuideSurface)==1))return false;
    toggle->click();w.RunCommand("file.new");
    return Explain("new document clears the suspended input state",!V2ToolSelection::Active(v)&&!notice->isVisible()&&pages->isEnabled());
}
bool OverlappingWindowInputs(V2MainWindow& w){
    if(!Setup(w))return false;w.RunCommand("selection.activate");w.RunCommand("file.new");
    using geometry::Vector3;using geometry::CurveSegment;
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    app::SelectionSet input;
    const std::vector<std::vector<CurveSegment>> loops={
        {line({-30,-20,0},{30,-20,0}),line({30,-20,0},{30,20,0}),line({30,20,0},{-30,20,0}),line({-30,20,0},{-30,-20,0})},
        {line({-10,-5,0},{10,-5,0}),line({10,-5,0},{10,5,0}),line({10,5,0},{-10,5,0})},
        {line({10,5,0},{-10,5,0}),line({-10,5,0},{-10,-5,0}),line({-10,-5,0},{9.997,-5,0})}};
    for(const auto& curves:loops){
        const auto added=w.Session().AddWire(curves,false,"重複投影の回帰");if(!added.committed)return false;
        input.entityIds.push_back(added.createdEntityIds.front());
    }
    auto& v=w.Viewport();v.SetSelection(input);const auto original=w.Session().Scene().curves;
    w.RunCommand("surface.from_lines");
    if(!Explain("duplicate edge and dangling overlap keep one holed face",w.LoopFacesTool().Plan()
        &&w.LoopFacesTool().Plan()->faces.size()==1&&w.LoopFacesTool().Dock()->FaceRowCount()==1))return false;
    Key(v,Qt::Key_Return);
    const auto made=w.Session().GetDocument().Snapshot().entities.back().id;bool hole=false,wall=false;
    for(const auto& shape:v.ShapeViews())if(shape.entityId==made)for(const auto& triangle:shape.mesh.triangles){
        hole=hole||modeling::RayHitsTriangle({0,0,10},{0,0,-1},triangle).has_value();
        wall=wall||modeling::RayHitsTriangle({20,0,10},{0,0,-1},triangle).has_value();
    }
    const auto& after=w.Session().Scene().curves;
    const bool unchanged=std::all_of(original.begin(),original.end(),[&](const auto& source){
        const auto found=std::find_if(after.begin(),after.end(),[&](const auto& c){return c.entityId==source.entityId&&c.segmentId==source.segmentId;});
        if(found==after.end()||found->segment.Kind()!=source.segment.Kind())return false;
        for(const double t:{0.,.25,.5,.75,1.})if(geometry::Distance(found->segment.Evaluate(t),source.segment.Evaluate(t))>1e-12)return false;
        return true;
    });
    return Explain("window stays empty and original source intervals are unchanged",!hole&&wall
        &&CountOfKind(w,domain::EntityKind::GuideSurface)==1&&unchanged
        &&std::count_if(after.begin(),after.end(),[&](const auto& c){return std::find(input.entityIds.begin(),input.entityIds.end(),c.entityId)!=input.entityIds.end();})==static_cast<int>(original.size()));
}
bool NonredundantNetworkInputs(V2MainWindow& w,bool openHole){
    if(!Setup(w))return false;w.RunCommand("selection.activate");w.RunCommand("file.new");
    using geometry::Vector3;using geometry::CurveSegment;
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    std::vector<CurveSegment> curves={line({-30,-20,0},{30,-20,0}),line({30,-20,0},{30,20,0}),
        line({30,20,0},{-30,20,0}),line({-30,20,0},{-30,-20,0})};
    if(openHole){
        const std::vector<CurveSegment> inner={line({-10,-5,0},{10,-5,0}),line({10,-5,0},{10,5,0}),
            line({10,5,0},{-10,5,0}),line({-10,5,0},{-10,-4.995,0})};
        curves.insert(curves.end(),inner.begin(),inner.end());
    }else curves.push_back(line({0,-20,0},{0,20,0}));
    const auto added=w.Session().AddWire(curves,false,"外周と内部線を同じワイヤーに保存");if(!added.committed)return false;
    app::SelectionSet selected;selected.entityIds={added.createdEntityIds.front()};w.Viewport().SetSelection(selected);
    w.RunCommand("surface.from_lines");
    const auto& plan=w.LoopFacesTool().Plan();
    if(openHole){
        const bool gap=plan&&!plan->gaps.empty();
        Key(w.Viewport(),Qt::Key_Return);
        return Explain("an open inner boundary is not discarded or silently filled",gap&&CountOfKind(w,domain::EntityKind::GuideSurface)==0);
    }
    return Explain("a divider touching edge interiors still creates two regions",plan&&plan->faces.size()==2);
}
bool ExplicitSelectionKeepsDrawing(V2MainWindow& w){
    if(!Setup(w))return false;auto& v=w.Viewport();Click(v,{-40,-30,0});
    if(!v.TypeIntoCursorField("7.25"))return false;
    auto* button=w.findChild<QToolButton*>("toolEditSelection");if(!button)return false;
    button->click();Click(v,{-20,20,0});Key(v,Qt::Key_Delete);
    if(!Inputs(w,1,1)||v.CursorPanel().states[v.CursorPanel().focusedIndex].text!="7.25")return false;
    Key(v,Qt::Key_Z,Qt::ControlModifier);if(!Inputs(w,1,2))return false;
    Key(v,Qt::Key_Escape);if(V2ToolSelection::Active(v)||!Inputs(w,1,2))return false;
    button->click();v.SetSelection({});Key(v,Qt::Key_Delete);Key(v,Qt::Key_Backspace);Key(v,Qt::Key_1,Qt::NoModifier,"1");
    if(!Explain("empty Delete does not replace the drawing tool",Inputs(w,1,2)&&V2ToolSelection::Active(v)
        &&v.CursorPanel().states[v.CursorPanel().focusedIndex].text=="7.25"))return false;
    button->click();Click(v,{-10,-30,0});return Inputs(w,0,3);
}
bool ReferencePickSurvivesSelection(V2MainWindow& w){
    if(!Setup(w))return false;auto& v=w.Viewport();w.RunCommand("selection.activate");
    bool called=false;v.BeginElementPick([](const auto&){return true;},[&](auto){called=true;});
    auto* button=w.findChild<QToolButton*>("toolEditSelection");button->click();Click(v,{-20,20,0});
    if(!Explain("edit selection does not fill the pending reference role",!called&&v.ReferencePickPending()&&!v.Selection().entityIds.empty()))return false;
    auto* use=w.findChild<QPushButton*>("toolSelectionUseInput");
    if(!Explain("pending reference cannot accidentally replace tool inputs",use&&!use->isEnabled()))return false;
    button->click();if(!v.ReferencePickPending()||called)return false;
    Click(v,{-20,20,0});return Explain("the original reference role resumes on return",called);
}
bool DocumentCaption(V2MainWindow& w){
    w.RunCommand("file.new");if(!w.windowTitle().contains(QStringLiteral("無題")))return false;
    QTemporaryDir directory;const auto path=directory.filePath(QStringLiteral("115 前面.kcd2"));
    if(!Setup(w))return false;
    w.SetPathChooser([&](bool){return path;});w.RunCommand("file.save_as");w.SetPathChooser({});
    w.RunCommand("file.new");if(!w.OpenDocumentFile(path))return false;
    if(!Explain("title shows the opened file name beside the application",w.windowTitle().contains(QStringLiteral("115 前面.kcd2"))
        &&w.windowTitle().startsWith("kachakachaCAD")&&w.windowFilePath()==path))return false;
    Shot(w,"file-caption");
    const auto title=w.windowTitle();if(w.OpenDocumentFile(directory.filePath("missing.kcd2"))||w.windowTitle()!=title)return false;
    w.RunCommand("file.new");return Explain("new clears the old file name",w.windowTitle().contains(QStringLiteral("無題"))&&!w.windowTitle().contains("115"));
}
}
std::vector<SelfTestCase> DrawingSelectionCases()
{
    return {
        {"HP-UX-FACE-CTRL Ctrl selection remains face input and creates a face",CtrlAddsFaceInputs},
        {"HP-UX-FACE-RESUME explicit input transfer and filled face resume",ScratchSelectionFaceInput},
        {"HP-UX-FACE-BRANCH overlapping projected intervals retain the window hole",OverlappingWindowInputs},
        {"HP-UX-FACE-DIVIDER compound wire retains an internal divider",[](auto& w){return NonredundantNetworkInputs(w,false);}},
        {"HP-UX-FACE-OPEN compound wire does not fill an open window",[](auto& w){return NonredundantNetworkInputs(w,true);}},
        {"HP-UX-SELECT modeling tools retain inputs through selection Delete and history",SelectDuringModelTools},
        {"HP-UX-DRAW explicit selection preserves draft and empty Delete",ExplicitSelectionKeepsDrawing},
        {"HP-UX-REF reference role pauses during arbitrary selection",ReferencePickSurvivesSelection},
        {"HP-UX-TITLE file name follows new open and failed open",DocumentCaption},
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
