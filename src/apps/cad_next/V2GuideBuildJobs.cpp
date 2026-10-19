#include <QObject>
#include "V2GuideBuildJobs.h"
#include "V2DocumentOpen.h"
#include "V2MainWindow.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QThread>
#include <QString>
#include <algorithm>
#include <future>
#include <utility>

using namespace kachakacha::v2;
struct V2GuideBuildJobs::Impl {
    using Result=base::Result<modeling::GuideSurfaceResult>;
    struct Job {
        base::EntityId id;
        modeling::GuideTable table;
        modeling::GuideSurfaceRequest request;
        modeling::GuideSurfaceAnalysis analysis;
        geometry::GeometryTolerance tolerance;
        std::future<Result> future;
        bool consumed=false;
    };
    std::vector<Job> jobs;
    std::size_t next=0;
    void StartNext() noexcept {
        if(next>=jobs.size())return;
        auto& job=jobs[next++];
        try {
            job.future=std::async(std::launch::async,[request=job.request,analysis=job.analysis,tolerance=job.tolerance]{
                return kernel::BuildGuideSurface(request,analysis,tolerance);
            });
        }catch(...){/* A resource-limited launch leaves this job to the serial builder. */}
    }
    ~Impl() {
        // Unadopted results own native handles even if an earlier feature failed.
        for(auto& job:jobs)if(job.future.valid())try {
            auto result=job.future.get();if(result.HasValue())kernel::ReleaseShape(result.Value().handle);
        }catch(...){}
    }
};
V2GuideBuildJobs::V2GuideBuildJobs(V2MainWindow& w,const std::vector<app::ShapeRebuildStep>& steps):QObject(&w),impl_(std::make_unique<Impl>()) {
    if(!V2DocumentOpen::IsWorker()||qEnvironmentVariableIsSet("KACHACAD_OPEN_SERIAL")||QThread::idealThreadCount()<4)return;
    const auto& document=w.Session().GetDocument();const auto& tolerance=document.Snapshot().settings.tolerance;
    for(const auto& step:steps) {
        if(step.kind!=app::ShapeRebuildKind::GuideSurface)continue;
        const auto* feature=document.FindFeature(step.featureId);
        const auto* definition=feature?std::get_if<domain::CreateGuideSurfaceDefinition>(&feature->definition):nullptr;
        if(!definition||definition->gptBuilder||feature->inputEntityIds.empty())continue;
        // A job may only depend on immutable source wires. Derived wires, surfaces,
        // native face references and parts retain the ordinary dependency order.
        const auto notSourceWire=[&](auto id){
            const auto* entity=document.FindEntity(id);
            return !entity||entity->kind!=domain::EntityKind::Wire||entity->editPolicy!=domain::EditPolicy::Source;
        };
        if(std::any_of(feature->inputEntityIds.begin(),feature->inputEntityIds.end(),notSourceWire))continue;
        const auto table=app::GuideTableFromDefinition(document,w.Session().Scene(),*definition);if(!table.HasValue())continue;
        if(std::any_of(table.Value().rows.begin(),table.Value().rows.end(),[&](const auto& row){
            return std::any_of(row.sourceWireIds.begin(),row.sourceWireIds.end(),notSourceWire);
        }))continue;
        const auto request=modeling::ToGuideSurfaceRequest(table.Value(),tolerance);if(!request.HasValue())continue;
        if(std::any_of(request.Value().chains.begin(),request.Value().chains.end(),[](const auto& chain){
            return !chain.supportSurfaceId.IsNil()||chain.role==modeling::ChainRole::SourceSurface;
        }))continue;
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance);if(!analysis.HasValue())continue;
        impl_->jobs.push_back({step.outputEntityId,table.Value(),request.Value(),analysis.Value(),tolerance,{},false});
    }
    // At most two independent calculations, even for documents with many faces.
    impl_->StartNext();impl_->StartNext();
}
V2GuideBuildJobs::~V2GuideBuildJobs()=default;
std::optional<V2PreparedGuideBuild> V2GuideBuildJobs::Take(V2MainWindow& w,base::EntityId id) {
    for(auto* child:w.children())if(auto* state=dynamic_cast<V2GuideBuildJobs*>(child)) {
        const auto found=std::find_if(state->impl_->jobs.begin(),state->impl_->jobs.end(),[id](const auto& job){return job.id==id;});
        if(found==state->impl_->jobs.end()||found->consumed||!found->future.valid())return std::nullopt;
        found->consumed=true;
        try {
            auto result=found->future.get();state->impl_->StartNext();
            if(result.HasValue())return V2PreparedGuideBuild{std::move(found->table),std::move(result.Value())};
        }catch(...){state->impl_->StartNext();}
        // The ordinary builder supplies its full diagnostic on failure.
        return std::nullopt;
    }
    return std::nullopt;
}
