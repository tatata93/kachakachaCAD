//! 形をつくるコマンド(V2MainWindow の一部)。押し出し・かご・足す引く・固定。
//!
//! 思想。**面が張れるかどうかを、面を張る前に判定する。**
//! だからここは必ず2段になる。まず core に調べさせ、通ったら kernel に作らせる。
//! 調べずに作らせると、失敗したときに「何が悪いのか」を言えない。
//!
//! もうひとつ。**出来た立体の形は文書に持たない。**
//! 持つと、入力を変えたのに形が古いまま、という食い違いが起きる。
//! 文書は「どう作ったか」だけを持ち、形は作り直す。

#include "V2MainWindow.h"

#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctWireCage.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/WireCage.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::modeling::SnapCurve;

//! 選んでいるワイヤーを、押し出しの輪郭にまとめる。
//! Entity ごとに1つの輪郭にする。選んだ順は保つ。
[[nodiscard]] std::vector<kachakacha::v2::modeling::ExtrudeProfile> ProfilesOfImpl(
    const std::vector<EntityId>& entityIds,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance)
{
    std::vector<kachakacha::v2::modeling::ExtrudeProfile> profiles;
    for (const EntityId& id : entityIds) {
        kachakacha::v2::modeling::ExtrudeProfile profile;
        profile.sourceEntityId = id;
        for (const SnapCurve& curve : scene.curves) {
            if (curve.entityId != id) {
                continue;
            }
            profile.segments.push_back(curve.segment);
            profile.segmentIds.push_back(curve.segmentId);
        }
        if (!profile.segments.empty()) {
            // 閉じているかを立てておく。立てないと、押し出しはいつまでも
            // 「開いた輪郭からは部品を作れません」と断る。実際にそうなった。
            profile.closed = kachakacha::v2::geometry::SegmentsFormClosedLoop(
                profile.segments, tolerance);
            profiles.push_back(std::move(profile));
        }
    }
    return profiles;
}

} // namespace

bool V2MainWindow::IsPartCommand(std::string_view id)
{
    return id == "part.extrude" || id == "part.thicken" || id == "part.thicken_to_plane"
        || id == "part.thickness_placement"
        || id == "part.from_wire_cage" || id == "part.boolean_add"
        || id == "part.boolean_cut";
}

void V2MainWindow::RunPartCommand(std::string_view id)
{
    if (id == "part.extrude") {
        RunExtrude();
        return;
    }
    if (id == "part.thicken") {
        RunThickenSurface();
        return;
    }
    if (id == "part.thicken_to_plane") {
        RunThickenSurfaceToPlane();
        return;
    }
    if (id == "part.thickness_placement") {
        using kachakacha::v2::fabrication::ThicknessPlacement;
        // 外側 → 中央 → 内側 → 外側。次に厚みを付けるときに効く。
        thicknessPlacement_ = thicknessPlacement_ == ThicknessPlacement::Outside
            ? ThicknessPlacement::Centered
            : thicknessPlacement_ == ThicknessPlacement::Centered ? ThicknessPlacement::Inside
                                                                  : ThicknessPlacement::Outside;
        SetStatus(QStringLiteral("厚みの付け方: %1(次に面へ厚みを付けるときに効きます)")
                .arg(QString::fromUtf8(
                    kachakacha::v2::fabrication::ThicknessPlacementNameJa(thicknessPlacement_))));
        return;
    }
    if (id == "part.from_wire_cage") {
        RunWireCage();
        return;
    }
    RunBoolean(id == "part.boolean_cut");
}

kachakacha::v2::app::ExtrudeFacts V2MainWindow::BuildExtrudeFacts(
    const std::vector<kachakacha::v2::modeling::ExtrudeProfile>& profiles) const
{
    kachakacha::v2::app::ExtrudeFacts facts;
    for (const auto& profile : profiles) {
        if (profile.closed) {
            ++facts.closedProfiles;
        } else {
            ++facts.openProfiles;
        }
    }
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == kachakacha::v2::domain::EntityKind::Part) {
            ++facts.parts;
        } else if (entity->kind == kachakacha::v2::domain::EntityKind::WorkPlane) {
            ++facts.targetPlanes;
        }
    }
    return facts;
}

std::vector<ExtrudeTargetChoice> V2MainWindow::ExtrudeTargets() const
{
    // 「ある面まで」の相手。いまは作業平面を相手にできる。
    // 部品の1つの面はまだ選べないので、相手には出さない。
    // 出しておいて選べないと、選べるつもりで押して断られることになる。
    std::vector<ExtrudeTargetChoice> targets;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind != kachakacha::v2::domain::EntityKind::WorkPlane) {
            continue;
        }
        targets.push_back(ExtrudeTargetChoice{entity.id,
            QString::fromStdString(entity.displayName.empty() ? std::string("作業平面")
                                                              : entity.displayName)});
    }
    return targets;
}

void V2MainWindow::RunExtrude()
{
    using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
    using kachakacha::v2::modeling::ExtrudeRequest;

    const auto& selection = viewport_->Selection();
    auto profiles = ExtrudeProfilesFor(selection.entityIds);
    if (profiles.empty()) {
        SetStatus(QStringLiteral("押し出し: 先にワイヤーを選んでください。"));
        return;
    }
    // 何を作るか、どこまで押すかを選ばせる。core は7通りの向きと5通りの終端を
    // 持っているのに、画面が1通りに固定していた。工程の案内はそれを前提に
    // 書いてあるので、案内と実物が食い違っていた。
    const auto facts = BuildExtrudeFacts(profiles);
    kachakacha::v2::app::ExtrudeChoice choice = extrudeChoice_;
    choice.distanceMm = ExtrudeDistanceMm();
    choice.hasSelectedPart = facts.parts > 0;
    if (extrudeChooser_) {
        const auto answered = extrudeChooser_(choice, facts);
        if (!answered.has_value()) {
            SetStatus(QStringLiteral("押し出し: やめました。"));
            return;
        }
        choice = *answered;
    }
    const auto checked = kachakacha::v2::app::ValidateExtrudeChoice(choice, facts);
    if (!checked.HasValue()) {
        ReportDiagnostics(checked.Diagnostics());
        return;
    }
    extrudeChoice_ = choice;
    std::optional<kachakacha::v2::modeling::WorkPlaneFrame> targetPlane;
    if (choice.targetEntityId.has_value()) {
        targetPlane = WorkPlaneFrameOf(*choice.targetEntityId);
    }
    ExtrudeRequest request = kachakacha::v2::app::ToExtrudeRequest(choice,
        std::move(profiles), viewport_->WorkPlane(), targetPlane);
    // カーネルには輪郭も側面も常に頼む。画面に出す辺と、型紙に要る「平らな1枚」が
    // そこから取れるからである。**文書のワイヤーにするかどうかは別の話** で、
    // それは利用者が選んだとおりにする。
    request.outputs.endProfileWire = true;
    request.outputs.sideBoundaryWires = true;

    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    // まず調べる。通らないものは作らせない。作らせてから断ると理由を言えない。
    const auto analysis = AnalyzeExtrudeRequest(request, tolerance);
    if (!analysis.HasValue()) {
        ReportDiagnostics(analysis.Diagnostics());
        return;
    }
    const auto built = kachakacha::v2::kernel::BuildExtrude(request, analysis.Value(),
        tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    if (built.Value().parts.empty() && choice.makePart) {
        SetStatus(QStringLiteral("押し出し: 立体になりませんでした。"));
        return;
    }
    kachakacha::v2::domain::ExtrudeDefinition definition;
    definition.profiles = selection.entityIds;
    // 向きは実際に押した向きを持つ。作業平面の法線を書き写すと、
    // 別の向きで押したときに、開き直すと違う向きへ押されてしまう。
    definition.direction = analysis.Value().direction;
    definition.distance.value = choice.distanceMm;
    definition.distance.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.extentMode = static_cast<int>(choice.extent);
    definition.booleanMode = static_cast<int>(choice.booleanMode);
    if (choice.targetEntityId.has_value()) {
        definition.targets.push_back(*choice.targetEntityId);
    }
    std::vector<CurveSegment> edges;
    for (const auto& wire : built.Value().endProfileWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    for (const auto& wire : built.Value().sideBoundaryWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    AdoptExtrudeResult(choice, definition, built.Value(), edges);
}

void V2MainWindow::AdoptExtrudeResult(const kachakacha::v2::app::ExtrudeChoice& choice,
    const kachakacha::v2::domain::ExtrudeDefinition& definition,
    const kachakacha::v2::kernel::ExtrudeBuildResult& built,
    const std::vector<CurveSegment>& edges)
{
    // 出来た立体を全部残す。先頭の1つだけを覚えていたので、
    // 穴あきの輪郭などで2つ以上出来たときに、残りが消えていた。
    //
    // ただし「ワイヤーだけ作る」と言われたら、立体は作らない。
    // カーネルは outputs によらず内部で立体を作るので、
    // 返ってきたからといって文書へ入れてはいけない。
    kachakacha::v2::base::EntityId partId;
    const std::size_t partCount = choice.makePart ? built.parts.size() : 0;
    for (std::size_t index = 0; index < partCount; ++index) {
        auto copy = definition;
        const std::string label = built.parts.size() > 1
            ? "押し出し " + std::to_string(index + 1)
            : std::string("押し出し");
        const auto made = AddPartFeature(kachakacha::v2::domain::FeatureType::Extrude,
            std::move(copy), built.parts[index].handle,
            index == 0 ? edges : std::vector<CurveSegment>{}, label.c_str());
        if (index == 0) {
            partId = made;
        }
    }
    // 型紙にするときは「平らな1枚」が要る。押し出しの端の輪郭がそれである。
    // 立体の辺を全部渡すと、厚みのぶんだけ平面から外れて FAB-P004 で断られる。
    if (!partId.IsNil() && !built.endProfileWires.empty()) {
        partFlatBoundary_[partId.ToString()] = built.endProfileWires.front();
    }
    // 頼まれたワイヤーは、画面に出すだけの辺ではなく **文書のワイヤー** にする。
    // 辺のままだと、選ぶことも、次の押し出しの輪郭にすることもできない。
    int wires = 0;
    if (choice.makeEndProfileWire) {
        for (const auto& wire : built.endProfileWires) {
            if (!AddPlainWire(wire, "押し出し先の輪郭").IsNil()) {
                ++wires;
            }
        }
    }
    if (choice.makeSideBoundaryWires) {
        for (const auto& wire : built.sideBoundaryWires) {
            if (!AddPlainWire(wire, "側面の境界").IsNil()) {
                ++wires;
            }
        }
    }
    if (partCount == 0) {
        AdoptCurrentDocument();
        SetStatus(QStringLiteral("押し出し: ワイヤーを %1 本作りました(立体は作っていません)。")
                .arg(wires));
        return;
    }
    SetStatus(QStringLiteral("押し出し: 厚み %1 mm の部品を %2 個"
                             "、ワイヤーを %3 本作りました(体積 %4 mm3)。")
            .arg(choice.distanceMm)
            .arg(static_cast<int>(partCount))
            .arg(wires)
            .arg(built.totalVolumeMm3));
}

void V2MainWindow::RunWireCage()
{
    using kachakacha::v2::modeling::AnalyzeWireCage;
    using kachakacha::v2::modeling::CageEdgeInput;
    using kachakacha::v2::modeling::PlanWireCageParts;

    const auto& selection = viewport_->Selection();
    std::vector<CageEdgeInput> inputs;
    for (const auto& curve : session_->Scene().curves) {
        if (!kachakacha::v2::app::IsSelected(selection, curve.entityId)) {
            continue;
        }
        inputs.push_back(CageEdgeInput{curve.entityId, curve.segmentId, curve.segment});
    }
    if (inputs.size() < 3) {
        SetStatus(QStringLiteral(
            "ワイヤー群から部品: 閉じたかごになる線を3本以上選んでください。"));
        return;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    // まず調べる。閉じていないかごは、ここで断る。
    const auto analysis = AnalyzeWireCage(inputs, tolerance);
    if (!analysis.HasValue()) {
        ReportDiagnostics(analysis.Diagnostics());
        return;
    }
    // 出来たシェルを全部部品にする。1シェル=1部品。まとめない。
    std::vector<std::size_t> shells;
    for (std::size_t index = 0; index < analysis.Value().shells.size(); ++index) {
        shells.push_back(index);
    }
    const auto planned = PlanWireCageParts(analysis.Value(), shells);
    if (!planned.HasValue()) {
        ReportDiagnostics(planned.Diagnostics());
        return;
    }
    const auto built = kachakacha::v2::kernel::BuildWireCageParts(inputs, analysis.Value(),
        planned.Value(), tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    if (built.Value().empty()) {
        SetStatus(QStringLiteral("ワイヤー群から部品: 立体になりませんでした。"));
        return;
    }
    kachakacha::v2::domain::CreatePartFromWireCageDefinition definition;
    definition.wires = selection.entityIds;
    std::vector<CurveSegment> edges;
    for (const auto& input : inputs) {
        edges.push_back(input.segment);
    }
    AddPartFeature(kachakacha::v2::domain::FeatureType::CreatePartFromWireCage,
        std::move(definition), built.Value().front().handle, edges, "かごから部品");
    SetStatus(QStringLiteral("ワイヤー群から部品: %1個の部品を作りました。")
            .arg(static_cast<int>(built.Value().size())));
}

void V2MainWindow::RunBoolean(bool cut)
{
    using kachakacha::v2::kernel::BooleanOperation;
    using kachakacha::v2::kernel::BuildBoolean;

    // 足す・引くは相手を明示して選ぶ。近い部品を勝手に選ばない(§8.4)。
    // 選んだ順で決まる。1つ目が土台、2つ目が相手である。
    const auto& selection = viewport_->Selection();
    std::vector<EntityId> parts;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr
            && entity->kind == kachakacha::v2::domain::EntityKind::Part) {
            parts.push_back(id);
        }
    }
    const QString label = cut ? QStringLiteral("引く") : QStringLiteral("足す");
    if (parts.size() != 2) {
        SetStatus(QStringLiteral("%1: 部品をちょうど2つ選んでください"
                                 "(1つ目が土台、2つ目が相手です)。")
                .arg(label));
        return;
    }
    const auto base = partShapes_.find(parts[0].ToString());
    const auto other = partShapes_.find(parts[1].ToString());
    if (base == partShapes_.end() || other == partShapes_.end()) {
        SetStatus(QStringLiteral("%1: 選んだ部品の立体がまだありません。").arg(label));
        return;
    }
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto built = BuildBoolean(
        cut ? BooleanOperation::Difference : BooleanOperation::Union, base->second,
        other->second, tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    kachakacha::v2::domain::BooleanDefinition definition;
    definition.mode = cut ? 1 : 0;
    definition.targets.push_back(parts[0]);
    definition.tools.push_back(parts[1]);
    // 出来た形の辺は、いまは持たない。持てるようになるまで、元の辺を使い回さない。
    // 使い回すと、足したのに元の形が見えたままになる。
    const auto madeId = AddPartFeature(kachakacha::v2::domain::FeatureType::Boolean,
        std::move(definition), built.Value().handle, {},
        cut ? "引く" : "足す");
    if (madeId.IsNil()) {
        return;
    }
    // 使い切った2つは隠す。消すと、作り方をたどれなくなる。
    (void)session_->GetDocument().Run(kachakacha::v2::document::SetVisibilityCommand(
        parts, kachakacha::v2::domain::Visibility::Hidden));
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("%1: 体積が %2 mm3 から %3 mm3 になりました。")
            .arg(label)
            .arg(built.Value().previousVolumeMm3, 0, 'f', 4)
            .arg(built.Value().volumeMm3, 0, 'f', 4));
}

kachakacha::v2::base::EntityId V2MainWindow::AddPartFeature(
    kachakacha::v2::domain::FeatureType type,
    kachakacha::v2::domain::FeatureDefinition definition,
    kachakacha::v2::modeling::KernelShapeHandle handle,
    const std::vector<kachakacha::v2::geometry::CurveSegment>& edges,
    const char* labelJa)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;

    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = type;
    feature.displayName = labelJa;
    feature.inputEntityIds = viewport_->Selection().entityIds;
    feature.definition = std::move(definition);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Part;
    entity.displayName = labelJa;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"part", entity.id, EntityKind::Part});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, labelJa));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return kachakacha::v2::base::EntityId{};
    }
    // 形そのものは文書に持たない。持つと入力と食い違う。
    // 出来た形の handle と、見せるための辺だけを画面側で覚えておく。
    partShapes_[entity.id.ToString()] = handle;
    partEdges_[entity.id.ToString()] = edges;
    AdoptCurrentDocument();
    RefreshPartEdges();
    return entity.id;
}

void V2MainWindow::RefreshPartEdges()
{
    // 部品の辺を場面へ足す。立体そのものはまだ描かないので、輪郭で見せる。
    // 描いていないものを「描いた」と言わないため、辺は補助線として出す。
    auto scene = session_->Scene();
    const auto append = [&](const std::map<std::string,
                             std::vector<CurveSegment>>& edges) {
        for (const auto& entry : edges) {
            const auto id = kachakacha::v2::base::EntityId::Parse(entry.first);
            if (!id.has_value()) {
                continue;
            }
            for (const auto& segment : entry.second) {
                scene.curves.push_back(kachakacha::v2::modeling::SnapCurve{*id,
                    ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>(), segment,
                    false});
            }
        }
    };
    append(partEdges_);
    // 形状ガイドの境界も同じように出す。出さないと、作ったのに何も見えない。
    append(guideEdges_);
    session_->SetScene(std::move(scene));
    viewport_->update();
}

std::vector<kachakacha::v2::modeling::ExtrudeProfile> V2MainWindow::ExtrudeProfilesFor(
    const std::vector<kachakacha::v2::base::EntityId>& entityIds) const
{
    // 押し出しと、開き直しの作り直しで、同じ輪郭の作り方を通す。
    // 道を分けると、開いたときだけ違う形が出来る。
    return ProfilesOfImpl(entityIds, session_->Scene(),
        session_->GetDocument().Snapshot().settings.tolerance);
}

void V2MainWindow::SetExtrudeChooser(
    std::function<std::optional<kachakacha::v2::app::ExtrudeChoice>(
        const kachakacha::v2::app::ExtrudeChoice&,
        const kachakacha::v2::app::ExtrudeFacts&)>
        chooser)
{
    extrudeChooser_ = std::move(chooser);
}

std::optional<kachakacha::v2::modeling::WorkPlaneFrame> V2MainWindow::WorkPlaneFrameOf(
    const kachakacha::v2::base::EntityId& entityId) const
{
    const auto* entity = session_->GetDocument().FindEntity(entityId);
    if (entity == nullptr) {
        return std::nullopt;
    }
    const auto* feature = session_->GetDocument().FindFeature(entity->createdBy);
    if (feature == nullptr) {
        return std::nullopt;
    }
    const auto* definition =
        std::get_if<kachakacha::v2::domain::CreateWorkPlaneDefinition>(
            &feature->definition);
    if (definition == nullptr) {
        return std::nullopt;
    }
    kachakacha::v2::modeling::WorkPlaneFrame frame;
    frame.origin = definition->origin;
    frame.normal = definition->normal;
    frame.uAxis = definition->uDirection;
    frame.vAxis = Cross(definition->normal, definition->uDirection);
    return frame;
}

void V2MainWindow::RunThickenSurface()
{
    using kachakacha::v2::fabrication::ThicknessPlacement;

    // 面に厚みを付けて立体にする。オーナーの手順の「面を各種距離でソリッド化する」。
    // ここが無かったので、断面から面までは作れるのに、その面から立体へ戻れなかった。
    std::vector<kachakacha::v2::base::EntityId> surfaces;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr
            && entity->kind == kachakacha::v2::domain::EntityKind::GuideSurface) {
            surfaces.push_back(id);
        }
    }
    if (surfaces.empty()) {
        SetStatus(QStringLiteral("面に厚みを付ける: 先に形状ガイドの面を選んでください。"));
        return;
    }
    const double thickness = ExtrudeDistanceMm();
    const ThicknessPlacement placement = thicknessPlacement_;
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    int made = 0;
    for (const auto& id : surfaces) {
        const auto found = guideShapes_.find(id.ToString());
        if (found == guideShapes_.end()) {
            SetStatus(QStringLiteral("面に厚みを付ける: 選んだ面の形がまだありません。"));
            return;
        }
        const auto built = kachakacha::v2::kernel::ThickenSurface(found->second,
            thickness, placement, tolerance);
        if (!built.HasValue()) {
            ReportDiagnostics(built.Diagnostics());
            return;
        }
        // 作り方は押し出しとは別の枠で持つ。入力が面であって輪郭ではないので、
        // 押し出しの作り直しの道を通すと「輪郭が無い」と言われて作り直せない。
        kachakacha::v2::domain::ThickenSurfaceDefinition definition;
        definition.surface = id;
        definition.thickness.value = thickness;
        definition.thickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
        definition.placement = static_cast<int>(placement);
        const auto partId = AddPartFeature(
            kachakacha::v2::domain::FeatureType::ThickenSurface, std::move(definition),
            built.Value().handle, built.Value().edges, "面に厚み");
        if (partId.IsNil()) {
            return;
        }
        ++made;
    }
    SetStatus(QStringLiteral("面に厚みを付ける: %1枚の面から厚み %2 mm(%3)の部品を作りました。")
            .arg(made)
            .arg(thickness)
            .arg(QString::fromUtf8(kachakacha::v2::fabrication::ThicknessPlacementNameJa(placement))));
}

void V2MainWindow::RunThickenSurfaceToPlane()
{
    // 「面を任意の面まで立体化」。面と作業平面の間を埋める。
    // 厚みを数で決めるのではなく、相手で決める。
    kachakacha::v2::base::EntityId surfaceId;
    kachakacha::v2::base::EntityId planeId;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == kachakacha::v2::domain::EntityKind::GuideSurface) {
            surfaceId = id;
        } else if (entity->kind == kachakacha::v2::domain::EntityKind::WorkPlane) {
            planeId = id;
        }
    }
    const auto frame = planeId.IsNil() ? std::nullopt : WorkPlaneFrameOf(planeId);
    const auto found = guideShapes_.find(surfaceId.ToString());
    if (surfaceId.IsNil() || !frame.has_value() || found == guideShapes_.end()) {
        SetStatus(QStringLiteral(
            "面を平面まで立体に: 形状ガイドの面を1つと、相手の作業平面を1つ選んでください。"));
        return;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto built = kachakacha::v2::kernel::ThickenSurfaceToPlane(found->second,
        frame->origin, frame->normal, tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    kachakacha::v2::domain::ThickenSurfaceDefinition definition;
    definition.surface = surfaceId;
    definition.targetPlane = planeId;
    definition.thickness.value = built.Value().thicknessMm;
    definition.thickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
    const auto partId = AddPartFeature(kachakacha::v2::domain::FeatureType::ThickenSurface,
        std::move(definition), built.Value().handle, built.Value().edges, "面を平面まで");
    if (partId.IsNil()) {
        return;
    }
    SetStatus(QStringLiteral(
        "面を平面まで立体に: 面と平面の間(最大 %1 mm)を埋めて、体積 %2 mm3 の部品にしました。")
            .arg(built.Value().thicknessMm, 0, 'f', 3)
            .arg(built.Value().volumeMm3, 0, 'f', 3));
}
