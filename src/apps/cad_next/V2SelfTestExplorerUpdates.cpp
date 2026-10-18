#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2EntityTree.h"
#include "V2Viewport.h"
#include "V2GeneratedCurves.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/CurveFit.h"
#include <QApplication>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QMouseEvent>
#include <QEvent>
#include <QPointF>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QSignalBlocker>
#include <QMenu>
#include <QAction>
#include <QWidget>
#include <QString>
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
    w.RunCommand("file.new");
    auto& view=w.Viewport();view.SetViewDirection(ViewDirection::Top);view.SetVisibleWidthMm(80);
    for(int i=0;i<2;++i){w.SelectTool(modeling::DrawingTool::Line);
        for(const auto p:{geometry::Vector3{-20,0,0},geometry::Vector3{20,0,0}}){const auto at=view.Mapping().Project(p);if(!at)return false;view.ClickAt(QPointF(at->x,at->y));}}
    w.RunCommand("selection.activate");view.SetSelection({});
    const auto at=view.Mapping().Project({0,0,0});if(!at)return false;view.HoverAt(QPointF(at->x,at->y));
    const auto labels=view.CandidateLabels();if(labels.size()<2)return false;QMenu menu(&w);
    const auto actions=w.BuildSelectMenu(menu,labels);actions.back()->hover();
    if(!Explain("menu hover previews exact overlapping line without committing",view.CandidateIndex()==int(actions.size()-1)&&view.Selection().entityIds.empty()))return false;
    const auto point=QPointF(at->x,at->y);
    view.SetContextMenuCallback([&](const auto&,const auto&){QEvent leave(QEvent::Leave);QApplication::sendEvent(&view,&leave);view.PreviewCandidate(1);return std::optional<int>{1};});
    view.PressRightWithoutMoving(point);if(view.Selection().entityIds.empty())return Explain("leaving the viewport for the candidate menu preserves candidates",false);
    view.SetSelection({});base::EntityId picked;
    view.BeginElementPick([](const auto&){return true;},[&](auto candidate){if(candidate)picked=candidate->entityId;});
    view.PressRightWithoutMoving(point);
    return Explain("candidate menu also reaches the active input role after viewport leave",!picked.IsNil()&&view.Selection().entityIds.empty());
}
std::vector<SelfTestCase> ExplorerUpdateCases(){return {{"HP-XP-06 bulk categories preserve selection and Undo",Categories},
    {"HP-XP-07 sibling buttons and hidden descendant preservation",GroupButtons},{"HP-XP-08 generated curve compaction preserves geometry",Compact},
    {"HP-XP-09 overlap preview and reference-picking roles",Overlap},
    {"HP-XP-10 grouped category visibility remains scoped",GroupCategoryScope}};}
}
