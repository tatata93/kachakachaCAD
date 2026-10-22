#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2DocumentOpen.h"
#include "V2DocumentCache.h"
#include "V2Viewport.h"
#include "V2GeometryJob.h"
#include "V2HeavyJobFixture.h"
#include "V2ThickenDock.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QIODevice>
#include <QKeyEvent>
#include <QObject>
#include <QProcess>
#include <QPushButton>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QImage>
#include <QColor>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace kachakacha::v2::selftest {
bool MakeCurvedGuideSurface(V2MainWindow&);
namespace {
struct SoftwareRendererScope {
    QByteArray previous=qgetenv("KACHACAD_SOFTWARE_RENDER");
    SoftwareRendererScope(){qputenv("KACHACAD_SOFTWARE_RENDER","1");}
    ~SoftwareRendererScope(){if(previous.isNull())qunsetenv("KACHACAD_SOFTWARE_RENDER");else qputenv("KACHACAD_SOFTWARE_RENDER",previous);}
};
std::string Snapshot(V2MainWindow& w) {
    io::DocumentFile file;file.snapshot=w.Session().GetDocument().Snapshot();return io::WriteDocumentJson(file);
}
bool Write(const QString& path,const io::DocumentFile& file) {
    const auto bytes=io::SaveDocument(file);
    return bytes.HasValue()&&io::WriteFileAtomically(path.toStdString(),bytes.Value()).HasValue();
}
io::DocumentFile HeavyFile(V2MainWindow& w,bool invalid,bool warped) {
    static const char* brep=
#include "../../../tests_v2/fixtures/warped-panel-source.inc"
    ;
    std::string surfaceBrep=brep;
    if(!warped&&!invalid){const auto source=HeavySmoothSurface();if(!source.HasValue())throw std::runtime_error(source.FirstSummaryJa());
        const auto captured=kernel::CaptureOutputShape(source.Value());kernel::ReleaseShape(source.Value());
        if(!captured.HasValue())throw std::runtime_error(captured.FirstSummaryJa());surfaceBrep=captured.Value();}
    auto file=V2DocumentOpen::FileForSave(w);base::DeterministicIdGenerator ids(900000);
    domain::Feature frozen;frozen.id=ids.NextTyped<base::IdKind::Feature>();
    frozen.type=domain::FeatureType::FreezeDerived;frozen.displayName=warped?"ねじれ帯の面":"滑らかな壺の面";
    domain::FreezeDerivedDefinition definition;definition.frozenBrep=invalid?"invalid BRep":surfaceBrep;frozen.definition=definition;
    domain::Entity surface;surface.id=ids.NextTyped<base::IdKind::Entity>();surface.kind=domain::EntityKind::GuideSurface;
    surface.createdBy=frozen.id;surface.displayName=frozen.displayName;surface.editPolicy=domain::EditPolicy::Frozen;
    frozen.outputs.push_back({"surface",surface.id,surface.kind});
    file.snapshot.features.push_back(frozen);file.snapshot.entities.push_back(surface);
    domain::Feature thick;thick.id=ids.NextTyped<base::IdKind::Feature>();thick.type=domain::FeatureType::ThickenSurface;
    thick.displayName=warped?"ねじれ帯の厚み":"複雑な壺の厚み";thick.inputEntityIds={surface.id};
    domain::ThickenSurfaceDefinition td;td.surface=surface.id;td.thickness.value=.8;td.placement=2;thick.definition=td;
    domain::Entity part;part.id=ids.NextTyped<base::IdKind::Entity>();part.kind=domain::EntityKind::Part;
    part.createdBy=thick.id;part.displayName=thick.displayName;part.editPolicy=domain::EditPolicy::Derived;
    thick.outputs.push_back({"part",part.id,part.kind});file.snapshot.features.push_back(thick);file.snapshot.entities.push_back(part);
    return file;
}
bool ExerciseStop(V2MainWindow& w,int mode) {
    QTemporaryDir temp;if(!temp.isValid())return false;w.RunCommand("file.new");
    const auto oldPath=temp.filePath("original.kcd2"),input=temp.filePath("incoming.kcd2");
    w.SetPathChooser([oldPath](bool){return oldPath;});w.RunCommand("file.save_as");
    // Saving establishes a history boundary; add a real edit after that boundary.
    if(!DrawRectangleByHand(w))return false;
    if(!Explain("select rectangle for source face",ClickOnAnyCurve(w,Qt::NoModifier)))return false;
    w.RunCommand("surface.create");if(!Explain("confirm source face",w.HandleToolKey(Qt::Key_Return,nullptr)))return false;
    w.SetMode(app::UiMode::Part);w.RunCommand("part.thicken");
    if(!Explain("old document contains a real solid",ClickOnAnyGuideSurface(w)&&w.ThickenDock().ClickConfirm()))return false;
    const auto before=Snapshot(w);
    w.RunCommand("edit.undo");const auto undo=Snapshot(w);w.RunCommand("edit.redo");
    if(!Explain("baseline Undo/Redo restores the document",Snapshot(w)==before&&undo!=before))return false;
    const auto cache=kernel::CachedShapeCount();const int shapes=w.KernelShapeCount();
    const auto baselineRevision=w.Session().GetDocument().Revision();
    if(!Write(input,HeavyFile(w,mode==2,mode==3)))return false;
    const auto inputBytes=io::ReadWholeFile(input.toStdString());
    if(mode==0){w.RunCommand("part.thicken");if(!Explain("old editor is armed before opening",w.ThickenShelfShown()))return false;}
    QTimer timer;timer.setInterval(5);QElapsedTimer running,total;total.start();bool stopped=false,timedOut=false;int ticks=0;
    QObject::connect(&timer,&QTimer::timeout,&w,[&]{
        auto* dialog=QApplication::activeModalWidget();if(!dialog||dialog->objectName()!="geometryJobDialog")return;++ticks;
        if(total.elapsed()>10000){timedOut=true;if(auto* child=dialog->findChild<QProcess*>("geometryJobProcess"))child->kill();return;}
        if(dialog->property("calculationPhase").toString().contains(QStringLiteral("複雑な壺の厚み"))&&!running.isValid())running.start();
        if(stopped||!running.isValid()||running.elapsed()<150)return;
        const auto report=qEnvironmentVariable("KACHACAD_JOB_REPORT_DIR");
        if(!report.isEmpty()){QDir().mkpath(report);dialog->grab().save(report+QString("/document-stop-%1.png").arg(mode));}
        if(mode==0){stopped=true;QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(dialog,&escape);}
        if(mode==1){auto* child=dialog->findChild<QProcess*>("geometryJobProcess");if(child){stopped=true;child->kill();}}
    });
    timer.start();w.SetPathChooser([input](bool){return input;});w.RunCommand("file.open");timer.stop();
    std::cout<<"document stop mode="<<mode<<", elapsed="<<total.elapsed()<<" ms, UI ticks="<<ticks<<'\n';
    std::cout<<"preserved: document="<<(Snapshot(w)==before)<<", revision="<<(w.Session().GetDocument().Revision()==baselineRevision)
        <<", cache="<<cache<<" -> "<<kernel::CachedShapeCount()<<", displayed shapes="<<shapes<<" -> "<<w.KernelShapeCount()<<'\n';
    if(!Explain("file open failure/cancel preserves the old document and cache",(mode>=2||stopped)&&!timedOut&&ticks>0&&total.elapsed()<10000
        &&Snapshot(w)==before&&w.Session().GetDocument().Revision()==baselineRevision
        &&kernel::CachedShapeCount()==cache&&w.KernelShapeCount()==shapes&&!V2GeometryJob::Active()))return false;
    if(!Explain("input file remains byte identical",io::ReadWholeFile(input.toStdString()).Value()==inputBytes.Value()))return false;
    w.RunCommand("edit.undo");if(!Explain("original Undo remains usable",Snapshot(w)==undo))return false;
    w.RunCommand("edit.redo");if(!Explain("original Redo remains usable",Snapshot(w)==before))return false;
    bool asked=false;w.SetPathChooser([&](bool){asked=true;return QString();});w.RunCommand("file.save");
    if(!Explain("original save path remains in effect",!asked&&io::LoadDocument(io::ReadWholeFile(oldPath.toStdString()).Value()).HasValue()))return false;
    return Explain("a normal document opens after the failed attempt",w.OpenDocumentFile(oldPath)&&Snapshot(w)==before);
}
bool Cancel(V2MainWindow& w){return ExerciseStop(w,0);}
bool Crash(V2MainWindow& w){return ExerciseStop(w,1);}
bool Failure(V2MainWindow& w){return ExerciseStop(w,2);}
bool Unsupported(V2MainWindow& w){return ExerciseStop(w,3);}
bool Metadata(V2MainWindow& w) {
    QTemporaryDir temp;if(!temp.isValid()||!w.ApplyManualState(QStringLiteral("select")))return false;
    auto file=V2DocumentOpen::FileForSave(w);file.metadata={"saved title","saved author","saved description"};
    file.sideEntries.push_back({"extras/reference.txt","reference bytes"});
    file.uiState=io::JsonValue::Object({{"futureSetting",io::JsonValue::String("preserved")}});
    const auto path=temp.filePath("metadata.kcd2");if(!Write(path,file)||!w.OpenDocumentFile(path))return false;
    w.SetPathChooser([](bool){return QString();});w.RunCommand("file.save");
    const auto bytes=io::ReadWholeFile(path.toStdString());if(!bytes.HasValue())return false;
    const auto saved=io::LoadDocument(bytes.Value());if(!saved.HasValue())return false;
    return Explain("save after prepared open preserves editable history and unknown file data",io::WriteDocumentJson(saved.Value())==io::WriteDocumentJson(file)
        &&saved.Value().sideEntries.size()==1&&saved.Value().sideEntries.front().data=="reference bytes");
}
struct CacheScope {
    QByteArray previous=qgetenv("KACHACAD_DOCUMENT_CACHE_DIR");QTemporaryDir folder;
    CacheScope(){qputenv("KACHACAD_DOCUMENT_CACHE_DIR",folder.path().toUtf8());}
    ~CacheScope(){if(previous.isNull())qunsetenv("KACHACAD_DOCUMENT_CACHE_DIR");else qputenv("KACHACAD_DOCUMENT_CACHE_DIR",previous);}
};
bool SameMesh(const modeling::ShapeMesh& a,const modeling::ShapeMesh& b) {
    if(a.edges!=b.edges||a.isoLines!=b.isoLines||a.minimum!=b.minimum||a.maximum!=b.maximum
        ||a.closed!=b.closed||a.faceCount!=b.faceCount||a.triangles.size()!=b.triangles.size())return false;
    for(std::size_t i=0;i<a.triangles.size();++i) {
        const auto& x=a.triangles[i];const auto& y=b.triangles[i];
        if(x.points!=y.points||x.normal!=y.normal||x.vertexNormals!=y.vertexNormals||x.faceIndex!=y.faceIndex)return false;
    }
    return true;
}
bool Cache(V2MainWindow& w) {
    CacheScope scope;if(!scope.folder.isValid())return false;
    w.RunCommand("file.new");if(!DrawRectangleByHand(w)||!ClickOnAnyCurve(w,Qt::NoModifier))return false;
    w.RunCommand("surface.create");if(!w.HandleToolKey(Qt::Key_Return,nullptr))return false;
    const auto path=scope.folder.filePath("model.kcd2");
    w.SetPathChooser([path](bool){return path;});w.RunCommand("file.save_as");
    const auto original=io::ReadWholeFile(path.toStdString());if(!original.HasValue())return false;
    const auto cached=V2DocumentCache::Read(original.Value());
    if(!Explain("saving seeds a complete calculation and display mesh cache",cached&&cached->meshes.size()==1
        &&w.Viewport().ShapeViews().size()==1&&SameMesh(cached->meshes.begin()->second,w.Viewport().ShapeViews().front().mesh)))return false;
    auto changed=V2DocumentOpen::FileForSave(w);changed.metadata.description="different file bytes";
    const auto bytes=io::SaveDocument(changed);
    if(!Explain("any source byte change invalidates cached result",bytes.HasValue()&&!V2DocumentCache::Read(bytes.Value())))return false;
    bool hit=false;QTimer timer;timer.setInterval(1);
    QObject::connect(&timer,&QTimer::timeout,&w,[&]{auto* job=QApplication::activeModalWidget();
        if(job&&job->property("calculationPhase").toString().contains(QStringLiteral("内容が一致")))hit=true;});
    QElapsedTimer elapsed;elapsed.start();timer.start();const bool opened=w.OpenDocumentFile(path);timer.stop();
    std::cout<<"cached saved document open="<<elapsed.elapsed()<<" ms\n";
    if(!Explain("cache restores exact native shape with editable history",opened&&hit&&w.KernelShapeCount()==1&&w.RebuildProblems().isEmpty()))return false;
    if(!Explain("warm open restores all display triangles, face indices, normals and curves",w.Viewport().ShapeViews().size()==1
        &&SameMesh(cached->meshes.begin()->second,w.Viewport().ShapeViews().front().mesh)))return false;
    const auto cachePath=scope.folder.filePath(V2DocumentCache::Key(original.Value())+".bin");
    QFile corrupted(cachePath);if(!corrupted.open(QIODevice::ReadWrite)||!corrupted.seek(corrupted.size()-1))return false;
    const char byte=char(corrupted.read(1).at(0)^0x7f);corrupted.seek(corrupted.size()-1);corrupted.write(&byte,1);corrupted.close();
    if(!Explain("corrupted cache rejected before native restoration",!V2DocumentCache::Read(original.Value())))return false;
    hit=false;timer.start();elapsed.restart();const bool rebuilt=w.OpenDocumentFile(path);timer.stop();
    std::cout<<"cold rebuild after cache corruption="<<elapsed.elapsed()<<" ms\n";
    return Explain("bad cache falls back to full reconstruction and leaves source intact",rebuilt&&!hit&&w.KernelShapeCount()==1
        &&V2DocumentCache::Read(original.Value()).has_value()&&io::ReadWholeFile(path.toStdString()).Value()==original.Value());
}
bool HiddenMesh(V2MainWindow& w) {
    CacheScope scope;if(!scope.folder.isValid())return false;
    w.RunCommand("file.new");if(!DrawRectangleByHand(w)||!ClickOnAnyCurve(w,Qt::NoModifier))return false;
    w.RunCommand("surface.create");if(!w.HandleToolKey(Qt::Key_Return,nullptr))return false;
    if(w.Viewport().ShapeViews().size()!=1)return false;
    auto file=V2DocumentOpen::FileForSave(w);
    // Compare against eager reconstruction of this same serialized definition,
    // rather than the interactive builder's permissible topology ordering.
    const auto visiblePath=scope.folder.filePath("visible.kcd2");
    if(!Write(visiblePath,file)||!w.OpenDocumentFile(visiblePath)||w.Viewport().ShapeViews().size()!=1)return false;
    const auto visibleBytes=io::ReadWholeFile(visiblePath.toStdString());if(!visibleBytes.HasValue())return false;
    const auto visibleData=V2DocumentCache::Read(visibleBytes.Value());if(!visibleData)return false;
    for(auto& entity:file.snapshot.entities)if(entity.kind==domain::EntityKind::GuideSurface)entity.visibility=domain::Visibility::Hidden;
    const auto path=scope.folder.filePath("hidden.kcd2");if(!Write(path,file)||!w.OpenDocumentFile(path))return false;
    const auto bytes=io::ReadWholeFile(path.toStdString());if(!bytes.HasValue())return false;
    const auto cached=V2DocumentCache::Read(bytes.Value());
    if(!Explain("hidden face keeps native geometry and history without eager display meshing",cached&&cached->guides.size()==1&&cached->guides==visibleData->guides&&cached->meshes.empty()&&w.KernelShapeCount()==1&&w.Viewport().ShapeViews().empty()))return false;
    w.RunCommand("view.show_all");
    // Transfer uses a BRep text round-trip. Remeshing that same transferred
    // native shape must be exact; a worker mesh can differ at sub-ulp edge ends.
    const auto restored=kernel::RestoreOutputShape(cached->guides.begin()->second);
    if(!restored.HasValue())return false;
    const auto eager=kernel::BuildShapeMesh(restored.Value(),0.0,8);kernel::ReleaseShape(restored.Value());
    if(!Explain("lazy and eager display meshes of the transferred native face are identical",eager.HasValue()
        &&w.Viewport().ShapeViews().size()==1&&SameMesh(eager.Value(),w.Viewport().ShapeViews().front().mesh)))return false;
    w.RunCommand("edit.undo");
    if(!Explain("Undo of visibility retains the native shape",w.Viewport().ShapeViews().empty()&&w.KernelShapeCount()==1))return false;
    return Explain("warm hidden read is still exact and source remains intact",w.OpenDocumentFile(path)&&w.Viewport().ShapeViews().empty()&&w.KernelShapeCount()==1&&io::ReadWholeFile(path.toStdString()).Value()==bytes.Value());
}
bool ParallelRead(V2MainWindow& w) {
    QTemporaryDir folder;if(!folder.isValid())return false;w.RunCommand("file.new");
    if(!MakeCurvedGuideSurface(w))return false;auto file=V2DocumentOpen::FileForSave(w);
    const auto source=std::find_if(file.snapshot.entities.begin(),file.snapshot.entities.end(),[](const auto& e){return e.kind==domain::EntityKind::GuideSurface;});
    if(source==file.snapshot.entities.end())return false;
    const auto feature=std::find_if(file.snapshot.features.begin(),file.snapshot.features.end(),[&](const auto& f){return f.id==source->createdBy;});
    if(feature==file.snapshot.features.end())return false;base::DeterministicIdGenerator ids(940000);
    auto clone=*feature;auto entity=*source;clone.id=ids.NextTyped<base::IdKind::Feature>();entity.id=ids.NextTyped<base::IdKind::Entity>();
    entity.createdBy=clone.id;clone.outputs={{"surface",entity.id,entity.kind}};file.snapshot.features.push_back(clone);file.snapshot.entities.push_back(entity);
    const auto path=folder.filePath("independent.kcd2");if(!Write(path,file))return false;
    const auto original=io::ReadWholeFile(path.toStdString());if(!original.HasValue())return false;
    struct SerialFlag {QByteArray previous=qgetenv("KACHACAD_OPEN_SERIAL");~SerialFlag(){if(previous.isNull())qunsetenv("KACHACAD_OPEN_SERIAL");else qputenv("KACHACAD_OPEN_SERIAL",previous);}}flag;
    std::optional<V2PreparedDocument> serial,parallel;
    for(bool sequential:{true,false}) {
        CacheScope scope;if(!scope.folder.isValid())return false;
        if(sequential)qputenv("KACHACAD_OPEN_SERIAL","1");else qunsetenv("KACHACAD_OPEN_SERIAL");
        if(!w.OpenDocumentFile(path))return false;
        auto data=V2DocumentCache::Read(original.Value());if(!data)return false;
        (sequential?serial:parallel)=std::move(data);
    }
    if(!Explain("parallel cold read preserves exact native BRep, editable archive and curve correspondence",serial&&parallel&&serial->guides.size()==2
        &&serial->guides==parallel->guides&&serial->parts==parallel->parts&&serial->archive==parallel->archive
        &&serial->guideEdges.size()==parallel->guideEdges.size()&&serial->meshes.size()==parallel->meshes.size()))return false;
    for(const auto& [id,edges]:serial->guideEdges) {
        if(!parallel->guideEdges.contains(id)||edges.size()!=parallel->guideEdges.at(id).size())return false;
        for(std::size_t i=0;i<edges.size();++i){const auto& a=edges[i];const auto& b=parallel->guideEdges.at(id)[i];
            if(!Explain("parallel native boundaries retain curve types and exact parameters",a.Kind()==b.Kind()&&a.ControlPoints()==b.ControlPoints()
                &&a.Center()==b.Center()&&a.Normal()==b.Normal()&&a.ReferenceDirection()==b.ReferenceDirection()&&a.Radius()==b.Radius()
                &&a.StartAngleRad()==b.StartAngleRad()&&a.SweepAngleRad()==b.SweepAngleRad()))return false;}
    }
    for(const auto& [id,mesh]:serial->meshes)if(!Explain("parallel rebuild retains exact display triangles normals and face indices",parallel->meshes.contains(id)&&SameMesh(mesh,parallel->meshes.at(id))))return false;
    return Explain("parallel rebuild does not overwrite the user's model",io::ReadWholeFile(path.toStdString()).Value()==original.Value());
}
void InteractionTiming(V2MainWindow& w,const char* label,ViewDirection direction=ViewDirection::Top) {
    auto& v=w.Viewport();w.resize(1500,950);QApplication::processEvents();
    v.SetViewDirection(direction);v.SetViewCenter({0,0,0});v.SetVisibleWidthMm(35);
    QImage image(v.size(),QImage::Format_ARGB32);
    for(bool profiles:{false,true}) {
        v.SetProfileRegionPicking(profiles);v.render(&image);
        std::vector<double> hover,paint;
        for(int i=0;i<24;++i) {
            QElapsedTimer timer;timer.start();
            v.HoverAt({v.width()*(.35+.012*i),v.height()*.5});
            hover.push_back(timer.nsecsElapsed()/1e6);timer.restart();v.render(&image);
            paint.push_back(timer.nsecsElapsed()/1e6);
        }
        for(auto* values:{&hover,&paint})std::sort(values->begin(),values->end());
        std::cout<<"interaction "<<label<<" profiles="<<profiles
            <<" triangles="<<v.ShapeTriangleCount()<<" hover median/p95 ms="<<hover[12]<<"/"<<hover[22]
            <<" paint median/p95 ms="<<paint[12]<<"/"<<paint[22]<<std::endl;
    }
    v.SetProfileRegionPicking(false);
}
bool Interaction(V2MainWindow& w) {
    w.RunCommand("file.new");w.RunCommand("selection.activate");
    modeling::ShapeMesh mesh;mesh.faceCount=4;
    for(int y=0;y<100;++y)for(int x=0;x<100;++x) {
        const double a=-10+x*.2,b=-10+y*.2;
        for(auto points:{std::array<geometry::Vector3,3>{{{a,b,0},{a+.2,b,0},{a+.2,b+.2,0}}},
            std::array<geometry::Vector3,3>{{{a,b,0},{a+.2,b+.2,0},{a,b+.2,0}}}}) {
            modeling::MeshTriangle triangle;triangle.points=points;triangle.normal={0,0,1};
            triangle.faceIndex=x/25;mesh.triangles.push_back(triangle);
        }
    }
    modeling::RefreshBounds(mesh);base::DeterministicIdGenerator ids(910000);
    const auto id=ids.NextTyped<base::IdKind::Entity>();
    w.Viewport().SetShapeViews({{id,mesh,true}});
    InteractionTiming(w,"dense GPU");
    // An empty image must not force the depth-composited software path.
    w.Viewport().SetImageViews({V2ImageView{}});InteractionTiming(w,"dense image path");
    w.Viewport().SetImageViews({});w.Viewport().SetShapeViews({});
    const SoftwareRendererScope software;
    V2Viewport v(w.Session());v.resize(900,600);v.SetShapeViews({{id,std::move(mesh),true}});
    v.SetViewDirection(ViewDirection::Top);v.SetVisibleWidthMm(30);v.SetProfileRegionPicking(true);
    v.SetImageViews({V2ImageView{}});QImage first(v.size(),QImage::Format_ARGB32),second=first;
    v.HoverAt({420,300});v.render(&first);
    const auto initial=v.InteractionCounts();
    v.render(&second);const bool identical=first==second;
    v.HoverAt({425,300});v.render(&second);const auto reused=v.InteractionCounts();
    if(!Explain("unchanged face reuses exact raster and immutable partitions",identical
        &&initial.rasterizations==reused.rasterizations&&initial.facePartitions==reused.facePartitions
        &&reused.meshTraversals==initial.meshTraversals+1))return false;
    v.HoverAt({600,300});v.render(&second);
    if(!Explain("face hover changes color without repartitioning geometry",first!=second
        &&v.InteractionCounts().facePartitions==initial.facePartitions))return false;
    const auto invalidates=[&](auto change){const auto before=v.InteractionCounts().rasterizations;
        change();v.render(&second);return v.InteractionCounts().rasterizations==before+1;};
    if(!Explain("camera, roles, images and analysis invalidate software layer",invalidates([&]{v.SetVisibleWidthMm(31);})
        &&invalidates([&]{v.SetFaceRoleColors({{id,0,QColor(Qt::red)}});})
        &&invalidates([&]{v.SetImageViews({V2ImageView{}});})
        &&invalidates([&]{v.SetAnalysisViews({});})))return false;
    V2ImageView image;image.image={1,1,{0xff00ff00u},1};
    modeling::ImageTriangle triangle;triangle.mesh.points={geometry::Vector3{-8,-8,1},{8,-8,1},{0,8,1}};
    triangle.mesh.normal={0,0,1};image.triangles={triangle};
    v.SetImageViews({image});v.render(&first);image.image.pixels[0]=0xffff0000u;
    v.SetImageViews({image});v.render(&second);
    if(!Explain("replacement pasted image updates rendered pixels",first!=second))return false;
    auto display=v.DisplaySettingsNow();display.shapesVisible=false;
    if(!Explain("shape visibility invalidates raster",invalidates([&]{v.SetDisplaySettings(display);})))return false;
    display.shapesVisible=true;v.SetDisplaySettings(display);v.SetImageViews({});v.SetShapeViews({});
    return true;
}
bool OwnerRead(V2MainWindow& w) {
    CacheScope scope;const auto path=qEnvironmentVariable("KACHACAD_OWNER_OPEN_FILE");
    const auto original=io::ReadWholeFile(path.toStdString());if(!original.HasValue())return false;
    QElapsedTimer time;time.start();if(!w.OpenDocumentFile(path))return false;const auto cold=time.elapsed();
    const auto snapshot=Snapshot(w);const auto count=w.KernelShapeCount();
    time.restart();if(!w.OpenDocumentFile(path))return false;const auto warm=time.elapsed();
    std::cout<<"owner KCD read cold="<<cold<<" ms, reused="<<warm<<" ms, shapes="<<count<<std::endl;
    if(qEnvironmentVariableIsSet("KACHACAD_INTERACTION_TIMING")) {
        InteractionTiming(w,"owner model");InteractionTiming(w,"owner front",ViewDirection::Front);
        InteractionTiming(w,"owner isometric",ViewDirection::Isometric);
    }
    return Explain("owner read cache keeps native geometry, history and source bytes",Snapshot(w)==snapshot&&w.KernelShapeCount()==count
        &&w.RebuildProblems().isEmpty()&&io::ReadWholeFile(path.toStdString()).Value()==original.Value());
}
}
std::vector<SelfTestCase> DocumentOpenCases(){std::vector<SelfTestCase> cases{
    {"HP-OPEN-01 Escape cancels saved vessel history with an armed editor and preserves Undo",Cancel},
    {"HP-OPEN-02 crashed rebuild preserves document path and permits retry",Crash},
    {"HP-OPEN-03 invalid rebuilt geometry preserves original document",Failure},
    {"HP-OPEN-04 prepared open preserves metadata and unknown archive entries",Metadata},
    {"HP-OPEN-05 unsupported warped history preserves the old document",Unsupported},
    {"HP-OPEN-06 saved native cache, invalidation and corruption recovery",Cache},
    {"HP-OPEN-07 hidden native shapes mesh on first visibility",HiddenMesh},
    {"HP-OPEN-08 parallel independent surfaces match serial cold rebuild",ParallelRead},
    {"HP-PERF-01 dense face hover and image compositing",Interaction}};
    if(qEnvironmentVariableIsSet("KACHACAD_OWNER_OPEN_FILE"))cases.push_back({"HP-OPEN-OWNER actual KCD load timing",OwnerRead});return cases;}
}
