#include "V2MainWindow.h"
#include "V2ExtrudeDock.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include <QString>
#include <algorithm>
#include <cmath>

std::vector<ExtrudeTargetChoice> V2MainWindow::ExtrudeStopChoices() const
{
    auto result = ExtrudeTargets();
    const auto name = [this](const kachakacha::v2::base::EntityId& id) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        return entity == nullptr ? QStringLiteral("対象") : QString::fromStdString(entity->displayName);
    };
    for (const auto& point : session_->Scene().points) result.push_back({point.entityId, name(point.entityId)});
    for (const auto& curve : session_->Scene().curves) {
        const auto count = std::count_if(session_->Scene().curves.begin(), session_->Scene().curves.end(),
            [&](const auto& other) { return other.entityId == curve.entityId; });
        if (count != 1 || curve.segment.Kind() != kachakacha::v2::geometry::CurveKind::Line) continue;
        result.push_back({curve.entityId, name(curve.entityId) + QStringLiteral("（線の傾きまで）")});
        result.push_back({curve.entityId, name(curve.entityId) + QStringLiteral("（始点まで）"), {}, 0.0});
        result.push_back({curve.entityId, name(curve.entityId) + QStringLiteral("（終点まで）"), {}, 1.0});
    }
    const auto plan = PlanExtrudeFromSelection();
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::GuideSurface
            && std::find(plan.profiles.begin(), plan.profiles.end(), entity.id) == plan.profiles.end()) {
            result.push_back({entity.id, name(entity.id) + QStringLiteral("（面まで）")});
        }
    }
    return result;
}

std::optional<kachakacha::v2::modeling::WorkPlaneFrame> V2MainWindow::ExtrudeStopPlane(
    const kachakacha::v2::base::EntityId& id,
    const kachakacha::v2::geometry::Vector3& direction, double parameter) const
{
    using namespace kachakacha::v2::geometry;
    if (const auto plane = WorkPlaneFrameOf(id); plane.has_value()) return plane;
    kachakacha::v2::modeling::WorkPlaneFrame plane;
    bool found = false;
    Vector3 normal = direction;
    for (const auto& point : session_->Scene().points) {
        if (point.entityId == id) { plane.origin = point.position; found = true; }
    }
    for (const auto& curve : session_->Scene().curves) {
        if (curve.entityId != id || curve.segment.Kind() != CurveKind::Line) continue;
        plane.origin = curve.segment.Evaluate(parameter >= 0.0 ? parameter : 0.0);
        if (parameter < 0.0) {
            const auto tangent = Normalized(curve.segment.EndPoint() - curve.segment.StartPoint(), 1.0e-12);
            normal = direction - tangent * Dot(direction, tangent);
        }
        found = true;
    }
    const auto shape = guideShapes_.find(id.ToString());
    if (shape != guideShapes_.end()) {
        const auto pose = kachakacha::v2::kernel::FacePoseNear(shape->second, 0, {},
            session_->GetDocument().Snapshot().settings.tolerance);
        if (!pose.HasValue() || !pose.Value().planar) return std::nullopt;
        plane.origin = pose.Value().point;
        normal = pose.Value().normal;
        found = true;
    }
    if (!found || !normal.IsFinite() || normal.Length() < 1.0e-9) return std::nullopt;
    plane.normal = Normalized(normal, 1.0e-12);
    const Vector3 reference = std::abs(plane.normal.z) < 0.9 ? Vector3{0, 0, 1} : Vector3{0, 1, 0};
    plane.uAxis = Normalized(Cross(reference, plane.normal), 1.0e-12);
    plane.vAxis = Cross(plane.normal, plane.uAxis);
    return plane;
}

kachakacha::v2::base::Result<kachakacha::v2::kernel::ExtrudeBuildResult> V2MainWindow::BuildToTarget(
    const kachakacha::v2::app::ExtrudeChoice& choice,
    const std::vector<kachakacha::v2::modeling::ExtrudeProfile>& profiles,
    kachakacha::v2::modeling::KernelShapeHandle source) const
{
    using namespace kachakacha::v2;
    auto request = app::ToExtrudeRequest(choice, profiles, viewport_->WorkPlane(), std::nullopt);
    request.directionMode = modeling::ExtrudeDirectionMode::CustomXYZ;
    request.customDirection = choice.customDirection;
    request.reversed = choice.reversed;
    modeling::KernelShapeHandle target;
    if (choice.targetEntityId.has_value()) {
        const auto plane = ExtrudeStopPlane(*choice.targetEntityId,
            choice.customDirection * (choice.reversed ? -1.0 : 1.0), choice.targetParameter);
        if (plane.has_value()) {
            request.targetKind = modeling::ExtrudeTargetKind::Plane;
            request.targetPlane = *plane;
        } else {
            const auto* entity = session_->GetDocument().FindEntity(*choice.targetEntityId);
            if (entity != nullptr && entity->kind == domain::EntityKind::Wire) {
                return base::Result<kernel::ExtrudeBuildResult>::Failure(base::MakeError("EXT-003",
                    "押し出し方向に平行な線では終端の斜面を決められません。", "端点、または作業平面を選択してください。"));
            }
            const auto found = guideShapes_.find(choice.targetEntityId->ToString());
            if (found != guideShapes_.end()) target = found->second;
        }
    }
    return kernel::BuildExtrudeToFace(request, source, target,
        session_->GetDocument().Snapshot().settings.tolerance);
}

bool V2MainWindow::HandleTargetExtrude(bool confirm)
{
    using namespace kachakacha::v2;
    if (!extrudeSnapshot_.has_value() || !viewport_->ExtrudeHandleShown()
        || extrudeDock_->ExtentMode() != modeling::ExtrudeExtentMode::ToTarget) return false;
    const auto plan = extrudeSnapshot_->plan;
    if (plan.kind == app::ExtrudeInputKind::FaceOnly) return false;
    app::ExtrudeChoice choice = extrudeChoice_;
    choice.direction = modeling::ExtrudeDirectionMode::CustomXYZ;
    choice.customDirection = ExtrudeDirectionNow();
    choice.reversed = false;
    choice.extent = modeling::ExtrudeExtentMode::ToTarget;
    choice.targetEntityId = extrudeDock_->TargetEntityId();
    choice.targetParameter = extrudeDock_->TargetParameter();
    choice.booleanMode = extrudeDock_->BooleanMode();
    choice.hasSelectedPart = !plan.targetSolid.IsNil();
    const auto output = extrudeDock_->Outputs();
    const auto refuse = [&](const QString& message) {
        app::ExtrudeHandle handle{viewport_->ExtrudeHandleOrigin(), choice.customDirection, extrudeDock_->DistanceMm()};
        viewport_->ShowExtrudeHandle(handle, {});
        viewport_->SetExtrudePreviewFaces({});
        extrudeDock_->ShowStatusLines({message}, false);
    };
    if (!extrudeDock_->DirectionReady()) {
        refuse(QStringLiteral("方向に使う直線、または0ではない方向を指定してください。"));
        return true;
    }
    if (!output.body || output.startWire || output.endWire || output.sideWires) {
        refuse(QStringLiteral("面・線・点までの押し出しは出力を「ソリッド / 面」にしてください。独立ワイヤーは未対応です。"));
        return true;
    }
    auto built = BuildToTarget(choice, extrudeSnapshot_->profiles);
    if (!built.HasValue()) { refuse(QString::fromStdString(built.FirstSummaryJa())); return true; }
    if (choice.booleanMode != modeling::ExtrudeBooleanMode::NewPart) {
        const auto target = partShapes_.find(plan.targetSolid.ToString());
        if (target == partShapes_.end() || built.Value().parts.size() != 1) {
            refuse(QStringLiteral("足す・引く相手の立体と1つの輪郭を指定してください。")); return true;
        }
        const auto operation = choice.booleanMode == modeling::ExtrudeBooleanMode::AddToPart
            ? kernel::BooleanOperation::Union : kernel::BooleanOperation::Difference;
        const auto combined = kernel::BuildBoolean(operation, target->second, built.Value().parts[0].handle,
            session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm);
        if (!combined.HasValue()) { refuse(QString::fromStdString(combined.FirstSummaryJa())); return true; }
        auto updated = built.Value();
        updated.parts[0].handle = combined.Value().handle;
        updated.parts[0].volumeMm3 = combined.Value().volumeMm3;
        built = decltype(built)::Success(std::move(updated));
    }
    std::vector<std::vector<geometry::Vector3>> edges, faces;
    for (const auto& part : built.Value().parts) {
        const auto mesh = kernel::BuildShapeMesh(part.handle);
        if (!mesh.HasValue()) { refuse(QString::fromStdString(mesh.FirstSummaryJa())); return true; }
        edges.insert(edges.end(), mesh.Value().edges.begin(), mesh.Value().edges.end());
        for (const auto& t : mesh.Value().triangles) faces.push_back({t.points[0], t.points[1], t.points[2]});
    }
    app::ExtrudeHandle handle{viewport_->ExtrudeHandleOrigin(), choice.customDirection, extrudeDock_->DistanceMm()};
    viewport_->ShowExtrudeHandle(handle, std::move(edges));
    viewport_->SetExtrudePreviewFaces(std::move(faces));
    extrudeDock_->ShowStatusLines({QStringLiteral("選んだ終端の形で止めます。この形で確定できます。")}, true);
    if (confirm) {
        modeling::ExtrudeAnalysis analysis;
        analysis.direction = choice.customDirection;
        CommitExtrude(choice, plan, analysis, built.Value());
    }
    return true;
}
