#include "V2FaceActions.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "V2ShapeLifetime.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/document/Commands.h"
#include <QLabel>
#include <QString>
#include <algorithm>
using namespace kachakacha::v2;

bool V2FaceActions::Extract(const std::vector<app::SelectionRef>& refs)
{
    struct Prepared {
        domain::Feature feature;
        domain::Entity entity;
        std::vector<geometry::CurveSegment> edges;
        fabrication::SurfacePatchSamples samples;
        bool reused = false;
    };
    std::vector<Prepared> prepared;v2ui::ShapeListOwner owner({});
    // Prepare the complete batch before touching the document or installing a
    // native handle. Failure on any face leaves no partially extracted inputs.
    for(const auto& ref:refs) {
        const auto found=window_.partShapes_.find(ref.entityId.ToString());
        if(found==window_.partShapes_.end() || !ref.pickedFaceIndex){
            status_->setText(QStringLiteral("立体の面を選び直してください。"));return false;}
        const auto face=kernel::OutputFace(found->second,*ref.pickedFaceIndex);
        if(!face.HasValue()){status_->setText(QString::fromStdString(face.FirstSummaryJa()));return false;}
        owner.values.push_back(face.Value());
        const auto brep=kernel::CaptureOutputShape(face.Value());
        if(!brep.HasValue()){status_->setText(QString::fromStdString(brep.FirstSummaryJa()));return false;}
        Prepared item;
        // The original solid and its independent face occupy the same place.
        // Re-picking that solid must not keep creating identical frozen guides.
        // Match exact native bytes, never a display name or a nearby surface.
        for(const auto& entity:window_.session_->GetDocument().Snapshot().entities) {
            if(entity.kind!=domain::EntityKind::GuideSurface || !window_.guideShapes_.contains(entity.id.ToString()))continue;
            const auto* feature=window_.session_->GetDocument().FindFeature(entity.createdBy);
            const auto* frozen=feature&&feature->enabled?std::get_if<domain::FreezeDerivedDefinition>(&feature->definition):nullptr;
            if(frozen && frozen->frozenBrep==brep.Value()) {item.entity=entity;item.reused=true;break;}
        }
        if(item.reused){prepared.push_back(std::move(item));continue;}
        const auto edges=kernel::OutputEdges(face.Value(),window_.session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm);
        if(!edges.HasValue()){status_->setText(QString::fromStdString(edges.FirstSummaryJa()));return false;}
        const auto samples=kernel::FaceSamplesOf(face.Value(),0);
        if(!samples.HasValue()){status_->setText(QString::fromStdString(samples.FirstSummaryJa()));return false;}
        item.edges=edges.Value();item.samples=samples.Value().samples;
        const auto* original=window_.session_->GetDocument().FindEntity(ref.entityId);
        auto& feature=item.feature;feature.id=window_.ids_->NextTyped<base::IdKind::Feature>();
        feature.type=domain::FeatureType::FreezeDerived;
        feature.displayName=(original?original->displayName:"立体")+" の面 "+std::to_string(*ref.pickedFaceIndex+1)+"（独立）";
        domain::FreezeDerivedDefinition definition;definition.frozenBrep=brep.Value();feature.definition=definition;
        auto& entity=item.entity;entity.id=window_.ids_->NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;
        entity.displayName=feature.displayName;entity.kind=domain::EntityKind::GuideSurface;entity.editPolicy=domain::EditPolicy::Frozen;
        feature.outputs.push_back({"face",entity.id,entity.kind});prepared.push_back(std::move(item));
    }
    document::Document::Transaction transaction(window_.session_->GetDocument(),"近似用の面を取り出す");
    for(const auto& item:prepared) {
        if(item.reused)continue;
        const auto added=window_.session_->GetDocument().Run(document::AddFeatureCommand(item.feature,{item.entity},"近似用の面を取り出す"));
        if(!added.committed){window_.ReportDiagnostics(added.diagnostics);return false;}
    }
    if(!transaction.Commit()){status_->setText(QStringLiteral("面を取り出せなかったため、変更を取り消しました。"));return false;}
    app::SelectionSet selection;
    // Retain other approximation inputs, but never include the complete solid
    // whose specific face was requested. Tree rows do not imply face zero.
    for(const auto& id:sources_.entityIds) {
        if(std::any_of(refs.begin(),refs.end(),[&](const auto& r){return r.entityId==id;}))continue;
        const auto* entity=window_.session_->GetDocument().FindEntity(id);
        if(entity && (entity->kind==domain::EntityKind::GuideSurface || entity->kind==domain::EntityKind::Part))selection.entityIds.push_back(id);
    }
    for(std::size_t i=0;i<prepared.size();++i) {
        auto& item=prepared[i];const auto key=item.entity.id.ToString();
        if(item.reused) {
            const auto selected=std::find(selection.entityIds.begin(),selection.entityIds.end(),item.entity.id);
            if(selected==selection.entityIds.end())selection.entityIds.push_back(item.entity.id);
            else selection.entityIds.erase(selected);
            continue;
        }
        window_.guideShapes_[key]=owner.Take(i);window_.guideEdges_[key]=std::move(item.edges);
        window_.guideSamples_[key]=std::move(item.samples);selection.entityIds.push_back(item.entity.id);
    }
    window_.operationHost_->SetShelves({});window_.AdoptCurrentDocument();window_.RefreshShapeViews();window_.RefreshExportCounts();
    window_.viewport_->SetSelection(std::move(selection));return true;
}
