//! 「面を作る」(オーナー指示 2026-09-15 §10〜§13、UI の正本)。
//!
//! 人が使う入口はこれ1つ。中では今までの `GuideTable` をそのまま使う。
//! これまでは「形状ガイド」と役割表の2本立てで、人から見て別物に見えていた。
//! しかも役割表は後ろの札にあり、**いまの作り方がどこにも出ていなかった。**

#include "V2MainWindow.h"
#include "V2SurfaceAnalysisTool.h"

#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/OuterLoopSplit.h"
#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/app/ToolFooter.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SurfaceDeviationLimit.h"

#include <QString>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using kachakacha::v2::app::SurfaceOrdering;
using kachakacha::v2::app::SurfaceSlotState;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;

namespace {

kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable> AddRegionBoundary(
    kachakacha::v2::modeling::GuideTable table,
    const kachakacha::v2::app::ProfileBoundary& boundary, ChainRole role,
    const kachakacha::v2::document::Document& document)
{
    using Out = kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>;
    if (boundary.entityIds.empty() || boundary.segments.empty()) {
        return Out::Failure(kachakacha::v2::base::MakeError("UI-R004",
            "輪郭の線がありません。", "閉じた輪郭の内側を選んでください。"));
    }
    kachakacha::v2::modeling::GuideTableSelection selection;
    selection.sourceWireId = boundary.entityIds.front();
    selection.segments = boundary.segments;
    std::vector<kachakacha::v2::base::EntityId> uniqueEntityIds;
    std::unordered_set<kachakacha::v2::base::EntityId> seenEntityIds;
    for (const auto& id : boundary.entityIds) {
        if (!seenEntityIds.insert(id).second) {
            continue;
        }
        uniqueEntityIds.push_back(id);
        const auto* entity = document.FindEntity(id);
        if (!selection.label.empty()) {
            selection.label += " + ";
        }
        selection.label += entity != nullptr && !entity->displayName.empty()
            ? entity->displayName : std::string("名前のない線");
    }
    auto added = kachakacha::v2::modeling::AddSelectionAsNewRow(table, role, selection);
    if (!added.HasValue()) {
        return added;
    }
    auto next = added.Value();
    next.rows.back().sourceWireIds = uniqueEntityIds;
    next.rows.back().sourceLabels.clear();
    for (const auto& id : uniqueEntityIds) {
        const auto* entity = document.FindEntity(id);
        next.rows.back().sourceLabels.push_back(entity == nullptr
                ? std::string("名前のない線") : entity->displayName);
    }
    return Out::Success(std::move(next));
}

//! 人が「向き反転」を押した線は、表の行を逆向きにする(ReverseRow は向きの印も持つ)。
[[nodiscard]] kachakacha::v2::modeling::GuideTable WithReversedRows(
    kachakacha::v2::modeling::GuideTable table, const kachakacha::v2::app::SurfaceInputState& input)
{
    for (std::size_t row = 0; row < table.rows.size(); ++row) {
        const auto& ids = table.rows[row].sourceWireIds;
        if (ids.size() == 1 && kachakacha::v2::app::SurfaceEntryReversed(input, ids.front())) {
            const auto flipped = kachakacha::v2::modeling::ReverseRow(table, row);
            if (flipped.HasValue()) {
                table = flipped.Value();
            }
        }
    }
    // 境界の辺ごとの連続条件と支持面(境界面・四辺面だけ)。
    if (kachakacha::v2::app::SurfaceTakesContinuity(input.method)) {
        for (auto& row : table.rows) {
            if (row.role != ChainRole::BoundarySide || row.sourceWireIds.size() != 1) {
                continue;
            }
            const auto condition =
                kachakacha::v2::app::EdgeConditionOf(input, row.sourceWireIds.front());
            row.continuity = condition.continuity;
            row.supportSurfaceId = condition.support;
        }
    }
    return table;
}

} // namespace

//! 選んだものから分かる事実。作り方を薦めるのに使う。
kachakacha::v2::app::SurfaceSelectionFacts V2MainWindow::SurfaceFactsNow() const
{
    kachakacha::v2::app::SurfaceSelectionFacts facts;
    const auto& document = session_->GetDocument();
    const auto& tolerance = document.Snapshot().settings.tolerance;
    std::vector<kachakacha::v2::base::EntityId> wireIds;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == EntityKind::GuideSurface) {
            ++facts.guideSurfaces;
            continue;
        }
        if (entity->kind != EntityKind::Wire) {
            continue;
        }
        wireIds.push_back(id);
    }
    const auto regions = kachakacha::v2::app::DetectProfileRegions(
        session_->Scene(), wireIds, tolerance);
    std::vector<kachakacha::v2::base::EntityId> used;
    for (const auto& region : regions) {
        ++facts.closedWires;
        ++facts.closedPlanarWires;
        for (const auto& id : kachakacha::v2::app::ProfileRegionEntityIds(region)) {
            if (std::find(used.begin(), used.end(), id) == used.end()) {
                used.push_back(id);
            }
        }
    }
    facts.openWires = wireIds.size() - used.size();
    return facts;
}

//! 選んでいる線を、その役割へ入れる。
void V2MainWindow::AddSelectionToSurfaceSlot(ChainRole role)
{
    std::vector<kachakacha::v2::base::EntityId> ids;
    const auto& document = session_->GetDocument();
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        const bool wanted = role == ChainRole::SourceSurface
            ? entity->kind == EntityKind::GuideSurface
            : entity->kind == EntityKind::Wire;
        if (wanted) {
            ids.push_back(id);
        }
    }
    if (ids.empty()) {
        SetStatus(QStringLiteral("面を作る: %1にするものを選んでください。")
                .arg(QString::fromUtf8(
                    std::string(kachakacha::v2::app::SurfaceSlotNameJa(role)).c_str())));
        return;
    }
    surfaceInput_ = kachakacha::v2::app::WithSurfaceEntries(surfaceInput_, role, ids, false);
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: %1を %2 本にしました。")
            .arg(QString::fromUtf8(
                std::string(kachakacha::v2::app::SurfaceSlotNameJa(role)).c_str()))
            .arg(static_cast<int>(
                kachakacha::v2::app::SurfaceSlotEntries(surfaceInput_, role).size())));
}

//! 棚に、いまの入力を映す。
void V2MainWindow::RefreshSurfaceDock()
{
    if (surfaceDock_ == nullptr) {
        return;
    }
    const auto& document = session_->GetDocument();
    const auto nameOf = [&document](const kachakacha::v2::base::EntityId& id) {
        const auto* entity = document.FindEntity(id);
        return entity != nullptr && !entity->displayName.empty()
            ? QString::fromStdString(entity->displayName)
            : QStringLiteral("名前のないもの");
    };
    V2SurfaceDock::SlotNames names;
    for (const auto& id : kachakacha::v2::app::SurfaceSectionOrder(surfaceInput_)) {
        names.sections.push_back(nameOf(id));
    }
    for (const auto& id : surfaceInput_.guides) {
        names.guides.push_back(nameOf(id));
    }
    for (const auto& id : surfaceInput_.centerlines) {
        names.centerlines.push_back(nameOf(id));
    }
    for (const auto& id : surfaceInput_.boundaries) {
        const auto condition = kachakacha::v2::app::EdgeConditionOf(surfaceInput_, id);
        const bool takes = kachakacha::v2::app::SurfaceTakesContinuity(surfaceInput_.method);
        // 辺ごとの連続条件を名前の後ろに出す(境界面・四辺面)。「境界 2  屋根.縁 G2」。
        names.boundaries.push_back(nameOf(id)
            + (takes ? QStringLiteral("  ") + QString::fromUtf8(std::string(
                           kachakacha::v2::modeling::SurfaceContinuityName(condition.continuity))
                                                                   .c_str())
                     : QString()));
        names.boundaryContinuity.push_back(static_cast<int>(condition.continuity));
        names.boundarySupports.push_back(condition.support.IsNil() ? QString()
                                                                   : nameOf(condition.support));
    }
    // 下見に出ている面が、指定した線からどれだけ外れているか。
    // **近づけて作る面は線の上に乗っていない。**知らずに板取りへ進むと、
    // 紙とプラ板を切ってから気づくことになる。
    QString deviation;
    // 作り方の内訳。下見が出ないときも出す(曲線網の「こちらなら作れます」など)。
    QString solver = QString::fromStdString(surfaceSolverNote_);
    if (surfaceSnapshot_.has_value()) {
        const auto request = kachakacha::v2::modeling::ToGuideSurfaceRequest(
            surfaceSnapshot_->table, session_->GetDocument().Snapshot().settings.tolerance);
        const auto note = request.HasValue()
            ? kachakacha::v2::modeling::SurfaceDeviationNoteJa(request.Value(),
                  surfaceSnapshot_->built.maximumDeviationMm,
                  session_->GetDocument().Snapshot().settings.tolerance)
            : std::string();
        deviation = QString::fromStdString(note);
        // 下見の製作性の目安(ガウス曲率から。断定しない)。解析の棚を開かなくても出す。
        if (surfaceAnalysis_ != nullptr) {
            const std::string developable = surfaceAnalysis_->PreviewDevelopabilityJa();
            if (!developable.empty()) {
                deviation += (deviation.isEmpty() ? QString() : QStringLiteral("\n"))
                    + QString::fromStdString(developable);
            }
        }
        if (!surfaceSnapshot_->batch.empty()) {
            solver += QStringLiteral("(%1 個に分けて、1 つずつ作ります)")
                          .arg(static_cast<int>(surfaceSnapshot_->batch.size()) + 1);
        }
    }
    surfaceDock_->ShowInput(surfaceInput_, names, surfaceSnapshot_.has_value(), deviation,
        solver);
    // 一番下の一行(正本の footer)。**まだ文書に入っていないこと**も、ここで言う。
    ShowToolFooter(surfaceShelfShown_
            ? QString::fromUtf8(kachakacha::v2::app::SurfaceFooterLine(surfaceInput_,
                  surfaceSnapshot_.has_value()).c_str())
            : QString());
    // 下見が変われば、面の解析(出していれば)も下見の面を塗り直す。
    if (surfaceAnalysis_ != nullptr) {
        surfaceAnalysis_->Refresh();
    }
}

//! いまの入力で出来上がる面を、線で出す(§12)。**文書へは何も書かない。**
//!
//! 作る前に一度必ずここを通る。ここで作れなかったものは確定させない。
//! 「見ていない形は作らない」を、面でも守るための場所。
void V2MainWindow::RefreshSurfacePreview()
{
    surfaceSnapshot_.reset();
    surfaceSolverNote_.clear();
    if (viewport_ != nullptr) {
        viewport_->HideToolPreview();
    }
    if (!surfaceShelfShown_ || !kachakacha::v2::app::SurfaceReadyToBuild(surfaceInput_)) {
        return;
    }
    // 一括(離した面の元の面が複数、回転体の断面が複数)は 1 つずつに分けて全部作る。
    // **どれか 1 つでも作れなければ、下見を出さない**(半分だけ作れたことにしない)。
    std::optional<SurfaceSnapshot> snapshot;
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines;
    for (const auto& part : kachakacha::v2::app::SurfaceBatchStates(surfaceInput_)) {
        const auto table = SurfaceTableFromInput(part);
        if (!table.HasValue()) {
            return;   // まだ足りない。棚の「4. 状態」が理由を出している。
        }
        // ここでは断りを出さない。入れている途中はまだ作れなくて当たり前で、
        // そのたびに赤い字を出すと、何が本当の失敗か分からなくなる。
        const auto built = BuildSurfaceFromTable(table.Value(), false);
        if (!built.has_value()) {
            return;
        }
        const auto preview = kachakacha::v2::app::SurfacePreviewLines(built->samples,
            built->boundary);
        lines.insert(lines.end(), preview.begin(), preview.end());
        if (!snapshot.has_value()) {
            snapshot = SurfaceSnapshot{table.Value(), *built, {}};
        } else {
            snapshot->batch.emplace_back(table.Value(), *built);
        }
    }
    surfaceSnapshot_ = snapshot;
    // 採用した断面の並びを状態へ書き戻す。画面の「3. 断面順」と 3D の札は、
    // 押した順ではなく **この順** を出す。手動固定なら渡した順がそのまま返ってくる。
    if (surfaceSnapshot_.has_value() && surfaceSnapshot_->batch.empty()) {
        surfaceInput_.adoptedOrder = surfaceAdoptedSections_;
    }
    if (viewport_ != nullptr && surfaceSnapshot_.has_value()) {
        viewport_->ShowToolPreview(lines);
    }
}

//! 「面を作る」を押した。選んだものを役割へ取り込み、作り方を薦めて棚を出す。
void V2MainWindow::RunSurfaceCreate()
{
    if (!surfaceShelfShown_) {
        // 始めるとき。選んだものを取り込み、作り方を薦める(§11)。
        const auto facts = SurfaceFactsNow();
        if (!surfaceInput_.methodChosenByUser) {
            surfaceInput_.method = kachakacha::v2::app::RecommendSurfaceMethod(facts);
        }
        // 取り込み先の欄は core が決める。画面と core で2回書かない。
        surfaceInput_.activeSlot =
            kachakacha::v2::app::DefaultSurfaceIntakeSlot(surfaceInput_.method);
        AddSelectionToSurfaceSlot(surfaceInput_.activeSlot);
        surfaceShelfShown_ = true;
        viewport_->SetToolPickActive(true);
        // 以後の 3D クリックは「押すたびに入れる/外す」。欄が正本、3D はその印。
        viewport_->SetToolPickToggle(true);
        MirrorSurfaceEntriesToSelection();
        viewport_->SetProfileRegionPicking(
            surfaceInput_.method == GuideSurfaceMethod::PlanarBoundary);
        RefreshRightShelves();
        RefreshSurfacePreview();
        RefreshSurfaceRoleLabels();
        RefreshSurfaceDock();
        SetStatus(QStringLiteral("面を作る\n作り方: %1\n右の棚で作り方と入力を決めて、"
                                 "Enter で確定します。Esc でやめます。")
                .arg(QString::fromUtf8(std::string(
                    kachakacha::v2::app::GuideSurfaceMethodLabelJa(surfaceInput_.method))
                        .c_str())));
        return;
    }
    ConfirmSurface();
}

//! 作り方を変える。**入れたものは捨てない**(§11)。
void V2MainWindow::ChooseSurfaceMethod(GuideSurfaceMethod method)
{
    surfaceInput_.method = method;
    surfaceInput_.methodChosenByUser = true;
    // いまの欄がその作り方で使えなければ、既定の欄へ戻す。入れたものは捨てない。
    surfaceInput_ = kachakacha::v2::app::WithActiveSlotSettled(surfaceInput_);
    viewport_->SetProfileRegionPicking(method == GuideSurfaceMethod::PlanarBoundary);
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: 作り方を「%1」にしました。"
                             "入れたものはそのまま残っています。")
            .arg(QString::fromUtf8(
                std::string(kachakacha::v2::app::GuideSurfaceMethodLabelJa(method)).c_str())));
}

void V2MainWindow::ChooseSurfaceOrdering(SurfaceOrdering ordering)
{
    if (ordering == SurfaceOrdering::ManualLock && surfaceInput_.explicitOrder.empty()) {
        // 手動固定にした瞬間に **画面に出ている並び**(自動の採用順)を、そのまま固定する。
        // 正本の注記「手動固定: この表示順をそのまま生成順として使います」。
        surfaceInput_.explicitOrder = kachakacha::v2::app::SurfaceSectionOrder(surfaceInput_);
    }
    surfaceInput_.ordering = ordering;
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
}

//! 断面を1つ動かす。手動固定のときだけ効く。
void V2MainWindow::MoveSurfaceSection(int from, int to)
{
    auto order = kachakacha::v2::app::SurfaceSectionOrder(surfaceInput_);
    if (from < 0 || to < 0 || from >= static_cast<int>(order.size())
        || to >= static_cast<int>(order.size())) {
        return;
    }
    const auto moved = order[static_cast<std::size_t>(from)];
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, moved);
    surfaceInput_.explicitOrder = order;
    surfaceInput_.ordering = SurfaceOrdering::ManualLock;
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
}

//! 入力をやり直す。作り方は残す。
void V2MainWindow::ResetSurfaceInput()
{
    const auto method = surfaceInput_.method;
    const bool chosen = surfaceInput_.methodChosenByUser;
    const auto activeSlot = surfaceInput_.activeSlot;
    surfaceInput_ = kachakacha::v2::app::SurfaceInputState{};
    surfaceInput_.method = method;
    surfaceInput_.methodChosenByUser = chosen;
    surfaceInput_.activeSlot = activeSlot;
    MirrorSurfaceEntriesToSelection();   // 3D の印も消す。古い入力を残さない。
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: 入力を空にしました。作り方はそのままです。"));
}

void V2MainWindow::EndSurfacePreview()
{
    surfaceShelfShown_ = false;
    surfaceSnapshot_.reset();
    // 欄の中身(断面・ガイド・境界の線)は次の面へ持ち越さない。持ち越すと、次に
    // 「面を作る」を押したとき前の面の線が黙って混ざる(別の文書なら消えた id を指す。
    // PC 4 回目 HP-RS-01: 面を 1 枚作った後の場面で 2 枚目が作れなかった)。作り方は残す。
    {
        const auto method = surfaceInput_.method;
        const bool chosen = surfaceInput_.methodChosenByUser;
        surfaceInput_ = kachakacha::v2::app::SurfaceInputState{};
        surfaceInput_.method = method;
        surfaceInput_.methodChosenByUser = chosen;
    }
    if (viewport_ != nullptr) {
        viewport_->HideToolPreview();
        viewport_->HideToolRoleLabels();
        viewport_->SetToolPickActive(false);
        viewport_->SetToolPickToggle(false);
        viewport_->SetProfileRegionPicking(false);
        // 欄の印(断面/ガイド/境界の線)を 3D の選択に残さない(HP-SF-06)。
        // 残すと、やめたあとも線が光ったままで「まだ面を作っている」ように見える。
        surfaceMirroring_ = true;
        viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
        surfaceMirroring_ = false;
    }
    surfaceMirror_.clear();
    ShowToolFooter(QString());
    RefreshRightShelves();
    if (surfaceAnalysis_ != nullptr) {
        surfaceAnalysis_->Refresh();   // 下見の面の塗りを消す。
    }
}

//! 入力から `GuideTable` を組み立てる。**作る直前の1回だけ。**
//!
//! 役割は、いまの作り方が使うものだけを入れる。
//! こうすると「方式を変える前に要らない行を自分で消す」が要らなくなる
//! (人向けの UI-R003 を無くす。内部の診断はそのまま残る)。
kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>
V2MainWindow::SurfaceTableFromInput() const
{
    return SurfaceTableFromInput(surfaceInput_);
}

kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>
V2MainWindow::SurfaceTableFromInput(
    const kachakacha::v2::app::SurfaceInputState& input) const
{
    using Out = kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>;
    kachakacha::v2::modeling::GuideTable table;
    table.method = input.method;
    // 手動固定は要求までそのまま渡す。カーネルに並べ替えさせない。
    table.lockSectionOrder = input.ordering == SurfaceOrdering::ManualLock;
    table.fourEdgeStyle = input.fourEdgeStyle;
    if (input.method == GuideSurfaceMethod::PlanarBoundary) {
        const auto regions = kachakacha::v2::app::DetectProfileRegions(session_->Scene(),
            input.boundaries,
            session_->GetDocument().Snapshot().settings.tolerance);
        if (!regions.empty()) {
            for (const auto& region : regions) {
                auto added = AddRegionBoundary(std::move(table), region.outer,
                    ChainRole::OuterBoundary, session_->GetDocument());
                if (!added.HasValue()) {
                    return Out::Failure(added.Diagnostics());
                }
                table = added.Value();
                for (const auto& hole : region.holes) {
                    added = AddRegionBoundary(std::move(table), hole,
                        ChainRole::HoleBoundary, session_->GetDocument());
                    if (!added.HasValue()) {
                        return Out::Failure(added.Diagnostics());
                    }
                    table = added.Value();
                }
            }
            return Out::Success(std::move(table));
        }
    }
    const auto addRows = [&](ChainRole role,
                             const std::vector<kachakacha::v2::base::EntityId>& ids) {
        table = WithRowsAdded(std::move(table), role, ids);
    };
    if (input.method == GuideSurfaceMethod::OffsetGuide) {
        for (const auto& id : input.sourceSurfaces) {
            const auto* entity = session_->GetDocument().FindEntity(id);
            const auto added = kachakacha::v2::modeling::AddSourceSurfaceRow(table, id,
                entity == nullptr ? std::string("面") : entity->displayName);
            if (added.HasValue()) {
                table = added.Value();
            }
        }
    }
    // **画面の3つの欄を、1欄につき1回だけ表へ移す。**
    // 役割ごとに回すと、平面(外形+穴)や曲線網(U+V)で同じ線が二度入る。
    for (int index = 0; index < kachakacha::v2::app::kSurfaceSlotCount; ++index) {
        const ChainRole slot = kachakacha::v2::app::SurfaceSlotKey(index);
        ChainRole role = slot;
        if (!kachakacha::v2::app::RoleForSurfaceSlot(input.method, slot, role)) {
            continue;   // この作り方では使わない欄。入っていても渡さない。
        }
        if (slot == ChainRole::Section) {
            // **画面に出ている順のまま渡す**(§13 の手動固定)。
            addRows(role, kachakacha::v2::app::SurfaceSectionOrder(input));
            continue;
        }
        if (input.method == GuideSurfaceMethod::Revolve && slot == ChainRole::GuideU) {
            // 回転体の「軸」。表の行にはせず、軸の点と向きと角度へ直す(guide.revolve と同じ道)。
            const auto axis = RevolveAxisFromInput();
            if (!axis.HasValue()) {
                return Out::Failure(axis.Diagnostics());
            }
            table = WithRevolveAxis(std::move(table), axis.Value());
            continue;
        }
        if ((input.method == GuideSurfaceMethod::PlanarBoundary
                || input.method == GuideSurfaceMethod::BoundaryFill)
            && slot == ChainRole::BoundarySide && input.boundaries.size() > 1) {
            std::vector<kachakacha::v2::modeling::GuideTableSelection> selections;
            for (const auto& id : input.boundaries) {
                const auto selected = kachakacha::v2::app::GuideSelectionOf(
                    session_->GetDocument(), session_->Scene(), id);
                if (selected.has_value()) {
                    selections.push_back(*selected);
                }
            }
            const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
            // 境界面は、外周の輪と面が必ず通る線へ分ける(オーナー方針 2026-09-22)。
            const auto combined = input.method == GuideSurfaceMethod::BoundaryFill
                ? kachakacha::v2::app::AddBoundaryFillRows(table, selections, tolerance)
                : kachakacha::v2::app::AddSelectionsAsConnectedRow(table, role, selections,
                      tolerance);
            if (!combined.HasValue()) {
                return Out::Failure(combined.Diagnostics());
            }
            table = combined.Value();
            continue;
        }
        addRows(role, kachakacha::v2::app::SurfaceSlotEntries(input, slot));
    }
    if (table.rows.empty()) {
        return Out::Failure(kachakacha::v2::base::MakeError("UI-R004",
            "面を作るものが入っていません。", "断面か境界を選んで「追加」を押してください。"));
    }
    table = WithReversedRows(std::move(table), input);
    return Out::Success(std::move(table));
}

//! その線たちを、その役割の行として表へ足す。取れない線は黙って飛ばす(欄の状態が言う)。
kachakacha::v2::modeling::GuideTable V2MainWindow::WithRowsAdded(
    kachakacha::v2::modeling::GuideTable table, ChainRole role,
    const std::vector<kachakacha::v2::base::EntityId>& ids) const
{
    for (const auto& id : ids) {
        const auto chosen = kachakacha::v2::app::GuideSelectionOf(
            session_->GetDocument(), session_->Scene(), id);
        if (!chosen.has_value()) {
            continue;
        }
        const auto added = kachakacha::v2::modeling::AddSelectionAsNewRow(table, role, *chosen);
        if (added.HasValue()) {
            table = added.Value();
        }
    }
    return table;
}

kachakacha::v2::modeling::GuideTable V2MainWindow::WithRevolveAxis(
    kachakacha::v2::modeling::GuideTable table, const kachakacha::v2::app::RevolveRequest& axis)
{
    table.revolveAxisPoint = axis.axisPoint;
    table.revolveAxisDirection = axis.axisDirection;
    table.revolveAngleRad = axis.angleDeg * 3.14159265358979323846 / 180.0;
    return table;
}

//! 確定して面を作る。**下見に出した写しをそのまま使う**(§9 と同じ決まり)。
//!
//! ここで選び直すと、見ていた線と別の面が出来る。
//! 下見が出ていないなら、まだ何も見ていないということなので、作らない。
void V2MainWindow::ConfirmSurface()
{
    if (!surfaceSnapshot_.has_value()) {
        RefreshSurfacePreview();
        RefreshSurfaceRoleLabels();
        RefreshSurfaceDock();
        if (!surfaceSnapshot_.has_value()) {
            ReportSurfaceNotReady();
            return;
        }
    }
    const auto snapshot = *surfaceSnapshot_;
    std::vector<kachakacha::v2::base::EntityId> inputs;
    for (const auto& id : kachakacha::v2::app::SurfaceSectionOrder(surfaceInput_)) {
        inputs.push_back(id);
    }
    for (const auto& id : surfaceInput_.guides) {
        inputs.push_back(id);
    }
    for (const auto& id : surfaceInput_.boundaries) {
        inputs.push_back(id);
    }
    for (const auto& id : surfaceInput_.centerlines) {
        inputs.push_back(id);
    }
    // 一括は 1 回の取り消しで全部戻るようにまとめる(配列の道具と同じ)。
    const bool batch = !snapshot.batch.empty();
    if (batch) {
        session_->GetDocument().BeginCompound("面を作る(一括)");
    }
    auto surfaceId = AdoptGuideSurface(snapshot.table, snapshot.built, inputs, "面");
    for (const auto& [table, built] : snapshot.batch) {
        if (surfaceId.IsNil()) {
            break;
        }
        surfaceId = AdoptGuideSurface(table, built, inputs, "面");
    }
    if (batch) {
        // 1 つでも作れなければ全部取り消す。半分だけ作れたことにしない。
        if (surfaceId.IsNil()) {
            session_->GetDocument().AbortCompound();
        } else {
            session_->GetDocument().EndCompound();
        }
    }
    if (surfaceId.IsNil()) {
        return;
    }
    const auto method = surfaceInput_.method;
    EndSurfacePreview();
    SetStatus(QStringLiteral("面を作る: 「%1」で面を作りました。")
            .arg(QString::fromUtf8(
                std::string(kachakacha::v2::app::GuideSurfaceMethodLabelJa(method)).c_str())));
}

//! なぜ作れないかを言う。断り方は1か所にまとめる。
void V2MainWindow::ReportSurfaceNotReady()
{
    if (!kachakacha::v2::app::SurfaceReadyToBuild(surfaceInput_)) {
        SetStatus(QStringLiteral("面を作る: まだ作れません。右の棚の「4. 状態」を"
                                 "見てください。"));
        return;
    }
    // 入力は揃っているのに作れない。理由はカーネルが持っている。
    // ここで初めて診断を出す。入れている途中に出すと、何が本当の失敗か分からない。
    const auto table = SurfaceTableFromInput();
    if (!table.HasValue()) {
        ReportDiagnostics(table.Diagnostics());
        return;
    }
    // 作れれば下見が出るはずなので、ここへ来た時点で必ず断りが出る。
    static_cast<void>(BuildSurfaceFromTable(table.Value(), true));
}
