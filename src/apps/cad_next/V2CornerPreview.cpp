//! C面取り / R丸めを 道具 → 相手(A・B)→ 下見 → Enter の文法にそろえる(引継ぎ 2026-09-17 の 6)。
//!
//! これまでは「線を2本選んで作成ボタン」で、押した瞬間に文書が変わり、
//! どう落ちるのか・どう丸まるのかは作ってからしか分からなかった。
//! ここでは面取りの道具を持って線を2本拾うと、**実際に計算した結果** を下見に出し、
//! A / B の札を 3D に、一番下の一行に量と共に出す。Enter で確定(従来の wire.chamfer /
//! wire.fillet と同じ道)、Esc で選択道具へ戻る。文書へは確定まで書かない。
//!
//! 下見と確定は同じ `EvaluateWireTransform` と同じ定義を使う。画面で別に計算しない。

#include "V2MainWindow.h"

#include "V2CornerDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ToolController.h"

#include <QString>

#include <string>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::WireTransformMethod;

//! 棚と数の棚から、いまの面取り/丸めの定義を組む。**下見も確定もこれ1つ。**
kachakacha::v2::domain::TransformWireDefinition V2MainWindow::CornerDefinitionFromDock() const
{
    kachakacha::v2::domain::TransformWireDefinition definition;
    const V2CornerChoice choice = cornerDock_->Choice();
    definition.method = choice.fillet ? WireTransformMethod::Fillet : WireTransformMethod::Chamfer;
    const double size = CornerSizeMm();
    definition.scalarArgument.value = size;
    definition.scalarArgument.expression = std::to_string(size);
    definition.scalarArgument.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.secondScalarMm = choice.secondSetbackMm;
    definition.firstKeepSide = choice.firstKeepSide;
    definition.secondKeepSide = choice.secondKeepSide;
    // 押した位置(Inventor と同じ): 押した点に近い交点を角にし、押した側を残す。
    // 選んだ順の 1 本目が A、2 本目が B。命中位置は選択の正本(ordered)が持っている。
    int found = 0;
    for (const auto& ref : viewport_->Selection().ordered) {
        const auto* entity = session_->GetDocument().FindEntity(ref.entityId);
        if (entity == nullptr || entity->kind != EntityKind::Wire) {
            continue;
        }
        if (found == 0) {
            definition.firstHint = ref.hitPoint;
        } else if (found == 1) {
            definition.secondHint = ref.hitPoint;
        }
        ++found;
    }
    return definition;
}

//! 線が2本(A・B)選ばれているか。道具は問わない(選んでから押す道も残す)。
bool V2MainWindow::CornerPairSelected(std::vector<EntityId>* wires) const
{
    std::vector<EntityId> found;
    for (const EntityId& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::Wire) {
            found.push_back(id);
        }
    }
    if (wires != nullptr) {
        *wires = found;
    }
    return found.size() == 2;
}

//! 下見を出し直す。道具を持っていなければ、自分が出したものだけ片づける。
void V2MainWindow::RefreshCornerPreview()
{
    if (viewport_ == nullptr || cornerDock_ == nullptr) {
        return;
    }
    std::vector<EntityId> wires;
    const bool holdingTool = session_->CurrentTool()
        == kachakacha::v2::modeling::DrawingTool::ChamferOrFilletPair;
    if (!holdingTool || !CornerPairSelected(&wires)) {
        if (cornerPreviewShown_) {
            cornerPreviewShown_ = false;
            viewport_->HideToolPreview();
            viewport_->HideToolRoleLabels();
            ShowToolFooter(QString());
        }
        return;
    }
    const auto& selection = viewport_->Selection();
    const auto inputs = kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    const auto definition = CornerDefinitionFromDock();
    const auto computed = kachakacha::v2::document::EvaluateWireTransform(definition, inputs);
    const QString label = definition.method == WireTransformMethod::Fillet
        ? QStringLiteral("R丸め")
        : QStringLiteral("C面取り");
    cornerPreviewShown_ = true;
    ShowRoleLabels({kachakacha::v2::app::ToolRoleLabel{wires[0], "A"},
        kachakacha::v2::app::ToolRoleLabel{wires[1], "B"}});
    QString footer = QStringLiteral("%1: A=%2 / B=%3 / SIZE=%4 mm")
                         .arg(label, cornerDock_->FirstText(), cornerDock_->SecondText())
                         .arg(CornerSizeMm(), 0, 'f', 3);
    if (!computed.HasValue()) {
        viewport_->HideToolPreview();
        ShowToolFooter(footer + QStringLiteral(" / no preview"));
        SetStatus(QStringLiteral("%1(下見): %2")
                .arg(label, QString::fromStdString(computed.FirstSummaryJa())));
        return;
    }
    viewport_->ShowToolPreview(
        kachakacha::v2::app::SurfaceBoundaryLines(computed.Value(), 12));
    ShowToolFooter(footer + QStringLiteral(" / Preview only"));
    SetStatus(QStringLiteral("%1(下見): %2 本になります。Enter で確定、Esc でやめます。")
            .arg(label)
            .arg(static_cast<int>(computed.Value().size())));
}

//! Enter / Esc。下見が出ているときだけ引き受ける。
bool V2MainWindow::HandleCornerToolKey(int key)
{
    if (!cornerPreviewShown_) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        RefreshCornerPreview();
        SetStatus(QStringLiteral("面取り: やめました。何も変えていません。"));
        return true;
    }
    if (key != Qt::Key_Return && key != Qt::Key_Enter) {
        return false;
    }
    // 確定は従来の命令そのもの。下見と同じ定義(CornerDefinitionFromDock)で作る。
    const auto revision = session_->GetDocument().Revision();
    RunCommand(cornerDock_->Choice().fillet ? "wire.fillet" : "wire.chamfer");
    if (session_->GetDocument().Revision() != revision) {
        // 作れたら道具を置く。A・B は縮んだ線としてまだ選ばれているので、
        // 持ったままだと縮んだ2本を相手に下見が出直す(PC 自己試験 HP-CN-01 2026-09-18)。
        const QString done = StatusText();
        SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        SetStatus(done);   // 「作りました」の一言は残す
    }
    return true;
}
