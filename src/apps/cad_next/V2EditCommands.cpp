// V2MainWindow の棚(測る棚・編集の棚)の受け口。
//
// 「測定」と「数値で編集」は道具ではなく棚を出す命令。ここにまとめる。
// 欄と定義の往復は core(app/MeasurePanel, app/EntityEdit)が持ち、ここは
// 選んでいるものを core へ渡し、返った定義を文書へ入れるだけ。
#include "V2MainWindow.h"

#include "kachakacha/app/EntityEdit.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"

#include <string>
#include <utility>
#include <vector>

bool V2MainWindow::IsShelfCommand(std::string_view id)
{
    return id == "measure.open" || id == "edit.numeric";
}

void V2MainWindow::RunShelfCommand(std::string_view id)
{
    if (id == "measure.open") {
        // 選んでいるものを測って出す。何も選んでいなければ、何を選ぶかを言う。
        RefreshMeasurements();
        measureDock_->show();
        measureDock_->raise();
        SetStatus(measureDock_->SummaryText());
        return;
    }
    if (id == "edit.numeric") {
        // 選んでいるものの数値を欄に出す。何も選んでいなければ、何を選ぶかを言う。
        RefreshEditDock();
        editDock_->show();
        editDock_->raise();
        SetStatus(editDock_->SelectionText());
    }
}

void V2MainWindow::RefreshMeasurements()
{
    if (measureDock_ == nullptr || viewport_ == nullptr) {
        return;
    }
    measureDock_->SetRequest(CurrentMeasureRequest());
    measureDock_->SetKeptCount(
        static_cast<int>(session_->GetDocument().Snapshot().referenceDimensions.size()));
}

kachakacha::v2::app::MeasureRequest V2MainWindow::CurrentMeasureRequest() const
{
    kachakacha::v2::app::MeasureRequest request;
    // 選んだものだけを測る。見えているだけのものを勝手に足さない。
    request.curves = kachakacha::v2::app::SelectedCurves(viewport_->Selection(),
        session_->Scene());
    request.toleranceMm =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    request.mode = measureDock_->Mode();
    request.targetIds = viewport_->Selection().entityIds;
    for (const auto& pick : viewport_->MeasurePicks()) {
        request.pickedPoints.push_back(pick.point);
        if (!pick.entityId.IsNil()) {
            request.targetIds.push_back(pick.entityId);
        }
    }
    return request;
}

void V2MainWindow::KeepMeasuredDimension()
{
    // 名前と値から参照寸法を作るのは core。文書へ入れるのはここ。形は変わらない。
    const auto dimension = kachakacha::v2::app::MeasureDimensionOf(CurrentMeasureRequest(),
        measureDock_->DimensionName().toStdString(),
        ids_->NextTyped<kachakacha::v2::base::IdKind::Dimension>());
    if (!dimension.HasValue()) {
        ReportDiagnostics(dimension.Diagnostics());
        return;
    }
    const auto added = session_->GetDocument().Run(
        kachakacha::v2::document::AddReferenceDimensionCommand(dimension.Value()));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    RefreshMeasurements();
    SetStatus(QStringLiteral("寸法「%1」を残しました(%2 %3)。")
            .arg(QString::fromStdString(dimension.Value().label))
            .arg(dimension.Value().recordedValue, 0, 'f', 3)
            .arg(QString::fromStdString(dimension.Value().unit)));
}

void V2MainWindow::ClearMeasurement()
{
    viewport_->ClearMeasurePicks();
    viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
    RefreshMeasurements();
    SetStatus(QStringLiteral("測定を消しました。"));
}

void V2MainWindow::RunHistoryCommand(bool undo)
{
    const bool moved = undo ? session_->Undo() : session_->Redo();
    if (!moved) {
        SetStatus(undo ? QStringLiteral("戻せる操作がありません。")
                       : QStringLiteral("やり直せる操作がありません。"));
        return;
    }
    // 文書だけ戻して場面を作り直さないと、消したはずの線が画面に残り、
    // 立体も古いままになる。文書が変わったあとの後始末を全部通す。
    AdoptCurrentDocument();
    SetStatus(undo ? QStringLiteral("元に戻しました。") : QStringLiteral("やり直しました。"));
}

// ---- 編集の棚 ----

kachakacha::v2::modeling::WorkPlaneFrame V2MainWindow::EditAngleFrame(
    const std::optional<kachakacha::v2::base::EntityId>& sourcePlaneId, QString* name) const
{
    // 「平面内角度」の基準。作成元平面があればそれ、無ければ作業中の平面。
    if (sourcePlaneId.has_value()) {
        if (const auto frame = WorkPlaneFrameOf(*sourcePlaneId); frame.has_value()) {
            const auto* plane = session_->GetDocument().FindEntity(*sourcePlaneId);
            if (name != nullptr && plane != nullptr) {
                *name = QStringLiteral("作成元平面「%1」")
                            .arg(QString::fromStdString(plane->displayName));
            }
            return *frame;
        }
    }
    const auto* active = session_->GetDocument().FindEntity(activeWorkPlaneId_);
    if (name != nullptr) {
        *name = active != nullptr
            ? QStringLiteral("作業中の平面「%1」").arg(QString::fromStdString(active->displayName))
            : QStringLiteral("作業中の平面");
    }
    return viewport_->WorkPlane();
}

void V2MainWindow::RefreshEditDock()
{
    using kachakacha::v2::domain::EntityKind;
    if (editDock_ == nullptr || viewport_ == nullptr) {
        return;
    }
    std::vector<std::pair<kachakacha::v2::base::EntityId, QString>> planes;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::WorkPlane) {
            planes.emplace_back(entity.id, QString::fromStdString(entity.displayName));
        }
    }
    editDock_->SetPlanes(planes);

    const auto& selection = viewport_->Selection();
    if (selection.entityIds.size() != 1) {
        const auto why = kachakacha::v2::app::NothingToEditDiagnostic(selection.entityIds.size());
        editDock_->ShowNothing(QString::fromStdString(why.summaryJa + " " + why.detailsJa));
        return;
    }
    const auto* entity = session_->GetDocument().FindEntity(selection.entityIds.front());
    const auto* feature = entity != nullptr
        ? session_->GetDocument().FindFeature(entity->createdBy)
        : nullptr;
    if (entity == nullptr || feature == nullptr) {
        editDock_->ShowNothing(QStringLiteral("選んだものの作り方が見つかりません。"));
        return;
    }
    const QString name = QString::fromStdString(entity->displayName);
    if (const auto* plane = std::get_if<kachakacha::v2::domain::CreateWorkPlaneDefinition>(
            &feature->definition)) {
        editDock_->ShowPlane(name, kachakacha::v2::app::PlaneEditFieldsOf(*plane));
        if (plane->isOriginPlane) {
            editDock_->SetMessage(QStringLiteral("原点の基準平面は数値で変えられません(UI-E002)。"));
        }
        return;
    }
    if (const auto* wire = std::get_if<kachakacha::v2::domain::CreateWireDefinition>(
            &feature->definition)) {
        const auto fields = kachakacha::v2::app::WireEditFieldsOf(*wire);
        if (!fields.HasValue()) {
            editDock_->ShowNothing(QString::fromStdString(
                fields.Diagnostics().front().summaryJa + " " + fields.Diagnostics().front().detailsJa));
            return;
        }
        kachakacha::v2::app::WireEditFields shown = fields.Value();
        // 補助線の印は Entity が持つ(定義の印と食い違っていたら Entity を正とする)。
        shown.construction = entity->construction;
        QString frameName;
        const auto frame = EditAngleFrame(shown.sourcePlaneId, &frameName);
        kachakacha::v2::app::LineMeasure measure;
        if (shown.shape == kachakacha::v2::app::WireEditShape::Line) {
            measure = kachakacha::v2::app::MeasureLine(shown.points[0], shown.points[1], frame);
        }
        editDock_->ShowWire(name, shown, measure, frameName);
        return;
    }
    editDock_->ShowNothing(QStringLiteral("%1「%2」は数値で編集できません。作業平面か線を選んでください。")
            .arg(QString::fromUtf8(std::string(
                     kachakacha::v2::domain::EntityKindNameJa(entity->kind)).c_str()),
                name));
}

void V2MainWindow::ApplySelectedEdit()
{
    using kachakacha::v2::document::SetConstructionCommand;
    using kachakacha::v2::document::UpdateFeatureDefinitionCommand;
    const auto& selection = viewport_->Selection();
    if (selection.entityIds.size() != 1) {
        ReportDiagnostics({kachakacha::v2::app::NothingToEditDiagnostic(selection.entityIds.size())});
        return;
    }
    const auto entityId = selection.entityIds.front();
    const auto* entity = session_->GetDocument().FindEntity(entityId);
    const auto* feature = entity != nullptr
        ? session_->GetDocument().FindFeature(entity->createdBy)
        : nullptr;
    if (entity == nullptr || feature == nullptr) {
        SetStatus(QStringLiteral("選んだものの作り方が見つかりません。"));
        return;
    }
    const auto tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    bool applied = false;
    if (const auto* plane = std::get_if<kachakacha::v2::domain::CreateWorkPlaneDefinition>(
            &feature->definition)) {
        const auto edited = kachakacha::v2::app::EditedPlaneDefinition(*plane,
            editDock_->PlaneFields(), tolerance);
        if (!edited.HasValue()) {
            ReportDiagnostics(edited.Diagnostics());
            editDock_->SetMessage(QString::fromStdString(edited.Diagnostics().front().summaryJa));
            return;
        }
        const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
            feature->id, edited.Value(), {}, "作業平面を数値で直す"));
        if (!changed.committed) {
            ReportDiagnostics(changed.diagnostics);
            return;
        }
        applied = true;
        if (activeWorkPlaneId_ == entityId) {
            // 作業中の平面を直したなら、グリッドと作図面も付いてくる。
            if (const auto frame = WorkPlaneFrameOf(entityId); frame.has_value()) {
                ApplyWorkPlane(*frame, entityId);
            }
        }
    } else if (const auto* wire = std::get_if<kachakacha::v2::domain::CreateWireDefinition>(
                   &feature->definition)) {
        const auto fields = editDock_->WireFields();
        const auto edited = kachakacha::v2::app::EditedWireDefinition(*wire, fields,
            EditAngleFrame(fields.sourcePlaneId, nullptr), *ids_);
        if (!edited.HasValue()) {
            ReportDiagnostics(edited.Diagnostics());
            editDock_->SetMessage(QString::fromStdString(edited.Diagnostics().front().summaryJa));
            return;
        }
        // 形と補助線の印をひとまとまりで入れる。元に戻すのは一度で済む。
        session_->GetDocument().BeginCompound("線を数値で直す");
        const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
            feature->id, edited.Value(), feature->inputEntityIds, "線を数値で直す"));
        if (changed.committed && entity->construction != fields.construction) {
            (void)session_->GetDocument().Run(
                SetConstructionCommand({entityId}, fields.construction));
        }
        session_->GetDocument().EndCompound();
        if (!changed.committed) {
            ReportDiagnostics(changed.diagnostics);
            return;
        }
        applied = true;
    }
    if (!applied) {
        SetStatus(QStringLiteral("この種類は数値で編集できません。"));
        return;
    }
    AdoptCurrentDocument();
    // 直したものをそのまま選んでおく。選び直さずに続けて直せる。
    kachakacha::v2::app::SelectionSet next;
    next.entityIds.push_back(entityId);
    viewport_->SetSelection(next);
    RefreshEditDock();
    SetStatus(QStringLiteral("数値変更を適用しました。"));
}
