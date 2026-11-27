#include "V2DocumentOpen.h"
#include "V2ToolSelection.h"
#include "V2DocumentTiming.h"
#include "V2GeometryJob.h"
#include "V2MainWindow.h"
#include "V2PatternDock.h"
#include "kachakacha/app/GeometryJobDiagnostic.h"
#include "kachakacha/app/DocumentIds.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctShapeTransfer.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QObject>
#include <QFileInfo>
#include <QString>
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using namespace kachakacha::v2;
using Shapes=std::map<std::string,modeling::KernelShapeHandle>;
using NativeTransfers = std::map<std::string, std::string>;
using ShapeResult = base::Result<modeling::KernelShapeHandle>;
using RestoreResult = base::Result<bool>;

RestoreResult CancelledRestore()
{
    return RestoreResult::Failure(base::MakeError(app::kGeometryJobCancelled, "文書を開くのを取り消しました。"));
}

struct NativeRestoreResults {
    std::vector<std::optional<ShapeResult>> shapes;
    ~NativeRestoreResults()
    {
        for (const auto& shape : shapes)
            if (shape && shape->HasValue()) kernel::ReleaseShape(shape->Value());
    }
};

std::optional<RestoreResult> RestoreParallel(const std::vector<const NativeTransfers::value_type*>& entries,
    Shapes& shapes, const std::function<bool()>& pulse)
{
    constexpr std::size_t lanes = 3;
    NativeRestoreResults restored;
    restored.shapes.resize(entries.size());
    std::atomic_bool stop{false};
    std::array<std::future<void>, lanes> workers;
    const auto restoreLane = [&](std::size_t lane) {
        for (auto i = lane; i < entries.size() && !stop.load(std::memory_order_relaxed); i += lanes) {
            auto& result = restored.shapes[i];
            result.emplace(kernel::RestoreTransferShape(entries[i]->second));
            if (!result->HasValue()) stop.store(true, std::memory_order_relaxed);
        }
    };
    try {
        for (std::size_t lane = 0; lane < lanes; ++lane)
            workers[lane] = std::async(std::launch::async, restoreLane, lane);
    } catch (const std::system_error&) {
        // Existing futures join before their results and inputs go out of scope.
        stop.store(true, std::memory_order_relaxed);
        return std::nullopt;
    }

    V2DocumentTiming timing("foreground-native-restore");
    timing.Mark("workers-started");
    // The callback pumps Qt events. Only the UI thread may call it.
    bool cancelled = false;
    for (auto& worker : workers) {
        while (worker.wait_for(std::chrono::milliseconds(4)) != std::future_status::ready) {
            if (!cancelled && !pulse()) {
                cancelled = true;
                stop.store(true, std::memory_order_relaxed);
            }
        }
        worker.get();
    }
    if (cancelled || !pulse()) return CancelledRestore();
    for (const auto& shape : restored.shapes)
        if (shape && !shape->HasValue()) return RestoreResult::Failure(shape->Diagnostics());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto& shape = restored.shapes[i];
        shapes.emplace(entries[i]->first, shape->Value());
        shape.reset();
    }
    return RestoreResult::Success(true);
}

void Release(const Shapes& parts,const Shapes& guides) {
    std::set<std::uint64_t> released;
    for(const auto* map:{&parts,&guides})for(const auto& entry:*map)
        if(released.insert(entry.second.value).second)kernel::ReleaseShape(entry.second);
}
struct Prepared {
    V2PreparedDocument data;io::DocumentFile file;Shapes parts,guides;bool transferred=false;
    ~Prepared(){if(!transferred)Release(parts,guides);}
};
RestoreResult Restore(const NativeTransfers& source,Shapes& shapes,
    const io::DocumentFile& file,domain::EntityKind kind,const std::function<bool()>& pulse) {
    const bool parallel = source.size() >= 64 && QThread::idealThreadCount() >= 4
        && !qEnvironmentVariableIsSet("KACHACAD_OPEN_SERIAL")
        && !qEnvironmentVariableIsSet("KACHACAD_OPEN_FOREGROUND_SERIAL");
    std::vector<const NativeTransfers::value_type*> entries;
    if (parallel) entries.reserve(source.size());
    std::map<base::EntityId,domain::EntityKind> kinds;
    for(const auto& entity:file.snapshot.entities)kinds.emplace(entity.id,entity.kind);
    const auto restoreOne = [&](const NativeTransfers::value_type& entry) {
        const auto shape = kernel::RestoreTransferShape(entry.second);
        if (!shape.HasValue()) return RestoreResult::Failure(shape.Diagnostics());
        shapes.emplace(entry.first, shape.Value());
        return RestoreResult::Success(true);
    };
    for(const auto& entry:source) {
        if(!pulse())return CancelledRestore();
        const auto id=base::EntityId::Parse(entry.first);
        const auto found=id?kinds.find(*id):kinds.end();
        if(found==kinds.end()||found->second!=kind)return base::Result<bool>::Failure(base::MakeError(app::kGeometryJobFailed,"再構築結果の参照が文書と一致しません。"));
        if (parallel) entries.push_back(&entry);
        else {
            const auto result = restoreOne(entry);
            if (!result.HasValue()) return result;
        }
    }
    if (entries.empty()) return RestoreResult::Success(true);
    if (const auto result = RestoreParallel(entries, shapes, pulse)) return *result;
    // A thread launch can fail under resource pressure. Retry on the UI thread.
    for (const auto* entry : entries) {
        if (!pulse()) return CancelledRestore();
        const auto result = restoreOne(*entry);
        if (!result.HasValue()) return result;
    }
    return RestoreResult::Success(true);
}
}
V2DocumentOpen::V2DocumentOpen(V2MainWindow& window):QObject(&window){}
V2DocumentOpen* V2DocumentOpen::State(V2MainWindow& window,bool create) {
    for(auto* child:window.children())if(auto* state=dynamic_cast<V2DocumentOpen*>(child))return state;
    return create?new V2DocumentOpen(window):nullptr;
}
kachakacha::v2::io::DocumentFile V2DocumentOpen::FileForSave(V2MainWindow& window) {
    const auto* state=State(window,false);auto file=state?state->extras_:kachakacha::v2::io::DocumentFile{};
    file.snapshot=window.session_->GetDocument().Snapshot();return file;
}
bool V2DocumentOpen::Adopt(V2MainWindow& w,kachakacha::v2::document::DocumentSnapshot snapshot,const std::function<void()>& install) {
    using namespace kachakacha::v2;
    V2DocumentTiming timing("adopt");
    const auto problems=w.session_->GetDocument().ResetTo(std::move(snapshot));
    if(std::any_of(problems.begin(),problems.end(),[](const auto& d){return d.IsError();})) {
        w.ReportDiagnostics(problems);w.SetStatus(QStringLiteral("開けませんでした。元の文書は保持しています。"));return false;
    }
    w.patternPages_.clear();
    if (w.patternDock_) w.patternDock_->SetPages({});
    w.processContext_.patternBuilt = false;
    app::AdvanceIdsPastDocument(*w.ids_,w.session_->GetDocument().Snapshot());
    timing.Mark("reset-and-advance-ids");
    app::EnsureOriginPlanes(w.session_->GetDocument(),*w.ids_);w.session_->GetDocument().MarkHistoryBoundary();
    w.EndArmedTools();if(auto* state=State(w,false))state->extras_=io::DocumentFile{};
    w.session_->SetScene(app::RebuildSceneKeepingView(w.session_->Scene(),w.session_->GetDocument().Snapshot(),*w.ids_));
    timing.Mark("rebuild-scene");
    w.viewport_->SetSelection(app::SelectionSet{});
    if(w.session_->GetDocument().FindEntity(w.activeWorkPlaneId_)==nullptr) {
        const auto top=app::OriginPlaneId(w.session_->GetDocument().Snapshot(),modeling::StandardPlaneKind::XY);
        if(top)w.ApplyWorkPlane(modeling::StandardPlane(modeling::StandardPlaneKind::XY),*top);
    }
    if(install)install();else w.RebuildKernelShapes();
    timing.Mark("install-or-rebuild-shapes");
    // A private rebuild worker has no visible explorer, ribbon or viewport.
    // Geometry, derived wires and source scene were installed above; defer only
    // their presentation to the visible process that adopts the transfer.
    if(!IsWorker())w.AdoptCurrentDocument(bool(install));timing.Mark("adopt-ui");
    if(!IsWorker()){w.RefreshWorkPlaneDock();w.viewport_->update();}
    timing.Mark("refresh-docks");return true;
}
bool V2DocumentOpen::Open(V2MainWindow& w,const QString& path) {
    using namespace kachakacha::v2;
    V2DocumentTiming timing("open");
    if(V2GeometryJob::Active())return false;
    const auto revision=w.session_->GetDocument().Revision();
    const auto documentId=w.session_->GetDocument().Snapshot().id;
    Prepared prepared;
    const auto ready=V2GeometryJob::PrepareDocument(&w,QFileInfo(path).absoluteFilePath(),[&](const QString& dataPath,const QString& token,const std::function<bool()>& pulse){
        timing.Mark("worker-finished");
        auto result=ReadPreparedDocument(dataPath,token,pulse);
        timing.Mark("read-transfer");
        if(!result.HasValue())return base::Result<bool>::Failure(result.Diagnostics());
        prepared.data=std::move(*result.Value());const auto loaded=io::LoadDocument(prepared.data.archive);
        if(!loaded.HasValue())return base::Result<bool>::Failure(loaded.Diagnostics());prepared.file=loaded.Value();
        timing.Mark("parse-transferred-archive");
        auto restored=Restore(prepared.data.parts,prepared.parts,prepared.file,domain::EntityKind::Part,pulse);
        if(!restored.HasValue())return restored;
        restored=Restore(prepared.data.guides,prepared.guides,prepared.file,domain::EntityKind::GuideSurface,pulse);
        timing.Mark("restore-native-shapes");return restored;
    });
    if(!ready.HasValue()){w.ReportDiagnostics(ready.Diagnostics());return false;}
    if(w.session_->GetDocument().Revision()!=revision||w.session_->GetDocument().Snapshot().id!=documentId) {
        w.SetStatus(QStringLiteral("計算中に文書が変わったため、読込結果を採用しませんでした。"));return false;
    }
    if(!Adopt(w,prepared.file.snapshot,[&]{
        auto oldParts=std::move(w.partShapes_),oldGuides=std::move(w.guideShapes_);
        w.partShapes_=std::move(prepared.parts);w.guideShapes_=std::move(prepared.guides);prepared.transferred=true;
        w.partEdges_=std::move(prepared.data.partEdges);w.partFlatBoundary_=std::move(prepared.data.flatBoundaries);
        w.guideEdges_=std::move(prepared.data.guideEdges);w.guideSamples_=std::move(prepared.data.samples);
        w.fabricationModels_=std::move(prepared.data.fabrication);w.guideTable_=std::move(prepared.data.guideTable);
        w.shapeMeshes_.clear();
        for(const auto* shapes:{&w.partShapes_,&w.guideShapes_})for(const auto& [id,handle]:*shapes) {
            const auto mesh=prepared.data.meshes.find(id);
            if(mesh!=prepared.data.meshes.end())w.shapeMeshes_.emplace(handle.value,std::move(mesh->second));
        }
        w.rebuildProblems_.clear();Release(oldParts,oldGuides);
        w.RefreshGuideTable();w.RefreshFabricationView();
    }))return false;
    prepared.file.snapshot={};State(w,true)->extras_=std::move(prepared.file);
    w.documentPath_=path;V2ToolSelection::Caption(w,path);w.SetStatus(QStringLiteral("%1 を開きました。").arg(path));timing.Mark("complete");return true;
}
