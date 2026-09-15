//! 「面を作る」(オーナー指示 2026-09-15 §10〜§13、UI の正本)。
//!
//! 人が使う入口はこれ1つ。中では今までの `GuideTable` をそのまま使う。
//! これまでは「形状ガイド」と役割表の2本立てで、人から見て別物に見えていた。
//! しかも役割表は後ろの札にあり、**いまの作り方がどこにも出ていなかった。**

#include "V2MainWindow.h"

#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QString>

#include <algorithm>
#include <string>
#include <vector>

using kachakacha::v2::app::SurfaceOrdering;
using kachakacha::v2::app::SurfaceSlotState;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;

//! 選んだものから分かる事実。作り方を薦めるのに使う。
kachakacha::v2::app::SurfaceSelectionFacts V2MainWindow::SurfaceFactsNow() const
{
    kachakacha::v2::app::SurfaceSelectionFacts facts;
    const auto& document = session_->GetDocument();
    const auto& tolerance = document.Snapshot().settings.tolerance;
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
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(id);
        const auto curves = kachakacha::v2::app::SelectedCurves(one, session_->Scene());
        if (curves.empty()) {
            continue;
        }
        const bool closed = kachakacha::v2::geometry::SegmentsFormClosedLoop(curves,
            tolerance);
        if (!closed) {
            ++facts.openWires;
            continue;
        }
        ++facts.closedWires;
        // 平面に載っているか。**閉じた同一平面の輪郭1本は平面を薦める。**
        std::vector<kachakacha::v2::geometry::Vector3> points;
        for (const auto& curve : curves) {
            for (int step = 0; step <= 8; ++step) {
                points.push_back(curve.Evaluate(static_cast<double>(step) / 8.0));
            }
        }
        const auto plane = kachakacha::v2::geometry::FitPlane(points);
        if (plane.valid) {
            ++facts.closedPlanarWires;
        }
    }
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
    std::vector<QString> names;
    for (const auto& id : kachakacha::v2::app::SurfaceSectionOrder(surfaceInput_)) {
        const auto* entity = document.FindEntity(id);
        names.push_back(entity != nullptr && !entity->displayName.empty()
                ? QString::fromStdString(entity->displayName)
                : QStringLiteral("名前のないもの"));
    }
    surfaceDock_->ShowInput(surfaceInput_, names, false);
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
        // 取り込み先は、薦めた作り方が使う役割のうち、線を入れる先。
        const ChainRole into = surfaceInput_.method == GuideSurfaceMethod::PlanarBoundary
            ? ChainRole::OuterBoundary
            : (surfaceInput_.method == GuideSurfaceMethod::BoundaryFill
                      ? ChainRole::BoundarySide
                      : (surfaceInput_.method == GuideSurfaceMethod::OffsetGuide
                                ? ChainRole::SourceSurface
                                : ChainRole::Section));
        AddSelectionToSurfaceSlot(into);
        surfaceShelfShown_ = true;
        RefreshRightShelves();
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
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: 作り方を「%1」にしました。"
                             "入れたものはそのまま残っています。")
            .arg(QString::fromUtf8(
                std::string(kachakacha::v2::app::GuideSurfaceMethodLabelJa(method)).c_str())));
}

void V2MainWindow::ChooseSurfaceOrdering(SurfaceOrdering ordering)
{
    surfaceInput_.ordering = ordering;
    if (ordering == SurfaceOrdering::ManualLock && surfaceInput_.explicitOrder.empty()) {
        // 手動固定にした瞬間の並びを、そのまま固定する。
        surfaceInput_.explicitOrder = surfaceInput_.sections;
    }
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
    RefreshSurfaceDock();
}

//! 入力をやり直す。作り方は残す。
void V2MainWindow::ResetSurfaceInput()
{
    const auto method = surfaceInput_.method;
    const bool chosen = surfaceInput_.methodChosenByUser;
    surfaceInput_ = kachakacha::v2::app::SurfaceInputState{};
    surfaceInput_.method = method;
    surfaceInput_.methodChosenByUser = chosen;
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("面を作る: 入力を空にしました。作り方はそのままです。"));
}

void V2MainWindow::EndSurfacePreview()
{
    surfaceShelfShown_ = false;
    RefreshRightShelves();
}

//! 入力から `GuideTable` を組み立てる。**作る直前の1回だけ。**
//!
//! 役割は、いまの作り方が使うものだけを入れる。
//! こうすると「方式を変える前に要らない行を自分で消す」が要らなくなる
//! (人向けの UI-R003 を無くす。内部の診断はそのまま残る)。
kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>
V2MainWindow::SurfaceTableFromInput() const
{
    using Out = kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideTable>;
    kachakacha::v2::modeling::GuideTable table;
    table.method = surfaceInput_.method;
    const auto& used = kachakacha::v2::modeling::RolesForMethod(surfaceInput_.method);
    const auto addRows = [&](ChainRole role,
                             const std::vector<kachakacha::v2::base::EntityId>& ids) {
        for (const auto& id : ids) {
            const auto chosen = kachakacha::v2::app::GuideSelectionOf(
                session_->GetDocument(), session_->Scene(), id);
            if (!chosen.has_value()) {
                continue;
            }
            const auto added = kachakacha::v2::modeling::AddSelectionAsNewRow(table, role,
                *chosen);
            if (added.HasValue()) {
                table = added.Value();
            }
        }
    };
    for (const ChainRole role : used) {
        if (role == ChainRole::SourceSurface) {
            for (const auto& id : surfaceInput_.sourceSurfaces) {
                const auto* entity = session_->GetDocument().FindEntity(id);
                const auto added = kachakacha::v2::modeling::AddSourceSurfaceRow(table, id,
                    entity == nullptr ? std::string("面") : entity->displayName);
                if (added.HasValue()) {
                    table = added.Value();
                }
            }
            continue;
        }
        if (role == ChainRole::Section) {
            // **画面に出ている順のまま渡す**(§13 の手動固定)。
            addRows(role, kachakacha::v2::app::SurfaceSectionOrder(surfaceInput_));
            continue;
        }
        addRows(role, kachakacha::v2::app::SurfaceSlotEntries(surfaceInput_, role));
    }
    if (table.rows.empty()) {
        return Out::Failure(kachakacha::v2::base::MakeError("UI-R004",
            "面を作るものが入っていません。", "断面か境界を選んで「追加」を押してください。"));
    }
    return Out::Success(std::move(table));
}

//! 確定して面を作る。
void V2MainWindow::ConfirmSurface()
{
    if (!kachakacha::v2::app::SurfaceReadyToBuild(surfaceInput_)) {
        RefreshSurfaceDock();
        SetStatus(QStringLiteral("面を作る: まだ作れません。右の棚の「4. 状態」を"
                                 "見てください。"));
        return;
    }
    const auto table = SurfaceTableFromInput();
    if (!table.HasValue()) {
        ReportDiagnostics(table.Diagnostics());
        return;
    }
    const auto built = BuildSurfaceFromTable(table.Value(), true);
    if (!built.has_value()) {
        return;   // 理由はそちらで言っている。
    }
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
    const auto surfaceId = AdoptGuideSurface(table.Value(), *built, inputs, "面");
    if (surfaceId.IsNil()) {
        return;
    }
    EndSurfacePreview();
    SetStatus(QStringLiteral("面を作る: 「%1」で面を作りました。")
            .arg(QString::fromUtf8(std::string(
                kachakacha::v2::app::GuideSurfaceMethodLabelJa(surfaceInput_.method))
                    .c_str())));
}
