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

#include <QString>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using kachakacha::v2::base::EntityId;

//! 固定する近似モデル 1 つぶんの写し。文書へ足すと entities / features の並びが作り直され、
//! ポインタは指す先を失う(実際にそれで固定の途中で落ちた)。使うものは先に写しておく。
struct FreezeJob {
    EntityId modelId;
    std::string modelName;
    kachakacha::v2::domain::CreateFabricationModelDefinition definition;
    kachakacha::v2::app::FabricationEvaluation evaluated;
};

//! 固定する近似モデル。選んでいるものを全部(選んだ順)。選んでいなければ文書の最後の 1 つ。
//! 1 つ目だけを固定していたので、2 つ選んでも 1 つしか固まらなかった。
[[nodiscard]] std::vector<FreezeJob> FreezeJobsFor(
    const kachakacha::v2::app::SelectionSet& selection,
    const kachakacha::v2::document::Document& document,
    const std::map<std::string, kachakacha::v2::app::FabricationEvaluation>& models)
{
    std::vector<EntityId> ids;
    for (const auto& id : selection.entityIds) {
        if (models.count(id.ToString()) != 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    }
    if (ids.empty()) {
        for (const auto& entity : document.Snapshot().entities) {
            if (entity.kind == kachakacha::v2::domain::EntityKind::FabricationModel
                && models.count(entity.id.ToString()) != 0) {
                ids.assign(1, entity.id);
            }
        }
    }
    std::vector<FreezeJob> jobs;
    for (const auto& id : ids) {
        const auto* entity = document.FindEntity(id);
        const auto* feature = entity == nullptr ? nullptr : document.FindFeature(entity->createdBy);
        const auto* definition = feature == nullptr
            ? nullptr
            : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
                  &feature->definition);
        const auto found = models.find(id.ToString());
        if (definition != nullptr && found != models.end()) {
            jobs.push_back(FreezeJob{id, entity->displayName, *definition, found->second});
        }
    }
    return jobs;
}

//! 近似モデルごとに固定する。全部を 1 回の元に戻すにまとめ、1 つでも作れなければ全部戻す。
template <class Freeze>
[[nodiscard]] bool FreezeEachModel(kachakacha::v2::document::Document& document,
    const std::string& label, const std::vector<FreezeJob>& jobs, Freeze&& freeze)
{
    kachakacha::v2::document::Document::Transaction transaction(document, label);
    for (const FreezeJob& job : jobs) {
        if (!freeze(job)) {
            return false;   // まとめごと無かったことにする
        }
    }
    return transaction.Commit();
}

//! 数えた結果。近似モデルが 2 つ以上なら、その数も言う。
[[nodiscard]] QString ModelCountNote(std::size_t models)
{
    return models > 1 ? QStringLiteral("(近似モデル %1 個)").arg(static_cast<int>(models))
                      : QString();
}

} // namespace

bool V2MainWindow::IsFreezeCommand(std::string_view id)
{
    return id == "derived.freeze" || id == "fabrication.freeze_state"
        || id == "fabrication.freeze_flat" || id == "fabrication.freeze_target"
        || id == "fabrication.freeze_wires";
}

void V2MainWindow::RunFreezeCommand(std::string_view id)
{
    FocusFabricationStageFor(id);   // 生成は製作の棚の 2 段目
    if (id == "derived.freeze") {
        FreezeSelectedDerived();
        return;
    }
    if (id == "fabrication.freeze_state") {
        FreezeFabricationState();
        return;
    }
    if (id == "fabrication.freeze_flat") {
        FreezeFlatOutline();
        return;
    }
    if (id == "fabrication.freeze_target") {
        FreezeTargetShape();
        return;
    }
    if (id == "fabrication.freeze_wires") {
        FreezeContourWires();
    }
}

//! 「Flat Wire」。いまの曲げ具合は変えずに、0%(平らに展開した状態)の輪郭を線にする。
//! 近似モデルは残る(正本 製作: ApproxPart自体は破壊しない)。選んだ近似モデルを全部、
//! 1 回の元に戻すで消えるようにまとめる(線を 1 本ずつ入れていたので、戻すのも 1 本ずつだった)。
void V2MainWindow::FreezeFlatOutline()
{
    const auto jobs = FreezeJobsFor(viewport_->Selection(), session_->GetDocument(),
        fabricationModels_);
    if (jobs.empty()) {
        SetStatus(QStringLiteral(
            "Flat Wire: 先に「近似」で近似モデルを作ってください。"));
        return;
    }
    int wires = 0;
    const bool committed = FreezeEachModel(session_->GetDocument(), "展開状態(0%)を線にする",
        jobs, [&](const FreezeJob& job) {
            if (!job.evaluated.bandMesh.has_value()) {
                return FreezeFlatPanels(job.evaluated.panels, wires);   // 面ごとの方式は型紙の線そのもの
            }
            // 文書の作り方は触らず、写しを 0% にして展開の姿勢を取る。
            kachakacha::v2::domain::CreateFabricationModelDefinition flat = job.definition;
            flat.masterPercent = 0.0;
            flat.creaseProgress.clear();
            flat.bandProgress.clear();
            const auto rails = kachakacha::v2::app::FoldedRailsOf(flat, job.evaluated, 0.0);
            for (std::size_t band = 0; band + 1 < rails.size(); band += 2) {
                const std::string label = job.modelName + " 部材" + std::to_string(band / 2 + 1)
                    + " (展開 0%)";
                if (AddPlainWire(PolylineOf(rails[band]), (label + " 下").c_str()).IsNil()
                    || AddPlainWire(PolylineOf(rails[band + 1]), (label + " 上").c_str()).IsNil()) {
                    return false;
                }
                wires += 2;
            }
            return true;
        });
    AdoptCurrentDocument();
    if (!committed) {
        SetStatus(QStringLiteral("Flat Wire: 途中で作れなかったので、作る前へ戻しました。"));
        return;
    }
    SetStatus(QStringLiteral("Flat Wire: 展開状態(0%)の線を %1 本作りました%2。近似モデルは残っています。")
            .arg(wires)
            .arg(ModelCountNote(jobs.size())));
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
    // 選んだものごとに 1 本ずつ固定する(全部を 1 本の線にまとめていた)。1 回の元に戻すで消える。
    const auto selection = viewport_->Selection();
    std::vector<std::pair<EntityId, std::vector<kachakacha::v2::geometry::CurveSegment>>> sources;
    for (const auto& id : selection.entityIds) {
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(id);
        auto segments = kachakacha::v2::app::SelectedCurves(one, session_->Scene());
        if (!segments.empty()) {
            sources.emplace_back(id, std::move(segments));
        }
    }
    if (sources.empty()) {
        SetStatus(QStringLiteral(
            "現在状態を固定: 線を持っている、作られたものを1つ以上選んでください。"));
        return;
    }
    int segmentCount = 0;
    bool committed = false;
    {
        kachakacha::v2::document::Document::Transaction transaction(session_->GetDocument(),
            "現在状態を固定");
        bool ok = true;
        std::vector<EntityId> originals;
        for (const auto& [id, segments] : sources) {
            if (AddPlainWire(segments, "固定").IsNil()) {
                ok = false;
                break;
            }
            segmentCount += static_cast<int>(segments.size());
            originals.push_back(id);
        }
        // 元は消さない。隠すだけ。消すと作り方をたどれなくなる。
        ok = ok
            && session_->GetDocument()
                   .Run(kachakacha::v2::document::SetVisibilityCommand(originals,
                       kachakacha::v2::domain::Visibility::Hidden))
                   .committed;
        committed = ok && transaction.Commit();
    }
    AdoptCurrentDocument();
    if (!committed) {
        SetStatus(QStringLiteral("現在状態を固定: 途中で作れなかったので、固定する前へ戻しました。"));
        return;
    }
    SetStatus(QStringLiteral(
        "現在状態を固定: %1 個のもの(%2本の線)を、作り方に付いていかない形にしました。"
        "元は隠してあります。")
            .arg(static_cast<int>(sources.size()))
            .arg(segmentCount));
}

void V2MainWindow::FreezeFabricationState()
{
    // いまの曲げ状態を、文書の普通のものにする(工程3 → 工程2 へ戻る道)。
    // 画面に出ている姿勢(FoldedRailsOf)そのものを使う。別の作り方で作り直すと、
    // 見えている形と出てくる形が食い違う ── V1 で実際に起きた(bandRails の教訓)。
    const auto jobs = FreezeJobsFor(viewport_->Selection(), session_->GetDocument(),
        fabricationModels_);
    if (jobs.empty()) {
        SetStatus(QStringLiteral(
            "現在状態を固定: 先に「製作モデルを作る」で近似モデルを作ってください。"));
        return;
    }
    int wires = 0;
    int surfaces = 0;
    int parts = 0;
    std::string stateName = "型紙の形";
    const bool committed = FreezeEachModel(session_->GetDocument(), "現在状態を固定", jobs,
        [&](const FreezeJob& job) {
            if (!job.evaluated.bandMesh.has_value()) {
                // V2 方式(面の分類)には曲げ状態の形が無い。型紙の線をそのまま置く。
                return FreezeFlatPanels(job.evaluated.panels, wires);
            }
            stateName = kachakacha::v2::app::FoldStateSummaryJa(job.definition);
            int w = 0;
            int s = 0;
            int p = 0;
            if (!FreezeWithDefinition(job.definition, job.modelName, job.evaluated, stateName, w,
                    s, p)) {
                return false;
            }
            wires += w;
            surfaces += s;
            parts += p;
            return true;
        });
    AdoptCurrentDocument();
    if (!committed) {
        SetStatus(QStringLiteral("現在状態を固定: 途中で作れなかったので、固定する前へ戻しました。"));
        return;
    }
    SetStatus(QStringLiteral("現在状態を固定(%1): 線 %2 本、面 %3 枚、部品 %4 個にしました%5。")
            .arg(QString::fromStdString(stateName))
            .arg(wires)
            .arg(surfaces)
            .arg(parts)
            .arg(ModelCountNote(jobs.size())));
}

//! 「Target 100%」。いまの曲げ具合は変えずに、100%(目標の形)の状態を固定して、
//! 固定で作るもの(freezeOutput_)の設定どおりに線や部品にする。近似モデルは残る。
void V2MainWindow::FreezeTargetShape()
{
    const auto jobs = FreezeJobsFor(viewport_->Selection(), session_->GetDocument(),
        fabricationModels_);
    if (jobs.empty()) {
        SetStatus(QStringLiteral(
            "目標形状(100%)を固定: 先に「近似」で近似モデルを作ってください。"));
        return;
    }
    int wires = 0;
    int surfaces = 0;
    int parts = 0;
    const bool committed = FreezeEachModel(session_->GetDocument(), "目標形状(100%)を固定", jobs,
        [&](const FreezeJob& job) {
            if (!job.evaluated.bandMesh.has_value()) {
                // V2 方式(面の分類)には曲げ状態の形が無い。型紙の線をそのまま置く。
                return FreezeFlatPanels(job.evaluated.panels, wires);
            }
            // 文書の作り方は触らず、写しを 100%(目標の形)にする。FreezeFlatOutline と同じ考え。
            kachakacha::v2::domain::CreateFabricationModelDefinition target = job.definition;
            target.masterPercent = 100.0;
            target.creaseProgress.clear();
            target.bandProgress.clear();
            int w = 0;
            int s = 0;
            int p = 0;
            if (!FreezeWithDefinition(target, job.modelName, job.evaluated, "目標100%", w, s, p)) {
                return false;
            }
            wires += w;
            surfaces += s;
            parts += p;
            return true;
        });
    AdoptCurrentDocument();
    if (!committed) {
        SetStatus(QStringLiteral(
            "目標形状(100%)を固定: 途中で作れなかったので、固定する前へ戻しました。"));
        return;
    }
    SetStatus(QStringLiteral(
        "目標形状(100%)を固定: 線 %1 本、面 %2 枚、部品 %3 個にしました%4。"
        "近似モデルはそのまま残っています。")
            .arg(wires)
            .arg(surfaces)
            .arg(parts)
            .arg(ModelCountNote(jobs.size())));
}

//! 「輪郭を線にする」(F-14)。いまの曲げ状態の輪郭だけを線として作る。
//! 「現在形状を生成」(freeze_state)と見た目がそっくりで、以前は同じ
//! fabrication.freeze_state を指していた張りぼてのボタンだった。ここで
//! 独立させ、固定で作るもの(freezeOutput_)の設定に関わらず線だけを作る
//! (利用者が「部品のみ」を選んでいても、この道具だけは線を作る)。
void V2MainWindow::FreezeContourWires()
{
    using kachakacha::v2::fabrication::FreezeOutput;

    const auto jobs = FreezeJobsFor(viewport_->Selection(), session_->GetDocument(),
        fabricationModels_);
    if (jobs.empty()) {
        SetStatus(QStringLiteral(
            "輪郭を線にする: 先に「近似」で近似モデルを作ってください。"));
        return;
    }
    // 固定で作るものを線のみへ一時的に切り替える。呼び終えたら必ず元へ戻す
    // (この道具が「固定で作るもの」の選び方そのものを書き換えてはならない)。
    const FreezeOutput saved = freezeOutput_;
    freezeOutput_ = FreezeOutput::WiresOnly;
    int wires = 0;
    std::string stateName = "型紙の形";
    const bool committed = FreezeEachModel(session_->GetDocument(), "輪郭を線にする", jobs,
        [&](const FreezeJob& job) {
            if (!job.evaluated.bandMesh.has_value()) {
                // V2 方式(面の分類)には曲げ状態の形が無い。型紙の線をそのまま置く。
                return FreezeFlatPanels(job.evaluated.panels, wires);
            }
            stateName = kachakacha::v2::app::FoldStateSummaryJa(job.definition);
            int w = 0;
            int s = 0;
            int p = 0;
            if (!FreezeWithDefinition(job.definition, job.modelName, job.evaluated, stateName, w,
                    s, p)) {
                return false;
            }
            wires += w;
            return true;
        });
    freezeOutput_ = saved;
    AdoptCurrentDocument();
    if (!committed) {
        SetStatus(QStringLiteral("輪郭を線にする: 途中で作れなかったので、作る前へ戻しました。"));
        return;
    }
    SetStatus(QStringLiteral(
        "輪郭を線にする(%1): 線 %2 本にしました%3。近似モデルはそのまま残っています。")
            .arg(QString::fromStdString(stateName))
            .arg(wires)
            .arg(ModelCountNote(jobs.size())));
}

//! freeze_state と freeze_target の共通の道。渡す定義が違うだけで、レールから
//! 線・面・部品を作るところから先は同じにする(コードを2度書かない)。
//! 1つの取り消しで戻せるよう、ひとまとまり(Transaction)にする。
bool V2MainWindow::FreezeWithDefinition(
    const kachakacha::v2::domain::CreateFabricationModelDefinition& definition,
    const std::string& modelName, const kachakacha::v2::app::FabricationEvaluation& evaluated,
    const std::string& stateName, int& wires, int& surfaces, int& parts)
{
    using kachakacha::v2::fabrication::FreezeOutput;
    wires = 0;
    surfaces = 0;
    parts = 0;
    kachakacha::v2::document::Document::Transaction transaction(
        session_->GetDocument(), "固定(" + stateName + ")");
    // 持ち上げ 0 で取る。画面では帯を離して見せるが、固定するのは本当の位置。
    const auto rails = kachakacha::v2::app::FoldedRailsOf(definition, evaluated, 0.0);
    for (std::size_t band = 0; band + 1 < rails.size(); band += 2) {
        const std::string label = modelName + " 部材"
            + std::to_string(band / 2 + 1) + " (" + stateName + ")";
        const auto bottom = AddPlainWire(PolylineOf(rails[band]), (label + " 下").c_str());
        const auto top = AddPlainWire(PolylineOf(rails[band + 1]), (label + " 上").c_str());
        if (bottom.IsNil() || top.IsNil()) {
            return false;
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
            return false;
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
        *evaluated.bandMesh, state.masterProgress, state.creaseFactors,
        state.unfoldBaseRail);
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
    return transaction.Commit();
}

//! 近似モデル 1 つぶんの部材を、型紙の座標そのままで作業平面の上へ線として置く。
//! 置き直さないのは、型紙で見えている形と1mmも違わせないためである。
//! その近似モデルの部材だけを置く(全部の近似モデルの部材を置いていた)。まとめは呼ぶ側。
bool V2MainWindow::FreezeFlatPanels(
    const std::vector<kachakacha::v2::fabrication::PatternPanel>& panels, int& wires)
{
    using kachakacha::v2::fabrication::PatternPlacement;
    PatternPlacement placement;
    for (const auto& panel : panels) {
        const auto placed = kachakacha::v2::fabrication::PlacePanelCurves(panel, placement);
        if (!placed.HasValue()) {
            ReportDiagnostics(placed.Diagnostics());
            return false;
        }
        std::vector<kachakacha::v2::geometry::CurveSegment> segments;
        for (const auto& curve : placed.Value()) {
            segments.push_back(curve.segment);
        }
        if (AddPlainWire(std::move(segments), "固定した部材").IsNil()) {
            return false;
        }
        ++wires;
    }
    return true;
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
