#include "V2FaceActions.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ShapeLifetime.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/modeling/WorkPlane.h"
#include <QObject>
#include <QString>
#include <algorithm>
#include <vector>

using namespace kachakacha::v2;
namespace {
// Face indices and contact frames are transient input state, never document references.
struct DrawingSupport {
    app::SelectionRef face;
    modeling::WorkPlaneFrame frame;
    modeling::KernelShapeHandle shape;
    bool planar = true;
    std::vector<modeling::SnapCurve> boundaries;
};
class DrawingSupports final : public QObject {
public:
    explicit DrawingSupports(V2MainWindow& window) : QObject(&window) {}
    base::DocumentId document;
    std::vector<DrawingSupport> faces;
};
DrawingSupports* State(const V2MainWindow& window)
{
    for (auto* child : window.children())
        if (auto* state = dynamic_cast<DrawingSupports*>(child)) return state;
    return nullptr;
}
}

bool V2FaceActions::RememberDrawingSupport(V2MainWindow& window, const app::SelectionRef& ref,
    const app::WorkPlaneChoice& choice, bool planar)
{
    const auto shape = DrawingShape(window, ref.entityId);
    if (!shape || !ref.pickedFaceIndex) return false;
    const auto request = app::BuildWorkPlaneRequest(choice, {});
    if (!request.HasValue()) { window.ReportDiagnostics(request.Diagnostics()); return false; }
    const auto built = modeling::BuildWorkPlane(request.Value(), window.session_->GetDocument().Snapshot().settings.tolerance);
    if (!built.HasValue()) { window.ReportDiagnostics(built.Diagnostics()); return false; }
    const auto face = kernel::OutputFace(*shape, *ref.pickedFaceIndex);
    if (!face.HasValue()) { window.ReportDiagnostics(face.Diagnostics()); return false; }
    const v2ui::ShapeListOwner owner({face.Value()});
    const auto edges = kernel::OutputEdges(face.Value(), window.session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm);
    if (!edges.HasValue()) { window.ReportDiagnostics(edges.Diagnostics()); return false; }
    auto* state = State(window);
    if (!state) state = new DrawingSupports(window);
    const auto document = window.session_->GetDocument().Snapshot().id;
    if (state->document != document) { state->faces.clear(); state->document = document; }
    // One last-picked face per shape avoids listing every face of complex solids.
    std::erase_if(state->faces, [&](const auto& face) { return face.face.entityId == ref.entityId; });
    DrawingSupport support{ref, built.Value(), *shape, planar, {}};
    for (const auto& edge : edges.Value())
        support.boundaries.push_back({ref.entityId, window.ids_->NextTyped<base::IdKind::Segment>(), edge});
    state->faces.push_back(std::move(support));
    return ActivateDrawingSupport(window, ref.entityId);
}

bool V2FaceActions::IsDrawingSupport(const V2MainWindow& window, base::EntityId id)
{
    const auto* state = State(window);
    return state && window.activeWorkPlaneId_ == id
        && state->document == window.session_->GetDocument().Snapshot().id
        && std::any_of(state->faces.begin(), state->faces.end(), [&](const auto& face) { return face.face.entityId == id; });
}

std::optional<app::SelectionRef> V2FaceActions::ActiveDrawingFace(const V2MainWindow& window)
{
    const auto* state = State(window);
    if (!state || state->document != window.session_->GetDocument().Snapshot().id) return std::nullopt;
    for (const auto& face : state->faces) if (IsDrawingSupport(window, face.face.entityId)) {
        const auto shape = DrawingShape(window, face.face.entityId);
        if (shape && shape->value == face.shape.value) return face.face;
    }
    return std::nullopt;
}

bool V2FaceActions::ActivateDrawingSupport(V2MainWindow& window, base::EntityId id)
{
    const auto* state = State(window);
    if (!state || state->document != window.session_->GetDocument().Snapshot().id) return false;
    for (const auto& support : state->faces) if (support.face.entityId == id) {
        const auto shape = DrawingShape(window, id);
        if (!shape || shape->value != support.shape.value) return false;
        const auto frame = support.frame;
        // A different face on the same solid also invalidates unfinished planar input.
        const bool restart = window.session_->HasPlacedPoints();
        if (restart) window.session_->CancelTool();
        window.RefreshPartEdges();
        window.ApplyWorkPlane(frame, id);
        if (restart) window.viewport_->OnToolChanged();
        window.RefreshWorkPlaneDock();
        return true;
    }
    return false;
}

void V2FaceActions::AppendDrawingSupportEdges(V2MainWindow& window, modeling::SnapScene& scene)
{
    const auto* state = State(window);
    if (!state || state->document != window.session_->GetDocument().Snapshot().id) return;
    const auto same = [](const geometry::CurveSegment& a, const geometry::CurveSegment& b) {
        if (a.Kind() != b.Kind()) return false;
        if (a.Kind() == geometry::CurveKind::Line)
            return (a.StartPoint() == b.StartPoint() && a.EndPoint() == b.EndPoint())
                || (a.StartPoint() == b.EndPoint() && a.EndPoint() == b.StartPoint());
        return a.ControlPoints() == b.ControlPoints() && a.Center() == b.Center() && a.Normal() == b.Normal()
            && a.ReferenceDirection() == b.ReferenceDirection() && a.Radius() == b.Radius()
            && a.StartAngleRad() == b.StartAngleRad() && a.SweepAngleRad() == b.SweepAngleRad();
    };
    for (const auto& support : state->faces) {
        const auto& document = window.session_->GetDocument();
        const auto* entity = document.FindEntity(support.face.entityId);
        const auto* feature = entity ? document.FindFeature(entity->createdBy) : nullptr;
        if (!entity || !feature || !feature->enabled || !app::EntityEffectivelyVisible(document.Snapshot(), *entity)) continue;
        const auto shape = DrawingShape(window, support.face.entityId);
        if (!shape || shape->value != support.shape.value) continue;
        // The guide's manufacturing outline is deliberately outer-only. Keep it
        // unchanged and supplement the input scene with this face's native inner edges.
        for (const auto& boundary : support.boundaries)
            if (std::none_of(scene.curves.begin(), scene.curves.end(), [&](const auto& existing) {
                    return existing.entityId == boundary.entityId && same(existing.segment, boundary.segment);
                })) scene.curves.push_back(boundary);
    }
}

void V2FaceActions::AppendDrawingSupports(V2MainWindow& window,
    std::vector<std::pair<base::EntityId, QString>>& planes)
{
    auto* state = State(window);
    if (state) {
        if (state->document != window.session_->GetDocument().Snapshot().id) state->faces.clear();
        std::erase_if(state->faces, [&](const auto& support) {
            const auto shape = DrawingShape(window, support.face.entityId);
            return !shape || !window.session_->GetDocument().FindEntity(support.face.entityId)
                || shape->value != support.shape.value;
        });
        for (const auto& support : state->faces) {
            const auto* entity = window.session_->GetDocument().FindEntity(support.face.entityId);
            const auto label = support.planar ? QStringLiteral("面: %1 [%2]") : QStringLiteral("接平面: %1 [%2]");
            planes.emplace_back(entity->id, label.arg(QString::fromStdString(entity->displayName))
                .arg(*support.face.pickedFaceIndex + 1));
        }
    }
    if (std::none_of(planes.begin(), planes.end(), [&](const auto& plane) { return plane.first == window.activeWorkPlaneId_; })) {
        const auto top = app::OriginPlaneId(window.session_->GetDocument().Snapshot(), modeling::StandardPlaneKind::XY);
        if (top) {
            if (window.session_->HasPlacedPoints()) { window.session_->CancelTool(); window.viewport_->OnToolChanged(); }
            window.ApplyWorkPlane(modeling::StandardPlane(modeling::StandardPlaneKind::XY), *top);
        }
    }
    planes.emplace_back(base::EntityId{}, QStringLiteral("3Dビューで面を指定…"));
}
