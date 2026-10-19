#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2EntityTree.h"
#include "V2Viewport.h"
#include "V2GeneratedCurves.h"
#include "V2LoopFacesTool.h"
#include "V2SurfaceDock.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/CurveFit.h"
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
#include <QImage>
#include <QVariant>
#include <cmath>
#include <algorithm>

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
    w.RunCommand("file.new");auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Top);
    const auto wire=DrawRectangleAtByHand(w,.25,.25,.65,.65);if(wire.IsNil())return false;
    view.SetSelection(app::SelectionSet{{wire}});w.RunCommand("surface.from_lines");HumanKey(&w.Viewport(),Qt::Key_Return);
    if(CountOfKind(w,domain::EntityKind::GuideSurface)!=1)return false;
    w.RunCommand("selection.activate");view.SetSelection({});
    geometry::Vector3 point;bool found=false;for(const auto& curve:w.Session().Scene().curves)if(curve.entityId==wire){point=curve.segment.Evaluate(.37);found=true;break;}
    const auto at=view.Mapping().Project(point);if(!found||!at)return false;const QPointF where(at->x,at->y);
    w.RunCommand("surface.from_lines");HumanClick(&view,where);
    if(!Explain("surface edge cannot steal a displayed source wire during face creation",view.Selection().entityIds==std::vector<base::EntityId>{wire}))return false;
    HumanClick(&view,where);if(!Explain("clicking again removes the source wire",view.Selection().entityIds.empty()))return false;
    HumanKey(&w.Viewport(),Qt::Key_Escape);w.RunCommand("surface.create");
    if(!w.SurfaceDock().ClickMethodCard(modeling::GuideSurfaceMethod::PlanarBoundary))return false;
    HumanClick(&view,where);
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
    w.ShowGridDock();QApplication::processEvents();auto* combo=w.findChild<QComboBox*>("surfaceGridMode");
    if(!combo||!combo->isVisible())return false;combo->setFocus();HumanKey(combo,Qt::Key_Home);
    return Explain("keyboard grid choice is view-only and synchronized",v.property("surfaceGridMode").toInt()==0&&w.Session().GetDocument().Snapshot().revision==before);
}
bool ConfirmFolderAction(V2MainWindow& w,const char* button){
    bool found=false;QTimer poll,deadline;deadline.setSingleShot(true);
    QObject::connect(&poll,&QTimer::timeout,&poll,[&]{if(auto* dialog=w.findChild<QDialog*>("confirmDependentDelete")){
        poll.stop();found=true;SaveInteractionShot(*dialog,"folder-delete");if(!button)dialog->reject();else if(auto* action=dialog->findChild<QPushButton*>(button))HumanClick(action,action->rect().center());else dialog->reject();}});
    QObject::connect(&deadline,&QTimer::timeout,&deadline,[&]{for(auto* dialog:w.findChildren<QDialog*>())dialog->reject();});
    poll.start(2);deadline.start(2000);w.RunCommand("edit.delete");return found;
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
    w.RunCommand("file.new");auto& v=w.Viewport();v.SetVisibleWidthMm(80);
    std::vector<base::EntityId> wires;
    for(const auto& ends:std::vector<std::pair<geometry::Vector3,geometry::Vector3>>{{{-20,-5,0},{-5,-5,0}},{{5,5,0},{20,5,0}}}){
        w.RunCommand("draw.line");for(const auto& point:{ends.first,ends.second}){auto at=v.Mapping().Project(point);if(!at)return false;HumanClick(&v,{at->x,at->y});}
        w.RunCommand("selection.activate");for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Wire&&std::find(wires.begin(),wires.end(),e.id)==wires.end())wires.push_back(e.id);
    }
    if(wires.size()!=2)return false;v.SetSelection(app::SelectionSet{wires});w.RunCommand("group.create");
    const auto group=w.Session().GetDocument().Snapshot().groups.back().id;const auto key="group:"+QString::fromStdString(group.ToString());
    v.SetSelection({});w.EntityTree()->clearSelection();w.RunCommand("wire.coincident");
    auto* row=w.EntityTree()->FindKey(key);if(!row)return false;w.EntityTree()->scrollToItem(row);QApplication::processEvents();
    const auto builds=w.property("explorerBuilds").toInt();HumanClick(w.EntityTree()->viewport(),w.EntityTree()->visualItemRect(row).center());
    return Explain("pending two-wire command may rebuild the tree during a folder click without stale rows",w.EntityTree()->FindKey(key)
        &&w.property("explorerBuilds").toInt()>builds&&w.Session().GetDocument().Snapshot().groups.size()==1);
}
bool MixedFolderDeletion(V2MainWindow& w){
    w.RunCommand("file.new");auto& v=w.Viewport();const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(wire.IsNil())return false;
    w.RunCommand("draw.point");const auto at=v.Mapping().Project({25,25,0});if(!at)return false;HumanClick(&v,{at->x,at->y});w.RunCommand("selection.activate");
    base::EntityId point;for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::Point)point=e.id;
    if(point.IsNil())return false;
    v.SetSelection(app::SelectionSet{{wire,point}});w.RunCommand("group.create");const auto group=w.Session().GetDocument().Snapshot().groups.back().id;
    v.SetSelection({});w.RunCommand("surface.create");if(!w.SurfaceDock().ClickMethodCard(modeling::GuideSurfaceMethod::PlanarBoundary))return false;
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

std::vector<SelfTestCase> ExplorerUpdateCases(){return {{"HP-XP-06 bulk categories preserve selection and Undo",Categories},
    {"HP-XP-07 sibling buttons and hidden descendant preservation",GroupButtons},{"HP-XP-08 generated curve compaction preserves geometry",Compact},
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
    {"HP-FP-09 move menu distinguishes paths and survives row rebuild",FolderMenuStable}};}
}
