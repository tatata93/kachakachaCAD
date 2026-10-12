#include "V2DocumentOpen.h"
#include "V2DocumentCache.h"
#include "V2MainWindow.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/kernel/OcctOutput.h"
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
base::Result<V2PreparedDocument> V2DocumentOpen::Capture(V2MainWindow& w,std::string archive) {
    V2PreparedDocument data;data.archive=std::move(archive);
    const auto capture=[](const auto& source,auto& target)->base::Result<bool>{
        for(const auto& entry:source){const auto shape=kernel::CaptureOutputShape(entry.second);
            if(!shape.HasValue())return base::Result<bool>::Failure(shape.Diagnostics());target[entry.first]=shape.Value();}
        return base::Result<bool>::Success(true);
    };
    const auto parts=capture(w.partShapes_,data.parts),guides=capture(w.guideShapes_,data.guides);
    if(!parts.HasValue())return base::Result<V2PreparedDocument>::Failure(parts.Diagnostics());
    if(!guides.HasValue())return base::Result<V2PreparedDocument>::Failure(guides.Diagnostics());
    data.partEdges=w.partEdges_;data.flatBoundaries=w.partFlatBoundary_;data.guideEdges=w.guideEdges_;
    data.samples=w.guideSamples_;data.fabrication=w.fabricationModels_;data.guideTable=w.guideTable_;
    return base::Result<V2PreparedDocument>::Success(std::move(data));
}
void V2DocumentOpen::CacheSaved(V2MainWindow& w,const std::string& archive) {
    if(IsWorker()||!w.rebuildProblems_.isEmpty())return;
    for(const auto& e:w.session_->GetDocument().Snapshot().entities) {
        const auto key=e.id.ToString();
        if(e.kind==domain::EntityKind::Part&&!w.partShapes_.contains(key))return;
        if(e.kind==domain::EntityKind::GuideSurface&&!w.guideShapes_.contains(key))return;
    }
    auto result=Capture(w,archive);
    if(result.HasValue()){auto data=result.Value();(void)V2DocumentCache::Write(archive,data);}
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
        Progress(0,0,"保存文書を読み込んでいます");
        const auto bytes=io::ReadWholeFile(request.value("source").toString().toStdString());
        if(!bytes.HasValue())throw bytes.FirstDiagnostic();
        auto loaded=io::LoadDocument(bytes.Value());if(!loaded.HasValue())throw loaded.FirstDiagnostic();
        if(auto cached=V2DocumentCache::Read(bytes.Value())) {
            Progress(1,1,"内容が一致する計算済み形状を復元しています");
            if(!WritePreparedDocument(dir.filePath("prepared.bin"),token,*cached))throw std::runtime_error("Cannot transfer cached document");
            response["ok"]=true;
        } else {
        V2MainWindow window;
        if(!Adopt(window,loaded.Value().snapshot)||!window.RebuildProblems().isEmpty())
            throw std::runtime_error(window.RebuildProblems().isEmpty()?window.StatusText().toStdString():window.RebuildProblems().toStdString());
        auto sourceFile=loaded.Value();sourceFile.snapshot=window.session_->GetDocument().Snapshot();
        Progress(1,1,"計算済み形状と製作データを転送する準備をしています");
        const auto archive=io::SaveDocument(sourceFile);if(!archive.HasValue())throw archive.FirstDiagnostic();
        auto captured=Capture(window,archive.Value());if(!captured.HasValue())throw captured.FirstDiagnostic();
        auto data=captured.Value();
        if(!WritePreparedDocument(dir.filePath("prepared.bin"),token,data))throw std::runtime_error("Cannot write prepared document");
        (void)V2DocumentCache::Write(bytes.Value(),data);
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
