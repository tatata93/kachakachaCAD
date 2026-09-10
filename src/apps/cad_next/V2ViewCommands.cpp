//! 見え方と作業中まとまりのコマンド(V2MainWindow の一部)。
//!
//! ここで扱うものは、どれも **形を変えない。**
//! 正対しても、グリッドを消しても、作業中のまとまりを変えても、
//! 文書の中の形は1mmも動かない。動かしてしまうと、
//! 「見やすくしただけ」のつもりが寸法を変えたことになる。

#include "V2MainWindow.h"

#include "kachakacha/document/Commands.h"

#include <QAction>
#include <QMenu>
#include <QTreeWidget>
#include <QPoint>

#include "kachakacha/app/DisplaySettings.h"
#include "kachakacha/app/EntityNaming.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/view/ViewOrientation.h"

#include <string>
#include <string_view>

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

void V2MainWindow::AlignViewToSelection()
{
    using kachakacha::v2::view::OrientationFacing;

    // 選んだ作業平面の法線へ正対する。形は変わらない。
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr
            || entity->kind != kachakacha::v2::domain::EntityKind::WorkPlane) {
            continue;
        }
        const auto* feature = session_->GetDocument().FindFeature(entity->createdBy);
        if (feature == nullptr) {
            continue;
        }
        const auto* definition =
            std::get_if<kachakacha::v2::domain::CreateWorkPlaneDefinition>(
                &feature->definition);
        if (definition == nullptr) {
            continue;
        }
        // 作り方ではなく、出来上がった法線を使う。作り直すと、
        // 平面を作ったときと違う向きになりかねない。
        const auto orientation = OrientationFacing(definition->normal,
            kachakacha::v2::geometry::Vector3{0.0, 0.0, 1.0});
        if (!orientation.HasValue()) {
            ReportDiagnostics(orientation.Diagnostics());
            return;
        }
        viewport_->SetOrientation(orientation.Value());
        SetStatus(QStringLiteral("%1 に正対しました。形は変わっていません。")
                .arg(QString::fromStdString(entity->displayName)));
        return;
    }
    SetStatus(QStringLiteral("正対: 作業平面を1つ選んでください。"));
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
