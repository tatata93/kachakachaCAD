#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2DocumentOpen.h"
#include "V2GeometryJob.h"
#include "V2ThickenDock.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QProcess>
#include <QPushButton>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

namespace kachakacha::v2::selftest {
namespace {
std::string Snapshot(V2MainWindow& w) {
    io::DocumentFile file;file.snapshot=w.Session().GetDocument().Snapshot();return io::WriteDocumentJson(file);
}
bool Write(const QString& path,const io::DocumentFile& file) {
    const auto bytes=io::SaveDocument(file);
    return bytes.HasValue()&&io::WriteFileAtomically(path.toStdString(),bytes.Value()).HasValue();
}
io::DocumentFile HeavyFile(V2MainWindow& w,bool invalid) {
    static const char* brep=
#include "../../../tests_v2/fixtures/warped-panel-source.inc"
    ;
    auto file=V2DocumentOpen::FileForSave(w);base::DeterministicIdGenerator ids(900000);
    domain::Feature frozen;frozen.id=ids.NextTyped<base::IdKind::Feature>();
    frozen.type=domain::FeatureType::FreezeDerived;frozen.displayName="warped source";
    domain::FreezeDerivedDefinition definition;definition.frozenBrep=invalid?"invalid BRep":brep;frozen.definition=definition;
    domain::Entity surface;surface.id=ids.NextTyped<base::IdKind::Entity>();surface.kind=domain::EntityKind::GuideSurface;
    surface.createdBy=frozen.id;surface.displayName="warped source";surface.editPolicy=domain::EditPolicy::Frozen;
    frozen.outputs.push_back({"surface",surface.id,surface.kind});
    file.snapshot.features.push_back(frozen);file.snapshot.entities.push_back(surface);
    domain::Feature thick;thick.id=ids.NextTyped<base::IdKind::Feature>();thick.type=domain::FeatureType::ThickenSurface;
    thick.displayName="slow warped thickness";thick.inputEntityIds={surface.id};
    domain::ThickenSurfaceDefinition td;td.surface=surface.id;td.thickness.value=.8;td.placement=2;thick.definition=td;
    domain::Entity part;part.id=ids.NextTyped<base::IdKind::Entity>();part.kind=domain::EntityKind::Part;
    part.createdBy=thick.id;part.displayName="slow warped thickness";part.editPolicy=domain::EditPolicy::Derived;
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
    if(!Write(input,HeavyFile(w,mode==2)))return false;
    const auto inputBytes=io::ReadWholeFile(input.toStdString());
    QTimer timer;timer.setInterval(5);QElapsedTimer running,total;total.start();bool stopped=false;int ticks=0;
    QObject::connect(&timer,&QTimer::timeout,&w,[&]{
        auto* dialog=QApplication::activeModalWidget();if(!dialog||dialog->objectName()!="geometryJobDialog")return;++ticks;
        if(dialog->property("calculationPhase").toString().contains("slow warped thickness")&&!running.isValid())running.start();
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
    if(!Explain("file open failure/cancel preserves the old document and cache",(mode==2||stopped)&&ticks>0&&total.elapsed()<10000
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
}
std::vector<SelfTestCase> DocumentOpenCases(){return {
    {"HP-OPEN-01 Escape cancels actual saved warped history and preserves Undo",Cancel},
    {"HP-OPEN-02 crashed rebuild preserves document path and permits retry",Crash},
    {"HP-OPEN-03 invalid rebuilt geometry preserves original document",Failure},
    {"HP-OPEN-04 prepared open preserves metadata and unknown archive entries",Metadata}};}
}
