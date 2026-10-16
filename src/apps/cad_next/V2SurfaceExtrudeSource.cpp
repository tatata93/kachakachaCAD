#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2Calculation.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <QString>
#include <iomanip>
#include <sstream>
#include <memory>

bool V2MainWindow::PrepareSurfaceExtrudeSource(kachakacha::v2::app::ExtrudePlan& plan) {
    using namespace kachakacha::v2;
    if (plan.profiles.empty()) return false;
    const auto id = plan.profiles.front();
    const auto* entity=session_->GetDocument().FindEntity(id);
    if(entity && entity->kind==domain::EntityKind::Wire && !guideShapes_.contains(id.ToString())) {
        app::SelectionSet profileSelection;
        for(const auto candidate:plan.profiles) {
            const auto* profile=session_->GetDocument().FindEntity(candidate);
            if(profile&&profile->kind==domain::EntityKind::Wire)profileSelection.entityIds.push_back(candidate);
        }
        const auto curves=app::SelectedWholeCurves(profileSelection,session_->Scene());
        const auto fit=geometry::FitPlane(geometry::SampleChain(curves,0.001));
        if(!fit.valid || fit.maximumDeviationMm<=session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm)return false;
        const auto& snapshot=session_->GetDocument().Snapshot();
        modeling::SnapScene selectedScene;
        for(const auto& curve:session_->Scene().curves)
            if(std::find(profileSelection.entityIds.begin(),profileSelection.entityIds.end(),curve.entityId)
                !=profileSelection.entityIds.end())selectedScene.curves.push_back(curve);
        auto boundaries=app::DetectSpatialBoundaries(selectedScene,snapshot.settings.tolerance);
        std::sort(boundaries.begin(),boundaries.end(),[](const auto& a,const auto& b){
            const auto area=[](const auto& boundary){geometry::Vector3 sum{};
                for(std::size_t i=0;i<boundary.sampled.size();++i)
                    sum=sum+geometry::Cross(boundary.sampled[i],boundary.sampled[(i+1)%boundary.sampled.size()]);
                return sum.Length();};
            return area(a)>area(b);
        });
        std::vector<std::vector<geometry::CurveSegment>> loops;
        for(const auto& boundary:boundaries)loops.push_back(boundary.segments);
        std::vector<modeling::KernelShapeHandle> supports;
        for(const auto* shapes:{&partShapes_,&guideShapes_})for(const auto& [key,handle]:*shapes) {
            const auto parsed=base::EntityId::Parse(key);if(!parsed)continue;
            const auto* support=session_->GetDocument().FindEntity(*parsed);
            if(support&&(support->kind==domain::EntityKind::Part||support->kind==domain::EntityKind::GuideSurface)
                &&app::EntityEffectivelyVisible(snapshot,*support))supports.push_back(handle);
        }
        const auto region=V2Calculation::Call(this,"輪郭内の面を切り出しています…",
            [](const auto&... args){return kernel::TrimSurfaceRegion(args...);},supports,
            loops,snapshot.settings.tolerance,
            snapshot.settings.tolerance.interactiveJoinMm);
        if(!region.HasValue()){ReportDiagnostics(region.Diagnostics());return false;}
        if(!region.Diagnostics().empty())ReportDiagnostics(region.Diagnostics());
        guideShapes_[id.ToString()]=region.Value();faceProfileSolid_=id;
        plan.profiles={id};plan.kind=app::ExtrudeInputKind::FaceOnly;return true;
    }
    if (plan.profiles.size() != 1) return false;
    if (guideShapes_.contains(id.ToString())) {
        plan.kind = app::ExtrudeInputKind::FaceOnly;
        return true;
    }
    if (plan.kind != app::ExtrudeInputKind::SolidAndFace) return false;
    const auto solid = partShapes_.find(id.ToString());
    if (solid == partShapes_.end()) return false;
    for (const auto& ref : viewport_->Selection().ordered) {
        if (ref.entityId != id || !ref.pickedFaceIndex || ref.kind != app::SelectionElementKind::Face) continue;
        const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
        const auto pose = kernel::FacePoseNear(solid->second, *ref.pickedFaceIndex, ref.hitPoint, tolerance);
        if (!pose.HasValue() || pose.Value().planar) return false; // Preserve planar push/pull.
        const auto face = kernel::OutputFace(solid->second, *ref.pickedFaceIndex);
        if (!face.HasValue()) { ReportDiagnostics(face.Diagnostics()); return false; }
        // A private preview shape, not a document entity. ForgetFaceProfile releases it.
        guideShapes_[id.ToString()] = face.Value();
        faceProfileSolid_ = id;
        faceNormal_ = pose.Value().normal;
        plan.kind = app::ExtrudeInputKind::FaceOnly;
        plan.targetSolid = {};
        return true;
    }
    return false;
}

kachakacha::v2::base::EntityId V2MainWindow::CommitSurfaceExtrudeFace() {
    using namespace kachakacha::v2;
    if (faceProfileSolid_.IsNil()) return {};
    const auto shape = guideShapes_.find(faceProfileSolid_.ToString());
    if (shape == guideShapes_.end()) return {};
    const auto brep = kernel::CaptureOutputShape(shape->second);
    if (!brep.HasValue()) { ReportDiagnostics(brep.Diagnostics()); return {}; }
    domain::Feature feature;
    feature.id = ids_->NextTyped<base::IdKind::Feature>();
    feature.type = domain::FeatureType::FreezeDerived;
    feature.displayName = "押し出し元フェイス";
    domain::FreezeDerivedDefinition definition;
    definition.frozenBrep = brep.Value(); feature.definition = definition;
    domain::Entity entity;
    entity.id = ids_->NextTyped<base::IdKind::Entity>(); entity.createdBy = feature.id;
    entity.kind = domain::EntityKind::GuideSurface; entity.displayName = feature.displayName;
    entity.editPolicy = domain::EditPolicy::Frozen;
    entity.visibility = domain::Visibility::Hidden;
    feature.outputs.push_back({"face", entity.id, entity.kind});
    const auto added = session_->GetDocument().Run(document::AddFeatureCommand(feature, {entity}, "押し出し元の面"));
    if (!added.committed) { ReportDiagnostics(added.diagnostics); return {}; }
    return entity.id;
}

kachakacha::v2::base::Result<kachakacha::v2::kernel::ExtrudeBuildResult> V2MainWindow::SurfaceExtrudePreview(
    const kachakacha::v2::app::ExtrudeChoice& choice,
    kachakacha::v2::modeling::KernelShapeHandle source, double start, double end) {
    using namespace kachakacha::v2;
    using Out = base::Result<kernel::ExtrudeBuildResult>;
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    std::ostringstream encoded; encoded << std::setprecision(17) << source.value << ':'
        << session_->GetDocument().Revision() << ':' << static_cast<int>(choice.extent) << ':'
        << start << ':' << end << ':' << choice.customDirection.x << ':' << choice.customDirection.y << ':'
        << choice.customDirection.z << ':' << choice.targetParameter << ':'
        << static_cast<int>(choice.endCap.mode) << ':' << choice.endCap.referenceNormal.x << ':'
        << choice.endCap.referenceNormal.y << ':' << choice.endCap.referenceNormal.z << ':'
        << choice.endCap.tiltRad << ':' << choice.endCap.headingRad << ':' << choice.endCap.extendTargetPlane << ':'
        << choice.targetOffsetMm << ':' << choice.targetFaceBrep << ':'
        << (choice.targetEntityId ? choice.targetEntityId->ToString() : "") << ':'
        << tolerance.modelLinearMm << ':' << tolerance.modelAngularRad << ':' << tolerance.numericEpsilon;
    auto& snapshot = *extrudeSnapshot_;
    const auto key = encoded.str();
    if (snapshot.nativePreview && snapshot.nativePreviewKey == key) return Out::Success(*snapshot.nativePreview);
    snapshot.nativePreview.reset(); snapshot.nativePreviewKey.clear();
    snapshot.nativePreviewMeshes.clear();
    auto built = (choice.extent == modeling::ExtrudeExtentMode::ToTarget || choice.endCap.Planar())
        ? BuildToTarget(choice, snapshot.profiles, source)
        : V2Calculation::Call(this, "面の押し出しを計算しています…",
            [](const auto&... values) { return kernel::BuildSurfaceExtrude(values...); },
            source, choice.customDirection, start, end, tolerance);
    if (!built.HasValue()) return built;
    snapshot.nativePreviewKey = key;
    snapshot.nativePreview = std::shared_ptr<kernel::ExtrudeBuildResult>(new kernel::ExtrudeBuildResult(built.Value()),
        [](auto* result) { for (const auto& part : result->parts) kernel::ReleaseShape(part.handle); delete result; });
    return built;
}
