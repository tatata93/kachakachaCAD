//! 「面を作る」のおまかせ(初心者の入口、プロンプト beginner_workflow)。
//!
//! 3D で線を普通に押す(Ctrl は要らない。もう一度押すと外れる)→ 選んだ線のつながりから
//! 断面・ガイド・境界・通る線・中心線を決め、作り方を理由と他の候補つきで薦める → 下見。
//! 人は線ごとに役割を右の棚(一覧の行の「役割」)か 3D の右クリックで直せる。直した役割は
//! おまかせでもそのまま使う。作り方のカードを押すと、おまかせは切れる(入れた線はそのまま)。
//!
//! 決め方は core(app/SurfaceRoleAssist)。ここは場面の曲線を集めて渡し、欄・3D の色・棚を合わせる。

#include "V2MainWindow.h"

#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/app/SurfaceRoleAssist.h"
#include "kachakacha/domain/Entity.h"

#include <QString>

#include <algorithm>
#include <string>
#include <vector>

using kachakacha::v2::app::WireRoleChoice;
using kachakacha::v2::base::EntityId;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

} // namespace

//! 入っている線を全部、つながりから分け直す(おまかせのときだけ)。人の決めた役割は残す。
void V2MainWindow::ReclassifySurfaceRoles()
{
    if (!surfaceInput_.autoRoles || session_ == nullptr) {
        surfaceRoles_ = kachakacha::v2::app::SurfaceRoleAnalysis{};
        return;
    }
    const auto& document = session_->GetDocument();
    std::vector<EntityId> entries = kachakacha::v2::app::AllSurfaceEntries(surfaceInput_);
    // もう入っていない線の役割は捨てる(古い決定を次の線に持ち越さない)。
    auto& overrides = surfaceInput_.roleOverrides;
    overrides.erase(std::remove_if(overrides.begin(), overrides.end(),
                        [&entries](const kachakacha::v2::app::WireRoleOverride& item) {
                            return std::find(entries.begin(), entries.end(), item.wire)
                                == entries.end();
                        }),
        overrides.end());
    // 並びは番号の順にする(欄を移っても同じ入力なら同じ答え)。
    std::sort(entries.begin(), entries.end(),
        [](const EntityId& l, const EntityId& r) { return l.ToString() < r.ToString(); });
    std::vector<kachakacha::v2::app::RoleWire> wires;
    for (const EntityId& id : entries) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr || entity->kind != kachakacha::v2::domain::EntityKind::Wire) {
            continue;
        }
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(id);
        kachakacha::v2::app::RoleWire wire{id,
            kachakacha::v2::app::SelectedCurves(one, session_->Scene())};
        if (!wire.segments.empty()) {
            wires.push_back(std::move(wire));
        }
    }
    surfaceRoles_ = kachakacha::v2::app::AnalyzeSurfaceRoles(wires, surfaceInput_.roleOverrides,
        document.Snapshot().settings.tolerance);
    surfaceInput_ = kachakacha::v2::app::WithClassifiedRoles(surfaceInput_, surfaceRoles_);
    if (viewport_ != nullptr) {
        viewport_->SetProfileRegionPicking(
            surfaceInput_.method == kachakacha::v2::modeling::GuideSurfaceMethod::PlanarBoundary,
            true);
    }
}

//! 線 1 本の役割を人が決めた(右の棚・右クリック)。自動に戻すなら Auto。
void V2MainWindow::SetSurfaceWireRole(const EntityId& id, WireRoleChoice role)
{
    if (!surfaceShelfShown_ || id.IsNil()) {
        return;
    }
    const auto* entity = session_->GetDocument().FindEntity(id);
    if (entity == nullptr || entity->kind != kachakacha::v2::domain::EntityKind::Wire) {
        return;
    }
    const auto entries = kachakacha::v2::app::AllSurfaceEntries(surfaceInput_);
    const bool present = std::find(entries.begin(), entries.end(), id) != entries.end();
    const auto slot = kachakacha::v2::app::SlotForWireRole(role);
    if (surfaceInput_.autoRoles) {
        surfaceInput_ = kachakacha::v2::app::WithWireRoleChoice(surfaceInput_, id, role);
        if (!present) {
            surfaceInput_ = kachakacha::v2::app::WithSurfaceEntries(surfaceInput_, slot, {id}, false);
        }
        ReclassifySurfaceRoles();
    } else if (role != WireRoleChoice::Auto) {
        // 作り方は人が決めている。その役割の欄へ移すだけ(作り方で使わない欄なら断る)。
        if (!kachakacha::v2::app::CanActivateSurfaceSlot(surfaceInput_, slot)) {
            SetStatus(Text("面を作る: この作り方では「"
                + std::string(kachakacha::v2::app::WireRoleLabelJa(role))
                + "」は使いません。作り方を変えるか、「おまかせに戻す」を押してください。"));
            RefreshSurfaceDock();
            return;
        }
        surfaceInput_ = kachakacha::v2::app::WithoutSurfaceEntries(surfaceInput_, {id});
        surfaceInput_ = kachakacha::v2::app::WithSurfaceEntries(surfaceInput_, slot, {id}, false);
    }
    MirrorSurfaceEntriesToSelection();
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(Text("面を作る: 「" + entity->displayName + "」を"
        + std::string(kachakacha::v2::app::WireRoleLabelJa(role))
        + (role == WireRoleChoice::Auto ? "(線のつながりから決める)" : "") + "にしました。"));
}

//! 他の候補を使う(右の棚の「この作り方にする」)。その候補の役割で入れ直す。
void V2MainWindow::UseSurfaceCandidate(int index)
{
    if (index < 0 || index >= static_cast<int>(surfaceRoles_.alternatives.size())) {
        return;
    }
    const auto candidate = surfaceRoles_.alternatives[static_cast<std::size_t>(index)];
    const std::string name = kachakacha::v2::app::SurfaceCandidateNameJa(candidate);
    if (!candidate.feasible) {
        SetStatus(Text("面を作る: 「" + name + "」はこのままでは作れません: " + candidate.reasonJa));
        return;
    }
    surfaceInput_ = kachakacha::v2::app::WithCandidateRoles(surfaceInput_, candidate);
    surfaceRoles_ = kachakacha::v2::app::SurfaceRoleAnalysis{};
    viewport_->SetProfileRegionPicking(
        surfaceInput_.method == kachakacha::v2::modeling::GuideSurfaceMethod::PlanarBoundary, true);
    MirrorSurfaceEntriesToSelection();
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(Text("面を作る: 作り方を「" + name + "」にし、その役割で入れ直しました。"));
}

//! 「おまかせに戻す」。作り方を人が選んだことを取り消し、つながりから決め直す。
void V2MainWindow::ResumeSurfaceAutoRoles()
{
    if (!surfaceShelfShown_) {
        return;
    }
    surfaceInput_.autoRoles = true;
    surfaceInput_.methodChosenByUser = false;
    surfaceInput_.slotChosenByUser = false;
    ReclassifySurfaceRoles();
    MirrorSurfaceEntriesToSelection();
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: おまかせに戻しました。役割と作り方を、線のつながりから決め直します。"));
}
