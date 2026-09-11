//! 固定のコマンド(V2MainWindow の一部)。
//!
//! 「固定」は、**作り方への依存を切って、いまの形そのものにする** ことである。
//!
//! なぜ要るか。作り方をたどれるのは良いことだが、
//! 元の線を直すと下流が全部作り直される。もう直さないと決めたものは、
//! 固定しておくと、うっかり元を触っても形が動かない。
//!
//! 固定しても **元は消さない。** 隠すだけである。
//! 消すと、どうやって作ったのかを二度とたどれなくなる。

#include "V2MainWindow.h"

#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/fabrication/PlanarPanel.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"

#include <string>
#include <utility>
#include <vector>

bool V2MainWindow::IsFreezeCommand(std::string_view id)
{
    return id == "derived.freeze" || id == "fabrication.freeze_state";
}

void V2MainWindow::RunFreezeCommand(std::string_view id)
{
    if (id == "derived.freeze") {
        FreezeSelectedDerived();
        return;
    }
    if (id == "fabrication.freeze_state") {
        FreezeFabricationState();
    }
}

kachakacha::v2::base::EntityId V2MainWindow::AddPlainWire(
    std::vector<kachakacha::v2::geometry::CurveSegment> segments, const char* labelJa)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::CreateWireDefinition;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    if (segments.empty()) {
        return kachakacha::v2::base::EntityId{};
    }
    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::CreateWire;
    feature.displayName = labelJa;
    // 入力を持たせない。持たせると、また元に付いていってしまう。
    CreateWireDefinition definition;
    definition.segments = std::move(segments);
    for (std::size_t index = 0; index < definition.segments.size(); ++index) {
        definition.segmentIds.push_back(
            ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
    }
    feature.definition = std::move(definition);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Wire;
    entity.displayName = labelJa;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, labelJa));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return kachakacha::v2::base::EntityId{};
    }
    return entity.id;
}

void V2MainWindow::FreezeSelectedDerived()
{
    const auto& selection = viewport_->Selection();
    const auto segments =
        kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    if (segments.empty()) {
        SetStatus(QStringLiteral(
            "現在状態を固定: 線を持っている、作られたものを1つ選んでください。"));
        return;
    }
    const auto made = AddPlainWire(segments, "固定");
    if (made.IsNil()) {
        return;
    }
    // 元は消さない。隠すだけ。消すと作り方をたどれなくなる。
    (void)session_->GetDocument().Run(kachakacha::v2::document::SetVisibilityCommand(
        selection.entityIds, kachakacha::v2::domain::Visibility::Hidden));
    AdoptCurrentDocument();
    SetStatus(QStringLiteral(
        "現在状態を固定: %1本の線を、作り方に付いていかない形にしました。"
        "元は隠してあります。")
            .arg(static_cast<int>(segments.size())));
}

void V2MainWindow::FreezeFabricationState()
{
    using kachakacha::v2::fabrication::FreezeOutput;

    // いまの曲げ状態を、文書の普通のものにする(工程3 → 工程2 へ戻る道)。
    // 画面に出ている姿勢(FoldedRailsOf)そのものを使う。別の作り方で作り直すと、
    // 見えている形と出てくる形が食い違う ── V1 で実際に起きた(bandRails の教訓)。
    const auto modelId = CurrentFabricationModelId();
    const auto* entityPointer = session_->GetDocument().FindEntity(modelId);
    const auto* feature = entityPointer == nullptr
        ? nullptr
        : session_->GetDocument().FindFeature(entityPointer->createdBy);
    const auto* definitionPointer = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
              &feature->definition);
    const auto evaluatedIterator = fabricationModels_.find(modelId.ToString());
    if (definitionPointer == nullptr || evaluatedIterator == fabricationModels_.end()) {
        SetStatus(QStringLiteral(
            "現在状態を固定: 先に「製作モデルを作る」で近似モデルを作ってください。"));
        return;
    }
    // ここから先は文書へ線や面を足す。足すと entities / features の並びが作り直され、
    // 上のポインタは指す先を失う。実際にそれで固定の途中で落ちた(パッケージの自己試験)。
    // 使うものは先に写しておく。
    const std::string modelName = entityPointer->displayName;
    const kachakacha::v2::domain::CreateFabricationModelDefinition definition =
        *definitionPointer;
    const kachakacha::v2::app::FabricationEvaluation evaluated = evaluatedIterator->second;
    if (!evaluated.bandMesh.has_value()) {
        // V2 方式(面の分類)には曲げ状態の形が無い。型紙の線をそのまま置く。
        FreezeFlatPanels();
        return;
    }
    // 持ち上げ 0 で取る。画面では帯を離して見せるが、固定するのは本当の位置。
    const auto rails = kachakacha::v2::app::FoldedRailsOf(definition, evaluated, 0.0);
    const std::string stateName = kachakacha::v2::app::FoldStateSummaryJa(definition);
    int wires = 0;
    int surfaces = 0;
    int parts = 0;
    for (std::size_t band = 0; band + 1 < rails.size(); band += 2) {
        const std::string label = modelName + " 部材"
            + std::to_string(band / 2 + 1) + " (" + stateName + ")";
        const auto bottom = AddPlainWire(PolylineOf(rails[band]), (label + " 下").c_str());
        const auto top = AddPlainWire(PolylineOf(rails[band + 1]), (label + " 上").c_str());
        if (bottom.IsNil() || top.IsNil()) {
            return;
        }
        wires += 2;
        if (freezeOutput_ == FreezeOutput::WiresOnly) {
            continue;
        }
        // 面: 2本のレールを断面にしたルールド面。作り方はワイヤーを指すので、
        // 開き直しても作り直せる。
        AdoptCurrentDocument();
        const auto surfaceId = CreateGuideSurfaceFromWires({bottom, top}, label + " 面");
        if (surfaceId.IsNil()) {
            return;
        }
        ++surfaces;
        if (freezeOutput_ == FreezeOutput::PartsOnly || freezeOutput_ == FreezeOutput::Both) {
            // 部品: その面に板厚を付ける。
            viewport_->SetSelection(kachakacha::v2::app::SelectionSet{{surfaceId}});
            const int before = static_cast<int>(partShapes_.size());
            RunThickenSurface();
            if (static_cast<int>(partShapes_.size()) > before) {
                ++parts;
            }
        }
    }
    // 接続スコープの線も、この曲げ状態の形へ寄せて固定する(V1 の PartFoldState と同じ)。
    const auto state = kachakacha::v2::app::ResolveFoldState(definition, *evaluated.bandMesh);
    const auto folded = kachakacha::v2::fabrication::FoldBandMesh(
        *evaluated.bandMesh, state.masterProgress);
    for (const auto& wire : kachakacha::v2::app::AdaptConnectionWires(
             *evaluated.bandMesh, folded, ConnectionScopeCurves(definition),
             evaluated.maximumDeviationMm + 0.35)) {
        if (!AddPlainWire(PolylineOf(wire.points),
                (wire.name + " (" + stateName + ")").c_str())
                 .IsNil()) {
            ++wires;
        }
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("現在状態を固定(%1): 線 %2 本、面 %3 枚、部品 %4 個にしました。")
            .arg(QString::fromStdString(stateName))
            .arg(wires)
            .arg(surfaces)
            .arg(parts));
}

void V2MainWindow::FreezeFlatPanels()
{
    using kachakacha::v2::fabrication::PatternPlacement;
    // 型紙の座標そのままで、作業平面の上へ線として置く。
    // 置き直さないのは、型紙で見えている形と1mmも違わせないためである。
    PatternPlacement placement;
    int wires = 0;
    for (const auto& panel : fabricationPanels_) {
        const auto placed = kachakacha::v2::fabrication::PlacePanelCurves(panel, placement);
        if (!placed.HasValue()) {
            ReportDiagnostics(placed.Diagnostics());
            return;
        }
        std::vector<kachakacha::v2::geometry::CurveSegment> segments;
        for (const auto& curve : placed.Value()) {
            segments.push_back(curve.segment);
        }
        if (AddPlainWire(std::move(segments), "固定した部材").IsNil()) {
            return;
        }
        ++wires;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("現在状態を固定: %1枚の部材を線にしました。型紙と同じ形です。")
            .arg(wires));
}

std::vector<kachakacha::v2::geometry::CurveSegment> V2MainWindow::PolylineOf(
    const std::vector<kachakacha::v2::geometry::Vector3>& points)
{
    // レールは点列。隣どうしを直線でつなぐ。長さ0の線は入れない(作れない)。
    std::vector<kachakacha::v2::geometry::CurveSegment> segments;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const auto made = kachakacha::v2::geometry::CurveSegment::MakeLine(
            points[index - 1], points[index]);
        if (made.HasValue()) {
            segments.push_back(made.Value());
        }
    }
    return segments;
}

void V2MainWindow::CycleFreezeOutput()
{
    using kachakacha::v2::fabrication::FreezeOutput;
    freezeOutput_ = freezeOutput_ == FreezeOutput::WiresOnly ? FreezeOutput::PartsOnly
        : freezeOutput_ == FreezeOutput::PartsOnly           ? FreezeOutput::Both
                                                             : FreezeOutput::WiresOnly;
    RefreshFabricationDock();
    SetStatus(QStringLiteral("固定で作るもの: %1")
            .arg(QString::fromUtf8(std::string(
                kachakacha::v2::fabrication::FreezeOutputNameJa(freezeOutput_))
                                       .c_str())));
}
