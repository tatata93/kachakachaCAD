#include "V2Calculation.h"
#include "V2ExtrudeWirePreview.h"
#include "V2MainWindow.h"
#include "V2ExtrudeDock.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include <QString>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
template<class Shapes>
bool ContainsHandle(const Shapes& shapes, kachakacha::v2::modeling::KernelShapeHandle handle)
{
    return std::any_of(shapes.begin(), shapes.end(),
        [&](const auto& entry) { return entry.second.value == handle.value; });
}
}

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
    if (choice.extent == modeling::ExtrudeExtentMode::ToTarget && choice.targetEntityId.has_value() && choice.targetFaceBrep.empty()) {
        const auto plane = ExtrudeStopPlane(*choice.targetEntityId,
            choice.customDirection * (choice.reversed ? -1.0 : 1.0), choice.targetParameter);
        if (plane.has_value() && guideShapes_.find(choice.targetEntityId->ToString()) == guideShapes_.end()) {
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
    if (choice.extent == modeling::ExtrudeExtentMode::ToTarget && !choice.targetFaceBrep.empty()) {
        const auto restored = kernel::RestoreOutputShape(choice.targetFaceBrep);
        if (!restored.HasValue()) return base::Result<kernel::ExtrudeBuildResult>::Failure(restored.Diagnostics());
        target = restored.Value();
    }
    const auto result = V2Calculation::Call(nullptr, "選んだ終端までの押し出しを計算しています…",
        [](const auto&... values) { return kachakacha::v2::kernel::BuildExtrudeEndCap(values...); }, request, source, target,
        session_->GetDocument().Snapshot().settings.tolerance, choice.targetOffsetMm, choice.endCap);
    if (choice.extent == modeling::ExtrudeExtentMode::ToTarget && !choice.targetFaceBrep.empty()) kernel::ReleaseShape(target);
    return result;
}

bool V2MainWindow::HandleTargetExtrude(bool confirm)
{
    using namespace kachakacha::v2;
    if (!extrudeSnapshot_.has_value() || !viewport_->ExtrudeHandleShown()
        || (extrudeDock_->ExtentMode() != modeling::ExtrudeExtentMode::ToTarget && !extrudeDock_->EndCap().Planar())) return false;
    const auto plan = extrudeSnapshot_->plan;
    if (plan.kind == app::ExtrudeInputKind::FaceOnly) return false;
    app::ExtrudeChoice choice = extrudeChoice_;
    choice.direction = modeling::ExtrudeDirectionMode::CustomXYZ;
    choice.customDirection = ExtrudeDirectionNow();
    choice.reversed = false;
    choice.extent = extrudeDock_->ExtentMode();
    choice.distanceMm = extrudeDock_->DistanceMm();
    choice.secondDistanceMm = extrudeDock_->SecondDistanceMm();
    choice.endCap = extrudeDock_->EndCap();
    choice.targetEntityId = extrudeDock_->TargetEntityId();
    choice.targetParameter = extrudeDock_->TargetParameter();
    choice.targetOffsetMm = extrudeDock_->TargetOffsetMm();
    choice.targetFaceBrep = extrudeDock_->TargetFaceBrep();
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
    if (!output.body && !output.startWire && !output.endWire && !output.sideWires) {
        refuse(QStringLiteral("作るものを1つ以上選んでください。"));
        return true;
    }
    choice.makePart = output.body;
    choice.makeStartProfileWire = output.startWire;
    choice.makeEndProfileWire = output.endWire;
    choice.makeSideBoundaryWires = output.sideWires;
    auto built = SurfaceExtrudePreview(choice, {}, choice.distanceMm, choice.secondDistanceMm);
    if (!built.HasValue()) { refuse(QString::fromStdString(built.FirstSummaryJa())); return true; }
    const auto retainedPreview = extrudeSnapshot_->nativePreview;
    // Boolean output is separately owned; the unmodified preview remains cached.
    auto temporary = std::shared_ptr<std::vector<modeling::KernelShapeHandle>>(
        new std::vector<modeling::KernelShapeHandle>, [this](auto* handles) {
            for (const auto& handle : *handles) {
                if (!ContainsHandle(partShapes_, handle)) {
                    if (extrudeSnapshot_) extrudeSnapshot_->nativePreviewMeshes.erase(handle.value);
                    kernel::ReleaseShape(handle);
                }
            }
            delete handles;
        });
    if (choice.makePart && choice.booleanMode != modeling::ExtrudeBooleanMode::NewPart) {
        const auto target = partShapes_.find(plan.targetSolid.ToString());
        if (target == partShapes_.end() || built.Value().parts.size() != 1) {
            refuse(QStringLiteral("足す・引く相手の立体と1つの輪郭を指定してください。")); return true;
        }
        const auto operation = choice.booleanMode == modeling::ExtrudeBooleanMode::AddToPart
            ? kernel::BooleanOperation::Union : kernel::BooleanOperation::Difference;
        const auto combined = V2Calculation::Call(nullptr, "部品の演算を計算しています…",
        [](const auto&... values) { return kachakacha::v2::kernel::BuildBoolean(values...); }, operation, target->second, built.Value().parts[0].handle,
            session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm);
        if (!combined.HasValue()) { refuse(QString::fromStdString(combined.FirstSummaryJa())); return true; }
        temporary->push_back(combined.Value().handle);
        auto updated = built.Value();
        updated.parts[0].handle = combined.Value().handle;
        updated.parts[0].volumeMm3 = combined.Value().volumeMm3;
        built = decltype(built)::Success(std::move(updated));
    }
    std::vector<std::vector<geometry::Vector3>> edges, faces;
    for (const auto& part : built.Value().parts) {
        auto& cache = extrudeSnapshot_->nativePreviewMeshes;
        if (cache.find(part.handle.value) == cache.end()) {
            const auto mesh = V2Calculation::Call(nullptr, "押し出しの表示を準備しています…",
                [](auto value) { return kernel::BuildShapeMesh(value); }, part.handle);
            if (!mesh.HasValue()) { refuse(QString::fromStdString(mesh.FirstSummaryJa())); return true; }
            cache.emplace(part.handle.value, mesh.Value());
        }
        const auto& mesh = cache.at(part.handle.value);
        edges.insert(edges.end(), mesh.edges.begin(), mesh.edges.end());
        if (output.body) for (const auto& t : mesh.triangles) faces.push_back({t.points[0], t.points[1], t.points[2]});
    }
    app::ExtrudeHandle handle{viewport_->ExtrudeHandleOrigin(), choice.customDirection, extrudeDock_->DistanceMm()};
    if (!output.body) edges = ExtrudeWirePreview(built.Value(), output.startWire, output.endWire, output.sideWires);
    viewport_->ShowExtrudeHandle(handle, std::move(edges));
    viewport_->SetExtrudePreviewFaces(std::move(faces));
    extrudeDock_->ShowStatusLines({QStringLiteral("指定した終端で止めます。この形で確定できます。")}, true);
    if (confirm) {
        modeling::ExtrudeAnalysis analysis;
        analysis.direction = choice.customDirection;
        CommitExtrude(choice, plan, analysis, built.Value());
        // Transfer only handles actually adopted by the committed document.
        auto& parts = retainedPreview->parts;
        parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const auto& part) {
            return ContainsHandle(partShapes_, part.handle);
        }), parts.end());
    }
    return true;
}

void V2MainWindow::BeginExtrudeStopPick()
{
    using namespace kachakacha::v2;
    if (viewport_->ReferencePickPending()) viewport_->CancelReferencePick();
    if (extrudeDock_->ExtentMode() != modeling::ExtrudeExtentMode::ToTarget) return;
    extrudeDock_->SetStopPicking(true);
    viewport_->BeginElementPick([this](const app::PickCandidate& pick) {
        const auto* entity = session_->GetDocument().FindEntity(pick.entityId);
        if (!entity) return false;
        if (entity->kind == domain::EntityKind::Part)
            return pick.kind == app::SelectionElementKind::Face && pick.pickedFaceIndex.has_value();
        return entity->kind == domain::EntityKind::GuideSurface || WorkPlaneFrameOf(pick.entityId).has_value();
    }, [this](std::optional<app::PickCandidate> pick) {
        extrudeDock_->SetStopPicking(false);
        if (!pick) { SetStatus(QStringLiteral("終端の選択をやめました。元の入力は保持しています。")); return; }
        const auto* entity = session_->GetDocument().FindEntity(pick->entityId);
        if (!entity) return;
        ExtrudeTargetChoice choice{pick->entityId, QString::fromStdString(entity->displayName)};
        if (entity->kind == domain::EntityKind::Part) {
            const auto shape = partShapes_.find(pick->entityId.ToString());
            if (shape == partShapes_.end() || !pick->pickedFaceIndex) return;
            const auto face = kernel::OutputFace(shape->second, *pick->pickedFaceIndex);
            if (!face.HasValue()) { ReportDiagnostics(face.Diagnostics()); return; }
            const auto brep = kernel::CaptureOutputShape(face.Value());
            kernel::ReleaseShape(face.Value());
            if (!brep.HasValue()) { ReportDiagnostics(brep.Diagnostics()); return; }
            choice.faceBrep = brep.Value();
            choice.labelJa += QStringLiteral("：フェイス%1（選択時の形を保持）").arg(*pick->pickedFaceIndex + 1);
        }
        extrudeDock_->PickStop(choice);
        SetStatus(QStringLiteral("終端を設定しました。＋は奥、−は手前へ終端をずらします。"));
    });
    SetStatus(QStringLiteral("終端の面を選択中：3Dのフェイス／面、または左ペインの面／作業平面を選んでください。"));
}
