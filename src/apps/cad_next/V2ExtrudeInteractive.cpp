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
#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/app/ExtrudePreview.h"
#include "kachakacha/app/ToolFooter.h"
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <QDialog>
#include <QString>

#include <algorithm>
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
    // **下見を出すときに、入力をそのまま留め置く**(オーナー指示 §9)。
    // 確定はこの写しから作る。選択を読み直さない。
    ExtrudeSnapshot snapshot;
    snapshot.plan = plan;
    snapshot.facePushPull = facePushPull_;
    snapshot.profiles = facePushPull_ ? FaceProfilesNow() : ExtrudeProfilesFor(plan.profiles);
    if (snapshot.profiles.empty()) {
        SetStatus(QStringLiteral("押し出し: 押す輪郭が取れませんでした。"
                                 "閉じた輪郭か、立体の平らな面を選んでください。"));
        return;
    }
    extrudeSnapshot_ = std::move(snapshot);
    // 面の押し引きは、抱えている縁を使う。文書にはまだ入れていない(R1 B2)。
    std::vector<kachakacha::v2::geometry::CurveSegment> curves;
    if (facePushPull_ && !faceProfileLoops_.empty()) {
        curves = faceProfileLoops_.front();
    } else if (!extrudeSnapshot_->profiles.empty()) {
        // Snapshot は選んだ複数線を1つの論理輪郭へ並べ直している。
        // 生の選択順を読み直すと、下見だけが線を飛び回り確定形状と食い違う。
        curves = extrudeSnapshot_->profiles.front().segments;
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

    // 輪郭の差し渡し。押し始めの距離が見える大きさかを決めるのに使う。
    Vector3 lowest = outline.front();
    Vector3 highest = outline.front();
    for (const Vector3& point : outline) {
        lowest = Vector3{std::min(lowest.x, point.x), std::min(lowest.y, point.y),
            std::min(lowest.z, point.z)};
        highest = Vector3{std::max(highest.x, point.x), std::max(highest.y, point.y),
            std::max(highest.z, point.z)};
    }
    const double spanMm = (highest - lowest).Length();

    ExtrudeHandle handle;
    handle.origin = center;
    // 向きは1か所(ExtrudeDirectionNow)から取る。矢印・下見・確定を必ず揃える。
    handle.direction = ExtrudeDirectionNow();
    // 距離は覚えている値。ただし輪郭に対して細すぎると下見が見えないので、
    // そのときだけ見える値へ寄せる(core が決める)。
    const double suggested = kachakacha::v2::app::SuggestExtrudeDistanceMm(
        ExtrudeDistanceMm(), spanMm);
    if (suggested != ExtrudeDistanceMm()) {
        parameterDock_->Apply(kachakacha::v2::app::ParameterId::ExtrudeLengthMm,
            QString::number(suggested, 'f', 2));
    }
    handle.distanceMm = suggested;
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(handle.distanceMm));
    viewport_->SetExtrudePreviewFaces(ExtrudePreviewFaces(handle.distanceMm));
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
    // **長さ1で返す。** 距離は別に持っているので、向きが長さを持っていると
    // 二重に掛かる。`{0,0,7}` を選んで距離 4mm と打つと、矢印と下見だけが
    // 28mm 進み、出来る形は 4mm だった(Codex P1-EXTRUDE-R6 B2 の続き、R7 B2)。
    // カーネル(ResolveDirection)も同じところで単位にしている。
    const auto unit = [&](const Vector3& value) {
        return usable(value)
            ? kachakacha::v2::geometry::Normalized(value, 1.0e-12)
            : workPlaneNormal;
    };
    switch (mode) {
    case ExtrudeDirectionMode::WorkPlaneNormal:
        return unit(workPlaneNormal);
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
        return unit(custom);
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
        return unit(fitted);
    }
    return unit(workPlaneNormal);
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
    // **下見は「選んだ出力」を映す**(オーナー指示)。
    // 「押し出し先ワイヤーのみ」で側面まで描くと、作られないものが見える。
    //
    // ただし元の輪郭と押し出し先の輪郭は、何を選んでいても必ず出す。
    // 出さないと、どこからどこまで押しているのかが読めない
    // (オーナー指示 §8「元輪郭 / 終端輪郭 / 方向 / 距離 が一目で区別できること」)。
    loops.push_back(extrudeOutline_);
    loops.push_back(moved);
    // 側面の線。ソリッドを作るか、側面ワイヤーを頼まれたときだけ出す。
    // 全部の点に出すと真っ黒になるので、間引いて出す。
    const bool showSides = extrudeChoice_.makePart || extrudeChoice_.makeSideBoundaryWires;
    if (!showSides) {
        return loops;
    }
    const std::size_t step = std::max<std::size_t>(1, extrudeOutline_.size() / 12);
    for (std::size_t index = 0; index < extrudeOutline_.size(); index += step) {
        loops.push_back({extrudeOutline_[index], extrudeOutline_[index] + offset});
    }
    return loops;
}

//! 下見に敷くうすい面(§8)。**ソリッドを作るときだけ。**
//! ワイヤーしか作らないのに面が見えると、作られないものが見えることになる。
std::vector<std::vector<Vector3>> V2MainWindow::ExtrudePreviewFaces(double distanceMm) const
{
    if (extrudeOutline_.empty() || !extrudeChoice_.makePart) {
        return {};
    }
    return kachakacha::v2::app::ExtrudeSweptFaces(extrudeOutline_,
        ExtrudeDirectionNow() * distanceMm);
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
    viewport_->SetExtrudePreviewFaces(ExtrudePreviewFaces(distanceMm));
}

void V2MainWindow::EndExtrudePreview()
{
    extrudeOutline_.clear();
    // 留め置いた写しも捨てる。次の押し出しが前の入力で作られないように。
    extrudeSnapshot_.reset();
    // 拾い方もふだんへ戻す。道具が終われば、特別な並べ替えはしない。
    viewport_->SetPickSlot(kachakacha::v2::app::ExtrudeSlot::None);
    viewport_->SetToolPickActive(false);
    // 面の押し引きは1回きりの状態である。残すと、次のふつうの押し出しが
    // 前の面の向きへ押される。
    facePushPull_ = false;
    // 抱えていた縁も捨てる。**文書には入れていない** ので、
    // やめれば文書は始める前とまったく同じである(R1 B2)。
    ForgetFaceProfile();
    viewport_->HideExtrudeHandle();
    viewport_->HideToolRoleLabels();
    viewport_->SetExtrudePreviewFaces({});
    ShowToolFooter(QString());
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
    if (!plan.profileIsFace && plan.profiles.size() > 1) {
        profiles = QStringLiteral("%1本の線（1つの閉じた輪郭）")
                       .arg(static_cast<int>(plan.profiles.size()));
    }
    extrudeDock_->ShowPlan(plan, target, profiles);
    // 棚に、いま効いている向きの決め方を映す。**見えているものが本当に効く。**
    // 映さないでおくと、棚の初期表示と手に持っている値が食い違ったまま動き出す。
    // 棚のふだんの2つで表せない決め方も、棚が3つ目に名前で出す。
    extrudeDock_->ChooseDirection(extrudeChoice_.direction);
    extrudeDock_->SetDistanceMm(viewport_->ExtrudeHandleDistanceMm());
    // 出力の欄に、いま作ろうとしているものを映す。
    kachakacha::v2::app::ExtrudeOutputs outputs;
    outputs.body = extrudeChoice_.makePart;
    outputs.startWire = extrudeChoice_.makeStartProfileWire;
    outputs.endWire = extrudeChoice_.makeEndProfileWire;
    outputs.sideWires = extrudeChoice_.makeSideBoundaryWires;
    extrudeDock_->ShowOutputs(outputs);
    extrudeShelfShown_ = true;
    RefreshRightShelves();
    RefreshExtrudeStatus(plan);
}

//! 拾う候補の並べ替えを、**いま足りないスロット**に合わせる(§6)。
//!
//! 構えて待っている間にも要る。輪郭を拾ったあと「輪郭」に留めておくと、
//! 次に相手の立体を押したときにその面が輪郭として拾われ、
//! 「面と輪郭の両方が選ばれています」で止まってしまう。
void V2MainWindow::RefreshExtrudePickSlot()
{
    if (viewport_ == nullptr) {
        return;
    }
    const auto plan = PlanExtrudeFromSelection();
    kachakacha::v2::app::ExtrudeInputState state;
    if (!plan.targetSolid.IsNil()) {
        state.target = plan.targetSolid;
    }
    state.profiles = plan.profiles;
    state.profileIsFace = plan.profileIsFace;
    if (extrudeDock_ != nullptr) {
        state.operation = extrudeDock_->BooleanMode();
    }
    // **並べ替えだけを動かす。**「道具が動いているか」はここでは触らない。
    // 触ると、構えて待っている間に足せなくなる。
    viewport_->SetPickSlot(kachakacha::v2::app::NextNeededSlot(state));
}

//! 下見を出している間に選択が変わった。**写しを作り直して、下見も出し直す。**
//!
//! 黙って読み直す道(確定のときに選択を読む)は塞いだ(オーナー指示 §9)。
//! 代わりに、選択が変わったことを入力へ明示に映して、見えているものを合わせる。
//! こうすれば「画面に出ていないもので作る」が起きない。
void V2MainWindow::RefreshExtrudeForSelectionChange()
{
    if (!viewport_->ExtrudeHandleShown() || facePushPull_) {
        return;   // 下見が無い / 面の押し引きは選択で変わらない
    }
    const auto plan = PlanExtrudeFromSelection();
    if (!plan.readyToPreview) {
        // 押せない選択になった。下見は出したままにせず、片付けて理由を言う。
        EndExtrudePreview();
        SetStatus(QStringLiteral("押し出し\n%1").arg(ExtrudePlanTextJa()));
        return;
    }
    if (extrudeSnapshot_.has_value() && extrudeSnapshot_->plan.profiles == plan.profiles
        && extrudeSnapshot_->plan.targetSolid == plan.targetSolid) {
        RefreshExtrudeStatus(plan);
        return;   // 入力は変わっていない。作り直さない。
    }
    // 入力が変わった。始めからやり直す(写し・輪郭・矢印・下見・棚)。
    const double keepDistance = viewport_->ExtrudeHandleDistanceMm();
    BeginExtrudePreview();
    if (viewport_->ExtrudeHandleShown()) {
        UpdateExtrudePreview(keepDistance);
    }
}

//! 「状態」欄を書き直す(UI の正本「3. 状態」、オーナー指示 §15)。
//!
//! 診断コードだけに頼らない。**通った道も言う。**
//! 「✓ 対象: 部品1」「✓ 入力は有効です」「確定すると 部品1 に「引く」で適用します」。
void V2MainWindow::RefreshExtrudeStatus(const kachakacha::v2::app::ExtrudePlan& plan)
{
    if (extrudeDock_ == nullptr) {
        return;
    }
    const auto& document = session_->GetDocument();
    const auto nameOf = [&document](const kachakacha::v2::base::EntityId& id) {
        const auto* entity = document.FindEntity(id);
        return entity != nullptr && !entity->displayName.empty()
            ? entity->displayName
            : std::string("名前のないもの");
    };
    // 画面の欄をそのまま、core の入力スロットへ写して聞く。
    kachakacha::v2::app::ExtrudeInputState state;
    if (!plan.targetSolid.IsNil()) {
        state.target = plan.targetSolid;
    }
    state.profiles = plan.profiles;
    state.profileIsFace = plan.profileIsFace;
    state.operation = extrudeDock_->BooleanMode();
    state.extent = extrudeDock_->ExtentMode();
    state.direction = extrudeDock_->DirectionMode();
    state.distanceMm = extrudeDock_->DistanceMm();
    state.outputs = extrudeDock_->Outputs();

    std::string targetName;
    if (!plan.targetSolid.IsNil()) {
        targetName = nameOf(plan.targetSolid);
    }
    std::vector<std::string> profileNames;
    for (const auto& id : plan.profiles) {
        profileNames.push_back(nameOf(id));
    }
    std::vector<QString> lines;
    for (const std::string& line : kachakacha::v2::app::ExtrudeStatusLinesJa(state,
             targetName, profileNames, viewport_->ExtrudeHandleShown())) {
        lines.push_back(QString::fromStdString(line));
    }
    // 作るものが1つも無いなら確定させない。理由は上の行に出ている。
    extrudeDock_->ShowStatusLines(lines, plan.readyToPreview && state.outputs.Any());
    // 3D の中にも、いまの役割を出す(§7)。棚の名前だけでは、
    // **画面のどの線がその役割なのかが分からない。**
    RefreshExtrudeRoleLabels(state);
    // 一番下の一行(正本の footer)。棚を閉じていても、いまの入力が読める。
    ShowToolFooter(QString::fromUtf8(kachakacha::v2::app::ExtrudeFooterLine(state,
        targetName, profileNames).c_str()));
    // 拾う候補の並べ替えは、**いま足りないスロット**に合わせる(§6)。
    // ずっと「輪郭」に留めておくと、輪郭が入ったあとに相手の立体を押しても
    // その面が輪郭として拾われ、「面と輪郭の両方が選ばれています」で止まる。
    // 足りているなら None。ふだんの拾い方へ戻す。**候補は捨てない。**
    viewport_->SetPickSlot(kachakacha::v2::app::NextNeededSlot(state));
    // 下見が出ている間は、まだ入力を集めている(§5)。
    // 素のクリックで、役割の違うものを足せるままにする。
    viewport_->SetToolPickActive(true);
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
    // 出力の欄も読む。**選んだとおりの物を作る。**
    const auto outputs = extrudeDock_->Outputs();
    extrudeChoice_.makePart = outputs.body;
    extrudeChoice_.makeStartProfileWire = outputs.startWire;
    extrudeChoice_.makeEndProfileWire = outputs.endWire;
    extrudeChoice_.makeSideBoundaryWires = outputs.sideWires;
    RefreshExtrudeStatus(PlanExtrudeFromSelection());
    // 向きが変わったら矢印も向き直す。数字はそのまま。
    kachakacha::v2::app::ExtrudeHandle handle;
    handle.origin = viewport_->ExtrudeHandleOrigin();
    handle.direction = ExtrudeDirectionNow();
    handle.distanceMm = extrudeDock_->DistanceMm();
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(handle.distanceMm));
    viewport_->SetExtrudePreviewFaces(ExtrudePreviewFaces(handle.distanceMm));
}

//! 「詳細...」。細かい設定は今までの窓で決める。
//!
//! 窓で決めても、**そこでは作らない。**決めたことを棚と矢印と下見へ映して戻す。
//! 作るのは「確定」である。棚のほかの欄と同じ扱いにする。
//!
//! 窓が答えたその足で作っていたので、画面に出ている矢印と下見は窓を開く前の
//! ままだった。見ながら決めることができず、「見えているものが本当に効く」も
//! 守れていなかった(Codex P1-EXTRUDE-R7 B1)。
//!
//! 窓はここで据え付けて、終わったら外す。据え付けたままにすると、
//! ふだんの確定でも窓が出て、「見ながら決める」ができなくなる。
void V2MainWindow::EditExtrudeWithDialog()
{
    if (!viewport_->ExtrudeHandleShown()) {
        SetStatus(QStringLiteral("押し出し: 先に押し出しを始めてください。"));
        return;
    }
    const auto plan = PlanExtrudeFromSelection();
    auto profiles = facePushPull_ ? FaceProfilesNow() : ExtrudeProfilesFor(plan.profiles);
    const auto facts = BuildExtrudeFacts(profiles);
    V2ExtrudeDialog dialog(extrudeChoice_, facts, ExtrudeTargets(), this);
    if (dialog.exec() != QDialog::Accepted) {
        SetStatus(QStringLiteral("押し出し: 詳細をやめました。"));
        return;
    }
    ApplyExtrudeChoice(dialog.Choice());
}

//! 窓が答えたひと組を、棚と矢印と下見へ映す。**作らない。**
void V2MainWindow::ApplyExtrudeChoice(const kachakacha::v2::app::ExtrudeChoice& choice)
{
    extrudeChoice_ = choice;
    // 棚は7通りの向きと5通りの終端をすべて名前で出せる。決めたとおりを映す。
    extrudeDock_->ChooseDirection(extrudeChoice_.direction);
    extrudeDock_->ChooseExtent(extrudeChoice_.extent);
    extrudeDock_->ChooseBoolean(extrudeChoice_.booleanMode);
    extrudeDock_->SetDistanceMm(extrudeChoice_.distanceMm);
    // 矢印と下見を作り直す。ここで初めて、画面が決めたとおりになる。
    kachakacha::v2::app::ExtrudeHandle handle;
    handle.origin = viewport_->ExtrudeHandleOrigin();
    handle.direction = ExtrudeDirectionNow();
    handle.distanceMm = extrudeChoice_.distanceMm;
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(handle.distanceMm));
    viewport_->SetExtrudePreviewFaces(ExtrudePreviewFaces(handle.distanceMm));
    SetStatus(QStringLiteral("押し出し\n%1\nこのとおりでよければ Enter で確定します。")
            .arg(QString::fromStdString(
                kachakacha::v2::app::ExtrudeSummaryJa(extrudeChoice_))));
}
