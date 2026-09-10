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

#include <algorithm>
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
        // 組立率を聞いて、文書の作り方へ書く。V1 と同じで、形が実際に曲がる。
        // これまでは 0/30/100 を順ぐりに変えるだけで、形が動かなかった。
        const auto modelId = CurrentFabricationModelId();
        if (modelId.IsNil()) {
            SetStatus(QStringLiteral(
                "組立状態: 先に「製作モデルを作る」で近似モデルを作ってください。"));
            return;
        }
        double current = 100.0;
        const auto* entity = session_->GetDocument().FindEntity(modelId);
        const auto* feature = entity == nullptr
            ? nullptr
            : session_->GetDocument().FindFeature(entity->createdBy);
        if (feature != nullptr) {
            if (const auto* definition =
                    std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
                        &feature->definition)) {
                current = definition->masterPercent;
            }
        }
        if (assemblyChooser_) {
            const auto answered = assemblyChooser_(current);
            if (!answered.has_value()) {
                SetStatus(QStringLiteral("組立状態: やめました。"));
                return;
            }
            current = *answered;
        }
        SetAssemblyPercent(current);
        return;
    }
    if (id == "fabrication.set_method") {
        // 次に作る近似モデルの方式を切り替える。両方式を残して選べるようにする。
        fabricationMethod_ = fabricationMethod_
                == kachakacha::v2::app::FabricationMethod::BandApproximation
            ? kachakacha::v2::app::FabricationMethod::ClassifyFaces
            : kachakacha::v2::app::FabricationMethod::BandApproximation;
        SetStatus(QStringLiteral("近似の方式: %1")
                .arg(QString::fromUtf8(std::string(
                    kachakacha::v2::app::FabricationMethodNameJa(fabricationMethod_))
                                           .c_str())));
        return;
    }
    SetStatus(QStringLiteral("固定は、部品を選んでから「形」→「現在状態を固定」で行います。"));
}

void V2MainWindow::RunFabricationCreate()
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::CreateFabricationModelDefinition;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    // 近似モデルは文書のものにする。これまでは画面の配列に置くだけで、
    // 保存すると消えていた。作り方を文書に入れ、開いたら作り直す。
    const auto& selection = viewport_->Selection();
    const auto sources = FabricationSourcesFor(selection.entityIds);
    if (sources.empty()) {
        SetStatus(QStringLiteral(
            "製作モデルを作る: 平らな1枚を持つ部品か、形状ガイドを選んでください。"));
        return;
    }
    CreateFabricationModelDefinition definition;
    for (const auto& source : sources) {
        definition.parts.push_back(source.entityId);
    }
    definition.method = static_cast<int>(fabricationMethod_);
    definition.targetMaxDeviation.value = kachakacha::v2::app::ParameterValueOf(
        parameterDock_->Values(), kachakacha::v2::app::ParameterId::MaxDeviationMm);
    definition.targetMaxDeviation.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.materialThickness.value = ExtrudeDistanceMm();
    definition.materialThickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto evaluated =
        kachakacha::v2::app::EvaluateFabrication(definition, sources, tolerance);
    if (!evaluated.HasValue()) {
        ReportDiagnostics(evaluated.Diagnostics());
        return;
    }
    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::CreateFabricationModel;
    feature.displayName = "近似モデル";
    feature.inputEntityIds = definition.parts;
    feature.definition = definition;
    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::FabricationModel;
    entity.displayName = "近似モデル";
    entity.createdBy = feature.id;
    feature.outputs.push_back(
        FeatureOutput{"fabrication", entity.id, EntityKind::FabricationModel});
    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, "製作モデルを作る"));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    fabricationModels_[entity.id.ToString()] = evaluated.Value();
    AdoptCurrentDocument();
    RefreshFabricationView();
    SetStatus(QStringLiteral("製作モデルを作る: %1")
            .arg(QString::fromStdString(evaluated.Value().summaryJa)));
}

std::vector<kachakacha::v2::app::FabricationSource> V2MainWindow::FabricationSourcesFor(
    const std::vector<kachakacha::v2::base::EntityId>& ids) const
{
    std::vector<kachakacha::v2::app::FabricationSource> sources;
    for (const auto& id : ids) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        kachakacha::v2::app::FabricationSource source;
        source.entityId = id;
        source.name = entity->displayName.empty() ? std::string("面") : entity->displayName;
        if (entity->kind == kachakacha::v2::domain::EntityKind::GuideSurface) {
            const auto found = guideSamples_.find(id.ToString());
            if (found != guideSamples_.end()) {
                source.samples = found->second;
                sources.push_back(std::move(source));
            }
            continue;
        }
        if (entity->kind == kachakacha::v2::domain::EntityKind::Part) {
            // 立体の辺ではなく、平らな1枚の輪郭を使う。
            // 立体の辺には厚みのぶんの高さがあるので、平らにならない。
            const auto found = partFlatBoundary_.find(id.ToString());
            if (found != partFlatBoundary_.end()) {
                source.flatBoundary = found->second;
                sources.push_back(std::move(source));
            }
        }
    }
    return sources;
}

bool V2MainWindow::RebuildFabricationModel(const kachakacha::v2::domain::Feature& feature,
    const kachakacha::v2::base::EntityId& output)
{
    const auto* definition =
        std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
            &feature.definition);
    if (definition == nullptr) {
        return false;
    }
    const auto sources = FabricationSourcesFor(definition->parts);
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto evaluated =
        kachakacha::v2::app::EvaluateFabrication(*definition, sources, tolerance);
    if (!evaluated.HasValue()) {
        return false;
    }
    fabricationModels_[output.ToString()] = evaluated.Value();
    return true;
}

kachakacha::v2::base::EntityId V2MainWindow::CurrentFabricationModelId() const
{
    // 選んでいればそれ。選んでいなければ、文書にある最後の近似モデル。
    for (const auto& id : viewport_->Selection().entityIds) {
        if (fabricationModels_.count(id.ToString()) != 0) {
            return id;
        }
    }
    kachakacha::v2::base::EntityId last;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::FabricationModel
            && fabricationModels_.count(entity.id.ToString()) != 0) {
            last = entity.id;
        }
    }
    return last;
}

void V2MainWindow::RefreshFabricationView()
{
    // 全近似モデルの部材を並べ直す。型紙はこの並びから作る。
    fabricationPanels_.clear();
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> rails;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind != kachakacha::v2::domain::EntityKind::FabricationModel) {
            continue;
        }
        const auto found = fabricationModels_.find(entity.id.ToString());
        if (found == fabricationModels_.end()) {
            continue;
        }
        for (const auto& panel : found->second.panels) {
            fabricationPanels_.push_back(panel);
        }
        const auto* feature = session_->GetDocument().FindFeature(entity.createdBy);
        if (feature == nullptr || entity.visibility != kachakacha::v2::domain::Visibility::Visible) {
            continue;
        }
        if (const auto* definition =
                std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
                    &feature->definition)) {
            // いまの曲げ状態での姿勢。画面のプレビューと固定・出力を同じ道にする。
            // V1 で、プレビューと出力を別の作り方にして食い違った教訓である。
            for (auto& rail : kachakacha::v2::app::FoldedRailsOf(*definition,
                     found->second, 8.0)) {
                rails.push_back(std::move(rail));
            }
        }
    }
    viewport_->SetFoldPreview(std::move(rails));
    processContext_.fabricationBuilt = !fabricationPanels_.empty();
    processContext_.panelCount = static_cast<int>(fabricationPanels_.size());
    processContext_.patternBuilt = !patternPages_.empty();
    SetProcessContext(processContext_);
    RefreshEntityList();
    viewport_->update();
}

void V2MainWindow::SetAssemblyPercent(double percent)
{
    using kachakacha::v2::document::UpdateFeatureDefinitionCommand;
    const auto modelId = CurrentFabricationModelId();
    const auto* entity = session_->GetDocument().FindEntity(modelId);
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    if (feature == nullptr) {
        return;
    }
    const auto* current =
        std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
            &feature->definition);
    if (current == nullptr) {
        return;
    }
    auto definition = *current;
    definition.masterPercent = std::clamp(percent, 0.0, 100.0);
    // 個別値は master を変えたら捨てる。V1 と同じ(個別 override が無い折り線だけ更新、
    // ではなく、全体を動かしたら全体に従う)。個別に戻したいときは改めて指定する。
    definition.creaseProgress.clear();
    definition.bandProgress.clear();
    const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
        feature->id, definition, feature->inputEntityIds, "組立状態を変える"));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    RefreshFabricationView();
    SetStatus(QStringLiteral("組立状態を %1%% にしました(%2)。")
            .arg(definition.masterPercent)
            .arg(QString::fromStdString(
                kachakacha::v2::app::FoldStateSummaryJa(definition))));
}

void V2MainWindow::SetAssemblyChooser(
    std::function<std::optional<double>(double current)> chooser)
{
    assemblyChooser_ = std::move(chooser);
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

    // 役割は選ばせない。線の形で決まる。
    //   閉じた輪 → 開口(窓)。切り抜く。
    //   閉じていない線 → 折り線。折るだけで切らない。
    // 外周は部品の輪郭がそのままなので、手で決める必要がない。
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
    const bool closed = kachakacha::v2::geometry::SegmentsFormClosedLoop(opening,
        tolerance);
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
        request.folds = panelFolds_[panel.panelId];
        request.foldIsMountain.assign(request.folds.size(), true);
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
    if (closed) {
        requests[*chosen].openings.push_back(opening);
    } else {
        // 折り線は切らない。切ると、折るところで板が分かれてしまう。
        requests[*chosen].folds.push_back(opening);
        requests[*chosen].foldIsMountain.push_back(true);
    }
    const auto rebuilt = BuildPlanarPanels(requests, tolerance.interactiveJoinMm);
    if (!rebuilt.HasValue()) {
        ReportDiagnostics(rebuilt.Diagnostics());
        return;
    }
    panelOpenings_[requests[*chosen].panelId] = requests[*chosen].openings;
    panelFolds_[requests[*chosen].panelId] = requests[*chosen].folds;
    fabricationPanels_ = rebuilt.Value();
    processContext_.patternBuilt = false;
    patternPages_.clear();
    SetProcessContext(processContext_);
    SetStatus(closed
            ? QStringLiteral("境界の役割: %1 に開口を1つ入れました(いま%2つ)。"
                             "型紙はもう一度作ってください。")
                  .arg(QString::fromStdString(requests[*chosen].panelId))
                  .arg(static_cast<int>(requests[*chosen].openings.size()))
            : QStringLiteral("境界の役割: %1 に折り線を1本入れました(いま%2本)。"
                             "折り線は切りません。型紙はもう一度作ってください。")
                  .arg(QString::fromStdString(requests[*chosen].panelId))
                  .arg(static_cast<int>(requests[*chosen].folds.size())));
}

