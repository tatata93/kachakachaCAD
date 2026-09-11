//! 配列(並べて複製する)。棚卸し B-2。
//!
//! 車体には窓が10個並ぶ。1つずつ複製して位置を打つと、10回打ち間違える機会がある。
//! ここは **どこへ何個置くか** を core(app/ArrayPlan)に決めてもらい、
//! その数だけ「コピー」を繰り返す。新しい作り方は増やさない ── 増やすと
//! 保存・読み込み・再計算を全部二重に作ることになる。
//!
//! **全部まとめて1回の「元に戻す」で消える。** 10個並べたあとに
//! 10回押して戻すのでは、やり直すたびに手間が10倍になる。

#include "V2MainWindow.h"

#include "kachakacha/app/ArrayPlan.h"
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
    using kachakacha::v2::domain::TransformWireDefinition;
    using kachakacha::v2::domain::WireTransformMethod;

    const auto selected = viewport_->Selection().entityIds;
    if (selected.empty()) {
        SetStatus(QStringLiteral("直線に並べる: 先に並べる線を選んでください。"));
        return;
    }
    if (!arrayChooser_) {
        SetStatus(QStringLiteral("直線に並べる: 並べ方を聞く窓がありません。"));
        return;
    }
    const auto answer = arrayChooser_(arrayChoice_, false);
    if (!answer.has_value()) {
        SetStatus(QStringLiteral("直線に並べる: やめました。"));
        return;
    }
    arrayChoice_ = *answer;
    const auto plan = kachakacha::v2::app::PlanLinearArray(arrayChoice_.step,
        arrayChoice_.count, arrayChoice_.spanIsTotal);
    if (!plan.HasValue()) {
        ReportDiagnostics(plan.Diagnostics());
        return;
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
        return;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("直線に並べる: %1本を写しました(元を含めて %2 個)。")
            .arg(made)
            .arg(arrayChoice_.count));
}

void V2MainWindow::RunCircularArray()
{
    using kachakacha::v2::domain::TransformWireDefinition;
    using kachakacha::v2::domain::WireTransformMethod;

    const auto selected = viewport_->Selection().entityIds;
    if (selected.empty()) {
        SetStatus(QStringLiteral("円に並べる: 先に並べる線を選んでください。"));
        return;
    }
    if (!arrayChooser_) {
        SetStatus(QStringLiteral("円に並べる: 並べ方を聞く窓がありません。"));
        return;
    }
    const auto answer = arrayChooser_(arrayChoice_, true);
    if (!answer.has_value()) {
        SetStatus(QStringLiteral("円に並べる: やめました。"));
        return;
    }
    arrayChoice_ = *answer;
    // 軸は作業平面の法線。別に聞かない ── 作図面の上で並べるのが普通で、
    // 軸だけ別の向きにすると、並べたものが面から浮く。
    const auto axis = viewport_->WorkPlane().normal;
    const auto plan = kachakacha::v2::app::PlanCircularArray(axis, arrayChoice_.totalAngleDeg,
        arrayChoice_.count);
    if (!plan.HasValue()) {
        ReportDiagnostics(plan.Diagnostics());
        return;
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
        return;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("円に並べる: %1本を写しました(元を含めて %2 個、%3 度)。")
            .arg(made)
            .arg(arrayChoice_.count)
            .arg(arrayChoice_.totalAngleDeg, 0, 'f', 1));
}

void V2MainWindow::SetArrayChooser(
    std::function<std::optional<V2ArrayChoice>(const V2ArrayChoice&, bool)> chooser)
{
    arrayChooser_ = std::move(chooser);
}
