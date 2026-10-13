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
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
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
private:
    static base::EntityId Id(const char* suffix){return *base::EntityId::Parse(std::string("00000000-0000-4000-8000-00000000")+suffix);}
    static bool Prepare(V2MainWindow& window,std::vector<base::EntityId>& sources);
    static bool Lights(V2MainWindow& window,const QString& output);
    static bool SaveLights(V2MainWindow& window,const QString& output);
    static void Frame(V2MainWindow& window,const std::vector<base::EntityId>& sources);
};
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
        if(e.kind==domain::EntityKind::Part||e.displayName.starts_with("曲面へ投影")||e.displayName.starts_with("投影した窓"))shown.entityIds.push_back(e.id);
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
std::vector<SelfTestCase> OwnerProjectionCases() {
    if(!std::getenv("KACHACAD_PROJECTION_OWNER_INPUT"))return {};
    return {{std::getenv("KACHACAD_PROJECTION_OWNER_LIGHTS")?"PROJ OWNER actual lamps and doors":"PROJ OWNER actual window face exercise",V2ProjectionOwnerExercise::Run}};
}
}
