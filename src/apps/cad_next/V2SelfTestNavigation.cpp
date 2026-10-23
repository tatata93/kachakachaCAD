#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2RenderSelection.h"
#include "V2NavigatorHighlight.h"
#include "kachakacha/app/ExtrudeDrag.h"
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariant>
#include <array>
#include <cmath>
#include <vector>
#include <iostream>

namespace kachakacha::v2::selftest {
namespace {
void Mouse(V2Viewport& view,QEvent::Type type,QPointF at,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,at,view.mapToGlobal(at.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(&view,&event);
}
void Capture(V2MainWindow& window,const char* name) {
    const auto path=qEnvironmentVariable("KACHACAD_NAV_PROOF_DIR");if(path.isEmpty())return;
    QDir dir(path);dir.mkpath(".");window.grab().save(dir.filePath(QString::fromUtf8(name)+".png"));
}
bool IndexedSelection(V2MainWindow&) {
    base::DeterministicIdGenerator ids(198400);
    std::array<base::EntityId,5> entities;for(auto& id:entities)id=ids.NextTyped<base::IdKind::Entity>();
    std::array<base::SegmentId,4> segments;for(auto& id:segments)id=ids.NextTyped<base::IdKind::Segment>();
    V2RenderSelection index(nullptr);
    for(const auto kind:{app::SelectionElementKind::Object,app::SelectionElementKind::Edge,app::SelectionElementKind::Face,
        app::SelectionElementKind::Vertex,app::SelectionElementKind::ControlPoint,app::SelectionElementKind::WorkPlane}) {
        for(int variant=0;variant<8;++variant) {
            app::SelectionSet selection;app::SelectionRef ref;ref.entityId=entities[0];ref.kind=kind;
            ref.profileSeed=geometry::Vector3{1,2,3};
            if(variant&1)ref.segmentId=segments[0];selection.ordered.push_back(ref);
            ref.entityId=entities[1];ref.kind=app::SelectionElementKind::Edge;ref.segmentId=segments[1];selection.ordered.push_back(ref);
            ref.entityId=entities[2];ref.kind=app::SelectionElementKind::Object;selection.ordered.push_back(ref);
            if(variant&2)selection.entityIds={entities[0],entities[1],entities[3],entities[3]};
            if(variant&4)selection.ordered.push_back(selection.ordered.front());
            index.Update(selection);
            if(index.ProfileSeeds()!=app::SelectedProfileSeeds(selection))return false;
            for(const auto entity:entities) {
                if(index.Entity(entity)!=app::IsSelected(selection,entity))return false;
                for(const auto segment:segments)if(index.Curve(entity,segment)!=app::IsCurveSelected(selection,entity,segment))return false;
            }
        }
    }
    index.Update({});return Explain("empty, partial edges, faces and compatibility IDs exactly match core",!index.Entity(entities[0])&&!index.Curve(entities[0],segments[0]));
}
bool CameraKeepsSelectionIndex(V2MainWindow& window) {
    window.RunCommand("file.new");if(!DrawRectangleByHand(window))return false;
    auto& view=window.Viewport();const auto curve=window.Session().Scene().curves.front();
    view.SetSelection(app::SelectionSet{{curve.entityId}});
    const auto revision=window.Session().GetDocument().Revision();auto& index=V2RenderSelectionOf(view);const auto builds=index.Builds();
    const auto partitions=view.InteractionCounts().facePartitions;
    for(int i=0;i<10;++i){view.OrbitByPixels(2,1);view.PanByPixels(1,-1);view.repaint();}
    if(!Explain("camera changes reuse selection index and shape partitions",index.Builds()==builds
        &&window.Session().GetDocument().Revision()==revision&&view.InteractionCounts().facePartitions==partitions))return false;
    bool nested=false;view.SetSelectionChangedCallback([&]{if(!nested){nested=true;view.SetSelection({});}});
    view.SetSelection(app::SelectionSet{{curve.entityId}});
    return Explain("reentrant selection notifications retain the newest index",view.Selection().entityIds.empty()&&!index.Entity(curve.entityId));
}
bool CompactTargets(V2MainWindow& window) {
    auto& view=window.Viewport();view.setFixedSize(320,240);QApplication::processEvents();const auto layout=view.ViewGadgets();
    if(!Explain("full navigator fits a 320 by 240 viewport with an 88px cube",layout.gadgets.size()==14
        &&layout.widthPx<=210&&layout.heightPx<=240&&view.ViewCubeRect().width()==88))return false;
    for(std::size_t i=0;i<layout.gadgets.size();++i) {
        const auto& g=layout.gadgets[i];
        const QRectF box(g.xPx,g.yPx,g.widthPx,g.heightPx);
        const auto hit=g.kind==view::ViewGadgetKind::AxisRing?view.ViewRingAt(box.center()):view.ViewButtonAt(box.center());
        std::cout<<"navigator target="<<i<<" hit="<<(hit?int(*hit):-1)<<" rect="<<box.x()<<","<<box.y()<<","<<box.width()<<","<<box.height()
            <<" cubeOverlap="<<box.intersects(view.ViewCubeRect())<<" inside="<<view.rect().contains(box.toAlignedRect())<<std::endl;
        if(!Explain("visible buttons and all six ring seats stay clear of cube",hit==i
            &&!box.intersects(view.ViewCubeRect())&&view.rect().contains(box.toAlignedRect())))return false;
        for(std::size_t j=0;j<i;++j) {
            const auto& other=layout.gadgets[j];
            if(box.intersects(QRectF(other.xPx,other.yPx,other.widthPx,other.heightPx)))return false;
        }
    }
    Capture(window,"compact-navigator");return true;
}
bool SafeNavigatorButtons(V2MainWindow& window) {
    auto& viewport=window.Viewport();int aligned=0;viewport.SetAlignSelectionCallback([&]{++aligned;});
    for(const auto kind:{view::ViewGadgetKind::Home,view::ViewGadgetKind::AlignSelection}) {
        viewport.SetViewDirection(ViewDirection::Top);const auto before=viewport.Orientation();
        for(const auto& g:viewport.ViewGadgets().gadgets)if(g.kind==kind) {
            const QPointF at(g.CenterXPx(),g.CenterYPx()),outside=at+QPointF(-160,50);
            viewport.PressViewButton(at,view::AxisArrowModifier::None);viewport.DragViewGadget(outside);viewport.ReleaseViewGadget(outside);
            if(aligned!=0||view::AngleBetween(before,viewport.Orientation())>1e-9)return false;
            viewport.PressViewButton(at,view::AxisArrowModifier::None);viewport.ReleaseViewGadget(outside);
            if(aligned!=0||view::AngleBetween(before,viewport.Orientation())>1e-9)return false;
        }
    }
    return Explain("dragging away never activates Home or align",aligned==0);
}
bool ConsistentNavigatorDrag(V2MainWindow& window) {
    auto& viewport=window.Viewport();viewport.SetViewDirection(ViewDirection::Isometric);const auto initial=viewport.Orientation();
    for(const auto& g:viewport.ViewGadgets().gadgets) {
        if(g.kind!=view::ViewGadgetKind::Orbit&&g.kind!=view::ViewGadgetKind::Roll)continue;
        viewport.SetOrientation(initial);const QPointF at(g.CenterXPx(),g.CenterYPx());
        const bool vertical=g.direction==view::ViewGadgetDirection::Up||g.direction==view::ViewGadgetDirection::Down;
        const auto direction=g.kind==view::ViewGadgetKind::Roll?view::ViewGadgetDirection::Positive
            :(vertical?view::ViewGadgetDirection::Down:view::ViewGadgetDirection::Right);
        const auto expected=view::RotateByScreenAxis(initial,direction,12*view::AxisArrowDegreesPerPixel(view::AxisArrowModifier::None));
        if(!viewport.PressViewButton(at,view::AxisArrowModifier::None))return false;
        viewport.DragViewGadget(at+(vertical?QPointF(0,12):QPointF(12,0)));viewport.ReleaseViewGadget(at+QPointF(12,12));
        if(!Explain("same screen-direction drag has the same rotation on either arrow",expected.HasValue()&&view::AngleBetween(expected.Value(),viewport.Orientation())<1e-7))return false;
        viewport.SetOrientation(initial);viewport.PressViewButton(at,view::AxisArrowModifier::Fine);viewport.ReleaseViewGadget(at);
        if(!Explain("Shift click rotates exactly five degrees",std::abs(view::AngleBetween(initial,viewport.Orientation())*180/3.141592653589793-5)<1e-6))return false;
    }
    return true;
}
bool CubeZonesAndExtrudeOverlap(V2MainWindow& window) {
    auto& viewport=window.Viewport();const auto revision=window.Session().GetDocument().Revision();
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
        if(x==0&&y==0&&z==0)continue;view::ViewCubeZone zone{x,y,z};const auto orientation=view::OrientationForZone(zone);
        if(!orientation.HasValue())return false;viewport.SetOrientation(orientation.Value());
        const auto hit=viewport.ViewCubeZoneAtScreen(viewport.ViewCubeRect().center());
        if(!Explain("all 26 cube regions are reachable in their facing orientation",hit&&*hit==zone))return false;
        QImage image(120,120,QImage::Format_ARGB32);image.fill(Qt::transparent);QPainter painter(&image);
        V2DrawNavigatorHighlight(painter,QRectF(16,16,88,88),orientation.Value(),zone,QColor(240,110,20));painter.end();
        int ink=0;for(int py=0;py<image.height();++py)for(int px=0;px<image.width();++px)if(image.pixelColor(px,py).alpha())++ink;
        if(!Explain("face edge and corner have a directly painted highlight",ink>20))return false;
    }
    viewport.SetViewDirection(ViewDirection::Top);const auto at=viewport.ViewCubeRect().center();
    const auto point=viewport.Mapping().UnprojectOntoPlane({at.x(),at.y()},{0,0,0},{0,0,1});if(!point)return false;
    viewport.ShowExtrudeHandle(app::ExtrudeHandle{*point-geometry::Vector3{5,0,0},{1,0,0},5},{});
    Mouse(viewport,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);
    if(!Explain("visible cube wins over an extrusion handle behind it",viewport.ViewCubeDragging()))return false;
    Mouse(viewport,QEvent::MouseMove,at+QPointF(15,0),Qt::NoButton,Qt::LeftButton);
    Mouse(viewport,QEvent::MouseButtonRelease,at+QPointF(15,0),Qt::LeftButton,Qt::NoButton);
    if(viewport.ExtrudeHandleDistanceMm()!=5||window.Session().GetDocument().Revision()!=revision)return false;
    viewport.HideExtrudeHandle();viewport.SetViewDirection(ViewDirection::Isometric);
    Mouse(viewport,QEvent::MouseMove,viewport.ViewCubeRect().center(),Qt::NoButton,Qt::NoButton);
    Capture(window,"navigator-hover");return true;
}
}
std::vector<SelfTestCase> NavigationCases() {
    return {{"HP-NAV-01 indexed selection matches core semantics",IndexedSelection},
        {"HP-NAV-02 camera and reentrant selection index",CameraKeepsSelectionIndex},
        {"HP-NAV-03 compact navigator exact targets",CompactTargets},
        {"HP-NAV-04 outside release cancels buttons",SafeNavigatorButtons},
        {"HP-NAV-05 consistent drag and fine clicks",ConsistentNavigatorDrag},
        {"HP-NAV-06 cube regions highlights and extrusion overlap",CubeZonesAndExtrudeOverlap}};
}
}
