//! 基準のコマンド(V2MainWindow の一部)。作業平面とグリッド。
//!
//! 思想。**線は必ずどこかの平面の上にある。**
//! 平面を決めずに引くと、あとで「この線はどの面の上か」が誰にも分からなくなる。
//! だから平面は文書に入れる。画面の飾りではない。
//!
//! グリッドは逆で、**見え方の都合なので文書に入れない。**
//! 保存して開き直したときに、相手の画面のグリッドまで変わってしまうのは行きすぎである。

#include "V2MainWindow.h"

#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/modeling/GridModel.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <string>
#include <utility>

namespace {

using kachakacha::v2::modeling::StandardPlaneKind;

//! 標準面の順ぐり。押すたびに XY → YZ → ZX と回る。
//! 選ぶ窓が付くまでのあいだ、これで3面とも作れる。
[[nodiscard]] StandardPlaneKind NextStandardPlane(StandardPlaneKind current)
{
    switch (current) {
    case StandardPlaneKind::XY: return StandardPlaneKind::YZ;
    case StandardPlaneKind::YZ: return StandardPlaneKind::ZX;
    case StandardPlaneKind::ZX: return StandardPlaneKind::XY;
    }
    return StandardPlaneKind::XY;
}

[[nodiscard]] const char* StandardPlaneNameJa(StandardPlaneKind kind)
{
    switch (kind) {
    case StandardPlaneKind::XY: return "XY(床)";
    case StandardPlaneKind::YZ: return "YZ(側面)";
    case StandardPlaneKind::ZX: return "ZX(正面)";
    }
    return "不明";
}

} // namespace

bool V2MainWindow::IsPlaneCommand(std::string_view id)
{
    return id == "workplane.create" || id == "workplane.set_active" || id == "grid.edit"
        || id == "grid.move_origin";
}

void V2MainWindow::RunPlaneCommand(std::string_view id)
{
    if (id == "workplane.create") {
        CreateStandardWorkPlane();
        return;
    }
    if (id == "workplane.set_active") {
        ActivateSelectedWorkPlane();
        return;
    }
    if (id == "grid.edit") {
        CycleGridSpacing();
        return;
    }
    if (id == "grid.move_origin") {
        MoveGridOriginByClick();
        return;
    }
}

void V2MainWindow::CreateStandardWorkPlane()
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::CreateWorkPlaneDefinition;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;
    using kachakacha::v2::modeling::BuildWorkPlane;
    using kachakacha::v2::modeling::WorkPlaneMethod;
    using kachakacha::v2::modeling::WorkPlaneRequest;

    nextStandardPlane_ = NextStandardPlane(nextStandardPlane_);
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::Standard;
    request.standard = nextStandardPlane_;
    const auto built = BuildWorkPlane(request,
        session_->GetDocument().Snapshot().settings.tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::CreateWorkPlane;
    feature.displayName = StandardPlaneNameJa(nextStandardPlane_);
    CreateWorkPlaneDefinition definition;
    definition.method = static_cast<int>(WorkPlaneMethod::Standard);
    definition.origin = built.Value().origin;
    definition.normal = built.Value().normal;
    definition.uDirection = built.Value().uAxis;
    feature.definition = std::move(definition);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::WorkPlane;
    entity.displayName = feature.displayName;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"plane", entity.id, EntityKind::WorkPlane});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, "作業平面を作る"));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    ApplyWorkPlane(built.Value(), entity.id);
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("%1 の作業平面を作って、作業中にしました。")
            .arg(QString::fromUtf8(StandardPlaneNameJa(nextStandardPlane_))));
}

void V2MainWindow::ActivateSelectedWorkPlane()
{
    using kachakacha::v2::domain::CreateWorkPlaneDefinition;
    using kachakacha::v2::domain::EntityKind;
    // 選んでいる作業平面を作業中にする。選んでいなければ、最後に作ったものにする。
    const auto& snapshot = session_->GetDocument().Snapshot();
    const auto& selection = viewport_->Selection();
    kachakacha::v2::base::EntityId chosen;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::WorkPlane) {
            chosen = id;
        }
    }
    if (chosen.IsNil()) {
        for (const auto& entity : snapshot.entities) {
            if (entity.kind == EntityKind::WorkPlane) {
                chosen = entity.id;
            }
        }
    }
    if (chosen.IsNil()) {
        SetStatus(QStringLiteral(
            "作業平面がありません。先に「基準」→「作業平面を作る」で作ってください。"));
        return;
    }
    for (const auto& feature : snapshot.features) {
        const auto* definition = std::get_if<CreateWorkPlaneDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        bool makesChosen = false;
        for (const auto& output : feature.outputs) {
            makesChosen = makesChosen || output.entityId == chosen;
        }
        if (!makesChosen) {
            continue;
        }
        kachakacha::v2::modeling::WorkPlaneFrame frame;
        frame.origin = definition->origin;
        frame.normal = definition->normal;
        frame.uAxis = definition->uDirection;
        // v 軸は法線と u から出す。持たせると食い違うので持たせない。
        frame.vAxis = kachakacha::v2::geometry::Vector3{
            definition->normal.y * definition->uDirection.z
                - definition->normal.z * definition->uDirection.y,
            definition->normal.z * definition->uDirection.x
                - definition->normal.x * definition->uDirection.z,
            definition->normal.x * definition->uDirection.y
                - definition->normal.y * definition->uDirection.x};
        ApplyWorkPlane(frame, chosen);
        SetStatus(QStringLiteral("%1 を作業中の平面にしました。")
                .arg(QString::fromStdString(feature.displayName)));
        return;
    }
    SetStatus(QStringLiteral("その作業平面の作り方が分かりませんでした。"));
}

void V2MainWindow::ApplyWorkPlane(const kachakacha::v2::modeling::WorkPlaneFrame& frame,
    const kachakacha::v2::base::EntityId& entityId)
{
    activeWorkPlaneId_ = entityId;
    viewport_->SetWorkPlane(frame);
    auto scene = session_->Scene();
    scene.workPlane.active = true;
    scene.workPlane.origin = frame.origin;
    scene.workPlane.normal = frame.normal;
    scene.grid.origin = frame.origin;
    scene.grid.uDirection = frame.uAxis;
    scene.grid.vDirection = frame.vAxis;
    session_->SetScene(std::move(scene));
    viewport_->update();
}

void V2MainWindow::CycleGridSpacing()
{
    using kachakacha::v2::modeling::GridDefinition;
    using kachakacha::v2::modeling::SetGridSpacing;
    // 1 → 2 → 5 → 10 → 20 mm と回る。模型でよく使う刻みだけを並べる。
    const double steps[] = {1.0, 2.0, 5.0, 10.0, 20.0};
    auto scene = session_->Scene();
    double next = steps[0];
    for (std::size_t index = 0; index < std::size(steps); ++index) {
        if (scene.grid.majorSpacingMm <= steps[index] + 1.0e-9) {
            next = steps[(index + 1) % std::size(steps)];
            break;
        }
    }
    // 場面のグリッドは画面用の形なので、core の形へ写してから頼む。
    // 検査は core にあるので、ここで刻みが正かどうかは見ない。
    GridDefinition definition;
    definition.visible = scene.grid.visible;
    definition.majorSpacingMm = scene.grid.majorSpacingMm;
    definition.subdivision = scene.grid.subdivision;
    const auto changed = SetGridSpacing(definition, next, definition.subdivision);
    if (!changed.HasValue()) {
        ReportDiagnostics(changed.Diagnostics());
        return;
    }
    scene.grid.majorSpacingMm = changed.Value().majorSpacingMm;
    scene.grid.subdivision = changed.Value().subdivision;
    session_->SetScene(std::move(scene));
    viewport_->update();
    SetStatus(QStringLiteral("グリッドの間隔を %1 mm にしました(副点は 1/%2)。")
            .arg(next)
            .arg(scene.grid.subdivision));
}

void V2MainWindow::MoveGridOriginByClick()
{
    // どこへ動かすかは、押した場所で決まる。選択では決まらない。
    // だから1回だけ押す場所を聞く。聞いていることは帯に出る。
    viewport_->BeginPointPick(
        [this](const V2Viewport::PickedPoint& picked) {
            auto scene = session_->Scene();
            const auto& plane = viewport_->WorkPlane();
            // 場面のグリッドは画面用の形なので、core の形へ写してから頼む。
            kachakacha::v2::modeling::GridDefinition definition;
            definition.visible = scene.grid.visible;
            definition.majorSpacingMm = scene.grid.majorSpacingMm;
            definition.subdivision = scene.grid.subdivision;
            const auto moved = kachakacha::v2::modeling::MoveGridOrigin(definition, plane,
                picked.point);
            if (!moved.HasValue()) {
                ReportDiagnostics(moved.Diagnostics());
                return;
            }
            scene.grid.origin = plane.PointAt(moved.Value().originUmm,
                moved.Value().originVmm);
            session_->SetScene(std::move(scene));
            viewport_->update();
            SetStatus(QStringLiteral("グリッド原点: 作業平面の上の (%1, %2) mm へ動かしました。")
                    .arg(moved.Value().originUmm, 0, 'f', 3)
                    .arg(moved.Value().originVmm, 0, 'f', 3));
        },
        "グリッド原点: 動かす先を1回押してください(Esc でやめます)。");
}
