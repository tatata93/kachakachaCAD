#include "V2EvaluationCache.h"
#include "V2DocumentCache.h"
#include "V2DocumentOpen.h"
#include "V2DocumentTiming.h"
#include "V2MainWindow.h"
#include "V2PreparedDocument.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/kernel/OcctShapeTransfer.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <map>
#include <set>
#include <utility>

using namespace kachakacha::v2;
namespace {
using Owners=std::map<QString,base::FeatureId>;
void CollectReferences(const QJsonValue& value,const Owners& owners,
    std::set<base::FeatureId>& references) {
    if(value.isString()) {
        const auto ref=value.toString();
        if(ref.size()!=36)return;
        if(const auto owner=owners.find(ref);owner!=owners.end())references.insert(owner->second);
    } else if(value.isArray()) {
        for(const auto& item:value.toArray())CollectReferences(item,owners,references);
    } else if(value.isObject()) {
        const auto object=value.toObject();
        for(auto item=object.begin();item!=object.end();++item)CollectReferences(item.value(),owners,references);
    }
}
std::map<std::string,QString> Keys(const document::DocumentSnapshot& snapshot) {
    // Serialize complete definitions with the existing lossless writer. Do not
    // round numbers through the reference walker or omit new geometry fields.
    // UUID references are collected from the complete record as well as the
    // explicit dependency list, including optional references in old files.
    io::DocumentFile header;
    header.snapshot.id=snapshot.id;header.snapshot.settings=snapshot.settings;
    header.snapshot.settings.activeGroupId.reset();
    header.snapshot.referenceDimensions=snapshot.referenceDimensions;
    const auto context=QCryptographicHash::hash(QByteArray::fromStdString(io::WriteDocumentJson(header)),
        QCryptographicHash::Sha256);
    Owners owners;
    std::map<base::EntityId,const domain::Entity*> entities;
    for(const auto& entity:snapshot.entities){
        entities.emplace(entity.id,&entity);
        owners.emplace(QString::fromStdString(entity.id.ToString()),entity.createdBy);
    }
    for(const auto& feature:snapshot.features){
        owners.emplace(QString::fromStdString(feature.id.ToString()),feature.id);
        if(const auto* wire=std::get_if<domain::CreateWireDefinition>(&feature.definition))
            for(const auto segment:wire->segmentIds)owners.emplace(QString::fromStdString(segment.ToString()),feature.id);
    }
    struct Record { QByteArray definition; std::set<base::FeatureId> inputs; QByteArray digest; };
    std::map<base::FeatureId,Record> records;
    for(auto feature:snapshot.features) {
        const auto id=feature.id;
        feature.revision=0;feature.displayName.clear();feature.derivedGroupId.reset();
        io::DocumentFile file;file.snapshot.id=snapshot.id;
        file.snapshot.features.push_back(feature);file.snapshot.evaluationOrder={id};
        for(const auto& output:feature.outputs) {
            const auto entity=entities.find(output.entityId);if(entity==entities.end())return {};
            auto value=*entity->second;value.revision=0;value.displayName.clear();value.groupId.reset();
            value.generatedFrom.reset(); // Provenance, not a geometry dependency.
            value.visibility=domain::Visibility::Visible;file.snapshot.entities.push_back(std::move(value));
        }
        auto& record=records[id];record.definition=QByteArray::fromStdString(io::WriteDocumentJson(file));
        const auto parsed=QJsonDocument::fromJson(record.definition);if(!parsed.isObject())return {};
        CollectReferences(parsed.object(),owners,record.inputs);
        for(const auto input:feature.inputEntityIds) {
            const auto entity=entities.find(input);if(entity==entities.end())return {};
            record.inputs.insert(entity->second->createdBy);
        }
        record.inputs.erase(id);
    }
    // Use the validated dependency order. If an optional reference is out of
    // order, or otherwise cannot be verified, its entire downstream chain is
    // left uncached. No speculative reuse on partial input matches.
    for(const auto id:snapshot.evaluationOrder) {
        auto found=records.find(id);if(found==records.end())return {};
        auto& record=found->second;
        const auto missing=std::any_of(record.inputs.begin(),record.inputs.end(),[&](const auto input){
            const auto parent=records.find(input);return parent==records.end()||parent->second.digest.isEmpty();
        });
        if(missing)continue;
        QCryptographicHash digest(QCryptographicHash::Sha256);
        digest.addData(context);digest.addData(QCryptographicHash::hash(record.definition,QCryptographicHash::Sha256));
        for(const auto input:record.inputs)digest.addData(records.at(input).digest);
        record.digest=digest.result();
    }
    std::map<std::string,QString> keys;
    for(const auto& feature:snapshot.features) {
        const auto& digest=records.at(feature.id).digest;if(digest.isEmpty())continue;
        const auto state=digest.toHex().toStdString();
        for(const auto& output:feature.outputs) {
            const auto entity=output.entityId.ToString();
            keys[entity]=V2DocumentCache::Key("feature-result-v2\n"+state+"\n"+entity);
        }
    }
    return keys;
}
template<class Map> void CopyOne(const Map& source,Map& target,const std::string& id) {
    if(const auto found=source.find(id);found!=source.end())target.emplace(id,found->second);
}
bool Cacheable(const app::ShapeRebuildStep& step,const document::Document& document) {
    using Kind=app::ShapeRebuildKind;
    if(step.kind==Kind::Extrude||step.kind==Kind::ThickenSurface)return true;
    if(step.kind!=Kind::GuideSurface)return false;
    const auto* feature=document.FindFeature(step.featureId);
    const auto* definition=feature?std::get_if<domain::CreateGuideSurfaceDefinition>(&feature->definition):nullptr;
    return definition&&!definition->gptBuilder;
}
}
struct V2EvaluationCache::Impl {
    struct Ready {
        std::shared_ptr<V2PreparedDocument> data;
        modeling::KernelShapeHandle handle;
    };
    V2MainWindow& w;
    std::map<std::string,QString> keys;
    std::map<std::string,Ready> ready;
    explicit Impl(V2MainWindow& window):w(window) {
        if(V2DocumentOpen::IsWorker()&&!qEnvironmentVariableIsSet("KACHACAD_EVALUATION_CACHE_DISABLE"))
            try {keys=Keys(window.Session().GetDocument().Snapshot());}catch(...){keys.clear();}
    }
    ~Impl(){for(const auto& [id,value]:ready)if(value.handle.Valid())kernel::ReleaseShape(value.handle);}
};
V2EvaluationCache::V2EvaluationCache(V2MainWindow& w,const std::vector<app::ShapeRebuildStep>& steps)
    :impl_(std::make_unique<Impl>(w)) {
    if(impl_->keys.empty()||steps.empty())return;
    V2DocumentTiming timing("feature-cache");
    const auto& document=w.Session().GetDocument();
    std::map<std::string,bool> wanted;
    for(const auto& step:steps) {
        if(!Cacheable(step,document))continue;
        if(const auto* entity=document.FindEntity(step.outputEntityId))
            wanted.emplace(step.outputEntityId.ToString(),entity->kind==domain::EntityKind::Part);
    }
    for(const auto& entry:V2DocumentCache::FeatureIndexes()) {
        if(wanted.empty())break;
        const auto index=QJsonDocument::fromJson(entry.contents).object();
        std::vector<std::string> matches;
        for(const auto& [id,part]:wanted) {
            const auto key=impl_->keys.find(id);
            if(key!=impl_->keys.end()&&!key->second.isEmpty()
                &&index.value(QString::fromStdString(id)).toString()==key->second)matches.push_back(id);
        }
        if(matches.empty())continue;
        timing.Mark(("indexed-matches-"+std::to_string(matches.size())).c_str());
        try {
            auto payload=V2DocumentCache::ReadKey(entry.key);if(!payload){timing.Mark("ignore-unreadable-payload");continue;}
            const auto archive=io::LoadDocument(payload->archive);if(!archive.HasValue()){timing.Mark("ignore-invalid-archive");continue;}
            // The small index is only a hint. Recompute identity from the
            // validated archive contained in the original transfer payload.
            const auto prior=Keys(archive.Value().snapshot);
            auto data=std::make_shared<V2PreparedDocument>(std::move(*payload));
            for(const auto& id:matches) {
                const auto old=prior.find(id);if(old==prior.end()||old->second!=impl_->keys.at(id))continue;
                const auto& shapes=wanted.at(id)?data->parts:data->guides;
                const auto found=shapes.find(id);if(found==shapes.end())continue;
                const auto shape=kernel::RestoreTransferShape(found->second);if(!shape.HasValue())continue;
                impl_->ready.emplace(id,Impl::Ready{data,shape.Value()});
                wanted.erase(id);
            }
            timing.Mark(("ready-"+std::to_string(impl_->ready.size())).c_str());
        }catch(...){timing.Mark("ignore-invalid-feature-result");}
    }
}
V2EvaluationCache::~V2EvaluationCache()=default;
bool V2EvaluationCache::Available(const app::ShapeRebuildStep& step) const {
    return impl_->ready.contains(step.outputEntityId.ToString());
}
bool V2EvaluationCache::Restore(const app::ShapeRebuildStep& step) {
    const auto id=step.outputEntityId.ToString();const auto found=impl_->ready.find(id);
    if(found==impl_->ready.end()||!found->second.handle.Valid())return false;
    auto& w=impl_->w;auto& data=*found->second.data;const auto handle=found->second.handle;
    const bool part=data.parts.contains(id);
    if(!part) {
        // Reconstruct the table from the current document so labels and source
        // selection agree with this revision, even on a native cache hit.
        const auto* feature=w.Session().GetDocument().FindFeature(step.featureId);
        const auto* definition=feature?std::get_if<domain::CreateGuideSurfaceDefinition>(&feature->definition):nullptr;
        if(!definition)return false;
        auto table=app::GuideTableFromDefinition(w.Session().GetDocument(),w.Session().Scene(),*definition);
        if(!table.HasValue())return false;
        w.guideTable_=std::move(table.Value());
    }
    (part?w.partShapes_:w.guideShapes_)[id]=handle;
    CopyOne(data.partEdges,w.partEdges_,id);CopyOne(data.flatBoundaries,w.partFlatBoundary_,id);
    CopyOne(data.guideEdges,w.guideEdges_,id);CopyOne(data.samples,w.guideSamples_,id);
    const auto* entity=w.Session().GetDocument().FindEntity(step.outputEntityId);
    if(data.meshes.contains(id)&&entity&&app::EntityEffectivelyVisible(w.Session().GetDocument().Snapshot(),*entity))
        w.shapeMeshes_[handle.value]=std::move(data.meshes.at(id));
    impl_->ready.erase(found);return true;
}
void V2EvaluationCache::IndexPrepared(V2MainWindow& w,const std::string& source,const V2PreparedDocument& prepared) {
    V2EvaluationCache cache(w,{});
    const auto& document=w.Session().GetDocument();
    QJsonObject index;
    for(const auto& step:app::PlanShapeRebuild(document.Snapshot())) {
        if(!Cacheable(step,document))continue;
        const auto id=step.outputEntityId.ToString();
        const auto key=cache.impl_->keys.find(id);if(key==cache.impl_->keys.end())continue;
        if(prepared.parts.contains(id)||prepared.guides.contains(id))index[QString::fromStdString(id)]=key->second;
    }
    if(!index.isEmpty())V2DocumentCache::WriteFeatureIndex(source,QJsonDocument(index).toJson(QJsonDocument::Compact));
}
