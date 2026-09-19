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
#include "kachakacha/app/PanelAdvice.h"
#include "kachakacha/fabrication/CurvatureAnalysis.h"
#include "kachakacha/fabrication/CurvedPanel.h"
#include "kachakacha/fabrication/PlanarPanel.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/kernel/OcctFaceAdjacency.h"
#include "kachakacha/kernel/OcctFaceQuery.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

//! 「近似部品の編集」。部材の編集の棚(2 段目)を出すだけ。文書は変えない。
void V2MainWindow::ShowPartEditShelf()
{
    if (fabricationDock_ != nullptr) {
        fabricationDock_->SetStageIndex(1);
    }
    ShowShelf(kachakacha::v2::app::Shelf::Fabrication);
    SetStatus(QStringLiteral("近似部品の編集: 3D で部材を押すか「対象部材」に番号を入れ、"
                             "分割・結合・切れ目・半径・曲げ状態を決めてください。"));
}

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
        // 道具が動いていれば作り直す。動いていなければ道具を始める。
        if (approxShelfShown_) {
            RefreshApproxAll();
        } else {
            RunFabricationCreate();
        }
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
    if (id == "fabrication.merge_parts") {
        MergeFabricationParts();
        return;
    }
    if (id == "fabrication.edit_part") {
        ShowPartEditShelf();
        return;
    }
    if (id == "fabrication.split_part") {
        SplitFabricationPart();
        return;
    }
    if (id == "fabrication.set_unfold_base") {
        SetUnfoldBaseRail();
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

// RunFabricationCreate は V2ApproxCommands.cpp へ移した(道具から始める形にしたため)。

//! `splitSolidFaces` は **作り方(定義)のもの** を渡す。棚の欄ではない。
//! 欄を見ると、開き直したときに、保存した作り方と別の部材が出来る。
std::vector<kachakacha::v2::app::FabricationSource> V2MainWindow::FabricationSourcesFor(
    const std::vector<kachakacha::v2::base::EntityId>& ids, bool splitSolidFaces) const
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
            if (splitSolidFaces) {
                // 立体を面ごとに分ける。箱なら6枚の型紙になる。
                // 面を1枚ずつ取れるようになったので、ここで初めてできる(EX-02 の副産物)。
                AppendSolidFaceSources(id, source.name, sources);
                continue;
            }
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
    const auto sources = FabricationSourcesFor(definition->parts, definition->splitSolidFaces);
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
    const auto* current = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
              &feature->definition);
    if (current == nullptr) {
        // 黙って何もしない、を作らない。何を選べばよいかを言う。
        SetStatus(QStringLiteral("曲げ状態: 先に近似モデルを 3D か一覧で選んでください。"));
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
    // 出来た型紙を下見の棚へ渡し、前に出す。
    // 見ないまま出すと、紙に収まっていないことに印刷してから気づく。
    if (patternDock_ != nullptr) {
        patternDock_->SetPages(patternPages_);
        ShowShelf(kachakacha::v2::app::Shelf::Pattern);
    }
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
    const auto sources = FabricationSourcesFor(definition.parts, definition.splitSolidFaces);
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
        fabricationDock_->SetMessage(QString::fromStdString(checked.FirstCode()
            + " " + checked.FirstSummaryJa()));
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
    // 半径の欄も映す。固定してあれば測り直しても触らない(§31)。
    RefreshBendRadius();
    fabricationDock_->SetParameterMm(kachakacha::v2::app::ParameterId::MaxDeviationMm,
        kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(),
            kachakacha::v2::app::ParameterId::MaxDeviationMm));
    fabricationDock_->SetFreezeOutput(freezeOutput_);
    // 欄は「次に作る近似モデル」の値。選んでいる近似モデルがあれば、その方式と組立率も出す。
    fabricationDock_->SetChoice(fabricationChoice_);
    const auto modelId = CurrentFabricationModelId();
    RefreshFabricationPartInfo(modelId);
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


//! 立体の面を1枚ずつ、近似の元にする(2026-09-14)。
//!
//! これまで立体は「平らな1枚」しか部材にできなかった。箱を選んでも1枚しか出ない。
//! 面を1枚ずつ取れるようになった(kernel/OcctFaceQuery)ので、
//! 面ごとに標本を取って、面ごとの部材にする。
//!
//! **面をまとめて1枚の部材にすることは、まだできない。**
//! いまの部材は「1枚の格子」で出来ていて、別々の面の格子を1つに繋ぐ道が無い。
//! そこは作り直しが要る。まとめたときの枚数は `PanelAdviceTextJa` が言うので、
//! いまは「何枚になるはずか」を読みながら、面ごとに切って貼る形になる。
void V2MainWindow::AppendSolidFaceSources(const kachakacha::v2::base::EntityId& partId,
    const std::string& partName,
    std::vector<kachakacha::v2::app::FabricationSource>& sources) const
{
    const auto found = partShapes_.find(partId.ToString());
    if (found == partShapes_.end()) {
        return;
    }
    const auto count = kachakacha::v2::kernel::ShapeFaceCount(found->second);
    if (!count.HasValue()) {
        return;
    }
    for (std::size_t face = 0; face < count.Value(); ++face) {
        const auto sampled = kachakacha::v2::kernel::FaceSamplesOf(found->second, face);
        if (!sampled.HasValue()) {
            continue;   // 取れない面は飛ばす。飛ばしたことは断りの文で分かる。
        }
        kachakacha::v2::app::FabricationSource source;
        source.entityId = partId;
        source.name = partName + " 面" + std::to_string(face + 1);
        source.samples = sampled.Value().samples;
        // 3D で押した面(EX-02 と同じ番号)を、あとで部材番号へ変えるために覚える。
        source.faceIndex = face;
        sources.push_back(std::move(source));
    }
}

//! 断ったときに「何枚に分ければ作れるか」まで言う(製作近似 §5)。
//!
//! いままでは「1枚では展開できません」で終わっていた。作る人には、
//! 切るのか、分けるのか、形を直すのかが決められない。
//!
//! 面ごとの曲がり方(kernel/OcctFaceQuery の標本 → CurvatureAnalysis)と、
//! 面どうしの隣り合わせ(kernel/OcctFaceAdjacency)を集めて、
//! 4通りの分け方を作り比べる(app/PanelAdvice)。
//!
//! **勝手に分けない。** 言うのは「こうすれば作れます」までである。
QString V2MainWindow::PanelAdviceTextJa(
    const std::vector<kachakacha::v2::base::EntityId>& partIds) const
{
    std::vector<kachakacha::v2::fabrication::PanelCandidate> panels;
    std::vector<kachakacha::v2::fabrication::PanelAdjacency> adjacencies;
    const double target = kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(),
        kachakacha::v2::app::ParameterId::MaxDeviationMm);
    for (const auto& id : partIds) {
        const auto found = partShapes_.find(id.ToString());
        if (found == partShapes_.end()) {
            continue;
        }
        const auto count = kachakacha::v2::kernel::ShapeFaceCount(found->second);
        if (!count.HasValue()) {
            continue;
        }
        const std::size_t base = panels.size();
        for (std::size_t face = 0; face < count.Value(); ++face) {
            const auto sampled = kachakacha::v2::kernel::FaceSamplesOf(found->second, face);
            if (!sampled.HasValue()) {
                continue;
            }
            const auto measured = kachakacha::v2::fabrication::AnalyzeCurvature(
                sampled.Value().samples, target);
            if (!measured.HasValue()) {
                continue;
            }
            kachakacha::v2::fabrication::PanelCandidate candidate;
            candidate.panelId = std::to_string(face + 1) + "枚目";
            candidate.classification = measured.Value().classification;
            candidate.areaMm2 = sampled.Value().areaMm2;
            candidate.doubleCurvedRatio = measured.Value().doubleCurvedRatio;
            // 1枚のまま平らにしたときのずれ。Gauss曲率と面の代表長さから見積もる。
            candidate.flattenDeviationMm = measured.Value().maximumAbsoluteGaussian
                * std::pow(std::sqrt(std::max(sampled.Value().areaMm2, 0.0)), 3.0) / 8.0;
            panels.push_back(std::move(candidate));
        }
        const auto neighbours = kachakacha::v2::kernel::FaceAdjacenciesOf(found->second,
            session_->GetDocument().Snapshot().settings.tolerance);
        if (!neighbours.HasValue()) {
            continue;
        }
        // 立体をまたいで番号がぶつからないよう、この立体の始まりぶんだけずらす。
        for (auto neighbour : neighbours.Value()) {
            neighbour.firstIndex += base;
            neighbour.secondIndex += base;
            adjacencies.push_back(neighbour);
        }
    }
    if (panels.empty()) {
        return QString();
    }
    // 4通りをすべて作り比べるので、ここの strategy は入口の既定でよい。
    // どの分け方を選んだかを覚える鍵はまだ無い(保存の形は GUARDED)。
    kachakacha::v2::fabrication::FabricationSettings settings;
    settings.panelCountLimit = fabricationChoice_.maximumPartCount;
    settings.minimumPanelWidthMm = fabricationChoice_.minimumPartWidthMm;
    const auto advice = kachakacha::v2::app::AdvisePanelStrategy(panels, adjacencies,
        settings, target);
    return QString::fromStdString(advice.messageJa);
}
