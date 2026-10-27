#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2Ribbon.h"
#include "V2SolidTool.h"
#include "V2ToolSelection.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/app/Ribbon.h"
#include "kachakacha/app/CommandCatalog.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QEvent>
#include <QIODevice>
#include <QPointF>
#include <QString>
#include <QWidget>
#include "V2EntityTree.h"
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTimer>
#include <QLabel>
#include <QListWidget>
#include "V2FabricationDock.h"
#include "kachakacha/app/Selection.h"
#include <cmath>

namespace kachakacha::v2::selftest {
namespace {
bool Press(QWidget* widget) {
    if(!widget || !widget->isVisible() || !widget->isEnabled())return false;
    widget->window()->activateWindow();widget->setFocus(Qt::MouseFocusReason);
    QPointer<QWidget> guard(widget);const QPointF p=dynamic_cast<QCheckBox*>(widget)
        ?QPointF(10,widget->rect().center().y()):QPointF(widget->rect().center());
    for(auto kind:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
        if(!guard)return false;
        QMouseEvent event(kind,p,QPointF(widget->mapToGlobal(p.toPoint())),Qt::LeftButton,
            kind==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(widget,&event);
    }
    QApplication::processEvents();return true;
}
bool Ribbon(V2MainWindow& w,std::string_view command) {
    for(const auto mode:app::AllUiModes())for(const auto& category:app::RibbonCategoriesFor(mode))
        for(const auto& tool:category.tools)if(tool.commandId==command){
            if(w.Mode()!=mode)w.SetMode(mode);QApplication::processEvents();
            for(auto* b:w.Ribbon().findChildren<QToolButton*>("ribbonCategory"))
                if(b->isVisible()&&b->text()==QString::fromUtf8(category.labelJa.data(),int(category.labelJa.size()))) {
                    if(!Press(b))return false;break;
                }
            for(auto* b:w.Ribbon().findChildren<QToolButton*>("ribbonTool"))
                if(b->isVisible()&&b->text()==QString::fromUtf8(tool.labelJa.data(),int(tool.labelJa.size())))return Press(b);
            return false;
        }
    return false;
}
void Click(V2Viewport& v,geometry::Vector3 p) {
    const auto projected=v.Mapping().Project(p);if(!projected)return;
    const QPointF pixel(projected->x,projected->y);
    for(auto kind:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
        QMouseEvent event(kind,pixel,QPointF(v.mapToGlobal(pixel.toPoint())),Qt::LeftButton,
            kind==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&v,&event);
    }
    QApplication::processEvents();
}
QPushButton* Button(QWidget& parent,const QString& text) {
    for(auto* b:parent.findChildren<QPushButton*>())if(b->text()==text&&b->isVisible())return b;
    return nullptr;
}
void Shot(V2MainWindow& w,const char* name) {
    const auto path=qEnvironmentVariable("KACHACAD_CONTINUITY_EVIDENCE");if(path.isEmpty())return;
    QDir dir(path);dir.mkpath(".");w.grab().save(dir.filePath(QString::fromUtf8(name)+".png"));
}
bool DrawAfter(V2MainWindow& w) {
    if(!Ribbon(w,"draw.line"))return Explain("visible ribbon line button",false);
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(200);v.SetSnapSuppressed(true);
    const int count=CountOfKind(w,domain::EntityKind::Wire);
    Click(v,{-30,-25,0});Click(v,{-10,-25,0});
    const auto directory=qEnvironmentVariable("KACHACAD_CONTINUITY_EVIDENCE");
    if(!directory.isEmpty()){QFile out(QDir(directory).filePath("line-state.txt"));if(out.open(QIODevice::WriteOnly))out.write(
        (QStringLiteral("tool=%1 points=%2 wires=%3 before=%4 status=%5").arg(int(w.Session().CurrentTool()))
        .arg(w.Session().PlacedPointCount()).arg(CountOfKind(w,domain::EntityKind::Wire)).arg(count).arg(w.StatusText())).toUtf8());}
    return Explain("next tool creates a line through viewport input",CountOfKind(w,domain::EntityKind::Wire)==count+1);
}
bool EmbossToLoft(V2MainWindow& w) {
    w.resize(1500,950);QApplication::processEvents();
    if(!DrawRectangleByHand(w))return false;
    const auto curve=w.Session().Scene().curves.front();
    w.SelectTool(modeling::DrawingTool::Select);w.Viewport().SetSelection({});
    if(!Ribbon(w,"part.curved_emboss")||!Ribbon(w,"part.loft_solid"))return false;
    w.Viewport().SetViewCenter(curve.segment.Evaluate(0.5));
    Click(w.Viewport(),curve.segment.Evaluate(0.5));
    Shot(w,"emboss-to-loft");
    return Explain("loft can select a wire after support-face picking",w.SolidTool().Input().profiles.size()==1);
}
bool OutputToDrawing(V2MainWindow& w) {
    if(!DrawRectangleByHand(w))return false;
    if(!Ribbon(w,"export.selection"))return false;
    auto* dialog=w.findChild<QDialog*>("selectionOutput");if(!dialog)return false;
    auto* combo=dialog->findChild<QComboBox*>("outputPlacement");if(!combo)return false;
    combo->setCurrentIndex(1);QApplication::processEvents();
    if(!Press(Button(*dialog,QStringLiteral("配置先を面上で指定"))))return false;
    const bool result=DrawAfter(w);Shot(w,"output-to-line");dialog->close();return result;
}
bool OutputPickReuse(V2MainWindow& w) {
    w.resize(1500,1000);QApplication::processEvents();
    if(!DrawRectangleByHand(w))return false;
    for(const auto* role:{"基準点を3Dで指定","元の向きを面から指定","配置先を面上で指定"}){
        if(!Ribbon(w,"export.selection"))return false;
        QDialog* dialog=nullptr;for(auto* candidate:w.findChildren<QDialog*>("selectionOutput"))if(candidate->isVisible())dialog=candidate;
        if(!dialog)return false;auto* combo=dialog->findChild<QComboBox*>("outputPlacement");combo->setCurrentIndex(1);QApplication::processEvents();
        if(!Press(Button(*dialog,QString::fromUtf8(role)))||!DrawAfter(w))return false;
        if(!Ribbon(w,"export.selection"))return false;
        int visible=0;for(auto* candidate:w.findChildren<QDialog*>("selectionOutput"))if(candidate->isVisible())++visible;
        if(!Explain("output window reused without stale point pick",visible==1&&!w.Viewport().PickPending()&&!w.Viewport().ReferencePickPending()))return false;
        if(!Press(Button(*dialog,QString::fromUtf8(role))))return false;
        dialog->close();QApplication::processEvents();
        if(!DrawAfter(w))return false;
    }
    return true;
}
bool FaceSelectionPause(V2MainWindow& w) {
    if(!w.ApplyToolShotState(QStringLiteral("ui-tool-fillet")))return false;
    w.SelectTool(modeling::DrawingTool::Select);w.Viewport().SetSelection({});
    if(!Ribbon(w,"workplane.from_face"))return false;
    if(!Press(w.findChild<QToolButton*>("toolEditSelection")))return false;
    if(!Explain("face action pauses for selection",V2ToolSelection::Active(w.Viewport())))return false;
    const auto revision=w.Session().GetDocument().Revision();
    const auto shapes=w.Viewport().ShapeViews();if(shapes.empty()||shapes.front().mesh.triangles.empty())return false;
    const auto t=shapes.front().mesh.triangles.front();
    Click(w.Viewport(),t.Center());Shot(w,"face-action-selection");
    return Explain("selection-only face click never creates a workplane",w.Session().GetDocument().Revision()==revision
        &&V2ToolSelection::Active(w.Viewport()));
}
void Settle(){QEventLoop loop;QTimer::singleShot(120,&loop,&QEventLoop::quit);loop.exec();}
void Key(QWidget& widget,int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    widget.window()->activateWindow();widget.setFocus();QApplication::processEvents();
    QKeyEvent press(QEvent::KeyPress,key,modifiers),release(QEvent::KeyRelease,key,modifiers);
    QApplication::sendEvent(&widget,&press);QApplication::sendEvent(&widget,&release);QApplication::processEvents();
}
bool Number(QWidget* widget,const QString& number) {
    if(!widget||!Press(widget))return false;
    const auto* floating=dynamic_cast<QDoubleSpinBox*>(widget);const auto* integer=dynamic_cast<QSpinBox*>(widget);
    const bool changing=(floating&&floating->value()!=number.toDouble())||(integer&&integer->value()!=number.toInt());
    auto* input=widget->findChild<QLineEdit*>();if(!input)return false;
    input->setFocus();input->selectAll();QInputMethodEvent text;text.setCommitString(number);QApplication::sendEvent(input,&text);
    if(changing)Key(*input,Qt::Key_Return);Key(*input,Qt::Key_Tab);Settle();return true;
}
bool SpacedPoints(V2MainWindow& w) {
    w.resize(1500,950);QApplication::processEvents();auto& v=w.Viewport();v.SetVisibleWidthMm(120);v.SetSnapSuppressed(true);
    const auto line=geometry::CurveSegment::MakeLine({-25,-20,0},{-15,-20,0}).Value();
    const auto arc=geometry::CurveSegment::MakeCircularArc({15,-20,0},{0,0,1},{1,0,0},10,0,1.5707963267948966).Value();
    if(!w.Session().AddWire({line},false,"手摺の直線").committed||!w.Session().AddWire({arc},false,"車体に沿う曲線").committed)return false;
    if(!w.SaveAndReopen(QStringLiteral("spaced-point-input.kcd2")))return false;
    v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(120);
    if(!Ribbon(w,"wire.spaced_points"))return false;
    Click(v,line.Evaluate(0.5));Click(v,arc.Evaluate(0.5));Settle();
    if(!Explain("two edges added by plain clicks",v.Selection().entityIds.size()==2))return false;
    if(!Number(w.findChild<QDoubleSpinBox*>("curvePointsPitch"),"2.000"))return false;
    Shot(w,"spaced-points-preview");
    if(!Explain("preview keeps the document unchanged",CountOfKind(w,domain::EntityKind::Point)==0))return false;
    if(!Press(w.findChild<QPushButton*>("curvePointsConfirm")))return false;
    if(!Explain("two edges produce six plus nine points",CountOfKind(w,domain::EntityKind::Point)==15))return false;
    Shot(w,"spaced-points-committed");Key(v,Qt::Key_Z,Qt::ControlModifier);
    if(!Explain("one actual Undo removes all generated points",CountOfKind(w,domain::EntityKind::Point)==0))return false;
    Key(v,Qt::Key_Y,Qt::ControlModifier);
    if(!Explain("Redo and save reload preserve points",CountOfKind(w,domain::EntityKind::Point)==15&&w.SaveAndReopen(QStringLiteral("spaced-point-result.kcd2"))&&CountOfKind(w,domain::EntityKind::Point)==15))return false;
    return DrawAfter(w);
}
bool CheckSpacedSpline(V2MainWindow& w,const geometry::CurveSegment& curve) {
    if(!w.Session().AddWire({curve},false,"3D曲線").committed||!w.SaveAndReopen(QStringLiteral("spaced-spline.kcd2")))return false;
    w.Viewport().SetVisibleWidthMm(120);
    if(!Ribbon(w,"wire.spaced_points"))return false;Click(w.Viewport(),curve.Evaluate(0.3));Settle();
    auto* mode=w.findChild<QComboBox*>("curvePointsMode");if(!mode)return false;mode->setCurrentIndex(1);Settle();
    if(!Number(w.findChild<QSpinBox*>("curvePointsDivisions"),"8")||!Press(w.findChild<QPushButton*>("curvePointsConfirm")))return false;
    const double length=curve.TotalLength(1e-8);std::vector<double> distances;
    for(const auto& p:w.Session().Scene().points){
        const auto nearest=curve.ClosestPoint(p.position);
        if(nearest.distance<1e-4)distances.push_back(curve.ArcLength(0,nearest.parameter,1e-8));}
    std::sort(distances.begin(),distances.end());
    if(!Explain("3D Bezier produces nine points",distances.size()==9))return false;
    for(std::size_t i=0;i<distances.size();++i)if(!Explain("equal arc lengths rather than equal parameters",std::abs(distances[i]-length*double(i)/8)<1e-4))return false;
    return true;
}
bool SpacedSpline(V2MainWindow& w) {
    const auto bezier=geometry::CurveSegment::MakeCubicBezier({{-20,-10,0},{-10,25,12},{5,-20,6},{20,10,8}}).Value();
    if(!CheckSpacedSpline(w,bezier)||!w.SaveAndReopen("spaced-bezier-result.kcd2"))return false;
    w.RunCommand("file.new");
    const auto spline=geometry::CurveSegment::MakeCubicBSpline({{-20,-10,0},{-10,25,12},{5,-20,6},{20,10,8},{28,-5,2}}).Value();
    return CheckSpacedSpline(w,spline);
}
bool SpacedLimitAndCancel(V2MainWindow& w) {
    w.resize(1500,950);QApplication::processEvents();
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},10).Value();
    if(!w.Session().AddWire({circle},false,"円").committed||!w.SaveAndReopen(QStringLiteral("spaced-circle.kcd2")))return false;
    if(!Ribbon(w,"wire.spaced_points"))return false;w.Viewport().SetViewCenter(circle.Evaluate(0.2));Click(w.Viewport(),circle.Evaluate(0.2));Settle();
    if(!Explain("circle selected before limit validation",w.Viewport().Selection().ordered.size()==1))return false;
    if(!Number(w.findChild<QDoubleSpinBox*>("curvePointsPitch"),"0.001"))return false;
    if(!Explain("excess output cannot be committed",!w.findChild<QPushButton*>("curvePointsConfirm")->isEnabled()))return false;
    if(!Number(w.findChild<QDoubleSpinBox*>("curvePointsPitch"),"2"))return false;
    Key(w.Viewport(),Qt::Key_Escape);
    if(!Explain("cancel clears preview without points",CountOfKind(w,domain::EntityKind::Point)==0&&!w.findChild<QPushButton*>("curvePointsConfirm")->isEnabled()))return false;
    Click(w.Viewport(),circle.Evaluate(0.2));Settle();
    auto* mode=w.findChild<QComboBox*>("curvePointsMode");mode->setCurrentIndex(1);Settle();
    if(!Number(w.findChild<QSpinBox*>("curvePointsDivisions"),"8")||!Press(w.findChild<QPushButton*>("curvePointsConfirm")))return false;
    if(!Explain("closed circle has eight distinct points",CountOfKind(w,domain::EntityKind::Point)==8))return false;
    return DrawAfter(w);
}
bool CopyRoleSequence(V2MainWindow& w) {
    if(!DrawRectangleByHand(w))return false;
    const auto id=w.Session().Scene().curves.front().entityId;
    w.Viewport().SetSelection(app::SelectionSet{{id}});
    if(!Ribbon(w,"wire.copy"))return false;
    w.Viewport().SetViewDirection(ViewDirection::Top);w.Viewport().SetVisibleWidthMm(200);w.Viewport().SetSnapSuppressed(true);
    Click(w.Viewport(),{-30,-30,0});Settle();
    auto* base=w.findChild<QLabel*>("transformBasePoint");auto* destination=w.findChild<QLabel*>("transformDestination");
    if(!Explain("base coordinates and destination step are shown",base&&destination&&base->isVisible()&&base->text().contains("-30.000")&&destination->text().contains(QStringLiteral("カーソル"))))return false;
    if(!Press(w.findChild<QPushButton*>("transformResetBase")))return false;
    if(!Explain("retake base keeps selected objects",w.Session().PlacedPointCount()==0&&w.Viewport().Selection().entityIds.size()==1))return false;
    const int before=CountOfKind(w,domain::EntityKind::Wire);
    Click(w.Viewport(),{-20,-30,0});Click(w.Viewport(),{-10,-30,0});Settle();Shot(w,"copy-explicit-roles");
    return Explain("destination creates one copy",CountOfKind(w,domain::EntityKind::Wire)==before+1);
}
bool PointSelectionRows(V2MainWindow& w) {
    const auto a=geometry::CurveSegment::MakeLine({-20,0,0},{-10,0,0}).Value();
    const auto b=geometry::CurveSegment::MakeLine({-10,0,0},{-10,10,0}).Value();
    const auto c=geometry::CurveSegment::MakeLine({10,0,0},{20,0,0}).Value();
    auto first=w.Session().AddWire({a,b},false,"Aの2辺");auto second=w.Session().AddWire({c},false,"Bの1辺");
    if(!first.committed||!second.committed||!w.SaveAndReopen("point-rows.kcd2"))return false;
    w.resize(1500,950);QApplication::processEvents();w.Viewport().SetVisibleWidthMm(120);
    if(!Ribbon(w,"wire.spaced_points"))return false;
    Click(w.Viewport(),a.Evaluate(.5));Click(w.Viewport(),b.Evaluate(.5));Click(w.Viewport(),c.Evaluate(.5));Settle();
    auto* roles=w.findChild<QWidget*>("curvePointsTargets");auto* list=roles?roles->findChild<QListWidget*>():nullptr;
    if(!Explain("three selected edges have three matching rows",list&&list->count()==3&&w.Viewport().Selection().ordered.size()==3))return false;
    list->setFocus();Key(*list,Qt::Key_Home);Key(*list,Qt::Key_Down);Key(*list,Qt::Key_Down);
    if(!Press(Button(*roles,QStringLiteral("この項目を外す"))))return false;
    const auto selected=w.Viewport().Selection();
    return Explain("removing B row preserves both A edges",selected.ordered.size()==2&&selected.entityIds.size()==1&&selected.entityIds.front()==first.createdEntityIds.front());
}
bool PointBoundaries(V2MainWindow& w) {
    const auto a=geometry::CurveSegment::MakeLine({0,-2,0},{1,-2,0}).Value();
    const auto b=geometry::CurveSegment::MakeLine({0,2,0},{1.001,2,0}).Value();
    if(!w.Session().AddWire({a},false,"999内点").committed||!w.Session().AddWire({b},false,"1000内点").committed||!w.SaveAndReopen("point-limit.kcd2"))return false;
    w.resize(1500,950);QApplication::processEvents();w.Viewport().SetVisibleWidthMm(20);
    if(!Ribbon(w,"wire.spaced_points"))return false;Click(w.Viewport(),a.Evaluate(.5));Settle();
    auto* endpoints=w.findChild<QCheckBox*>("curvePointsEndpoints");if(!endpoints||!Press(endpoints))return false;
    if(!Number(w.findChild<QDoubleSpinBox*>("curvePointsPitch"),"0.001"))return false;
    auto* status=w.findChild<QLabel*>("curvePointsStatus");auto* confirm=w.findChild<QPushButton*>("curvePointsConfirm");
    if(!Explain("999 inner points are accepted",status->text().contains(QStringLiteral("999点"))&&confirm->isEnabled()))return false;
    if(!Press(endpoints)||!Explain("1001 points with endpoints are rejected",!confirm->isEnabled()))return false;
    if(!Press(endpoints)||!Press(w.findChild<QPushButton*>("curvePointsClear")))return false;
    Click(w.Viewport(),b.Evaluate(.5));Settle();
    if(!Explain("exactly 1000 inner points are accepted",status->text().contains(QStringLiteral("1000点"))&&confirm->isEnabled()))return false;
    QElapsedTimer timer;timer.start();if(!Press(confirm))return false;const auto milliseconds=timer.elapsed();
    const auto directory=qEnvironmentVariable("KACHACAD_CONTINUITY_EVIDENCE");if(!directory.isEmpty()){
        QFile output(QDir(directory).filePath("maximum-point-commit.json"));if(!output.open(QIODevice::WriteOnly))return false;
        output.write(QJsonDocument(QJsonObject{{"points",1000},{"commit_and_ui_settle_ms",milliseconds}}).toJson());}
    if(!Explain("maximum accepted output really commits all 1000 points",CountOfKind(w,domain::EntityKind::Point)==1000))return false;
    Key(w.Viewport(),Qt::Key_Z,Qt::ControlModifier);
    return Explain("maximum output has one Undo",CountOfKind(w,domain::EntityKind::Point)==0)&&DrawAfter(w);
}
bool PointNumericEnter(V2MainWindow& w) {
    const auto line=geometry::CurveSegment::MakeLine({-10,0,0},{0,0,0}).Value();
    if(!w.Session().AddWire({line},false,"Enter試験").committed||!w.SaveAndReopen("point-enter.kcd2")||!Ribbon(w,"wire.spaced_points"))return false;
    Click(w.Viewport(),line.Evaluate(.5));Settle();
    if(!Press(w.findChild<QCheckBox*>("curvePointsReverse")))return false;
    auto* spin=w.findChild<QDoubleSpinBox*>("curvePointsPitch");if(!spin||!Press(spin))return false;
    auto* input=spin->findChild<QLineEdit*>();input->selectAll();QInputMethodEvent text;text.setCommitString("3.000");QApplication::sendEvent(input,&text);
    Key(*input,Qt::Key_Return);Settle();
    if(!Explain("first Enter applies a new value without committing",spin->value()==3&&CountOfKind(w,domain::EntityKind::Point)==0))return false;
    Key(*input,Qt::Key_Return);Settle();
    if(!Explain("second Enter confirms while focus stays in numeric field",CountOfKind(w,domain::EntityKind::Point)==5))return false;
    for(const double x:{-10.0,-9.0,-6.0,-3.0,0.0}){
        bool found=false;for(const auto& p:w.Session().Scene().points)if(geometry::Distance(p.position,{x,0,0})<1e-5)found=true;
        if(!Explain("reversed pitch starts from the original end",found))return false;
    }
    return true;
}
bool PointTreeRename(V2MainWindow& w) {
    const auto line=geometry::CurveSegment::MakeLine({-20,0,0},{0,0,0}).Value();
    const auto added=w.Session().AddWire({line},false,"手摺");
    if(!added.committed||!w.SaveAndReopen("point-tree-label.kcd2")||!Ribbon(w,"wire.spaced_points"))return false;
    const auto id=added.createdEntityIds.front();auto* tree=w.EntityTree();auto* row=w.ItemOfEntity(id);
    tree->setCurrentItem(row);tree->scrollToItem(row);Settle();
    if(!Explain("rename fixture has selected curve preview",w.findChild<QPushButton*>("curvePointsConfirm")->isEnabled()))return false;
    const auto visibleEditor=[tree]{for(auto* edit:tree->findChildren<QLineEdit*>())if(edit->isVisible())return edit;return static_cast<QLineEdit*>(nullptr);};
    Key(*tree,Qt::Key_F2);auto* editor=visibleEditor();if(!editor)return false;
    editor->selectAll();QInputMethodEvent text;text.setCommitString(QStringLiteral("手摺の基準線"));QApplication::sendEvent(editor,&text);
    if(!Explain("tree editor accepts replacement label",editor->text()==QStringLiteral("手摺の基準線")))return false;
    Key(*editor,Qt::Key_Return);Settle();
    if(!Explain(("tree Enter name="+w.Session().GetDocument().FindEntity(id)->displayName+" points="+std::to_string(CountOfKind(w,domain::EntityKind::Point))).c_str(),w.Session().GetDocument().FindEntity(id)->displayName=="手摺の基準線"&&CountOfKind(w,domain::EntityKind::Point)==0))return false;
    Key(*tree,Qt::Key_F2);editor=visibleEditor();if(!editor)return false;
    const auto selected=w.Viewport().Selection().entityIds;const auto revision=w.Session().GetDocument().Revision();
    Key(*editor,Qt::Key_Escape);
    return Explain("tree Escape cancels only label editing",w.Viewport().Selection().entityIds==selected
        &&w.Session().GetDocument().Revision()==revision&&CountOfKind(w,domain::EntityKind::Point)==0);
}
bool ModeWidgetReuse(V2MainWindow& w) {
    if(!Ribbon(w,"draw.line"))return false;
    QPointer<QToolButton> line;
    for(auto* button:w.Ribbon().findChildren<QToolButton*>("ribbonTool"))
        if(button->isVisible()&&button->text()==QStringLiteral("線"))line=button;
    const int category=w.Ribbon().CurrentCategory();
    w.Ribbon().ShowCategory(category);w.Ribbon().ShowMode(app::UiMode::Drawing);
    if(!Explain("same ribbon category keeps its live button",line&&line->isVisible()))return false;
    for(auto* button:w.Ribbon().findChildren<QToolButton*>("ribbonCategory"))
        if(button->isVisible()&&button->isChecked()){
            if(!Press(button)||!Explain("reclick keeps current category checked",button->isChecked()))return false;break;}
    QPointer<QLineEdit> search=w.findChild<QLineEdit*>("toolSearch");
    if(!search)return false;search->setText("part.extrude");
    for(const auto mode:{app::UiMode::Part,app::UiMode::Output,app::UiMode::Drawing}){
        w.SetMode(mode);QApplication::processEvents();
        if(!Explain("mode switch reuses chooser and resets the filter",search&&search==w.findChild<QLineEdit*>("toolSearch")&&search->text().isEmpty()))return false;
    }
    w.OperationHost().FocusToolSearch();search->setText("wire.spaced_points");QApplication::processEvents();
    QPushButton* result=nullptr;
    for(auto* button:w.findChildren<QPushButton*>("toolChoice"))if(button->isVisible()&&button->text()==QStringLiteral("曲線に等間隔の点"))result=button;
    if(!Press(result)||!Explain("reused search starts the requested tool",w.findChild<QWidget*>("curvePointsPanel")&&w.findChild<QWidget*>("curvePointsPanel")->isVisible()))return false;
    return DrawAfter(w);
}
bool FabricationCancel(V2MainWindow& w) {
    if(!MakeCurvedGuideSurface(w))return false;
    w.Viewport().SetSelection(app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface));
    if(w.FabricationMethodInUse()!=app::FabricationMethod::BandApproximation){w.RunCommand("fabrication.set_method");
        w.Viewport().SetSelection(app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface));}
    w.RunCommand("fabrication.create");w.RunCommand("fabrication.create");
    if(!Explain("partition fixture has at least two bands",w.FabricationPanelCount()>=2))return false;
    w.SetMode(app::UiMode::Fabrication);w.FabricationDock().SetPartNumbersText("1");
    if(!Ribbon(w,"fabrication.merge_parts")||!Press(Button(w,QStringLiteral("結合をプレビュー")))||!Explain("merge preview is shown",w.PendingPartitionShown()))return false;
    Key(w.Viewport(),Qt::Key_Escape);
    if(!Explain("Escape discards uncommitted partition agreement",!w.PendingPartitionShown()))return false;
    if(!Ribbon(w,"fabrication.merge_parts")||!Press(Button(w,QStringLiteral("結合をプレビュー")))||!w.PendingPartitionShown())return false;
    w.SetMode(app::UiMode::Drawing);
    return Explain("mode change discards partition agreement",!w.PendingPartitionShown())&&DrawAfter(w);
}
bool RibbonToolExits(V2MainWindow& w) {
    w.resize(1600,1000);QApplication::processEvents();QJsonArray audited;
    for(const auto mode:app::AllUiModes())for(const auto& category:app::RibbonCategoriesFor(mode))for(const auto& tool:category.tools){
        if(mode==app::UiMode::Instructions)continue; // Its separate editor toolbar has dedicated UI cases.
        const auto* command=app::FindCommand(tool.commandId);
        if(tool.Blocked()||!command||(command->mode!=app::CommandMode::Tool&&command->mode!=app::CommandMode::Modeless))continue;
        w.Viewport().SetSelection({});w.SetMode(mode);QApplication::processEvents();
        bool categoryPressed=false;
        for(auto* b:w.Ribbon().findChildren<QToolButton*>("ribbonCategory"))if(b->isVisible()&&b->text()==QString::fromUtf8(category.labelJa.data(),int(category.labelJa.size()))){categoryPressed=Press(b);break;}
        if(!Explain("ribbon category is reachable",categoryPressed))return false;
        bool pressed=false;
        for(auto* b:w.Ribbon().findChildren<QToolButton*>("ribbonTool"))if(b->isVisible()&&b->text()==QString::fromUtf8(tool.labelJa.data(),int(tool.labelJa.size()))){pressed=Press(b);break;}
        if(!Explain(("actual ribbon tool: "+std::string(tool.commandId)).c_str(),pressed))return false;
        const auto status=w.StatusText();
        if(!Explain(("usable line after "+std::string(tool.commandId)).c_str(),DrawAfter(w)))return false;
        audited.append(QJsonObject{{"command",QString::fromUtf8(tool.commandId.data(),int(tool.commandId.size()))},
            {"label",QString::fromUtf8(tool.labelJa.data(),int(tool.labelJa.size()))},{"method",tool.surfaceMethod.value_or(-1)},
            {"status_after_entry",status},{"next_line_created",true}});
    }
    const auto directory=qEnvironmentVariable("KACHACAD_CONTINUITY_EVIDENCE");if(!directory.isEmpty()){
        QFile output(QDir(directory).filePath("ribbon-tool-exits.json"));if(!output.open(QIODevice::WriteOnly))return false;
        output.write(QJsonDocument(QJsonObject{{"scope","actual visible Tool/Modeless ribbon variants followed by viewport line creation; not proof of every source geometry outcome"},{"count",audited.size()},{"entries",audited}}).toJson());}
    w.SetMode(app::UiMode::Instructions);
    return Explain("CAD tool inventory was exercised",audited.size()>30)&&DrawAfter(w);
}
}
std::vector<SelfTestCase> ToolContinuityCases(){return {
    {"HP-CONTINUITY support-face pick to loft wire via ribbon",EmbossToLoft},
    {"HP-CONTINUITY output destination pick to drawing via ribbon",OutputToDrawing},
    {"HP-CONTINUITY output point and face picks reuse and close safely",OutputPickReuse},
    {"HP-CONTINUITY face action pauses during selection editing",FaceSelectionPause},
    {"HP-CONTINUITY fabrication partition Escape and mode change",FabricationCancel},
    {"HP-CONTINUITY visible ribbon tool variants allow the next drawing operation",RibbonToolExits},
    {"HP-CONTINUITY copy base retake and destination roles",CopyRoleSequence},
    {"HP-CURVEPOINTS multiple edges preview commit history save and next tool",SpacedPoints},
    {"HP-CURVEPOINTS three dimensional spline arc length spacing",SpacedSpline},
    {"HP-CURVEPOINTS output bound cancel and closed curve deduplication",SpacedLimitAndCancel},
    {"HP-CURVEPOINTS mixed segments preserve exact selection rows",PointSelectionRows},
    {"HP-CURVEPOINTS 999 1000 and 1001 output boundaries",PointBoundaries},
    {"HP-CURVEPOINTS first numeric Enter previews second confirms",PointNumericEnter},
    {"HP-CURVEPOINTS tree label Enter and Escape never generate points",PointTreeRename},
    {"HP-CONTINUITY mode and category reuse preserves commands and search",ModeWidgetReuse}};}
}
