#include "V2ToolSelection.h"
#include "V2SelfTest.h"
#include "V2SelfTestToolbar.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2EntityTree.h"
#include "V2Ribbon.h"
#include "V2RoleInput.h"
#include "V2WorkPlaneDock.h"
#include "V2ExtrudeDock.h"
#include "V2BooleanDock.h"
#include "V2ThickenDock.h"
#include "V2SolidTool.h"
#include "V2ProjectionTool.h"
#include "V2SurfaceDock.h"
#include "V2SurfaceEditTool.h"
#include "V2EdgeFinishTool.h"
#include "V2ShellSplitTool.h"
#include "V2MeasureDock.h"
#include "V2CornerDock.h"
#include "V2DrawingDock.h"
#include "V2FabricationDock.h"
#include "V2LoopFacesTool.h"
#include "V2EndCapInput.h"
#include "V2OutputTool.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/app/Ribbon.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/io/DocumentFile.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QByteArray>
#include <QDialog>
#include <QEvent>
#include <QEventLoop>
#include <QTimer>
#include <QIODevice>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QWidget>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QToolButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QTreeWidgetItem>
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace kachakacha::v2::selftest {
namespace {
#include "../../../tests_v2/fixtures/selection-route-document.inc"
using geometry::Vector3;
base::EntityId Id(const std::string& name) {
    return *base::EntityId::Parse(selectionfixture::aliases.at(name));
}
const std::array<Vector3,3> points{{{-25,-35,6},{0,-30,12},{-10,-10,21}}};
bool Mouse(QWidget& widget,QPointF at,Qt::KeyboardModifiers modifiers=Qt::NoModifier,bool settle=true) {
    if(!widget.isVisible() || !widget.isEnabled())return false;
    QMouseEvent move(QEvent::MouseMove,at,widget.mapToGlobal(at.toPoint()),Qt::NoButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(&widget,&move);
    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,at,QPointF(widget.mapToGlobal(at.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
        QApplication::sendEvent(&widget,&event);
    }
    if(settle)QApplication::processEvents();return true;
}
bool Press(QWidget* widget,bool settle=true) {
    if(!widget)return false;
    for(auto* p=widget->parentWidget();p;p=p->parentWidget())
        if(auto* scroll=dynamic_cast<QScrollArea*>(p))scroll->ensureWidgetVisible(widget);
    if(settle)QApplication::processEvents();
    const auto at=dynamic_cast<QCheckBox*>(widget)?QPoint(8,widget->rect().center().y()):widget->rect().center();
    return Mouse(*widget,at,Qt::NoModifier,settle);
}
bool Key(QWidget& widget,int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    for(auto type:{QEvent::KeyPress,QEvent::KeyRelease}) {
        QKeyEvent event(type,key,modifiers);QApplication::sendEvent(&widget,&event);
    }
    QApplication::processEvents();return true;
}
bool Choose(QComboBox* combo,const QString& text) {
    if(!combo || !combo->isVisible() || !combo->isEnabled())return false;
    const int wanted=combo->findText(text);if(wanted<0)return false;
    for(auto* p=combo->parentWidget();p;p=p->parentWidget())
        if(auto* scroll=dynamic_cast<QScrollArea*>(p))scroll->ensureWidgetVisible(combo);
    combo->setFocus(Qt::TabFocusReason);QApplication::processEvents();
    // Closed-combo arrow keys are a real Qt input path. Sending Escape directly
    // to a combo rather than its popup would incorrectly cancel the CAD tool.
    for(int i=0;i<=combo->count()&&combo->currentIndex()!=wanted;++i)
        Key(*combo,combo->currentIndex()<wanted?Qt::Key_Down:Qt::Key_Up);
    return combo->currentIndex()==wanted;
}
bool Tree(V2MainWindow& w,base::EntityId id,Qt::KeyboardModifiers modifiers=Qt::NoModifier,bool settle=true) {
    auto* tree=w.EntityTree();tree->setFocus();QApplication::processEvents();
    auto* row=w.ItemOfEntity(id);if(!row)return false;
    for(auto* p=row->parent();p;p=p->parent())p->setExpanded(true);
    tree->doItemsLayout();tree->scrollToItem(row,QAbstractItemView::PositionAtCenter);
    QApplication::processEvents();row=w.ItemOfEntity(id);if(!row)return false;
    const auto at=tree->visualItemRect(row).intersected(tree->viewport()->rect()).center();
    return tree->itemAt(at)==row && Mouse(*tree->viewport(),at,modifiers,settle);
}
bool World(V2MainWindow& w,Vector3 point) {
    const auto at=w.Viewport().Mapping().Project(point);if(!at)return false;
    const QPointF pixel(at->x,at->y);
    return w.Viewport().rect().contains(pixel.toPoint()) && !w.Viewport().ViewButtonAt(pixel)
        && Mouse(w.Viewport(),pixel);
}
bool Ribbon(V2MainWindow& w,std::string_view command) {
    for(const auto mode:app::AllUiModes())for(const auto& category:app::RibbonCategoriesFor(mode))
        for(const auto& tool:category.tools)if(tool.commandId==command && !tool.Blocked()) {
            w.SetMode(mode);QApplication::processEvents();
            const auto press=[](QWidget* button){return Press(button);};
            return ClickRibbonItem(w.Ribbon(),"ribbonCategory",QString::fromUtf8(category.labelJa),press)
                && ClickRibbonItem(w.Ribbon(),"ribbonTool",QString::fromUtf8(tool.labelJa),press);
        }
    return false;
}
bool Fixture(V2MainWindow& w) {
    auto directory=std::make_shared<QTemporaryDir>();if(!directory->isValid())return false;
    const auto parsed=io::ReadDocumentJson(selectionfixture::documentJson);
    for(const auto& diagnostic:parsed.Diagnostics())Note((diagnostic.code+" / "+diagnostic.detailsJa).c_str());
    if(!Explain("general selection fixture parses",parsed.HasValue()))return false;
    const auto archive=io::SaveDocument(parsed.Value());if(!archive.HasValue())return false;
    const auto path=directory->filePath("selection-fixture.kcd2");QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromStdString(archive.Value()))<0)return false;
    file.close();QObject::connect(&w,&QObject::destroyed,[directory]{(void)directory;});
    w.resize(1600,1000);QApplication::processEvents();
    if(!w.OpenDocumentFile(path))return Explain("general selection fixture opens",false);
    w.SetWorkPlaneChooser({});w.RunCommand("selection.activate");
    w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().SetViewCenter({});
    w.Viewport().SetVisibleWidthMm(180);w.Viewport().SetSnapSuppressed(false);
    return true;
}
bool HidePart(V2MainWindow& w,const char* alias) {
    auto* tree=w.EntityTree();auto* row=w.ItemOfEntity(Id(alias));if(!row)return false;
    for(auto* p=row->parent();p;p=p->parent())p->setExpanded(true);
    tree->scrollToItem(row);QApplication::processEvents();
    if(row->checkState(0)==Qt::Unchecked)return true;
    QStyleOptionViewItem option;option.initFrom(tree);option.rect=tree->visualItemRect(row);
    option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=row->checkState(0);
    const auto at=tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,tree).center();
    return Mouse(*tree->viewport(),at) && w.ItemOfEntity(Id(alias))->checkState(0)==Qt::Unchecked;
}
V2RoleInput* Role(V2MainWindow& w,const QString& text) {
    for(auto* widget:w.findChildren<QWidget*>())if(auto* role=dynamic_cast<V2RoleInput*>(widget))
        if(role->isVisible() && role->name->text().contains(text))return role;
    return nullptr;
}
bool WorkPlaneRoute(V2MainWindow& w,int index,bool tree) {
    if(!Fixture(w) || !HidePart(w,"part0") || !HidePart(w,"part1") || !Ribbon(w,"workplane.create"))return false;
    const auto& methods=app::WorkPlaneMethods();auto* combo=w.findChild<QComboBox*>("workPlaneMethod");
    if(!Explain("all current workplane modes are covered",methods.size()==12 && combo && combo->count()==12))return false;
    const auto method=methods[index];const auto item=combo->model()->index(index,0);
    if(!(combo->model()->flags(item)&Qt::ItemIsEnabled))
        return Explain("unavailable support methods are disabled with a reason, not passed as selectable inputs",
            !combo->itemData(index,Qt::ToolTipRole).toString().isEmpty());
    if(!Choose(combo,QString::fromUtf8(modeling::WorkPlaneMethodNameJa(method).data(),int(modeling::WorkPlaneMethodNameJa(method).size()))))return false;
    if(w.WorkPlaneDock()->Choice().method!=method)return false;
    const auto revision=w.Session().GetDocument().Revision();const auto needs=app::NeedsOf(method);
    w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().SetVisibleWidthMm(180);
    for(int i=0;i<needs.points;++i) {
        if(!Press(w.findChild<QPushButton*>(QStringLiteral("workPlanePickRole%1").arg(i))) || !w.Viewport().PickPending())return false;
        if(!(tree?Tree(w,Id("point"+std::to_string(i))):World(w,points[i])))return false;
        if(!Explain("point role finishes from the actual chosen source",!w.Viewport().PickPending()))return false;
        const auto choice=w.WorkPlaneDock()->Choice();const auto materials=w.CollectWorkPlaneMaterials(choice);
        const auto picked=method==modeling::WorkPlaneMethod::ThreePoints?choice.threePoints[i]:materials.points.at(i);
        if(!Explain("point keeps its 3D coordinate",geometry::Distance(picked,points[i])<1e-7))return false;
    }
    for(int i=0;i<needs.edges;++i) {
        if(!Press(w.findChild<QPushButton*>(QStringLiteral("workPlanePickRole%1").arg(i+3))))return false;
        if(!(tree?Tree(w,Id(i?"lineB":"lineA")):World(w,i?Vector3{-15,-12.5,0}:Vector3{-25,-20,0})))return false;
        const auto materials=w.CollectWorkPlaneMaterials(w.WorkPlaneDock()->Choice());
        if(!Explain("edge enters its own role without clearing the earlier inputs",!w.Viewport().ReferencePickPending()&&materials.edges.size()==std::size_t(i+1)))return false;
    }
    for(int i=0;i<needs.planes;++i) {
        if(!Press(w.findChild<QPushButton*>(QStringLiteral("workPlanePickRole%1").arg(i+5))))return false;
        const auto id=Id(i?"plane1":"plane0");
        if(!(tree?Tree(w,id):World(w,i?Vector3{55,35,10}:Vector3{55,-30,0})))return false;
        const auto choice=w.WorkPlaneDock()->Choice();
        if(!Explain("reference plane enters the intended role",!w.Viewport().ReferencePickPending()
            &&(i?choice.secondPlaneId:choice.referencePlaneId)==id))return false;
    }
    return Explain("input collection never changes document geometry",w.Session().GetDocument().Revision()==revision);
}
template<std::size_t I> bool WorkPlaneCase(V2MainWindow& w){return WorkPlaneRoute(w,int(I/2),I%2!=0);}
template<std::size_t... I> std::vector<SelfTestCase> WorkPlaneCases(std::index_sequence<I...>) {
    static const std::array<std::string,sizeof...(I)> names{([](std::size_t i){
        return "HP-SELECT-WP "+std::to_string(i/2)+(i%2?" tree role inputs":" viewport role inputs");})(I)...};
    return {{names[I].c_str(),WorkPlaneCase<I>}...};
}
bool CornerCtrl(V2MainWindow& w) {
    if(!Fixture(w) || !Tree(w,Id("lineA")) || !Ribbon(w,"wire.fillet"))return false;
    auto* arm=w.findChild<QPushButton*>("selectionRolePick1");if(!Press(arm))return false;
    if(!Tree(w,Id("lineB"),Qt::ControlModifier) || w.Viewport().ReferencePickPending())return false;
    auto* a=Role(w,QStringLiteral("線 A"));auto* b=Role(w,QStringLiteral("線 B"));
    return Explain("Ctrl tree click supplies the new row, not the already selected other role",
        a&&b&&a->value->text().contains(QStringLiteral("直線A"))&&b->value->text().contains(QStringLiteral("直線B"))&&w.TreeSelectedRowCount()==2);
}
bool TreeModifiers(V2MainWindow& w) {
    if(!Fixture(w) || !Tree(w,Id("profile0")) || !Ribbon(w,"part.extrude"))return false;
    if(!Tree(w,Id("part0")) || !Tree(w,Id("part1"),Qt::ControlModifier))return false;
    const auto selected=[&](const char* alias){const auto& ids=w.Viewport().Selection().entityIds;
        return std::find(ids.begin(),ids.end(),Id(alias))!=ids.end();};
    if(!Explain("Ctrl adds the second object while a tool remains active",selected("part0")&&selected("part1")))return false;
    if(!Tree(w,Id("part0"),Qt::ControlModifier))return false;
    if(!Explain("Ctrl re-click removes only the clicked object",!selected("part0")&&selected("part1")))return false;
    if(!Tree(w,Id("part0"),Qt::ControlModifier|Qt::ShiftModifier))return false;
    return Explain("Ctrl remains additive when Shift is also held",selected("part0")&&selected("part1"));
}
bool GroupToolInput(V2MainWindow& w) {
    if(!Fixture(w)||!Ribbon(w,"wire.spaced_points"))return false;
    const auto& snapshot=w.Session().GetDocument().Snapshot();
    base::EntityId origin;std::vector<base::EntityId> members;
    const auto group=snapshot.groups.back().id;
    for(const auto& entity:snapshot.entities) {
        if(entity.groupId==group)members.push_back(entity.id);
        else if(entity.kind==domain::EntityKind::WorkPlane)origin=entity.id;
    }
    if(origin.IsNil()||members.size()<2||!Tree(w,origin))return false;
    const auto groupClick=[&](Qt::KeyboardModifiers modifiers){
        auto* tree=w.EntityTree();auto* row=tree->FindKey("group:"+QString::fromStdString(group.ToString()));if(!row)return false;
        tree->scrollToItem(row);QApplication::processEvents();const auto at=tree->visualItemRect(row).center();
        return tree->itemAt(at)==row&&Mouse(*tree->viewport(),at,modifiers);
    };
    if(!groupClick(Qt::ControlModifier))return false;
    Note(("group members="+std::to_string(members.size())+" selected="+std::to_string(w.Viewport().Selection().entityIds.size())+" status="+w.StatusText().toStdString()).c_str());
    for(const auto& id:members)if(std::find(w.Viewport().Selection().entityIds.begin(),w.Viewport().Selection().entityIds.end(),id)==w.Viewport().Selection().entityIds.end())Note(("missing group member="+id.ToString()).c_str());
    const auto contains=[&](base::EntityId id){const auto& ids=w.Viewport().Selection().entityIds;
        return std::find(ids.begin(),ids.end(),id)!=ids.end();};
    if(!Explain("Ctrl group input adds every member while preserving a previously selected object",
        contains(origin)&&std::all_of(members.begin(),members.end(),contains)))return false;
    if(!groupClick(Qt::NoModifier)||!Explain("plain group input selects the complete bundle",
        !contains(origin)&&w.Viewport().Selection().entityIds.size()==members.size()))return false;
    if(!groupClick(Qt::ControlModifier)||!Explain("Ctrl group re-click removes the bundle",w.Viewport().Selection().entityIds.empty()))return false;
    if(!Ribbon(w,"workplane.create")||!Choose(w.findChild<QComboBox*>("workPlaneMethod"),QString::fromUtf8("3点を通る"))
        ||!Press(w.findChild<QPushButton*>("workPlanePickRole0"))||!groupClick(Qt::NoModifier))return false;
    return Explain("a single point role never guesses a child from a group",w.Viewport().PickPending());
}
bool PointRetake(V2MainWindow& w) {
    if(!Fixture(w) || !Ribbon(w,"workplane.create") || !Choose(w.findChild<QComboBox*>("workPlaneMethod"),QStringLiteral("3点を通る")))return false;
    for(int role:{0,1,0}) {
        if(!Press(w.findChild<QPushButton*>(QStringLiteral("workPlanePickRole%1").arg(role))) || !Tree(w,Id("point0")))return false;
        if(!Explain("the same tree row can be selected again for a new or replaced role",!w.Viewport().PickPending()
            && geometry::Distance(w.WorkPlaneDock()->Choice().threePoints[role],points[0])<1e-7))return false;
    }
    return true;
}
bool PointRejectAndStale(V2MainWindow& w) {
    if(!Fixture(w) || !Ribbon(w,"workplane.create") || !Choose(w.findChild<QComboBox*>("workPlaneMethod"),QStringLiteral("3点を通る")))return false;
    auto* arm=w.findChild<QPushButton*>("workPlanePickRole0");if(!Press(arm) || !Tree(w,Id("lineA")))return false;
    if(!Explain("whole wire never becomes a guessed endpoint",w.Viewport().PickPending()&&w.StatusText().contains(QStringLiteral("点"))))return false;
    const auto before=w.WorkPlaneDock()->Choice();const auto revision=w.Session().GetDocument().Revision();
    if(!Tree(w,Id("point0"),Qt::NoModifier,false)
        || !Press(w.findChild<QPushButton*>("workPlanePickRole1"),false))return false;
    QApplication::processEvents();
    if(!Explain("queued click cannot enter a newly armed role",w.Viewport().PickPending()
        && w.WorkPlaneDock()->Choice().threePoints==before.threePoints))return false;
    if(!Tree(w,Id("point1"),Qt::NoModifier,false))return false;
    w.RunCommand("draw.line");QApplication::processEvents();
    return Explain("queued click cannot edit a different tool or document",!w.Viewport().PickPending()
        &&w.Session().GetDocument().Revision()==revision&&w.Session().PlacedPointCount()==0);
}
bool GridPoint(V2MainWindow& w) {
    if(!Fixture(w))return false;w.RunCommand("grid.move_origin");
    if(!w.Viewport().PickPending() || !Tree(w,Id("point1")))return false;
    const auto& plane=w.Viewport().WorkPlane();
    const auto expected=plane.PointAt(plane.CoordinateU(points[1]),plane.CoordinateV(points[1]));
    return Explain("tree point supplies the grid reference without guessing a wire end",!w.Viewport().PickPending()
        && geometry::Distance(expected,w.Session().Scene().grid.origin)<1e-7);
}

bool Button(V2MainWindow& w,const QString& text) {
    for(auto* b:w.findChildren<QAbstractButton*>())if(b->isVisible()&&b->text()==text)return Press(b);
    return false;
}
Vector3 Location(const std::string& alias) {
    static const std::map<std::string,Vector3> positions{{"lineA",{-25,-20,0}},{"lineB",{-15,-12.5,0}},
        {"axis",{0,-20,7.5}},{"path",{17.5,-20,7.5}},{"circle",{35,30,0}},
        {"profile0",{-40,-5,0}},{"profile1",{-30,0,0}},{"profile2",{0,20,8}},
        {"guide0",{-45,2.5,0}},{"guide1",{-22,2.5,0}},{"guide2",{0,27.5,8}},
        {"part0",{-45,2.5,5}},{"part1",{-22,2.5,5}},
        {"plane0",{55,-30,0}},{"plane1",{55,35,10}}};
    return positions.at(alias);
}
bool Pick(V2MainWindow& w,const std::string& alias,bool tree) {
    return tree?Tree(w,Id(alias)):World(w,Location(alias));
}
bool RolePick(V2MainWindow& w,const QString& title,const std::string& alias,bool tree) {
    auto* role=Role(w,title);
    if(!Explain(("visible input role: "+title.toStdString()).c_str(),role&&Press(role->arm)))return false;
    if(!Pick(w,alias,tree))return false;
    const auto* entity=w.Session().GetDocument().FindEntity(Id(alias));
    return Explain(("chosen entity enters the visible input: "+title.toStdString()).c_str(),
        entity&&role->value->text().contains(QString::fromStdString(entity->displayName)));
}
bool ProjectionRoute(V2MainWindow& w,int mode,bool tree) {
    if(!Fixture(w)||!Ribbon(w,"wire.project_surface"))return false;
    auto* tool=dynamic_cast<V2ProjectionTool*>(w.findChild<QWidget*>("projectionPanel"));
    auto* combo=w.findChild<QComboBox*>("projectionMode");if(!tool||!combo||combo->count()!=3)return false;
    if(!Choose(combo,combo->itemText(mode))||!Press(w.findChild<QPushButton*>("projectionPickWire"))
        ||!Pick(w,"circle",tree)||tool->WireCount()!=1)return Explain("projection source enters its role",false);
    if(!Press(w.findChild<QPushButton*>("projectionPickTarget"))||!Pick(w,"guide2",tree))return false;
    return Explain("all projection modes keep separate source and support roles",tool->WireCount()==1&&tool->TargetCount()==1);
}
bool BooleanRoute(V2MainWindow& w,int mode,bool tree) {
    static const char* commands[]={"part.boolean_add","part.boolean_cut","part.boolean_intersect",
        "part.trim_overlap","part.split_overlap","part.contact_wire"};
    if(!Fixture(w)||!Ribbon(w,commands[mode]))return false;
    w.Viewport().SetViewDirection(ViewDirection::Top);
    if(!RolePick(w,mode<3?QStringLiteral("土台"):QStringLiteral("加工する対象"),"part0",tree)
        ||!RolePick(w,mode==4?QStringLiteral("切断する"):QStringLiteral("相手"),"part1",tree))return false;
    const auto input=w.BooleanInput();
    if(!Explain("Boolean mode has the exact two independently assigned entities",input.target==Id("part0")
        &&input.tools==std::vector<base::EntityId>{Id("part1")}))return false;
    if(!Explain("both independently chosen inputs remain highlighted in the tree",w.TreeSelectedRowCount()==2))return false;
    auto* role=Role(w,mode==4?QStringLiteral("切断する"):QStringLiteral("相手"));if(!Press(role->clear))return false;
    return Explain("clearing the second Boolean role preserves the first",w.BooleanInput().target==Id("part0")&&w.BooleanInput().tools.empty());
}
bool ThickenRoute(V2MainWindow& w,int mode,bool tree) {
    if(!Fixture(w)||!Ribbon(w,"part.thicken"))return false;
    if(mode<4) {
        const QStringList labels{QStringLiteral("外側"),QStringLiteral("中央"),QStringLiteral("内側"),QStringLiteral("面まで")};
        if(!Button(w,labels[mode]))return false;
    } else if(mode==4) {
        if(!Press(w.findChild<QPushButton*>("thickenCorrectOpposite")))return false;
    } else if(mode==5) {
        if(!Press(w.findChild<QCheckBox*>("thickenIndependent")))return false;
    } else {
        auto* output=w.findChild<QComboBox*>("thickenOutput");
        if(!output||mode-6>=output->count()||!Choose(output,output->itemText(mode-6)))return false;
    }
    if(!RolePick(w,QStringLiteral("厚みを付ける面"),"guide2",tree))return false;
    if(!Explain("thickness source is the chosen surface",w.ThickenInput().surfaces==std::vector<base::EntityId>{Id("guide2")}))return false;
    if(mode==3) {
        if(!RolePick(w,QStringLiteral("終端の面"),"plane1",tree))return false;
        if(!Explain("stop plane preserves the source",w.ThickenInput().targetPlane==Id("plane1")
            &&w.ThickenInput().surfaces==std::vector<base::EntityId>{Id("guide2")}))return false;
    }
    auto* source=Role(w,QStringLiteral("厚みを付ける面"));if(!Press(source->clear))return false;
    return Explain("clearing the source preserves a separately chosen stop",w.ThickenInput().surfaces.empty()
        &&(mode!=3||w.ThickenInput().targetPlane==Id("plane1")));
}
bool SolidRoute(V2MainWindow& w,int variant,bool tree) {
    const int kind=variant/3,option=variant%3;
    static const char* commands[]={"part.revolve","part.loft_solid","part.sweep"};
    if(!Fixture(w)||!Ribbon(w,commands[kind]))return false;
    auto* dock=w.SolidTool().Dock();
    if(kind==0) {
        if(!Button(w,dock->MethodCardTextJa(option)))return false;
    } else if(option>0) {
        if(!Explain("unimplemented solid method is disabled with a reason",!dock->MethodCardEnabled(option)
            &&!dock->MethodCardWhyJa(option).isEmpty()))return false;
        return true;
    }
    auto* profile=Role(w,kind==1?QStringLiteral("断面"):QStringLiteral("輪郭"));
    if(!Explain("solid profile role is reachable",profile&&Press(profile->arm))||!Pick(w,"profile2",tree))return false;
    if(!Explain("solid profile is the clicked whole profile",w.SolidTool().Input().profiles==std::vector<base::EntityId>{Id("profile2")}))return false;
    if(kind==1) {
        if(!Press(profile->arm)||!Pick(w,"profile1",tree))return false;
        return Explain("loft accepts the second section without replacing the first",w.SolidTool().Input().profiles.size()==2);
    }
    if(!RolePick(w,kind==0?QStringLiteral("回転軸"):QStringLiteral("経路"),kind==0?"axis":"path",tree))return false;
    return Explain("axis or path does not replace the solid profile",w.SolidTool().Input().profiles==std::vector<base::EntityId>{Id("profile2")}
        &&(kind==0?w.SolidTool().Input().axis==Id("axis"):
            w.SolidTool().Input().path==std::vector<base::EntityId>{Id("path")}));
}
bool CornerRoute(V2MainWindow& w,int variant,bool tree) {
    static const char* commands[]={"wire.chamfer","wire.fillet","wire.corner_chamfer","wire.corner_fillet"};
    if(!Fixture(w)||!Ribbon(w,commands[variant]))return false;
    if(variant<2)return RolePick(w,QStringLiteral("線 A"),"lineA",tree)&&RolePick(w,QStringLiteral("線 B"),"lineB",tree);
    return RolePick(w,QStringLiteral("加工するワイヤー"),"profile2",tree);
}

bool ExtrudeRoute(V2MainWindow& w,int variant,bool tree) {
    if(!Fixture(w)||!Ribbon(w,"part.extrude"))return false;
    // Each enabled direction and extent is tested with fresh, independently
    // selected inputs. This is axis coverage, not a Cartesian geometry proof.
    if(!RolePick(w,QStringLiteral("押す面"),"profile2",tree))return false;
    auto& dock=w.ExtrudeDock();
    if(variant<7) {
        auto* combo=w.findChild<QComboBox*>("extrudeDirection");
        if(!combo||combo->count()!=7||!Choose(combo,combo->itemText(variant)))return false;
        const auto mode=app::ExtrudeDirections()[variant];
        if(dock.DirectionMode()!=mode)return false;
        if(auto* pick=w.findChild<QPushButton*>("extrudePickDirection");pick&&pick->isVisible()) {
            if(!Press(pick)||!Pick(w,"lineA",tree)||w.Viewport().ReferencePickPending())return false;
        }
    } else if(variant<12) {
        const auto labels=dock.ExtentLabels();QComboBox* combo=nullptr;
        for(auto* c:w.findChildren<QComboBox*>())if(c->isVisible()&&c->count()==5&&c->itemText(0)==labels[0]){combo=c;break;}
        if(!combo||!Choose(combo,combo->itemText(variant-7)))return false;
        if(dock.ExtentMode()==modeling::ExtrudeExtentMode::ToTarget) {
            if(!Press(w.findChild<QPushButton*>("extrudePickStop"))||!Pick(w,"plane1",tree))return false;
            if(!Explain("extrude stop enters separately from the profile",dock.TargetEntityId()==Id("plane1")&&!w.Viewport().ReferencePickPending()))return false;
        }
    } else {
        if(!RolePick(w,QStringLiteral("足す／引く相手"),"part0",tree))return false;
        QComboBox* combo=nullptr;const auto labels=app::ExtrudeBooleans();
        const auto first=app::ExtrudeBooleanNameJa(labels.front());
        for(auto* c:w.findChildren<QComboBox*>())if(c->isVisible()&&c->count()==3&&c->findText(QString::fromUtf8(first.data(),int(first.size())))>=0){combo=c;break;}
        const auto label=app::ExtrudeBooleanNameJa(labels[variant-12]);
        if(!Explain("Boolean option is available after selecting target and profile",combo!=nullptr)
            ||!Choose(combo,QString::fromUtf8(label.data(),int(label.size()))))return false;
    }
    return Explain("mode change preserves the selected extrude profile",w.PlanExtrudeFromSelection().profiles==std::vector<base::EntityId>{Id("profile2")});
}
bool SurfaceRoute(V2MainWindow& w,int methodIndex,bool tree) {
    if(!Fixture(w)||!HidePart(w,"part0")||!HidePart(w,"part1")||!Ribbon(w,"surface.create"))return false;
    std::vector<modeling::GuideSurfaceMethod> methods=app::MainSurfaceMethods();
    const auto other=app::OtherSurfaceMethods();methods.insert(methods.end(),other.begin(),other.end());
    if(!Explain("all surface methods are explicitly enumerated",methods.size()==10))return false;
    const auto method=methods[methodIndex];
    const auto text=app::GuideSurfaceMethodLabelJa(method);bool chosen=false;
    if(methodIndex<int(app::MainSurfaceMethods().size())) {
        for(auto* b:w.findChildren<QPushButton*>())if(b->isVisible()&&b->text().startsWith(QString::fromUtf8(text.data(),int(text.size()))+"\n")){chosen=Press(b);break;}
    } else {
        auto* combo=w.findChild<QComboBox*>("surfaceOtherMethods");chosen=Choose(combo,QString::fromUtf8(text.data(),int(text.size())));
    }
    if(!chosen||w.SurfaceInput().method!=method)return false;
    if(method==modeling::GuideSurfaceMethod::OffsetGuide) {
        if(!Pick(w,"guide2",tree))return false;
        return Explain("offset method receives a surface",w.SurfaceInput().sourceSurfaces==std::vector<base::EntityId>{Id("guide2")});
    }
    // The visible role button establishes exactly where the chosen line goes.
    if(!Pick(w,"lineA",tree))return false;
    const auto& input=w.SurfaceInput();
    const auto contains=[&](const auto& ids){return std::find(ids.begin(),ids.end(),Id("lineA"))!=ids.end();};
    if(!Explain("surface method receives the actual line in one input role",contains(input.sections)||contains(input.boundaries)||contains(input.guides)||contains(input.centerlines)))return false;
    if(!Pick(w,"lineB",tree))return false;
    const auto& next=w.SurfaceInput();
    const auto hasBoth=[&](const auto& ids){return std::find(ids.begin(),ids.end(),Id("lineA"))!=ids.end()&&std::find(ids.begin(),ids.end(),Id("lineB"))!=ids.end();};
    const auto entries=app::AllSurfaceEntries(next);
    return Explain("second surface input preserves the first across independent roles",hasBoth(entries));
}
bool SurfaceEditRoute(V2MainWindow& w,int variant,bool tree) {
    static const char* commands[]={"surface.match","surface.bridge","surface.refit","surface.mirror","surface.iso_curves"};
    if(!Fixture(w)||!HidePart(w,"part0")||!HidePart(w,"part1")||!Ribbon(w,commands[variant]))return false;
    const bool edges=variant<2;
    if(!Pick(w,"guide2",tree))return false;
    const auto& input=w.SurfaceEdit().Input();
    if(edges&&tree)return Explain("whole surface never guesses an edge and clearly asks for the 3D edge",
        input.edges.empty()&&w.StatusText().contains(QStringLiteral("3D")));
    if(edges)return Explain("3D surface edge enters an edge role",!input.edges.empty()&&input.edges.front().surface==Id("guide2")&&input.edges.front().edgeIndex>=0);
    if(!Explain("whole surface enters the surface editing role",input.surfaces==std::vector<base::EntityId>{Id("guide2")}))return false;
    return Pick(w,"guide0",tree)&&Explain("additional surface preserves the earlier input",w.SurfaceEdit().Input().surfaces.size()==2);
}
bool PartDetailRoute(V2MainWindow& w,int variant,bool tree) {
    static const char* commands[]={"part.fillet","part.chamfer","part.shell","part.split"};
    if(!Fixture(w)||!Ribbon(w,commands[variant]))return false;
    w.Viewport().SetViewDirection(ViewDirection::Top);
    if(!RolePick(w,QStringLiteral("① 部品"),"part0",tree))return false;
    if(variant<2) {
        auto* role=Role(w,QStringLiteral("②"));if(!role||!Press(role->arm)||!Pick(w,"part0",tree))return false;
        if(tree)return Explain("tree part is not misread as an arbitrary finishing edge",w.EdgeFinishTool().Input().edges.empty());
        return Explain("3D edge candidate enters the finish role",!w.EdgeFinishTool().Input().edges.empty());
    }
    if(variant==2) {
        auto* role=Role(w,QStringLiteral("②"));if(!role||!Press(role->arm)||!Pick(w,"part0",tree))return false;
        if(tree)return Explain("tree part is not misread as an arbitrary opening face",w.ShellSplitTool().Input().faces.empty());
        return Explain("3D face enters the shell opening role",!w.ShellSplitTool().Input().faces.empty());
    }
    return Explain("split receives the exact part",w.ShellSplitTool().Input().part==Id("part0"));
}
bool TransformRoute(V2MainWindow& w,int variant,bool tree) {
    static const char* commands[]={"wire.move","wire.copy","wire.rotate","wire.mirror","wire.scale",
        "part.move","part.copy","part.rotate","part.mirror","wire.array_linear","wire.array_circular","part.array_linear","part.array_circular"};
    if(!Fixture(w)||!Ribbon(w,commands[variant]))return false;
    const bool part=variant>=5&&variant<=8||variant>=11;
    const char* first=part?"part0":"lineA";const char* second=part?"part1":"lineB";
    w.Viewport().SetViewDirection(ViewDirection::Top);
    auto* role=Role(w,variant>=9?QStringLiteral("並べる対象"):QStringLiteral("移動・複写する対象"));
    if(!role||!Press(role->arm)||!Pick(w,first,tree))return false;
    if(!Press(role->arm))return false;
    if(tree) {if(!Tree(w,Id(second),Qt::ControlModifier))return false;}
    else {const auto projected=w.Viewport().Mapping().Project(Location(second));if(!projected||!Mouse(w.Viewport(),{projected->x,projected->y},Qt::ControlModifier))return false;}
    const auto ids=w.Viewport().Selection().entityIds;
    return Explain("multiple transform/array inputs retain both chosen entities",std::find(ids.begin(),ids.end(),Id(first))!=ids.end()
        &&std::find(ids.begin(),ids.end(),Id(second))!=ids.end());
}
bool ApproxRoute(V2MainWindow& w,int policy,bool tree) {
    if(!Fixture(w)||!Ribbon(w,"fabrication.create"))return false;
    if(!Press(w.findChild<QPushButton*>("fabricationCompareToggle")))return false;
    const auto labels=w.FabricationDock().PolicyLabels();
    if(labels.size()!=4||!Button(w,labels[policy])||!RolePick(w,QStringLiteral("近似する面"),"guide2",tree))return false;
    return Explain("each approximation policy receives the exact source surface",w.ApproxInput().sources==std::vector<base::EntityId>{Id("guide2")}
        &&w.ApproxInput().policy==app::ApproxPolicySpecs()[policy].policy);
}
bool MeasureRoute(V2MainWindow& w,int mode,bool tree) {
    if(!Fixture(w)||!Ribbon(w,"measure.open"))return false;
    QComboBox* combo=nullptr;const auto labels=app::MeasureModes();const auto label=app::MeasureModeNameJa(labels[0]);
    for(auto* c:w.findChildren<QComboBox*>())if(c->isVisible()&&c->count()==5&&c->itemText(0)==QString::fromUtf8(label.data(),int(label.size()))){combo=c;break;}
    if(!combo||!Choose(combo,combo->itemText(mode)))return false;
    const int count=app::MeasurePointCount(labels[mode]);
    if(count) {
        if(tree) {
            if(!Tree(w,Id("point0")))return false;
            return Explain("measurement coordinate mode does not invent a picked location from an object row",w.CurrentMeasureRequest().pickedPoints.empty());
        }
        for(int i=0;i<count;++i)if(!World(w,points[i]))return false;
        return Explain("each measurement point is accepted through an actual 3D click",w.CurrentMeasureRequest().pickedPoints.size()==std::size_t(count));
    }
    if(!Pick(w,mode==4?"circle":"lineA",tree))return false;
    return Explain("object measurement receives the selected curve",!w.CurrentMeasureRequest().curves.empty()&&w.MeasureDock().RowCount()>0);
}
bool DrawingModes(V2MainWindow& w,int tool) {
    static const char* commands[]={"draw.point","draw.line","draw.rectangle","draw.circle","draw.arc","draw.bezier","draw.spline","draw.polyline"};
    if(!Fixture(w)||!Ribbon(w,commands[tool]))return false;
    const auto labels=w.DrawingDock().MethodLabels();
    const std::size_t methods=std::max<std::size_t>(labels.size(),1);
    for(std::size_t m=0;m<methods;++m) {
        if(m>0) {if(!Fixture(w)||!Ribbon(w,commands[tool]))return false;}
        if(!labels.empty()) {
            if(!w.DrawingDock().MethodEnabled(labels[m])) {
                if(!Explain("unavailable drawing mode explains why",!w.DrawingDock().HintText().isEmpty()))return false;
                continue;
            }
            if(!Button(w,labels[m]))return false;
        }
        w.Viewport().SetViewDirection(ViewDirection::Top);w.Viewport().SetSnapSuppressed(true);
        const auto wires=CountOfKind(w,domain::EntityKind::Wire),pointCount=CountOfKind(w,domain::EntityKind::Point);
        const std::array<Vector3,4> p{{{5,5,0},{5.5,5.2,0},{5.5,5.5,0},{6,5.8,0}}};
        const int clicks=tool==0?1:(tool==3&&m==2||tool==4&&m<3)?3:(tool==5||tool==6||tool==7)?4:2;
        for(int i=0;i<clicks;++i)if(!World(w,p[i]))return false;
        if(tool==6||tool==7||tool==1)Key(w.Viewport(),Qt::Key_Return);
        if(!Explain(("actual mouse input creates geometry for "+std::string(commands[tool])+" / "+std::to_string(m)).c_str(),
            tool==0?CountOfKind(w,domain::EntityKind::Point)>pointCount:CountOfKind(w,domain::EntityKind::Wire)>wires))return false;
    }
    return true;
}
template<std::size_t I> bool MoreCase(V2MainWindow& w) {
    constexpr int pair=int(I/2);constexpr bool tree=I%2!=0;
    if constexpr(pair<13)return TransformRoute(w,pair,tree);
    else if constexpr(pair<17)return ApproxRoute(w,pair-13,tree);
    else return MeasureRoute(w,pair-17,tree);
}
template<std::size_t... I> std::vector<SelfTestCase> MoreCases(std::index_sequence<I...>) {
    static const std::array<std::string,sizeof...(I)> names{([](std::size_t i){const auto p=i/2;
        return "HP-SELECT-MORE "+std::string(p<13?"transform-array":p<17?"approximation":"measure")
            +" "+std::to_string(p)+(i%2?" tree":" viewport");})(I)...};
    return {{names[I].c_str(),MoreCase<I>}...};
}
template<std::size_t I> bool DrawingCase(V2MainWindow& w){return DrawingModes(w,int(I));}
template<std::size_t... I> std::vector<SelfTestCase> DrawingCases(std::index_sequence<I...>) {
    static const std::array<std::string,sizeof...(I)> names{("HP-SELECT-DRAW all enabled modes "+std::to_string(I))...};
    return {{names[I].c_str(),DrawingCase<I>}...};
}
template<std::size_t I> bool CurvePointsRoute(V2MainWindow& w) {
    constexpr bool tree=I%2!=0;constexpr int mode=I/2;
    if(!Fixture(w)||!Ribbon(w,"wire.spaced_points"))return false;
    auto* combo=w.findChild<QComboBox*>("curvePointsMode");
    if(!combo||combo->count()!=2||!Choose(combo,combo->itemText(mode)))return false;
    if(!Pick(w,"lineA",tree)||!Pick(w,"lineB",tree))return false;
    // The point preview is intentionally debounced; use the same event loop
    // rather than invoking the tool or inserting points directly.
    QEventLoop loop;QTimer::singleShot(150,&loop,&QEventLoop::quit);loop.exec();
    auto* confirm=w.findChild<QPushButton*>("curvePointsConfirm");
    if(!Explain("both curve-point modes show a usable preview from each selection source",confirm&&confirm->isEnabled()))return false;
    auto* role=Role(w,QString::fromUtf8("点を並べる辺"));if(!role)return false;
    const auto before=role->value->text();
    if(!Press(w.findChild<QToolButton*>("toolEditSelection"))||!Tree(w,Id("circle")))return false;
    QEventLoop paused;QTimer::singleShot(150,&paused,&QEventLoop::quit);paused.exec();
    if(!Explain("edit selection never replaces the curve-point tool input",role->value->text()==before))return false;
    if(!Press(w.findChild<QPushButton*>("toolSelectionResume"))||!Press(w.findChild<QPushButton*>("curvePointsClear")))return false;
    return Explain("clearing curve-point input clears its preview",!confirm->isEnabled());
}
bool DimensionPausedTree(V2MainWindow& w) {
    if(!Fixture(w)||!Ribbon(w,"dimension.open"))return false;
    if(!Press(w.findChild<QPushButton*>("dimensionPickRole0")))return false;
    auto* role=Role(w,QString::fromUtf8("対象 A"));if(!role)return false;
    const auto before=role->value->text();
    if(!Press(w.findChild<QToolButton*>("toolEditSelection"))||!V2ToolSelection::Active(w.Viewport()))return false;
    if(!Tree(w,Id("lineA")))return false;
    if(!Explain("paused dimension ignores ordinary tree selection",role->value->text()==before))return false;
    if(!Press(w.findChild<QPushButton*>("toolSelectionResume"))||V2ToolSelection::Active(w.Viewport()))return false;
    return Explain("resuming keeps the pending dimension role usable",Tree(w,Id("lineA"))&&role->value->text()!=before);
}
bool DimensionRoute(V2MainWindow& w,int mode,bool tree) {
    if(!Fixture(w)||!Ribbon(w,"dimension.open"))return false;
    auto* combo=w.findChild<QComboBox*>("dimensionKind");
    if(!combo||combo->count()!=9||!Choose(combo,combo->itemText(mode)))return false;
    for(int role=0;role<(mode>=5?2:1);++role) {
        const std::string alias=mode==3||mode==4?"circle":role?"lineB":"lineA";
        if(!Press(w.findChild<QPushButton*>(QStringLiteral("dimensionPickRole%1").arg(role))))return false;
        if(tree&&mode==5&&role==1) {if(!Tree(w,Id(alias),Qt::ControlModifier))return false;}
        else if(!Pick(w,alias,tree))return false;
        if(tree&&mode>=6&&!Button(w,QStringLiteral("左ペインの選択を取り込む")))return false;
        auto* input=Role(w,role?QStringLiteral("対象 B"):QStringLiteral("対象 A"));
        if(!Explain("dimension input is visible after the documented tree-import or direct 3D route",input&&input->value->text().contains(
            QString::fromStdString(w.Session().GetDocument().FindEntity(Id(alias))->displayName))))return false;
    }
    return true;
}
bool ExtraSurfaceRoute(V2MainWindow& w,int variant,bool tree) {
    if(!Fixture(w))return false;
    if(variant<2) {
        if(!Ribbon(w,"surface.gpt_create"))return false;
        auto* mode=w.findChild<QComboBox*>("gptSurfaceMethod");if(!mode||mode->count()!=2||!Choose(mode,mode->itemText(variant)))return false;
        if(!Pick(w,"profile2",tree))return false;
        auto* list=w.findChild<QTreeWidget*>("gptSurfaceInputs");
        return Explain("independent surface method receives the selected wire",list&&list->topLevelItemCount()>0);
    }
    if(variant==2) {
        if(!Ribbon(w,"surface.from_lines")||!Pick(w,"profile2",tree))return false;
        if(!w.LoopFacesTool().Plan()||w.LoopFacesTool().Plan()->faces.empty())
            if(!Press(w.findChild<QPushButton*>("loopFacesWholeWires")))return false;
        auto* details=w.findChild<QToolButton*>("loopFacesDetails");
        if(!details||(!details->isChecked()&&!Press(details)))return false;
        auto* methods=w.findChild<QComboBox*>("loopFaceMethod1");
        if(!methods)return false;
        QStringList labels;for(int m=0;m<methods->count();++m)labels.push_back(methods->itemText(m));
        for(const auto& label:labels) {
            methods=nullptr;
            for(auto* c:w.findChildren<QComboBox*>("loopFaceMethod1"))if(c->isVisible()){methods=c;break;}
            if(!Choose(methods,label))return false;
            const auto& plan=w.LoopFacesTool().Plan();
            const auto ids=w.Viewport().Selection().entityIds;
            if(!Explain("loop face methods retain the same selected closed profile",plan&&plan->faces.size()==1
                &&std::find(ids.begin(),ids.end(),Id("profile2"))!=ids.end()))return false;
        }
        return true;
    }
    if(!Ribbon(w,"part.curved_emboss"))return false;
    auto* output=w.findChild<QComboBox*>("embossOutput");if(!output)return false;
    if(variant<6) {if(!Choose(output,output->itemText(variant-3)))return false;}
    else if(!Button(w,variant==6?QStringLiteral("抜き勾配"):QStringLiteral("斜め押し出し")))return false;
    return RolePick(w,QStringLiteral("支持面"),"guide2",tree)&&RolePick(w,QStringLiteral("押し出す輪郭"),"profile2",tree);
}
template<std::size_t I> bool OtherCase(V2MainWindow& w) {
    constexpr int pair=int(I/2);constexpr bool tree=I%2!=0;
    if constexpr(pair<9)return DimensionRoute(w,pair,tree);
    else return ExtraSurfaceRoute(w,pair-9,tree);
}
template<std::size_t... I> std::vector<SelfTestCase> OtherCases(std::index_sequence<I...>) {
    static const std::array<std::string,sizeof...(I)> names{([](std::size_t i){const auto p=i/2;
        return "HP-SELECT-OTHER "+std::string(p<9?"dimension":"surface-emboss")+" "+std::to_string(p)+(i%2?" tree":" viewport");})(I)...};
    return {{names[I].c_str(),OtherCase<I>}...};
}
QComboBox* VisibleCombo(V2MainWindow& w,const char* name) {
    for(auto* c:w.findChildren<QComboBox*>(name))if(c->isVisible())return c;return nullptr;
}
bool EndCapRoute(V2MainWindow& w,int variant,bool tree) {
    const bool extrude=variant<4;const int mode=variant%4;
    if(!Fixture(w)||!Ribbon(w,extrude?"part.extrude":"part.curved_emboss"))return false;
    if(extrude) {if(!RolePick(w,QStringLiteral("押す面"),"profile2",tree))return false;}
    else if(!RolePick(w,QStringLiteral("支持面"),"guide2",tree)||!RolePick(w,QStringLiteral("押し出す輪郭"),"profile2",tree))return false;
    auto* combo=VisibleCombo(w,"endCapMode");if(!combo||combo->count()!=4||!Choose(combo,combo->itemText(mode)))return false;
    auto* cap=dynamic_cast<V2EndCapInput*>(combo->parentWidget());if(!cap)return false;
    if(mode==2) {
        QPushButton* button=nullptr;for(auto* b:w.findChildren<QPushButton*>("endCapPick"))if(b->isVisible()){button=b;break;}
        if(!Press(button)||!Pick(w,"plane1",tree)||w.Viewport().ReferencePickPending())return false;
        if(!Explain("end-cap reference retains its actual plane normal",geometry::Distance(cap->Options().referenceNormal,{0,0,1})<1e-7))return false;
    }
    return Explain("end-cap mode preserves its source input",extrude?w.PlanExtrudeFromSelection().profiles==std::vector<base::EntityId>{Id("profile2")}:
        Role(w,QStringLiteral("押し出す輪郭"))->value->text().contains(QString::fromStdString(w.Session().GetDocument().FindEntity(Id("profile2"))->displayName)));
}
bool OutputPointRoute(V2MainWindow& w,bool tree) {
    if(!Fixture(w)||!Pick(w,"profile2",tree)||!Ribbon(w,"export.selection"))return false;
    V2OutputTool* output=nullptr;for(auto* d:w.findChildren<QDialog*>())if(auto* tool=dynamic_cast<V2OutputTool*>(d);tool&&tool->isVisible()){output=tool;break;}
    auto* combo=VisibleCombo(w,"outputPlacement");if(!output||!combo||!Choose(combo,combo->itemText(1)))return false;
    if(!Button(w,QStringLiteral("基準点を3Dで指定")))return false;
    if(tree) {if(!Tree(w,Id("point1")))return false;}
    else if(!World(w,points[1]))return false;
    for(int i=0;i<3;++i) {
        auto* field=output->findChild<QDoubleSpinBox*>(QStringLiteral("outputSourcePoint_%1").arg(i));
        const double expected=i==0?points[1].x:i==1?points[1].y:points[1].z;
        if(!Explain("output point role uses the exact 3D point from its own input surface",field&&std::abs(field->value()-expected)<1e-7))return false;
    }
    if(!Explain("output point completes and retains the captured model",!w.Viewport().PickPending()&&output->AssetCount()==1))return false;
    output->close();return true;
}
bool OutputFaceOwnership(V2MainWindow& w) {
    if(!Fixture(w)||!Pick(w,"profile2",true)||!Ribbon(w,"export.selection"))return false;
    auto* combo=VisibleCombo(w,"outputPlacement");if(!combo||!Choose(combo,combo->itemText(1)))return false;
    const auto before=kernel::CachedShapeCount();
    for(int i=0;i<4;++i) {
        if(!Button(w,QStringLiteral("元の向きを面から指定"))||!World(w,Location("guide2")))return false;
        if(!Explain("each output face pick releases its temporary native face",kernel::CachedShapeCount()==before))return false;
    }
    for(auto* d:w.findChildren<QDialog*>())if(dynamic_cast<V2OutputTool*>(d))d->close();return true;
}
template<std::size_t I> bool ReferenceCase(V2MainWindow& w) {
    constexpr int pair=int(I/2);constexpr bool tree=I%2!=0;
    if constexpr(pair<8)return EndCapRoute(w,pair,tree);else return OutputPointRoute(w,tree);
}
template<std::size_t... I> std::vector<SelfTestCase> ReferenceCases(std::index_sequence<I...>) {
    static const std::array<std::string,sizeof...(I)> names{([](std::size_t i){const auto p=i/2;
        return "HP-SELECT-REFERENCE "+std::string(p<8?"end-cap":"output-point")+" "+std::to_string(p)+(i%2?" tree":" viewport");})(I)...};
    return {{names[I].c_str(),ReferenceCase<I>}...};
}
template<std::size_t I> bool AdditionalCase(V2MainWindow& w) {
    constexpr int pair=int(I/2);constexpr bool tree=I%2!=0;
    if constexpr(pair<15)return ExtrudeRoute(w,pair,tree);
    else if constexpr(pair<25)return SurfaceRoute(w,pair-15,tree);
    else if constexpr(pair<30)return SurfaceEditRoute(w,pair-25,tree);
    else return PartDetailRoute(w,pair-30,tree);
}
template<std::size_t... I> std::vector<SelfTestCase> AdditionalCases(std::index_sequence<I...>) {
    static const std::array<std::string,sizeof...(I)> names{([](std::size_t i){const auto p=i/2;
        return "HP-SELECT-AXES "+std::string(p<15?"extrude":p<25?"surface":p<30?"surface-edit":"part-detail")
            +" "+std::to_string(p)+(i%2?" tree":" viewport");})(I)...};
    return {{names[I].c_str(),AdditionalCase<I>}...};
}

template<std::size_t I> bool MatrixCase(V2MainWindow& w) {
    constexpr int pair=int(I/2);constexpr bool tree=I%2!=0;
    if constexpr(pair<3)return ProjectionRoute(w,pair,tree);
    else if constexpr(pair<9)return BooleanRoute(w,pair-3,tree);
    else if constexpr(pair<18)return ThickenRoute(w,pair-9,tree);
    else if constexpr(pair<27)return SolidRoute(w,pair-18,tree);
    else return CornerRoute(w,pair-27,tree);
}
template<std::size_t... I> std::vector<SelfTestCase> MatrixCases(std::index_sequence<I...>) {
    static const std::array<std::string,sizeof...(I)> names{([](std::size_t i){
        const auto pair=i/2;const auto family=pair<3?"projection":pair<9?"boolean":pair<18?"thicken":pair<27?"solid":"corner";
        return "HP-SELECT-MATRIX "+std::string(family)+" "+std::to_string(pair)+(i%2?" tree":" viewport");})(I)...};
    return {{names[I].c_str(),MatrixCase<I>}...};
}
}
std::vector<SelfTestCase> SelectionRouteCases() {
    auto cases=WorkPlaneCases(std::make_index_sequence<24>{});
    const auto matrix=MatrixCases(std::make_index_sequence<62>{});cases.insert(cases.end(),matrix.begin(),matrix.end());
    const auto axes=AdditionalCases(std::make_index_sequence<68>{});cases.insert(cases.end(),axes.begin(),axes.end());
    const auto more=MoreCases(std::make_index_sequence<44>{});cases.insert(cases.end(),more.begin(),more.end());
    const auto drawing=DrawingCases(std::make_index_sequence<8>{});cases.insert(cases.end(),drawing.begin(),drawing.end());
    const auto other=OtherCases(std::make_index_sequence<34>{});cases.insert(cases.end(),other.begin(),other.end());
    const auto references=ReferenceCases(std::make_index_sequence<18>{});cases.insert(cases.end(),references.begin(),references.end());
    cases.insert(cases.end(),{{"HP-SELECT corner Ctrl tree role preserves first line",CornerCtrl},
        {"HP-SELECT tree point retake and same row reuse",PointRetake},
        {"HP-SELECT active tool tree modifiers preserve Ctrl and Shift intent",TreeModifiers},
        {"HP-SELECT paused dimension retains its input during tree selection",DimensionPausedTree},
        {"HP-SELECT group bundles accept tool input without guessing single roles",GroupToolInput},
        {"HP-SELECT spaced points pitch viewport",CurvePointsRoute<0>},
        {"HP-SELECT spaced points pitch tree",CurvePointsRoute<1>},
        {"HP-SELECT spaced points divisions viewport",CurvePointsRoute<2>},
        {"HP-SELECT spaced points divisions tree",CurvePointsRoute<3>},
        {"HP-SELECT point invalid input stale click and tool transition",PointRejectAndStale},
        {"HP-SELECT grid origin accepts explicit tree point",GridPoint},
        {"HP-SELECT output surface reference picks release temporary faces",OutputFaceOwnership}});
    return cases;
}
}
