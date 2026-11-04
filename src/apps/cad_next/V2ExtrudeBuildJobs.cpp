#include "V2ExtrudeBuildJobs.h"
#include "V2DocumentOpen.h"
#include "V2MainWindow.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QThread>
#include <QObject>
#include <algorithm>
#include <future>
#include <map>
#include <utility>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <type_traits>

using namespace kachakacha::v2;
struct V2ExtrudeBuildJobs::Impl {
    using Result=base::Result<kernel::ExtrudeBuildResult>;
    static_assert(std::is_nothrow_move_constructible_v<Result>&&std::is_nothrow_move_assignable_v<Result>);
    struct Job {
        base::FeatureId id;
        modeling::ExtrudeRequest request;
        modeling::ExtrudeAnalysis analysis;
        geometry::GeometryTolerance tolerance;
        std::optional<Result> result;
        bool ready=false;
    };
    std::vector<Job> jobs;std::map<base::FeatureId,std::size_t> positions;
    std::vector<std::future<void>> workers;
    std::atomic<std::size_t> next{0};std::atomic<unsigned> live{0};std::atomic<bool> stop{false};
    std::mutex mutex;std::condition_variable changed;
    static void Release(const Result& result) {
        if(result.HasValue())for(const auto& part:result.Value().parts)kernel::ReleaseShape(part.handle);
    }
    void Work() noexcept {
        try {
            while(!stop.load()) {
                const auto i=next.fetch_add(1);if(i>=jobs.size())break;auto& job=jobs[i];
                std::optional<Result> result;
                try {result=kernel::BuildExtrude(job.request,job.analysis,job.tolerance);}catch(...){}
                try {std::lock_guard lock(mutex);job.result=std::move(result);job.ready=true;}
                catch(...) {if(result)Release(*result);throw;}
                changed.notify_all();
            }
        }catch(...){} // A failed lane must still wake a waiter for the serial fallback.
        {std::lock_guard lock(mutex);live.fetch_sub(1);}
        changed.notify_all();
    }
    void Start() noexcept {
        if(jobs.size()<2)return;
        try {workers.reserve(2);}catch(...){return;}
        for(int lane=0;lane<2;++lane) {
            live.fetch_add(1);
            try {workers.push_back(std::async(std::launch::async,[this]{Work();}));}
            catch(...){live.fetch_sub(1);break;}
        }
    }
    ~Impl(){
        stop.store(true);changed.notify_all();
        for(auto& worker:workers)if(worker.valid())try {worker.get();}catch(...){}
        for(const auto& job:jobs)if(job.result)Release(*job.result);
    }
};
V2ExtrudeBuildJobs::V2ExtrudeBuildJobs(V2MainWindow& w,const std::vector<app::ShapeRebuildStep>& steps,
    const Builder& build):QObject(&w),impl_(std::make_unique<Impl>()) {
    if(!V2DocumentOpen::IsWorker()||qEnvironmentVariableIsSet("KACHACAD_OPEN_SERIAL")||QThread::idealThreadCount()<4)return;
    const auto& document=w.Session().GetDocument();const auto& tolerance=document.Snapshot().settings.tolerance;
    const auto source=[&](base::EntityId id) {
        const auto* entity=document.FindEntity(id);
        return entity&&entity->kind==domain::EntityKind::Wire&&entity->editPolicy==domain::EditPolicy::Source;
    };
    for(const auto& step:steps) {
        if(step.kind!=app::ShapeRebuildKind::Extrude||step.outputOrdinal!=0)continue;
        const auto* feature=document.FindFeature(step.featureId);
        const auto* definition=feature?std::get_if<domain::ExtrudeDefinition>(&feature->definition):nullptr;
        if(!definition||feature->outputs.size()!=1||definition->profiles.empty()||definition->booleanMode!=0
            ||definition->extentTarget||definition->endCap.Planar()||!definition->targets.empty()
            ||!definition->targetFaceBrep.empty()||definition->extentMode==static_cast<int>(modeling::ExtrudeExtentMode::ToTarget)
            ||!std::all_of(feature->inputEntityIds.begin(),feature->inputEntityIds.end(),source)
            ||!std::all_of(definition->profiles.begin(),definition->profiles.end(),source))continue;
        auto request=build(*definition);const auto analysis=modeling::AnalyzeExtrudeRequest(request,tolerance);
        if(!analysis.HasValue()||analysis.Value().expectedPartCount!=1)continue;
        impl_->positions.emplace(feature->id,impl_->jobs.size());
        impl_->jobs.push_back({feature->id,std::move(request),analysis.Value(),tolerance,{},false});
    }
    impl_->Start();
}
V2ExtrudeBuildJobs::~V2ExtrudeBuildJobs()=default;
bool V2ExtrudeBuildJobs::HasIndependentWork(V2MainWindow& w) {
    for(auto* child:w.children())if(auto* state=dynamic_cast<V2ExtrudeBuildJobs*>(child))return !state->impl_->workers.empty();
    return false;
}
std::optional<V2ExtrudeBuildJobs::Impl::Result> V2ExtrudeBuildJobs::Take(V2MainWindow& w,base::FeatureId id) {
    for(auto* child:w.children())if(auto* state=dynamic_cast<V2ExtrudeBuildJobs*>(child)) {
        const auto at=state->impl_->positions.find(id);if(at==state->impl_->positions.end())return std::nullopt;
        auto& job=state->impl_->jobs[at->second];
        std::optional<Impl::Result> result;
        {std::unique_lock lock(state->impl_->mutex);
            state->impl_->changed.wait(lock,[&]{return job.ready||state->impl_->live.load()==0;});
            result=std::move(job.result);job.result.reset();}
        if(result) {
            if(!result->HasValue()||result->Value().parts.size()==1)return result;
            Impl::Release(*result);
        }
        return std::nullopt;
    }
    return std::nullopt;
}
