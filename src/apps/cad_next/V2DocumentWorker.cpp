#include "V2DocumentOpen.h"
#include "V2DocumentCache.h"
#include "V2DocumentTiming.h"
#include "V2PreparedMeshes.h"
#include "V2EvaluationCache.h"
#include "V2MainWindow.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctShapeTransfer.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QString>
#include <array>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

using namespace kachakacha::v2;

namespace {

using NativeShapes = std::map<std::string, modeling::KernelShapeHandle>;
using NativeBytes = std::map<std::string, std::string>;
using ShapeCapture = base::Result<std::string>;
using CaptureResult = base::Result<bool>;

CaptureResult TransferShape(const document::Document& document, const NativeShapes::value_type& entry,
    const ShapeCapture& shape, NativeBytes& target)
{
    if (shape.HasValue()) {
        target.emplace(entry.first, shape.Value());
        return CaptureResult::Success(true);
    }
    auto diagnostics = shape.Diagnostics();
    const auto id = base::EntityId::Parse(entry.first);
    const auto* entity = id ? document.FindEntity(*id) : nullptr;
    for (auto& diagnostic : diagnostics) {
        diagnostic.summaryJa = (entity ? entity->displayName : entry.first) + ": " + diagnostic.summaryJa;
        diagnostic.detailsJa += " (" + entry.first + ")";
    }
    return CaptureResult::Failure(std::move(diagnostics));
}

CaptureResult CaptureNativeShapes(const document::Document& document, const NativeShapes& source,
    NativeBytes& target, std::size_t lanes)
{
    if (lanes == 1 || source.size() < 64) {
        for (const auto& entry : source) {
            const auto written = TransferShape(document, entry, kernel::CaptureTransferShape(entry.second), target);
            if (!written.HasValue()) return written;
        }
        return CaptureResult::Success(true);
    }

    std::vector<const NativeShapes::value_type*> entries;
    entries.reserve(source.size());
    for (const auto& entry : source) entries.push_back(&entry);
    std::vector<std::optional<ShapeCapture>> captures(entries.size());
    const auto capture = [&](std::size_t lane) {
        for (auto i = lane; i < entries.size(); i += lanes)
            captures[i].emplace(kernel::CaptureTransferShape(entries[i]->second));
    };
    // The rebuild has finished. Each lane only reads native shapes and writes its own result slots.
    std::array<std::future<void>, 2> workers;
    for (std::size_t lane = 1; lane < lanes; ++lane) {
        try { workers[lane - 1] = std::async(std::launch::async, capture, lane); }
        catch (const std::system_error&) { capture(lane); }
    }
    capture(0);
    for (auto& worker : workers) if (worker.valid()) worker.get();
    // Commit in the original order so a failed shape reports the same entity as serial capture.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto written = TransferShape(document, *entries[i], *captures[i], target);
        if (!written.HasValue()) return written;
        captures[i].reset();
    }
    return CaptureResult::Success(true);
}

} // namespace

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
    const int processors = QThread::idealThreadCount();
    std::size_t lanes = 1;
    if (IsWorker() && processors >= 4
        && !qEnvironmentVariableIsSet("KACHACAD_OPEN_SERIAL")
        && !qEnvironmentVariableIsSet("KACHACAD_NATIVE_CAPTURE_SERIAL")) {
        lanes = processors >= 6 ? 3 : 2;
    }
    const auto& document = w.Session().GetDocument();
    const auto parts = CaptureNativeShapes(document, w.partShapes_, data.parts, lanes);
    const auto guides = CaptureNativeShapes(document, w.guideShapes_, data.guides, lanes);
    timing.Mark("serialize-native");
    if(!parts.HasValue())return Captured::Failure(parts.Diagnostics());
    if(!guides.HasValue())return Captured::Failure(guides.Diagnostics());
    data.partEdges=w.partEdges_;data.flatBoundaries=w.partFlatBoundary_;data.guideEdges=w.guideEdges_;
    data.samples=w.guideSamples_;data.fabrication=w.fabricationModels_;data.guideTable=w.guideTable_;
    std::vector<V2PreparedMeshRequest> requests;
    for(const auto* shapes:{&w.partShapes_,&w.guideShapes_})for(const auto& [id,handle]:*shapes) {
        const auto cached=w.shapeMeshes_.find(handle.value);
        if(cached!=w.shapeMeshes_.end())data.meshes[id]=cached->second;
        else {
            const auto entityId=base::EntityId::Parse(id);
            const auto* entity=entityId?w.session_->GetDocument().FindEntity(*entityId):nullptr;
            // Native geometry and history are transferred for every entity. Display
            // meshes for hidden entities are built on demand when they become visible.
            if(entity&&!app::EntityEffectivelyVisible(w.session_->GetDocument().Snapshot(),*entity))continue;
            requests.push_back({id,handle,shapes==&w.guideShapes_});
        }
    }
    const auto meshes=V2BuildPreparedMeshes(requests,data.meshes,IsWorker()&&QThread::idealThreadCount()>=4
        &&!qEnvironmentVariableIsSet("KACHACAD_OPEN_SERIAL"));
    timing.Mark("prepare-visible-meshes");
    if(!meshes.HasValue())return Captured::Failure(meshes.Diagnostics());
    return Captured::Success(std::move(result));
}
void V2DocumentOpen::CacheSaved(V2MainWindow& w,const std::string& archive) {
    if(IsWorker()||!w.rebuildProblems_.isEmpty())return;
    if(V2DocumentCache::HasMatchingPayload(archive))return;
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
        const auto cacheMode = request.value("cacheMode").toString();
        const bool validateOnOpen = cacheMode == "validateOnOpen";
        const auto validation = validateOnOpen ? V2DocumentCache::TransferValidation::InReader
                                               : V2DocumentCache::TransferValidation::InWorker;
        if(cacheMode != "rebuild" && V2DocumentCache::Transfer(bytes.Value(),dir.filePath("prepared.bin"),token,validation)) {
            timing.Mark("read-cache-hit");
            Progress(1,1,"内容が一致する計算済み形状を復元しています");
            timing.Mark("write-transfer-cache");
            response["ok"]=true;
            if (validateOnOpen) response["validateCacheInReader"] = true;
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
        std::future<base::Result<std::string>> archiveJob;
        if (window.KernelShapeCount() >= 64 && QThread::idealThreadCount() >= 4
            && !qEnvironmentVariableIsSet("KACHACAD_OPEN_SERIAL")
            && !qEnvironmentVariableIsSet("KACHACAD_OPEN_ARCHIVE_SERIAL")) {
            try {
                // The detached snapshot contains no viewport or kernel shapes.
                archiveJob = std::async(std::launch::async, [&sourceFile] { return io::SaveDocument(sourceFile); });
                timing.Mark("schedule-archive-encode");
            } catch (const std::system_error&) {
                // Continue on this worker when the extra thread cannot start.
            }
        }
        std::string archiveBytes;
        if (!archiveJob.valid()) {
            const auto archive = io::SaveDocument(sourceFile);
            if (!archive.HasValue()) throw archive.FirstDiagnostic();
            archiveBytes = archive.Value();
            timing.Mark("encode-archive");
        }
        auto indexJob = V2EvaluationCache::PrepareIndexKeys(sourceFile.snapshot, window.KernelShapeCount());
        if (indexJob.valid()) timing.Mark("schedule-feature-index");
        auto captured = Capture(window, {});
        timing.Mark("capture-native-and-meshes");
        if (archiveJob.valid()) {
            const auto archive = archiveJob.get();
            if (!archive.HasValue()) throw archive.FirstDiagnostic();
            archiveBytes = archive.Value();
            timing.Mark("await-archive-encode");
        }
        if (!captured.HasValue()) throw captured.FirstDiagnostic();
        auto& data=*captured.Value();
        data.archive = std::move(archiveBytes);
        timing.Mark("ready-to-write-transfer");
        if(!WritePreparedDocument(dir.filePath("prepared.bin"),token,data,V2PreparedFile::WorkerReply))
            throw std::runtime_error("Cannot write prepared document");
        timing.Mark("write-transfer");
        if(V2DocumentCache::StoreTransfer(bytes.Value(),dir.filePath("prepared.bin"),token))
            V2EvaluationCache::IndexPrepared(window,bytes.Value(),data,indexJob);
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
