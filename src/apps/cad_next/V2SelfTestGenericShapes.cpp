#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2DrawingDock.h"
#include "V2SurfaceDock.h"
#include "V2BooleanDock.h"
#include "V2ThickenDock.h"
#include "V2LoopFacesTool.h"
#include "V2LoopFacesDock.h"
#include "V2OutputTool.h"
#include "V2DocumentOpen.h"
#include "V2SolidTool.h"
#include "V2SolidDock.h"
#include "V2ExtrudeDock.h"
#include "V2ParameterDock.h"
#include "V2PartDock.h"
#include "V2EntityTree.h"
#include "V2Ribbon.h"
#include "kachakacha/app/Ribbon.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/modeling/MeshPick.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QPixmap>
#include <QLineEdit>
#include <QFocusEvent>
#include <QChar>
#include <QPointF>
#include <QString>
#include <QWidget>
#include <QToolButton>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Shape.hxx>
#endif

namespace kachakacha::v2::selftest {
namespace {
using base::EntityId;
using domain::EntityKind;
using geometry::Vector3;
bool ToolButton(V2MainWindow& w,std::string_view command);

void Reveal(QWidget& widget)
{
    for(auto* parent=widget.parentWidget();parent;parent=parent->parentWidget())
        if(auto* scroll=dynamic_cast<QScrollArea*>(parent))scroll->ensureWidgetVisible(&widget);
    QApplication::processEvents();
}

bool Mouse(QWidget& widget,QPointF point,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    // 3Dの位置は PickWire/PickShape が配置確定後に求めている。ここで
    // 再度イベントを処理すると、下見済みの位置を次の配置へ持ち越してしまう。
    if(!dynamic_cast<V2Viewport*>(&widget))Reveal(widget);
    if(!widget.isVisible()||!widget.isEnabled())return false;
    if(dynamic_cast<QPushButton*>(&widget))point=widget.rect().center();
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,point,QPointF(widget.mapToGlobal(point.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
        QApplication::sendEvent(&widget,&event);
    }
    QApplication::processEvents();return true;
}

bool Number(QDoubleSpinBox* field,double value)
{
    if(!field)return false;Reveal(*field);if(!field->isVisible()||!field->isEnabled())return false;
    auto* input=field->findChild<QLineEdit*>();if(!input)return false;
    field->window()->activateWindow();input->setFocus();QApplication::processEvents();
    QFocusEvent focus(QEvent::FocusIn);QApplication::sendEvent(field,&focus);
    input->selectAll();
    for(const auto ch:QString::number(value,'g',12)) {
        const int key=ch==QChar('.')?Qt::Key_Period:ch==QChar('-')?Qt::Key_Minus:Qt::Key_0+ch.digitValue();
        QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier,QString(ch));QApplication::sendEvent(input,&event);
    }
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(input,&enter);
    QKeyEvent commit(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(input,&commit);
    QApplication::processEvents();
    const auto message=QStringLiteral("typed field %1: wanted %2, actual %3, text %4")
        .arg(field->objectName()).arg(value,0,'g',12).arg(field->value(),0,'g',12).arg(field->text()).toStdString();
    return Explain(message.c_str(),std::abs(field->value()-value)<1e-7);
}

std::vector<EntityId> Entities(V2MainWindow& w,EntityKind kind)
{
    std::vector<EntityId> ids;for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==kind)ids.push_back(e.id);
    return ids;
}

EntityId Coordinates(V2MainWindow& w,int kind,const std::vector<Vector3>& points,double radius=1)
{
    const auto before=Entities(w,EntityKind::Wire);if(!ToolButton(w,"draw.line"))return {};
    auto* scope=w.findChild<QComboBox*>("drawingScope");if(!scope)return {};
    scope->setCurrentIndex(kind==1?1:0);
    auto* modes=w.findChild<QTabWidget*>("drawingInputModes");if(!modes)return {};modes->setCurrentIndex(1);
    auto* type=w.findChild<QComboBox*>("drawingCoordinateKind");if(!type||!type->isVisible())return {};type->setCurrentIndex(kind);
    for(std::size_t i=0;i<points.size();++i) {
        const double values[]{points[i].x,points[i].y,points[i].z};
        for(std::size_t axis=0;axis<(kind==1?3u:2u);++axis)
            if(!Number(w.findChild<QDoubleSpinBox*>(QStringLiteral("drawingCoordinateP%1_%2").arg(i+1).arg(axis)),values[axis]))return {};
    }
    if(kind==2&&!Number(w.findChild<QDoubleSpinBox*>("drawingCoordinateRadius"),radius))return {};
    auto* create=w.findChild<QPushButton*>("drawingCreateCoordinates");
    if(!create||!Mouse(*create,create->rect().center()))return {};
    const auto after=Entities(w,EntityKind::Wire);
    if(!Explain("visible coordinate fields and button create one wire",after.size()==before.size()+1))return {};
    for(const auto id:after)if(std::find(before.begin(),before.end(),id)==before.end())return id;
    return {};
}

bool PickWire(V2MainWindow& w,EntityId id,Qt::KeyboardModifiers modifiers=Qt::ControlModifier,bool expected=true)
{
    auto& view=w.Viewport();
    // パネル変更後の配置を確定してから、人が見る現在の線の位置を求める。
    // Mouse() 内のイベント処理より前の古い座標を使うと別の線を押してしまう。
    Reveal(view);
    for(const auto& curve:w.Session().Scene().curves)if(curve.entityId==id) {
        const auto screen=view.Mapping().Project(curve.segment.Evaluate(.37));if(!screen)continue;
        const QPointF p(screen->x,screen->y);view.HoverAt(p);
        for(std::size_t i=0,n=view.CandidateCount();i<n;++i) {
            const auto candidate=view.CurrentCandidate();
            if(candidate&&candidate->entityId==id&&candidate->segmentId==curve.segmentId) {
                const auto sizeBefore=view.size();
                if(!Mouse(view,p,modifiers))return false;
                const auto& selected=view.Selection().entityIds;
                std::string actual;for(const auto chosen:selected)actual+=chosen.ToString()+" ";
                const auto projected=view.Mapping().Project(curve.segment.Evaluate(.37));
                const auto after=view.CurrentCandidate();
                const auto message="clicked wire "+id.ToString()+"; selected "+std::to_string(selected.size())
                    +" ["+actual+"]; candidate "+(after?after->entityId.ToString():"none")
                    +"; viewport "+std::to_string(sizeBefore.width())+"/"+std::to_string(view.width())
                    +"; pixel "+std::to_string(p.x())+"/"+std::to_string(p.y())
                    +" -> "+(projected?std::to_string(projected->x)+"/"+std::to_string(projected->y):"none")
                    +"; "+w.StatusText().toStdString();
                if((std::find(selected.begin(),selected.end(),id)!=selected.end())!=expected)
                    w.grab().save(qEnvironmentVariable("KACHACAD_GENERIC_OUTPUT")+"-pick-failure.png");
                return Explain(message.c_str(),(std::find(selected.begin(),selected.end(),id)!=selected.end())==expected);
            }
            view.CycleCandidate(false);
        }
    }
    return Explain("wire can be picked using the viewport and candidate cycling",false);
}

bool PickShape(V2MainWindow& w,EntityId id,Qt::KeyboardModifiers modifiers=Qt::NoModifier,bool faceOnly=false)
{
    auto& view=w.Viewport();
    Reveal(view);
    for(const auto& shape:view.ShapeViews())if(shape.entityId==id)
        for(const auto& triangle:shape.mesh.triangles) {
            const auto p=(triangle.points[0]+triangle.points[1]+triangle.points[2])*(1.0/3);
            const auto screen=view.Mapping().Project(p);if(!screen)continue;
            const QPointF pixel(screen->x,screen->y);view.HoverAt(pixel);
            for(std::size_t i=0,n=view.CandidateCount();i<n;++i) {
                const auto candidate=view.CurrentCandidate();
                if(candidate&&candidate->entityId==id&&(!faceOnly||candidate->kind==app::SelectionElementKind::Face)) {
                    const auto size=view.size();const bool clicked=Mouse(view,pixel,modifiers);
                    const auto& picked=view.Selection().entityIds;
                    const bool selected=std::find(picked.begin(),picked.end(),id)!=picked.end();
                    const auto message="shape click "+id.ToString()+"; width "+std::to_string(size.width())
                        +"/"+std::to_string(view.width())+"; "+w.StatusText().toStdString();
                    if(!selected)w.grab().save(qEnvironmentVariable("KACHACAD_GENERIC_OUTPUT")+"-shape-failure.png");
                    return Explain(message.c_str(),clicked&&selected);
                }
                view.CycleCandidate(false);
            }
        }
    return Explain("surface or solid can be picked through its visible geometry",false);
}

void View(V2MainWindow& w)
{
    w.resize(1600,1000);QApplication::processEvents();
    w.SelectTool(modeling::DrawingTool::Select);w.Viewport().SetViewDirection(ViewDirection::Isometric);
    w.Viewport().FitToDocument();QApplication::processEvents();Mouse(w.Viewport(),{2,2});
}

bool Outline(V2MainWindow& w,const std::vector<Vector3>& p,bool spatial,std::vector<EntityId>& ids)
{
    for(std::size_t i=0;i<p.size();++i) {
        const auto id=Coordinates(w,spatial?1:0,{p[i],p[(i+1)%p.size()]});if(id.IsNil())return false;ids.push_back(id);
    }
    return true;
}

bool MakeFaces(V2MainWindow& w,const std::vector<EntityId>& wires,int expected)
{
    View(w);for(const auto id:wires)if(!PickWire(w,id))return false;
    w.RunCommand("surface.from_lines");
    const auto plan=w.LoopFacesTool().Plan();
    if(auto* dock=w.LoopFacesTool().Dock()) {
        for(int row=0;row<dock->FaceRowCount();++row)Note(dock->FaceRowTextJa(row).toStdString().c_str());
        Note(dock->SelectionTextJa().toStdString().c_str());
    }
    const auto message="boundary preview: selected "+std::to_string(w.Viewport().Selection().entityIds.size())
        +" wires; planned "+std::to_string(plan?plan->faces.size():0)+"; "+w.StatusText().toStdString();
    if(!Explain(message.c_str(),plan.has_value()&&!plan->faces.empty()))return false;
    if(!w.HandleToolKey(Qt::Key_Return,nullptr))return false;
    const auto made="surface confirmation: wanted "+std::to_string(expected)+", actual "+std::to_string(CountOfKind(w,EntityKind::GuideSurface))+"; "+w.StatusText().toStdString();
    return Explain(made.c_str(),CountOfKind(w,EntityKind::GuideSurface)==expected);
}

bool RayMaterial(V2MainWindow& w,EntityKind kind,Vector3 p)
{
    for(const auto& shape:w.Viewport().ShapeViews()) {
        const auto* e=w.Session().GetDocument().FindEntity(shape.entityId);if(!e||e->kind!=kind)continue;
        for(const auto& triangle:shape.mesh.triangles)if(modeling::RayHitsTriangle(p,{0,0,-1},triangle))return true;
    }
    return false;
}

void Shot(V2MainWindow& w,const char* name)
{
    const auto prefix=qEnvironmentVariable("KACHACAD_GENERIC_OUTPUT");
    if(!prefix.isEmpty()){QApplication::processEvents();w.grab().save(prefix+QString::fromUtf8(name)+".png");}
}

bool FineLip(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> wires;
    if(!Outline(w,{{0,0,0},{1,0,0},{1,.005,0},{.006,.005,0},{.006,.009,0},{0,.009,0}},false,wires))return false;
    if(!MakeFaces(w,wires,1))return false;
    Shot(w,"-fine-lip");const auto before=CountOfKind(w,EntityKind::Wire);
    w.RunCommand("edit.undo");if(CountOfKind(w,EntityKind::GuideSurface)!=0)return false;
    w.RunCommand("edit.redo");QTemporaryDir dir;
    return Explain("fine concave detail survives redo and save/reopen",w.SaveAndReopen(dir.filePath("fine.kcd2"))
        &&CountOfKind(w,EntityKind::GuideSurface)==1&&CountOfKind(w,EntityKind::Wire)==before&&w.RebuildProblems().isEmpty());
}

bool OffsetMask(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> wires;
    if(!Outline(w,{{0,0,0},{20,0,0},{20,6,0},{10,6,0},{10,14,0},{20,14,0},{20,20,0},{0,20,0}},false,wires))return false;
    for(const auto center:{Vector3{3,3,0},Vector3{3,12,0}}) {
        const auto circle=Coordinates(w,2,{center},1);if(circle.IsNil())return false;wires.push_back(circle);
    }
    if(!MakeFaces(w,wires,1))return false;const auto source=Entities(w,EntityKind::GuideSurface).back();
    View(w);if(!PickShape(w,source))return false;w.RunCommand("surface.create");
    auto* method=w.findChild<QComboBox*>("surfaceOtherMethods");if(!method||!method->isVisible())return false;
    method->setCurrentIndex(1);
    if(!Number(w.findChild<QDoubleSpinBox*>("surfaceOffsetDistance"),.75))return false;
    if(!Explain("offset has an entered distance and a real preview",w.SurfaceInput().offsetDistanceMm==.75&&w.SurfacePreviewShown()))return false;
    Shot(w,"-offset-preview");if(!w.HandleToolKey(Qt::Key_Return,nullptr))return false;
    if(!Explain("offset keeps both holes and concave notch",CountOfKind(w,EntityKind::GuideSurface)==2
        &&!RayMaterial(w,EntityKind::GuideSurface,{3,3,10})&&!RayMaterial(w,EntityKind::GuideSurface,{3,12,10})
        &&!RayMaterial(w,EntityKind::GuideSurface,{15,10,10})&&RayMaterial(w,EntityKind::GuideSurface,{7,10,10})))return false;
    const auto moved=Entities(w,EntityKind::GuideSurface).back();View(w);if(!PickShape(w,moved))return false;
    w.RunCommand("part.thicken");if(!Number(w.findChild<QDoubleSpinBox*>("thickenAmount"),.2))return false;
    if(!w.ThickenDock().ClickConfirm())return false;Shot(w,"-perforated-mask");
    QTemporaryDir dir;
    return Explain("perforated offset shell can be thickened and reopened",CountOfKind(w,EntityKind::Part)==1
        &&w.SaveAndReopen(dir.filePath("mask.kcd2"))&&w.RebuildProblems().isEmpty());
}

EntityId Box(V2MainWindow& w,double x0,double y0,double x1,double y1)
{
    Note("box: type four sides");
    std::vector<EntityId> wires;if(!Outline(w,{{x0,y0,0},{x1,y0,0},{x1,y1,0},{x0,y1,0}},false,wires))return {};
    View(w);for(const auto wire:wires)if(!PickWire(w,wire))return {};Note("box: open extrusion");w.RunCommand("part.extrude");
    if(!Number(w.findChild<QDoubleSpinBox*>("extrudeDistance"),10)||!w.HandleToolKey(Qt::Key_Return,nullptr))return {};
    return Entities(w,EntityKind::Part).back();
}

bool OldSingleOutputCut(V2MainWindow& w,const QString& path)
{
    auto file=V2DocumentOpen::FileForSave(w);EntityId removed;
    for(auto& feature:file.snapshot.features)if(std::holds_alternative<domain::BooleanDefinition>(feature.definition)) {
        if(feature.outputs.size()!=2)return false;
        removed=feature.outputs.back().entityId;feature.outputs.pop_back();
    }
    if(removed.IsNil())return false;
    std::erase_if(file.snapshot.entities,[&](const auto& entity){return entity.id==removed;});
    const auto bytes=io::SaveDocument(file);
    if(!bytes.HasValue()||!io::WriteFileAtomically(path.toStdString(),bytes.Value()).HasValue())return false;
    return Explain("older single-output split cut reopens without losing either solid",w.OpenDocumentFile(path)
        &&w.RebuildProblems().isEmpty()&&CountOfKind(w,EntityKind::Part)==3
        &&RayMaterial(w,EntityKind::Part,{2,5,20})&&RayMaterial(w,EntityKind::Part,{8,5,20})
        &&!RayMaterial(w,EntityKind::Part,{5,5,20}));
}

bool SeparatedCut(V2MainWindow& w)
{
    w.RunCommand("file.new");const auto base=Box(w,0,0,10,10),cutter=Box(w,4,-1,6,11);
    if(base.IsNil()||cutter.IsNil())return false;
    Note("cut: pick base and cutter");View(w);w.RunCommand("part.boolean_cut");if(!PickShape(w,base)||!PickShape(w,cutter))return false;
    if(!Explain(("preview tells the user it will make two parts: "+w.BooleanDock().StatusTextJa().toStdString()
        +"; "+w.StatusText().toStdString()).c_str(),w.BooleanDock().StatusTextJa().contains(QStringLiteral("2 個"))))return false;
    Note("cut: confirm");if(!w.HandleToolKey(Qt::Key_Return,nullptr)||CountOfKind(w,EntityKind::Part)!=4)return false;
    Shot(w,"-separated-cut");w.RunCommand("edit.undo");if(CountOfKind(w,EntityKind::Part)!=2)return false;
    w.RunCommand("edit.redo");QTemporaryDir dir;Note("cut: save reopen");if(!w.SaveAndReopen(dir.filePath("cut.kcd2")))return false;
    int visible=0;std::vector<double> centers;
    for(const auto& shape:w.Viewport().ShapeViews())if(!shape.surface) {
        const auto* e=w.Session().GetDocument().FindEntity(shape.entityId);
        if(e&&e->kind==EntityKind::Part&&e->visibility==domain::Visibility::Visible) {
            ++visible;centers.push_back((shape.mesh.minimum.x+shape.mesh.maximum.x)*.5);
        }
    }
    std::sort(centers.begin(),centers.end());
    return Explain("both distinct connected parts survive undo redo and reopen",visible==2&&centers.size()==2
        &&std::abs(centers[0]-2)<1e-5&&std::abs(centers[1]-8)<1e-5&&w.RebuildProblems().isEmpty())
        &&OldSingleOutputCut(w,dir.filePath("older-single-output.kcd2"));
}

bool OffsetWorkPlane(V2MainWindow& w,double z,bool front=false)
{
    w.SetWorkPlaneChooser({});
    View(w);if(!ToolButton(w,"workplane.create"))return false;
    auto* method=w.findChild<QComboBox*>("workPlaneMethod");auto* reference=w.findChild<QComboBox*>("workPlaneReference");
    if(!Explain("workplane controls are available",method&&reference&&method->isVisible()))return false;
    method->setCurrentIndex(method->findText(QStringLiteral("平面から離す")));
    auto at=reference->findText(front?QStringLiteral("正面"):QStringLiteral("上面"),Qt::MatchContains);
    if(at<0)at=reference->findText(front?QStringLiteral("front_XZ"):QStringLiteral("top_XY"),Qt::MatchContains);
    if(at<0){for(int i=0;i<reference->count();++i)Note(reference->itemText(i).toStdString().c_str());return false;}
    reference->setCurrentIndex(at);
    if(!Number(w.findChild<QDoubleSpinBox*>("workPlaneDistance"),z))return false;
    QPushButton* create=nullptr;
    for(auto* button:w.findChildren<QPushButton*>())
        if(button->isVisible()&&button->text()==QStringLiteral("平面を作る")){create=button;break;}
    const bool pressed=create&&Mouse(*create,create->rect().center());
    const auto message=QStringLiteral("offset workplane: target %1, origin %2/%3/%4, reference %5; %6")
        .arg(z).arg(w.Viewport().WorkPlane().origin.x).arg(w.Viewport().WorkPlane().origin.y)
        .arg(w.Viewport().WorkPlane().origin.z).arg(reference->currentText()).arg(w.StatusText()).toStdString();
    return Explain(message.c_str(),pressed&&(front?std::abs(w.Viewport().WorkPlane().normal.y)>.99
        :std::abs(w.Viewport().WorkPlane().origin.z-z)<1e-6));
}

bool OrganicShell(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> sections;
    for(const auto zr:{std::pair{-10.0,6.0},std::pair{0.0,10.0},std::pair{12.0,5.0}}) {
        if(!OffsetWorkPlane(w,zr.first))return false;
        const auto circle=Coordinates(w,2,{{0,0,0}},zr.second);if(circle.IsNil())return false;sections.push_back(circle);
    }
    View(w);w.RunCommand("surface.create");
    if(!w.SurfaceDock().ClickMethodCard(modeling::GuideSurfaceMethod::LoftSections))return false;
    for(const auto index:{2u,0u,1u})if(!PickWire(w,sections[index]))return false;
    if(!Explain("rounded varying sections produce an organic shell preview",w.SurfacePreviewShown()))return false;
    if(!w.HandleToolKey(Qt::Key_Return,nullptr)||CountOfKind(w,EntityKind::GuideSurface)!=1)return false;
    const auto surface=Entities(w,EntityKind::GuideSurface).back();View(w);if(!PickShape(w,surface))return false;
    w.RunCommand("part.thicken");if(!Number(w.findChild<QDoubleSpinBox*>("thickenAmount"),.2)||!w.ThickenDock().ClickConfirm())return false;
    Shot(w,"-organic-shell");QTemporaryDir dir;
    if(!w.SaveAndReopen(dir.filePath("organic.kcd2"))||!w.RebuildProblems().isEmpty())return false;
    View(w);const auto part=Entities(w,EntityKind::Part).back();if(!PickShape(w,part))return false;
    auto* output=V2OutputTool::Open(w,2);if(!output)return false;output->ChooseFormat(1);
    const bool ok=output->AssetCount()==1&&output->SaveTo(dir.filePath("organic.step"));output->close();
    return Explain("human-made organic shell saves, rebuilds and exports as a checked STEP",ok);
}

bool TwoCurvedRings(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> wires;
    for(double x:{0.0,40.0})for(double s:{10.0,6.0}) {
        const double z=.02*s*s;
        if(!Outline(w,{{x-s,-s,z},{x+s,-s,-z},{x+s,s,z},{x-s,s,-z}},true,wires))return false;
    }
    if(!MakeFaces(w,wires,2))return false;
    if(!Explain("both curved holes remain empty",!RayMaterial(w,EntityKind::GuideSurface,{0,0,20})
        &&!RayMaterial(w,EntityKind::GuideSurface,{40,0,20})&&RayMaterial(w,EntityKind::GuideSurface,{8,0,20})
        &&RayMaterial(w,EntityKind::GuideSurface,{48,0,20})))return false;
    Shot(w,"-two-curved-rings");QTemporaryDir dir;
    return w.SaveAndReopen(dir.filePath("rings.kcd2"))&&w.RebuildProblems().isEmpty();
}

// These fixtures use visible numerical fields, ribbon buttons and ordinary
// viewport clicks. Read-only native checks below never construct the fixture.
bool ToolButton(V2MainWindow& w,std::string_view command)
{
    for(const auto mode:app::AllUiModes())for(const auto& category:app::RibbonCategoriesFor(mode))
        for(const auto& tool:category.tools)if(tool.commandId==command){
            if(w.Mode()!=mode)w.SetMode(mode);QApplication::processEvents();
            for(auto* button:w.Ribbon().findChildren<QToolButton*>("ribbonCategory"))
                if(button->isVisible()&&button->text()==QString::fromUtf8(category.labelJa.data(),int(category.labelJa.size()))){
                    if(!Mouse(*button,button->rect().center()))return false;break;
                }
            for(auto* button:w.Ribbon().findChildren<QToolButton*>("ribbonTool"))
                if(button->isVisible()&&button->text()==QString::fromUtf8(tool.labelJa.data(),int(tool.labelJa.size())))
                    return Mouse(*button,button->rect().center());
            return Explain("requested ribbon tool is reachable",false);
        }
    return Explain("requested command has a visible ribbon route",false);
}

std::vector<EntityId> VisibleParts(V2MainWindow& w)
{
    std::vector<EntityId> ids;for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==EntityKind::Part&&e.visibility==domain::Visibility::Visible)ids.push_back(e.id);
    return ids;
}

bool SelectVisiblePartRows(V2MainWindow& w)
{
    auto* tree=w.EntityTree();bool first=true;
    for(const auto id:VisibleParts(w)){
        auto* row=w.ItemOfEntity(id);if(!row)return false;tree->scrollToItem(row);QApplication::processEvents();
        if(!Mouse(*tree->viewport(),tree->visualItemRect(row).center(),first?Qt::NoModifier:Qt::ControlModifier))return false;
        first=false;
    }
    return !first;
}

#ifdef KACHACAD_V2_WITH_OCCT
const std::vector<TopoDS_Shape>& NativeParts(V2MainWindow& w)
{
    // Inspect what the ordinary output dialog actually saves. This avoids a
    // private UI accessor or constructing substitute geometry in the test.
    static std::string document;static std::uint64_t revision=0;static std::vector<TopoDS_Shape> shapes;
    const auto snapshot=w.Session().GetDocument().Snapshot();
    if(document==snapshot.id.ToString()&&revision==snapshot.revision)return shapes;
    shapes.clear();document=snapshot.id.ToString();revision=snapshot.revision;
    if(!SelectVisiblePartRows(w))return shapes;
    QTemporaryDir temporary;const auto path=temporary.filePath("native-inspection.kcd2");
    auto* output=V2OutputTool::Open(w,2);if(!output)return shapes;
    const bool saved=output->SaveTo(path);output->close();if(!saved)return shapes;
    const auto bytes=io::ReadWholeFile(path.toStdString());if(!bytes.HasValue())return shapes;
    const auto file=io::LoadDocument(bytes.Value());if(!file.HasValue())return shapes;
    for(const auto& feature:file.Value().snapshot.features){
        const auto* frozen=std::get_if<domain::FreezeDerivedDefinition>(&feature.definition);if(!frozen){shapes.clear();return shapes;}
        const auto restored=kernel::RestoreOutputShape(frozen->frozenBrep);if(!restored.HasValue()){shapes.clear();return shapes;}
        TopoDS_Shape shape;const bool found=kernel::LookupShape(restored.Value(),shape);kernel::ReleaseShape(restored.Value());
        if(!found){shapes.clear();return shapes;}shapes.push_back(shape);
    }
    if(shapes.size()!=VisibleParts(w).size())shapes.clear();
    return shapes;
}
#endif

bool MaterialAt(V2MainWindow& w,Vector3 point)
{
#ifdef KACHACAD_V2_WITH_OCCT
    for(const auto& shape:NativeParts(w)){
        BRepClass3d_SolidClassifier classify(shape,gp_Pnt(point.x,point.y,point.z),1e-7);
        if(classify.State()==TopAbs_IN)return true;
    }
#endif
    return false;
}

bool VolumeIs(V2MainWindow& w,double expected)
{
    double actual=0;
#ifdef KACHACAD_V2_WITH_OCCT
    for(const auto& shape:NativeParts(w)){
        GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);actual+=std::abs(properties.Mass());
    }
#endif
    Note(("native volume "+std::to_string(actual)+" expected "+std::to_string(expected)).c_str());
    return Explain("visible connected solids keep the independently calculated volume",std::abs(actual-expected)<1e-5*std::max(1.0,expected));
}

std::vector<Vector3> SourceWireSamples(V2MainWindow& w)
{
    std::vector<Vector3> samples;
    for(const auto& curve:w.Session().Scene().curves){
        const auto* entity=w.Session().GetDocument().FindEntity(curve.entityId);
        if(entity&&entity->kind==EntityKind::Wire)
            for(int i=0;i<=16;++i)samples.push_back(curve.segment.Evaluate(i/16.));
    }
    return samples;
}

bool SameWireSamples(V2MainWindow& w,const std::vector<Vector3>& before)
{
    const auto after=SourceWireSamples(w);if(after.size()!=before.size())return false;
    for(std::size_t i=0;i<before.size();++i)if((before[i]-after[i]).Length()>1e-8)return false;
    return true;
}

bool FinishShape(V2MainWindow& w,const char* name,std::size_t visibleCount)
{
    const auto wires=Entities(w,EntityKind::Wire),parts=Entities(w,EntityKind::Part);
    const auto sourceSamples=SourceWireSamples(w);
    if(!Explain("expected visible parts and no rebuild failures",VisibleParts(w).size()==visibleCount&&w.RebuildProblems().isEmpty()))return false;
    w.RunCommand("edit.undo");w.RunCommand("edit.redo");
    if(!Explain("Undo/Redo keeps source geometry and all wire/part UUIDs",Entities(w,EntityKind::Wire)==wires
        &&Entities(w,EntityKind::Part)==parts&&SameWireSamples(w,sourceSamples)))return false;
    QTemporaryDir temporary;const auto prefix=qEnvironmentVariable("KACHACAD_GENERIC_OUTPUT");
    const auto path=prefix.isEmpty()?temporary.filePath(QString::fromUtf8(name)):prefix+"-"+name;
    if(!w.SaveAndReopen(path+".kcd2")||!w.RebuildProblems().isEmpty()||Entities(w,EntityKind::Wire)!=wires
        ||Entities(w,EntityKind::Part)!=parts||VisibleParts(w).size()!=visibleCount||!SameWireSamples(w,sourceSamples))return false;
    if(!prefix.isEmpty()){
        w.SetPathChooser([path](bool){return path+".kcd2";});w.RunCommand("file.save_as");w.SetPathChooser({});
        if(!Explain("retain the complete editable fixture after save/reopen",w.StatusText().contains(QStringLiteral("保存しました"))))return false;
    }
    View(w);Shot(w,(std::string("-")+name).c_str());
    if(!SelectVisiblePartRows(w))return false;
    auto* output=V2OutputTool::Open(w,2);if(!output)return false;
    const bool selected=output->AssetCount()==int(visibleCount);output->ChooseFormat(1);
    const bool step=selected&&output->SaveTo(path+".step");output->ChooseFormat(0);
    const bool stl=step&&output->SaveTo(path+".stl");output->close();
    const auto stepBytes=io::ReadWholeFile((path+".step").toStdString());
    const auto stlBytes=io::ReadWholeFile((path+".stl").toStdString());
    std::uint32_t triangles=0;
    if(stlBytes.HasValue()&&stlBytes.Value().size()>=84)
        for(unsigned i=0;i<4;++i)triangles|=std::uint32_t(static_cast<unsigned char>(stlBytes.Value()[80+i]))<<(8*i);
    return Explain("normal output dialog exports checked STEP and nonempty binary STL",stl&&stepBytes.HasValue()
        &&stepBytes.Value().starts_with("ISO-10303-21;")&&stlBytes.HasValue()&&triangles>0
        &&stlBytes.Value().size()==84+std::uint64_t(triangles)*50);
}

EntityId ExtrudeWires(V2MainWindow& w,const std::vector<EntityId>& wires,double distance)
{
    Note(("extrude "+std::to_string(wires.size())+" wires by "+std::to_string(distance)).c_str());
    const auto thickness=app::ParameterValueOf(w.ParameterDock().Values(),app::ParameterId::ExtrudeDistance);
    const auto before=Entities(w,EntityKind::Part).size();View(w);
    if(!ToolButton(w,"part.extrude"))return {};
    for(const auto id:wires)if(!PickWire(w,id,Qt::NoModifier))return {};
    if(!Explain("ordinary clicks accumulate the intended profile wires",w.Viewport().Selection().entityIds==wires))return {};
    if(!PickWire(w,wires.back(),Qt::NoModifier,false)||w.Viewport().Selection().entityIds.size()!=wires.size()-1
        ||!PickWire(w,wires.back(),Qt::NoModifier)||w.Viewport().Selection().entityIds!=wires)return {};
    if(!Number(w.findChild<QDoubleSpinBox*>("extrudeDistance"),distance))return {};
    if(!Explain("extrusion length is reflected in its own field and never changes fabrication stock thickness",
        app::ParameterValueOf(w.ParameterDock().Values(),app::ParameterId::ExtrudeDistance)==thickness
        &&std::abs(w.PartDock().ParameterMm(app::ParameterId::ExtrudeLengthMm)-std::abs(distance))<1e-9))return {};
    const bool confirmed=w.HandleToolKey(Qt::Key_Return,nullptr);
    if(!Explain(("extrude commit: "+w.StatusText().toStdString()).c_str(),confirmed&&Entities(w,EntityKind::Part).size()==before+1))return {};
    return Entities(w,EntityKind::Part).back();
}

EntityId BooleanParts(V2MainWindow& w,EntityId target,EntityId tool,bool add)
{
    Note((std::string("boolean ")+(add?"add ":"cut ")+target.ToString()+" with "+tool.ToString()).c_str());
    const auto before=Entities(w,EntityKind::Part).size();View(w);
    if(!ToolButton(w,add?"part.boolean_add":"part.boolean_cut")||!PickShape(w,target,Qt::NoModifier,true)
        ||!Explain("face pick goes to the target slot",w.BooleanInput().target==target)
        ||!PickShape(w,tool,Qt::NoModifier,true)
        ||!Explain("second face pick goes only to the tool slot",w.BooleanInput().tools==std::vector<EntityId>{tool}))return {};
    Note(w.BooleanDock().StatusTextJa().toStdString().c_str());
    const bool confirmed=w.BooleanDock().ClickConfirm();
    if(!Explain(("boolean commit: "+w.StatusText().toStdString()).c_str(),confirmed&&Entities(w,EntityKind::Part).size()==before+1))return {};
    return Entities(w,EntityKind::Part).back();
}

EntityId RevolveWire(V2MainWindow& w,EntityId profile,EntityId axis,double degrees)
{
    View(w);if(!ToolButton(w,"part.revolve")||!PickWire(w,profile,Qt::NoModifier)||!PickWire(w,axis,Qt::NoModifier))return {};
    if(!Explain("profile and axis slots match the ordinary clicks",w.SolidTool().Input().profiles==std::vector<EntityId>{profile}
        &&w.SolidTool().Input().axis==axis))return {};
    auto* dock=w.SolidTool().Dock();
    if(degrees!=360&&(!dock->ClickMethodCard(1)||!Number(w.findChild<QDoubleSpinBox*>("solidAngle"),degrees)))return {};
    if(!Explain(("revolve preview: "+dock->StatusTextJa().toStdString()).c_str(),dock->ConfirmEnabled())||!dock->ClickConfirm())return {};
    return Entities(w,EntityKind::Part).back();
}

bool WingPlate(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> wires;
    if(!Outline(w,{{0,0,0},{35,18,0},{32,25,0},{0,8,0}},false,wires)||ExtrudeWires(w,wires,.8).IsNil())return false;
    if(!VolumeIs(w,222)||!MaterialAt(w,{10,8,.4})||MaterialAt(w,{33,5,.4}))return false;
    return FinishShape(w,"swept-wing-plate",1);
}

bool HydraulicCylinder(V2MainWindow& w)
{
    w.RunCommand("file.new");const auto outer=Coordinates(w,2,{{0,0,0}},5),inner=Coordinates(w,2,{{0,0,0}},4);
    if(outer.IsNil()||inner.IsNil()||ExtrudeWires(w,{outer,inner},35).IsNil()||!OffsetWorkPlane(w,20))return false;
    const auto profile=Coordinates(w,2,{{0,0,0}},2);if(profile.IsNil()||ExtrudeWires(w,{profile},35).IsNil())return false;
    if(!VolumeIs(w,455*3.14159265358979323846)||MaterialAt(w,{0,0,5})||!MaterialAt(w,{4.5,0,5})
        ||!MaterialAt(w,{0,0,40})||MaterialAt(w,{3,0,25}))return false;
    return FinishShape(w,"hydraulic-cylinder-rod",2);
}

bool PipeElbow(V2MainWindow& w)
{
    w.RunCommand("file.new");const auto outer=Coordinates(w,2,{{14,0,0}},3),inner=Coordinates(w,2,{{14,0,0}},2);
    const auto axis=Coordinates(w,1,{{0,-5,0},{0,5,0}});
    if(outer.IsNil()||inner.IsNil()||axis.IsNil())return false;
    const auto shell=RevolveWire(w,outer,axis,90),bore=RevolveWire(w,inner,axis,90);
    if(shell.IsNil()||bore.IsNil()||BooleanParts(w,shell,bore,false).IsNil())return false;
    constexpr double pi=3.14159265358979323846;
    if(!VolumeIs(w,35*pi*pi)||MaterialAt(w,{14,0,-.1})||MaterialAt(w,{9.8994949366,0,-9.8994949366})
        ||MaterialAt(w,{.1,0,-14})||!MaterialAt(w,{16.5,0,-.1}))return false;
    return FinishShape(w,"hollow-elbow",1);
}

bool FlangedPipe(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> profile;
    for(const auto radius:{15.,5.})profile.push_back(Coordinates(w,2,{{0,0,0}},radius));
    for(const auto center:{Vector3{10,0,0},Vector3{-10,0,0},Vector3{0,10,0},Vector3{0,-10,0}})
        profile.push_back(Coordinates(w,2,{center},1.5));
    if(std::any_of(profile.begin(),profile.end(),[](auto id){return id.IsNil();}))return false;
    const auto flange=ExtrudeWires(w,profile,2);
    const auto outer=Coordinates(w,2,{{0,0,0}},7),inner=Coordinates(w,2,{{0,0,0}},5);
    if(flange.IsNil()||outer.IsNil()||inner.IsNil())return false;
    const auto tube=ExtrudeWires(w,{outer,inner},15);
    if(tube.IsNil()||BooleanParts(w,flange,tube,true).IsNil())return false;
    if(!VolumeIs(w,694*3.14159265358979323846)||MaterialAt(w,{0,0,1})||MaterialAt(w,{10,0,1})
        ||MaterialAt(w,{-10,0,1})||MaterialAt(w,{0,10,1})||MaterialAt(w,{0,-10,1})
        ||!MaterialAt(w,{12,3,1})||!MaterialAt(w,{6,0,10}))return false;
    return FinishShape(w,"flanged-pipe-five-holes",1);
}

bool BucketSkin(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> arcs;
    for(const auto z:{0.,20.}){
        if(!OffsetWorkPlane(w,z))return false;
        const auto id=Coordinates(w,3,{{-12,0,0},{0,-12,0},{12,0,0}});if(id.IsNil())return false;arcs.push_back(id);
    }
    View(w);if(!ToolButton(w,"surface.create")||!w.SurfaceDock().ClickMethodCard(modeling::GuideSurfaceMethod::RuledSections))return false;
    for(const auto id:arcs)if(!PickWire(w,id,Qt::NoModifier))return false;
    if(!Explain("both open curved sections are accepted",w.SurfaceInput().sections.size()==2&&w.SurfacePreviewShown())
        ||!w.HandleToolKey(Qt::Key_Return,nullptr))return false;
    const auto surface=Entities(w,EntityKind::GuideSurface).back();View(w);
    if(!ToolButton(w,"part.thicken")||!PickShape(w,surface,Qt::NoModifier,true)
        ||w.ThickenInput().surfaces!=std::vector<EntityId>{surface}
        ||!w.ThickenDock().ClickPlacementCard(fabrication::ThicknessPlacement::Centered)
        ||!Number(w.findChild<QDoubleSpinBox*>("thickenAmount"),.6)||!w.ThickenDock().ClickConfirm())return false;
    if(MaterialAt(w,{0,0,10})||MaterialAt(w,{0,-5,10})||!MaterialAt(w,{0,-12,10})||MaterialAt(w,{0,5,10}))return false;
    return FinishShape(w,"open-curved-bucket-skin",1);
}

EntityId JoinWires(V2MainWindow& w,const std::vector<EntityId>& wires)
{
    const auto before=Entities(w,EntityKind::Wire);
    View(w);if(!ToolButton(w,"wire.join"))return {};
    for(const auto id:wires)if(!PickWire(w,id,Qt::NoModifier))return {};
    if(!w.HandleToolKey(Qt::Key_Return,nullptr))return {};
    const auto after=Entities(w,EntityKind::Wire);
    for(const auto id:after)if(std::find(before.begin(),before.end(),id)==before.end()){
        const auto* entity=w.Session().GetDocument().FindEntity(id);
        if(entity&&entity->visibility==domain::Visibility::Visible)return id;
    }
    return {};
}

Vector3 WingPoint(Vector3 p,double offset,double degrees)
{
    const double a=degrees*3.14159265358979323846/180;
    return {std::round((offset+p.x*std::cos(a)-p.y*std::sin(a))*1000)/1000,
        std::round((p.x*std::sin(a)+p.y*std::cos(a))*1000)/1000,0};
}

bool TwistedWing(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> sections;
    for(int i=0;i<3;++i){
        const double chord=24-8*i;const double angle=i==0?0:i==1?8:15;
        if(!OffsetWorkPlane(w,15*i))return false;
        const auto transform=[&](std::vector<Vector3> points){for(auto& p:points)p=WingPoint(p,5*i,angle);return points;};
        const auto upper=Coordinates(w,4,transform({{chord,.025,0},{.6*chord,.15*chord,0},{.02*chord,.15*chord,0},{0,0,0}}));
        const auto lower=Coordinates(w,4,transform({{0,0,0},{.02*chord,-.05*chord,0},{.6*chord,-.05*chord,0},{chord,-.025,0}}));
        const auto rear=Coordinates(w,0,transform({{chord,-.025,0},{chord,.025,0}}));
        if(upper.IsNil()||lower.IsNil()||rear.IsNil())return false;
        const auto joined=JoinWires(w,{upper,lower,rear});if(joined.IsNil())return false;sections.push_back(joined);
    }
    View(w);if(!ToolButton(w,"part.loft_solid"))return false;
    for(const auto id:sections)if(!PickWire(w,id,Qt::NoModifier))return false;
    auto* dock=w.SolidTool().Dock();
    if(!Explain("three ordered airfoil sections retain their thin trailing edges and twist",
        w.SolidTool().Input().profiles==sections&&dock&&dock->ConfirmEnabled())||!dock->ClickConfirm())return false;
    for(int i=0;i<3;++i){
        const double chord=24-8*i,angle=i==0?0:i==1?8:15;
        auto inside=WingPoint({.4*chord,.025*chord,0},5*i,angle);
        auto outside=WingPoint({.4*chord,.3*chord,0},5*i,angle);
        inside.z=outside.z=i==0?.002:i==2?29.998:15;
        if(!Explain("airfoil section has material inside and empty space above it",MaterialAt(w,inside)&&!MaterialAt(w,outside)))return false;
    }
    return FinishShape(w,"tapered-twisted-airfoil",1);
}

EntityId RevolvedBall(V2MainWindow& w,double radius,EntityId axis)
{
    const auto arc=Coordinates(w,3,{{0,-radius,0},{radius,0,0},{0,radius,0}});
    const auto diameter=Coordinates(w,0,{{0,radius,0},{0,-radius,0}});
    if(arc.IsNil()||diameter.IsNil())return {};
    const auto before=Entities(w,EntityKind::Wire);
    View(w);if(!ToolButton(w,"wire.join")||!PickWire(w,arc,Qt::NoModifier)
        ||!PickWire(w,diameter,Qt::NoModifier)||!w.HandleToolKey(Qt::Key_Return,nullptr))return {};
    for(const auto id:Entities(w,EntityKind::Wire))
        if(std::find(before.begin(),before.end(),id)==before.end())return RevolveWire(w,id,axis,360);
    return {};
}

bool PiercedVessel(V2MainWindow& w)
{
    w.RunCommand("file.new");const auto axis=Coordinates(w,1,{{0,-20,0},{0,20,0}});
    if(axis.IsNil())return false;
    const auto outer=RevolvedBall(w,12,axis),inner=RevolvedBall(w,11.4,axis);
    if(outer.IsNil()||inner.IsNil())return false;
    const auto shell=BooleanParts(w,outer,inner,false);constexpr double pi=3.14159265358979323846;
    if(shell.IsNil()||!VolumeIs(w,4*pi/3*(12*12*12-11.4*11.4*11.4)))return false;
    const auto circle=Coordinates(w,2,{{0,0,0}},2);if(circle.IsNil())return false;
    const auto bore=ExtrudeWires(w,{circle},20);
    if(bore.IsNil()||BooleanParts(w,shell,bore,false).IsNil())return false;
    if(!Explain("spherical shell keeps its cavity, both rotation poles and lower/side walls; upper port is open",
        !MaterialAt(w,{0,0,0})&&!MaterialAt(w,{0,0,11.7})&&MaterialAt(w,{0,0,-11.7})
        &&MaterialAt(w,{0,11.7,0})&&MaterialAt(w,{0,-11.7,0})&&MaterialAt(w,{11.7,0,0})))return false;
    return FinishShape(w,"pierced-spherical-vessel",1);
}

bool PipeTee(V2MainWindow& w)
{
    w.RunCommand("file.new");const auto outer=Coordinates(w,2,{{0,0,0}},5),inner=Coordinates(w,2,{{0,0,0}},4);
    if(outer.IsNil()||inner.IsNil())return false;
    const auto main=ExtrudeWires(w,{outer,inner},20);if(main.IsNil()||!OffsetWorkPlane(w,0,true))return false;
    // The standard front plane uses U=Z, V=X, normal=+Y. Type its
    // displayed plane coordinates, rather than mistaking them for world X/Z.
    const auto frame=w.Viewport().WorkPlane();const Vector3 location{0,0,10};
    const Vector3 center{frame.CoordinateU(location),frame.CoordinateV(location),0};
    const auto sideOuter=Coordinates(w,2,{center},3),sideInner=Coordinates(w,2,{center},2);
    if(sideOuter.IsNil()||sideInner.IsNil())return false;
    const auto side=ExtrudeWires(w,{sideOuter,sideInner},12);
    if(side.IsNil())return false;const auto joined=BooleanParts(w,main,side,true);if(joined.IsNil())return false;
    const auto sideBore=ExtrudeWires(w,{sideInner},12);if(sideBore.IsNil())return false;
    const auto pierced=BooleanParts(w,joined,sideBore,false);if(pierced.IsNil())return false;
    const auto mainBore=ExtrudeWires(w,{inner},20);
    if(mainBore.IsNil()||BooleanParts(w,pierced,mainBore,false).IsNil())return false;
    if(!Explain("both tube bores communicate through the junction without a blocking wall",
        !MaterialAt(w,{0,0,3})&&!MaterialAt(w,{0,0,10})&&!MaterialAt(w,{0,8,10})
        &&!MaterialAt(w,{0,4.5,10})&&MaterialAt(w,{4.5,0,5})&&MaterialAt(w,{2.5,8,10})))return false;
    return FinishShape(w,"communicating-pipe-tee",1);
}

bool LinkArm(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> profile;
    const std::vector<Vector3> polygon{{0,0,0},{30,0,0},{38,12,0},{30,18,0},{24,8,0},{0,8,0}};
    if(!Outline(w,polygon,false,profile))return false;
    for(const auto center:{Vector3{4,4,0},Vector3{32,11,0}}){
        const auto id=Coordinates(w,2,{center},1.5);if(id.IsNil())return false;profile.push_back(id);
    }
    if(ExtrudeWires(w,profile,2).IsNil())return false;
    double twice=0;for(std::size_t i=0;i<polygon.size();++i){const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];twice+=a.x*b.y-a.y*b.x;}
    if(!VolumeIs(w,std::abs(twice)-9*3.14159265358979323846)||MaterialAt(w,{4,4,1})
        ||MaterialAt(w,{32,11,1})||!MaterialAt(w,{16,4,1})||MaterialAt(w,{16,12,1}))return false;
    return FinishShape(w,"concave-link-arm-two-bores",1);
}

bool MixedProfilePicking(V2MainWindow& w)
{
    w.RunCommand("file.new");const auto circle=Coordinates(w,2,{{0,0,0}},8);if(circle.IsNil())return false;
    View(w);if(!ToolButton(w,"part.extrude"))return false;
    const auto inside=w.Viewport().Mapping().Project({2,1,0});if(!inside)return false;
    if(!Mouse(w.Viewport(),{inside->x,inside->y})
        ||!Explain("interior click selects one complete seeded profile",w.Viewport().Selection().entityIds==std::vector<EntityId>{circle}
            &&!app::SelectedProfileSeeds(w.Viewport().Selection()).empty()))return false;
    if(!PickWire(w,circle,Qt::NoModifier,false)
        ||!Explain("boundary click removes the seeded region without leaving an edge reference",
            w.Viewport().Selection().entityIds.empty()&&w.Viewport().Selection().ordered.empty()))return false;
    if(!PickWire(w,circle,Qt::NoModifier))return false;
    const auto refreshed=w.Viewport().Mapping().Project({2,1,0});if(!refreshed)return false;
    return Mouse(w.Viewport(),{refreshed->x,refreshed->y})
        &&Explain("interior click can remove an individually selected complete boundary",w.Viewport().Selection().entityIds.empty());
}

bool PressBoundaryAt(V2MainWindow& w,EntityId id,Vector3 point)
{
    Reveal(w.Viewport());const auto pixel=w.Viewport().Mapping().Project(point);if(!pixel)return false;
    const QPointF at(pixel->x,pixel->y);w.Viewport().HoverAt(at);
    for(std::size_t i=0,n=w.Viewport().CandidateCount();i<n;++i){
        const auto candidate=w.Viewport().CurrentCandidate();
        if(candidate&&candidate->entityId==id&&candidate->kind==app::SelectionElementKind::Edge)
            return Mouse(w.Viewport(),at);
        w.Viewport().CycleCandidate(false);
    }
    return Explain("specified visible boundary is selectable",false);
}

EntityId NetworkProfile(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> sides;
    if(!Outline(w,{{0,0,0},{10,0,0},{10,10,0},{0,10,0}},false,sides))return {};
    const auto outer=JoinWires(w,sides);if(outer.IsNil())return {};
    if(Coordinates(w,0,{{3,0,0},{3,10,0}}).IsNil())return {};
    View(w);w.Viewport().SetViewDirection(ViewDirection::Top);QApplication::processEvents();
    return ToolButton(w,"part.extrude")?outer:EntityId{};
}

bool AdjacentProfilePreservation(V2MainWindow& w)
{
    const auto leftClick=[&](){const auto at=w.Viewport().Mapping().Project({1,5,0});
        return at&&Mouse(w.Viewport(),{at->x,at->y});};
    auto outer=NetworkProfile(w);if(outer.IsNil()||!leftClick())return false;
    const auto seeds=app::SelectedProfileSeeds(w.Viewport().Selection());
    if(!Explain("left cell has its own seed",seeds.size()==1)||!PressBoundaryAt(w,outer,{10,5,0}))return false;
    if(!Explain("right-only boundary does not remove the left cell's seed",app::SelectedProfileSeeds(w.Viewport().Selection())==seeds))return false;
    outer=NetworkProfile(w);if(outer.IsNil()||!PressBoundaryAt(w,outer,{6,0,0}))return false;
    const auto before=w.Viewport().Selection();
    if(!Explain("whole source bottom edge is selected",before.ordered.size()==1&&before.ordered.front().segmentId.has_value()))return false;
    if(!leftClick()||!leftClick())return false;
    const auto after=w.Viewport().Selection();
    return Explain("removing the left cell preserves the original edge extending into the right cell",
        after.entityIds==before.entityIds&&after.ordered.size()==1&&after.ordered.front().kind==before.ordered.front().kind
        &&after.ordered.front().segmentId==before.ordered.front().segmentId&&!after.ordered.front().profileSeed);
}

bool SplitBoundaryCoverage(V2MainWindow& w)
{
    w.RunCommand("file.new");std::vector<EntityId> sides;
    if(!Outline(w,{{0,0,0},{10,0,0},{10,10,0},{0,10,0}},false,sides))return false;
    const auto outer=JoinWires(w,sides);
    if(outer.IsNil()||Coordinates(w,0,{{3,0,0},{3,4,0}}).IsNil())return false;
    View(w);w.Viewport().SetViewDirection(ViewDirection::Top);QApplication::processEvents();
    if(!ToolButton(w,"part.extrude")||!Explain("short internal T branch leaves one closed region",w.Viewport().ProfileRegionCount()==1)
        ||!PressBoundaryAt(w,outer,{6,0,0}))return false;
    auto clickInside=[&](){const auto at=w.Viewport().Mapping().Project({8,5,0});return at&&Mouse(w.Viewport(),{at->x,at->y});};
    if(!clickInside())return false;
    if(!app::SelectedProfileSeeds(w.Viewport().Selection()).empty()&&!clickInside())return false;
    return Explain("interior removal clears the complete source edge covered by several boundary pieces",
        w.Viewport().Selection().entityIds.empty()&&w.Viewport().Selection().ordered.empty());
}
}

std::vector<SelfTestCase> GenericShapesCases()
{
    return {{"HP-GENERIC-01 numeric fine concave lip through UI",FineLip},
        {"HP-GENERIC-02 perforated concave mask offset thickness through UI",OffsetMask},
        {"HP-GENERIC-03 separating boolean produces independent reopened parts through UI",SeparatedCut},
        {"HP-GENERIC-04 rounded organic loft shell through UI and STEP",OrganicShell},
        {"HP-GENERIC-05 separate curved perforated regions through UI",TwoCurvedRings},
        {"HP-EXTENDED-01 swept wing plate through ordinary UI",WingPlate},
        {"HP-EXTENDED-02 hollow cylinder and independent rod through UI",HydraulicCylinder},
        {"HP-EXTENDED-03 hollow curved elbow through UI and STEP",PipeElbow},
        {"HP-EXTENDED-04 multi-hole flange and pipe union through UI",FlangedPipe},
        {"HP-EXTENDED-05 open curved bucket skin through UI",BucketSkin},
        {"HP-EXTENDED-06 tapered twisted airfoil loft through UI",TwistedWing},
        {"HP-EXTENDED-07 spherical vessel poles seam and open port through UI",PiercedVessel},
        {"HP-EXTENDED-08 communicating hollow pipe tee through UI",PipeTee},
        {"HP-EXTENDED-09 concave link arm with independent pin holes through UI",LinkArm},
        {"HP-EXTENDED-10 mixed interior and individual boundary selection through UI",MixedProfilePicking},
        {"HP-EXTENDED-11 adjacent profile seeds and spanning source edge preservation through UI",AdjacentProfilePreservation},
        {"HP-EXTENDED-12 whole edge covered by split pieces in one region through UI",SplitBoundaryCoverage}};
}
}
