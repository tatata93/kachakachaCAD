//! 「面を作る」の入力欄と 3D の行き来(引継ぎ 2026-09-17 の 1)。
//!
//! これまで、面を作っている最中の 3D クリックは **作り方の既定の欄** へしか
//! 入らなかった。断面を入れたあとにガイドを入れる道が無く、外す道も無く、
//! 3D で選択を外しても欄には残っていた。
//!
//! ここでは次の決まりにする。
//!   - 欄が正本。3D の選択の印は、欄に入っているものの合計に合わせる。
//!   - 3D で押したものは「いまの欄」へ入る。もう一度押すと外れる。Ctrl は要らない。
//!   - 「いまの欄」は右の棚の「ここへ選ぶ」で替える。押された形でいつも見える。
//!   - 「解除」で欄ごと空にする。手動固定の並びからも消す。古い入力を残さない。
//!
//! 3D の選択は「押したものだけが増える/減る」形でしか変わらない
//! (`V2Viewport::SetToolPickToggle`)ので、差分から何を押したかが一意に読める。

#include "V2MainWindow.h"

#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceInputState.h"

#include <QString>

#include <algorithm>
#include <string>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::ChainRole;

namespace {

//! `left` にあって `right` に無いもの。
[[nodiscard]] std::vector<EntityId> Missing(const std::vector<EntityId>& left,
    const std::vector<EntityId>& right)
{
    std::vector<EntityId> out;
    for (const EntityId& id : left) {
        if (std::find(right.begin(), right.end(), id) == right.end()) {
            out.push_back(id);
        }
    }
    return out;
}

} // namespace

//! 3D の選択の印を、欄に入っているものの合計に合わせる。**欄が正本。**
void V2MainWindow::MirrorSurfaceEntriesToSelection()
{
    if (viewport_ == nullptr) {
        return;
    }
    surfaceMirror_ = kachakacha::v2::app::AllSurfaceEntries(surfaceInput_);
    kachakacha::v2::app::SelectionSet mirrored;
    mirrored.entityIds = surfaceMirror_;
    for (const EntityId& id : surfaceMirror_) {
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = id;
        mirrored.ordered.push_back(ref);
    }
    // 自分で入れ替えるときは、選択の便りを自分で読まない。堂々巡りになる。
    surfaceMirroring_ = true;
    viewport_->SetSelection(std::move(mirrored));
    surfaceMirroring_ = false;
}

//! 面を作っている最中に 3D の選択が変わった。差分を「いまの欄」へ入れる/外す。
void V2MainWindow::RefreshSurfaceForSelectionChange()
{
    if (!surfaceShelfShown_ || surfaceMirroring_ || viewport_ == nullptr) {
        return;
    }
    const auto& now = viewport_->Selection().entityIds;
    auto added = Missing(now, surfaceMirror_);
    auto removed = Missing(surfaceMirror_, now);
    const auto& document = session_->GetDocument();
    // 押した当人が分かるなら、それを「いまの欄」へ入れる/外す/移す。
    // 差分だけだと、断面に入っている線をガイドの欄で押しても(選択は減るだけで)移らなかった
    // (PC 自己試験 HP-SF-06 2026-09-19)。
    if (const auto pick = viewport_->TakeLastToolPick(); pick.has_value()) {
        added.clear();
        removed.clear();
        added.push_back(*pick);
    }
    // いまの欄に入るのは、その欄が受ける種類のものだけ。
    // 元の面の欄は形状ガイド、それ以外は線。立体などは黙って受けない。
    std::vector<EntityId> accepted;
    for (const EntityId& id : added) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        const bool wanted = surfaceInput_.activeSlot == ChainRole::SourceSurface
            ? entity->kind == kachakacha::v2::domain::EntityKind::GuideSurface
            : entity->kind == kachakacha::v2::domain::EntityKind::Wire;
        if (wanted) {
            accepted.push_back(id);
        }
    }
    if (accepted.empty() && removed.empty()) {
        MirrorSurfaceEntriesToSelection();   // 受けなかったものを選択に残さない。
        return;
    }
    surfaceInput_ = kachakacha::v2::app::WithoutSurfaceEntries(surfaceInput_, removed);
    surfaceInput_ = kachakacha::v2::app::WithSurfaceEntriesToggled(surfaceInput_,
        surfaceInput_.activeSlot, accepted);
    // 回転体は断面が入ったら次は軸(自動遷移)。
    surfaceInput_ = kachakacha::v2::app::WithSurfaceSlotAdvanced(surfaceInput_);
    MirrorSurfaceEntriesToSelection();
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    const auto& slot = kachakacha::v2::app::SurfaceSlotEntries(surfaceInput_,
        surfaceInput_.activeSlot);
    SetStatus(QStringLiteral("面を作る: %1 は %2 本。%3")
            .arg(QString::fromUtf8(std::string(kachakacha::v2::app::SurfaceSlotNameJa(
                                       surfaceInput_.method, surfaceInput_.activeSlot))
                                       .c_str()))
            .arg(static_cast<int>(slot.size()))
            .arg(QString::fromUtf8(
                kachakacha::v2::app::SurfaceActiveSlotHintJa(surfaceInput_).c_str())));
}

//! 右の棚の「ここへ選ぶ」。以後の 3D クリックはその欄へ入る。
void V2MainWindow::ActivateSurfaceSlot(ChainRole slot)
{
    if (!kachakacha::v2::app::CanActivateSurfaceSlot(surfaceInput_, slot)) {
        SetStatus(QStringLiteral("面を作る: この作り方では「%1」は使いません。")
                .arg(QString::fromUtf8(
                    std::string(kachakacha::v2::app::SurfaceSlotNameJa(slot)).c_str())));
        RefreshSurfaceDock();   // 押された形を元に戻す。
        return;
    }
    surfaceInput_ = kachakacha::v2::app::WithActiveSurfaceSlot(surfaceInput_, slot);
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: %1")
            .arg(QString::fromUtf8(
                kachakacha::v2::app::SurfaceActiveSlotHintJa(surfaceInput_).c_str())));
}

//! 右の棚の「解除」。その欄を空にし、3D の印も外す。
void V2MainWindow::ClearSurfaceSlot(ChainRole slot)
{
    surfaceInput_ = kachakacha::v2::app::WithSurfaceSlotCleared(surfaceInput_, slot);
    MirrorSurfaceEntriesToSelection();
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: %1 を空にしました。")
            .arg(QString::fromUtf8(
                std::string(kachakacha::v2::app::SurfaceSlotNameJa(slot)).c_str())));
}
