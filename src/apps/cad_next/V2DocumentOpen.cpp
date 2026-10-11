#include "V2DocumentOpen.h"
#include "V2GeometryJob.h"
#include "V2MainWindow.h"
#include "kachakacha/app/GeometryJobDiagnostic.h"
#include "kachakacha/app/DocumentIds.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QObject>
#include <QFileInfo>
#include <QString>
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace {
using namespace kachakacha::v2;
using Shapes=std::map<std::string,modeling::KernelShapeHandle>;
void Release(const Shapes& parts,const Shapes& guides) {
    std::set<std::uint64_t> released;
    for(const auto* map:{&parts,&guides})for(const auto& entry:*map)
        if(released.insert(entry.second.value).second)kernel::ReleaseShape(entry.second);
}
struct Prepared {
    V2PreparedDocument data;io::DocumentFile file;Shapes parts,guides;bool transferred=false;
    ~Prepared(){if(!transferred)Release(parts,guides);}
};
base::Result<bool> Restore(const std::map<std::string,std::string>& source,Shapes& shapes,
    const io::DocumentFile& file,domain::EntityKind kind,const std::function<bool()>& pulse) {
    for(const auto& entry:source) {
        if(!pulse())return base::Result<bool>::Failure(base::MakeError(app::kGeometryJobCancelled,"文書を開くのを取り消しました。"));
        const auto id=base::EntityId::Parse(entry.first);
        const auto found=std::find_if(file.snapshot.entities.begin(),file.snapshot.entities.end(),[&](const auto& e){return id&&e.id==*id&&e.kind==kind;});
        if(found==file.snapshot.entities.end())return base::Result<bool>::Failure(base::MakeError(app::kGeometryJobFailed,"再構築結果の参照が文書と一致しません。"));
        const auto shape=kernel::RestoreOutputShape(entry.second);
        if(!shape.HasValue())return base::Result<bool>::Failure(shape.Diagnostics());
        shapes.emplace(entry.first,shape.Value());
    }
    return base::Result<bool>::Success(true);
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
    const auto problems=w.session_->GetDocument().ResetTo(std::move(snapshot));
    if(std::any_of(problems.begin(),problems.end(),[](const auto& d){return d.IsError();})) {
        w.ReportDiagnostics(problems);w.SetStatus(QStringLiteral("開けませんでした。元の文書は保持しています。"));return false;
    }
    app::AdvanceIdsPastDocument(*w.ids_,w.session_->GetDocument().Snapshot());
    app::EnsureOriginPlanes(w.session_->GetDocument(),*w.ids_);w.session_->GetDocument().MarkHistoryBoundary();
    w.EndArmedTools();if(auto* state=State(w,false))state->extras_=io::DocumentFile{};
    w.session_->SetScene(app::RebuildSceneKeepingView(w.session_->Scene(),w.session_->GetDocument().Snapshot(),*w.ids_));
    w.viewport_->SetSelection(app::SelectionSet{});
    if(w.session_->GetDocument().FindEntity(w.activeWorkPlaneId_)==nullptr) {
        const auto top=app::OriginPlaneId(w.session_->GetDocument().Snapshot(),modeling::StandardPlaneKind::XY);
        if(top)w.ApplyWorkPlane(modeling::StandardPlane(modeling::StandardPlaneKind::XY),*top);
    }
    if(install)install();else w.RebuildKernelShapes();
    w.AdoptCurrentDocument();w.RefreshPartEdges();w.RefreshWorkPlaneDock();w.viewport_->update();return true;
}
bool V2DocumentOpen::Open(V2MainWindow& w,const QString& path) {
    using namespace kachakacha::v2;
    if(V2GeometryJob::Active())return false;
    const auto revision=w.session_->GetDocument().Revision();
    const auto documentId=w.session_->GetDocument().Snapshot().id;
    Prepared prepared;
    const auto ready=V2GeometryJob::PrepareDocument(&w,QFileInfo(path).absoluteFilePath(),[&](const QString& dataPath,const QString& token,const std::function<bool()>& pulse){
        auto result=ReadPreparedDocument(dataPath,token,pulse);
        if(!result.HasValue())return base::Result<bool>::Failure(result.Diagnostics());
        prepared.data=result.Value();const auto loaded=io::LoadDocument(prepared.data.archive);
        if(!loaded.HasValue())return base::Result<bool>::Failure(loaded.Diagnostics());prepared.file=loaded.Value();
        auto restored=Restore(prepared.data.parts,prepared.parts,prepared.file,domain::EntityKind::Part,pulse);
        if(!restored.HasValue())return restored;
        return Restore(prepared.data.guides,prepared.guides,prepared.file,domain::EntityKind::GuideSurface,pulse);
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
        w.rebuildProblems_.clear();Release(oldParts,oldGuides);
        w.RefreshGuideTable();w.RefreshFabricationView();
    }))return false;
    prepared.file.snapshot={};State(w,true)->extras_=std::move(prepared.file);
    w.documentPath_=path;w.SetStatus(QStringLiteral("%1 を開きました。").arg(path));return true;
}
