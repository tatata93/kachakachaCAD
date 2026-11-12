#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2LoopFacesTool.h"
#include "V2LoopFacesDock.h"
#include "V2Viewport.h"
#include "V2EntityTree.h"
#include "V2DocumentOpen.h"
#include "V2SelfTestPrimitiveUi.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Pnt.hxx>
#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QEvent>
#include <QIODevice>
#include <QPointF>
#include <QString>
#include <QThread>
#include <QMouseEvent>
#include <string>
#include <algorithm>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <cmath>

namespace kachakacha::v2::selftest {
namespace {
std::uint64_t Mark() {
    const auto h=kernel::StoreShape(BRepBuilderAPI_MakeVertex(gp_Pnt()).Shape());
    kernel::ReleaseShape(h);return h.value;
}
bool Ready(V2MainWindow& w) {
    QElapsedTimer clock;clock.start();
    while(clock.elapsed()<5000) {
        QApplication::processEvents();
        if(w.LoopFacesTool().Dock()->ConfirmEnabled())return true;
        QThread::msleep(1);
    }
    return Explain("visible face preview becomes confirmable",false);
}
bool Pick(V2MainWindow& w,base::EntityId id,Qt::KeyboardModifiers mods) {
    auto* tree=w.EntityTree();auto* item=w.ItemOfEntity(id);if(!tree||!item)return false;
    tree->scrollToItem(item);QApplication::processEvents();
    const auto point=QPointF(tree->visualItemRect(item).center());
    QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,mods);
    QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,mods);
    QApplication::sendEvent(tree->viewport(),&press);QApplication::sendEvent(tree->viewport(),&release);
    return std::find(w.Viewport().Selection().entityIds.begin(),w.Viewport().Selection().entityIds.end(),id)!=w.Viewport().Selection().entityIds.end();
}
bool Preview(V2MainWindow& w,bool hole) {
    w.RunCommand("file.new");w.resize(1600,1000);
    const auto outer=DrawRectangleAtByHand(w,.30,.30,.65,.65);
    base::EntityId inner;
    if(hole)inner=DrawRectangleAtByHand(w,.40,.40,.50,.50);
    if(outer.IsNil()||(hole&&inner.IsNil())||!Pick(w,outer,Qt::NoModifier))return false;
    if(hole&&!Pick(w,inner,Qt::ControlModifier))return false;
    w.RunCommand("surface.from_lines");return Ready(w);
}
bool ConfirmReusesPreview(V2MainWindow& w,bool hole) {
    if(!Preview(w,hole))return false;
    const auto before=Mark();QElapsedTimer timer;timer.start();
    if(!w.LoopFacesTool().Dock()->ClickConfirm())return false;
    const auto ms=timer.elapsed();const auto after=Mark();
    Note(("confirmed face ms="+std::to_string(ms)+", native allocations="+std::to_string(after-before-1)).c_str());
    return Explain("confirmation keeps one valid visible face",CountOfKind(w,domain::EntityKind::GuideSurface)==1
        &&w.Viewport().ShapeViewCount()==1&&w.RebuildProblems().isEmpty())
        &&Explain("confirmation adopts the already inspected face without generating it again",after==before+1);
}
bool Plane(V2MainWindow& w){return ConfirmReusesPreview(w,false);}
bool Hole(V2MainWindow& w){return ConfirmReusesPreview(w,true);}
bool Cancel(V2MainWindow& w) {
    w.RunCommand("file.new");const auto native=kernel::CachedShapeCount();
    if(!Preview(w,false)||!w.LoopFacesTool().Dock()->ClickCancel())return false;
    return Explain("cancelling the visible preview releases all temporary faces",kernel::CachedShapeCount()==native
        &&CountOfKind(w,domain::EntityKind::GuideSurface)==0);
}
struct CacheScope {
    QTemporaryDir folder;
    QByteArray previous=qgetenv("KACHACAD_DOCUMENT_CACHE_DIR"),trace=qgetenv("KACHACAD_OPEN_TRACE_DIR");
    CacheScope(){qputenv("KACHACAD_DOCUMENT_CACHE_DIR",folder.path().toUtf8());qputenv("KACHACAD_OPEN_TRACE_DIR",folder.filePath("trace").toUtf8());}
    ~CacheScope(){if(previous.isNull())qunsetenv("KACHACAD_DOCUMENT_CACHE_DIR");else qputenv("KACHACAD_DOCUMENT_CACHE_DIR",previous);
        if(trace.isNull())qunsetenv("KACHACAD_OPEN_TRACE_DIR");else qputenv("KACHACAD_OPEN_TRACE_DIR",trace);}
};
bool WriteFixture(const QString& path,const io::DocumentFile& file) {
    const auto bytes=io::SaveDocument(file);return bytes.HasValue()&&io::WriteFileAtomically(path.toStdString(),bytes.Value()).HasValue();
}
int Hits(const CacheScope& scope) {
    int count=0;
    for(const auto& path:QDir(scope.folder.filePath("trace")).entryInfoList({"*.jsonl"},QDir::Files)) {
        QFile file(path.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))continue;
        while(!file.atEnd()) {
            const auto event=QJsonDocument::fromJson(file.readLine()).object();
            if(event.value("scope").toString()=="rebuild"&&event.value("stage").toString().endsWith("-cache"))++count;
        }
    }
    return count;
}
double Height(V2MainWindow& w,base::EntityId id={}) {
    for(const auto& v:w.Viewport().ShapeViews())if(!v.surface&&(id.IsNil()||id==v.entityId))return v.mesh.maximum.z-v.mesh.minimum.z;
    return -1;
}
bool RevisionReuse(V2MainWindow& w,bool corrupt) {
    CacheScope scope;if(!scope.folder.isValid())return false;
    w.RunCommand("file.new");if(MakeBoxByHand(w,.3,.3,.65,.65).IsNil())return false;
    auto file=V2DocumentOpen::FileForSave(w);const auto first=scope.folder.filePath("first.kcd2");
    if(!WriteFixture(first,file)||!w.OpenDocumentFile(first))return false;
    const double original=Height(w);const int hits=Hits(scope);
    if(corrupt) {
        const QDir dir(scope.folder.path());const auto entries=dir.entryInfoList({"*.bin"},QDir::Files);
        if(!Explain("cold open indexes one existing prepared document",entries.size()==1
            &&dir.exists(entries.front().completeBaseName()+".features.json")))return false;
        const auto path=entries.front().absoluteFilePath();QFile payload(path);if(!payload.open(QIODevice::WriteOnly))return false;
        const QByteArray bad="invalid cached native result";payload.write(bad);payload.close();
        const auto key=entries.front().completeBaseName();QFile manifest(dir.filePath(key+".json"));
        if(!manifest.open(QIODevice::WriteOnly))return false;
        manifest.write(QJsonDocument(QJsonObject{{"key",key},{"size",bad.size()},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(bad,QCryptographicHash::Sha256).toHex())}}).toJson());manifest.close();
    }
    // A separately delivered revision has no whole-document cache. Only the
    // display label changes; all geometry and stable identities stay identical.
    for(auto& e:file.snapshot.entities)e.displayName+=" revision";
    const auto second=scope.folder.filePath("renamed.kcd2");
    if(!WriteFixture(second,file))return false;
    QElapsedTimer elapsed;elapsed.start();if(!w.OpenDocumentFile(second))return false;
    Note(("variant open ms="+std::to_string(elapsed.elapsed())+", feature cache hits="+std::to_string(Hits(scope)-hits)).c_str());
    if(!Explain("same geometry remains exact",Height(w)==original&&w.KernelShapeCount()==1))return false;
    if(!Explain("validated reuse or corrupt-result rebuild",Hits(scope)-hits==(corrupt?0:1)))return false;
    for(auto& f:file.snapshot.features)if(auto* e=std::get_if<domain::ExtrudeDefinition>(&f.definition)) {
        e->distance.value*=2;e->distance.expression.clear();
    }
    const auto changed=scope.folder.filePath("changed.kcd2");const int before=Hits(scope);
    if(!WriteFixture(changed,file)||!w.OpenDocumentFile(changed))return false;
    return Explain("changing generation parameters never reuses stale geometry",Hits(scope)==before
        &&std::abs(Height(w)-2*original)<1e-8&&w.KernelShapeCount()==1);
}
bool Reuse(V2MainWindow& w){return RevisionReuse(w,false);}
bool Corrupt(V2MainWindow& w){return RevisionReuse(w,true);}
bool Independent(V2MainWindow& w) {
    CacheScope scope;if(!scope.folder.isValid())return false;
    w.RunCommand("file.new");w.resize(1600,1000);
    const auto a=MakeBoxByHand(w,.25,.26,.38,.44);
    const auto b=MakeBoxByHand(w,.56,.55,.70,.69);
    if(a.IsNil()||b.IsNil()||a==b)return false;
    auto file=V2DocumentOpen::FileForSave(w);
    const auto first=scope.folder.filePath("two-parts.kcd2");
    if(!WriteFixture(first,file)||!w.OpenDocumentFile(first))return false;
    const auto oldA=Height(w,a),oldB=Height(w,b);
    bool edited=false;
    for(auto& f:file.snapshot.features)if(std::any_of(f.outputs.begin(),f.outputs.end(),[&](const auto& o){return o.entityId==a;})) {
        auto* extrude=std::get_if<domain::ExtrudeDefinition>(&f.definition);if(!extrude)return false;
        extrude->distance.value*=2;extrude->distance.expression.clear();edited=true;
    }
    const int priorHits=Hits(scope);const auto next=scope.folder.filePath("one-part-changed.kcd2");
    if(!edited||!WriteFixture(next,file)||!w.OpenDocumentFile(next))return false;
    return Explain("the edited part is rebuilt while the independent part is reused",Hits(scope)==priorHits+1
        &&std::abs(Height(w,a)-2*oldA)<1e-8&&Height(w,b)==oldB&&w.KernelShapeCount()==2);
}
}
std::vector<SelfTestCase> GenerationLatencyCases() {
    return {{"HP-GL-01 confirmation reuses inspected planar face",Plane},
        {"HP-GL-02 confirmation reuses inspected face and hole",Hole},
        {"HP-GL-03 cancel releases prepared face",Cancel},
        {"HP-GL-04 new archive reuses unchanged feature and invalidates changed distance",Reuse},
        {"HP-GL-05 corrupt result with matching digest is rebuilt",Corrupt},
        {"HP-GL-06 editing one part reuses the independent part",Independent}};
}
}
