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

#include "kachakacha/app/SurfaceJig.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctWireCage.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/WireCage.h"

#include <QString>

#include <array>
#include <cstdint>
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
    const bool allOpen = profiles.size() > 1
        && std::all_of(profiles.begin(), profiles.end(), [](const auto& profile) {
               return !profile.closed;
           });
    if (!allOpen) {
        return profiles;
    }
    std::vector<kachakacha::v2::geometry::ChainInput> inputs;
    std::vector<kachakacha::v2::base::SegmentId> originalSegmentIds;
    for (const SnapCurve& curve : scene.curves) {
        if (std::find(entityIds.begin(), entityIds.end(), curve.entityId) != entityIds.end()) {
            std::array<std::uint8_t, 16> bytes{};
            const std::size_t number = inputs.size() + 1;
            bytes[15] = static_cast<std::uint8_t>(number & 0xFF);
            bytes[14] = static_cast<std::uint8_t>((number >> 8) & 0xFF);
            inputs.push_back({kachakacha::v2::base::EntityId{},
                kachakacha::v2::base::SegmentId(kachakacha::v2::base::Uuid(bytes)),
                curve.segment});
            originalSegmentIds.push_back(curve.segmentId);
        }
    }
    const auto ordered = kachakacha::v2::geometry::AnalyzeChain(inputs, tolerance);
    if (!ordered.HasValue() || !ordered.Value().order.closed) {
        return profiles;
    }
    kachakacha::v2::modeling::ExtrudeProfile combined;
    combined.sourceEntityId = entityIds.front();
    combined.closed = true;
    for (const auto& item : ordered.Value().order.segments) {
        const auto found = std::find_if(inputs.begin(), inputs.end(), [&](const auto& input) {
            return input.segmentId == item.segmentId;
        });
        if (found == inputs.end()) {
            continue;
        }
        CurveSegment segment = found->segment;
        if (item.reversed) {
            const auto reversed = kachakacha::v2::geometry::ReverseCurve(segment);
            if (!reversed.HasValue()) {
                return profiles;
            }
            segment = reversed.Value();
        }
        combined.segments.push_back(std::move(segment));
        const auto at = static_cast<std::size_t>(std::distance(inputs.begin(), found));
        combined.segmentIds.push_back(originalSegmentIds[at]);
    }
    return {std::move(combined)};
}

} // namespace

bool V2MainWindow::IsPartCommand(std::string_view id)
{
    return id == "part.extrude" || id == "part.thicken" || id == "part.thicken_to_plane"
        || id == "part.thickness_placement" || id == "part.surface_jig"
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
    if (id == "part.surface_jig") {
        RunSurfaceJig();
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

//! 押し出しの命令。窓を出す前に、まず画面で見せる。
//!
//! 「選ぶ → CADが読む → 下見 → 引くか打つ → Enter で確定」に揃える
//! (オーナー指示 2026-09-14 §2)。窓で全部決めてから作る道は、
//! 押す前に何ができるのか見えないので、初めての人には難しい。
void V2MainWindow::RunExtrude()
{
    const auto opening = PlanExtrudeFromSelection();
    // 面を押すときは、押す面の縁を **その場限りの輪郭として** 取り出す。
    // 文書はまだ変えない。下見を出しただけで文書が変わってはいけない(R1 B2)。
    if (opening.kind == kachakacha::v2::app::ExtrudeInputKind::SolidAndFace
        && !viewport_->ExtrudeHandleShown()) {
        if (!PickFaceProfile()) {
            return;   // 理由はそちらで言っている。
        }
        facePushPull_ = true;
    }
    if (!opening.readyToPreview) {
        // 足りないものを言う。「不正な入力です」で終わらせない。
        SetStatus(QStringLiteral("押し出し\n%1").arg(ExtrudePlanTextJa()));
        return;
    }
    if (!viewport_->ExtrudeHandleShown()) {
        // まだ下見が出ていない。出して待つ。Enter で確定される。
        BeginExtrudePreview();
        return;
    }
    // すでに下見が出ている状態でもう一度押されたら、確定とみなす。
    ConfirmExtrude();
}

//! 足す・引くの相手の形。NewPart なら空の番号を返す。
//!
//! ここを渡していなかったので、立体に窓を開ける押し出しは一度も通っていなかった
//! (KER-E004 で断られていた)。面の押し引き(EX-02)を通したときに露見した。
std::optional<kachakacha::v2::modeling::KernelShapeHandle>
V2MainWindow::BooleanTargetShapeFor(const kachakacha::v2::app::ExtrudeChoice& choice,
    const kachakacha::v2::app::ExtrudePlan& plan)
{
    // ソリッドを作らないなら、足す・引く相手は要らない(オーナー指示)。
    if (!choice.makePart
        || choice.booleanMode == kachakacha::v2::modeling::ExtrudeBooleanMode::NewPart) {
        return kachakacha::v2::modeling::KernelShapeHandle{};
    }
    const auto found = partShapes_.find(plan.targetSolid.ToString());
    if (found == partShapes_.end()) {
        SetStatus(QStringLiteral("押し出し: 足す・引く相手の立体が見つかりません。"
                                 "加工する立体を選び直してください。"));
        return std::nullopt;
    }
    return found->second;
}

//! 決めごと(距離・向き・演算)を整える。やめたら値を返さない。
//!
//! ConfirmExtrude から切り出したのは、1関数100行の門を越えたためである。
//! 切る場所は「決める」と「作る」の境目にした。
std::optional<V2MainWindow::PreparedExtrudeChoice> V2MainWindow::PrepareExtrudeChoice(
    const kachakacha::v2::app::ExtrudePlan& plan,
    const std::vector<kachakacha::v2::modeling::ExtrudeProfile>& profiles)
{
    const auto facts = BuildExtrudeFacts(profiles);
    // いまの選択をどう読んだかを、決める前に見せる。
    SetStatus(QStringLiteral("押し出し\n%1").arg(ExtrudePlanTextJa()));
    kachakacha::v2::app::ExtrudeChoice choice = extrudeChoice_;
    // 向きも矢印が持っている値を使う。矢印・下見・確定を1か所から取る。
    // ここを作業平面の法線のままにすると、矢印は輪郭の平面へ向いているのに
    // 作る形だけ別の向きへ進む。別の平面に引いた輪郭では
    // 「この向きでは厚みが出ません」(EXT-007)で断られる。
    if (extrudeShelfShown_ && !facePushPull_) {
        // 棚が出ているなら、そこに出ている向きの決め方がそのまま作る形になる。
        // 棚は7通りすべてを名前で出せるので、詳細の窓で選んだ向きも
        // 棚を通って戻ってくる(Codex P1-EXTRUDE-R4 B1、R5 B1、R6 B2)。
        choice.direction = extrudeDock_->DirectionMode();
        extrudeChoice_.direction = choice.direction;
    }
    // 向きの畳み込みは **ここではやらない。** 窓で選び直される前に畳むと、
    // 人が選んだ決め方が失われる(Codex P1-EXTRUDE-R6 B2)。
    // 畳むのは、窓の答えまで決まった後、作る形を作る直前だけである。
    // 距離は矢印が持っている値を使う。引いた結果と作る形を必ず一致させる。
    choice.distanceMm = viewport_->ExtrudeHandleShown()
        ? viewport_->ExtrudeHandleDistanceMm()
        : ExtrudeDistanceMm();
    choice.hasSelectedPart = facts.parts > 0;
    if (extrudeShelfShown_) {
        // 棚が出ているなら、そこに出ている値がそのまま作る形になる。
        choice.reversed = extrudeDock_->Reversed();
        choice.extent = extrudeDock_->Symmetric()
            ? kachakacha::v2::modeling::ExtrudeExtentMode::SymmetricDistance
            : kachakacha::v2::modeling::ExtrudeExtentMode::Distance;
    }
    // 既定の操作を当てるのは **入力を最初に読んだときだけ** である(R1 B4)。
    // 確定のたびに当て直すと、棚で選んだ「足す/引く/新しい部品」が
    // 表示はそのままに、実行だけ別の演算になる。
    if (extrudeShelfShown_) {
        choice.booleanMode = extrudeDock_->BooleanMode();
        // 出力は棚の「結果」欄がそのまま正本になる。
        // **見た目だけの欄にしない。**選んだとおりの物が文書に出来る。
        const auto outputs = extrudeDock_->Outputs();
        choice.makePart = outputs.body;
        choice.makeStartProfileWire = outputs.startWire;
        choice.makeEndProfileWire = outputs.endWire;
        choice.makeSideBoundaryWires = outputs.sideWires;
    } else if (plan.kind == kachakacha::v2::app::ExtrudeInputKind::SolidAndProfile) {
        choice.booleanMode = plan.defaultOperation;
    }
    // 面の押し引きは向きを面の法線へ畳む。覚えている決め方は退避しておく。
    const auto rememberedDirection = extrudeChoice_.direction;
    const auto rememberedCustom = extrudeChoice_.customDirection;
    if (facePushPull_ && !ApplyFacePushPull(choice)) {
        return std::nullopt;
    }
    // 決めごとを外から差し替える口。ふだんは空である。
    // 「詳細...」の窓はここを通らない。窓は決めたことを棚と矢印へ映して戻り、
    // 作るのは確定である(Codex P1-EXTRUDE-R7 B1)。ここを使うのは、
    // 画面を出さない自己試験だけになった。あれば従う。
    if (extrudeChooser_) {
        const auto answered = extrudeChooser_(choice, facts);
        if (!answered.has_value()) {
            SetStatus(QStringLiteral("押し出し: やめました。"));
            return std::nullopt;
        }
        choice = *answered;
    }
    const auto checked = kachakacha::v2::app::ValidateExtrudeChoice(choice, facts);
    if (!checked.HasValue()) {
        ReportDiagnostics(checked.Diagnostics());
        return std::nullopt;
    }
    PreparedExtrudeChoice prepared;
    prepared.remembered = choice;
    prepared.resolved = choice;
    if (facePushPull_) {
        // 面の押し引きが決めた向きは、その面だけのものである。覚えると、
        // 次にふつうの押し出しをしたとき、その面の法線を引きずる。
        prepared.remembered.direction = rememberedDirection;
        prepared.remembered.customDirection = rememberedCustom;
    } else if (choice.direction
        == kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal) {
        // 「輪郭に垂直」だけは、こちらとカーネルで当て方が違う。
        // こちらは下見に出している折れ線から当て、カーネルは輪郭そのものから
        // 当てるので、符号が食い違う余地が残る。**矢印で見えている向きを渡す。**
        // 残る5通りはこちらとカーネルの解き方が同じなので、畳まずに渡す。
        // 畳まなければ、向きが使えない値のときカーネルが理由を言える。
        prepared.resolved.direction
            = kachakacha::v2::modeling::ExtrudeDirectionMode::CustomXYZ;
        prepared.resolved.customDirection = ExtrudeDirectionForMode(
            choice.direction, choice.customDirection);
    }
    return prepared;
}

//! 出ている下見のとおりに作る。
void V2MainWindow::ConfirmExtrude()
{
    using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
    using kachakacha::v2::modeling::ExtrudeRequest;

    const auto& selection = viewport_->Selection();
    // **下見を出した瞬間の写しから作る。選択を読み直さない**(オーナー指示 §9)。
    //
    // これまではここで `PlanExtrudeFromSelection()` を呼んでいた。
    // 下見を出したあとに選択が変わると、画面に出ているものと作られるものが
    // 別になる。「Previewに見えていない入力でCommitしない」を守れない。
    if (!extrudeSnapshot_.has_value()) {
        // 写しが無い = 下見を出していない。何が足りないかを言って終わる。
        SetStatus(QStringLiteral("押し出し\n%1").arg(ExtrudePlanTextJa()));
        return;
    }
    const auto plan = extrudeSnapshot_->plan;
    if (!plan.readyToPreview) {
        SetStatus(QStringLiteral("押し出し\n%1").arg(ExtrudePlanTextJa()));
        return;
    }
    // 輪郭も写しから取る。**読み取りが輪郭と決めたものだけ。** 選んだもの全部ではない。
    auto profiles = extrudeSnapshot_->profiles;
    if (profiles.empty()) {
        SetStatus(QStringLiteral("押し出し: 押す輪郭が取れませんでした。"
                                 "閉じた輪郭か、立体の平らな面を選んでください。"));
        return;
    }
    // 何を作るか、どこまで押すかを選ばせる。core は7通りの向きと5通りの終端を
    // 持っているのに、画面が1通りに固定していた。工程の案内はそれを前提に
    // 書いてあるので、案内と実物が食い違っていた。
    const auto preparedOrNone = PrepareExtrudeChoice(plan, profiles);
    if (!preparedOrNone.has_value()) {
        return;   // やめたか、断った。理由はそちらで言っている。
    }
    // 作る形はこちら。向きは解いてある。
    const kachakacha::v2::app::ExtrudeChoice choice = preparedOrNone->resolved;
    // 覚えるのはこちら。人が選んだ決め方のまま持つ。窓で「X方向」や
    // 「選んだ線の向き」を選んでも、次に棚と窓へそのまま出る
    // (Codex P1-EXTRUDE-R5 B1、R6 B2)。
    extrudeChoice_ = preparedOrNone->remembered;
    std::optional<kachakacha::v2::modeling::WorkPlaneFrame> targetPlane;
    if (choice.targetEntityId.has_value()) {
        targetPlane = WorkPlaneFrameOf(*choice.targetEntityId);
    }
    // 開始側の輪郭を作るなら、押す前の輪郭をここで控える。
    // **元の輪郭を作り変えるのではなく、新しい文書のワイヤーとして作る。**
    std::vector<std::vector<CurveSegment>> startLoops;
    if (choice.makeStartProfileWire) {
        for (const auto& profile : profiles) {
            if (!profile.segments.empty()) {
                startLoops.push_back(profile.segments);
            }
        }
    }
    ExtrudeRequest request = kachakacha::v2::app::ToExtrudeRequest(choice,
        std::move(profiles), viewport_->WorkPlane(), targetPlane);
    // カーネルには輪郭も側面も常に頼む。画面に出す辺と、型紙に要る「平らな1枚」が
    // そこから取れるからである。**文書のワイヤーにするかどうかは別の話** で、
    // それは利用者が選んだとおりにする。
    request.outputs.endProfileWire = true;
    request.outputs.sideBoundaryWires = true;
    // ソリッドを作らないなら、演算そのものを行わない。
    // 行ってしまうと、相手の立体がカーネルの中で切られる。
    if (!choice.makePart) {
        request.booleanMode = kachakacha::v2::modeling::ExtrudeBooleanMode::NewPart;
    }

    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    // まず調べる。通らないものは作らせない。作らせてから断ると理由を言えない。
    const auto analysis = AnalyzeExtrudeRequest(request, tolerance);
    if (!analysis.HasValue()) {
        ReportDiagnostics(analysis.Diagnostics());
        return;
    }
    // 足す・引くには相手の形が要る。渡さないと KER-E004 で断られる。
    const auto booleanTarget = BooleanTargetShapeFor(choice, plan);
    if (!booleanTarget.has_value()) {
        return;   // 理由はそちらで言っている。
    }
    const auto built = kachakacha::v2::kernel::BuildExtrude(request, analysis.Value(),
        tolerance, *booleanTarget);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    if (built.Value().parts.empty() && choice.makePart) {
        SetStatus(QStringLiteral("押し出し: 立体になりませんでした。"));
        return;
    }
    extrudeStartLoops_ = std::move(startLoops);
    CommitExtrude(choice, plan, analysis.Value(), built.Value());
    extrudeStartLoops_.clear();
}

//! 出来た形を文書へ入れる。**1回の操作は1回の取り消しで戻る**(R1 B3)。
//!
//! 面の縁のワイヤー、押し出しの Feature とその出力、足し引きの相手の非表示 ──
//! これらは1つの操作である。ばらばらに入れると、1回取り消しても
//! 元の立体が出てくるだけで加工後の立体が残る、という中途半端な形になる。
void V2MainWindow::CommitExtrude(const kachakacha::v2::app::ExtrudeChoice& choice,
    const kachakacha::v2::app::ExtrudePlan& plan,
    const kachakacha::v2::modeling::ExtrudeAnalysis& analysis,
    const kachakacha::v2::kernel::ExtrudeBuildResult& built)
{
    const bool kept = CommitExtrudeAtomically(choice, plan, analysis, built);
    ForgetFaceProfile();
    // **どの道で抜けても、ここを必ず通る。**
    //
    // 覚えている形と場面は、文書の写しでしかない。途中で失敗して文書が戻ったのに
    // 写しだけが新しいままだと、文書に無い立体や辺が画面と書き出しに残る
    // (Codex P1-EXTRUDE-R3 B2)。だから通っても通らなくても、
    // 戻ったあとの文書から作り直す。
    AdoptCurrentDocument();
    RebuildKernelShapes();
    if (!kept) {
        SetStatus(QStringLiteral("押し出し: 途中で作れなかったので、"
                                 "押す前の状態へ戻しました。"));
    }
}

//! 文書を変えるところだけ。ここを抜けた時点で、まとめは閉じているか戻っている。
//!
//! 呼ぶ側が必ず後始末(場面と覚えている形の作り直し)をするので、
//! ここでは早く抜けてよい。
bool V2MainWindow::CommitExtrudeAtomically(const kachakacha::v2::app::ExtrudeChoice& choice,
    const kachakacha::v2::app::ExtrudePlan& plan,
    const kachakacha::v2::modeling::ExtrudeAnalysis& analysis,
    const kachakacha::v2::kernel::ExtrudeBuildResult& built)
{
    // まとめの係。`Commit()` を呼ばずに抜けたら、始める前へ戻り履歴も増えない。
    // 「一度入れてから取り消す」方式だと、押す前にやっていた別の操作を
    // 取り消してしまう(Codex P1-EXTRUDE-R2 B1)。
    kachakacha::v2::document::Document::Transaction transaction(session_->GetDocument(),
        "押し出し");
    std::vector<kachakacha::v2::base::EntityId> faceWires;
    if (facePushPull_ && !CommitFaceProfileWires(faceWires)) {
        // 途中で入らなかった。まとめごと無かったことにする。外周だけ残さない。
        return false;
    }
    kachakacha::v2::domain::ExtrudeDefinition definition;
    // 面の押し引きは、いま作った縁のワイヤーが押し出しの元になる。
    // 記録する輪郭も、読み取りが輪郭と決めたものだけ。選択を読み直さない。
    // 読み直すと、足す・引くの相手の立体まで輪郭として記録され、
    // 開き直したときに違うものを押そうとする。
    definition.profiles = facePushPull_ ? faceWires : plan.profiles;
    // 向きは実際に押した向きを持つ。作業平面の法線を書き写すと、
    // 別の向きで押したときに、開き直すと違う向きへ押されてしまう。
    definition.direction = analysis.direction;
    definition.distance.value = choice.distanceMm;
    definition.distance.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.extentMode = static_cast<int>(choice.extent);
    definition.booleanMode = static_cast<int>(choice.booleanMode);
    // 足す・引くの相手は「加工する立体」である。開き直したときも同じ相手へ当てる。
    // **ソリッドを作らないなら、足す・引くは起きない**(オーナー指示)。
    // 起きない演算で相手を隠すと、押していない立体が画面から消える。
    const bool boolean = choice.makePart
        && choice.booleanMode != kachakacha::v2::modeling::ExtrudeBooleanMode::NewPart;
    if (boolean && !plan.targetSolid.IsNil()) {
        definition.targets.push_back(plan.targetSolid);
    } else if (choice.targetEntityId.has_value()) {
        // 「ある面まで」の相手(作業平面)。足し引きの相手とは別物である。
        definition.targets.push_back(*choice.targetEntityId);
    }
    std::vector<CurveSegment> edges;
    for (const auto& wire : built.endProfileWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    for (const auto& wire : built.sideBoundaryWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    // 押し出しが何に依っているかを明示して渡す。画面の選択を読み直させない。
    // 読み直すと、記録が指す番号と依存の番号が食い違い、
    // 元を編集しても作り直されない(R2 B2)。
    std::vector<kachakacha::v2::base::EntityId> inputs = definition.profiles;
    if (boolean && !plan.targetSolid.IsNil()) {
        inputs.push_back(plan.targetSolid);
    }
    if (!AdoptExtrudeResult(choice, definition, built, edges, inputs)) {
        return false;   // まとめごと無かったことにする。途中の形を残さない。
    }
    // 使い切った元の立体は隠す。出したままだと加工前と加工後が2つ並んで見える。
    // 消さないのは、作り方をたどれなくしないためである(足し引きと同じ扱い)。
    if (boolean && !plan.targetSolid.IsNil()) {
        const auto hidden = session_->GetDocument().Run(
            kachakacha::v2::document::SetVisibilityCommand({plan.targetSolid},
                kachakacha::v2::domain::Visibility::Hidden));
        if (!hidden.committed) {
            ReportDiagnostics(hidden.diagnostics);
            return false;
        }
    }
    return transaction.Commit();
}

//! 抱えている面の縁を、押し出しの輪郭にする。文書へは入れない。
std::vector<kachakacha::v2::modeling::ExtrudeProfile> V2MainWindow::FaceProfilesNow() const
{
    std::vector<kachakacha::v2::modeling::ExtrudeProfile> profiles;
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    for (const auto& loop : faceProfileLoops_) {
        kachakacha::v2::modeling::ExtrudeProfile profile;
        profile.segments = loop;
        profile.closed = kachakacha::v2::geometry::SegmentsFormClosedLoop(loop, tolerance);
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

bool V2MainWindow::AdoptExtrudeResult(const kachakacha::v2::app::ExtrudeChoice& choice,
    const kachakacha::v2::domain::ExtrudeDefinition& definition,
    const kachakacha::v2::kernel::ExtrudeBuildResult& built,
    const std::vector<CurveSegment>& edges,
    const std::vector<kachakacha::v2::base::EntityId>& inputs)
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
            index == 0 ? edges : std::vector<CurveSegment>{}, label.c_str(), inputs);
        if (made.IsNil()) {
            return false;   // 1つでも入らなければ、全体を無かったことにする。
        }
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
    // 開始側の輪郭。**押す前の輪郭を写した、新しいワイヤーである。**
    // 元のワイヤーを作り変えて代用しない(オーナー指示)。
    if (choice.makeStartProfileWire) {
        for (const auto& loop : extrudeStartLoops_) {
            if (AddPlainWire(loop, "押し出し元の輪郭").IsNil()) {
                return false;
            }
            ++wires;
        }
    }
    if (choice.makeEndProfileWire) {
        for (const auto& wire : built.endProfileWires) {
            if (AddPlainWire(wire, "押し出し先の輪郭").IsNil()) {
                return false;
            }
            ++wires;
        }
    }
    if (choice.makeSideBoundaryWires) {
        for (const auto& wire : built.sideBoundaryWires) {
            if (AddPlainWire(wire, "側面の境界").IsNil()) {
                return false;
            }
            ++wires;
        }
    }
    // 作り終えたら下見と矢印を片付ける。残すと、もう作られない形が画面に残る。
    EndExtrudePreview();
    if (partCount == 0) {
        SetStatus(QStringLiteral("押し出し: ワイヤーを %1 本作りました(立体は作っていません)。")
                .arg(wires));
        return true;
    }
    SetStatus(QStringLiteral("押し出し: 厚み %1 mm の部品を %2 個"
                             "、ワイヤーを %3 本作りました(体積 %4 mm3)。")
            .arg(choice.distanceMm)
            .arg(static_cast<int>(partCount))
            .arg(wires)
            .arg(built.totalVolumeMm3));
    return true;
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
    const char* labelJa, const std::vector<kachakacha::v2::base::EntityId>& inputs)
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
    // 何に依っているかは **呼ぶ側が渡す。** 画面の選択を読み直すと、
    // 記録が指す番号と依存の番号が食い違い、元を編集しても作り直されない(R2 B2)。
    feature.inputEntityIds = inputs.empty() ? viewport_->Selection().entityIds : inputs;
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

kachakacha::v2::base::EntityId V2MainWindow::JigContactSurface(
    kachakacha::v2::base::EntityId sourceId, const kachakacha::v2::app::SurfaceJigPlan& plan)
{
    if (!plan.NeedsOffsetSurface()) {
        return sourceId;   // すき間 0。元の面にぴったり当てる。
    }
    // すき間だけ離した面。作り方は「離した面」なので、元の面を直せば治具も付いてくる。
    kachakacha::v2::modeling::GuideTable table;
    table.method = kachakacha::v2::modeling::GuideSurfaceMethod::OffsetGuide;
    table.offsetDistanceMm = plan.offsetDistanceMm;
    const auto* entity = session_->GetDocument().FindEntity(sourceId);
    const auto added = kachakacha::v2::modeling::AddSourceSurfaceRow(table, sourceId,
        entity != nullptr ? entity->displayName : std::string("面"));
    if (!added.HasValue()) {
        ReportDiagnostics(added.Diagnostics());
        return kachakacha::v2::base::EntityId{};
    }
    const auto built = BuildSurfaceFromTable(added.Value(), true);
    if (!built.has_value()) {
        return kachakacha::v2::base::EntityId{};
    }
    return AdoptGuideSurface(added.Value(), *built, {sourceId}, "治具の当たり面");
}

bool V2MainWindow::AddJigSolid(kachakacha::v2::base::EntityId contactId,
    const kachakacha::v2::app::SurfaceJigPlan& plan)
{
    const auto found = guideShapes_.find(contactId.ToString());
    if (found == guideShapes_.end()) {
        SetStatus(QStringLiteral("治具: 当たり面の形がまだありません。"));
        return false;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto solid = kachakacha::v2::kernel::ThickenSurface(found->second,
        plan.thicknessMm, plan.placement, tolerance);
    if (!solid.HasValue()) {
        ReportDiagnostics(solid.Diagnostics());
        return false;
    }
    kachakacha::v2::domain::ThickenSurfaceDefinition definition;
    definition.surface = contactId;
    definition.thickness.value = plan.thicknessMm;
    definition.thickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.placement = static_cast<int>(plan.placement);
    return !AddPartFeature(kachakacha::v2::domain::FeatureType::ThickenSurface,
        std::move(definition), solid.Value().handle, solid.Value().edges, "治具")
                .IsNil();
}

void V2MainWindow::RunSurfaceJig()
{
    // 治具(V1 の body_surface_jig)。専用の立体は作らず、V2 の二手で同じものを出す:
    // 「離した面」で接触面を作り、その面に厚みを付けて当て板にする。
    // 二手をひとまとまりにするので、元に戻すのは一度で済む。
    std::vector<kachakacha::v2::base::EntityId> surfaces;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr
            && entity->kind == kachakacha::v2::domain::EntityKind::GuideSurface) {
            surfaces.push_back(id);
        }
    }
    const auto& values = parameterDock_->Values();
    const auto plan = kachakacha::v2::app::PlanSurfaceJig(
        kachakacha::v2::app::ParameterValueOf(values,
            kachakacha::v2::app::ParameterId::JigClearanceMm),
        kachakacha::v2::app::ParameterValueOf(values,
            kachakacha::v2::app::ParameterId::JigThicknessMm),
        surfaces.size());
    if (!plan.HasValue()) {
        ReportDiagnostics(plan.Diagnostics());
        return;
    }
    const auto revisionBefore = session_->GetDocument().Revision();
    session_->GetDocument().BeginCompound("治具を作る");
    const auto contact = JigContactSurface(surfaces.front(), plan.Value());
    bool ok = !contact.IsNil();
    if (ok) {
        ok = AddJigSolid(contact, plan.Value());
    }
    session_->GetDocument().EndCompound();
    if (!ok) {
        // 途中まで入ったものを戻す。半端な当たり面だけを残さない。
        if (session_->GetDocument().Revision() != revisionBefore) {
            (void)session_->Undo();
            AdoptCurrentDocument();
        }
        return;
    }
    SetStatus(QStringLiteral("治具: すき間 %1 mm、厚み %2 mm(%3)の当て板を作りました。")
            .arg(std::abs(plan.Value().offsetDistanceMm), 0, 'f', 3)
            .arg(plan.Value().thicknessMm, 0, 'f', 3)
            .arg(QString::fromUtf8(kachakacha::v2::fabrication::ThicknessPlacementNameJa(
                plan.Value().placement))));
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
