//! 開き直したときに、立体と面を作り直す。
//!
//! 実形状(OCCT の形)は文書に持たない。持つと、入力を直したのに形が古いまま、
//! という食い違いが起きる。そのかわり **開いたときに作り方から作り直す** 必要がある。
//! これをしていなかったので、保存して開き直すと立体も面も消えていた。
//! 一覧には名前が残り、線も残るので、消えたことに気づきにくい。
//! 気づかないまま STEP で出そうとして、はじめて「立体がありません」と言われる。
//!
//! どれをどの順で作り直すかは core(app/ShapeRebuild.h)が決める。
//! ここはその段取りに従って、作ったときと同じ道をもう一度通すだけである。

#include "V2MainWindow.h"

#include "kachakacha/app/ShapeRebuild.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/WireCage.h"

#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctWireCage.h"

#include <string>
#include <vector>

namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::CurveSegment;

} // namespace

bool V2MainWindow::RebuildExtrudeShape(const kachakacha::v2::domain::Feature& feature,
    const EntityId& output)
{
    using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
    using kachakacha::v2::modeling::ExtrudeRequest;

    const auto* definition =
        std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
    if (definition == nullptr) {
        return false;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    ExtrudeRequest request;
    request.profiles = ExtrudeProfilesFor(definition->profiles);
    if (request.profiles.empty()) {
        return false;
    }
    // 向きは保存してある値をそのまま使う。いまの作業平面から作り直さない。
    // 作り直すと、開いたときの作業平面しだいで形が変わってしまう。
    request.directionMode = kachakacha::v2::modeling::ExtrudeDirectionMode::CustomXYZ;
    request.customDirection = definition->direction;
    request.extent = static_cast<kachakacha::v2::modeling::ExtrudeExtentMode>(
        definition->extentMode);
    request.distanceMm = definition->distance.value;
    request.outputs.part = true;
    request.outputs.endProfileWire = true;
    request.outputs.sideBoundaryWires = true;
    const auto analysis = AnalyzeExtrudeRequest(request, tolerance);
    if (!analysis.HasValue()) {
        return false;
    }
    const auto built =
        kachakacha::v2::kernel::BuildExtrude(request, analysis.Value(), tolerance);
    if (!built.HasValue() || built.Value().parts.empty()) {
        return false;
    }
    std::vector<CurveSegment> edges;
    for (const auto& wire : built.Value().endProfileWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    for (const auto& wire : built.Value().sideBoundaryWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    partShapes_[output.ToString()] = built.Value().parts.front().handle;
    partEdges_[output.ToString()] = std::move(edges);
    if (!built.Value().endProfileWires.empty()) {
        partFlatBoundary_[output.ToString()] = built.Value().endProfileWires.front();
    }
    return true;
}

bool V2MainWindow::RebuildWireCageShape(const kachakacha::v2::domain::Feature& feature,
    const EntityId& output)
{
    using kachakacha::v2::modeling::AnalyzeWireCage;
    using kachakacha::v2::modeling::CageEdgeInput;
    using kachakacha::v2::modeling::PlanWireCageParts;

    const auto* definition =
        std::get_if<kachakacha::v2::domain::CreatePartFromWireCageDefinition>(
            &feature.definition);
    if (definition == nullptr) {
        return false;
    }
    std::vector<CageEdgeInput> inputs;
    for (const auto& curve : session_->Scene().curves) {
        for (const EntityId& id : definition->wires) {
            if (curve.entityId == id) {
                inputs.push_back(
                    CageEdgeInput{curve.entityId, curve.segmentId, curve.segment});
                break;
            }
        }
    }
    if (inputs.size() < 3) {
        return false;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto analysis = AnalyzeWireCage(inputs, tolerance);
    if (!analysis.HasValue()) {
        return false;
    }
    std::vector<std::size_t> shells;
    for (std::size_t index = 0; index < analysis.Value().shells.size(); ++index) {
        shells.push_back(index);
    }
    const auto planned = PlanWireCageParts(analysis.Value(), shells);
    if (!planned.HasValue()) {
        return false;
    }
    const auto built = kachakacha::v2::kernel::BuildWireCageParts(inputs,
        analysis.Value(), planned.Value(), tolerance);
    if (!built.HasValue() || built.Value().empty()) {
        return false;
    }
    std::vector<CurveSegment> edges;
    for (const auto& input : inputs) {
        edges.push_back(input.segment);
    }
    partShapes_[output.ToString()] = built.Value().front().handle;
    partEdges_[output.ToString()] = std::move(edges);
    return true;
}

bool V2MainWindow::RebuildBooleanShape(const kachakacha::v2::domain::Feature& feature,
    const EntityId& output)
{
    using kachakacha::v2::kernel::BooleanOperation;

    const auto* definition =
        std::get_if<kachakacha::v2::domain::BooleanDefinition>(&feature.definition);
    if (definition == nullptr || definition->targets.empty()
        || definition->tools.empty()) {
        return false;
    }
    // 材料は先に作り直してある。評価順に従っているので、ここでは必ず見つかる。
    // 見つからないなら、材料の作り直しが失敗している。黙って作らない。
    const auto base = partShapes_.find(definition->targets.front().ToString());
    const auto other = partShapes_.find(definition->tools.front().ToString());
    if (base == partShapes_.end() || other == partShapes_.end()) {
        return false;
    }
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto built = kachakacha::v2::kernel::BuildBoolean(
        definition->mode == 1 ? BooleanOperation::Difference : BooleanOperation::Union,
        base->second, other->second, tolerance);
    if (!built.HasValue()) {
        return false;
    }
    partShapes_[output.ToString()] = built.Value().handle;
    return true;
}

bool V2MainWindow::RebuildThickenShape(const kachakacha::v2::domain::Feature& feature,
    const EntityId& output)
{
    const auto* definition =
        std::get_if<kachakacha::v2::domain::ThickenSurfaceDefinition>(&feature.definition);
    if (definition == nullptr) {
        return false;
    }
    // 面は先に作り直してある。評価順に従っているので、ここでは必ず見つかる。
    const auto found = guideShapes_.find(definition->surface.ToString());
    if (found == guideShapes_.end()) {
        return false;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto built = kachakacha::v2::kernel::ThickenSurface(found->second,
        definition->thickness.value,
        static_cast<kachakacha::v2::fabrication::ThicknessPlacement>(definition->placement),
        tolerance);
    if (!built.HasValue()) {
        return false;
    }
    partShapes_[output.ToString()] = built.Value().handle;
    partEdges_[output.ToString()] = built.Value().edges;
    return true;
}

void V2MainWindow::RebuildKernelShapes()
{
    // 覚えていた形をいったん捨てる。捨てないと、開く前の文書の形が混ざる。
    partShapes_.clear();
    partEdges_.clear();
    partFlatBoundary_.clear();
    guideShapes_.clear();
    guideEdges_.clear();
    guideSamples_.clear();
    fabricationModels_.clear();
    // 役割表も文書の線を指している。前の文書の表を残すと、無い線の行が並ぶ。
    guideTable_ = kachakacha::v2::modeling::GuideTable{};

    const auto snapshot = session_->GetDocument().Snapshot();
    const auto steps = kachakacha::v2::app::PlanShapeRebuild(snapshot);
    if (steps.empty()) {
        // 作り直すものが無くても、表と画面は空にし直す。ここで返すと前の表が残った。
        RefreshGuideTable();
        RefreshPartEdges();
        RefreshFabricationView();
        return;
    }
    int made = 0;
    std::vector<std::string> failed;
    for (const auto& step : steps) {
        const auto* feature = session_->GetDocument().FindFeature(step.featureId);
        if (feature == nullptr) {
            continue;
        }
        bool ok = false;
        switch (step.kind) {
        case kachakacha::v2::app::ShapeRebuildKind::Extrude:
            ok = RebuildExtrudeShape(*feature, step.outputEntityId);
            break;
        case kachakacha::v2::app::ShapeRebuildKind::WireCage:
            ok = RebuildWireCageShape(*feature, step.outputEntityId);
            break;
        case kachakacha::v2::app::ShapeRebuildKind::Boolean:
            ok = RebuildBooleanShape(*feature, step.outputEntityId);
            break;
        case kachakacha::v2::app::ShapeRebuildKind::GuideSurface:
            ok = RebuildGuideSurfaceShape(*feature, step.outputEntityId);
            break;
        case kachakacha::v2::app::ShapeRebuildKind::ThickenSurface:
            ok = RebuildThickenShape(*feature, step.outputEntityId);
            break;
        case kachakacha::v2::app::ShapeRebuildKind::FabricationModel:
            ok = RebuildFabricationModel(*feature, step.outputEntityId);
            break;
        }
        if (ok) {
            ++made;
        } else {
            failed.push_back(step.displayName.empty()
                    ? std::string(ShapeRebuildKindNameJa(step.kind))
                    : step.displayName);
        }
    }
    RefreshPartEdges();
    RefreshFabricationView();
    RefreshExportCounts();
    // 最後に作り直した面の表が残る。開いた直後に表が空だと、何を元に作ったか見えない。
    RefreshGuideTable();
    if (failed.empty()) {
        return;
    }
    // 作り直せなかったものは黙って捨てない。名前を出す。
    // 黙って捨てると、出そうとしたときに初めて気づくことになる。
    QString names;
    for (const std::string& name : failed) {
        if (!names.isEmpty()) {
            names += QStringLiteral("、");
        }
        names += QString::fromStdString(name);
    }
    SetStatus(QStringLiteral("開きました。%1個の形を作り直しました。"
                             "%2 は作り直せませんでした。")
            .arg(made)
            .arg(names));
}
