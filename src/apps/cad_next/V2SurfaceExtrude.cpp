#include "V2ExtrudeReferenceState.h"
#include "V2ExtrudeWirePreview.h"
#include "V2Calculation.h"
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

kachakacha::v2::app::ExtrudeChoice ReadSurfaceChoice(V2ExtrudeDock& dock,
    kachakacha::v2::app::ExtrudeChoice choice, const Vector3& direction)
{
    const auto outputs = dock.Outputs();
    choice.makePart = outputs.body;
    choice.makeStartProfileWire = outputs.startWire;
    choice.makeEndProfileWire = outputs.endWire;
    choice.makeSideBoundaryWires = outputs.sideWires;
    choice.direction = kachakacha::v2::modeling::ExtrudeDirectionMode::CustomXYZ;
    choice.customDirection = direction;
    choice.reversed = false;
    dock.ReadStop(choice);
    choice.extent = dock.ExtentMode();
    choice.endCap = dock.EndCap();
    choice.distanceMm = dock.DistanceMm();
    choice.secondDistanceMm = dock.SecondDistanceMm();
    return choice;
}

bool ShowBuilt(V2Viewport& viewport, V2ExtrudeDock& dock, const kachakacha::v2::kernel::ExtrudeBuildResult& built,
    const kachakacha::v2::app::ExtrudeHandle& handle,
    std::map<std::uint64_t, kachakacha::v2::modeling::ShapeMesh>& cache,
    bool showBody, bool start, bool end, bool sides)
{
    std::vector<std::vector<Vector3>> edges, faces;
    for (const auto& part : built.parts) {
        if (cache.find(part.handle.value) == cache.end()) {
            const auto mesh = V2Calculation::Call(nullptr, "押し出しの表示を準備しています…",
                [](auto value) { return kachakacha::v2::kernel::BuildShapeMesh(value); }, part.handle);
            if (!mesh.HasValue()) {
                viewport.SetExtrudePreviewFaces({});
                dock.ShowStatusLines({QStringLiteral("表示の準備を中止したか、表示形状を作れませんでした。再プレビューしてください。")}, false);
                return false;
            }
            cache.emplace(part.handle.value, mesh.Value());
        }
        const auto& mesh = cache.at(part.handle.value);
        edges.insert(edges.end(), mesh.edges.begin(), mesh.edges.end());
        if (showBody) for (const auto& triangle : mesh.triangles) {
            faces.push_back({triangle.points[0], triangle.points[1], triangle.points[2]});
        }
    }
    if (!showBody) edges = ExtrudeWirePreview(built, start, end, sides);
    viewport.ShowExtrudeHandle(handle, std::move(edges));
    viewport.SetExtrudePreviewFaces(std::move(faces));
    return true;
}
} // namespace

bool V2MainWindow::HandleSurfaceExtrude(bool confirm)
{
    auto plan = extrudeSnapshot_.has_value() ? extrudeSnapshot_->plan : PlanExtrudeFromSelection();
    if (!PrepareSurfaceExtrudeSource(plan)) return false;
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
    if (!outputs.body && !outputs.startWire && !outputs.endWire && !outputs.sideWires) {
        viewport_->ShowExtrudeHandle(handle, {});
        viewport_->SetExtrudePreviewFaces({});
        extrudeDock_->ShowStatusLines({QStringLiteral("作るものを1つ以上選んでください。")}, false);
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
    const auto choice = ReadSurfaceChoice(*extrudeDock_, extrudeChoice_, handle.direction);
    const auto built = SurfaceExtrudePreview(choice, found->second, offsets.first, offsets.second);
    if (!built.HasValue()) {
        viewport_->ShowExtrudeHandle(handle, {});
        viewport_->SetExtrudePreviewFaces({});
        extrudeDock_->ShowStatusLines({QString::fromStdString(built.FirstSummaryJa())}, false);
        return true;
    }
    if (!ShowBuilt(*viewport_, *extrudeDock_, built.Value(), handle, extrudeSnapshot_->nativePreviewMeshes,
            outputs.body, outputs.startWire, outputs.endWire, outputs.sideWires)) return true;
    extrudeDock_->ShowStatusLines({QStringLiteral("指定した終端で押し出します。体積 %1 mm3")
        .arg(built.Value().totalVolumeMm3, 0, 'f', 3)}, true);
    if (!confirm) return true;
    // Retain the result across shelf updates, and stop selection callbacks from
    // rebuilding it with every document revision made by this transaction.
    const auto committedPreview = extrudeSnapshot_->nativePreview;
    viewport_->HideExtrudeHandle();
    bool kept = false;
    {
        kachakacha::v2::document::Document::Transaction transaction(session_->GetDocument(), "面の押し出し");
        bool ready = true;
        if (outputs.body && !faceProfileSolid_.IsNil()) {
            const auto face = CommitSurfaceExtrudeFace();
            ready = !face.IsNil();
            if (ready) plan.profiles = {face};
        }
        if (ready) {
            ExtrudeDefinition definition;
            definition.profiles = plan.profiles;
            definition.direction = handle.direction;
            definition.distance.value = handle.distanceMm;
            definition.distance.kind = kachakacha::v2::geometry::QuantityKind::Length;
            definition.extentMode = static_cast<int>(choice.extent);
            definition.secondDistanceMm = choice.secondDistanceMm;
            StoreExtrudeReference(definition, choice);
            auto inputs = plan.profiles;
            if (definition.extentTarget) inputs.push_back(*definition.extentTarget);
            kept = AdoptExtrudeResult(choice, definition, built.Value(), {}, inputs) && transaction.Commit();
        }
    }
    if (kept && outputs.body) committedPreview->parts.clear(); // Document owns adopted handles.
    EndExtrudePreview();
    AdoptCurrentDocument();
    if (kept) RefreshPartEdges();
    if (!kept) {
        RebuildKernelShapes();
        SetStatus(QStringLiteral("押し出しを確定できなかったため、変更を戻しました。"));
    }
    EndExtrudePreview(true);
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
    RestoreExtrudeReference(choice, definition);
    choice.distanceMm = definition.distance.value;
    choice.secondDistanceMm = definition.secondDistanceMm;
    const auto result = (choice.extent == ExtrudeExtentMode::ToTarget || choice.endCap.Planar()) ? BuildToTarget(choice, {}, found->second)
        : V2Calculation::Call(nullptr, "面の押し出しを計算しています…",
        [](const auto&... values) { return kachakacha::v2::kernel::BuildSurfaceExtrude(values...); }, found->second,
              definition.direction, offsets.first, offsets.second,
              session_->GetDocument().Snapshot().settings.tolerance);
    if (!result.HasValue() || ordinal >= result.Value().parts.size()) return false;
    partShapes_[output.ToString()] = result.Value().parts[ordinal].handle;
    return true;
}
