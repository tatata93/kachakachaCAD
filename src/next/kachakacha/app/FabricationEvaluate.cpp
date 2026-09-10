#include "kachakacha/app/FabricationEvaluate.h"

#include "kachakacha/fabrication/CurvedPanel.h"
#include "kachakacha/fabrication/PlanarPanel.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;
using fabrication::BandMesh;
using fabrication::PatternPanel;

[[nodiscard]] std::string Rounded(double value)
{
    const double snapped = std::round(value * 1000.0) / 1000.0;
    std::string text = std::to_string(snapped);
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

//! 平らな部材を作る。開口と折り線は、外周と同じ平面に載っている部材へ入れる。
//! 窓は、それが描かれている壁のものである。人に選ばせない。近いほうへ寄せない。
[[nodiscard]] Result<std::vector<PatternPanel>> BuildPlanarWithMarkings(
    std::vector<fabrication::PlanarPanelRequest> planar, const FabricationMarkings& markings,
    double toleranceMm)
{
    using Out = Result<std::vector<PatternPanel>>;
    for (const auto& opening : markings.openings) {
        const auto chosen = fabrication::PanelForOpening(planar, opening, toleranceMm);
        if (!chosen.has_value()) {
            return Out::Failure(MakeError("FAB-M003",
                "その線は、どの部材の面にも載っていません。",
                "開口や折り線は、部材と同じ平面の上に描いてください。"));
        }
        planar[*chosen].openings.push_back(opening);
    }
    for (const auto& fold : markings.folds) {
        const auto chosen = fabrication::PanelForOpening(planar, fold, toleranceMm);
        if (!chosen.has_value()) {
            return Out::Failure(MakeError("FAB-M003",
                "その線は、どの部材の面にも載っていません。",
                "開口や折り線は、部材と同じ平面の上に描いてください。"));
        }
        // 折り線は切らない。切ると、折るところで板が分かれてしまう。
        planar[*chosen].folds.push_back(fold);
        planar[*chosen].foldIsMountain.push_back(true);
    }
    return fabrication::BuildPlanarPanels(planar, toleranceMm);
}

//! V2 方式: 面ごとに「伸ばさずに平らにできるか」を検査して展開する。
[[nodiscard]] Result<FabricationEvaluation> EvaluateByClassification(
    const domain::CreateFabricationModelDefinition& definition,
    const std::vector<FabricationSource>& sources, const FabricationMarkings& markings,
    double toleranceMm)
{
    using Out = Result<FabricationEvaluation>;
    FabricationEvaluation made;
    made.method = FabricationMethod::ClassifyFaces;
    std::vector<fabrication::PlanarPanelRequest> planar;
    for (const FabricationSource& source : sources) {
        if (source.samples.has_value()) {
            const auto panel = fabrication::BuildCurvedPanel(source.name, *source.samples,
                definition.targetMaxDeviation.value);
            if (!panel.HasValue()) {
                // 伸ばさずには平らにできない面は断る。近い形へ均して成功にしない。
                // 均したいなら、V1 方式(帯近似)を選ぶ。
                return Out::Failure(panel.Diagnostics());
            }
            made.panels.push_back(panel.Value().panel);
            made.maximumDeviationMm =
                std::max(made.maximumDeviationMm, panel.Value().distortionMm);
            continue;
        }
        if (source.flatBoundary.has_value()) {
            fabrication::PlanarPanelRequest request;
            request.panelId = source.name;
            request.boundary = *source.flatBoundary;
            planar.push_back(std::move(request));
        }
    }
    if (!planar.empty()) {
        const auto panels = BuildPlanarWithMarkings(std::move(planar), markings, toleranceMm);
        if (!panels.HasValue()) {
            return Out::Failure(panels.Diagnostics());
        }
        for (const auto& panel : panels.Value()) {
            made.panels.push_back(panel);
        }
    }
    made.reachedTolerance = true;
    made.summaryJa = "面を分類して " + std::to_string(made.panels.size()) + " 枚の部材にしました。";
    return Out::Success(std::move(made));
}

//! V1 方式: 面を帯へ近似し直す。
[[nodiscard]] Result<FabricationEvaluation> EvaluateByBands(
    const domain::CreateFabricationModelDefinition& definition,
    const std::vector<FabricationSource>& sources, const FabricationMarkings& markings,
    double toleranceMm)
{
    using Out = Result<FabricationEvaluation>;
    FabricationEvaluation made;
    made.method = FabricationMethod::BandApproximation;
    std::vector<fabrication::PlanarPanelRequest> planar;
    for (const FabricationSource& source : sources) {
        if (source.samples.has_value()) {
            const fabrication::SampledSurface surface(*source.samples);
            fabrication::BandApproximationOptions options = BandOptionsOf(definition);
            if (definition.splitAxis == 2) {
                // 自動: 曲がっている方向を横切るように切る。
                options.splitAxis = fabrication::ChooseSplitAxis(surface);
            }
            const auto bands = fabrication::ApproximateBands(surface, options);
            if (!bands.HasValue()) {
                return Out::Failure(bands.Diagnostics());
            }
            const auto mesh = fabrication::DevelopBandMesh(surface, options.splitAxis,
                bands.Value().railParameters, 96);
            if (!mesh.HasValue()) {
                return Out::Failure(mesh.Diagnostics());
            }
            const auto angles = fabrication::MeasureCreaseAngles(mesh.Value());
            for (auto& panel : PanelsFromBandMesh(source.name, mesh.Value(), angles)) {
                made.panels.push_back(std::move(panel));
            }
            made.maximumDeviationMm =
                std::max(made.maximumDeviationMm, bands.Value().maximumDeviationMm);
            made.reachedTolerance =
                made.reachedTolerance && bands.Value().reachedRequestedTolerance;
            // 帯メッシュは1面ぶんだけ覚える。複数の面を1つの近似モデルにするときは、
            // 面ごとに近似モデルを作る(曲げ状態は面ごとに別のものなので)。
            if (!made.bandMesh.has_value()) {
                made.bands = bands.Value();
                made.bandMesh = mesh.Value();
            }
            continue;
        }
        if (source.flatBoundary.has_value()) {
            fabrication::PlanarPanelRequest request;
            request.panelId = source.name;
            request.boundary = *source.flatBoundary;
            planar.push_back(std::move(request));
        }
    }
    if (!planar.empty()) {
        const auto panels = BuildPlanarWithMarkings(std::move(planar), markings, toleranceMm);
        if (!panels.HasValue()) {
            return Out::Failure(panels.Diagnostics());
        }
        for (const auto& panel : panels.Value()) {
            made.panels.push_back(panel);
        }
    }
    made.summaryJa = "帯へ近似して " + std::to_string(made.panels.size())
        + " 枚の部材にしました。ずれは最大 " + Rounded(made.maximumDeviationMm) + " mm"
        + (made.reachedTolerance ? "(許容内)" : "(許容を超えています)") + "。";
    return Out::Success(std::move(made));
}

} // namespace

std::string_view FabricationMethodNameJa(FabricationMethod method) noexcept
{
    switch (method) {
    case FabricationMethod::ClassifyFaces:     return "面を分類して展開(V2方式)";
    case FabricationMethod::BandApproximation: return "帯へ近似し直す(V1方式)";
    }
    return "不明";
}

FabricationMethod FabricationMethodOf(
    const domain::CreateFabricationModelDefinition& definition) noexcept
{
    return definition.method == 1 ? FabricationMethod::BandApproximation
                                  : FabricationMethod::ClassifyFaces;
}

fabrication::BandApproximationOptions BandOptionsOf(
    const domain::CreateFabricationModelDefinition& definition)
{
    fabrication::BandApproximationOptions options;
    // 2(自動)のときの実際の軸は、面ごとに EvaluateFabrication が決める。ここでは V にしておく。
    options.splitAxis = definition.splitAxis == 0 ? fabrication::BandSplitAxis::U
                                                  : fabrication::BandSplitAxis::V;
    options.automaticBoundaries = definition.automaticBoundaries;
    options.maximumDeviationMm = definition.targetMaxDeviation.value;
    options.maximumPartCount = definition.maximumPartCount;
    options.minimumPartWidthMm = definition.minimumPartWidthMm;
    options.manualBoundaries = definition.manualBoundaries;
    return options;
}

Result<FabricationEvaluation> EvaluateFabrication(
    const domain::CreateFabricationModelDefinition& definition,
    const std::vector<FabricationSource>& sources, const FabricationMarkings& markings,
    double toleranceMm)
{
    using Out = Result<FabricationEvaluation>;
    if (sources.empty()) {
        return Out::Failure(MakeError("FAB-M001", "近似する元がありません。",
            "平らな1枚を持つ部品か、形状ガイドを選んでください。"));
    }
    if (!(definition.targetMaxDeviation.value > 0.0)) {
        return Out::Failure(MakeError("FAB-M002", "許すずれが正の数ではありません。",
            "どこまでのずれなら許すかを決めてください。"));
    }
    return FabricationMethodOf(definition) == FabricationMethod::BandApproximation
        ? EvaluateByBands(definition, sources, markings, toleranceMm)
        : EvaluateByClassification(definition, sources, markings, toleranceMm);
}

std::vector<PatternPanel> PanelsFromBandMesh(const std::string& baseName,
    const BandMesh& mesh, const std::vector<double>& creaseAnglesRad)
{
    std::vector<PatternPanel> panels;
    for (int band = 0; band < mesh.BandCount(); ++band) {
        const auto& bottom = mesh.developed[static_cast<std::size_t>(band)];
        const auto& top = mesh.developed[static_cast<std::size_t>(band) + 1];
        PatternPanel panel;
        panel.panelId = baseName + " 部材" + std::to_string(band + 1);
        // 外周: 下レールを順に、上レールを逆に。閉じた輪になる。
        panel.outline.insert(panel.outline.end(), bottom.begin(), bottom.end());
        panel.outline.insert(panel.outline.end(), top.rbegin(), top.rend());
        // 隣の帯との境目は折り線。上レールがそれに当たる(最後の帯には無い)。
        if (band + 1 < mesh.BandCount()) {
            PatternPanel::Fold fold;
            fold.foldId = panel.panelId + " 折り線";
            fold.path = top;
            const std::size_t rail = static_cast<std::size_t>(band);
            const int sense = rail < mesh.creaseDirections.size()
                ? mesh.creaseDirections[rail]
                : 0;
            fold.sense = sense < 0 ? fabrication::FoldSense::Valley
                                   : fabrication::FoldSense::Mountain;
            fold.angleRad = rail < creaseAnglesRad.size() ? creaseAnglesRad[rail] : 0.0;
            panel.folds.push_back(std::move(fold));
        }
        panels.push_back(std::move(panel));
    }
    return panels;
}

ResolvedFoldState ResolveFoldState(
    const domain::CreateFabricationModelDefinition& definition, const BandMesh& mesh)
{
    ResolvedFoldState state;
    state.masterProgress = std::clamp(definition.masterPercent, 0.0, 100.0) / 100.0;
    // 個別値が空、または数が合わなければ master で埋める。
    // 帯数は再近似で変わりうるので、古い個別値を理由に開けなくしない。
    const std::size_t creases = static_cast<std::size_t>(mesh.CreaseCount());
    const std::size_t bandCount = static_cast<std::size_t>(mesh.BandCount());
    state.creaseProgress.assign(creases, state.masterProgress);
    if (definition.creaseProgress.size() == creases) {
        for (std::size_t index = 0; index < creases; ++index) {
            state.creaseProgress[index] =
                std::clamp(definition.creaseProgress[index], 0.0, 1.0);
        }
    }
    state.bandProgress.assign(bandCount, state.masterProgress);
    if (definition.bandProgress.size() == bandCount) {
        for (std::size_t index = 0; index < bandCount; ++index) {
            state.bandProgress[index] = std::clamp(definition.bandProgress[index], 0.0, 1.0);
        }
    }
    return state;
}

std::vector<std::vector<geometry::Vector3>> FoldedRailsOf(
    const domain::CreateFabricationModelDefinition& definition,
    const FabricationEvaluation& evaluation, double liftMm)
{
    if (!evaluation.bandMesh.has_value()) {
        return {};
    }
    const ResolvedFoldState state = ResolveFoldState(definition, *evaluation.bandMesh);
    // 折り線の個別値は master との比で持ち、帯の進行度でさらに補間される。
    // V1 と同じ扱い: creaseProgress は「1 = 完成形の折り角」で、帯の t が掛かる。
    std::vector<double> creaseRelative(state.creaseProgress.size(), 1.0);
    for (std::size_t index = 0; index < state.creaseProgress.size(); ++index) {
        creaseRelative[index] = state.masterProgress > 1.0e-9
            ? std::min(1.0, state.creaseProgress[index] / state.masterProgress)
            : 1.0;
    }
    const auto rails = fabrication::BuildBandFoldRails(*evaluation.bandMesh,
        creaseRelative, state.bandProgress, liftMm);
    return rails.HasValue() ? rails.Value() : std::vector<std::vector<geometry::Vector3>>{};
}

std::string FoldStateSummaryJa(const domain::CreateFabricationModelDefinition& definition)
{
    std::string text = "組立 " + Rounded(definition.masterPercent) + "%";
    if (!definition.bandProgress.empty()) {
        text += "(帯ごとに個別指定)";
    } else if (!definition.creaseProgress.empty()) {
        text += "(折り線ごとに個別指定)";
    }
    return text;
}

} // namespace kachakacha::v2::app
