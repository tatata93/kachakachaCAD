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
#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/fabrication/CurvedPanel.h"
#include "kachakacha/fabrication/PlanarPanel.h"
#include "kachakacha/geometry/WireChain.h"

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
    if (id == "fabrication.assign_role") {
        AssignOpeningRole();
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
    // 曲がった面(形状ガイド)は、展開してから部材にする。
    std::vector<kachakacha::v2::fabrication::PatternPanel> unfolded;
    if (!UnfoldSelectedSurfaces(unfolded)) {
        return;
    }
    std::vector<PlanarPanelRequest> requests;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr
            || entity->kind != kachakacha::v2::domain::EntityKind::Part) {
            continue;
        }
        // 立体の辺ではなく、平らな1枚の輪郭を使う。
        // 立体の辺には厚みのぶんの高さがあるので、平らにならない。
        const auto found = partFlatBoundary_.find(id.ToString());
        if (found == partFlatBoundary_.end()) {
            continue;
        }
        PlanarPanelRequest request;
        request.panelId = entity->displayName;
        request.boundary = found->second;
        requests.push_back(std::move(request));
    }
    if (requests.empty() && unfolded.empty()) {
        SetStatus(QStringLiteral(
            "製作モデルを作る: 平らな1枚を持つ部品か、形状ガイドを選んでください。"));
        return;
    }
    if (requests.empty()) {
        // 曲がった面だけを選んだとき。展開した部材をそのまま使う。
        fabricationPanels_ = unfolded;
        panelBoundary_.clear();
        panelOpenings_.clear();
        processContext_.fabricationBuilt = true;
        processContext_.panelCount = static_cast<int>(fabricationPanels_.size());
        processContext_.patternBuilt = false;
        patternPages_.clear();
        SetProcessContext(processContext_);
        SetStatus(QStringLiteral(
            "製作モデルを作る: 曲がった面を展開して %1枚の部材にしました。")
                .arg(static_cast<int>(fabricationPanels_.size())));
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
    // 展開した面があれば、そのまま足す。平らな部品と混ぜて1つの型紙にできる。
    for (const auto& panel : unfolded) {
        fabricationPanels_.push_back(panel);
    }
    // 元の輪郭を覚えておく。あとで開口を足すときに、ここから作り直す。
    panelBoundary_.clear();
    panelOpenings_.clear();
    for (const auto& request : requests) {
        panelBoundary_[request.panelId] = request.boundary;
        panelOpenings_[request.panelId] = request.openings;
    }
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

void V2MainWindow::AssignOpeningRole()
{
    using kachakacha::v2::fabrication::BuildPlanarPanels;
    using kachakacha::v2::fabrication::PlanarPanelRequest;

    // いまできる役割の割り当ては「開口」だけである。
    // 外周は部品の輪郭がそのままなので、手で決める必要がない。
    // 折り線と切れ目は、曲がった面を扱えるようになってから入れる。
    // 出来ないものを、出来るふりをして並べない。
    if (fabricationPanels_.empty()) {
        SetStatus(QStringLiteral(
            "境界の役割: 先に「製作モデルを作る」で部材にしてください。"));
        return;
    }
    const auto& selection = viewport_->Selection();
    std::vector<kachakacha::v2::geometry::CurveSegment> opening;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr
            || entity->kind != kachakacha::v2::domain::EntityKind::Wire) {
            continue;
        }
        for (const auto& curve : session_->Scene().curves) {
            if (curve.entityId == id) {
                opening.push_back(curve.segment);
            }
        }
    }
    if (opening.empty()) {
        SetStatus(QStringLiteral(
            "境界の役割: 開口にしたい線を選んでください(窓など)。"));
        return;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    if (!kachakacha::v2::geometry::SegmentsFormClosedLoop(opening, tolerance)) {
        // 開いた線は穴にならない。開いたまま切ると、板が2つに割れる。
        SetStatus(QStringLiteral(
            "境界の役割: 開口は閉じた輪でなければなりません。線がつながっていません。"));
        return;
    }
    // 覚えてある元の輪郭から作り直す。前の結果へ足すと、
    // 押すたびに開口が増えていってしまう。
    std::vector<PlanarPanelRequest> requests;
    for (const auto& panel : fabricationPanels_) {
        PlanarPanelRequest request;
        request.panelId = panel.panelId;
        const auto found = panelBoundary_.find(panel.panelId);
        if (found == panelBoundary_.end()) {
            continue;
        }
        request.boundary = found->second;
        request.openings = panelOpenings_[panel.panelId];
        requests.push_back(std::move(request));
    }
    if (requests.empty()) {
        SetStatus(QStringLiteral("境界の役割: 元の輪郭が見つかりません。"));
        return;
    }
    // どの部材のものかは、外周と同じ平面に載っているかで決まる。
    // 窓は、それが描かれている壁のものである。人に選ばせる必要はない。
    const auto chosen = kachakacha::v2::fabrication::PanelForOpening(requests, opening,
        tolerance.interactiveJoinMm);
    if (!chosen.has_value()) {
        // 近いほうへ寄せない。寄せると、頼んでいない壁に穴が開く。
        SetStatus(QStringLiteral(
            "境界の役割: その線は、どの部材の面にも載っていません。"
            "部材と同じ平面の上に描いてください。"));
        return;
    }
    requests[*chosen].openings.push_back(opening);
    const auto rebuilt = BuildPlanarPanels(requests, tolerance.interactiveJoinMm);
    if (!rebuilt.HasValue()) {
        ReportDiagnostics(rebuilt.Diagnostics());
        return;
    }
    panelOpenings_[requests[*chosen].panelId] = requests[*chosen].openings;
    fabricationPanels_ = rebuilt.Value();
    processContext_.patternBuilt = false;
    patternPages_.clear();
    SetProcessContext(processContext_);
    SetStatus(QStringLiteral(
        "境界の役割: %1 に開口を1つ入れました(いま%2つ)。"
        "型紙はもう一度作ってください。")
            .arg(QString::fromStdString(requests[*chosen].panelId))
            .arg(static_cast<int>(requests[*chosen].openings.size())));
}

bool V2MainWindow::UnfoldSelectedSurfaces(
    std::vector<kachakacha::v2::fabrication::PatternPanel>& into)
{
    using kachakacha::v2::fabrication::BuildCurvedPanel;

    // 許すずれは「数」の棚から取る。ここが答えを決めるので、
    // 決め打ちにすると、通るか通らないかを人が選べない。
    const double allowed = kachakacha::v2::app::ParameterValueOf(
        parameterDock_->Values(), kachakacha::v2::app::ParameterId::MaxDeviationMm);
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr
            || entity->kind != kachakacha::v2::domain::EntityKind::GuideSurface) {
            continue;
        }
        const auto found = guideSamples_.find(id.ToString());
        if (found == guideSamples_.end()) {
            continue;
        }
        const auto made = BuildCurvedPanel(entity->displayName, found->second, allowed);
        if (!made.HasValue()) {
            // 伸ばさずには平らにできない面は断る。近い形へ均して成功にしない。
            ReportDiagnostics(made.Diagnostics());
            return false;
        }
        into.push_back(made.Value().panel);
    }
    return true;
}
