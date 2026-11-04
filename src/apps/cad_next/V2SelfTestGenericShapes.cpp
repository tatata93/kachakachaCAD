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
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {
using base::EntityId;
using domain::EntityKind;
using geometry::Vector3;

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
    const auto before=Entities(w,EntityKind::Wire);w.SelectTool(modeling::DrawingTool::Line);
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

bool PickWire(V2MainWindow& w,EntityId id)
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
                if(!Mouse(view,p,Qt::ControlModifier))return false;
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
                if(std::find(selected.begin(),selected.end(),id)==selected.end())
                    w.grab().save(qEnvironmentVariable("KACHACAD_GENERIC_OUTPUT")+"-pick-failure.png");
                return Explain(message.c_str(),std::find(selected.begin(),selected.end(),id)!=selected.end());
            }
            view.CycleCandidate(false);
        }
    }
    return Explain("wire can be picked using the viewport and candidate cycling",false);
}

bool PickShape(V2MainWindow& w,EntityId id,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
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
                if(candidate&&candidate->entityId==id) {
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

bool OffsetWorkPlane(V2MainWindow& w,double z)
{
    w.SetWorkPlaneChooser({});
    View(w);w.RunCommand("workplane.create");
    auto* method=w.findChild<QComboBox*>("workPlaneMethod");auto* reference=w.findChild<QComboBox*>("workPlaneReference");
    if(!Explain("workplane controls are available",method&&reference&&method->isVisible()))return false;
    method->setCurrentIndex(method->findText(QStringLiteral("平面から離す")));
    auto at=reference->findText(QStringLiteral("上面"),Qt::MatchContains);
    if(at<0)at=reference->findText(QStringLiteral("top_XY"),Qt::MatchContains);
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
    return Explain(message.c_str(),pressed&&std::abs(w.Viewport().WorkPlane().origin.z-z)<1e-6);
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
}

std::vector<SelfTestCase> GenericShapesCases()
{
    return {{"HP-GENERIC-01 numeric fine concave lip through UI",FineLip},
        {"HP-GENERIC-02 perforated concave mask offset thickness through UI",OffsetMask},
        {"HP-GENERIC-03 separating boolean produces independent reopened parts through UI",SeparatedCut},
        {"HP-GENERIC-04 rounded organic loft shell through UI and STEP",OrganicShell},
        {"HP-GENERIC-05 separate curved perforated regions through UI",TwoCurvedRings}};
}
}
