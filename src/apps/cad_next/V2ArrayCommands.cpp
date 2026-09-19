//! 配列(並べて複製する)。棚卸し B-2、指示書 D-23。
//!
//! 車体には窓が10個並ぶ。1つずつ複製して位置を打つと、10回打ち間違える機会がある。
//! ここは **どこへ何個置くか** を core(app/ArrayPlan)に決めてもらい、
//! その数だけ「コピー」を繰り返す。新しい作り方は増やさない ── 増やすと
//! 保存・読み込み・再計算を全部二重に作ることになる。
//!
//! **全部まとめて1回の「元に戻す」で消える。** 10個並べたあとに
//! 10回押して戻すのでは、やり直すたびに手間が10倍になる。
//!
//! 聞き方は2つある。
//!   1. `arrayChooser_` が差し替えられている(自己試験) ── 今までどおり窓に聞いたことにする。
//!   2. 差し替えが無ければ(本番) ── 右の棚(Shelf::Array、V2ArrayDock)を出して欄で聞き、
//!      「確定」(ConfirmArray)で初めて並べる。Esc/「キャンセル」(EndArray)でやめる。

#include "V2MainWindow.h"

#include "kachakacha/app/ArrayPlan.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/domain/Feature.h"

#include <QString>

#include <string>
#include <vector>

bool V2MainWindow::IsArrayCommand(std::string_view id)
{
    return id == "wire.array_linear" || id == "wire.array_circular";
}

void V2MainWindow::RunArrayCommand(std::string_view id)
{
    if (id == "wire.array_linear") {
        RunLinearArray();
        return;
    }
    if (id == "wire.array_circular") {
        RunCircularArray();
    }
}

void V2MainWindow::RunLinearArray()
{
    const auto selected = viewport_->Selection().entityIds;
    if (selected.empty()) {
        SetStatus(QStringLiteral("直線に並べる: 先に並べる線を選んでください。"));
        return;
    }
    if (arrayChooser_) {
        const auto answer = arrayChooser_(arrayChoice_, false);
        if (!answer.has_value()) {
            SetStatus(QStringLiteral("直線に並べる: やめました。"));
            return;
        }
        arrayChoice_ = *answer;
        CommitLinearArray();
        return;
    }
    // 本番はここ。棚を出して、確定(ConfirmArray)を待つ。
    arrayChoice_.circular = false;
    if (arrayDock_ != nullptr) {
        arrayDock_->ShowChoice(arrayChoice_, false);
    }
    ShowShelf(kachakacha::v2::app::Shelf::Array);
    SetStatus(QStringLiteral("直線に並べる: 個数・間隔・方向を決めて「確定」を押してください。"));
}

void V2MainWindow::RunCircularArray()
{
    const auto selected = viewport_->Selection().entityIds;
    if (selected.empty()) {
        SetStatus(QStringLiteral("円に並べる: 先に並べる線を選んでください。"));
        return;
    }
    if (arrayChooser_) {
        const auto answer = arrayChooser_(arrayChoice_, true);
        if (!answer.has_value()) {
            SetStatus(QStringLiteral("円に並べる: やめました。"));
            return;
        }
        arrayChoice_ = *answer;
        CommitCircularArray();
        return;
    }
    arrayChoice_.circular = true;
    if (arrayDock_ != nullptr) {
        arrayDock_->ShowChoice(arrayChoice_, true);
    }
    ShowShelf(kachakacha::v2::app::Shelf::Array);
    SetStatus(QStringLiteral("円に並べる: 個数・中心・全体角度を決めて「確定」を押してください。"));
}

//! いまの arrayChoice_(直線)で、選んでいる線を並べる。窓の道でも棚の道でも同じ道を通る。
//! 並べられたら真(棚の道はこれで棚を片付ける。断ったときは棚を残して直させる)。
bool V2MainWindow::CommitLinearArray()
{
    using kachakacha::v2::domain::TransformWireDefinition;
    using kachakacha::v2::domain::WireTransformMethod;

    const auto selected = viewport_->Selection().entityIds;
    if (selected.empty()) {
        SetStatus(QStringLiteral("直線に並べる: 先に並べる線を選んでください。"));
        return false;
    }
    const auto plan = kachakacha::v2::app::PlanLinearArray(arrayChoice_.step,
        arrayChoice_.count, arrayChoice_.spanIsTotal);
    if (!plan.HasValue()) {
        ReportDiagnostics(plan.Diagnostics());
        return false;
    }
    // まとめて1回で戻せるようにする。10回押して戻すのでは手間が10倍になる。
    session_->GetDocument().BeginCompound("配列");
    int made = 0;
    for (const auto& offset : plan.Value()) {
        TransformWireDefinition definition;
        definition.method = WireTransformMethod::Copy;
        definition.vectorArgument = offset;
        for (const auto& entityId : selected) {
            if (TransformOneWire(definition, entityId, QStringLiteral("直線に並べる"))) {
                ++made;
            }
        }
    }
    session_->GetDocument().EndCompound();
    if (made == 0) {
        SetStatus(QStringLiteral("直線に並べる: 並べられる線がありませんでした。"));
        return false;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("直線に並べる: %1本を写しました(元を含めて %2 個)。")
            .arg(made)
            .arg(arrayChoice_.count));
    return true;
}

//! いまの arrayChoice_(円形)で、選んでいる線を並べる。
bool V2MainWindow::CommitCircularArray()
{
    using kachakacha::v2::domain::TransformWireDefinition;
    using kachakacha::v2::domain::WireTransformMethod;

    const auto selected = viewport_->Selection().entityIds;
    if (selected.empty()) {
        SetStatus(QStringLiteral("円に並べる: 先に並べる線を選んでください。"));
        return false;
    }
    // 軸は作業平面の法線。別に聞かない ── 作図面の上で並べるのが普通で、
    // 軸だけ別の向きにすると、並べたものが面から浮く。
    const auto axis = viewport_->WorkPlane().normal;
    const auto plan = kachakacha::v2::app::PlanCircularArray(axis, arrayChoice_.totalAngleDeg,
        arrayChoice_.count);
    if (!plan.HasValue()) {
        ReportDiagnostics(plan.Diagnostics());
        return false;
    }
    session_->GetDocument().BeginCompound("配列");
    int made = 0;
    for (const auto& step : plan.Value()) {
        TransformWireDefinition definition;
        definition.method = WireTransformMethod::Rotate;
        definition.pointArgument = arrayChoice_.center;
        definition.vectorArgument = axis;
        definition.scalarArgument.value = step.angleRad;
        for (const auto& entityId : selected) {
            if (TransformOneWire(definition, entityId, QStringLiteral("円に並べる"))) {
                ++made;
            }
        }
    }
    session_->GetDocument().EndCompound();
    if (made == 0) {
        SetStatus(QStringLiteral("円に並べる: 並べられる線がありませんでした。"));
        return false;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("円に並べる: %1本を写しました(元を含めて %2 個、%3 度)。")
            .arg(made)
            .arg(arrayChoice_.count)
            .arg(arrayChoice_.totalAngleDeg, 0, 'f', 1));
    return true;
}

//! 棚の「確定」。u/v(作業平面上)を世界座標へ変換してから、いつもの道を通す。
void V2MainWindow::ConfirmArray()
{
    if (arrayDock_ == nullptr) {
        return;
    }
    const auto plane = viewport_->WorkPlane();
    arrayChoice_ = arrayDock_->Choice();
    bool committed = false;
    if (arrayChoice_.circular) {
        // center は仮置きで (u, v, 0) に入っている(V2ArrayDock::Choice のコメント参照)。
        arrayChoice_.center = plane.PointAt(arrayChoice_.center.x, arrayChoice_.center.y);
        committed = CommitCircularArray();
    } else {
        // step も同じく、u/v 平面上の方向がそのまま (x, y, 0) に入っている。
        const auto direction = plane.uAxis * arrayChoice_.step.x
            + plane.vAxis * arrayChoice_.step.y;
        arrayChoice_.step = direction;
        committed = CommitLinearArray();
    }
    // 断ったときは棚を残す。まだ何も作っていないので、欄を直させる(ConfirmBoolean と同じ考え)。
    if (committed) {
        EndArray();
    }
}

//! Esc / 「キャンセル」。棚を片付けて、いつもの棚へ戻す。
void V2MainWindow::EndArray()
{
    RefreshRightShelves();
}

void V2MainWindow::SetArrayChooser(
    std::function<std::optional<V2ArrayChoice>(const V2ArrayChoice&, bool)> chooser)
{
    arrayChooser_ = std::move(chooser);
}
