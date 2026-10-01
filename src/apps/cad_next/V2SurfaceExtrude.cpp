#include "V2MainWindow.h"
#include "V2ExtrudeDock.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/app/ExtrudeDrag.h"
#include <QString>
#include <utility>

namespace {
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::domain::ExtrudeDefinition;

std::pair<double, double> Offsets(int mode, double distance, double second)
{
    const auto extent = static_cast<ExtrudeExtentMode>(mode);
    if (extent == ExtrudeExtentMode::SymmetricDistance) return {-distance * 0.5, distance * 0.5};
    if (extent == ExtrudeExtentMode::TwoDistances) return {-second, distance};
    return {0.0, distance};
}

void ShowBuilt(V2Viewport& viewport, const kachakacha::v2::kernel::ExtrudeBuildResult& built,
    const kachakacha::v2::app::ExtrudeHandle& handle)
{
    std::vector<std::vector<Vector3>> edges, faces;
    for (const auto& part : built.parts) {
        const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(part.handle);
        if (!mesh.HasValue()) continue;
        edges.insert(edges.end(), mesh.Value().edges.begin(), mesh.Value().edges.end());
        for (const auto& triangle : mesh.Value().triangles) {
            faces.push_back({triangle.points[0], triangle.points[1], triangle.points[2]});
        }
    }
    viewport.ShowExtrudeHandle(handle, std::move(edges));
    viewport.SetExtrudePreviewFaces(std::move(faces));
}
} // namespace

bool V2MainWindow::HandleSurfaceExtrude(bool confirm)
{
    const auto plan = extrudeSnapshot_.has_value() ? extrudeSnapshot_->plan : PlanExtrudeFromSelection();
    if (plan.kind != kachakacha::v2::app::ExtrudeInputKind::FaceOnly || plan.profiles.size() != 1) return false;
    const auto found = guideShapes_.find(plan.profiles.front().ToString());
    if (found == guideShapes_.end()) return false;
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    if (!extrudeSnapshot_.has_value()) {
        const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(found->second);
        if (!mesh.HasValue()) { ReportDiagnostics(mesh.Diagnostics()); return true; }
        const Vector3 center = (mesh.Value().minimum + mesh.Value().maximum) * 0.5;
        const auto pose = kachakacha::v2::kernel::FacePoseNear(found->second, 0, center, tolerance);
        if (!pose.HasValue()) { ReportDiagnostics(pose.Diagnostics()); return true; }
        faceNormal_ = pose.Value().normal;
        facePushPull_ = true;
        ExtrudeSnapshot snapshot;
        snapshot.plan = plan;
        extrudeSnapshot_ = snapshot;
        kachakacha::v2::app::ExtrudeHandle initial;
        initial.origin = center;
        initial.direction = ExtrudeDirectionNow();
        initial.distanceMm = ExtrudeDistanceMm();
        viewport_->ShowExtrudeHandle(initial, {});
        ShowExtrudeShelf(plan);
    }
    kachakacha::v2::app::ExtrudeHandle handle;
    handle.origin = viewport_->ExtrudeHandleOrigin();
    handle.direction = ExtrudeDirectionNow();
    handle.distanceMm = extrudeDock_->DistanceMm();
    if (!extrudeDock_->DirectionReady()) {
        viewport_->ShowExtrudeHandle(handle, {});
        viewport_->SetExtrudePreviewFaces({});
        extrudeDock_->ShowStatusLines({QStringLiteral("方向に使う直線、または0ではない方向を指定してください。")}, false);
        return true;
    }
    const auto outputs = extrudeDock_->Outputs();
    if (!outputs.body || outputs.startWire || outputs.endWire || outputs.sideWires) {
        viewport_->ShowExtrudeHandle(handle, {});
        viewport_->SetExtrudePreviewFaces({});
        extrudeDock_->ShowStatusLines({QStringLiteral("曲面の押し出しは出力を「ソリッド / 面」にしてください。独立ワイヤー出力は未対応です。")}, false);
        return true;
    }
    if (extrudeDock_->ExtentMode() == ExtrudeExtentMode::ThroughAll) {
        viewport_->ShowExtrudeHandle(handle, {});
        viewport_->SetExtrudePreviewFaces({});
        extrudeDock_->ShowStatusLines({QStringLiteral("曲面の押し出しは現在、距離・対称・両方向の距離を指定してください。")}, false);
        return true;
    }
    const auto offsets = Offsets(static_cast<int>(extrudeDock_->ExtentMode()),
        handle.distanceMm, extrudeDock_->SecondDistanceMm());
    auto choice = extrudeChoice_;
    choice.direction = kachakacha::v2::modeling::ExtrudeDirectionMode::CustomXYZ;
    choice.customDirection = handle.direction;
    choice.reversed = false;
    choice.targetEntityId = extrudeDock_->TargetEntityId();
    choice.targetParameter = extrudeDock_->TargetParameter();
    const auto built = extrudeDock_->ExtentMode() == ExtrudeExtentMode::ToTarget
        ? BuildToTarget(choice, {}, found->second)
        : kachakacha::v2::kernel::BuildSurfaceExtrude(found->second,
              handle.direction, offsets.first, offsets.second, tolerance);
    if (!built.HasValue()) {
        viewport_->ShowExtrudeHandle(handle, {});
        viewport_->SetExtrudePreviewFaces({});
        extrudeDock_->ShowStatusLines({QString::fromStdString(built.FirstSummaryJa())}, false);
        return true;
    }
    ShowBuilt(*viewport_, built.Value(), handle);
    extrudeDock_->ShowStatusLines({QStringLiteral("曲面の形を保って押し出します。体積 %1 mm3")
        .arg(built.Value().totalVolumeMm3, 0, 'f', 3)}, true);
    if (!confirm) return true;
    kachakacha::v2::document::Document::Transaction transaction(session_->GetDocument(), "面の押し出し");
    for (const auto& part : built.Value().parts) {
        ExtrudeDefinition definition;
        definition.profiles = plan.profiles;
        definition.direction = handle.direction;
        definition.distance.value = handle.distanceMm;
        definition.distance.kind = kachakacha::v2::geometry::QuantityKind::Length;
        definition.extentMode = static_cast<int>(extrudeDock_->ExtentMode());
        definition.secondDistanceMm = extrudeDock_->SecondDistanceMm();
        auto inputs = plan.profiles;
        if (extrudeDock_->ExtentMode() == ExtrudeExtentMode::ToTarget) {
            definition.extentTarget = choice.targetEntityId;
            definition.targetParameter = choice.targetParameter;
            if (choice.targetEntityId.has_value()) inputs.push_back(*choice.targetEntityId);
        }
        if (AddPartFeature(kachakacha::v2::domain::FeatureType::Extrude, definition,
                part.handle, {}, "面の押し出し", inputs).IsNil()) return true;
    }
    if (!transaction.Commit()) return true;
    EndExtrudePreview();
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("面を押し出して立体を作りました。"));
    return true;
}

bool V2MainWindow::RebuildSurfaceExtrude(const ExtrudeDefinition& definition,
    const kachakacha::v2::base::EntityId& output, std::size_t ordinal)
{
    const auto found = guideShapes_.find(definition.profiles.front().ToString());
    if (found == guideShapes_.end()) return false;
    const auto offsets = Offsets(definition.extentMode, definition.distance.value, definition.secondDistanceMm);
    kachakacha::v2::app::ExtrudeChoice choice;
    choice.direction = kachakacha::v2::modeling::ExtrudeDirectionMode::CustomXYZ;
    choice.customDirection = definition.direction;
    choice.extent = static_cast<ExtrudeExtentMode>(definition.extentMode);
    choice.targetEntityId = definition.extentTarget;
    choice.targetParameter = definition.targetParameter;
    const auto result = choice.extent == ExtrudeExtentMode::ToTarget ? BuildToTarget(choice, {}, found->second)
        : kachakacha::v2::kernel::BuildSurfaceExtrude(found->second,
              definition.direction, offsets.first, offsets.second,
              session_->GetDocument().Snapshot().settings.tolerance);
    if (!result.HasValue() || ordinal >= result.Value().parts.size()) return false;
    partShapes_[output.ToString()] = result.Value().parts[ordinal].handle;
    return true;
}
