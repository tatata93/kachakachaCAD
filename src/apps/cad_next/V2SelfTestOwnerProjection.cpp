#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ProjectionTool.h"
#include "V2EntityTree.h"
#include "V2ExtrudeDock.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QIODevice>
#include <QDir>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QPushButton>
#include <QMouseEvent>
#include <QEvent>
#include <QPointF>
#include <QPixmap>
#include <QString>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cmath>
using namespace kachakacha::v2;
class V2ProjectionOwnerExercise {
public:
    static bool Run(V2MainWindow& window);
    static bool VerifyLastPart(V2MainWindow& window,double expectedVolume);
private:
    static base::EntityId Id(const char* suffix){return *base::EntityId::Parse(std::string("00000000-0000-4000-8000-00000000")+suffix);}
    static bool Prepare(V2MainWindow& window,std::vector<base::EntityId>& sources);
    static bool Lights(V2MainWindow& window,const QString& output);
    static bool LightFaces(V2MainWindow& window,const QString& output);
    static bool LightEndCap(V2MainWindow& window,const QString& output,base::EntityId face);
    static bool SaveLights(V2MainWindow& window,const QString& output);
    static void Frame(V2MainWindow& window,const std::vector<base::EntityId>& sources);
};
bool V2ProjectionOwnerExercise::VerifyLastPart(V2MainWindow& window,double expectedVolume) {
    const auto& entity=window.Session().GetDocument().Snapshot().entities.back();
    const auto native=window.partShapes_.find(entity.id.ToString());if(native==window.partShapes_.end())return false;
    const auto checked=kernel::CheckSolidForExport(native->second,.001);
    if(!checked.HasValue()||!checked.Value().Ok())return false;
    return std::abs(checked.Value().volumeMm3-expectedVolume)<1e-4;
}

bool V2ProjectionOwnerExercise::LightEndCap(V2MainWindow& window,const QString& output,base::EntityId source) {
    const auto target=kernel::OutputFace(window.partShapes_.at(Id("4748").ToString()),0);
    if(!target.HasValue())return false;
    const auto pose=kernel::FacePoseNear(target.Value(),0,{0,-30,20},{});
    const auto bytes=kernel::CaptureOutputShape(target.Value());kernel::ReleaseShape(target.Value());
    if(!pose.HasValue()||!pose.Value().planar||!bytes.HasValue())return false;
    app::SelectionSet selected;selected.entityIds={source};window.Viewport().SetSelection(selected);window.RunCommand("part.extrude");
    const auto center=window.Viewport().ExtrudeHandleOrigin();
    const double sign=pose.Value().point.y<center.y?-1:1;
    std::cout<<"OWNER cap: source "<<center.x<<","<<center.y<<","<<center.z<<" target "
        <<pose.Value().point.x<<","<<pose.Value().point.y<<","<<pose.Value().point.z<<std::endl;
    app::ExtrudeChoice choice;choice.direction=modeling::ExtrudeDirectionMode::CustomXYZ;choice.customDirection={0,sign,0};
    choice.extent=modeling::ExtrudeExtentMode::ToTarget;
    choice.endCap.extendTargetPlane=true;window.ApplyExtrudeChoice(choice);
    auto* pick=window.findChild<QPushButton*>("extrudePickStop");if(!pick)return false;
    pick->click();if(!window.Viewport().ReferencePickPending())return false;
    bool clicked=false;
    for(const auto& view:window.Viewport().ShapeViews()) {
        if(view.entityId!=Id("4748"))continue;
        for(const auto& triangle:view.mesh.triangles) {
            if(triangle.faceIndex!=0)continue;
            const auto screen=window.Viewport().Mapping().Project((triangle.points[0]+triangle.points[1]+triangle.points[2])*(1.0/3));
            if(!screen)continue;const QPointF point(screen->x,screen->y);
            if(!window.Viewport().rect().contains(point.toPoint()))continue;
            QMouseEvent move(QEvent::MouseMove,point,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(&window.Viewport(),&move);
            const auto hit=window.Viewport().CurrentCandidate();
            if(!hit||hit->entityId!=Id("4748")||hit->pickedFaceIndex!=0)continue;
            QApplication::processEvents();window.grab().save(output+"/lamp-central-plane-pick.png");
            QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(&window.Viewport(),&press);QApplication::sendEvent(&window.Viewport(),&release);
            clicked=true;break;
        }
        break;
    }
    if(!clicked||window.Viewport().ReferencePickPending()||window.ExtrudeDock().TargetFaceBrep().empty()
        ||window.ExtrudeDock().TargetEntityId()!=Id("4748")||window.Viewport().Selection().entityIds!=selected.entityIds) {
        std::cout<<"OWNER central face mouse pick failed clicked="<<clicked<<" pending="<<window.Viewport().ReferencePickPending()<<std::endl;return false;
    }
    const int before=selftest::CountOfKind(window,domain::EntityKind::Part);
    QApplication::processEvents();window.grab().save(output+"/lamp-central-plane-preview.png");
    window.ExtrudeDock().PressConfirm();
    if(selftest::CountOfKind(window,domain::EntityKind::Part)!=before+1){std::cout<<window.StatusText().toStdString()<<std::endl;return false;}
    const auto& entity=window.Session().GetDocument().Snapshot().entities.back();
    const auto shape=window.partShapes_.find(entity.id.ToString());if(shape==window.partShapes_.end())return false;
    const auto checked=kernel::CheckSolidForExport(shape->second,.001);
    const auto count=kernel::ShapeFaceCount(shape->second);bool aligned=false;
    if(!checked.HasValue()||!checked.Value().Ok()||!count.HasValue())return false;
    for(std::size_t i=0;i<count.Value();++i) {
        const auto face=kernel::FacePoseNear(shape->second,i,center,{});if(!face.HasValue()||!face.Value().planar)continue;
        const double cosine=std::abs(geometry::Dot(face.Value().normal,pose.Value().normal));
        const double distance=std::abs(geometry::Dot(face.Value().point-pose.Value().point,pose.Value().normal));
        aligned=aligned||(cosine>1-1e-8&&distance<.001);
    }
    if(!aligned)return false;
    std::cout<<"OWNER cap verified: closed export solid, central-plane end within .001 mm, volume "<<checked.Value().volumeMm3<<std::endl;
    window.RunCommand("selection.activate");Frame(window,{});window.grab().save(output+"/lamp-central-plane-result.png");
    app::SelectionSet detail;detail.entityIds={entity.id};window.Viewport().SetSelection(detail);
    window.Viewport().IsolateSelection();window.Viewport().FitToDocument();window.Viewport().SetSelection({});
    window.SetStatus(QStringLiteral("実モデルのライト：中央面まで押し出し。閉ソリッド・終端の同一平面を0.001 mm以内で確認。"));
    QApplication::processEvents();window.grab().save(output+"/lamp-central-plane-detail.png");
    return window.SaveAndReopen("owner-lamp-central-plane.kcd2")&&selftest::CountOfKind(window,domain::EntityKind::Part)==before+1;
}

bool V2ProjectionOwnerExercise::LightFaces(V2MainWindow& window,const QString& output) {
    const auto before=window.Session().GetDocument().Snapshot();
    const int parts=selftest::CountOfKind(window,domain::EntityKind::Part);
    double expectedVolume=0,actualVolume=0,volumeBudget=0;
    constexpr double projectionTolerance=.001;
    auto* tool=V2ProjectionTool::Open(window);
    for(const auto* id:{"0115","011b","0121","0124","012d","0130"}) {
        app::SelectionRef ref;ref.entityId=Id(id);if(!tool->AddWire(ref))return false;
        app::SelectionSet selection;selection.entityIds={ref.entityId};const auto curves=app::SelectedCurves(selection,window.Session().Scene());
        if(curves.size()!=1||curves.front().Kind()!=geometry::CurveKind::Circle)return false;
        expectedVolume+=geometry::kPi*curves.front().Radius()*curves.front().Radius()*.5;
        // Radial-envelope volume budget for this fixture, not a certified global projection-error bound.
        volumeBudget+=geometry::kPi*(2*curves.front().Radius()*projectionTolerance+projectionTolerance*projectionTolerance)*.5;
    }
    for(const auto* id:{"2af9","310c","37d8","3f34"}) {
        app::SelectionRef ref;ref.entityId=Id(id);ref.kind=app::SelectionElementKind::Face;ref.pickedFaceIndex=0;
        ref.hitPoint={0,-27,14};if(!tool->AddTarget(ref))return false;
    }
    tool->findChild<QComboBox*>("projectionOutput")->setCurrentIndex(1);
    tool->findChild<QDoubleSpinBox*>("projectionTolerance")->setValue(projectionTolerance);
    if(!tool->Preview()){std::cout<<"OWNER lamp face preview "<<window.StatusText().toStdString()<<std::endl;return false;}
    Frame(window,{});window.grab().save(output+"/lamp-faces-preview.png");
    if(!tool->Commit())return false;
    std::vector<base::EntityId> faces;
    for(const auto& e:window.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::GuideSurface&&std::none_of(before.entities.begin(),before.entities.end(),[&](const auto& old){return old.id==e.id;}))faces.push_back(e.id);
    std::cout<<"OWNER lamp faces "<<faces.size()<<std::endl;
    // Two lamps cross a support-face seam: six source circles become eight native patches.
    if(faces.size()!=8)return false;
    if(!qEnvironmentVariable("KACHACAD_PROJECTION_OWNER_ENDCAP").isEmpty())return LightEndCap(window,output,faces.front());
    for(const auto& id:faces) {
        app::SelectionSet selection;selection.entityIds={id};window.Viewport().SetSelection(selection);
        window.RunCommand("part.extrude");
        if(!window.Viewport().ExtrudeHandleShown())return false;
        window.ExtrudeDock().ChooseDirection(modeling::ExtrudeDirectionMode::CustomXYZ);
        window.ExtrudeDock().SetCustomDirection({0,-1,0});window.ExtrudeDock().TypeDistanceMm(.5);
        const int count=selftest::CountOfKind(window,domain::EntityKind::Part);
        const bool entered=window.HandleToolKey(Qt::Key_Return,nullptr);
        std::cout<<"OWNER lamp face extrusion "<<id.ToString()<<" "<<entered<<" "<<window.StatusText().toStdString()<<std::endl;
        if(!entered||selftest::CountOfKind(window,domain::EntityKind::Part)!=count+1){std::cout<<"OWNER unexpected part count"<<std::endl;return false;}
        const auto& e=window.Session().GetDocument().Snapshot().entities.back();
        const auto shape=window.partShapes_.find(e.id.ToString());if(shape==window.partShapes_.end()){std::cout<<"OWNER missing committed native shape"<<std::endl;return false;}
        const auto checked=kernel::CheckSolidForExport(shape->second,.001);
        if(!checked.HasValue()||!checked.Value().Ok()||checked.Value().volumeMm3<=0){
            std::cout<<"OWNER solid check "<<checked.FirstMessageJa()<<std::endl;
            if(checked.HasValue())std::cout<<"OWNER closed "<<checked.Value().closed<<" positive "<<checked.Value().positiveVolume<<" selfFree "<<checked.Value().selfIntersectionFree<<" volume "<<checked.Value().volumeMm3<<std::endl;
            return false;
        }
        actualVolume+=checked.Value().volumeMm3;
    }
    std::cout<<"OWNER lamp volume expected "<<expectedVolume<<" actual "<<actualVolume<<" budget "<<volumeBudget<<" mm3"<<std::endl;
    if(std::abs(actualVolume-expectedVolume)>volumeBudget+1e-5)return false;
    window.RunCommand("edit.undo");if(selftest::CountOfKind(window,domain::EntityKind::Part)!=parts+7)return false;
    window.RunCommand("edit.redo");if(selftest::CountOfKind(window,domain::EntityKind::Part)!=parts+8)return false;
    window.RunCommand("selection.activate");Frame(window,{});window.grab().save(output+"/lamp-faces-extruded.png");
    return window.SaveAndReopen("owner-lamp-face-extrusion.kcd2")&&selftest::CountOfKind(window,domain::EntityKind::Part)==parts+8;
}
bool V2ProjectionOwnerExercise::Lights(V2MainWindow& window,const QString& output) {
    std::vector<modeling::KernelShapeHandle> faces;
    for(const auto* id:{"2af9","310c","37d8","3f34","4748"}) {
        const auto face=kernel::OutputFace(window.partShapes_.at(Id(id).ToString()),0);if(!face.HasValue())return false;
        faces.push_back(face.Value());const auto bytes=kernel::CaptureOutputShape(face.Value());
        QFile file(output+"/"+id+".brep");if(!bytes.HasValue()||!file.open(QIODevice::WriteOnly))return false;
        file.write(bytes.Value().data(),bytes.Value().size());
    }
    bool good=true;
    for(const auto* id:{"0115","011b","0121","0124","012d","0130","00c0","00d1","00db"}) {
        app::SelectionSet selection;selection.entityIds={Id(id)};const auto curves=app::SelectedCurves(selection,window.Session().Scene());
        kernel::WireProjectionOptions options;const bool door=id[2]=='c'||id[2]=='d';
        const auto result=kernel::ProjectNativeWires(door?std::vector<modeling::KernelShapeHandle>{faces.back()}:faces,{{curves,{0,-1,0}}},options);
        std::cout<<"OWNER SOURCE "<<id<<" curves "<<curves.size()<<" ";
        if(!result.HasValue()){std::cout<<result.FirstMessageJa()<<std::endl;good=false;continue;}
        std::cout<<"runs "<<result.Value().wires.size()<<" ends "<<result.Value().openEnds<<std::endl;
        for(const auto& run:result.Value().wires) {
            std::cout<<"  segments "<<run.size()<<" closed "<<geometry::SegmentsFormClosedLoop(run,window.Session().GetDocument().Snapshot().settings.tolerance)<<std::endl;
        }
        good=good&&result.Value().wires.size()==1&&result.Value().openEnds==0;
    }
    for(const auto& face:faces)kernel::ReleaseShape(face);if(!good)return false;
    auto* tool=V2ProjectionTool::Open(window);
    for(const auto* id:{"0115","011b","0121","0124","012d","0130"}) {
        app::SelectionRef ref;ref.entityId=Id(id);if(!tool->AddWire(ref))return false;
    }
    for(const auto* id:{"2af9","310c","37d8","3f34"}) {
        app::SelectionRef ref;ref.entityId=Id(id);ref.kind=app::SelectionElementKind::Face;ref.pickedFaceIndex=0;
        ref.hitPoint={0,-27,14};if(!tool->AddTarget(ref))return false;
    }
    if(!tool->Preview())return false;Frame(window,{});window.grab().save(output+"/lamp-preview.png");
    const auto count=window.Session().GetDocument().Snapshot().entities.size();if(!tool->Commit())return false;
    for(const auto* id:{"00c0","00d1","00db"}){app::SelectionRef ref;ref.entityId=Id(id);if(!tool->AddWire(ref))return false;}
    app::SelectionRef central;central.entityId=Id("4748");central.kind=app::SelectionElementKind::Face;central.pickedFaceIndex=0;central.hitPoint={0,-27,14};
    if(!tool->AddTarget(central)||!tool->Preview()||!tool->Commit())return false;
    std::vector<base::EntityId> projected;
    const auto snapshot=window.Session().GetDocument().Snapshot();
    for(std::size_t i=count;i<snapshot.entities.size();++i)if(snapshot.entities[i].kind==domain::EntityKind::Wire)projected.push_back(snapshot.entities[i].id);
    for(std::size_t i=0;i<projected.size();++i) {
        app::SelectionSet selection;selection.entityIds={projected[i]};window.Viewport().SetSelection(selection);
        const auto plan=window.PlanExtrudeFromSelection();std::cout<<"OWNER projected "<<i<<" extrusion ready "<<plan.readyToPreview<<" reason "<<plan.needsJa<<std::endl;
        if(i>=6) {
            window.RunCommand("part.extrude");if(!window.Viewport().ExtrudeHandleShown())return false;
            window.ExtrudeDock().TypeDistanceMm(.5);
            if(!window.HandleToolKey(Qt::Key_Return,nullptr)){std::cout<<"OWNER extrusion "<<window.StatusText().toStdString()<<std::endl;return false;}
            const auto& e=window.Session().GetDocument().Snapshot().entities.back();
            const auto shape=window.partShapes_.find(e.id.ToString());if(shape==window.partShapes_.end())return false;
            const auto mesh=kernel::BuildShapeMesh(shape->second);if(!mesh.HasValue()||mesh.Value().triangles.empty())return false;
            double low=1e100,high=-1e100;for(const auto& t:mesh.Value().triangles)for(const auto& p:t.points){low=std::min(low,p.y);high=std::max(high,p.y);}
            std::cout<<"OWNER actual extrusion depth "<<high-low<<" mm"<<std::endl;if(std::abs(high-low-.5)>1e-5)return false;
        }
    }
    window.RunCommand("selection.activate");Frame(window,{});window.grab().save(output+"/lamp-result.png");
    return SaveLights(window,output);
}
bool V2ProjectionOwnerExercise::SaveLights(V2MainWindow& window,const QString& output) {
    if(selftest::CountOfKind(window,domain::EntityKind::Part)!=13)return false;
    window.RunCommand("edit.undo");if(selftest::CountOfKind(window,domain::EntityKind::Part)!=12)return false;
    window.RunCommand("edit.redo");if(selftest::CountOfKind(window,domain::EntityKind::Part)!=13)return false;
    const auto file=output+"/115-lamps-door-projection.kcd2";
    window.SetPathChooser([file](bool){return file;});window.RunCommand("file.save_as");window.SetPathChooser({});
    if(!window.StatusText().contains(QStringLiteral("保存しました"))||!window.OpenDocumentFile(file))return false;
    std::cout<<"OWNER 6 closed lamps + 3 closed door/window outlines; 3 extrusions saved and reopened"<<std::endl;
    return selftest::CountOfKind(window,domain::EntityKind::Part)==13;
}
bool V2ProjectionOwnerExercise::Prepare(V2MainWindow& window,std::vector<base::EntityId>& sources) {
    auto& document=window.Session().GetDocument();document::Group group;group.id=window.ids_->NextTyped<base::IdKind::Group>();
    group.displayName="窓投影の確認";group.parentId=document.Snapshot().settings.activeGroupId;
    if(!document.Run(document::AddGroupCommand(group)).committed||!document.Run(document::SetActiveGroupCommand(group.id)).committed)return false;
    const char* originalIds[]={"0057","005e","00db"};const char* labels[]={"投影元: 右窓（不足辺補完）","投影元: 左窓（不足辺補完）","投影元: 中央窓"};
    for(int i=0;i<3;++i) {
        app::SelectionSet selected;selected.entityIds={Id(originalIds[i])};auto curves=app::SelectedCurves(selected,window.Session().Scene());
        if(curves.empty())return false;
        if(!geometry::SegmentsFormClosedLoop(curves,document.Snapshot().settings.tolerance)) {
            const auto closing=geometry::CurveSegment::MakeLine(curves.back().EndPoint(),curves.front().StartPoint());if(!closing.HasValue())return false;curves.push_back(closing.Value());
        }
        for(auto& c:curves)c=geometry::TranslateCurve(c,{0,-35,0});
        const auto id=window.AddPlainWire(curves,labels[i]);if(id.IsNil())return false;sources.push_back(id);
    }
    window.AdoptCurrentDocument();return true;
}
void V2ProjectionOwnerExercise::Frame(V2MainWindow& window,const std::vector<base::EntityId>& sources) {
    app::SelectionSet shown;shown.entityIds=sources;
    for(const auto& e:window.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::Part||e.displayName.starts_with("曲面へ投影")||e.displayName.starts_with("投影した"))shown.entityIds.push_back(e.id);
    window.Viewport().SetSelection(shown);window.Viewport().IsolateSelection();window.Viewport().SetSelection({});
    window.Viewport().SetViewDirection(ViewDirection::Isometric);window.Viewport().FitToDocument();
    for(QTreeWidgetItemIterator row(window.EntityTree());*row;++row) {
        const auto key=V2EntityTree::KeyOf(*row);
        if(key.contains("/section:")&&((*row)->text(0)==QStringLiteral("ワイヤー")||(*row)->text(0)==QStringLiteral("作業面")))
            (*row)->setExpanded(false);
    }
    for(const auto& id:sources)if(auto* row=window.ItemOfEntity(id))for(auto* parent=row->parent();parent;parent=parent->parent())parent->setExpanded(true);
    QApplication::processEvents();
}
bool V2ProjectionOwnerExercise::Run(V2MainWindow& window) {
    const auto path=qEnvironmentVariable("KACHACAD_PROJECTION_OWNER_INPUT");
    const auto output=qEnvironmentVariable("KACHACAD_PROJECTION_OWNER_OUTPUT");
    if(path.isEmpty()||output.isEmpty()||!QDir().mkpath(output))return false;
    QFile original(path);if(!original.open(QIODevice::ReadOnly))return false;const auto originalBytes=original.readAll();original.close();
    if(!window.OpenDocumentFile(path))return false;
    std::cout<<"OWNER loaded: "<<window.KernelShapeCount()<<" native shapes"<<std::endl;
    if(!qEnvironmentVariable("KACHACAD_PROJECTION_OWNER_LIGHT_FACES").isEmpty()) {
        const bool good=LightFaces(window,output);QFile reread(path);
        return good&&reread.open(QIODevice::ReadOnly)&&reread.readAll()==originalBytes;
    }
    if(!qEnvironmentVariable("KACHACAD_PROJECTION_OWNER_LIGHTS").isEmpty()) {
        const bool good=Lights(window,output);QFile reread(path);
        return good&&reread.open(QIODevice::ReadOnly)&&reread.readAll()==originalBytes;
    }
    std::map<std::string,std::string> nativeBefore;
    for(const auto& [id,shape]:window.partShapes_)nativeBefore[id]=kernel::CaptureOutputShape(shape).Value();
    const auto before=window.Session().GetDocument().Snapshot();
    std::vector<base::EntityId> sources;if(!Prepare(window,sources))return false;
    Frame(window,sources);auto* tool=V2ProjectionTool::Open(window);
    for(const auto& id:sources){app::SelectionRef ref;ref.entityId=id;if(!tool->AddWire(ref))return false;}
    const char* targets[]={"2af9","37d8","4748"};
    for(int i=0;i<3;++i) {
        app::SelectionRef ref;ref.entityId=Id(targets[i]);ref.kind=app::SelectionElementKind::Face;ref.pickedFaceIndex=0;
        ref.hitPoint={i==0?10.0:(i==1?-10.0:0.0),-27,21};if(!tool->AddTarget(ref))return false;
    }
    tool->findChild<QListWidget*>("projectionWireRows")->setCurrentRow(-1);
    for(auto* b:tool->findChildren<QPushButton*>())if(b->text()==QStringLiteral("Y方向"))b->click();
    tool->findChild<QDoubleSpinBox*>("projectionDistance")->setValue(17);
    tool->findChild<QComboBox*>("projectionOutput")->setCurrentIndex(1);
    if(!tool->Preview()){std::cout<<"OWNER preview failed: "<<tool->findChild<QLabel*>("projectionStatus")->text().toStdString()<<std::endl;return false;}
    std::cout<<"OWNER preview: "<<tool->findChild<QLabel*>("projectionStatus")->text().toStdString()<<std::endl;
    for(const auto& [id,bytes]:nativeBefore)if(kernel::CaptureOutputShape(window.partShapes_.at(id)).Value()!=bytes)return false;
    window.Viewport().update();QApplication::processEvents();window.grab().save(output+"/owner-projection-ui.png");
    const auto entityCount=window.Session().GetDocument().Snapshot().entities.size();
    if(!tool->Commit())return false;
    const auto& completed=window.Session().GetDocument().Snapshot();std::size_t windows=0,closed=0;double surfaceError=0;
    for(std::size_t i=entityCount;i<completed.entities.size();++i) {
        const auto& e=completed.entities[i];
        if(e.kind==domain::EntityKind::GuideSurface)++windows;
        if(e.kind==domain::EntityKind::Wire) {
            app::SelectionSet selected;selected.entityIds={e.id};const auto curves=app::SelectedCurves(selected,window.Session().Scene());
            if(!geometry::SegmentsFormClosedLoop(curves,completed.settings.tolerance))return false;
            const auto support=kernel::OutputFace(window.partShapes_.at(Id(targets[closed]).ToString()),0);if(!support.HasValue())return false;
            for(const auto& c:curves)for(int j=0;j<=32;++j) {
                const auto point=c.Evaluate(j/32.0);const auto frame=kernel::OutputSurfaceFrame(support.Value(),point);
                if(!frame.HasValue()){kernel::ReleaseShape(support.Value());return false;}
                surfaceError=std::max(surfaceError,(frame.Value().origin-point).Length());
            }
            kernel::ReleaseShape(support.Value());++closed;
        }
    }
    std::cout<<"OWNER result: "<<windows<<" native window faces / "<<closed<<" closed wires"<<std::endl;
    std::cout<<"OWNER independent surface samples: max error "<<surfaceError<<" mm"<<std::endl;
    if(windows!=3||closed!=3||surfaceError>.003)return false;
    for(const auto& e:before.entities) {
        const auto* unchanged=window.Session().GetDocument().FindEntity(e.id);
        if(!unchanged||unchanged->displayName!=e.displayName||unchanged->visibility!=e.visibility||unchanged->groupId!=e.groupId)return false;
    }
    if(!window.Session().GetDocument().Run(document::SetVisibilityCommand(sources,domain::Visibility::Hidden)).committed)return false;
    window.AdoptCurrentDocument();Frame(window,{});window.Viewport().SetSelection({});window.RunCommand("selection.activate");
    window.grab().save(output+"/owner-window-result.png");
    const auto resultFile=output+"/115-window-projection.kcd2";
    window.SetPathChooser([resultFile](bool){return resultFile;});window.RunCommand("file.save_as");window.SetPathChooser({});
    if(!window.StatusText().contains(QStringLiteral("保存しました"))||!window.OpenDocumentFile(resultFile)||window.KernelShapeCount()!=23)return false;
    std::cout<<"OWNER saved and reopened; original names, visibility, groups and native bodies unchanged"<<std::endl;
    QFile reread(path);return reread.open(QIODevice::ReadOnly)&&reread.readAll()==originalBytes;
}
namespace kachakacha::v2::selftest {
bool VerifyProjectedLampNative(V2MainWindow& window,double expectedVolume) {
    return V2ProjectionOwnerExercise::VerifyLastPart(window,expectedVolume);
}
std::vector<SelfTestCase> OwnerProjectionCases() {
    if(!std::getenv("KACHACAD_PROJECTION_OWNER_INPUT"))return {};
    return {{std::getenv("KACHACAD_PROJECTION_OWNER_LIGHT_FACES")?"PROJ OWNER six projected lamp faces extrude":
        (std::getenv("KACHACAD_PROJECTION_OWNER_LIGHTS")?"PROJ OWNER actual lamps and doors":"PROJ OWNER actual window face exercise"),V2ProjectionOwnerExercise::Run}};
}
}
