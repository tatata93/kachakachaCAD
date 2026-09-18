//! 形状ガイドのコマンド(V2MainWindow の一部)。
//!
//! 形状ガイドは「面をどう作るか」を役割で言い表したものである。
//! V1 は「選んだ順が入力の順」だったので、選び直すたびに面が変わった。
//! V2 は役割の表を持ち、選択とは切り離す。
//!
//! ここでやるのは3つだけ。
//!   1. 表を要求へ直して、core に検査してもらう
//!   2. 通ったら OCCT に作ってもらい、Feature を1つ足す
//!   3. 開き直したときに、保存した作り方から同じ道で作り直す
//! 表そのものを組み立てる操作(役割・向き・順)は V2GuideTableCommands.cpp にある。
//! 面が作れるかどうかの判断は、1つもここに書かない。

#include "V2MainWindow.h"

#include "kachakacha/app/ExplorerModel.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/RevolveSurface.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QString>

#include <string>
#include <utility>
#include <vector>

using kachakacha::v2::app::AutoSectionTable;
using kachakacha::v2::app::GuideTableDraft;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideTable;

bool V2MainWindow::IsGuideCommand(std::string_view id)
{
    // 「面を作る」も同じ係が受ける。中では今までの表をそのまま使う。
    return id.rfind("guide.", 0) == 0 || id == "surface.create";
}

void V2MainWindow::RunGuideCommand(std::string_view id)
{
    if (id == "surface.create") {
        // **人が使う入口はこれ1つ**(オーナー指示 §10)。
        RunSurfaceCreate();
        return;
    }
    if (id == "guide.create") {
        CreateGuideSurfaceFromSelection();
        return;
    }
    if (id == "guide.revolve") {
        CreateRevolvedSurface();
        return;
    }
    RunGuideTableCommand(id);
}

void V2MainWindow::CreateRevolvedSurface()
{
    using kachakacha::v2::domain::EntityKind;
    // 1本目が断面、2本目が軸。V1 は X/Y/Z のコンボだったが、V2 は線を選ぶ。
    std::vector<kachakacha::v2::base::EntityId> wires;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::Wire) {
            wires.push_back(id);
        }
    }
    const auto curvesOf = [this](const kachakacha::v2::base::EntityId& id) {
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(id);
        return kachakacha::v2::app::SelectedCurves(one, session_->Scene());
    };
    const auto& values = parameterDock_->Values();
    // 断面・軸・角度の検査は core(REV-E001〜E005)。断面の数は回転面では使わない。
    const auto request = kachakacha::v2::app::MakeRevolveRequest(
        wires.empty() ? std::vector<kachakacha::v2::geometry::CurveSegment>{} : curvesOf(wires[0]),
        wires.size() < 2 ? std::vector<kachakacha::v2::geometry::CurveSegment>{} : curvesOf(wires[1]),
        kachakacha::v2::app::ParameterValueOf(values,
            kachakacha::v2::app::ParameterId::RevolveAngleDeg));
    if (!request.HasValue()) {
        ReportDiagnostics(request.Diagnostics());
        return;
    }
    // 回転面は形状ガイドの作り方の 1 つ(Revolve)。断面 1 本の表に軸と角度を添える。
    // 回した写しを並べてロフトすると、断面の並び順が重心で決められて崩れ、
    // 一周では最初と最後が重なって断られる。面そのものを回して作る。
    GuideTableDraft made = AutoSectionTable(session_->GetDocument(), session_->Scene(),
        {wires[0]});
    if (!made.diagnostics.empty()) {
        ReportDiagnostics(made.diagnostics);
        return;
    }
    const auto switched = kachakacha::v2::modeling::SetGuideTableMethod(made.table,
        GuideSurfaceMethod::Revolve);
    if (!switched.HasValue()) {
        ReportDiagnostics(switched.Diagnostics());
        return;
    }
    GuideTable table = switched.Value();
    table.revolveAxisPoint = request.Value().axisPoint;
    table.revolveAxisDirection = request.Value().axisDirection;
    table.revolveAngleRad = request.Value().angleDeg * 3.14159265358979323846 / 180.0;
    const auto built = BuildSurfaceFromTable(table, true);
    if (!built.has_value()) {
        return;
    }
    const auto surfaceId = AdoptGuideSurface(table, *built, {wires[0], wires[1]}, "回転面");
    if (surfaceId.IsNil()) {
        return;
    }
    kachakacha::v2::app::SelectionSet next;
    next.entityIds.push_back(surfaceId);
    viewport_->SetSelection(next);
    SetStatus(QStringLiteral("回転体: %1° 回して形状ガイド「回転面」を作りました(ずれ %2 mm)。")
            .arg(request.Value().angleDeg, 0, 'f', 1)
            .arg(built->maximumDeviationMm, 0, 'f', 4));
}

//! 「回転体」を押した。面を作るの道具を、作り方 = 回転体 で構える。
//! 選んであった線は、選んだ順に 断面 → 軸 へ入れる(選んでから押す道も残す)。
//! 構えている間の2度目は確定。
void V2MainWindow::RunRevolveTool()
{
    using kachakacha::v2::domain::EntityKind;
    if (surfaceShelfShown_) {
        ConfirmSurface();
        return;
    }
    std::vector<kachakacha::v2::base::EntityId> wires;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::Wire) {
            wires.push_back(id);
        }
    }
    surfaceInput_ = kachakacha::v2::app::SurfaceInputState{};
    surfaceInput_.method = GuideSurfaceMethod::Revolve;
    surfaceInput_.methodChosenByUser = true;
    if (!wires.empty()) {
        surfaceInput_.sections.push_back(wires[0]);
    }
    if (wires.size() >= 2) {
        surfaceInput_.guides.push_back(wires[1]);   // 軸
    }
    // 取り込みは済ませた。RunSurfaceCreate に二重に取り込ませない。
    viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
    RunSurfaceCreate();
    surfaceInput_ = kachakacha::v2::app::WithSurfaceSlotAdvanced(surfaceInput_);
    MirrorSurfaceEntriesToSelection();
    RefreshSurfacePreview();
    RefreshSurfaceRoleLabels();
    RefreshSurfaceDock();
    SetStatus(QStringLiteral("回転体\n3D で断面の線を押し、次に軸の直線を押してください。"
                             "角度は数の棚の「回転体の角度」。Enter で確定、Esc でやめます。\n%1")
            .arg(QString::fromUtf8(
                kachakacha::v2::app::SurfaceActiveSlotHintJa(surfaceInput_).c_str())));
}

kachakacha::v2::base::Result<kachakacha::v2::app::RevolveRequest>
V2MainWindow::RevolveAxisFromInput() const
{
    const auto curvesOf = [this](const kachakacha::v2::base::EntityId& id) {
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(id);
        return kachakacha::v2::app::SelectedCurves(one, session_->Scene());
    };
    const auto empty = std::vector<kachakacha::v2::geometry::CurveSegment>{};
    return kachakacha::v2::app::MakeRevolveRequest(
        surfaceInput_.sections.empty() ? empty : curvesOf(surfaceInput_.sections.front()),
        surfaceInput_.guides.empty() ? empty : curvesOf(surfaceInput_.guides.front()),
        kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(),
            kachakacha::v2::app::ParameterId::RevolveAngleDeg));
}

std::optional<kachakacha::v2::modeling::GuideSurfaceResult> V2MainWindow::BuildSurfaceFromTable(
    const GuideTable& table, bool report)
{
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto request = kachakacha::v2::modeling::ToGuideSurfaceRequest(table, tolerance);
    if (!request.HasValue()) {
        if (report) {
            ReportDiagnostics(request.Diagnostics());
        }
        return std::nullopt;
    }
    // まず調べる。通らないものは作らせない。作らせてから断ると理由を言えない。
    const auto analysis =
        kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request.Value(), tolerance);
    if (!analysis.HasValue()) {
        if (report) {
            ReportDiagnostics(analysis.Diagnostics());
        }
        return std::nullopt;
    }
    // 検査が採用した断面の並びを覚える。画面の「3. 断面順」はこれを出す。
    // 押した順ではなく、**実際に作る順**を見せる(引継ぎ 2026-09-17 の 2)。
    surfaceAdoptedSections_ = kachakacha::v2::modeling::AdoptedSectionSources(
        request.Value(), analysis.Value());
    // 離した面は、元の面の実体が要る。表が指す形状ガイドの handle を渡す。
    kachakacha::v2::modeling::KernelShapeHandle source;
    for (const auto& row : table.rows) {
        if (row.role == ChainRole::SourceSurface && !row.sourceWireIds.empty()) {
            const auto found = guideShapes_.find(row.sourceWireIds.front().ToString());
            if (found != guideShapes_.end()) {
                source = found->second;
            }
        }
    }
    const auto built = kachakacha::v2::kernel::BuildGuideSurface(request.Value(),
        analysis.Value(), tolerance, source);
    if (!built.HasValue()) {
        if (report) {
            ReportDiagnostics(built.Diagnostics());
        }
        return std::nullopt;
    }
    return built.Value();
}

kachakacha::v2::base::EntityId V2MainWindow::AdoptGuideSurface(const GuideTable& table,
    const kachakacha::v2::modeling::GuideSurfaceResult& built,
    const std::vector<kachakacha::v2::base::EntityId>& inputs, const std::string& label)
{
    using kachakacha::v2::document::AddFeatureCommand;
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
    // 元ワイヤーと役割と向きを覚える。空のまま保存していたので、
    // 開き直しても面を作り直せなかった。写し方は core に1つだけ置く。
    feature.definition = kachakacha::v2::app::DefinitionFromGuideTable(table);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::GuideSurface;
    // 同じ名前を並べない(「平面」「平面 2」)。欄と一覧で見分けるため。
    entity.displayName = kachakacha::v2::app::UniqueDisplayName(
        session_->GetDocument().Snapshot(), EntityKind::GuideSurface, label);
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
    // 文書と核には面が入っていても、表示用の形状一覧を更新しなければ
    // 確定後の面は見えず、画面からも拾えない。プレビュー線を消す前に作った
    // 面を通常の表示・選択経路へ載せる。
    RefreshShapeViews();
    RefreshEntityList();
    RefreshCommandVisibility();
    SetStatus(QStringLiteral(
        "形状ガイド: %1で%2行から面を作りました。線からのずれは最大 %3 mm です。")
            .arg(QString::fromUtf8(std::string(
                kachakacha::v2::app::GuideSurfaceMethodLabelJa(table.method)).c_str()))
            .arg(static_cast<int>(table.rows.size()))
            .arg(built.maximumDeviationMm, 0, 'f', 4));
    return entity.id;
}

kachakacha::v2::base::EntityId V2MainWindow::CreateGuideSurfaceFromWires(
    const std::vector<kachakacha::v2::base::EntityId>& wireIds, const std::string& label)
{
    // 固定など、選択ではなく id で指した線から面を作る。作り方は guide.create と同じ。
    const GuideTableDraft made =
        AutoSectionTable(session_->GetDocument(), session_->Scene(), wireIds);
    if (!made.diagnostics.empty()) {
        ReportDiagnostics(made.diagnostics);
        return kachakacha::v2::base::EntityId{};
    }
    if (made.sections < 2) {
        SetStatus(QStringLiteral("形状ガイド: 断面が2つ以上要ります。"));
        return kachakacha::v2::base::EntityId{};
    }
    const auto built = BuildSurfaceFromTable(made.table, true);
    if (!built.has_value()) {
        return kachakacha::v2::base::EntityId{};
    }
    return AdoptGuideSurface(made.table, *built, wireIds, label);
}

bool V2MainWindow::RebuildGuideSurfaceShape(const kachakacha::v2::domain::Feature& feature,
    const kachakacha::v2::base::EntityId& output)
{
    // 開き直したときの作り直し。作ったときと同じ道(表 → 要求 → kernel)を通す。
    // Feature はもう文書にあるので、ここでは形だけを作って覚える。
    const auto* definition =
        std::get_if<kachakacha::v2::domain::CreateGuideSurfaceDefinition>(
            &feature.definition);
    if (definition == nullptr) {
        return false;
    }
    const auto table = kachakacha::v2::app::GuideTableFromDefinition(
        session_->GetDocument(), session_->Scene(), *definition);
    if (!table.HasValue()) {
        ReportDiagnostics(table.Diagnostics());
        return false;
    }
    const auto built = BuildSurfaceFromTable(table.Value(), true);
    if (!built.has_value()) {
        return false;
    }
    guideShapes_[output.ToString()] = built->handle;
    guideEdges_[output.ToString()] = built->boundary;
    guideSamples_[output.ToString()] = built->samples;
    guideTable_ = table.Value();
    return true;
}

void V2MainWindow::CreateGuideSurfaceFromSelection()
{
    // 「おまかせ」。選んだ線を選んだ順に断面として並べる。
    // 役割を自分で決めたいときは、表のコマンド(guide.add_row など)を使う。
    const GuideTableDraft made = AutoSectionTable(session_->GetDocument(),
        session_->Scene(), viewport_->Selection().entityIds);
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
    const auto built = BuildSurfaceFromTable(made.table, true);
    if (!built.has_value()) {
        return;
    }
    (void)AdoptGuideSurface(made.table, *built, viewport_->Selection().entityIds,
        "形状ガイド");
}

//! その作り方で面を作れるか試す。**文書は変えない。**
//!
//! 雲側は OCCT を組み立てられないので、どの作り方なら作れるかを机上で決めると
//! 往復が増える。ここを通せば、1回の往復で全部の作り方の結果が分かる。
QString V2MainWindow::TryBuildSurface(
    const kachakacha::v2::domain::CreateGuideSurfaceDefinition& definition)
{
    const auto table = kachakacha::v2::app::GuideTableFromDefinition(
        session_->GetDocument(), session_->Scene(), definition);
    if (!table.HasValue()) {
        const auto first = table.FirstDiagnostic();
        return QString::fromStdString(first.code + " " + first.summaryJa + " "
            + first.detailsJa);
    }
    const QString before = StatusText();
    const auto built = BuildSurfaceFromTable(table.Value(), true);
    const QString why = StatusText();
    SetStatus(before);
    if (built.has_value()) {
        return QString();
    }
    return why.isEmpty() ? QStringLiteral("理由が出ませんでした。") : why;
}
