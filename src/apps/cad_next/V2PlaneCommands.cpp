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
#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/modeling/GridModel.h"
#include "kachakacha/modeling/WorkPlane.h"
#include "kachakacha/view/ViewOrientation.h"

#include <QComboBox>

#include <string>
#include <utility>
#include <vector>

namespace {

using kachakacha::v2::modeling::StandardPlaneKind;

//! 標準面の順ぐり。棚の初期値を、作るたびに XY → YZ → ZX と回す。
[[nodiscard]] StandardPlaneKind NextStandardPlane(StandardPlaneKind current)
{
    switch (current) {
    case StandardPlaneKind::XY: return StandardPlaneKind::YZ;
    case StandardPlaneKind::YZ: return StandardPlaneKind::ZX;
    case StandardPlaneKind::ZX: return StandardPlaneKind::XY;
    }
    return StandardPlaneKind::XY;
}

} // namespace

bool V2MainWindow::IsPlaneCommand(std::string_view id)
{
    return id == "workplane.create" || id == "workplane.set_active" || id == "grid.edit"
        || id == "grid.move_origin" || id == "view.align_workplane";
}

void V2MainWindow::RunPlaneCommand(std::string_view id)
{
    if (id == "workplane.create") {
        RunWorkPlaneCreate();
        return;
    }
    if (id == "workplane.set_active") {
        ActivateSelectedWorkPlane();
        return;
    }
    if (id == "view.align_workplane") {
        AlignViewToActiveWorkPlane();
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

void V2MainWindow::RunWorkPlaneCreate()
{
    using kachakacha::v2::modeling::WorkPlaneMethod;
    // 作り方は12通りある。標準面しか作れないと、原点を通らない平面
    // ── station ごとの断面 ── が置けない。
    WorkPlaneChoice choice;
    choice.method = WorkPlaneMethod::Standard;
    choice.standard = NextStandardPlane(nextStandardPlane_);
    if (workPlaneChooser_) {
        // 試験の道。棚を開かず、答えをもらって作る。
        const auto answered = workPlaneChooser_(choice, BuildWorkPlaneFacts());
        if (!answered.has_value()) {
            SetStatus(QStringLiteral("作業平面: やめました。"));
            return;
        }
        CreateWorkPlaneFromChoice(*answered, true);
        return;
    }
    // 本体の道。V1 と同じく右の棚で作り方と数を決め、「平面を作る」で作る。
    if (workPlaneDock_ == nullptr) {
        SetStatus(QStringLiteral("作業平面の棚がありません。"));
        return;
    }
    if (workPlaneDock_->Choice().method == WorkPlaneMethod::Standard) {
        WorkPlaneChoice shown = workPlaneDock_->Choice();
        shown.standard = choice.standard;
        workPlaneDock_->SetChoice(shown);
    }
    RefreshWorkPlaneDock();
    workPlaneDock_->show();
    workPlaneDock_->raise();
    SetStatus(QStringLiteral(
        "作業平面: 右の「作業平面」で作り方と数を決め、「平面を作る」を押してください。"));
}

void V2MainWindow::CreateWorkPlaneFromDock()
{
    if (workPlaneDock_ == nullptr) {
        return;
    }
    CreateWorkPlaneFromChoice(workPlaneDock_->Choice(), workPlaneDock_->ActivateAfterCreate());
}

void V2MainWindow::CreateWorkPlaneFromChoice(const WorkPlaneChoice& choice, bool activate)
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

    // 足りるか、数の欄で代えられるかは core が決める。ここは材料を集めて渡すだけ。
    const auto request = kachakacha::v2::app::BuildWorkPlaneRequest(choice,
        CollectWorkPlaneMaterials(choice));
    if (!request.HasValue()) {
        ReportDiagnostics(request.Diagnostics());
        return;
    }
    const auto built = BuildWorkPlane(request.Value(),
        session_->GetDocument().Snapshot().settings.tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    if (choice.method == WorkPlaneMethod::Standard) {
        nextStandardPlane_ = choice.standard;
    }
    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::CreateWorkPlane;
    feature.displayName = kachakacha::v2::app::WorkPlaneDisplayName(choice);
    CreateWorkPlaneDefinition definition;
    definition.method = static_cast<int>(choice.method);
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
    if (activate) {
        ApplyWorkPlane(built.Value(), entity.id);
    }
    AdoptCurrentDocument();
    SetStatus(activate
            ? QStringLiteral("%1 の作業平面を作って、作業中にしました。")
                  .arg(QString::fromStdString(feature.displayName))
            : QStringLiteral("%1 の作業平面を作りました。")
                  .arg(QString::fromStdString(feature.displayName)));
}

void V2MainWindow::RefreshWorkPlaneDock()
{
    // 文書にある平面を並べる。原点の3面は最初から選べる。
    std::vector<std::pair<kachakacha::v2::base::EntityId, QString>> planes;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::WorkPlane) {
            planes.emplace_back(entity.id, QString::fromStdString(entity.displayName));
        }
    }
    if (workPlaneDock_ == nullptr) {
        return;
    }
    workPlaneDock_->SetPlanes(planes);
    workPlaneDock_->Refresh(BuildWorkPlaneFacts());
    // 上の帯の「作図面」コンボも同じ一覧。作業中のものを選んだ状態にする。
    if (planeCombo_ == nullptr) {
        return;
    }
    refreshingPlaneCombo_ = true;
    planeCombo_->clear();
    planeComboIds_.clear();
    int current = -1;
    for (const auto& [id, name] : planes) {
        if (id == activeWorkPlaneId_) {
            current = static_cast<int>(planeComboIds_.size());
        }
        planeComboIds_.push_back(id);
        planeCombo_->addItem(name);
    }
    planeCombo_->setCurrentIndex(current);
    refreshingPlaneCombo_ = false;
}

void V2MainWindow::ActivateSelectedWorkPlane()
{
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
    if (!ActivateWorkPlaneById(chosen)) {
        SetStatus(QStringLiteral("その作業平面の作り方が分かりませんでした。"));
    }
}

bool V2MainWindow::ActivateWorkPlaneById(const kachakacha::v2::base::EntityId& id)
{
    // 作り方ではなく、出来上がった枠(原点・法線・u)を使う。v は法線と u から出す。
    const auto frame = WorkPlaneFrameOf(id);
    const auto* entity = session_->GetDocument().FindEntity(id);
    if (!frame.has_value() || entity == nullptr) {
        return false;
    }
    ApplyWorkPlane(*frame, id);
    RefreshWorkPlaneDock();
    SetStatus(QStringLiteral("%1 を作業中の平面にしました。")
            .arg(QString::fromStdString(entity->displayName)));
    return true;
}

void V2MainWindow::AlignViewToActiveWorkPlane()
{
    // 上の帯の「正対」。V1 と同じく、作業中の作図面の正面から見る。形は変わらない。
    const auto* entity = session_->GetDocument().FindEntity(activeWorkPlaneId_);
    if (entity == nullptr) {
        SetStatus(QStringLiteral("正対: 作業中の作図面がありません。"));
        return;
    }
    const auto orientation = kachakacha::v2::view::OrientationFacing(
        viewport_->WorkPlane().normal, viewport_->WorkPlane().vAxis);
    if (!orientation.HasValue()) {
        ReportDiagnostics(orientation.Diagnostics());
        return;
    }
    viewport_->SetOrientation(orientation.Value());
    SetStatus(QStringLiteral("%1 に正対しました。形は変わっていません。")
            .arg(QString::fromStdString(entity->displayName)));
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

void V2MainWindow::SetWorkPlaneChooser(
    std::function<std::optional<WorkPlaneChoice>(const WorkPlaneChoice&,
        const kachakacha::v2::app::WorkPlaneFacts&)>
        chooser)
{
    workPlaneChooser_ = std::move(chooser);
}

kachakacha::v2::app::WorkPlaneFacts V2MainWindow::BuildWorkPlaneFacts() const
{
    kachakacha::v2::app::WorkPlaneFacts facts;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == kachakacha::v2::domain::EntityKind::WorkPlane) {
            ++facts.planes;
        } else if (entity->kind == kachakacha::v2::domain::EntityKind::Point) {
            ++facts.points;
        } else if (entity->kind == kachakacha::v2::domain::EntityKind::Wire) {
            ++facts.edges;
        }
    }
    return facts;
}

kachakacha::v2::app::WorkPlaneMaterials V2MainWindow::CollectWorkPlaneMaterials(
    const WorkPlaneChoice& choice) const
{
    kachakacha::v2::app::WorkPlaneMaterials materials;
    // 材料は「いま選んでいるもの」から取る。選んだ順を保つ。
    // 順を変えると、3点で作る平面の向きが変わってしまう。
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == kachakacha::v2::domain::EntityKind::WorkPlane) {
            if (const auto frame = WorkPlaneFrameOf(id); frame.has_value()) {
                materials.selectedPlanes.push_back(*frame);
            }
            continue;
        }
        if (entity->kind == kachakacha::v2::domain::EntityKind::Point) {
            for (const auto& point : session_->Scene().points) {
                if (point.entityId == id) {
                    materials.points.push_back(point.position);
                }
            }
            continue;
        }
        if (entity->kind == kachakacha::v2::domain::EntityKind::Wire) {
            for (const auto& curve : session_->Scene().curves) {
                if (curve.entityId == id) {
                    materials.edges.push_back(curve.segment);
                }
            }
        }
    }
    // コンボで決めた平面。選択より優先するかは core が決める。
    if (choice.referencePlaneId.has_value()) {
        materials.referencePlane = WorkPlaneFrameOf(*choice.referencePlaneId);
    }
    if (choice.secondPlaneId.has_value()) {
        materials.secondPlane = WorkPlaneFrameOf(*choice.secondPlaneId);
    }
    return materials;
}

int V2MainWindow::PlaneComboCount() const
{
    return planeCombo_ == nullptr ? 0 : planeCombo_->count();
}

QString V2MainWindow::PlaneComboText(int index) const
{
    if (planeCombo_ == nullptr || index < 0 || index >= planeCombo_->count()) {
        return QString();
    }
    return planeCombo_->itemText(index);
}

int V2MainWindow::PlaneComboCurrent() const
{
    return planeCombo_ == nullptr ? -1 : planeCombo_->currentIndex();
}

void V2MainWindow::SelectPlaneCombo(int index)
{
    if (planeCombo_ != nullptr) {
        planeCombo_->setCurrentIndex(index);
    }
}
