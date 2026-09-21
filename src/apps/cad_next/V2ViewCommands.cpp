//! 見え方と作業中グループのコマンド(V2MainWindow の一部)。
//!
//! ここで扱うものは、どれも **形を変えない。**
//! 正対しても、グリッドを消しても、作業中のグループを変えても、
//! 文書の中の形は1mmも動かない。動かしてしまうと、
//! 「見やすくしただけ」のつもりが寸法を変えたことになる。

#include "V2MainWindow.h"
#include "V2SurfaceEditTool.h"
#include "V2OperationPanelHost.h"

#include "V2SurfaceDock.h"

#include "V2EntityTree.h"

#include "kachakacha/app/SurfaceFacing.h"
#include "kachakacha/kernel/OcctFaceQuery.h"

#include <algorithm>

#include "kachakacha/document/Commands.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "kachakacha/app/DisplaySettings.h"
#include "kachakacha/app/EntityNaming.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/view/FacingPlan.h"
#include "kachakacha/view/ViewOrientation.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

bool V2MainWindow::IsViewCommand(std::string_view id)
{
    return id == "view.align_selection" || id == "view.align_selection_back"
        || id == "view.display_settings" || id == "view.number_settings"
        || id == "group.set_active" || id == "view.hide_selected"
        || id == "view.show_all" || id == "edit.delete"
        || id == "view.stage_all" || id == "view.stage_no_grid"
        || id == "view.stage_no_construction" || id == "view.stage_selection_only"
        || id == "entity.rename";
}

void V2MainWindow::ToggleSnap()
{
    snapEnabled_ = !snapEnabled_;
    if (QAction* action = ActionFor("snap.toggle"); action != nullptr) {
        action->setChecked(snapEnabled_);
    }
    // 吸着は「道具として切る」と「S で一時的に止める」の2つがある。
    // 画面がその両方をまとめて持つ。片方だけ見ると、S を離した瞬間に
    // 切ってあったはずの吸着が戻る。
    viewport_->SetSnapSuppressed(!snapEnabled_);
    SetStatus(snapEnabled_ ? QStringLiteral("吸着を入れました。")
                           : QStringLiteral("吸着を切りました(Sでも一時的に止められます)。"));
}

void V2MainWindow::RunViewCommand(std::string_view id)
{
    if (id == "view.display_settings") {
        // 右の「表示」の棚を前に出す(V1 の表示設定タブ)。段は Ctrl+1/2/3 で直に選ぶ。
        ShowDisplayDock();
        return;
    }
    if (id == "view.number_settings") {
        ShowNumberDock();
        return;
    }
    if (id == "view.stage_selection_only") {
        ApplyDisplayStage(kachakacha::v2::app::DisplayStage::SelectionOnly);
        return;
    }
    if (id == "view.stage_all") {
        ApplyDisplayStage(kachakacha::v2::app::DisplayStage::All);
        return;
    }
    if (id == "view.stage_no_grid") {
        ApplyDisplayStage(kachakacha::v2::app::DisplayStage::NoGrid);
        return;
    }
    if (id == "view.stage_no_construction") {
        ApplyDisplayStage(kachakacha::v2::app::DisplayStage::NoConstruction);
        return;
    }
    if (id == "entity.rename") {
        BeginRenameSelected();
        return;
    }
    if (id == "view.align_selection" || id == "view.align_selection_back") {
        // 「反対側から正対」は、同じ道を裏側から通るだけである。
        // 別の道にすると、真ん中・大きさ・選択の残し方が食い違う。
        facingFromBehind_ = id == "view.align_selection_back";
        AlignViewToSelection();
        facingFromBehind_ = false;
        return;
    }
    if (id == "group.set_active") {
        ActivateSelectedGroup();
        return;
    }
    if (id == "view.hide_selected") {
        HideSelected();
        return;
    }
    if (id == "view.show_all") {
        ShowAllEntities();
        return;
    }
    if (id == "edit.delete") {
        DeleteSelected();
        return;
    }
}

void V2MainWindow::HideSelected()
{
    // 隠すだけ。形は消さない。消してしまうと、隠したつもりが元に戻せなくなる。
    const auto& selection = viewport_->Selection();
    if (selection.entityIds.empty()) {
        SetStatus(QStringLiteral("選択を隠す: 先に選んでください。"));
        return;
    }
    const auto hidden = session_->GetDocument().Run(
        kachakacha::v2::document::SetVisibilityCommand(selection.entityIds,
            kachakacha::v2::domain::Visibility::Hidden));
    if (!hidden.committed) {
        ReportDiagnostics(hidden.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("%1個を隠しました(Ctrl+Shift+H で全部出せます)。")
            .arg(static_cast<int>(selection.entityIds.size())));
}

void V2MainWindow::ShowAllEntities()
{
    std::vector<kachakacha::v2::base::EntityId> hidden;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.visibility == kachakacha::v2::domain::Visibility::Hidden) {
            hidden.push_back(entity.id);
        }
    }
    if (hidden.empty()) {
        SetStatus(QStringLiteral("すべて表示: 隠しているものはありません。"));
        return;
    }
    const auto shown = session_->GetDocument().Run(
        kachakacha::v2::document::SetVisibilityCommand(hidden,
            kachakacha::v2::domain::Visibility::Visible));
    if (!shown.committed) {
        ReportDiagnostics(shown.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("%1個を出しました。").arg(static_cast<int>(hidden.size())));
}

void V2MainWindow::DeleteSelected()
{
    using kachakacha::v2::document::RemoveFeatureCommand;
    using kachakacha::v2::document::RemovePolicy;
    const auto selection = viewport_->Selection();
    if (selection.entityIds.empty()) {
        SetStatus(QStringLiteral("削除: 先に選んでください。"));
        return;
    }
    int removed = 0;
    int refused = 0;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        // 下流があるものは消さない。消すと、そこから作った物の作り方が消える。
        const auto result = session_->GetDocument().Run(RemoveFeatureCommand(
            entity->createdBy, RemovePolicy::RefuseIfUsed, "削除"));
        if (result.committed) {
            ++removed;
        } else {
            ++refused;
        }
    }
    viewport_->PruneSelection();
    AdoptCurrentDocument();
    if (removed == 0) {
        SetStatus(QStringLiteral(
            "削除: どれも消せませんでした。ここから作った物があるためです"
            "(隠すなら Ctrl+H)。"));
        return;
    }
    SetStatus(refused == 0
            ? QStringLiteral("%1個を消しました。").arg(removed)
            : QStringLiteral("%1個を消しました。%2個は、ここから作った物があるので残しました。")
                  .arg(removed)
                  .arg(refused));
}

void V2MainWindow::AdoptTreeSelection()
{
    // 左の一覧で選んだものを、3D 画面の選択にする(V1 の modelTree の itemSelectionChanged)。
    if (entityTree_ == nullptr || viewport_ == nullptr || syncingSelection_) {
        return;
    }
    const auto idOf = [this](QTreeWidgetItem* item) {
        for (const auto& [row, id] : entityItems_) {
            if (row == item) {
                return id;
            }
        }
        return kachakacha::v2::base::EntityId{};
    };
    kachakacha::v2::app::SelectionSet next;
    const auto add = [&next](const kachakacha::v2::base::EntityId& id) {
        if (id.IsNil()) {
            return;
        }
        for (const auto& already : next.entityIds) {
            if (already == id) {
                return;
            }
        }
        next.entityIds.push_back(id);
    };
    for (QTreeWidgetItem* item : entityTree_->selectedItems()) {
        if (item == nullptr) {
            continue;
        }
        const auto id = idOf(item);
        if (!id.IsNil()) {
            add(id);
            continue;
        }
        // グループや「原点」の見出しを選んだら、その下のもの全部へ広げる(V1 と同じ)。
        for (int child = 0; child < item->childCount(); ++child) {
            add(idOf(item->child(child)));
        }
    }
    syncingSelection_ = true;
    viewport_->SetSelection(next);
    syncingSelection_ = false;
    // 選択が変わったあとの後始末は、3D 画面で選んだときと同じ道を通す。
    RefreshExportCounts();
    RefreshMeasurements();
    RefreshEditDock();
    RefreshCornerDock();
    RefreshFabricationDock();
    RefreshWorkPlaneDock();
    RefreshCommandVisibility();
    viewport_->update();
}

void V2MainWindow::HighlightTreeForSelection()
{
    // 3D 画面で選んだものを、左の一覧でも光らせる(V1 の UpdateSelections の updateTree)。
    if (entityTree_ == nullptr || viewport_ == nullptr || syncingSelection_) {
        return;
    }
    const auto& selected = viewport_->Selection().entityIds;
    syncingSelection_ = true;
    const bool blocked = entityTree_->blockSignals(true);
    entityTree_->clearSelection();
    QTreeWidgetItem* last = nullptr;
    for (const auto& [item, id] : entityItems_) {
        const bool wanted = std::any_of(selected.begin(), selected.end(),
            [&id](const kachakacha::v2::base::EntityId& picked) { return picked == id; });
        if (wanted && item != nullptr) {
            item->setSelected(true);
            last = item;
        }
    }
    if (last != nullptr) {
        // 最後に選んだものが見えるところまで送る。選んだのに画面外では気づけない。
        entityTree_->scrollToItem(last);
    }
    entityTree_->blockSignals(blocked);
    syncingSelection_ = false;
}

namespace {

//! 線を標本化する。直線は両端だけ、曲線は V1 と同じ 64 分割。
void SampleCurve(const kachakacha::v2::geometry::CurveSegment& segment,
    std::vector<kachakacha::v2::geometry::Vector3>& into)
{
    const int samples = segment.Kind() == kachakacha::v2::geometry::CurveKind::Line ? 1 : 64;
    for (int index = 0; index <= samples; ++index) {
        into.push_back(segment.Evaluate(static_cast<double>(index)
            / static_cast<double>(samples)));
    }
}

} // namespace

//! 選んだものから、正対に使う点と向きを集める。
//! 作業平面は四隅と法線、線は標本点、点はその位置。
//! 線がある作業平面の上に全部載っていれば、その面の向きを使う(V1 と同じ)。
//! 向きを持つ相手が見つかった。**最初の1つだけが向きを決める。**
//!
//! 最後に選んだものが黙って上書きする作りだと、2枚選んだときに
//! どちらの向きになるかが人には分からない。ここは先着で決め、
//! 向きの違うものが混じっていたことは帯で伝える。
void V2MainWindow::NoteFacingDirection(FacingTarget& target,
    const kachakacha::v2::geometry::Vector3& normal,
    const kachakacha::v2::geometry::Vector3& uAxis)
{
    using kachakacha::v2::geometry::Dot;
    using kachakacha::v2::geometry::Normalized;
    if (!target.normal.has_value()) {
        target.normal = normal;
        target.uAxis = uAxis;
        return;
    }
    // だいたい同じ向き(裏表は問わない)なら、混ざっているとは言わない。
    const double alignment = std::abs(Dot(Normalized(*target.normal), Normalized(normal)));
    if (alignment < 0.999) {
        target.mixedDirections = true;
    }
}

void V2MainWindow::CollectFacingTarget(FacingTarget& target) const
{
    using kachakacha::v2::domain::EntityKind;
    const auto& document = session_->GetDocument();
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == EntityKind::WorkPlane) {
            const auto frame = WorkPlaneFrameOf(id);
            if (!frame.has_value()) {
                continue;
            }
            const double half = 50.0;
            target.points.push_back(frame->PointAt(-half, -half));
            target.points.push_back(frame->PointAt(half, -half));
            target.points.push_back(frame->PointAt(half, half));
            target.points.push_back(frame->PointAt(-half, half));
            // 作業平面は向きがはっきりしている。推さずにそのまま使う。
            NoteFacingDirection(target, frame->normal, frame->uAxis);
            ++target.count;
            continue;
        }
        if (entity->kind == EntityKind::GuideSurface) {
            // 形状ガイドの面。標本の格子から向きを出す。曲がっていれば真ん中の向き。
            if (AppendSurfaceFacing(id, target)) {
                ++target.count;
            }
            continue;
        }
        const std::size_t before = target.points.size();
        for (const auto& curve : session_->Scene().curves) {
            if (curve.entityId == id) {
                SampleCurve(curve.segment, target.points);
            }
        }
        for (const auto& point : session_->Scene().points) {
            if (point.entityId == id) {
                target.points.push_back(point.position);
            }
        }
        if (target.points.size() > before) {
            ++target.count;
            continue;
        }
        if (entity->kind == EntityKind::Part) {
            // 立体そのもの。線も点も持たないので、いままでは何も集まらず、
            // 「作業平面・線・点のどれかを選んでください」と断っていた。
            // 面を選んでいればその面、選んでいなければ立体全体を相手にする。
            if (AppendSolidFacing(id, target)) {
                ++target.count;
            }
        }
    }
}

//! 立体を正対の相手にする。面を選んでいればその面、そうでなければ立体全体。
//!
//! 面を1枚だけ選んでいるときに立体全体を Fit するのは禁止(オーナー指示 §44)。
//! 選んだ面が画面いっぱいになるようにする。
bool V2MainWindow::AppendSolidFacing(const kachakacha::v2::base::EntityId& id,
    FacingTarget& target) const
{
    const auto shape = partShapes_.find(id.ToString());
    if (shape == partShapes_.end()) {
        return false;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    // 面を選んでいるか。選んでいれば、その面だけを相手にする。
    for (const auto& ref : viewport_->Selection().ordered) {
        if (ref.entityId != id
            || ref.kind != kachakacha::v2::app::SelectionElementKind::Face
            || !ref.pickedFaceIndex.has_value()) {
            continue;
        }
        const auto sampled = kachakacha::v2::kernel::FaceSamplesOf(shape->second,
            *ref.pickedFaceIndex);
        const auto pose = kachakacha::v2::kernel::FacePoseNear(shape->second,
            *ref.pickedFaceIndex, ref.hitPoint, tolerance);
        if (!sampled.HasValue() || !pose.HasValue()) {
            continue;
        }
        for (const auto& point : sampled.Value().samples.points) {
            target.points.push_back(point);
        }
        NoteFacingDirection(target, pose.Value().normal, pose.Value().uAxis);
        return true;
    }
    // 面を選んでいない。立体の網の点をそのまま相手にする(向きは推す)。
    return AppendMeshPoints(id, target);
}

//! 画面に出している形の広がりを集める。向きは決めない。
//!
//! 三角形を全部入れると何万点にもなるので、外接箱の8隅だけを使う。
//! 中央と大きさはこれで決まる。向きは「いまのまま」にする ──
//! 立体そのものに「正面」は無いので、勝手に回すと押すたびに向きが変わる。
bool V2MainWindow::AppendMeshPoints(const kachakacha::v2::base::EntityId& id,
    FacingTarget& target) const
{
    bool found = false;
    for (const auto& view : viewport_->ShapeViews()) {
        if (view.entityId != id || view.mesh.Empty()) {
            continue;
        }
        const auto& low = view.mesh.minimum;
        const auto& high = view.mesh.maximum;
        for (int corner = 0; corner < 8; ++corner) {
            target.points.push_back(kachakacha::v2::geometry::Vector3{
                (corner & 1) != 0 ? high.x : low.x,
                (corner & 2) != 0 ? high.y : low.y,
                (corner & 4) != 0 ? high.z : low.z});
        }
        found = true;
    }
    if (found) {
        target.keepOrientation = true;
    }
    return found;
}

//! 形状ガイドの面を正対の相手にする。標本の格子から向きを出す。
//!
//! 曲がった面には1つの法線が無い。真ん中の標本の周りから出す(オーナー指示 §5)。
bool V2MainWindow::AppendSurfaceFacing(const kachakacha::v2::base::EntityId& id,
    FacingTarget& target) const
{
    const auto found = guideSamples_.find(id.ToString());
    if (found == guideSamples_.end()) {
        return AppendMeshPoints(id, target);
    }
    const auto& samples = found->second;
    if (samples.rowCount < 2 || samples.columnCount < 2
        || samples.points.size() < samples.rowCount * samples.columnCount) {
        return AppendMeshPoints(id, target);
    }
    for (const auto& point : samples.points) {
        target.points.push_back(point);
    }
    const auto pose = kachakacha::v2::app::SurfaceFacingPose(samples);
    if (pose.has_value()) {
        NoteFacingDirection(target, pose->normal, pose->uAxis);
    }
    return true;
}

void V2MainWindow::AlignViewToSelection()
{
    using kachakacha::v2::view::BestFitNormal;
    using kachakacha::v2::view::PlanFacingSelection;

    // V1 の「選択に正対」は3つを同時にやる ── 向き・真ん中・大きさ。
    // 向きだけ変えて中身が画面の外にあると「きいていない」ようにしか見えない。
    FacingTarget target;
    CollectFacingTarget(target);
    if (target.points.empty()) {
        SetStatus(QStringLiteral("正対: 作業平面・線・点のどれかを選んでください。"));
        return;
    }
    // `PlanFacingSelection` は「いま見ている側に留まる」。正対のたびに裏へ回り込むと、
    // 押すたびに模型が裏返って見えるためである。
    // 「反対側から正対」は、**渡す視線を裏返して** その決まりに乗る。
    // ここで法線を裏返すと、向こう側で元へ戻されてしまう。
    const auto realDirection = kachakacha::v2::view::ForwardOf(viewport_->Orientation());
    const auto viewDirection = facingFromBehind_ ? realDirection * -1.0 : realDirection;
    kachakacha::v2::geometry::Vector3 normal;
    kachakacha::v2::geometry::Vector3 uAxis;
    if (target.normal.has_value()) {
        normal = *target.normal;
        uAxis = target.uAxis.value_or(kachakacha::v2::geometry::Vector3{1.0, 0.0, 0.0});
    } else if (target.keepOrientation) {
        // 立体そのものには「正面」が無い。向きは変えず、中央と大きさだけ合わせる。
        // 立体そのものには「正面」が無い。いまの向きのままにする。
        normal = realDirection * -1.0;
        uAxis = kachakacha::v2::view::RightOf(viewport_->Orientation());
    } else {
        // 面が分かっていないものは、点の並びから推す(V1 と同じ)。
        const auto guessed = BestFitNormal(target.points, viewDirection);
        if (!guessed.HasValue()) {
            ReportDiagnostics(guessed.Diagnostics());
            return;
        }
        normal = guessed.Value();
        uAxis = FacingUAxisHint(target.points, normal);
    }
    const auto plan = PlanFacingSelection(target.points, normal, uAxis, viewDirection);
    if (!plan.HasValue()) {
        ReportDiagnostics(plan.Diagnostics());
        return;
    }
    // 選択は変えない。正対したら選び直し、では作図へ進めない(オーナー指示 §1-8)。
    const auto keptSelection = viewport_->Selection();
    viewport_->SetOrientation(plan.Value().orientation);
    viewport_->SetViewCenter(plan.Value().center);
    // 少し余白をつけて収める。ぴったりだと端が画面の縁に貼りつく。
    viewport_->SetVisibleWidthMm(plan.Value().spanMm * 1.4);
    viewport_->SetSelection(keptSelection);
    viewport_->update();
    SetStatus(QStringLiteral("%1個に%2正対しました。真ん中に寄せて、大きさも合わせました。"
                             "形は変わっていません。%3")
            .arg(target.count)
            .arg(facingFromBehind_ ? QStringLiteral("反対側から") : QString())
            .arg(target.mixedDirections
                    ? QStringLiteral("向きの違うものが混じっていたので、"
                                     "最初の1つの向きに合わせ、全部が入る大きさにしました。")
                    : QString()));
}

//! 面の上の「横」の見当。点の並びのうち、法線と直交する成分がいちばん長いもの。
kachakacha::v2::geometry::Vector3 V2MainWindow::FacingUAxisHint(
    const std::vector<kachakacha::v2::geometry::Vector3>& points,
    const kachakacha::v2::geometry::Vector3& normal)
{
    using kachakacha::v2::geometry::Dot;
    using kachakacha::v2::geometry::Normalized;
    using kachakacha::v2::geometry::Vector3;
    const Vector3 unit = Normalized(normal);
    Vector3 best;
    double bestLength = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const Vector3 along = points[index] - points.front();
        const Vector3 flat = along - unit * Dot(along, unit);
        if (flat.LengthSquared() > bestLength) {
            bestLength = flat.LengthSquared();
            best = flat;
        }
    }
    return best;
}

void V2MainWindow::ActivateSelectedGroup()
{
    // 選んだものが入っているグループを、作業中にする。
    // 選んでいなければ、作業中を外す。外せないと、一度入れたら抜けられない。
    const auto& snapshot = session_->GetDocument().Snapshot();
    std::optional<kachakacha::v2::base::GroupId> target;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr || !entity->groupId.has_value()) {
            continue;
        }
        if (target.has_value() && *target != *entity->groupId) {
            SetStatus(QStringLiteral(
                "作業中グループ: グループが2つ以上あります。1つにしてください。"));
            return;
        }
        target = entity->groupId;
    }
    if (!SetActiveGroup(target)) {
        return;
    }
    (void)snapshot;
    SetStatus(target.has_value()
            ? QStringLiteral("作業中グループを変えました。%1").arg(ActiveGroupText())
            : QStringLiteral("作業中グループを外しました。"));
}

void V2MainWindow::ShowSelectMenu(const QPoint& at)
{
    // 候補を渡さない入口(一覧の右クリックなど)。並ぶのは台帳のコマンドだけ。
    (void)ShowSelectMenuWithCandidates(at, {});
}

std::vector<QAction*> V2MainWindow::BuildSelectMenu(QMenu& menu,
    const std::vector<QString>& candidateLabels)
{
    // 重なっているものは、押した1点だけでは選び分けられない。
    // 名前と部分要素の種別を先頭に出して、利用者が1件だけ決められるようにする。
    // 見出しの中身は画面(V2Viewport)が作る。ここで作り直すと、
    // 出ている候補と選ばれるものが食い違う。
    std::vector<QAction*> candidateActions;
    if (!candidateLabels.empty()) {
        menu.addSection(QStringLiteral("この場所の候補"));
        candidateActions.reserve(candidateLabels.size());
        for (const QString& label : candidateLabels) {
            candidateActions.push_back(menu.addAction(label));
        }
        menu.addSeparator();
    }
    // 選んでいるものに対してできることを、その場に出す。
    // メニューに並べるのは台帳のコマンドだけ。ここで別の入口を作らない。
    // 別に作ると、押せるかどうかの判断も文言も二重になる。
    static const char* const kEntries[] = {
        "edit.undo",
        "edit.redo",
        "measure.open",
        "group.create",
        "group.dissolve",
        "view.align_selection",
        "view.align_selection_back",
        "wire.split",
        "wire.join",
        "view.hide_selected",
        "view.show_all",
        "edit.delete",
        "part.extrude",
        "fabrication.create",
        "derived.freeze",
    };
    for (const char* id : kEntries) {
        QAction* action = ActionFor(id);
        if (action == nullptr) {
            continue;
        }
        menu.addAction(action);
    }
    return candidateActions;
}

std::optional<int> V2MainWindow::ShowSelectMenuWithCandidates(const QPoint& at,
    const std::vector<QString>& candidateLabels)
{
    QMenu menu(this);
    const std::vector<QAction*> candidateActions = BuildSelectMenu(menu, candidateLabels);
    if (menu.isEmpty()) {
        return std::nullopt;
    }
    const QAction* chosen = menu.exec(at);
    if (chosen == nullptr) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < candidateActions.size(); ++index) {
        if (candidateActions[index] == chosen) {
            return static_cast<int>(index);
        }
    }
    // 台帳のコマンドを選んだ。実行は QAction 側で済んでいるので、選択は動かさない。
    return std::nullopt;
}

void V2MainWindow::ApplyDisplayStage(kachakacha::v2::app::DisplayStage stage)
{
    // 段の中身は core が決める。画面はそれを当てて、名前をそのまま出すだけ。
    // 太さ・様式・薄くする・グリッドの出し方は人が決めたものなので、段では戻さない。
    displayStage_ = stage;
    ApplyDisplaySettings(kachakacha::v2::app::ApplyStage(viewport_->DisplaySettingsNow(),
        displayStage_));
    SetStatus(QString::fromUtf8(std::string(
        kachakacha::v2::app::DisplayStageNameJa(displayStage_)).c_str()));
}

void V2MainWindow::ApplyDisplaySettings(const kachakacha::v2::app::DisplaySettings& settings)
{
    viewport_->SetDisplaySettings(settings);
    if (displayDock_ != nullptr) {
        displayDock_->SetSettings(settings, displayStage_);
    }
    viewport_->update();
}

//! 数の棚を前に出す。製作モードの2枚目として常設していたのをやめた代わりの入口
//! (指示書 C-09、右は「いまの道具の1枚」)。
void V2MainWindow::ShowNumberDock()
{
    if (parameterDock_ == nullptr) {
        return;
    }
    ShowShelf(kachakacha::v2::app::Shelf::Parameter);
    SetStatus(QStringLiteral("数の設定: 右の「数」で板厚・面取り量・型紙の余白・縮尺を決めてください。"));
}

void V2MainWindow::ShowDisplayDock()
{
    if (displayDock_ == nullptr) {
        return;
    }
    displayDock_->SetChoice(CurrentDisplayChoice(), displayStage_);
    ShowShelf(kachakacha::v2::app::Shelf::Display);
    SetStatus(QStringLiteral("表示: 右の「表示」で線の太さ・様式・色と段を決めてください。"));
}

V2DisplayChoice V2MainWindow::CurrentDisplayChoice() const
{
    V2DisplayChoice choice;
    choice.settings = viewport_->DisplaySettingsNow();
    choice.wireColor = viewport_->Colors().wire;
    choice.constructionColor = viewport_->Colors().construction;
    choice.backgroundColor = viewport_->Colors().background;
    return choice;
}

void V2MainWindow::ApplyDisplayChoice(const V2DisplayChoice& choice)
{
    // 見え方だけ。文書は変えない(AT-UIX-010)。色は画面の持ち物。
    ViewportPalette palette = viewport_->Colors();
    if (choice.wireColor.isValid()) {
        palette.wire = choice.wireColor;
    }
    if (choice.constructionColor.isValid()) {
        palette.construction = choice.constructionColor;
    }
    if (choice.backgroundColor.isValid()) {
        palette.background = choice.backgroundColor;
    }
    viewport_->SetPalette(palette);
    viewport_->SetDisplaySettings(choice.settings);
    viewport_->update();
}

void V2MainWindow::BeginRenameSelected()
{
    if (entityTree_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = entityTree_->currentItem();
    if (item == nullptr || EntityForItem(item) == nullptr) {
        SetStatus(QStringLiteral("名前を変えるものを、一覧で選んでください。"));
        return;
    }
    entityTree_->editItem(item, 0);
    SetStatus(QStringLiteral("新しい名前を入れて Enter を押してください。"));
}

void V2MainWindow::RenameEntityFromItem(QTreeWidgetItem* item)
{
    using kachakacha::v2::document::RenameEntityCommand;
    const auto* entityId = EntityForItem(item);
    if (item == nullptr || entityId == nullptr) {
        return;
    }
    const auto* entity = session_->GetDocument().FindEntity(*entityId);
    if (entity == nullptr) {
        return;
    }
    const auto normalized =
        kachakacha::v2::app::NormalizeEntityName(item->text(0).toStdString());
    if (!normalized.HasValue()) {
        ReportDiagnostics(normalized.Diagnostics());
        RefreshEntityList();   // 元の名前へ戻す。空のまま出しっぱなしにしない。
        return;
    }
    if (!kachakacha::v2::app::NameActuallyChanges(entity->displayName,
            normalized.Value())) {
        // 同じ名前を入れ直しただけ。履歴を伸ばさない。
        RefreshEntityList();
        return;
    }
    const auto renamed = session_->GetDocument().Run(
        RenameEntityCommand(*entityId, normalized.Value()));
    if (!renamed.committed) {
        ReportDiagnostics(renamed.diagnostics);
    } else {
        SetStatus(QStringLiteral("名前を「%1」にしました。")
                .arg(QString::fromStdString(normalized.Value())));
    }
    RefreshEntityList();
}

const kachakacha::v2::base::EntityId* V2MainWindow::EntityForItem(
    const QTreeWidgetItem* item) const
{
    for (const auto& entry : entityItems_) {
        if (entry.first == item) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool V2MainWindow::SelectTreeRowForEntity(const kachakacha::v2::base::EntityId& entityId)
{
    if (entityTree_ == nullptr) {
        return false;
    }
    for (const auto& entry : entityItems_) {
        if (entry.second != entityId) {
            continue;
        }
        entityTree_->clearSelection();
        entry.first->setSelected(true);
        // 人が押したときと同じ道を通す。別の入口を作ると、試験が通っても
        // 人が押したときには動かない、ということが起こる。
        AdoptTreeSelection();
        return true;
    }
    return false;
}

int V2MainWindow::TreeSelectedRowCount() const
{
    if (entityTree_ == nullptr) {
        return 0;
    }
    return entityTree_->selectedItems().size();
}

QDockWidget* V2MainWindow::DockForShelf(kachakacha::v2::app::Shelf shelf) const
{
    using kachakacha::v2::app::Shelf;
    switch (shelf) {
    case Shelf::WorkPlane:   return workPlaneDock_;
    case Shelf::Drawing:     return drawingDock_;
    case Shelf::Edit:        return editDock_;
    case Shelf::Corner:      return cornerDock_;
    case Shelf::Measure:     return measureDock_;
    case Shelf::GuideTable:  return guideDock_;
    case Shelf::Fabrication: return fabricationDock_;
    case Shelf::Export:      return exportDock_;
    case Shelf::Grid:        return gridDock_;
    case Shelf::Display:     return displayDock_;
    case Shelf::Parameter:   return parameterDock_;
    case Shelf::Pattern:     return patternDock_;
    case Shelf::Part:        return partDock_;
    case Shelf::Extrude:     return extrudeDock_;
    case Shelf::Surface:     return surfaceDock_;
    case Shelf::Boolean:     return booleanDock_;
    case Shelf::Thicken:     return thickenDock_;
    case Shelf::Array:       return arrayDock_;
    case Shelf::SurfaceEdit: return surfaceEdit_ != nullptr ? surfaceEdit_->Dock() : nullptr;
    case Shelf::None:        break;
    }
    return nullptr;
}

//! 右に出す棚を、いまの道具とモードに合わせる(オーナー指摘 2026-09-11)。
//!
//! どれを出すかは core の ShelfLayout が決める。ここでやるのは出し入れだけ。
//! 画面で決めると、確かめるのに画面を出さなければならなくなる。
void V2MainWindow::RefreshRightShelves()
{
    using kachakacha::v2::app::Shelf;
    if (drawingDock_ == nullptr || exportDock_ == nullptr) {
        return;   // まだ組み立てている途中。
    }
    // 作図の棚は、中身も道具に合わせる。棚を出すだけでは足りない。
    // ベジェ曲線に持ち替えたのに「円弧の作り方」が出たままだった
    // (オーナー指摘 2026-09-13)。
    drawingDock_->SetTool(session_->CurrentTool());
    const auto wanted = kachakacha::v2::app::ShelvesFor(mode_, session_->CurrentTool(),
        extrudeShelfShown_, surfaceShelfShown_, booleanShelfShown_, thickenShelfShown_,
        surfaceEdit_ != nullptr && surfaceEdit_->Active());
    if (operationHost_ != nullptr) {
        operationHost_->SetShelves(wanted);
    }
    if (operationDock_ != nullptr) {
        operationDock_->show();
        operationDock_->raise();
    }
    // 作業平面の棚が隠れたなら、下見も片づける(D-24)。
    RefreshWorkPlanePreview();
    // 棚が替われば、状態行と HUD の道具名(棚で進める操作の名前)も替わる。
    RefreshStatusLine();
}

bool V2MainWindow::ShelfShown(kachakacha::v2::app::Shelf shelf) const
{
    return operationHost_ != nullptr && operationHost_->Shows(shelf);
}

void V2MainWindow::ShowShelf(kachakacha::v2::app::Shelf shelf)
{
    if (operationHost_ == nullptr || operationDock_ == nullptr) {
        return;
    }
    operationHost_->SetShelves({shelf});
    operationDock_->show();
    operationDock_->raise();
    // 作業平面の棚を出した直後(初期値のまま)も、隠れて他の棚に替わったときも、
    // ここで下見の要不要を決め直す(D-24)。
    RefreshWorkPlanePreview();
    RefreshStatusLine();
}
