//! 押し出しの下見と矢印ハンドル(オーナー指示 2026-09-14 §6・§7)。
//!
//! ここまでの押し出しは、命令を押すと窓が出て、そこで全部決めてから作る、
//! という道だった。押す前に何ができるのかが見えない。
//!
//! いまは、命令を押すと画面に矢印が出る。引くと距離が変わり、
//! 出来上がる形が破線で見える。数字は右の欄と常に同じ値になる。
//! Enter で確定、Esc でやめる。
//!
//! 出す破線は **本物の輪郭を押し出した形** である。作り物ではない。
//! 輪郭をそのまま距離ぶん動かした先と、角どうしをつないだ側面の線を出す。
//! 確定すると、この線のとおりの立体が出来る。

#include "V2MainWindow.h"

#include "V2ExtrudeDialog.h"
#include "V2ExtrudeDock.h"
#include "V2ParameterDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/ExtrudeDrag.h"
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <QDialog>
#include <QString>

#include <cmath>
#include <string>
#include <vector>

namespace {

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;

//! 曲線1本を折れ線にする。破線で出すだけなので、細かすぎなくてよい。
constexpr int kSamplesPerCurve = 16;

[[nodiscard]] std::vector<Vector3> SampleChain(const std::vector<CurveSegment>& curves)
{
    std::vector<Vector3> points;
    for (const CurveSegment& curve : curves) {
        for (int index = 0; index <= kSamplesPerCurve; ++index) {
            const double t = static_cast<double>(index) / kSamplesPerCurve;
            points.push_back(curve.Evaluate(t));
        }
    }
    return points;
}

} // namespace

void V2MainWindow::BeginExtrudePreview()
{
    using kachakacha::v2::app::ExtrudeHandle;
    const auto plan = PlanExtrudeFromSelection();
    if (!plan.readyToPreview) {
        return;
    }
    // 面の押し引きは、抱えている縁を使う。文書にはまだ入れていない(R1 B2)。
    std::vector<kachakacha::v2::geometry::CurveSegment> curves;
    if (facePushPull_ && !faceProfileLoops_.empty()) {
        curves = faceProfileLoops_.front();
    } else {
        curves = kachakacha::v2::app::SelectedCurves(viewport_->Selection(),
            session_->Scene());
    }
    if (curves.empty()) {
        return;
    }
    const std::vector<Vector3> outline = SampleChain(curves);
    if (outline.empty()) {
        return;
    }
    // 矢印の根元は輪郭の重心。端に出すと、どの輪郭のものか分からない。
    Vector3 center{};
    for (const Vector3& point : outline) {
        center = center + point;
    }
    center = center * (1.0 / static_cast<double>(outline.size()));

    // 輪郭を先に覚える。向きは輪郭の平面から決めるので、
    // 覚える前に向きを聞くと、前の輪郭の平面で答えてしまう。
    extrudeOutline_ = outline;

    ExtrudeHandle handle;
    handle.origin = center;
    // 向きは1か所(ExtrudeDirectionNow)から取る。矢印・下見・確定を必ず揃える。
    handle.direction = ExtrudeDirectionNow();
    handle.distanceMm = ExtrudeDistanceMm();
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(handle.distanceMm));
    // 右の棚に、CADが何をどう読んだかと、いま変えられるものを出す。
    ShowExtrudeShelf(plan);
    SetStatus(QStringLiteral("押し出し\n%1\n矢印を引くか、数の棚の「押し出し距離」で"
                             "決めてください。Enter で確定、Esc でやめます。")
            .arg(ExtrudePlanTextJa()));
}

//! 押し出す向き。**ここだけが決める。**
//!
//! 矢印・下見・確定形状が、みな同じ向きでなければならない。
//! 下見だけ作業平面の法線を使っていたので、傾いた面を押し引きすると
//! 矢印と下見が別の方へ進んでいた(Codex P1-EXTRUDE-R1 B1)。
Vector3 V2MainWindow::ExtrudeDirectionNow() const
{
    Vector3 direction = ExtrudeBaseDirectionNow();
    if (extrudeChoice_.reversed) {
        direction = direction * -1.0;
    }
    return direction;
}

//! 反転を掛ける前の向き。反転は棚が持っているので、二重に掛けないために分ける。
//!
//! **向きの決め方は1か所(`extrudeChoice_.direction`)が持つ。**
//! 棚も、詳細の窓も、矢印も、下見も、確定も、保存する作り方も、全部ここを通る。
//! 棚に欄があるのに読んでいなかったので、選んでも何も変わらなかった
//! (Codex P1-EXTRUDE-R4 B1)。
Vector3 V2MainWindow::ExtrudeBaseDirectionNow() const
{
    // 面の押し引きは押す面が向きを決める。棚も窓も効かない(欄も隠してある)。
    if (facePushPull_) {
        return faceNormal_;
    }
    return ExtrudeDirectionForMode(
        extrudeChoice_.direction, extrudeChoice_.customDirection);
}

//! 決め方ひとつを、向きひとつに解く。**7通りすべてここで解く。**
//!
//! ここが2通りしか解いていなかったので、詳細の窓で「X方向」や「選んだ線の向き」を
//! 選んでも、矢印と下見だけは作業平面の法線へ進んでいた。作る形はカーネルが
//! 別に解いていたので、見えているものと出来るものが違っていた
//! (Codex P1-EXTRUDE-R6 B2)。
//!
//! 解き方はカーネル(modeling/ExtrudeInput.cpp の ResolveDirection)と同じにする。
//! 違うのは「輪郭に垂直」のときだけで、こちらは下見に出している折れ線から
//! 当てる。向きの符号を作業平面に合わせるのも、矢印が今までと同じ側を向くため。
Vector3 V2MainWindow::ExtrudeDirectionForMode(
    kachakacha::v2::modeling::ExtrudeDirectionMode mode,
    const Vector3& custom) const
{
    using kachakacha::v2::modeling::ExtrudeDirectionMode;
    const Vector3 workPlaneNormal = viewport_->WorkPlane().normal;
    const auto usable = [&](const Vector3& value) {
        return value.IsFinite() && value.Length() > 1.0e-9;
    };
    switch (mode) {
    case ExtrudeDirectionMode::WorkPlaneNormal:
        return workPlaneNormal;
    case ExtrudeDirectionMode::WorldX:
        return Vector3{1.0, 0.0, 0.0};
    case ExtrudeDirectionMode::WorldY:
        return Vector3{0.0, 1.0, 0.0};
    case ExtrudeDirectionMode::WorldZ:
        return Vector3{0.0, 0.0, 1.0};
    case ExtrudeDirectionMode::SelectedVector:
    case ExtrudeDirectionMode::CustomXYZ:
        // 使えない値でも黙って別の向きへ倒さない。矢印だけは描けるように
        // 作業平面の法線を借りるが、断るのはカーネルの仕事である。
        return usable(custom) ? custom : workPlaneNormal;
    case ExtrudeDirectionMode::ProfileNormal:
        break;
    }
    // 輪郭が自分の平面を持っているなら、そちらの法線で押す。
    //
    // 作業平面の法線で押すと、別の平面に引いてある輪郭
    // (例えば前頭部の窓は x = 一定の面に載っている)を選んだときに、
    // 向きが輪郭の平面の中に寝てしまい「厚みが出ません」(EXT-007)で断られる。
    // 人は選んだ輪郭を押したいのであって、いま作図している面の向きへ
    // 押したいわけではない。
    //
    // 作業平面の上に引いた輪郭では、この法線は作業平面の法線と同じになる。
    // 向きは作業平面の法線に合わせておく。矢印の向きが今までと変わらない。
    const auto plane = kachakacha::v2::geometry::FitPlane(extrudeOutline_);
    if (plane.valid && usable(plane.normal)) {
        Vector3 fitted = plane.normal;
        if (kachakacha::v2::geometry::Dot(fitted, workPlaneNormal) < 0.0) {
            fitted = fitted * -1.0;
        }
        return fitted;
    }
    return workPlaneNormal;
}

std::vector<std::vector<Vector3>> V2MainWindow::ExtrudePreviewLoops(double distanceMm) const
{
    std::vector<std::vector<Vector3>> loops;
    if (extrudeOutline_.empty()) {
        return loops;
    }
    const Vector3 offset = ExtrudeDirectionNow() * distanceMm;
    // 押し出した先の輪郭。
    std::vector<Vector3> moved;
    moved.reserve(extrudeOutline_.size());
    for (const Vector3& point : extrudeOutline_) {
        moved.push_back(point + offset);
    }
    loops.push_back(extrudeOutline_);
    loops.push_back(moved);
    // 側面の線。全部の点に出すと真っ黒になるので、間引いて出す。
    const std::size_t step = std::max<std::size_t>(1, extrudeOutline_.size() / 12);
    for (std::size_t index = 0; index < extrudeOutline_.size(); index += step) {
        loops.push_back({extrudeOutline_[index], extrudeOutline_[index] + offset});
    }
    return loops;
}

void V2MainWindow::UpdateExtrudePreview(double distanceMm)
{
    if (!viewport_->ExtrudeHandleShown()) {
        return;
    }
    // 右の欄も同じ値にする。片方だけ動くと、どちらが本当か分からなくなる。
    parameterDock_->Apply(kachakacha::v2::app::ParameterId::ExtrudeDistance,
        QString::number(distanceMm, 'f', 2));
    kachakacha::v2::app::ExtrudeHandle handle;
    handle.origin = viewport_->ExtrudeHandleOrigin();
    handle.direction = viewport_->ExtrudeHandleDirection();
    handle.distanceMm = distanceMm;
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(distanceMm));
}

void V2MainWindow::EndExtrudePreview()
{
    extrudeOutline_.clear();
    // 面の押し引きは1回きりの状態である。残すと、次のふつうの押し出しが
    // 前の面の向きへ押される。
    facePushPull_ = false;
    // 抱えていた縁も捨てる。**文書には入れていない** ので、
    // やめれば文書は始める前とまったく同じである(R1 B2)。
    ForgetFaceProfile();
    viewport_->HideExtrudeHandle();
    // 棚も片付ける。前の操作の欄が残ると、いま何をしているのか読めなくなる。
    extrudeShelfShown_ = false;
    RefreshRightShelves();
}

//! 押し出しの棚を出して、読み取りを映す。
void V2MainWindow::ShowExtrudeShelf(const kachakacha::v2::app::ExtrudePlan& plan)
{
    const auto& document = session_->GetDocument();
    const auto nameOf = [&document](const kachakacha::v2::base::EntityId& id) {
        const auto* entity = document.FindEntity(id);
        return entity != nullptr && !entity->displayName.empty()
            ? QString::fromStdString(entity->displayName)
            : QStringLiteral("名前のないもの");
    };
    QString target;
    if (!plan.targetSolid.IsNil()) {
        target = nameOf(plan.targetSolid);
    }
    QString profiles;
    for (const auto& id : plan.profiles) {
        if (!profiles.isEmpty()) {
            profiles += QStringLiteral("、");
        }
        profiles += nameOf(id);
    }
    extrudeDock_->ShowPlan(plan, target, profiles);
    // 棚に、いま効いている向きの決め方を映す。**見えているものが本当に効く。**
    // 映さないでおくと、棚の初期表示と手に持っている値が食い違ったまま動き出す。
    // 棚のふだんの2つで表せない決め方も、棚が3つ目に名前で出す。
    extrudeDock_->ChooseDirection(extrudeChoice_.direction);
    extrudeDock_->SetDistanceMm(viewport_->ExtrudeHandleDistanceMm());
    extrudeShelfShown_ = true;
    RefreshRightShelves();
}

//! 読み取った入力の片方を外して選び直す(EX-07)。
//!
//! 選択を丸ごと捨てない。外したいほうだけ外す。
//! 「対象を選び直す」で輪郭まで消えたら、二度手間になる。
void V2MainWindow::ReselectExtrudeInput(bool target)
{
    const auto plan = PlanExtrudeFromSelection();
    std::vector<kachakacha::v2::base::EntityId> removed;
    if (target) {
        if (plan.targetSolid.IsNil()) {
            SetStatus(QStringLiteral("押し出し: 外せる対象がありません。"));
            return;
        }
        removed.push_back(plan.targetSolid);
    } else {
        if (plan.profiles.empty()) {
            SetStatus(QStringLiteral("押し出し: 外せる輪郭がありません。"));
            return;
        }
        removed = plan.profiles;
    }
    // 下見は前の入力で作ったものである。入力が変わるので必ず消す。
    if (viewport_->ExtrudeHandleShown()) {
        viewport_->HideExtrudeHandle();
        extrudeOutline_.clear();
    }
    viewport_->SetSelection(
        kachakacha::v2::app::SelectionWithout(viewport_->Selection(), removed));
    // 外したあとの読み取りをそのまま映す。棚は出したままにする。
    // 消すと、いま何を選び直しているのかが画面から消える。
    const auto after = PlanExtrudeFromSelection();
    ShowExtrudeShelf(after);
    SetStatus(QStringLiteral("押し出し: %1を外しました。%2")
            .arg(target ? QStringLiteral("対象") : QStringLiteral("輪郭"),
                QString::fromStdString(after.needsJa.empty()
                        ? std::string("選び直したら、もう一度押し出してください。")
                        : after.needsJa)));
}

//! 棚の欄が変わった。向きと距離を取り直して、下見を作り直す。
void V2MainWindow::RefreshExtrudeFromDock()
{
    if (!viewport_->ExtrudeHandleShown()) {
        return;
    }
    extrudeChoice_.reversed = extrudeDock_->Reversed();
    extrudeChoice_.extent = extrudeDock_->Symmetric()
        ? kachakacha::v2::modeling::ExtrudeExtentMode::SymmetricDistance
        : kachakacha::v2::modeling::ExtrudeExtentMode::Distance;
    extrudeChoice_.booleanMode = extrudeDock_->BooleanMode();
    // 向きの欄も読む。読まないと、選んでも何も変わらない。
    if (!facePushPull_) {
        extrudeChoice_.direction = extrudeDock_->DirectionMode();
    }
    // 向きが変わったら矢印も向き直す。数字はそのまま。
    kachakacha::v2::app::ExtrudeHandle handle;
    handle.origin = viewport_->ExtrudeHandleOrigin();
    handle.direction = ExtrudeDirectionNow();
    handle.distanceMm = extrudeDock_->DistanceMm();
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(handle.distanceMm));
}

//! 「詳細...」。細かい設定は今までの窓で決める。
//! 右へ全部並べると、どれを見ればよいのか分からなくなる。
//!
//! 窓はここで据え付けて、終わったら外す。据え付けたままにすると、
//! ふだんの確定でも窓が出て、「見ながら決める」ができなくなる。
void V2MainWindow::ConfirmExtrudeWithDialog()
{
    auto previous = extrudeChooser_;
    SetExtrudeChooser([this](const kachakacha::v2::app::ExtrudeChoice& initial,
                          const kachakacha::v2::app::ExtrudeFacts& facts)
                          -> std::optional<kachakacha::v2::app::ExtrudeChoice> {
        V2ExtrudeDialog dialog(initial, facts, ExtrudeTargets(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        return dialog.Choice();
    });
    ConfirmExtrude();
    SetExtrudeChooser(std::move(previous));
}
