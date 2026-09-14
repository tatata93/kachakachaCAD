//! 立体の面をつまんで押す・引く(EX-02)。
//!
//! 押し出しは輪郭からしか始められない。面から始めるには、押す面の縁を
//! 輪郭として取り出す道が要る。カーネル(kernel/OcctFaceQuery)がそれを返す。
//!
//! **新しい押し出しの仕掛けは作らない。** 取り出した縁を文書のワイヤーにして、
//! いままでの押し出しへそのまま渡す。こうする理由は2つある。
//!
//!   1. 突き合わせ(体積・面数の予測)が今までのものでそのまま効く。
//!   2. **開き直しても作り直せる。** 押し出しの記録(ExtrudeDefinition)は
//!      「どのワイヤーを押したか」で出来ている。面番号は作り直すたびに
//!      変わるので記録できない(architecture-and-data.md §6)。
//!      縁をワイヤーとして文書に残せば、記録の形を変えずに済む。
//!
//! CODEX_REVIEW_REQUIRED: 面の押し引きが文書にワイヤーを1本(穴があればその数だけ)
//! 増やす、という決めごとである。保存の形も Command の作られ方も変えていないが、
//! 「面を押したのに線が増える」ことの是非は見てほしい。
//! 代わりの案は ExtrudeDefinition へ面の意味的キーを足すことで、そちらは GUARDED。

#include "V2MainWindow.h"

#include "V2Viewport.h"

#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/app/FacePushPull.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/kernel/OcctFaceQuery.h"

#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace {

//! 選択の中から、押す面(番号つき)を1つ探す。
//! 番号が付いていない面は、拾い直してもらう。
[[nodiscard]] std::optional<kachakacha::v2::app::SelectionRef> PickedFaceOf(
    const kachakacha::v2::app::SelectionSet& selection)
{
    for (const auto& ref : selection.ordered) {
        if (ref.kind == kachakacha::v2::app::SelectionElementKind::Face
            && ref.pickedFaceIndex.has_value()) {
            return ref;
        }
    }
    return std::nullopt;
}

} // namespace

//! 押す面の縁を **その場限りの輪郭として** 取り出す。文書はまだ変えない。
//!
//! 下見を出しただけで文書が変わってはいけない(Codex P1-EXTRUDE-R1 B2)。
//! Esc でも棚のキャンセルでも道具替えでも、確定していない縁が残ってしまう。
//! 道具の約束は「文書が変わるのは確定のときだけ」である。
//! ここでは縁を窓が抱えるだけにして、確定のときにまとめて文書へ入れる。
bool V2MainWindow::PickFaceProfile()
{
    const auto& selection = viewport_->Selection();
    const auto face = PickedFaceOf(selection);
    if (!face.has_value()) {
        SetStatus(QStringLiteral("押し出し: 押す面をもう一度押してください。"));
        return false;
    }
    const auto found = partShapes_.find(face->entityId.ToString());
    if (found == partShapes_.end()) {
        SetStatus(QStringLiteral("押し出し: その面の元になる立体が見つかりません。"));
        return false;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto boundary = kachakacha::v2::kernel::FaceBoundaryOf(found->second,
        *face->pickedFaceIndex, tolerance);
    if (!boundary.HasValue()) {
        ReportDiagnostics(boundary.Diagnostics());
        return false;
    }
    // 外周と穴を、その場限りの輪郭として抱える。**まだ文書へは入れない。**
    faceProfileLoops_.clear();
    if (!boundary.Value().outerLoop.empty()) {
        faceProfileLoops_.push_back(boundary.Value().outerLoop);
    }
    for (const auto& hole : boundary.Value().holeLoops) {
        if (!hole.empty()) {
            faceProfileLoops_.push_back(hole);
        }
    }
    if (faceProfileLoops_.empty()) {
        SetStatus(QStringLiteral("押し出し: その面からは輪郭を取れませんでした。"));
        return false;
    }
    faceProfileSolid_ = face->entityId;
    faceNormal_ = boundary.Value().outwardNormal;
    return true;
}

//! 抱えていた縁を、いま文書へ入れる。確定のときだけ呼ぶ。
//!
//! 途中で1つでも入らなければ、**1つも入れない。** 外周だけ残ると、
//! 開いているはずの窓が塞がった形が作られる。
//! 呼ぶ側が compound の中で呼ぶので、失敗したら compound ごと捨てる。
bool V2MainWindow::CommitFaceProfileWires(
    std::vector<kachakacha::v2::base::EntityId>& made)
{
    for (std::size_t index = 0; index < faceProfileLoops_.size(); ++index) {
        const std::string label = index == 0 ? "面の縁" : "面の縁(穴)";
        const auto result = session_->AddWire(faceProfileLoops_[index], false, label);
        if (!result.committed) {
            ReportDiagnostics(result.diagnostics);
            return false;
        }
        for (const auto& id : result.createdEntityIds) {
            made.push_back(id);
        }
    }
    return !made.empty();
}

//! 抱えていた縁を捨てる。取消・道具替え・確定のあとに呼ぶ。
void V2MainWindow::ForgetFaceProfile()
{
    faceProfileLoops_.clear();
    faceProfileSolid_ = kachakacha::v2::base::EntityId{};
}

bool V2MainWindow::ApplyFacePushPull(kachakacha::v2::app::ExtrudeChoice& choice)
{
    // 作る人は「外へ引けば増える、中へ押せば減る」と思って引く。
    // 押し出しの中身は「正の距離」「足す/引くは別の欄」で出来ている。
    // その言い換えは core(app/FacePushPull)がやる。
    const double signedDistanceMm = choice.reversed ? -choice.distanceMm
                                                    : choice.distanceMm;
    const auto pushPull = kachakacha::v2::app::PlanFacePushPull(signedDistanceMm);
    if (!pushPull.ready) {
        SetStatus(QStringLiteral("押し出し: %1")
                .arg(QString::fromStdString(pushPull.messageJa)));
        return false;
    }
    choice.distanceMm = pushPull.distanceMm;
    choice.reversed = false;
    // 向きは面の外向き法線をそのまま渡す。輪郭の法線に任せると、
    // 縁の回り方しだいで裏返り、外へ引いたつもりが中へ入る。
    choice.direction = kachakacha::v2::modeling::ExtrudeDirectionMode::CustomXYZ;
    choice.customDirection = pushPull.reversed ? faceNormal_ * -1.0 : faceNormal_;
    choice.booleanMode = pushPull.booleanMode;
    choice.hasSelectedPart = true;
    choice.makePart = true;
    SetStatus(QStringLiteral("押し出し: %1")
            .arg(QString::fromStdString(pushPull.messageJa)));
    return true;
}
