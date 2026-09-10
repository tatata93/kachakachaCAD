//! 形状ガイドのコマンド(V2MainWindow の一部)。
//!
//! 形状ガイドは「面をどう作るか」を役割で言い表したものである。
//! V1 は「選んだ順が入力の順」だったので、選び直すたびに面が変わった。
//! V2 は役割の表を持ち、選択とは切り離す。
//!
//! ここでやるのは3つだけ。
//!   1. 選んだ線を、断面として表へ並べる
//!   2. 表を要求へ直して、core に検査してもらう
//!   3. 通ったら OCCT に作ってもらい、Feature を1つ足す
//! 面が作れるかどうかの判断は、1つもここに書かない。

#include "V2MainWindow.h"

#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <string>
#include <utility>
#include <vector>

bool V2MainWindow::IsGuideCommand(std::string_view id)
{
    return id == "guide.create";
}

void V2MainWindow::RunGuideCommand(std::string_view id)
{
    if (id == "guide.create") {
        CreateGuideSurfaceFromSelection();
    }
}

namespace {

using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideTable;
using kachakacha::v2::modeling::GuideTableSelection;

//! 選んだ線を、選んだ順に断面として並べる。
//! 断面どうしを渡す面(ロフト)が、いちばん素直な作り方である。
struct SectionTable {
    GuideTable table;
    int sections = 0;
    std::vector<kachakacha::v2::base::Diagnostic> diagnostics;
};

//! 元ワイヤーの並びから表を作る。選択からでも、保存した作り方からでも、
//! 同じ道を通す。道を分けると、開き直したときだけ違う面が出来る。
[[nodiscard]] SectionTable CollectSectionRowsFor(
    const kachakacha::v2::document::Document& document,
    const kachakacha::v2::modeling::SnapScene& scene,
    const std::vector<kachakacha::v2::base::EntityId>& wireIds)
{
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::modeling::AddSelectionAsNewRow;

    SectionTable made;
    // 作り方は断面の数で決まる。2本なら渡すだけ(ルールド)、3本以上なら
    // なめらかに通す(ロフト)。2本にロフトは使えないし、
    // 3本をルールドで渡すと真ん中の断面が捨てられる。
    made.table.method = kachakacha::v2::modeling::GuideSurfaceMethod::RuledSections;
    for (const auto& id : wireIds) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr || entity->kind != EntityKind::Wire) {
            continue;
        }
        GuideTableSelection chosen;
        chosen.sourceWireId = id;
        chosen.label = entity->displayName;
        for (const auto& curve : scene.curves) {
            if (curve.entityId == id) {
                chosen.segments.push_back(curve.segment);
            }
        }
        if (chosen.segments.empty()) {
            continue;
        }
        const auto added = AddSelectionAsNewRow(made.table, ChainRole::Section, chosen);
        if (!added.HasValue()) {
            made.diagnostics = added.Diagnostics();
            return made;
        }
        made.table = added.Value();
        ++made.sections;
    }
    if (made.sections >= 3) {
        const auto switched = kachakacha::v2::modeling::SetGuideTableMethod(made.table,
            kachakacha::v2::modeling::GuideSurfaceMethod::LoftSections);
        if (!switched.HasValue()) {
            made.diagnostics = switched.Diagnostics();
            return made;
        }
        made.table = switched.Value();
    }
    return made;
}

[[nodiscard]] SectionTable CollectSectionRows(
    const kachakacha::v2::document::Document& document,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::app::SelectionSet& selection)
{
    return CollectSectionRowsFor(document, scene, selection.entityIds);
}

} // namespace

kachakacha::v2::base::EntityId V2MainWindow::AdoptGuideSurface(
    const kachakacha::v2::modeling::GuideTable& table,
    const kachakacha::v2::modeling::GuideSurfaceResult& built, int sections,
    const std::vector<kachakacha::v2::base::EntityId>& inputs, const std::string& label)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::CreateGuideSurfaceDefinition;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::CreateGuideSurface;
    feature.displayName = label;
    feature.inputEntityIds = inputs;
    CreateGuideSurfaceDefinition definition;
    definition.method = static_cast<int>(table.method);
    // 元ワイヤーを覚える。空のまま保存していたので、開き直しても面を作り直せなかった。
    // 一覧には名前が残るので、消えたことに気づきにくい。
    for (const auto& row : table.rows) {
        definition.roles.push_back(static_cast<int>(row.role));
        kachakacha::v2::domain::WireChainRef chain;
        for (const auto& wireId : row.sourceWireIds) {
            kachakacha::v2::domain::SegmentRef ref;
            ref.entityId = wireId;
            chain.segments.push_back(ref);
            chain.reversed.push_back(row.reversed);
        }
        definition.chains.push_back(std::move(chain));
    }
    feature.definition = std::move(definition);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::GuideSurface;
    entity.displayName = label;
    entity.createdBy = feature.id;
    feature.outputs.push_back(
        FeatureOutput{"surface", entity.id, EntityKind::GuideSurface});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, label));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return kachakacha::v2::base::EntityId{};
    }
    // 形そのものは文書に入れない。画面側が handle と境界の線を覚える。
    guideShapes_[entity.id.ToString()] = built.handle;
    guideEdges_[entity.id.ToString()] = built.boundary;
    // 標本も覚えておく。曲がった面を展開するときに要る。
    guideSamples_[entity.id.ToString()] = built.samples;
    guideTable_ = table;
    RefreshGuideTable();
    RefreshPartEdges();
    RefreshEntityList();
    RefreshCommandVisibility();
    SetStatus(QStringLiteral(
        "形状ガイド: %1で断面%2枚から面を作りました。線からのずれは最大 %3 mm です。")
            .arg(sections >= 3 ? QStringLiteral("ロフト") : QStringLiteral("ルールド"))
            .arg(sections)
            .arg(built.maximumDeviationMm, 0, 'f', 4));
    return entity.id;
}

kachakacha::v2::base::EntityId V2MainWindow::CreateGuideSurfaceFromWires(
    const std::vector<kachakacha::v2::base::EntityId>& wireIds, const std::string& label)
{
    // 固定など、選択ではなく id で指した線から面を作る。作り方は guide.create と同じ。
    const SectionTable made =
        CollectSectionRowsFor(session_->GetDocument(), session_->Scene(), wireIds);
    if (!made.diagnostics.empty()) {
        ReportDiagnostics(made.diagnostics);
        return kachakacha::v2::base::EntityId{};
    }
    if (made.sections < 2) {
        SetStatus(QStringLiteral("形状ガイド: 断面が2つ以上要ります。"));
        return kachakacha::v2::base::EntityId{};
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto request =
        kachakacha::v2::modeling::ToGuideSurfaceRequest(made.table, tolerance);
    if (!request.HasValue()) {
        ReportDiagnostics(request.Diagnostics());
        return kachakacha::v2::base::EntityId{};
    }
    const auto analysis =
        kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request.Value(), tolerance);
    if (!analysis.HasValue()) {
        ReportDiagnostics(analysis.Diagnostics());
        return kachakacha::v2::base::EntityId{};
    }
    const auto built = kachakacha::v2::kernel::BuildGuideSurface(request.Value(),
        analysis.Value(), tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return kachakacha::v2::base::EntityId{};
    }
    return AdoptGuideSurface(made.table, built.Value(), made.sections, wireIds, label);
}

bool V2MainWindow::BuildGuideSurfaceInto(
    const std::vector<kachakacha::v2::base::EntityId>& wireIds,
    const kachakacha::v2::base::EntityId& output)
{
    // 開き直したときの作り直し。作ったときと同じ道を通す。
    // Feature はもう文書にあるので、ここでは形だけを作って覚える。
    const SectionTable made =
        CollectSectionRowsFor(session_->GetDocument(), session_->Scene(), wireIds);
    if (!made.diagnostics.empty() || made.sections < 2) {
        return false;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto request =
        kachakacha::v2::modeling::ToGuideSurfaceRequest(made.table, tolerance);
    if (!request.HasValue()) {
        return false;
    }
    const auto analysis =
        kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request.Value(), tolerance);
    if (!analysis.HasValue()) {
        return false;
    }
    const auto built = kachakacha::v2::kernel::BuildGuideSurface(request.Value(),
        analysis.Value(), tolerance);
    if (!built.HasValue()) {
        return false;
    }
    guideShapes_[output.ToString()] = built.Value().handle;
    guideEdges_[output.ToString()] = built.Value().boundary;
    guideSamples_[output.ToString()] = built.Value().samples;
    guideTable_ = made.table;
    return true;
}

void V2MainWindow::CreateGuideSurfaceFromSelection()
{
    const SectionTable made = CollectSectionRows(session_->GetDocument(),
        session_->Scene(), viewport_->Selection());
    if (!made.diagnostics.empty()) {
        ReportDiagnostics(made.diagnostics);
        return;
    }
    if (made.sections < 2) {
        // 1枚では渡す相手がいない。作れないことを、作れたことにしない。
        SetStatus(QStringLiteral(
            "形状ガイド: 断面が2つ以上要ります(いまは%1つ)。線を2本以上選んでください。")
                .arg(made.sections));
        return;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto request =
        kachakacha::v2::modeling::ToGuideSurfaceRequest(made.table, tolerance);
    if (!request.HasValue()) {
        ReportDiagnostics(request.Diagnostics());
        return;
    }
    // まず調べる。通らないものは作らせない。作らせてから断ると理由を言えない。
    const auto analysis =
        kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request.Value(), tolerance);
    if (!analysis.HasValue()) {
        ReportDiagnostics(analysis.Diagnostics());
        return;
    }
    const auto built = kachakacha::v2::kernel::BuildGuideSurface(request.Value(),
        analysis.Value(), tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    (void)AdoptGuideSurface(made.table, built.Value(), made.sections,
        viewport_->Selection().entityIds, "形状ガイド");
}
