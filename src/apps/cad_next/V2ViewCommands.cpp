//! 見え方と作業中まとまりのコマンド(V2MainWindow の一部)。
//!
//! ここで扱うものは、どれも **形を変えない。**
//! 正対しても、グリッドを消しても、作業中のまとまりを変えても、
//! 文書の中の形は1mmも動かない。動かしてしまうと、
//! 「見やすくしただけ」のつもりが寸法を変えたことになる。

#include "V2MainWindow.h"

#include "kachakacha/app/DisplaySettings.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/view/ViewOrientation.h"

#include <string>
#include <string_view>

bool V2MainWindow::IsViewCommand(std::string_view id)
{
    return id == "view.align_selection" || id == "view.display_settings"
        || id == "group.set_active";
}

void V2MainWindow::RunViewCommand(std::string_view id)
{
    if (id == "view.display_settings") {
        displayStage_ = kachakacha::v2::app::NextDisplayStage(displayStage_);
        viewport_->SetDisplaySettings(
            kachakacha::v2::app::SettingsForStage(displayStage_));
        SetStatus(QString::fromUtf8(std::string(
            kachakacha::v2::app::DisplayStageNameJa(displayStage_)).c_str()));
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
}

void V2MainWindow::AlignViewToSelection()
{
    using kachakacha::v2::view::OrientationFacing;

    // 選んだ作業平面の法線へ正対する。形は変わらない。
    const auto& snapshot = session_->GetDocument().Snapshot();
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
