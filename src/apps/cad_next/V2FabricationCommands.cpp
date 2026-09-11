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

#include "kachakacha/app/FabricationOptions.h"

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
    // 「現在状態を固定」は固定の側(V2FreezeCommands.cpp)が受ける。
    // ここで先に取ると、固定へ届かず「固定は部品を選んでから」と返してしまう。
    return id.rfind("fabrication.", 0) == 0 && !IsFreezeCommand(id);
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
        AssignOpeningRole(false);
        return;
    }
    if (id == "fabrication.assign_relief_cut") {
        AssignOpeningRole(true);
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
    if (id == "fabrication.set_connection_scope") {
        SetConnectionScope();
        return;
    }
    if (id == "fabrication.freeze_output") {
        CycleFreezeOutput();
        return;
    }
    if (id == "fabrication.set_method") {
        // 次に作る近似モデルの方式を切り替える。両方式を残して選べるようにする。
        // 製作の棚の「方式」と同じ値(棚はこの値を映す)。
        fabricationMethod_ = fabricationMethod_
                == kachakacha::v2::app::FabricationMethod::BandApproximation
            ? kachakacha::v2::app::FabricationMethod::ClassifyFaces
            : kachakacha::v2::app::FabricationMethod::BandApproximation;
        fabricationChoice_.method = fabricationMethod_;
        RefreshFabricationDock();
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
    // 製作の棚の欄(方式・分割軸・境界・上限・最小幅・再現度)を作り方へ写す。
    kachakacha::v2::app::ApplyFabricationChoice(definition, fabricationChoice_);
    definition.targetMaxDeviation.value = kachakacha::v2::app::ParameterValueOf(
        parameterDock_->Values(), kachakacha::v2::app::ParameterId::MaxDeviationMm);
    definition.targetMaxDeviation.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.materialThickness.value = ExtrudeDistanceMm();
    definition.materialThickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto evaluated = kachakacha::v2::app::EvaluateFabrication(definition, sources,
        FabricationMarkingsFor(definition), tolerance);
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
    const auto evaluated = kachakacha::v2::app::EvaluateFabrication(*definition, sources,
        FabricationMarkingsFor(*definition), tolerance);
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

void V2MainWindow::SetAssemblyPercent(double percent, const QString& parts)
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
    // 部材番号を挙げたら、その部材だけが曲がる(V1 の part_model_part_assembly)。
    // 空なら全体。どちらも決めるのは core(UI-F006)。
    const auto numbers = kachakacha::v2::app::ParsePartNumberList(parts.toStdString());
    if (!numbers.HasValue()) {
        ReportDiagnostics(numbers.Diagnostics());
        return;
    }
    const auto found = fabricationModels_.find(modelId.ToString());
    const int bandCount = found != fabricationModels_.end()
        ? static_cast<int>(found->second.panels.size())
        : 0;
    const auto updated = kachakacha::v2::app::UpdateBandProgress(*current, bandCount,
        numbers.Value(), percent);
    if (!updated.HasValue()) {
        ReportDiagnostics(updated.Diagnostics());
        return;
    }
    auto definition = *current;
    definition.masterPercent = updated.Value().masterPercent;
    definition.bandProgress = updated.Value().bandProgress;
    // 折り線ごとの値は、全体を動かしたときだけ捨てる(V1 と同じ)。
    if (numbers.Value().empty()) {
        definition.creaseProgress.clear();
    }
    const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
        feature->id, definition, feature->inputEntityIds, "組立状態を変える"));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    RefreshFabricationView();
    if (!numbers.Value().empty()) {
        SetStatus(QStringLiteral("選んだ %1 枚の部材を組立 %2%% にしました(他の部材は変わりません)。")
                .arg(static_cast<int>(numbers.Value().size()))
                .arg(percent));
        return;
    }
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

kachakacha::v2::app::FabricationMarkings V2MainWindow::FabricationMarkingsFor(
    const kachakacha::v2::domain::CreateFabricationModelDefinition& definition) const
{
    kachakacha::v2::app::FabricationMarkings markings;
    const auto curvesOf = [this](const kachakacha::v2::base::EntityId& id) {
        std::vector<kachakacha::v2::geometry::CurveSegment> curves;
        for (const auto& curve : session_->Scene().curves) {
            if (curve.entityId == id) {
                curves.push_back(curve.segment);
            }
        }
        return curves;
    };
    for (const auto& id : definition.openingWires) {
        auto curves = curvesOf(id);
        if (!curves.empty()) {
            markings.openings.push_back(std::move(curves));
        }
    }
    for (const auto& id : definition.foldWires) {
        auto curves = curvesOf(id);
        if (!curves.empty()) {
            markings.folds.push_back(std::move(curves));
        }
    }
    for (const auto& id : definition.reliefCutWires) {
        auto curves = curvesOf(id);
        if (!curves.empty()) {
            markings.reliefCuts.push_back(std::move(curves));
        }
    }
    return markings;
}

void V2MainWindow::AssignOpeningRole(bool reliefCut)
{
    using kachakacha::v2::document::UpdateFeatureDefinitionCommand;

    // 選んだ線を、いまの近似モデルの開口(閉じていれば)か折り線(開いていれば)にする。
    // 作り方(定義)へ線の id を足して作り直す。画面の配列へ足すのではない。
    // 定義に入るので、保存して開き直しても開口は残る。
    const auto modelId = CurrentFabricationModelId();
    const auto* entity = session_->GetDocument().FindEntity(modelId);
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    const auto* current = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
              &feature->definition);
    if (current == nullptr) {
        SetStatus(QStringLiteral(
            "境界の役割: 先に「製作モデルを作る」で近似モデルを作ってください。"));
        return;
    }
    std::vector<kachakacha::v2::base::EntityId> wires;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* picked = session_->GetDocument().FindEntity(id);
        if (picked != nullptr && picked->kind == kachakacha::v2::domain::EntityKind::Wire) {
            wires.push_back(id);
        }
    }
    if (wires.empty()) {
        SetStatus(reliefCut
                ? QStringLiteral("切れ目: 切れ目にしたい開いた線を選んでください。")
                : QStringLiteral("境界の役割: 開口にしたい線を選んでください(窓など)。"));
        return;
    }
    auto definition = *current;
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    int openings = 0;
    int folds = 0;
    for (const auto& id : wires) {
        if (reliefCut) {
            // 切れ目(V1 の plate_relief_cut)。閉じているかは core が近似のときに見る(FAB-M005)。
            definition.reliefCutWires.push_back(id);
            continue;
        }
        std::vector<kachakacha::v2::geometry::CurveSegment> curves;
        for (const auto& curve : session_->Scene().curves) {
            if (curve.entityId == id) {
                curves.push_back(curve.segment);
            }
        }
        if (kachakacha::v2::geometry::SegmentsFormClosedLoop(curves, tolerance)) {
            definition.openingWires.push_back(id);
            ++openings;
        } else {
            definition.foldWires.push_back(id);
            ++folds;
        }
    }
    // 先に作れるかを確かめる。定義を書き換えてから断ると、壊れた作り方が残る。
    const auto sources = FabricationSourcesFor(definition.parts);
    const auto evaluated = kachakacha::v2::app::EvaluateFabrication(definition, sources,
        FabricationMarkingsFor(definition), tolerance.interactiveJoinMm);
    if (!evaluated.HasValue()) {
        ReportDiagnostics(evaluated.Diagnostics());
        return;
    }
    auto inputs = feature->inputEntityIds;
    inputs.insert(inputs.end(), wires.begin(), wires.end());
    const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
        feature->id, definition, inputs, reliefCut ? "切れ目を入れる" : "境界の役割を決める"));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    fabricationModels_[modelId.ToString()] = evaluated.Value();
    patternPages_.clear();
    RefreshFabricationView();
    if (reliefCut) {
        SetStatus(QStringLiteral("切れ目: %1 本入れました(いま切れ目 %2)。型紙はもう一度作ってください。")
                .arg(static_cast<int>(wires.size()))
                .arg(static_cast<int>(definition.reliefCutWires.size())));
        return;
    }
    SetStatus(QStringLiteral("境界の役割: 開口を %1 つ、折り線を %2 本入れました"
                             "(いま開口 %3、折り線 %4)。型紙はもう一度作ってください。")
            .arg(openings)
            .arg(folds)
            .arg(static_cast<int>(definition.openingWires.size()))
            .arg(static_cast<int>(definition.foldWires.size())));
}

std::vector<std::pair<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>>>
V2MainWindow::ConnectionScopeCurves(
    const kachakacha::v2::domain::CreateFabricationModelDefinition& definition) const
{
    std::vector<std::pair<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>>>
        wires;
    for (const auto& id : definition.connectionWires) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        std::vector<kachakacha::v2::geometry::CurveSegment> curves;
        for (const auto& curve : session_->Scene().curves) {
            if (curve.entityId == id) {
                curves.push_back(curve.segment);
            }
        }
        if (!curves.empty()) {
            wires.emplace_back(entity->displayName.empty() ? std::string("線")
                                                            : entity->displayName,
                std::move(curves));
        }
    }
    return wires;
}

void V2MainWindow::SetConnectionScope()
{
    using kachakacha::v2::document::UpdateFeatureDefinitionCommand;

    // 選んだ線を接続スコープにする(V1 の合意13)。近似したことで隣の部品と合わなく
    // なる線を、近似の実形状へ寄せた「_接続」の線として作る。元の線は変えない。
    const auto modelId = CurrentFabricationModelId();
    const auto* entity = session_->GetDocument().FindEntity(modelId);
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    const auto* current = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
              &feature->definition);
    const auto evaluated = fabricationModels_.find(modelId.ToString());
    if (current == nullptr || evaluated == fabricationModels_.end()) {
        SetStatus(QStringLiteral(
            "接続スコープ: 先に「製作モデルを作る」で近似モデルを作ってください。"));
        return;
    }
    if (!evaluated->second.bandMesh.has_value()) {
        SetStatus(QStringLiteral(
            "接続スコープ: 帯近似(V1方式)の近似モデルにだけ使えます。"));
        return;
    }
    std::vector<kachakacha::v2::base::EntityId> wires;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* picked = session_->GetDocument().FindEntity(id);
        if (picked != nullptr && picked->kind == kachakacha::v2::domain::EntityKind::Wire
            && picked->displayName.find("_接続") == std::string::npos) {
            wires.push_back(id);
        }
    }
    if (wires.empty()) {
        SetStatus(QStringLiteral("接続スコープ: 近似へ寄せたい線を選んでください。"));
        return;
    }
    auto definition = *current;
    for (const auto& id : wires) {
        if (std::find(definition.connectionWires.begin(), definition.connectionWires.end(),
                id) == definition.connectionWires.end()) {
            definition.connectionWires.push_back(id);
        }
    }
    auto inputs = feature->inputEntityIds;
    inputs.insert(inputs.end(), wires.begin(), wires.end());
    const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
        feature->id, definition, inputs, "接続スコープを決める"));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    // 寄せた線を作る。完成形(mesh.world)の上へ寄せる。
    const double snap = evaluated->second.maximumDeviationMm + 0.35;
    const auto adapted = kachakacha::v2::app::AdaptConnectionWires(
        *evaluated->second.bandMesh, evaluated->second.bandMesh->world,
        ConnectionScopeCurves(definition), snap);
    int made = 0;
    int snapped = 0;
    for (const auto& wire : adapted) {
        if (!AddPlainWire(PolylineOf(wire.points), wire.name.c_str()).IsNil()) {
            ++made;
            snapped += wire.snappedPoints;
        }
    }
    AdoptCurrentDocument();
    RefreshFabricationView();
    SetStatus(QStringLiteral("接続スコープ: %1 本を近似の形へ寄せました(寄せた点 %2)。"
                             "元の線はそのままです。")
            .arg(made)
            .arg(snapped));
}

void V2MainWindow::AdoptFabricationChoice()
{
    if (fabricationDock_ == nullptr) {
        return;
    }
    // 欄の値の検査は core。断られたら理由を棚に出し、前の値のまま。
    QString boundaryError;
    if (!fabricationDock_->ManualBoundariesReadable(&boundaryError)) {
        fabricationDock_->SetMessage(boundaryError);
        return;
    }
    const auto checked = kachakacha::v2::app::CheckFabricationChoice(fabricationDock_->Choice());
    if (!checked.HasValue()) {
        fabricationDock_->SetMessage(QString::fromStdString(checked.Diagnostics().front().code
            + " " + checked.Diagnostics().front().summaryJa));
        return;
    }
    fabricationChoice_ = checked.Value();
    fabricationMethod_ = fabricationChoice_.method;
    fabricationDock_->SetMessage(QString());
}

void V2MainWindow::RefreshFabricationDock()
{
    if (fabricationDock_ == nullptr || parameterDock_ == nullptr) {
        return;
    }
    fabricationDock_->SetParameterMm(kachakacha::v2::app::ParameterId::ExtrudeDistance,
        ExtrudeDistanceMm());
    fabricationDock_->SetParameterMm(kachakacha::v2::app::ParameterId::MaxDeviationMm,
        kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(),
            kachakacha::v2::app::ParameterId::MaxDeviationMm));
    fabricationDock_->SetFreezeOutput(freezeOutput_);
    // 欄は「次に作る近似モデル」の値。選んでいる近似モデルがあれば、その方式と組立率も出す。
    fabricationDock_->SetChoice(fabricationChoice_);
    const auto modelId = CurrentFabricationModelId();
    const auto* entity = session_->GetDocument().FindEntity(modelId);
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    const auto* definition = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
              &feature->definition);
    if (definition != nullptr) {
        QString material;
        if (entity->manufacturing.has_value() && !entity->manufacturing->materialName.empty()) {
            material = QStringLiteral(" 材料 %1 × %2 枚")
                           .arg(QString::fromStdString(entity->manufacturing->materialName))
                           .arg(entity->manufacturing->layerCount);
        }
        fabricationDock_->SetModelText(QStringLiteral("近似モデル: %1(%2)%3")
                .arg(QString::fromStdString(entity->displayName),
                    QString::fromUtf8(std::string(kachakacha::v2::app::FabricationMethodNameJa(
                        kachakacha::v2::app::FabricationMethodOf(*definition)))
                                          .c_str()),
                    material));
        fabricationDock_->SetAssemblyPercent(definition->masterPercent);
        return;
    }
    fabricationDock_->SetModelText(
        QStringLiteral("近似モデル: (なし。部品か形状ガイドを選んで「製作モデルを作る」)"));
}

void V2MainWindow::ApplyMaterialToSelection(const QString& material, int layers)
{
    using kachakacha::v2::domain::EntityKind;
    // 材料は部品・形状ガイド・近似モデルに付く。線には付かない(core が断る)。
    std::vector<kachakacha::v2::base::EntityId> targets;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr
            && (entity->kind == EntityKind::Part || entity->kind == EntityKind::GuideSurface
                || entity->kind == EntityKind::FabricationModel)) {
            targets.push_back(id);
        }
    }
    if (targets.empty()) {
        SetStatus(QStringLiteral("材料: 部品か形状ガイドか近似モデルを選んでから当ててください。"));
        return;
    }
    kachakacha::v2::domain::ManufacturingProperties properties;
    properties.materialName = material.toStdString();
    properties.nominalThicknessMm = ExtrudeDistanceMm();
    properties.layerCount = layers;
    const auto changed = session_->GetDocument().Run(
        kachakacha::v2::document::SetManufacturingCommand(targets, properties));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    RefreshEntityList();
    RefreshFabricationDock();
    SetStatus(QStringLiteral("材料「%1」× %2 枚を %3 個に付けました(板厚 %4 mm)。")
            .arg(material)
            .arg(layers)
            .arg(static_cast<int>(targets.size()))
            .arg(ExtrudeDistanceMm(), 0, 'f', 3));
}

