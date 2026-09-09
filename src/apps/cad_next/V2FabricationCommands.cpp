//! 製作のコマンド(V2MainWindow の一部)。製作モデルと型紙。
//!
//! 思想。**立体は「作れる形」とは限らない。**
//! 平らな面は板から切れる。曲がった面は展開が要る。
//! だから型紙にする前に「平らかどうか」を必ず調べる。
//! 調べずに近似すると、切ってから合わないことに気づくことになる。
//!
//! いま扱えるのは平らな部材だけである。曲がった面は断る。
//! 扱えるふりをしない。

#include "V2MainWindow.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/fabrication/PlanarPanel.h"

#include <string>
#include <utility>
#include <vector>

bool V2MainWindow::IsFabricationCommand(std::string_view id)
{
    return id.rfind("fabrication.", 0) == 0;
}

void V2MainWindow::RunFabricationCommand(std::string_view id)
{
    if (id == "fabrication.create") {
        RunFabricationCreate();
        return;
    }
    if (id == "fabrication.create_pattern") {
        RunCreatePattern();
        return;
    }
    if (id == "fabrication.preview_update") {
        RunFabricationCreate();
        return;
    }
    if (id == "fabrication.set_assembly") {
        // 組立状態は 0% / 30% / 100% を順ぐりに切り替える。
        // どの状態でも辺の長さは変わらない。変わるのは形だけである。
        const int steps[] = {0, 30, 100};
        for (std::size_t index = 0; index < std::size(steps); ++index) {
            if (assemblyPercent_ == steps[index]) {
                assemblyPercent_ = steps[(index + 1) % std::size(steps)];
                break;
            }
        }
        processContext_.fabricationBuilt = !fabricationPanels_.empty();
        SetProcessContext(processContext_);
        SetStatus(QStringLiteral("組立状態を %1%% にしました"
                                 "(どの状態でも辺の長さは変わりません)。")
                .arg(assemblyPercent_));
        return;
    }
    SetStatus(QStringLiteral("固定は、部品を選んでから「形」→「現在状態を固定」で行います。"));
}

void V2MainWindow::RunFabricationCreate()
{
    using kachakacha::v2::fabrication::BuildPlanarPanels;
    using kachakacha::v2::fabrication::PlanarPanelRequest;

    const auto& selection = viewport_->Selection();
    std::vector<PlanarPanelRequest> requests;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr
            || entity->kind != kachakacha::v2::domain::EntityKind::Part) {
            continue;
        }
        const auto found = partEdges_.find(id.ToString());
        if (found == partEdges_.end()) {
            continue;
        }
        PlanarPanelRequest request;
        request.panelId = entity->displayName;
        request.boundary = found->second;
        requests.push_back(std::move(request));
    }
    if (requests.empty()) {
        SetStatus(QStringLiteral(
            "製作モデルを作る: 先に部品を選んでください(押し出しで作ったものです)。"));
        return;
    }
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto panels = BuildPlanarPanels(requests, tolerance);
    if (!panels.HasValue()) {
        // 平らでない部材は断る。近似すると切ってから合わない。
        ReportDiagnostics(panels.Diagnostics());
        return;
    }
    fabricationPanels_ = panels.Value();
    processContext_.fabricationBuilt = true;
    processContext_.panelCount = static_cast<int>(fabricationPanels_.size());
    processContext_.patternBuilt = false;
    SetProcessContext(processContext_);
    SetStatus(QStringLiteral("製作モデルを作る: %1枚の部材にしました。すべて平らです。")
            .arg(static_cast<int>(fabricationPanels_.size())));
}

void V2MainWindow::RunCreatePattern()
{
    using kachakacha::v2::fabrication::LayoutPattern;
    using kachakacha::v2::fabrication::PaperSize;
    using kachakacha::v2::fabrication::PlacePanelCurves;

    if (fabricationPanels_.empty()) {
        SetStatus(QStringLiteral(
            "型紙を作る: 先に「製作モデルを作る」で部材にしてください。"));
        return;
    }
    PaperSize paper;
    const auto layout = LayoutPattern(fabricationPanels_, paper);
    if (!layout.HasValue()) {
        ReportDiagnostics(layout.Diagnostics());
        return;
    }
    // 置いた部材を、原寸のまま紙の上の曲線へ戻す。書き出しはこれを使う。
    patternPages_.clear();
    patternPages_.resize(static_cast<std::size_t>(std::max(1, layout.Value().pageCount)));
    for (auto& page : patternPages_) {
        page.widthMm = paper.widthMm;
        page.heightMm = paper.heightMm;
    }
    for (const auto& placement : layout.Value().placements) {
        for (const auto& panel : fabricationPanels_) {
            if (panel.panelId != placement.panelId) {
                continue;
            }
            const auto curves = PlacePanelCurves(panel, placement);
            if (!curves.HasValue()) {
                ReportDiagnostics(curves.Diagnostics());
                return;
            }
            const std::size_t page = static_cast<std::size_t>(
                std::max(0, placement.pageIndex));
            if (page >= patternPages_.size()) {
                continue;
            }
            for (const auto& curve : curves.Value()) {
                patternPages_[page].curves.push_back(curve);
            }
        }
    }
    processContext_.patternBuilt = true;
    SetProcessContext(processContext_);
    RefreshExportCounts();
    SetStatus(QStringLiteral("型紙を作る: A4 %1ページに %2枚を並べました(原寸)。")
            .arg(static_cast<int>(patternPages_.size()))
            .arg(static_cast<int>(fabricationPanels_.size())));
}
