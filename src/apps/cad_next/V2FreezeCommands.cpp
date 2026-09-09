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
    using kachakacha::v2::fabrication::PatternPlacement;

    if (fabricationPanels_.empty()) {
        SetStatus(QStringLiteral(
            "現在状態を固定: 先に「製作モデルを作る」で部材にしてください。"));
        return;
    }
    // 型紙の座標そのままで、作業平面の上へ線として置く。
    // 置き直さないのは、型紙で見えている形と1mmも違わせないためである。
    PatternPlacement placement;
    int wires = 0;
    int curves = 0;
    for (const auto& panel : fabricationPanels_) {
        const auto placed =
            kachakacha::v2::fabrication::PlacePanelCurves(panel, placement);
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
        curves += static_cast<int>(placed.Value().size());
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral(
        "現在状態を固定: %1枚の部材を %2本の線にしました。型紙と同じ形です。")
            .arg(wires)
            .arg(curves));
}
