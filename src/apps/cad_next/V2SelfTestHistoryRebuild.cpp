#include "V2SelfTest.h"
#include "V2SelfTestPrimitiveUi.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2EditDock.h"
#include "V2LoopFacesTool.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Pnt.hxx>
#include <QPointF>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace kachakacha::v2::selftest {
namespace {
using geometry::Vector3;
struct Bounds { Vector3 lo, hi; };

std::optional<Bounds> BoundsOf(V2MainWindow& w, base::EntityId id)
{
    for (const auto& view : w.Viewport().ShapeViews())
        if (view.entityId == id && !view.surface && !view.mesh.Empty())
            return Bounds{view.mesh.minimum, view.mesh.maximum};
    return {};
}

double BoxVolume(const Bounds& b)
{
    return (b.hi.x-b.lo.x)*(b.hi.y-b.lo.y)*(b.hi.z-b.lo.z);
}

double VisibleVolume(V2MainWindow& w)
{
    double result=0;
    for (const auto& view : w.Viewport().ShapeViews()) {
        if (view.surface) continue;
        double volume=0;
        for (const auto& triangle : view.mesh.triangles) {
            const auto& a=triangle.points[0];
            const auto& b=triangle.points[1];
            const auto& c=triangle.points[2];
            volume+=(a.x*(b.y*c.z-b.z*c.y)+a.y*(b.z*c.x-b.x*c.z)
                +a.z*(b.x*c.y-b.y*c.x))/6;
        }
        result+=std::abs(volume);
    }
    return result;
}

// A disposable vertex observes the monotonic native allocation sequence. It is
// never added to the document, selected, or used in modeling. A pure removal
// should allocate no replacement native shapes for the three unchanged boxes.
std::uint64_t AllocationMark()
{
    const auto mark=kernel::StoreShape(BRepBuilderAPI_MakeVertex(gp_Pnt()).Shape());
    kernel::ReleaseShape(mark);
    return mark.value;
}
}

bool HistoryRetainsUnchangedParts(V2MainWindow& w)
{
    w.RunCommand("file.new");
    const auto first=MakeBoxByHand(w,.25,.35,.45,.65);
    const auto second=MakeBoxByHand(w,.40,.35,.60,.65);
    const auto third=MakeBoxByHand(w,.72,.35,.87,.65);
    const auto a=BoundsOf(w,first), b=BoundsOf(w,second), c=BoundsOf(w,third);
    if (!Explain("three actual UI boxes exist",a&&b&&c)) return false;
    const Bounds overlap{{std::max(a->lo.x,b->lo.x),std::max(a->lo.y,b->lo.y),std::max(a->lo.z,b->lo.z)},
        {std::min(a->hi.x,b->hi.x),std::min(a->hi.y,b->hi.y),std::min(a->hi.z,b->hi.z)}};
    const double separate=BoxVolume(*a)+BoxVolume(*b)+BoxVolume(*c);
    const double united=separate-BoxVolume(overlap);
    if (!Explain("fixture has a positive overlapping region",BoxVolume(overlap)>0)) return false;
    w.Viewport().SelectAt(QPointF(2,2),Qt::NoModifier);
    w.RunCommand("part.boolean_add");
    if (!PressPart(w,first)||!PressPart(w,second)
        ||!w.HandleToolKey(Qt::Key_Return,nullptr)
        ||!Explain("union adds exactly one part",CountOfKind(w,domain::EntityKind::Part)==4)) return false;
    const auto equalVolume=[&](double expected) {
        const double actual=VisibleVolume(w);
        Note(("box mesh volume="+std::to_string(actual)+" expected="+std::to_string(expected)).c_str());
        return Explain("analytic cube volume is preserved",std::abs(actual-expected)<1e-6*expected);
    };
    if (!equalVolume(united)) return false;
    const auto committedNativeCount=kernel::CachedShapeCount();
    for (int repeat=0;repeat<4;++repeat) {
        const auto before=AllocationMark();
        w.RunCommand("edit.undo");
        const auto after=AllocationMark();
        Note(("native allocations during Undo="+std::to_string(after-before-1)).c_str());
        if (!Explain("Undo removes only the new output",CountOfKind(w,domain::EntityKind::Part)==3
                &&w.KernelShapeCount()==3&&w.Viewport().ShapeViewCount()==3
                &&kernel::CachedShapeCount()+1==committedNativeCount)
            ||!equalVolume(separate)
            ||!Explain("Undo does not recreate unchanged native boxes",after==before+1)) return false;
        w.RunCommand("edit.redo");
        const auto unchanged=BoundsOf(w,third);
        if (!Explain("Redo restores union and unrelated part",CountOfKind(w,domain::EntityKind::Part)==4
                &&w.KernelShapeCount()==4&&w.Viewport().ShapeViewCount()==2
                &&kernel::CachedShapeCount()==committedNativeCount)
            ||!equalVolume(united)
            ||!Explain("unrelated part keeps its bounds",unchanged
                &&(unchanged->lo-c->lo).Length()<1e-9
                &&(unchanged->hi-c->hi).Length()<1e-9)) return false;
    }
    return true;
}

bool HistoryRebuildsModifiedSource(V2MainWindow& w)
{
    w.RunCommand("file.new");
    const auto part=MakeBoxByHand(w,.3,.35,.6,.65);
    const auto original=BoundsOf(w,part);
    const auto* entity=w.Session().GetDocument().FindEntity(part);
    const auto* feature=entity ? w.Session().GetDocument().FindFeature(entity->createdBy) : nullptr;
    const auto* extrude=feature ? std::get_if<domain::ExtrudeDefinition>(&feature->definition) : nullptr;
    if (!original||!extrude||extrude->profiles.empty()) return false;
    const auto profile=extrude->profiles.front();
    if (!w.SelectTreeRowForEntity(profile)) return false;
    w.RunCommand("edit.numeric");
    auto& dock=w.EditDock();
    const auto fields=dock.WireFields();
    if (!Explain("source rectangle has editable point fields",dock.IsShowingWire()&&fields.points.size()>=4)) return false;
    for (std::size_t i=0;i<fields.points.size();++i) {
        auto point=fields.points[i];point.x*=1.25;
        dock.SetPoint(static_cast<int>(i),point);
    }
    // Spin boxes have finite decimal precision. Derive the expected area from
    // the actual entered values, not an unattainable exact 1.25 scale factor.
    const auto entered=dock.WireFields().points;
    double twiceArea=0;
    for (std::size_t i=0;i<entered.size();++i) {
        const auto& a=entered[i];const auto& b=entered[(i+1)%entered.size()];
        twiceArea+=a.x*b.y-b.x*a.y;
    }
    const double editedVolume=.5*std::abs(twiceArea)*(original->hi.z-original->lo.z);
    const auto revision=w.Session().GetDocument().Revision();
    dock.PressApply();
    if (!Explain("source edit commits through the numeric panel",w.Session().GetDocument().Revision()!=revision)) return false;
    w.RunCommand("selection.activate");
    for (bool undo:{true,false,true,false}) {
        const auto before=AllocationMark();
        w.RunCommand(undo ? "edit.undo" : "edit.redo");
        const auto after=AllocationMark();
        const double expected=undo ? BoxVolume(*original) : editedVolume;
        Note((std::string(undo ? "source Undo" : "source Redo")+" volume="+std::to_string(VisibleVolume(w))
            +" expected="+std::to_string(expected)+" native allocations="+std::to_string(after-before-1)
            +" status="+w.StatusText().toStdString()).c_str());
        if (!Explain("edited source forces a fresh native solid",after>before+1)
            ||!Explain("edited profile Undo Redo has the expected solid volume",
                std::abs(VisibleVolume(w)-expected)<expected*1e-6)
            ||!Explain("source edit history keeps one live document solid",w.KernelShapeCount()==1
                &&w.RebuildProblems().isEmpty())) return false;
    }
    return true;
}

namespace {
bool MousePoint(V2MainWindow& w,Vector3 point)
{
    auto& v=w.Viewport();const auto pixel=v.Mapping().Project(point);if(!pixel)return false;
    const QPointF local(pixel->x,pixel->y);v.setFocus();
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,local,QPointF(v.mapToGlobal(local.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&v,&event);
    }
    QApplication::processEvents();return true;
}

void HistoryKey(V2MainWindow& w,int key,Qt::KeyboardModifiers modifiers=Qt::ControlModifier)
{
    w.activateWindow();w.Viewport().setFocus();QApplication::processEvents();
    for(const auto type:{QEvent::KeyPress,QEvent::KeyRelease}) {
        QKeyEvent event(type,key,modifiers);QApplication::sendEvent(&w.Viewport(),&event);
    }
    QApplication::processEvents();
}

bool CircleByMouse(V2MainWindow& w,double radius)
{
    w.SelectTool(modeling::DrawingTool::Circle);
    return MousePoint(w,{10,0,0})&&MousePoint(w,{10+radius,0,0});
}

std::vector<base::EntityId> SurfaceIds(V2MainWindow& w)
{
    std::vector<base::EntityId> result;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::GuideSurface)result.push_back(e.id);
    return result;
}
}

bool HistoryRetainsUnchangedSurfaces(V2MainWindow& w)
{
    w.RunCommand("file.new");w.resize(1500,900);w.show();QApplication::processEvents();
    if(MakeBoxByHand(w,.18,.30,.32,.45).IsNil())return false;
    w.RunCommand("view.top");
    if(!CircleByMouse(w,15)||!CircleByMouse(w,10))return false;
    std::vector<base::EntityId> wires;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::Wire&&e.displayName.find("円")!=std::string::npos)wires.push_back(e.id);
    if(!Explain("two circles drawn by actual mouse events",wires.size()==2))return false;
    const auto select=[&] {app::SelectionSet selection;selection.entityIds=wires;w.Viewport().SetSelection(selection);};
    const auto make=[&] {
        w.RunCommand("selection.activate");select();w.RunCommand("surface.from_lines");
        HistoryKey(w,Qt::Key_Return,Qt::NoModifier);
        return !SurfaceIds(w).empty()&&w.RebuildProblems().isEmpty();
    };
    if(!make())return false;
    const auto first=SurfaceIds(w).front();
    double area=0;
    for(const auto& view:w.Viewport().ShapeViews())if(view.entityId==first)
        for(const auto& t:view.mesh.triangles)area+=geometry::Cross(t.points[1]-t.points[0],t.points[2]-t.points[0]).Length()*.5;
    if(!Explain("surface keeps the inner circular opening",std::abs(area-125*std::acos(-1.0))<4))return false;
    // Add a second, overlapping surface so Undo must retain an existing guide
    // and a solid, with their original native edges, not just an empty document.
    if(!make()||SurfaceIds(w).size()!=2)return false;
    std::vector<modeling::SnapCurve> retained;
    for(const auto& c:w.Session().Scene().curves)if(c.entityId==first)retained.push_back(c);
    const auto committedCount=kernel::CachedShapeCount();
    for(int repeat=0;repeat<3;++repeat) {
        const auto before=AllocationMark();HistoryKey(w,Qt::Key_Z);const auto after=AllocationMark();
        if(!Explain("surface Undo allocates no replacement native shapes",after==before+1)
            ||!Explain("only the latest surface is removed",SurfaceIds(w).size()==1
                &&SurfaceIds(w).front()==first&&CountOfKind(w,domain::EntityKind::Part)==1
                &&w.Viewport().ShapeViewCount()==2&&w.RebuildProblems().isEmpty()))return false;
        for(const auto& old:retained)if(!Explain("unrelated surface keeps native edge identities",
            std::any_of(w.Session().Scene().curves.begin(),w.Session().Scene().curves.end(),
                [&](const auto& c){return c.entityId==old.entityId&&c.segmentId==old.segmentId;})))return false;
        HistoryKey(w,Qt::Key_Y);
        if(!Explain("Redo restores the annular surface without native leaks",SurfaceIds(w).size()==2
            &&kernel::CachedShapeCount()==committedCount&&w.RebuildProblems().isEmpty()))return false;
    }
    HistoryKey(w,Qt::Key_Z);
    if(!make()||SurfaceIds(w).size()!=2||w.Session().GetDocument().CanRedo())return false;
    select();w.RunCommand("surface.from_lines");
    QApplication::processEvents(); // Finish the preview before measuring Undo allocations.
    if(!w.LoopFacesTool().Active())return false;
    const auto before=AllocationMark();HistoryKey(w,Qt::Key_Z);const auto after=AllocationMark();
    Note(("preview Undo: active="+std::to_string(w.LoopFacesTool().Active())
        +" previews="+std::to_string(w.Viewport().ToolPreview().size())+" surfaces="+std::to_string(SurfaceIds(w).size())
        +" native allocations="+std::to_string(after-before-1)).c_str());
    if(!Explain("history cancels the active surface preview before changing native ownership",
        !w.LoopFacesTool().Active()&&w.Viewport().ToolPreview().empty()&&SurfaceIds(w).size()==1
        &&after==before+1&&w.RebuildProblems().isEmpty()))return false;
    if(!make()||SurfaceIds(w).size()!=2)return false;
    return w.SaveAndReopen("surface-history-ring.kcd2")&&SurfaceIds(w).size()==2
        &&w.Viewport().ShapeViewCount()==3&&w.RebuildProblems().isEmpty();
}
}
