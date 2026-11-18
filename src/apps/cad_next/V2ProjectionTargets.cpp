#include "V2ProjectionTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ShapeLifetime.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include <QComboBox>
#include <QLabel>
#include <QPointer>
#include <QString>
#include <algorithm>
#include <cmath>

using namespace kachakacha::v2;
namespace {
base::Result<modeling::KernelShapeHandle> PlanePatch(const modeling::WorkPlaneFrame& plane,
    double left, double right, double bottom, double top, const geometry::GeometryTolerance& tolerance)
{
    using Out = base::Result<modeling::KernelShapeHandle>;
    const geometry::Vector3 corners[] = {{left, bottom, 0}, {right, bottom, 0}, {right, top, 0}, {left, top, 0}};
    modeling::GuideChain boundary; boundary.role = modeling::ChainRole::OuterBoundary; boundary.closed = true;
    for (int i = 0; i < 4; ++i) {
        const auto line = geometry::CurveSegment::MakeLine(corners[i], corners[(i + 1) % 4]);
        if (!line.HasValue()) return Out::Failure(line.Diagnostics());
        boundary.segments.push_back(line.Value());
    }
    modeling::GuideSurfaceRequest request; request.method = modeling::GuideSurfaceMethod::PlanarBoundary;
    request.chains = {boundary};
    const auto analysis = modeling::AnalyzeGuideSurfaceRequest(request, tolerance);
    if (!analysis.HasValue()) return Out::Failure(analysis.Diagnostics());
    const auto flat = kernel::BuildGuideSurface(request, analysis.Value(), tolerance);
    if (!flat.HasValue()) return Out::Failure(flat.Diagnostics());
    v2ui::ShapeListOwner owner({flat.Value().handle});
    geometry::OutputPlacement placement; placement.keepPosition = false;
    placement.destination = {plane.origin, plane.normal, plane.uAxis};
    return kernel::PlaceOutputShape(flat.Value().handle, placement);
}

geometry::Bounds3 CurveHull(const geometry::CurveSegment& curve)
{
    geometry::Bounds3 hull;
    if (curve.Kind() == geometry::CurveKind::Circle || curve.Kind() == geometry::CurveKind::CircularArc) {
        const geometry::Vector3 radius{curve.Radius(), curve.Radius(), curve.Radius()};
        hull.Add(curve.Center() - radius); hull.Add(curve.Center() + radius);
    } else for (const auto& point : curve.ControlPoints()) hull.Add(point);
    return hull;
}
}

base::Result<modeling::KernelShapeHandle> V2ProjectionTool::TargetShape(const app::SelectionRef& ref) const
{
    using Out = base::Result<modeling::KernelShapeHandle>;
    const auto fail = [](const char* detail) { return Out::Failure(base::MakeError(
        "UI-PRT1", "投影先を選べません。", detail)); };
    const auto* entity = window_.Session().GetDocument().FindEntity(ref.entityId);
    if (!entity) return fail("対象がありません。選び直してください。");
    if (entity->kind == domain::EntityKind::WorkPlane) {
        const auto plane = window_.WorkPlaneFrameOf(ref.entityId);
        if (!plane) return fail("作業面の位置・向きを確認してください。");
        return PlanePatch(*plane, -1, 1, -1, 1, window_.Session().GetDocument().Snapshot().settings.tolerance);
    }
    const auto& shapes = entity->kind == domain::EntityKind::Part ? window_.partShapes_ : window_.guideShapes_;
    const auto source = shapes.find(ref.entityId.ToString());
    if (source == shapes.end()) return fail("投影先にはサーフェス・ソリッドのフェイス・作業面を指定してください。");
    if (ref.pickedFaceIndex) return kernel::OutputFace(source->second, *ref.pickedFaceIndex);
    if (entity->kind == domain::EntityKind::Part) return fail("立体の投影先フェイスを3Dビューで選んでください。");
    const auto bytes = kernel::CaptureOutputShape(source->second);
    if (!bytes.HasValue()) return Out::Failure(bytes.Diagnostics());
    return kernel::RestoreOutputShape(bytes.Value());
}

bool V2ProjectionTool::GatherProjectionTargets(const kernel::WireProjectionOptions& options,
    std::vector<modeling::KernelShapeHandle>& faces, std::vector<modeling::KernelShapeHandle>& owned)
{
    for (const auto& target : targets_) {
        const auto plane = window_.WorkPlaneFrameOf(target.ref.entityId);
        if (!plane) { faces.push_back(target.shape); continue; }
        geometry::Bounds3 range;
        const auto sourceU = geometry::Normalized(options.sourceU);
        const auto sourceV = geometry::Normalized(options.sourceV - sourceU * geometry::Dot(options.sourceV, sourceU));
        const auto placed = options.targetPoint - plane->normal * plane->SignedDistance(options.targetPoint);
        for (const auto& source : sources_) for (const auto& curve : source.input.curves) {
            const auto hull = CurveHull(curve);
            const auto direction = options.mode == kernel::WireProjectionMode::Closest
                ? plane->normal : geometry::Normalized(source.input.direction);
            const auto denominator = geometry::Dot(direction, plane->normal);
            for (int i = 0; i < 8; ++i) {
                auto point = geometry::Vector3{i & 1 ? hull.maximum.x : hull.minimum.x,
                    i & 2 ? hull.maximum.y : hull.minimum.y, i & 4 ? hull.maximum.z : hull.minimum.z};
                if (options.mode == kernel::WireProjectionMode::Wrap) {
                    const auto delta = point - options.sourceOrigin;
                    const double x = geometry::Dot(delta, sourceU), y = geometry::Dot(delta, sourceV);
                    const double c = std::cos(options.wrapRotationRad), s = std::sin(options.wrapRotationRad);
                    point = placed + plane->uAxis * (c * x - s * y) + plane->vAxis * (s * x + c * y);
                } else if (std::abs(denominator) > 1e-10) {
                    point = point - direction * (plane->SignedDistance(point) / denominator);
                }
                range.Add({plane->CoordinateU(point), plane->CoordinateV(point), 0});
            }
        }
        const auto margin = std::max(1., range.DiagonalLength() * .05);
        const auto shape = PlanePatch(*plane, range.minimum.x - margin, range.maximum.x + margin,
            range.minimum.y - margin, range.maximum.y + margin, window_.Session().GetDocument().Snapshot().settings.tolerance);
        if (!shape.HasValue()) { status_->setText(QString::fromStdString(shape.FirstMessageJa())); return false; }
        owned.push_back(shape.Value()); faces.push_back(shape.Value());
    }
    return true;
}

void V2ProjectionTool::BeginTargetPick()
{
    const QPointer<V2ProjectionTool> self(this);
    window_.viewport_->BeginElementPick([self](const app::PickCandidate& candidate) {
        if (!self || !self->isVisible()) return false;
        const auto* entity = self->window_.Session().GetDocument().FindEntity(candidate.entityId);
        if (!entity) return false;
        if (self->role_ == 3 && std::none_of(self->targets_.begin(), self->targets_.end(),
                [&](const auto& target) { return target.ref.entityId == candidate.entityId; })) return false;
        if (entity->kind == domain::EntityKind::WorkPlane)
            return self->role_ == 3 || self->targetKind_->currentIndex() == 1;
        if (self->role_ != 3 && self->targetKind_->currentIndex() != 0) return false;
        return (candidate.kind == app::SelectionElementKind::Face && candidate.pickedFaceIndex
            && (entity->kind == domain::EntityKind::Part || entity->kind == domain::EntityKind::GuideSurface))
            || (candidate.kind == app::SelectionElementKind::Object && entity->kind == domain::EntityKind::GuideSurface);
    }, [self](std::optional<app::PickCandidate> candidate) {
        if (!self || !self->isVisible() || !candidate) return;
        app::SelectionRef ref; ref.entityId = candidate->entityId; ref.kind = candidate->kind;
        ref.pickedFaceIndex = candidate->pickedFaceIndex; ref.hitPoint = candidate->hitPoint;
        const auto* entity = self->window_.Session().GetDocument().FindEntity(ref.entityId);
        // Preserve whole-surface projection; a solid always requires its specific face.
        if (entity && entity->kind == domain::EntityKind::GuideSurface) ref.pickedFaceIndex.reset();
        if (self->role_ == 3) {
            const auto target = std::find_if(self->targets_.begin(), self->targets_.end(), [&](const auto& value) {
                return value.ref.entityId == ref.entityId && value.ref.pickedFaceIndex == ref.pickedFaceIndex;
            });
            if (target == self->targets_.end()) {
                self->status_->setText(QStringLiteral("選択済みの投影先をクリックしてください。"));
            } else { self->targetPoint_ = ref.hitPoint; self->Invalidate(); }
        } else self->AddTarget(ref);
        self->BeginTargetPick();
    });
    window_.viewport_->setProperty("referencePickPrompt", pickPrompt_->text());
}
