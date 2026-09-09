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

[[nodiscard]] SectionTable CollectSectionRows(
    const kachakacha::v2::document::Document& document,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::app::SelectionSet& selection)
{
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::modeling::AddSelectionAsNewRow;

    SectionTable made;
    made.table.method = kachakacha::v2::modeling::GuideSurfaceMethod::LoftSections;
    for (const auto& id : selection.entityIds) {
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
    return made;
}

} // namespace

void V2MainWindow::AdoptGuideSurface(const kachakacha::v2::modeling::GuideTable& table,
    const kachakacha::v2::modeling::GuideSurfaceResult& built, int sections)
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
    feature.displayName = "形状ガイド";
    feature.inputEntityIds = viewport_->Selection().entityIds;
    CreateGuideSurfaceDefinition definition;
    definition.method =
        static_cast<int>(kachakacha::v2::modeling::GuideSurfaceMethod::LoftSections);
    for (const auto& row : table.rows) {
        definition.roles.push_back(static_cast<int>(row.role));
        definition.chains.push_back(kachakacha::v2::domain::WireChainRef{});
    }
    feature.definition = std::move(definition);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::GuideSurface;
    entity.displayName = "形状ガイド";
    entity.createdBy = feature.id;
    feature.outputs.push_back(
        FeatureOutput{"surface", entity.id, EntityKind::GuideSurface});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, "形状ガイド"));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    // 形そのものは文書に入れない。画面側が handle と境界の線を覚える。
    guideShapes_[entity.id.ToString()] = built.handle;
    guideEdges_[entity.id.ToString()] = built.boundary;
    guideTable_ = table;
    RefreshGuideTable();
    RefreshPartEdges();
    RefreshEntityList();
    RefreshCommandVisibility();
    SetStatus(QStringLiteral(
        "形状ガイド: 断面%1枚から面を作りました。線からのずれは最大 %2 mm です。")
            .arg(sections)
            .arg(built.maximumDeviationMm, 0, 'f', 4));
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
    AdoptGuideSurface(made.table, built.Value(), made.sections);
}
