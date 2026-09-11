//! 見え方と作業中まとまりのコマンド(V2MainWindow の一部)。
//!
//! ここで扱うものは、どれも **形を変えない。**
//! 正対しても、グリッドを消しても、作業中のまとまりを変えても、
//! 文書の中の形は1mmも動かない。動かしてしまうと、
//! 「見やすくしただけ」のつもりが寸法を変えたことになる。

#include "V2MainWindow.h"

#include <algorithm>

#include "kachakacha/document/Commands.h"

#include <QAction>
#include <QMenu>
#include <QTreeWidget>
#include <QPoint>

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
    return id == "view.align_selection" || id == "view.display_settings"
        || id == "group.set_active" || id == "view.hide_selected"
        || id == "view.show_all" || id == "edit.delete"
        || id == "view.stage_all" || id == "view.stage_no_grid"
        || id == "view.stage_no_construction" || id == "view.stage_selection_only"
        || id == "entity.rename";
}

void V2MainWindow::RunViewCommand(std::string_view id)
{
    if (id == "view.display_settings") {
        // 右の「表示」の棚を前に出す(V1 の表示設定タブ)。段は Ctrl+1/2/3 で直に選ぶ。
        ShowDisplayDock();
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
    if (id == "view.align_selection") {
        AlignViewToSelection();
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
        // まとまりや「原点」の見出しを選んだら、その下のもの全部へ広げる(V1 と同じ)。
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
            target.normal = frame->normal;
            target.uAxis = frame->uAxis;
            ++target.count;
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
        }
    }
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
    const auto viewDirection = kachakacha::v2::view::ForwardOf(viewport_->Orientation());
    kachakacha::v2::geometry::Vector3 normal;
    kachakacha::v2::geometry::Vector3 uAxis;
    if (target.normal.has_value()) {
        normal = *target.normal;
        uAxis = target.uAxis.value_or(kachakacha::v2::geometry::Vector3{1.0, 0.0, 0.0});
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
    viewport_->SetOrientation(plan.Value().orientation);
    viewport_->SetViewCenter(plan.Value().center);
    // 少し余白をつけて収める。ぴったりだと端が画面の縁に貼りつく。
    viewport_->SetVisibleWidthMm(plan.Value().spanMm * 1.4);
    viewport_->update();
    SetStatus(QStringLiteral("%1個に正対しました。形は変わっていません。")
            .arg(target.count));
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
    // 選んだものが入っているまとまりを、作業中にする。
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
                "作業中グループ: まとまりが2つ以上あります。1つにしてください。"));
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
    // 選んでいるものに対してできることを、その場に出す。
    // メニューに並べるのは台帳のコマンドだけ。ここで別の入口を作らない。
    // 別に作ると、押せるかどうかの判断も文言も二重になる。
    static const char* const kEntries[] = {
        "edit.undo",
        "edit.redo",
        "measure.open",
        "view.align_selection",
        "wire.split",
        "wire.join",
        "view.hide_selected",
        "view.show_all",
        "edit.delete",
        "part.extrude",
        "fabrication.create",
        "derived.freeze",
    };
    QMenu menu(this);
    for (const char* id : kEntries) {
        QAction* action = ActionFor(id);
        if (action == nullptr) {
            continue;
        }
        menu.addAction(action);
    }
    if (menu.isEmpty()) {
        return;
    }
    menu.exec(at);
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

void V2MainWindow::ShowDisplayDock()
{
    if (displayDock_ == nullptr) {
        return;
    }
    displayDock_->SetChoice(CurrentDisplayChoice(), displayStage_);
    displayDock_->show();
    displayDock_->raise();
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
