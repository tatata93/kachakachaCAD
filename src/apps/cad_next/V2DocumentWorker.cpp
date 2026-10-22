#include "V2DocumentOpen.h"
#include "V2DocumentCache.h"
#include "V2DocumentTiming.h"
#include "V2MainWindow.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace kachakacha::v2;
bool V2DocumentOpen::IsWorker(){return QCoreApplication::instance()&&QCoreApplication::instance()->property("documentRebuildWorker").toBool();}
void V2DocumentOpen::Progress(int completed,int total,const std::string& name) {
    if(!IsWorker())return;
    auto text=QStringLiteral("再構築 %1/%2: %3").arg(completed).arg(total).arg(QString::fromStdString(name));
    text.replace('\n',' ');text.replace('\r',' ');std::cout<<"KACHACAD_PHASE:"<<text.toUtf8().constData()<<std::endl;
}
base::Result<std::unique_ptr<V2PreparedDocument>> V2DocumentOpen::Capture(V2MainWindow& w,std::string archive) {
    V2DocumentTiming timing("capture");
    using Captured=base::Result<std::unique_ptr<V2PreparedDocument>>;
    auto result=std::make_unique<V2PreparedDocument>();auto& data=*result;data.archive=std::move(archive);
    const auto capture=[](const auto& source,auto& target)->base::Result<bool>{
        for(const auto& entry:source){const auto shape=kernel::CaptureOutputShape(entry.second);
            if(!shape.HasValue())return base::Result<bool>::Failure(shape.Diagnostics());target[entry.first]=shape.Value();}
        return base::Result<bool>::Success(true);
    };
    const auto parts=capture(w.partShapes_,data.parts),guides=capture(w.guideShapes_,data.guides);
    timing.Mark("serialize-native");
    if(!parts.HasValue())return Captured::Failure(parts.Diagnostics());
    if(!guides.HasValue())return Captured::Failure(guides.Diagnostics());
    data.partEdges=w.partEdges_;data.flatBoundaries=w.partFlatBoundary_;data.guideEdges=w.guideEdges_;
    data.samples=w.guideSamples_;data.fabrication=w.fabricationModels_;data.guideTable=w.guideTable_;
    for(const auto* shapes:{&w.partShapes_,&w.guideShapes_})for(const auto& [id,handle]:*shapes) {
        const auto cached=w.shapeMeshes_.find(handle.value);
        if(cached!=w.shapeMeshes_.end())data.meshes[id]=cached->second;
        else {
            const auto entityId=base::EntityId::Parse(id);
            const auto* entity=entityId?w.session_->GetDocument().FindEntity(*entityId):nullptr;
            // Native geometry and history are transferred for every entity. Display
            // meshes for hidden entities are built on demand when they become visible.
            if(entity&&!app::EntityEffectivelyVisible(w.session_->GetDocument().Snapshot(),*entity))continue;
            const auto mesh=kernel::BuildShapeMesh(handle,0.0,shapes==&w.guideShapes_?8:0);
            if(!mesh.HasValue())return Captured::Failure(mesh.Diagnostics());
            data.meshes[id]=mesh.Value();
            timing.Mark(("mesh-"+id).c_str());
        }
    }
    return Captured::Success(std::move(result));
}
void V2DocumentOpen::CacheSaved(V2MainWindow& w,const std::string& archive) {
    if(IsWorker()||!w.rebuildProblems_.isEmpty())return;
    if(V2DocumentCache::Read(archive))return;
    for(const auto& e:w.session_->GetDocument().Snapshot().entities) {
        const auto key=e.id.ToString();
        if(e.kind==domain::EntityKind::Part&&!w.partShapes_.contains(key))return;
        if(e.kind==domain::EntityKind::GuideSurface&&!w.guideShapes_.contains(key))return;
    }
    auto result=Capture(w,archive);
    if(result.HasValue())(void)V2DocumentCache::Write(archive,*result.Value());
}
int V2DocumentOpen::Worker(const QString& directory) {
    QCoreApplication::instance()->setProperty("documentRebuildWorker",true);
    const QDir dir(directory);QFile requestFile(dir.filePath("request.json"));
    if(!requestFile.open(QIODevice::ReadOnly)||requestFile.size()>1024*1024)return 2;
    const auto request=QJsonDocument::fromJson(requestFile.readAll()).object();
    const auto token=request.value("token").toString();
    if(request.value("version").toInt()!=1||token.isEmpty()||request.value("source").toString().isEmpty())return 2;
    QJsonObject response{{"version",1},{"token",token},{"ok",false}};
    try {
        V2DocumentTiming timing("worker");
        Progress(0,0,"保存文書を読み込んでいます");
        const auto bytes=io::ReadWholeFile(request.value("source").toString().toStdString());
        if(!bytes.HasValue())throw bytes.FirstDiagnostic();
        timing.Mark("read-archive");
        if(V2DocumentCache::Transfer(bytes.Value(),dir.filePath("prepared.bin"),token)) {
            timing.Mark("read-cache-hit");
            Progress(1,1,"内容が一致する計算済み形状を復元しています");
            timing.Mark("write-transfer-cache");
            response["ok"]=true;
        } else {
        timing.Mark("read-cache-miss");
        auto loaded=io::LoadDocument(bytes.Value());if(!loaded.HasValue())throw loaded.FirstDiagnostic();
        timing.Mark("parse-archive");
        V2MainWindow window;
        timing.Mark("create-worker-window");
        if(!Adopt(window,loaded.Value().snapshot)||!window.RebuildProblems().isEmpty())
            throw std::runtime_error(window.RebuildProblems().isEmpty()?window.StatusText().toStdString():window.RebuildProblems().toStdString());
        timing.Mark("rebuild-and-adopt");
        auto sourceFile=loaded.Value();sourceFile.snapshot=window.session_->GetDocument().Snapshot();
        Progress(1,1,"計算済み形状と製作データを転送する準備をしています");
        const auto archive=io::SaveDocument(sourceFile);if(!archive.HasValue())throw archive.FirstDiagnostic();
        timing.Mark("encode-archive");
        auto captured=Capture(window,archive.Value());if(!captured.HasValue())throw captured.FirstDiagnostic();
        auto& data=*captured.Value();
        timing.Mark("capture-native-and-meshes");
        if(!WritePreparedDocument(dir.filePath("prepared.bin"),token,data))throw std::runtime_error("Cannot write prepared document");
        timing.Mark("write-transfer");
        (void)V2DocumentCache::StoreTransfer(bytes.Value(),dir.filePath("prepared.bin"),token);
        timing.Mark("write-cache");
        response["ok"]=true;
        }
    }catch(const base::Diagnostic& diagnostic) {
        response["code"]=QString::fromStdString(diagnostic.code);
        response["summary"]=QString::fromStdString(diagnostic.summaryJa);
        response["details"]=QString::fromStdString(diagnostic.detailsJa);
    }catch(const std::exception& e) {
        response["summary"]=QStringLiteral("文書を開けませんでした。元の文書は保持しています。");response["details"]=QString::fromUtf8(e.what());
    }catch(...) {
        response["summary"]=QStringLiteral("文書の再構築中に問題が発生しました。元の文書は保持しています。");
    }
    QSaveFile file(dir.filePath("response.json"));const auto bytes=QJsonDocument(response).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit()?0:3;
}
