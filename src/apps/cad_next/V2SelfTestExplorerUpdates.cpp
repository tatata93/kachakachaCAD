#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2EntityTree.h"
#include "V2Viewport.h"
#include "V2GeneratedCurves.h"
#include "V2RenderSelection.h"
#include "V2LoopFacesTool.h"
#include "V2SurfaceDock.h"
#include "V2FabricationDock.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/CurveFit.h"
#include "kachakacha/geometry/WireEdit.h"
#include <QApplication>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QMouseEvent>
#include <QEvent>
#include <QPointF>
#include <QPoint>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QSignalBlocker>
#include <QMenu>
#include <QAction>
#include <QWidget>
#include <QString>
#include <QKeyEvent>
#include <QObject>
#include <QPixmap>
#include <QEventLoop>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QComboBox>
#include <QToolButton>
#include <QImage>
#include <QVariant>
#include <cmath>
#include <algorithm>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {
bool Categories(V2MainWindow& w) {
    w.RunCommand("file.new");
    if(!DrawRectangleByHand(w))return false;
    std::vector<base::EntityId> wires;for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Wire)wires.push_back(e.id);
    if(wires.empty())return false;
    auto* tree=w.EntityTree();auto* row=w.ItemOfEntity(wires.front());auto* category=row->parent();
    tree->setCurrentItem(row);const auto key=V2EntityTree::KeyOf(row);
    QStyleOptionViewItem option;option.initFrom(tree);option.rect=tree->visualItemRect(category);
    option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=category->checkState(0);
    const QPointF hit=tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,tree).center();
    QMouseEvent press(QEvent::MouseButtonPress,hit,hit,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,hit,hit,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(),&press);QApplication::sendEvent(tree->viewport(),&release);QApplication::processEvents();
    for(const auto id:wires)if(w.Session().GetDocument().FindEntity(id)->visibility!=domain::Visibility::Hidden)return false;
    if(!Explain("category changes preserve row identity and selection",w.ItemOfEntity(wires.front())==row&&V2EntityTree::KeyOf(tree->currentItem())==key))return false;
    w.RunCommand("edit.undo");
    for(const auto id:wires)if(w.Session().GetDocument().FindEntity(id)->visibility!=domain::Visibility::Visible)return false;
    row=w.ItemOfEntity(wires.front());row->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    return Explain("a mixed category is indicated",w.ItemOfEntity(wires.front())->parent()->checkState(0)==Qt::PartiallyChecked||wires.size()==1);
}
bool GroupButtons(V2MainWindow& w) {
    w.RunCommand("file.new");w.Viewport().SetSelection({});w.EntityTree()->clearSelection();w.RunCommand("group.create");
    auto root=w.Session().GetDocument().Snapshot().groups.back().id;
    auto select=[&](base::GroupId id){w.EntityTree()->clearSelection();for(const auto& item:w.GroupItems())if(item.second==id)w.EntityTree()->setCurrentItem(item.first);QApplication::processEvents();};
    select(root);auto* child=w.findChild<QPushButton*>("explorerChildGroup");auto* sibling=w.findChild<QPushButton*>("explorerSiblingGroup");
    if(!child||!sibling||!child->isEnabled())return false;child->click();const auto nested=w.Session().GetDocument().Snapshot().groups.back().id;
    select(nested);sibling->click();const auto peer=w.Session().GetDocument().Snapshot().groups.back();
    if(!Explain("new group is a sibling of the selected nested group",peer.parentId==root))return false;
    const auto hidden=w.Session().GetDocument().Run(document::SetGroupVisibilityCommand(nested,false));if(!hidden.committed)return false;
    select(root);w.RunCommand("view.hide_selected");
    for(const auto& item:w.GroupItems())if(item.second==root)item.first->setCheckState(0,Qt::Checked);
    QApplication::processEvents();
    for(const auto& group:w.Session().GetDocument().Snapshot().groups)if(group.id==nested&&group.visible)return false;
    if(!Explain("showing parent preserves hidden nested group",w.SaveAndReopen("explorer-group-visibility.kcd2")))return false;
    w.RunCommand("file.new");
    return Explain("child group control clears after a new document",!child->isEnabled());
}
bool GroupCategoryScope(V2MainWindow& w) {
    w.RunCommand("file.new");
    const auto a=DrawRectangleAtByHand(w,.1,.2,.25,.4),b=DrawRectangleAtByHand(w,.3,.2,.45,.4),c=DrawRectangleAtByHand(w,.6,.2,.8,.4);
    if(a.IsNil()||b.IsNil()||c.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{a,b}});w.RunCommand("group.create");
    w.Viewport().SetSelection(app::SelectionSet{{c}});w.RunCommand("group.create");
    auto* category=w.ItemOfEntity(a)->parent();
    if(!V2EntityTree::KeyOf(category).startsWith("group:"))return false;
    const auto visible=[&](base::EntityId id){return w.Session().GetDocument().FindEntity(id)->visibility==domain::Visibility::Visible;};
    category->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    if(!Explain("group category hides its own members and preserves other groups",!visible(a)&&!visible(b)&&visible(c)))return false;
    w.RunCommand("edit.undo");if(!Explain("one undo restores both grouped wires",visible(a)&&visible(b)&&visible(c)))return false;
    w.RunCommand("edit.redo");if(visible(a)||visible(b)||!visible(c))return false;
    w.ItemOfEntity(a)->parent()->setCheckState(0,Qt::Checked);QApplication::processEvents();
    return Explain("group category can be shown again",visible(a)&&visible(b)&&visible(c));
}
bool SmoothBoundaryRuns(V2MainWindow& w) {
    using namespace geometry;
    const auto a=CurveSegment::MakeCubicBezier({{0,0,0},{1,0,0},{2,1,1},{3,1,1}}).Value();
    const auto b=CurveSegment::MakeCubicBezier({{3,1,1},{5,1,1},{6,3,0},{7,4,0}}).Value();
    const auto line=CurveSegment::MakeLine({-2,0,0},{0,0,0}).Value();
    const auto seam=CurveSegment::MakeLine({3,1,1},{3,1,1.2}).Value();
    const auto reversed=ReverseCurve(b).Value();
    auto runs=V2GeneratedBoundaryRuns({{line},{reversed},{seam},{a}},1e-6);
    if(!Explain("smooth 3D curves join through a solid seam; tangent line stays separate",runs.size()==3))return false;
    const auto smooth=std::find_if(runs.begin(),runs.end(),V2IsSmoothCurvedWire);
    if(!Explain("curved run retains both exact cubic spans",smooth!=runs.end()&&smooth->size()==2))return false;
    for(const auto& c:*smooth)for(int i=0;i<=20;++i) {
        const auto point=c.Evaluate(i/20.0);
        if(!Explain("grouping leaves every sampled point on its original span",std::min(a.ClosestPoint(point).distance,b.ClosestPoint(point).distance)<1e-7))return false;
    }
    auto sharp=CurveSegment::MakeCubicBezier({{3,1,1},{3,2,1},{4,3,1},{5,3,1}}).Value();
    auto gap=CurveSegment::MakeCubicBezier({{3.0001,1,1},{5,1,1},{6,3,0},{7,4,0}}).Value();
    if(!Explain("corners gaps and ambiguous curved branches remain distinct",
        V2GeneratedBoundaryRuns({{a},{sharp}},1e-6).size()==2
        &&V2GeneratedBoundaryRuns({{a},{gap}},1e-6).size()==2
        &&V2GeneratedBoundaryRuns({{a},{b},{b}},1e-6).size()==3))return false;
    constexpr double pi=3.14159265358979323846;
    std::vector<std::vector<CurveSegment>> circle;
    for(int i=0;i<4;++i)circle.push_back({CurveSegment::MakeCircularArc({},{0,0,1},{1,0,0},3,i*pi/2,pi/2).Value()});
    const auto closed=V2GeneratedBoundaryRuns(circle,1e-6);
    if(!Explain("closed curved boundaries remain closed",closed.size()==1&&closed.front().size()==1&&closed.front().front().Kind()==CurveKind::Circle))return false;
    w.RunCommand("file.new");const auto wire=w.AddPlainWire(*smooth,"境界の曲線",false);
    const auto straight=w.AddPlainWire({line},"境界の直線",false);
    if(wire.IsNil()||straight.IsNil())return false;
    if(!w.SaveAndReopen("smooth-boundary-input.kcd2"))return false;
    auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Isometric);view.FitToDocument();
    w.RunCommand("selection.activate");view.SetSelection({});
    if(!Explain("generated smooth curve is pickable in the rebuilt scene",ClickOnCurveOf(w,wire)&&view.Selection().ordered.size()==1))return false;
    const auto selected=view.Selection().ordered.front();
    if(!Explain("one actual 3D click selects the entire smooth curve only",selected.entityId==wire
        &&selected.kind==app::SelectionElementKind::Object&&!selected.segmentId&&view.Selection().entityIds.size()==1))return false;
    const auto sourceCurves=w.Session().Scene().curves;
    const auto first=std::find_if(sourceCurves.begin(),sourceCurves.end(),[&](const auto& c){return c.entityId==wire;});
    if(first==sourceCurves.end())return false;
    app::SelectionRef span;span.entityId=wire;span.kind=app::SelectionElementKind::Edge;span.segmentId=first->segmentId;
    app::SelectionSet partial;partial.entityIds={wire};partial.ordered={span};view.SetSelection(partial);
    const auto screen=view.Mapping().Project(first->segment.Evaluate(.5));if(!screen)return false;
    view.SelectAt(QPointF(screen->x,screen->y),Qt::ControlModifier);
    if(!Explain("whole-curve Ctrl replaces a preceding single-span reference",view.Selection().ordered.size()==1
        &&view.Selection().ordered.front().kind==app::SelectionElementKind::Object))return false;
    view.SelectAt(QPointF(screen->x,screen->y),Qt::ControlModifier);
    if(!Explain("next Ctrl removes the complete curve without a stale partial selection",view.Selection().entityIds.empty()))return false;
    if(!ClickOnCurveOf(w,wire))return false;
    QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);
    QApplication::sendEvent(&view,&remove);QApplication::processEvents();
    if(!Explain("delete removes only that curve",!w.Session().GetDocument().FindEntity(wire)
        &&w.Session().GetDocument().FindEntity(straight)))return false;
    w.RunCommand("edit.undo");
    if(!Explain("undo and save restore the exact curved wire",w.Session().GetDocument().FindEntity(wire)
        &&w.SaveAndReopen("smooth-thickness-boundary.kcd2")))return false;
    return Explain("whole smooth curve is still pickable after reload",ClickOnCurveOf(w,wire)
        &&view.Selection().ordered.size()==1&&view.Selection().ordered.front().kind==app::SelectionElementKind::Object);
}
bool SmoothMixedWire(V2MainWindow& w) {
    using namespace geometry;
    w.resize(1600,1000);QApplication::processEvents();
    w.RunCommand("file.new");
    const auto line=CurveSegment::MakeLine({-3,0,0},{}).Value();
    const auto a=CurveSegment::MakeCubicBezier({{0,0,0},{1,0,0},{2,1,1},{3,1,1}}).Value();
    const auto b=CurveSegment::MakeCubicBezier({{3,1,1},{5,1,1},{6,3,0},{7,4,0}}).Value();
    const auto tail=CurveSegment::MakeLine({7,4,0},{7,7,0}).Value();
    const auto wire=w.AddPlainWire({line,a,b,tail},"直線と空間曲線",false);
    if(wire.IsNil()||!w.SaveAndReopen("mixed-curve-input.kcd2"))return false;
    const auto* f=w.Session().GetDocument().FindFeature(w.Session().GetDocument().FindEntity(wire)->createdBy);
    const auto source=std::get<domain::CreateWireDefinition>(f->definition);
    auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Isometric);v.FitToDocument();w.RunCommand("selection.activate");v.SetSelection({});
    const auto clickWorld=[&](Vector3 target,Qt::KeyboardModifiers modifiers) {
        const auto screen=v.Mapping().Project(target);if(!screen)return;
        const QPointF point(screen->x,screen->y);
        QMouseEvent move(QEvent::MouseMove,point,point,Qt::NoButton,Qt::NoButton,modifiers);
        QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,modifiers);
        QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,modifiers);
        QApplication::sendEvent(&v,&move);QApplication::sendEvent(&v,&press);QApplication::sendEvent(&v,&release);QApplication::processEvents();
    };
    const auto click=[&](Qt::KeyboardModifiers modifiers){clickWorld(a.Evaluate(.4),modifiers);};
    click(Qt::NoModifier);
    const auto selected=[&](int i){return app::IsCurveSelected(v.Selection(),wire,source.segmentIds[i]);};
    if(!Explain("one click selects both smooth curved spans without either straight segment",!selected(0)&&selected(1)&&selected(2)&&!selected(3)))return false;
    const auto& ink=V2RenderSelectionOf(v);
    if(!Explain("hover ink covers the same curved run only",!ink.HoverCurve(wire,source.segmentIds[0])
        &&ink.HoverCurve(wire,source.segmentIds[1])&&ink.HoverCurve(wire,source.segmentIds[2])&&!ink.HoverCurve(wire,source.segmentIds[3])))return false;
    click(Qt::ControlModifier);if(!Explain("Ctrl toggles the complete curve off",v.Selection().entityIds.empty()))return false;
    v.SetSelection(app::SelectionSet{{wire}});click(Qt::ControlModifier);
    if(!Explain("Ctrl removes only the curved run from a whole-wire selection",selected(0)&&!selected(1)&&!selected(2)&&selected(3)))return false;
    std::optional<app::PickCandidate> exact;
    v.BeginElementPick([](const auto& c){return c.kind==app::SelectionElementKind::Edge;},[&](auto p){exact=p;});
    click(Qt::NoModifier);
    if(!Explain("explicit individual-edge reference pick remains one exact span",exact&&exact->segmentId==source.segmentIds[1]))return false;
    v.SetSelection({});click(Qt::NoModifier);
    QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(&v,&remove);QApplication::processEvents();
    const auto kept=[&](base::SegmentId id,const CurveSegment& original){
        const auto& curves=w.Session().Scene().curves;const auto at=std::find_if(curves.begin(),curves.end(),[&](const auto& c){return c.segmentId==id;});
        return at!=curves.end()&&Distance(at->segment.StartPoint(),original.StartPoint())<1e-12&&Distance(at->segment.EndPoint(),original.EndPoint())<1e-12;};
    if(!Explain("curve deletion preserves the IDs and geometry of both straight segments",kept(source.segmentIds[0],line)&&kept(source.segmentIds[3],tail)))return false;
    w.RunCommand("edit.undo");
    if(!w.SaveAndReopen("mixed-curve-restored.kcd2"))return false;
    w.RunCommand("selection.activate");v.SetViewDirection(ViewDirection::Isometric);v.FitToDocument();
    v.SetSelection({});click(Qt::NoModifier);
    if(!selected(1)||!selected(2)) {
        Note(w.StatusText().toUtf8().constData());
        const auto screen=v.Mapping().Project(a.Evaluate(.4));
        if(screen)Note((std::string("reload curve covered by navigator=")+std::to_string(v.ViewButtonAt(QPointF(screen->x,screen->y)).has_value())).c_str());
    }
    if(!Explain("mixed curve grouping rebuilds after Undo and reload",!selected(0)&&selected(1)&&selected(2)&&!selected(3)))return false;
    v.SetSelection(app::SelectionSet{{wire}});v.SetSnapSuppressed(true);w.SelectTool(modeling::DrawingTool::Copy);
    clickWorld({-5,-5,0},Qt::NoModifier);clickWorld({15,-5,0},Qt::NoModifier);
    if(!Explain("actual basepoint and destination copy the entire mixed wire",CountOfKind(w,domain::EntityKind::Wire)==2))return false;
    base::EntityId copy;for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Wire&&e.id!=wire)copy=e.id;
    w.RunCommand("selection.activate");v.FitToDocument();v.SetSelection({});
    clickWorld(a.Evaluate(.4)+Vector3{20,0,0},Qt::NoModifier);
    auto copied=app::SelectedCurves(v.Selection(),w.Session().Scene());
    if(!Explain("copied geometry uses the same curved-only editing unit",v.Selection().entityIds==std::vector{copy}
        &&copied.size()==2&&std::all_of(copied.begin(),copied.end(),[](const auto& c){return c.Kind()!=CurveKind::Line;})))return false;
    return w.SaveAndReopen("mixed-curve-copy.kcd2");
}
bool Compact(V2MainWindow&) {
    using namespace geometry;
    std::vector<CurveSegment> lines;
    for(int i=0;i<40;++i)lines.push_back(CurveSegment::MakeLine({double(i),0,0},{double(i+1),0,0}).Value());
    lines.push_back(CurveSegment::MakeLine({40,0,0},{40,1,0}).Value());
    const auto simple=V2CompactGeneratedCurves(lines);
    if(!Explain("forty collinear samples become one edge and preserve the corner",simple.size()==2&&Distance(simple[0].EndPoint(),{40,0,0})<1e-12))return false;
    const std::vector<Vector3> controls={{0,0,0},{1,2,1},{2,3,2},{4,3,1},{5,1,0},{7,0,2}};
    std::vector<CurveSegment> spans;for(const auto& p:UniformBSplineBezierSpans(controls))spans.push_back(CurveSegment::MakeCubicBezier(std::vector<Vector3>(p.begin(),p.end())).Value());
    const auto spline=V2CompactGeneratedCurves(spans);
    if(!Explain("equivalent Bezier spans become one spline",spline.size()==1&&spline.front().Kind()==CurveKind::CubicBSpline))return false;
    for(int i=0;i<=100;++i){const double t=i/100.0;const auto index=std::min(spans.size()-1,std::size_t(t*spans.size()));
        if(Distance(spline.front().Evaluate(t),spans[index].Evaluate(t*spans.size()-index))>1e-9)return false;}
    constexpr double pi=3.14159265358979323846;
    const auto a=CurveSegment::MakeCircularArc({},{0,0,1},{1,0,0},3,1,-pi).Value();
    const auto b=CurveSegment::MakeCircularArc({},{0,0,1},{1,0,0},3,1-pi,-pi).Value();
    const auto circle=V2CompactGeneratedCurves({a,b});
    return Explain("joined circular spans retain start and orientation",circle.size()==1&&circle.front().Kind()==CurveKind::Circle
        &&Distance(circle.front().StartPoint(),a.StartPoint())<1e-9&&Dot(circle.front().FirstDerivative(0),a.FirstDerivative(0))>0);
}
}
bool Overlap(V2MainWindow& w) {
    w.resize(1500,950);QApplication::processEvents();
    w.RunCommand("file.new");
    auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Top);view.SetVisibleWidthMm(80);
    for(int i=0;i<2;++i){w.SelectTool(modeling::DrawingTool::Line);
        for(const auto p:{geometry::Vector3{-20,0,0},geometry::Vector3{20,0,0}}){const auto at=view.Mapping().Project(p);if(!at)return false;view.ClickAt(QPointF(at->x,at->y));}}
    w.RunCommand("selection.activate");view.SetSelection({});
    const auto at=view.Mapping().Project({0,0,0});if(!at)return false;view.HoverAt(QPointF(at->x,at->y));
    const auto labels=view.CandidateLabels();if(labels.size()<2)return false;QMenu menu(&w);
    const auto actions=w.BuildSelectMenu(menu,labels);actions.back()->hover();
    if(!Explain("menu hover previews exact overlapping line without committing",view.CandidateIndex()==int(actions.size()-1)&&view.Selection().entityIds.empty()))return false;
    const auto shots=qEnvironmentVariable("KACHACAD_FOLDER_SHOTS");
    if(!shots.isEmpty()){
        QDir directory(shots);directory.mkpath(".");menu.popup(w.mapToGlobal(QPoint(600,300)));QApplication::processEvents();
        const auto hoverPoint=menu.actionGeometry(actions.back()).center();
        QMouseEvent hover(QEvent::MouseMove,hoverPoint,menu.mapToGlobal(hoverPoint),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&menu,&hover);QApplication::processEvents();
        menu.grab().save(directory.filePath("overlap-candidates-menu.png"));w.grab().save(directory.filePath("overlap-candidates-highlight.png"));menu.close();QApplication::processEvents();
    }
    const auto point=QPointF(at->x,at->y);
    view.SetContextMenuCallback([&](const auto&,const auto&){QEvent leave(QEvent::Leave);QApplication::sendEvent(&view,&leave);view.PreviewCandidate(1);return std::optional<int>{1};});
    view.PressRightWithoutMoving(point);if(view.Selection().entityIds.empty())return Explain("leaving the viewport for the candidate menu preserves candidates",false);
    view.SetSelection({});base::EntityId picked;
    view.BeginElementPick([](const auto&){return true;},[&](auto candidate){if(candidate)picked=candidate->entityId;});
    view.PressRightWithoutMoving(point);
    return Explain("candidate menu also reaches the active input role after viewport leave",!picked.IsNil()&&view.Selection().entityIds.empty());
}

namespace {
void SaveInteractionShot(QWidget& widget,const char* name){
    const auto path=qEnvironmentVariable("KACHACAD_FOLDER_SHOTS");if(path.isEmpty())return;
    QDir directory(path);directory.mkpath(".");widget.grab().save(directory.filePath(QString::fromUtf8(name)+".png"));
}
void HumanClick(QWidget* widget,const QPointF& point){
    QMouseEvent press(QEvent::MouseButtonPress,point,widget->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,point,widget->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(widget,&press);QApplication::sendEvent(widget,&release);QEventLoop settle;QTimer::singleShot(15,&settle,&QEventLoop::quit);settle.exec();
}
void HumanKey(QWidget* widget,int key){
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier),release(QEvent::KeyRelease,key,Qt::NoModifier);
    QApplication::sendEvent(widget,&press);QApplication::sendEvent(widget,&release);QEventLoop settle;QTimer::singleShot(15,&settle,&QEventLoop::quit);settle.exec();
}
QPointF CheckPosition(V2EntityTree& tree,QTreeWidgetItem* row){
    QStyleOptionViewItem option;option.initFrom(&tree);option.rect=tree.visualItemRect(row);
    option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=row->checkState(0);
    return tree.style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,&tree).center();
}
bool SelectedVisibilityClicks(V2MainWindow& w){
    w.RunCommand("file.new");w.resize(1500,950);QApplication::processEvents();
    const auto a=DrawRectangleAtByHand(w,.2,.2,.4,.4),b=DrawRectangleAtByHand(w,.6,.2,.8,.4);
    if(a.IsNil()||b.IsNil())return false;w.RunCommand("selection.activate");
    auto* tree=w.EntityTree();auto* row=w.ItemOfEntity(a);tree->scrollToItem(row);QApplication::processEvents();
    HumanClick(tree->viewport(),tree->visualItemRect(row).center());
    const auto selected=w.Viewport().Selection().entityIds;const auto key=V2EntityTree::KeyOf(tree->currentItem());
    if(!Explain("label click selects the intended wire",selected==std::vector<base::EntityId>{a}))return false;
    const auto visible=[&](auto id){return w.Session().GetDocument().FindEntity(id)->visibility==domain::Visibility::Visible;};
    for(const bool expected:{false,true}){
        row=w.ItemOfEntity(a);HumanClick(tree->viewport(),CheckPosition(*tree,row));
        if(!Explain("selected row checkbox changes visibility exactly once",visible(a)==expected&&visible(b)))return false;
        if(!Explain("checkbox preserves exact selection and current row",w.Viewport().Selection().entityIds==selected
            &&V2EntityTree::KeyOf(tree->currentItem())==key))return false;
    }
    // A hand rarely stays on one pixel. A check gesture must not inherit the
    // last label click's Qt rubber-band origin or change any other selection.
    HumanClick(tree->viewport(),tree->visualItemRect(w.ItemOfEntity(b)).center());
    const auto other=w.Viewport().Selection().entityIds;row=w.ItemOfEntity(a);const auto p=CheckPosition(*tree,row);
    QMouseEvent press(QEvent::MouseButtonPress,p,tree->viewport()->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(),&press);
    QMouseEvent move(QEvent::MouseMove,p+QPointF(1,1),tree->viewport()->mapToGlobal((p+QPointF(1,1)).toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(),&move);
    QMouseEvent release(QEvent::MouseButtonRelease,p,tree->viewport()->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(),&release);QApplication::processEvents();
    if(!Explain("tiny checkbox motion cannot select the category",!visible(a)&&visible(b)&&w.Viewport().Selection().entityIds==other))return false;
    SaveInteractionShot(w,"selected-visibility");w.RunCommand("edit.undo");
    return Explain("visibility toggle is one undoable edit",visible(a)&&visible(b));
}
bool GeneratedCategories(V2MainWindow& w){
    w.RunCommand("file.new");
    const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(wire.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{wire}});w.RunCommand("surface.from_lines");HumanKey(&w.Viewport(),Qt::Key_Return);
    base::EntityId source;for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::GuideSurface)source=e.id;
    if(source.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{source}});w.FabricationDock().SetEqualPartCount(1);
    w.RunCommand("fabrication.create");w.RunCommand("fabrication.create");
    base::EntityId model;for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::FabricationModel)model=e.id;
    if(!Explain("a normal plane creates a fabrication model",!model.IsNil()))return false;
    auto* output=w.findChild<QComboBox*>("fabricationGenerationOutput");if(!output)return false;
    for(int kind:{3,1}){
        w.Viewport().SetSelection(app::SelectionSet{{model}});output->setCurrentIndex(kind);w.RunCommand("fabrication.freeze_state");
    }
    std::vector<base::EntityId> wires,faces,parts;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.generatedFrom==model){
        if(e.kind==domain::EntityKind::Wire)wires.push_back(e.id);
        if(e.kind==domain::EntityKind::GuideSurface)faces.push_back(e.id);
        if(e.kind==domain::EntityKind::Part)parts.push_back(e.id);
    }
    if(!Explain("generated fixture includes wire surface and solid outputs",!wires.empty()&&!faces.empty()&&!parts.empty()))return false;
    auto* tree=w.EntityTree();const auto root="entity:"+QString::fromStdString(model.ToString())+"/generated";
    const auto category=[&](int kind){return tree->FindKey(root+"/section:"+QString::number(kind));};
    if(!Explain("generated objects have the same type categories as ordinary groups",category(3)&&category(4)&&category(5)))return false;
    const auto visible=[&](base::EntityId id){return w.Session().GetDocument().FindEntity(id)->visibility==domain::Visibility::Visible;};
    const auto current=w.Session().GetDocument().Snapshot();
    const auto unchangedOthers=[&]{for(const auto& e:current.entities)
        if(std::find(wires.begin(),wires.end(),e.id)==wires.end()
            &&w.Session().GetDocument().FindEntity(e.id)->visibility!=e.visibility)return false;return true;};
    const auto click=[&](QTreeWidgetItem* row){
        if(!row)return false;for(auto* p=row->parent();p;p=p->parent())p->setExpanded(true);
        tree->scrollToItem(row);QApplication::processEvents();HumanClick(tree->viewport(),CheckPosition(*tree,row));return true;};
    auto* selected=w.ItemOfEntity(wires.front());for(auto* p=selected->parent();p;p=p->parent())p->setExpanded(true);
    tree->scrollToItem(selected);QApplication::processEvents();HumanClick(tree->viewport(),tree->visualItemRect(selected).center());
    const auto selection=w.Viewport().Selection().entityIds;
    if(!click(category(3)))return false;
    if(!Explain("wire checkbox preserves selected row other types and source",unchangedOthers()
        &&w.Viewport().Selection().entityIds==selection
        &&std::none_of(wires.begin(),wires.end(),visible)))return false;
    w.RunCommand("edit.undo");if(!Explain("one undo restores all generated wires",std::all_of(wires.begin(),wires.end(),visible)))return false;
    w.RunCommand("edit.redo");if(std::any_of(wires.begin(),wires.end(),visible))return false;
    if(!click(tree->FindKey(root)))return false;
    if(!Explain("mixed generated folder can show its complete contents",std::all_of(wires.begin(),wires.end(),visible)
        &&std::all_of(faces.begin(),faces.end(),visible)&&std::all_of(parts.begin(),parts.end(),visible)))return false;
    if(!click(category(4)))return false;
    return Explain("type visibility survives save and reopen without hiding source or other types",w.SaveAndReopen("generated-categories.kcd2")
        &&visible(source)&&visible(wire)&&std::all_of(wires.begin(),wires.end(),visible)
        &&std::none_of(faces.begin(),faces.end(),visible)&&std::all_of(parts.begin(),parts.end(),visible)
        &&w.RebuildProblems().isEmpty());
}

bool FolderInputs(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(wire.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{wire}});
    auto* newFolder=w.findChild<QPushButton*>("explorerSiblingGroup");if(!newFolder||!newFolder->isVisible())return false;
    HumanClick(newFolder,newFolder->rect().center());
    const auto group=w.Session().GetDocument().Snapshot().groups.back().id;
    if(!Explain("new empty folder does not move the selected wire",!w.Session().GetDocument().FindEntity(wire)->groupId))return false;
    auto* tree=w.EntityTree();const auto key="group:"+QString::fromStdString(group.ToString());
    auto* row=tree->FindKey(key);if(!row)return false;
    w.RunCommand("entity.rename");QApplication::processEvents();
    auto* editor=tree->findChild<QLineEdit*>();if(!Explain("common rename opens the folder label editor",editor&&editor->isVisible()))return false;
    editor->selectAll();QInputMethodEvent text;text.setCommitString(QStringLiteral("ライト"));QApplication::sendEvent(editor,&text);
    HumanKey(editor,Qt::Key_Return);
    Note((std::string("folder name after Enter: ")+w.Session().GetDocument().Snapshot().groups.back().displayName).c_str());
    if(!Explain("Enter commits the folder name",w.Session().GetDocument().Snapshot().groups.back().displayName=="ライト"))return false;
    row=tree->FindKey(key);tree->DropOnto({w.ItemOfEntity(wire)},row);
    row=tree->FindKey(key);if(!row||row->childCount()!=1)return Explain("folder contains only populated category",false);
    tree->setFocus();tree->scrollToItem(row);QApplication::processEvents();
    const bool expanded=row->isExpanded();HumanClick(tree->viewport(),tree->visualItemRect(row).center());
    if(!Explain("one label click toggles the folder without changing visibility",row->isExpanded()!=expanded&&w.Session().GetDocument().Snapshot().groups.back().visible))return false;
    HumanKey(tree,Qt::Key_Right);if(!Explain("right arrow opens the folder",row->isExpanded()))return false;
    HumanKey(tree,Qt::Key_F2);editor=tree->findChild<QLineEdit*>();
    if(!Explain("F2 edits a folder through the keyboard route",editor&&editor->isVisible()))return false;
    HumanKey(editor,Qt::Key_Escape);SaveInteractionShot(w,"folder-open");
    return Explain("folder editing retains the wire",w.Session().GetDocument().FindEntity(wire)!=nullptr);
}
bool WireUnderSurface(V2MainWindow& w){
    const auto trace=[&](const char* phase){
        const auto dir=qEnvironmentVariable("KACHACAD_TOOL_AUDIT_DIR");if(dir.isEmpty())return;
        QFile file(dir+"/wire-under-face.txt");if(!file.open(QIODevice::Append))return;
        file.write(phase);file.write("\n");file.write(w.StatusText().toUtf8());file.write("\n");
        for(const auto& ref:w.Viewport().Selection().ordered)file.write((ref.entityId.ToString()+" kind="+std::to_string(int(ref.kind))+" segment="+(ref.segmentId?ref.segmentId->ToString():"none")+"\n").c_str());
    };
    w.RunCommand("file.new");auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Top);
    const auto wire=DrawRectangleAtByHand(w,.25,.25,.65,.65);if(wire.IsNil())return false;
    view.SetSelection(app::SelectionSet{{wire}});w.RunCommand("surface.from_lines");HumanKey(&w.Viewport(),Qt::Key_Return);
    if(CountOfKind(w,domain::EntityKind::GuideSurface)!=1)return false;
    w.RunCommand("selection.activate");view.SetSelection({});
    geometry::Vector3 point;bool found=false;for(const auto& curve:w.Session().Scene().curves)if(curve.entityId==wire){point=curve.segment.Evaluate(.37);found=true;break;}
    const auto at=view.Mapping().Project(point);if(!found||!at)return false;const QPointF where(at->x,at->y);
    w.RunCommand("surface.from_lines");trace("before click");HumanClick(&view,where);trace("first click");
    if(!Explain("surface edge cannot steal a displayed source wire during face creation",view.Selection().entityIds==std::vector<base::EntityId>{wire}))return false;
    HumanClick(&view,where);trace("second click");if(!Explain("clicking again removes the source wire",view.Selection().entityIds.empty()))return false;
    HumanKey(&w.Viewport(),Qt::Key_Escape);w.RunCommand("surface.create");
    if(!w.SurfaceDock().ClickMethodCard(modeling::GuideSurfaceMethod::PlanarBoundary))return false;
    // Changing docks can resize the viewport. Click the displayed world-space
    // wire, not the old pixel location from the previous tool's layout.
    QApplication::processEvents();const auto current=view.Mapping().Project(point);if(!current)return false;
    HumanClick(&view,QPointF(current->x,current->y));
    trace("advanced click");
    if(!Explain("advanced surface boundary also takes the source wire",view.Selection().entityIds==std::vector<base::EntityId>{wire}))return false;
    SaveInteractionShot(w,"wire-under-face");HumanKey(&w.Viewport(),Qt::Key_Escape);return Explain("both source wire and face remain visible",view.ShapeViews().size()==1&&w.Session().GetDocument().FindEntity(wire)->visibility==domain::Visibility::Visible);
}
bool SurfaceGridChoice(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.25,.25,.65,.65);if(wire.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{wire}});w.RunCommand("surface.from_lines");HumanKey(&w.Viewport(),Qt::Key_Return);w.RunCommand("selection.activate");
    auto& v=w.Viewport();v.SetSelection({});const auto before=w.Session().GetDocument().Snapshot().revision;
    const auto faces=v.ShapeViews().front().mesh.faceCount;const auto triangles=v.ShapeViews().front().mesh.triangles.size();
    auto choice=w.CurrentGridChoice();choice.grid.visible=false;choice.surfaceGridMode=0;w.ApplyGridChoice(choice);QApplication::processEvents();
    SaveInteractionShot(w,"surface-grid-off");const auto off=v.grab().toImage();choice.surfaceGridMode=2;w.ApplyGridChoice(choice);QApplication::processEvents();const auto all=v.grab().toImage();SaveInteractionShot(w,"surface-grid-all");
    if(!Explain("grid display changes pixels without splitting native faces",off!=all&&v.ShapeViews().front().mesh.faceCount==faces&&v.ShapeViews().front().mesh.triangles.size()==triangles))return false;
    choice.surfaceGridMode=1;w.ApplyGridChoice(choice);QApplication::processEvents();const auto selectedOnly=v.grab().toImage();
    if(!Explain("unselected surface does not draw a UV grid in selected-only mode",selectedOnly==off))return false;
    v.SetSelection(app::SelectionSet{{v.ShapeViews().front().entityId}});
    choice.surfaceGridMode=0;w.ApplyGridChoice(choice);QApplication::processEvents();const auto selectedOff=v.grab().toImage();
    choice.surfaceGridMode=1;w.ApplyGridChoice(choice);QApplication::processEvents();const auto selectedGrid=v.grab().toImage();
    choice.surfaceGridMode=2;w.ApplyGridChoice(choice);QApplication::processEvents();const auto selectedAll=v.grab().toImage();
    if(!Explain("selected-only grid matches all-surfaces grid for the selected face",selectedGrid!=selectedOff&&selectedGrid==selectedAll))return false;
    v.SetSelection({});choice.surfaceGridMode=1;w.ApplyGridChoice(choice);QApplication::processEvents();
    w.ShowGridDock();QApplication::processEvents();
    auto* details=w.findChild<QToolButton*>("gridDetails");if(!details)return false;details->setChecked(true);
    auto* combo=w.findChild<QComboBox*>("surfaceGridMode");
    if(!combo||!combo->isVisible())return false;combo->setFocus();HumanKey(combo,Qt::Key_Home);
    return Explain("keyboard grid choice is view-only and synchronized",v.property("surfaceGridMode").toInt()==0&&w.Session().GetDocument().Snapshot().revision==before);
}
bool MeshDisplayCycle(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.25,.25,.65,.65);if(wire.IsNil())return false;
    auto& v=w.Viewport();v.SetSelection(app::SelectionSet{{wire}});w.RunCommand("surface.from_lines");
    HumanKey(&v,Qt::Key_Return);w.RunCommand("selection.activate");
    if(v.ShapeViews().empty())return false;
    auto* row=w.ItemOfEntity(wire);if(!row)return false;row->setCheckState(0,Qt::Unchecked);v.SetSelection({});
    auto choice=w.CurrentGridChoice();choice.grid.visible=false;choice.surfaceGridMode=0;w.ApplyGridChoice(choice);QApplication::processEvents();
    const auto revision=w.Session().GetDocument().Snapshot().revision;const auto before=v.grab().toImage();
    const auto faces=v.ShapeViews().front().mesh.faceCount,triangles=v.ShapeViews().front().mesh.triangles.size();
    auto display=v.DisplaySettingsNow();
    for(int i=0;i<2;++i){
        display.shapesVisible=false;v.SetDisplaySettings(display);QApplication::processEvents();const auto hidden=v.grab().toImage();
        display.shapesVisible=true;v.SetDisplaySettings(display);QApplication::processEvents();const auto restored=v.grab().toImage();
        if(!Explain("surface display cycles restore the exact pixels without drawing duplicate mesh edges",hidden!=before&&restored==before))return false;
    }
    return Explain("surface display keeps native topology and document revision",v.ShapeViews().front().mesh.faceCount==faces
        &&v.ShapeViews().front().mesh.triangles.size()==triangles&&w.Session().GetDocument().Snapshot().revision==revision);
}
bool ConfirmFolderAction(V2MainWindow& w,const char* button){
    bool found=false;QTimer poll,deadline;deadline.setSingleShot(true);
    QObject::connect(&poll,&QTimer::timeout,&poll,[&]{if(auto* dialog=w.findChild<QDialog*>("confirmDependentDelete")){
        poll.stop();found=true;SaveInteractionShot(*dialog,"folder-delete");if(!button)dialog->reject();else if(auto* action=dialog->findChild<QPushButton*>(button))HumanClick(action,action->rect().center());else dialog->reject();}});
    QObject::connect(&deadline,&QTimer::timeout,&deadline,[&]{for(auto* dialog:w.findChildren<QDialog*>())dialog->reject();});
    poll.start(2);deadline.start(2000);w.RunCommand("edit.delete");return found;
}
bool SameMesh(const modeling::ShapeMesh& a,const modeling::ShapeMesh& b){
    if(a.triangles.size()!=b.triangles.size()||a.edges!=b.edges||a.isoLines!=b.isoLines||a.faceCount!=b.faceCount)return false;
    for(std::size_t i=0;i<a.triangles.size();++i){const auto& x=a.triangles[i];const auto& y=b.triangles[i];
        if(x.points!=y.points||x.normal!=y.normal||x.faceIndex!=y.faceIndex||x.vertexNormals!=y.vertexNormals)return false;}
    return true;
}
bool VisibilityKeepsNativeGeometry(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.25,.25,.65,.65);if(wire.IsNil())return false;
    auto& v=w.Viewport();v.SetSelection(app::SelectionSet{{wire}});w.RunCommand("surface.from_lines");
    HumanKey(&v,Qt::Key_Return);w.RunCommand("selection.activate");v.SetSelection({});
    if(v.ShapeViews().empty())return false;
    const auto mesh=v.ShapeViews().front().mesh;
    const auto partitions=v.InteractionCounts().facePartitions;
    const auto curves=w.Session().Scene().curves;
    auto* row=w.ItemOfEntity(wire);auto* tree=w.EntityTree();tree->setCurrentItem(row);
    const auto key=V2EntityTree::KeyOf(row);const auto builds=w.property("explorerBuilds").toInt();
    row->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    if(!Explain("wire checkbox preserves face topology, mesh cache, selected row and tree",w.ItemOfEntity(wire)==row
        &&V2EntityTree::KeyOf(tree->currentItem())==key&&w.property("explorerBuilds").toInt()==builds
        &&v.InteractionCounts().facePartitions==partitions&&SameMesh(v.ShapeViews().front().mesh,mesh)))return false;
    for(const auto& old:curves)if(old.entityId!=wire){const auto found=std::find_if(w.Session().Scene().curves.begin(),w.Session().Scene().curves.end(),[&](const auto& now){return now.entityId==old.entityId&&now.segmentId==old.segmentId;});
        if(found==w.Session().Scene().curves.end()||found->segment.Kind()!=old.segment.Kind()
            ||found->segment.StartPoint()!=old.segment.StartPoint()||found->segment.EndPoint()!=old.segment.EndPoint())return Explain("native edge IDs remain stable",false);}
    w.RunCommand("edit.undo");if(w.Session().GetDocument().FindEntity(wire)->visibility!=domain::Visibility::Visible)return false;
    w.RunCommand("edit.redo");if(w.Session().GetDocument().FindEntity(wire)->visibility!=domain::Visibility::Hidden)return false;
    w.RunCommand("edit.undo");
    return Explain("wire visibility and unchanged native face survive Undo/Redo and reopen",w.SaveAndReopen("visibility-native-cache.kcd2")
        &&SameMesh(v.ShapeViews().front().mesh,mesh));
}
bool FolderDeletion(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(wire.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{wire}});w.RunCommand("group.create");
    const auto group=w.Session().GetDocument().Snapshot().groups.back().id;auto* tree=w.EntityTree();
    const auto select=[&]{tree->clearSelection();tree->setCurrentItem(tree->FindKey("group:"+QString::fromStdString(group.ToString())));QApplication::processEvents();};
    select();if(!ConfirmFolderAction(w,nullptr))return false;
    if(!Explain("canceling folder deletion retains folder and geometry",w.Session().GetDocument().Snapshot().groups.size()==1&&w.Session().GetDocument().FindEntity(wire)))return false;
    select();if(!ConfirmFolderAction(w,"keepGroupContents"))return false;
    if(!Explain("dissolve retains geometry and removes folder",w.Session().GetDocument().Snapshot().groups.empty()&&w.Session().GetDocument().FindEntity(wire)&&!w.Session().GetDocument().FindEntity(wire)->groupId))return false;
    w.RunCommand("edit.undo");select();if(!ConfirmFolderAction(w,"confirmDelete"))return false;
    if(!Explain("delete removes the folder together with its contents",w.Session().GetDocument().Snapshot().groups.empty()&&!w.Session().GetDocument().FindEntity(wire)))return false;
    w.RunCommand("edit.undo");if(!Explain("one undo restores the folder and contents",w.Session().GetDocument().Snapshot().groups.size()==1&&w.Session().GetDocument().FindEntity(wire)))return false;
    w.RunCommand("edit.redo");w.RunCommand("edit.undo");
    return Explain("restored folder survives save/reopen",w.SaveAndReopen("folder-delete.kcd2"));
}
bool RenameDuringTool(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(wire.IsNil())return false;
    w.Viewport().SetSelection(app::SelectionSet{{wire}});w.RunCommand("group.create");
    const auto group=w.Session().GetDocument().Snapshot().groups.back().id;
    w.RunCommand("surface.from_lines");if(!w.LoopFacesTool().Active())return false;
    w.EntityTree()->setCurrentItem(w.EntityTree()->FindKey("group:"+QString::fromStdString(group.ToString())));
    w.RunCommand("entity.rename");QApplication::processEvents();auto* editor=w.EntityTree()->findChild<QLineEdit*>();if(!editor||!editor->isVisible())return false;
    editor->selectAll();QInputMethodEvent text;text.setCommitString(QStringLiteral("窓"));QApplication::sendEvent(editor,&text);HumanKey(editor,Qt::Key_Return);
    Note((std::string("armed rename: guides=")+std::to_string(CountOfKind(w,domain::EntityKind::GuideSurface))+" active="+std::to_string(w.LoopFacesTool().Active())+" name="+w.Session().GetDocument().Snapshot().groups.back().displayName).c_str());
    return Explain("renaming while a tool is active never confirms geometry",CountOfKind(w,domain::EntityKind::GuideSurface)==0&&w.LoopFacesTool().Active()
        &&w.Session().GetDocument().Snapshot().groups.back().displayName=="窓");
}

bool PendingFolderPick(V2MainWindow& w){
    w.RunCommand("file.new");auto& v=w.Viewport();v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(80);
    std::vector<base::EntityId> wires;
    for(const auto& ends:std::vector<std::pair<geometry::Vector3,geometry::Vector3>>{{{-35,-10,0},{-20,-10,0}},{{-35,10,0},{-20,10,0}}}){
        w.RunCommand("draw.line");QApplication::processEvents();
        for(const auto& point:{ends.first,ends.second}){auto at=v.Mapping().Project(point);if(!Explain("folder fixture endpoint projects inside viewport",at&&v.rect().contains(QPoint(at->x,at->y))))return false;HumanClick(&v,{at->x,at->y});}
        w.RunCommand("selection.activate");for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Wire&&std::find(wires.begin(),wires.end(),e.id)==wires.end())wires.push_back(e.id);
    }
    if(!Explain(("folder fixture line count="+std::to_string(wires.size())+": "+w.StatusText().toStdString()).c_str(),wires.size()==2))return false;v.SetSelection(app::SelectionSet{wires});w.RunCommand("group.create");
    const auto group=w.Session().GetDocument().Snapshot().groups.back().id;const auto key="group:"+QString::fromStdString(group.ToString());
    v.SetSelection({});w.EntityTree()->clearSelection();w.RunCommand("wire.coincident");
    auto* row=w.EntityTree()->FindKey(key);if(!row)return false;w.EntityTree()->scrollToItem(row);QApplication::processEvents();
    const auto builds=w.property("explorerBuilds").toInt();HumanClick(w.EntityTree()->viewport(),w.EntityTree()->visualItemRect(row).center());
    return Explain("pending two-wire command may rebuild the tree during a folder click without stale rows",w.EntityTree()->FindKey(key)
        &&w.property("explorerBuilds").toInt()>builds&&w.Session().GetDocument().Snapshot().groups.size()==1);
}
bool MixedFolderDeletion(V2MainWindow& w){
    w.RunCommand("file.new");auto& v=w.Viewport();const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(!Explain("folder input rectangle created",!wire.IsNil()))return false;
    w.RunCommand("draw.point");QApplication::processEvents();const auto at=v.Mapping().Project({-30,-25,0});if(!at)return false;HumanClick(&v,{at->x,at->y});w.RunCommand("selection.activate");
    base::EntityId point;for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Point)point=e.id;
    if(!Explain((std::string("folder input point created: ")+w.StatusText().toStdString()).c_str(),!point.IsNil()))return false;
    v.SetSelection(app::SelectionSet{{wire,point}});w.RunCommand("group.create");const auto group=w.Session().GetDocument().Snapshot().groups.back().id;
    v.SetSelection({});w.RunCommand("surface.create");
    if(!Explain((std::string("surface method card reachable: ")+w.StatusText().toStdString()).c_str(),w.SurfaceDock().ClickMethodCard(modeling::GuideSurfaceMethod::PlanarBoundary)))return false;
    w.EntityTree()->setCurrentItem(w.EntityTree()->FindKey("group:"+QString::fromStdString(group.ToString())));QApplication::processEvents();
    if(!Explain("surface tool keeps the folder row while mirroring only accepted wires",w.EntityTree()->FindKey("group:"+QString::fromStdString(group.ToString()))->isSelected()
        &&std::find(v.Selection().entityIds.begin(),v.Selection().entityIds.end(),point)==v.Selection().entityIds.end()))return false;
    if(!ConfirmFolderAction(w,"confirmDelete"))return false;
    if(!Explain("folder delete uses document membership including filtered points",!w.Session().GetDocument().FindEntity(wire)&&!w.Session().GetDocument().FindEntity(point)
        &&w.Session().GetDocument().Snapshot().groups.empty()))return false;
    w.RunCommand("edit.undo");w.RunCommand("edit.redo");
    return Explain("no dangling folder memberships survive saving",w.SaveAndReopen("folder-filtered-delete.kcd2"));
}

base::GroupId NamedFolder(V2MainWindow& w,const char* name,std::optional<base::GroupId> parent={}){
    w.EntityTree()->clearSelection();w.Viewport().SetSelection({});w.RunCommand("group.create");
    const auto id=w.Session().GetDocument().Snapshot().groups.back().id;
    auto* row=w.EntityTree()->FindKey("group:"+QString::fromStdString(id.ToString()));
    row->setText(0,QString::fromUtf8(name));QApplication::processEvents();
    if(parent)w.DropTreeItemsOnto({w.EntityTree()->FindKey("group:"+QString::fromStdString(id.ToString()))},w.EntityTree()->FindKey("group:"+QString::fromStdString(parent->ToString())));
    return id;
}
bool FolderAncestorDrop(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(wire.IsNil())return false;
    const auto a=NamedFolder(w,"車体"),b=NamedFolder(w,"ライト",a),c=NamedFolder(w,"移動先");
    auto* tree=w.EntityTree();const auto row=[&](base::GroupId id){return tree->FindKey("group:"+QString::fromStdString(id.ToString()));};
    tree->DropOnto({w.ItemOfEntity(wire)},row(b));w.ItemOfEntity(wire)->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    const auto parent=[&](base::GroupId id){for(const auto& g:w.Session().GetDocument().Snapshot().groups)if(g.id==id)return g.parentId;return std::optional<base::GroupId>{};};
    const auto preserved=[&]{const auto* e=w.Session().GetDocument().FindEntity(wire);return parent(b)==a&&e&&e->groupId==b&&e->visibility==domain::Visibility::Hidden;};
    // Qt only accepts a native internal drag after its own drag session. Exercise
    // the production drop notification with the exact selected row set here.
    tree->DropOnto({w.ItemOfEntity(wire),row(b),row(a)},row(c));
    if(!Explain("moving selected parent child and member preserves nested membership",parent(a)==c&&preserved()))return false;
    w.RunCommand("edit.undo");if(!Explain("one undo restores the complete hierarchy",!parent(a)&&preserved()))return false;
    w.RunCommand("edit.redo");if(parent(a)!=c||!preserved())return false;
    tree->DropOnto({row(a),row(b),w.ItemOfEntity(wire)},nullptr);
    if(!Explain("moving the selection to root also preserves nested membership",!parent(a)&&preserved()))return false;
    return Explain("nested multi-selection move survives save and reopen",w.SaveAndReopen("folder-ancestor-move.kcd2")&&preserved());
}
bool FolderMenuStable(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.25,.3,.45,.5),other=DrawRectangleAtByHand(w,.6,.3,.8,.5);
    if(wire.IsNil()||other.IsNil())return false;
    const auto body=NamedFolder(w,"車体"),a=NamedFolder(w,"ライト",body),bogie=NamedFolder(w,"台車"),b=NamedFolder(w,"ライト",bogie);
    auto* tree=w.EntityTree();tree->clearSelection();tree->setCurrentItem(w.ItemOfEntity(wire));QMenu menu(&w);w.BuildMoveToGroupMenu(menu);
    auto* into=menu.actions().front()->menu();if(!into)return false;QAction *left=nullptr,*right=nullptr;
    for(auto* action:into->actions()){if(action->text()==QStringLiteral("車体/ライト"))left=action;if(action->text()==QStringLiteral("台車/ライト"))right=action;}
    if(!Explain("same-named destinations show complete paths",left&&right))return false;
    const auto before=w.Session().GetDocument().Snapshot().revision;into->popup(w.mapToGlobal(QPoint(300,300)));QApplication::processEvents();SaveInteractionShot(*into,"folder-move-paths");HumanKey(into,Qt::Key_Escape);
    if(!Explain("canceling the move menu does not change the document",w.Session().GetDocument().Snapshot().revision==before))return false;
    w.Viewport().SetSelection(app::SelectionSet{{other}});w.RunCommand("group.create");
    const auto otherGroup=w.Session().GetDocument().FindEntity(other)->groupId;
    for(const auto& target:std::vector<std::pair<QAction*,base::GroupId>>{{left,a},{right,b}}){
        into->popup(w.mapToGlobal(QPoint(300,300)));QApplication::processEvents();HumanClick(into,into->actionGeometry(target.first).center());
        if(!Explain("menu resolves stable source and destination after row replacement",w.Session().GetDocument().FindEntity(wire)->groupId==target.second
            &&w.Session().GetDocument().FindEntity(other)->groupId==otherGroup))return false;
    }
    w.RunCommand("edit.undo");if(w.Session().GetDocument().FindEntity(wire)->groupId!=a)return false;w.RunCommand("edit.redo");
    return Explain("menu move history survives save and reopen",w.SaveAndReopen("folder-menu-move.kcd2")&&w.Session().GetDocument().FindEntity(wire)->groupId==b);
}

}

std::vector<SelfTestCase> ExplorerUpdateCases(){return {{"HP-PERF-04 mesh display cycles retain exact surface image",MeshDisplayCycle},
    {"HP-XP-GENERATED type folders preserve other outputs Undo and reload",GeneratedCategories},
    {"HP-XP-06 bulk categories preserve selection and Undo",Categories},
    {"HP-UX-TREE selected checkbox and small pointer motion preserve selection",SelectedVisibilityClicks},
    {"HP-XP-07 sibling buttons and hidden descendant preservation",GroupButtons},{"HP-XP-08 generated curve compaction preserves geometry",Compact},
    {"HP-XP-BOUNDARY smooth thickness curves preserve geometry and whole UI picks",SmoothBoundaryRuns},
    {"HP-XP-MIXED smooth curved spans keep neighboring straight segments editable",SmoothMixedWire},
    {"HP-XP-09 overlap preview and reference-picking roles",Overlap},
    {"HP-XP-10 grouped category visibility remains scoped",GroupCategoryScope},
    {"HP-FP-01 folder mouse keyboard naming and empty creation",FolderInputs},
    {"HP-FP-02 source wires remain pickable under displayed faces",WireUnderSurface},
    {"HP-FP-03 surface grid is optional and keeps native topology",SurfaceGridChoice},
    {"HP-FP-04 folder delete cancel dissolve Undo and save",FolderDeletion},
    {"HP-FP-05 inline rename cannot confirm an armed modeling tool",RenameDuringTool},
    {"HP-FP-06 folder pick remains safe when a pending command rebuilds rows",PendingFolderPick},
    {"HP-FP-07 folder deletion includes members filtered by an active tool",MixedFolderDeletion},
    {"HP-FP-08 ancestor drag keeps nested hierarchy and Undo",FolderAncestorDrop},
    {"HP-FP-09 move menu distinguishes paths and survives row rebuild",FolderMenuStable},
    {"HP-PERF-02 visibility retains native geometry and edge identity",VisibilityKeepsNativeGeometry}};}
}
