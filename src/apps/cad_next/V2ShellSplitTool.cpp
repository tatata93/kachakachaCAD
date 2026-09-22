//! 「シェル・分割」の道具。V2ShellSplitTool.h の頭の注記を見よ。

#include "V2ShellSplitTool.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctShellSplit.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/ToolController.h"

#include <QKeyEvent>
#include <QString>

#include <cstdio>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::ShellSplitDefinition;
using kachakacha::v2::geometry::Vector3;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

[[nodiscard]] std::string LabelOf(int method)
{
    return std::string(kachakacha::v2::app::ShellSplitMethodNameJa(method));
}

[[nodiscard]] std::string Triple(const Vector3& value, const char* format)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), format, value.x, value.y, value.z);
    return buffer;
}

//! 作れなかった理由(一文と、あれば詳しい話)。
template <class R>
[[nodiscard]] std::string RefusalOf(const R& made)
{
    std::string text = made.FirstSummaryJa();
    const auto first = made.FirstDiagnostic();
    if (!first.detailsJa.empty()) {
        text += "(" + first.detailsJa + ")";
    }
    return text;
}

} // namespace

V2ShellSplitTool::V2ShellSplitTool(V2MainWindow& window)
    : window_(window)
{
    dock_ = new V2ShellSplitDock(&window);
    dock_->SetMethodHandler([this](int method) {
        input_ = kachakacha::v2::app::WithShellSplitMethod(input_, method);
        Refresh();
    });
    dock_->SetValueHandlers(
        [this](double value) {
            input_.thicknessMm = value;
            Refresh();
        },
        [this](double value) {
            input_.splitOffsetMm = value;
            Refresh();
        });
    dock_->SetClearHandlers(
        [this] {
            input_ = kachakacha::v2::app::WithoutShellSplitPart(input_);
            MirrorToSelection();
            Refresh();
        },
        [this] {
            input_ = kachakacha::v2::app::WithoutShellSplitFaces(input_);
            Refresh();
        });
    dock_->SetActionHandlers([this] { Confirm(); },
        [this] {
            const std::string label = LabelOf(input_.method);
            End();
            window_.SetStatus(Text(label + ": やめました。何も作っていません。"));
        });
}

bool V2ShellSplitTool::Handles(std::string_view commandId)
{
    int method = 0;
    return kachakacha::v2::app::ShellSplitMethodForCommand(commandId, method);
}

void V2ShellSplitTool::Begin(std::string_view commandId)
{
    int method = 0;
    if (!kachakacha::v2::app::ShellSplitMethodForCommand(commandId, method)) {
        return;
    }
    if (active_) {
        // 構えている間の2度目は確定。違う作り方なら作り方だけ替える(部品はそのまま)。
        if (method == input_.method) {
            Confirm();
            return;
        }
        input_ = kachakacha::v2::app::WithShellSplitMethod(input_, method);
        Refresh();
        return;
    }
    if (window_.session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select) {
        window_.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    }
    const double previousThickness = input_.thicknessMm;
    input_ = kachakacha::v2::app::ShellSplitInputState{};
    input_.method = method;
    input_.thicknessMm = previousThickness > 0.0 ? previousThickness : 2.0;
    // 選んであった部品は相手に入れる(シェルの面は 3D で押して選ぶ)。
    for (const EntityId& id : window_.viewport_->Selection().entityIds) {
        const auto* entity = window_.session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::Part) {
            input_ = kachakacha::v2::app::WithShellSplitPick(input_, id, std::nullopt, std::nullopt);
            break;
        }
    }
    active_ = true;
    window_.RefreshRightShelves();
    window_.viewport_->SetToolPickActive(true);
    window_.viewport_->SetToolPickToggle(true);
    MirrorToSelection();
    Refresh();
    window_.SetStatus(Text(method == 1
            ? std::string("分割: 分けたい部品を 3D で押してください。いまの作業平面で 2 つに分けます"
                          "(位置は右の棚の「ずらす」で動かせます)。Enter で確定、Esc でやめます。")
            : std::string("シェル: 部品の開けたい面を 3D で押してください(何枚でも。入っている面を押すと"
                          "外れます)。肉厚は右の棚で決めます。Enter で確定、Esc でやめます。")));
}

void V2ShellSplitTool::End()
{
    active_ = false;
    built_.reset();
    builtOther_.reset();
    outcome_ = kachakacha::v2::app::ShellSplitOutcome{};
    if (window_.viewport_ != nullptr) {
        window_.viewport_->HideToolPreview();
        window_.viewport_->HideToolRoleLabels();
        window_.viewport_->SetToolPickActive(false);
        window_.viewport_->SetToolPickToggle(false);
        mirroring_ = true;
        window_.viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
        mirroring_ = false;
    }
    window_.ShowToolFooter(QString());
    window_.RefreshRightShelves();
}

bool V2ShellSplitTool::HandleKey(int key)
{
    if (!active_) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        const std::string label = LabelOf(input_.method);
        End();
        window_.SetStatus(Text(label + ": やめました。何も作っていません。"));
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        Confirm();
        return true;
    }
    return false;
}

//! 3D の選択の印を、相手の部品に合わせる(面は下見の線で見える)。
void V2ShellSplitTool::MirrorToSelection()
{
    kachakacha::v2::app::SelectionSet mirrored;
    if (!input_.part.IsNil()) {
        mirrored.entityIds.push_back(input_.part);
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = input_.part;
        mirrored.ordered.push_back(ref);
    }
    mirroring_ = true;
    window_.viewport_->SetSelection(std::move(mirrored));
    mirroring_ = false;
}

void V2ShellSplitTool::HandleSelectionChanged()
{
    if (!active_ || mirroring_ || window_.viewport_ == nullptr) {
        return;
    }
    if (const auto pick = window_.viewport_->TakeLastToolPick(); pick.has_value()) {
        ApplyPick(*pick);
    }
    MirrorToSelection();   // 受けなかったもの(線など)を選択に残さない。
    Refresh();
}

//! 部品を押した。シェルなら押した点に一番近い面を核に尋ね、入っている面と同じなら外す。
void V2ShellSplitTool::ApplyPick(const EntityId& id)
{
    const auto* entity = window_.session_->GetDocument().FindEntity(id);
    if (entity == nullptr || entity->kind != EntityKind::Part) {
        window_.SetStatus(Text(LabelOf(input_.method) + ": 部品を押してください。"));
        return;
    }
    if (input_.method == 1) {
        input_ = kachakacha::v2::app::WithShellSplitPick(input_, id, std::nullopt, std::nullopt);
        return;
    }
    const auto shape = window_.partShapes_.find(id.ToString());
    const auto hit = window_.viewport_->LastToolPickPoint();
    std::optional<Vector3> facePoint;
    std::optional<std::size_t> sameFaceAs;
    if (shape != window_.partShapes_.end() && hit.has_value()) {
        const auto nearest = kachakacha::v2::kernel::NearestSolidFace(shape->second, *hit);
        if (nearest.HasValue()) {
            facePoint = nearest.Value().point;
            const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
            for (std::size_t index = 0; input_.part == id && index < input_.faces.size(); ++index) {
                const auto existing = kachakacha::v2::kernel::SolidFaceAt(shape->second,
                    input_.faces[index], tolerance);
                if (existing.HasValue() && existing.Value().faceIndex == nearest.Value().faceIndex) {
                    sameFaceAs = index;
                    break;
                }
            }
        } else {
            window_.ReportDiagnostics(nearest.Diagnostics());
        }
    }
    input_ = kachakacha::v2::app::WithShellSplitPick(input_, id, facePoint, sameFaceAs);
}

std::string V2ShellSplitTool::NameOf(const EntityId& id) const
{
    const auto* entity = id.IsNil() ? nullptr : window_.session_->GetDocument().FindEntity(id);
    return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                              : std::string("名前のないもの");
}

//! 分ける平面: いまの作業平面を、その法線の向きへ「ずらす」だけ動かしたもの。
V2ShellSplitTool::SplitPlane V2ShellSplitTool::CurrentSplitPlane() const
{
    const auto& frame = window_.viewport_->WorkPlane();
    SplitPlane plane;
    plane.normal = kachakacha::v2::geometry::Normalized(frame.normal);
    plane.origin = frame.origin + plane.normal * input_.splitOffsetMm;
    return plane;
}

std::string V2ShellSplitTool::PlaneTextJa() const
{
    const SplitPlane plane = CurrentSplitPlane();
    return "いまの作業平面をずらした平面(点 " + Triple(plane.origin, "(%.3f, %.3f, %.3f)")
        + "、向き " + Triple(plane.normal, "(%.3f, %.3f, %.3f)") + ")";
}

void V2ShellSplitTool::Refresh()
{
    RefreshPreview();
    RefreshDock();
}

void V2ShellSplitTool::RefreshPreview()
{
    outcome_ = kachakacha::v2::app::ShellSplitOutcome{};
    built_.reset();
    builtOther_.reset();
    window_.viewport_->HideToolPreview();
    const auto shape = input_.part.IsNil() ? window_.partShapes_.end()
                                           : window_.partShapes_.find(input_.part.ToString());
    if (shape == window_.partShapes_.end()) {
        return;
    }
    if (input_.method == 1) {
        RefreshSplitPreview(shape->second);
    } else {
        RefreshShellPreview(shape->second);
    }
}

//! そろっていれば **実際に核でシェルにして** 稜線を下見に出す。作れなければ選んだ面の縁だけ。
void V2ShellSplitTool::RefreshShellPreview(const kachakacha::v2::modeling::KernelShapeHandle& shape)
{
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    std::vector<std::vector<Vector3>> chosen;
    for (const auto& point : input_.faces) {
        const auto face = kachakacha::v2::kernel::SolidFaceAt(shape, point, tolerance);
        if (face.HasValue()) {
            for (const auto& edge : face.Value().outline) {
                chosen.push_back(edge);
            }
        }
    }
    if (!kachakacha::v2::app::ShellSplitReady(input_)) {
        if (!chosen.empty()) {
            window_.viewport_->ShowToolPreview(std::move(chosen));
        }
        return;
    }
    outcome_.evaluated = true;
    const auto made = kachakacha::v2::kernel::ShellSolid(shape, input_.faces, input_.thicknessMm,
        tolerance);
    if (!made.HasValue()) {
        outcome_.refusalJa = RefusalOf(made);
        window_.viewport_->ShowToolPreview(std::move(chosen));   // どの面かは見せる
        return;
    }
    outcome_.available = true;
    outcome_.volumeMm3 = made.Value().volumeMm3;
    outcome_.previousVolumeMm3 = made.Value().previousVolumeMm3;
    built_ = made.Value().handle;
    const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(made.Value().handle);
    if (mesh.HasValue()) {
        window_.viewport_->ShowToolPreview(mesh.Value().edges);
    }
}

//! **実際に核で分けて** 両側の稜線を下見に出す。
void V2ShellSplitTool::RefreshSplitPreview(const kachakacha::v2::modeling::KernelShapeHandle& shape)
{
    outcome_.evaluated = true;
    const SplitPlane plane = CurrentSplitPlane();
    const auto made = kachakacha::v2::kernel::SplitSolidByPlane(shape, plane.origin, plane.normal,
        window_.session_->GetDocument().Snapshot().settings.tolerance);
    if (!made.HasValue()) {
        outcome_.refusalJa = RefusalOf(made);
        return;
    }
    outcome_.available = true;
    outcome_.volumeMm3 = made.Value().positiveVolumeMm3;
    outcome_.otherVolumeMm3 = made.Value().negativeVolumeMm3;
    outcome_.previousVolumeMm3 = made.Value().positiveVolumeMm3 + made.Value().negativeVolumeMm3;
    outcome_.positivePieces = made.Value().positiveSolidCount;
    outcome_.negativePieces = made.Value().negativeSolidCount;
    built_ = made.Value().positive;
    builtOther_ = made.Value().negative;
    builtPlane_ = plane;
    std::vector<std::vector<Vector3>> edges;
    for (const auto& handle : {made.Value().positive, made.Value().negative}) {
        const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(handle);
        if (mesh.HasValue()) {
            edges.insert(edges.end(), mesh.Value().edges.begin(), mesh.Value().edges.end());
        }
    }
    if (!edges.empty()) {
        window_.viewport_->ShowToolPreview(std::move(edges));
    }
}

void V2ShellSplitTool::RefreshDock()
{
    const bool previewShown = !window_.viewport_->ToolPreview().empty() && outcome_.available;
    std::vector<QString> lines;
    for (const std::string& line :
        kachakacha::v2::app::ShellSplitStatusLinesJa(input_, outcome_, previewShown)) {
        lines.push_back(Text(line));
    }
    const std::string part = NameOf(input_.part);
    dock_->ShowInput(input_, Text(part), Text(PlaneTextJa()), lines, built_.has_value());
    window_.ShowToolFooter(active_
            ? Text(kachakacha::v2::app::ShellSplitFooterLine(input_, part, outcome_, previewShown))
            : QString());
    if (!active_) {
        return;
    }
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    if (!input_.part.IsNil()) {
        labels.push_back({input_.part, "PART"});
    }
    window_.ShowRoleLabels(labels);
}

void V2ShellSplitTool::Confirm()
{
    if (!built_.has_value()) {
        window_.SetStatus(Text(LabelOf(input_.method) + ": まだ作れません。"
            + (outcome_.refusalJa.empty() ? kachakacha::v2::app::ShellSplitHintJa(input_)
                                          : outcome_.refusalJa)));
        RefreshDock();
        return;
    }
    if (input_.method == 1) {
        ConfirmSplit();
    } else {
        ConfirmShell();
    }
}

//! 確定(シェル)。**下見に使った形をそのまま**文書へ入れる。元の部品を隠すのも含めて 1 回で戻せる。
void V2ShellSplitTool::ConfirmShell()
{
    const std::string label = LabelOf(0);
    ShellSplitDefinition definition;
    definition.method = 0;
    definition.source = input_.part;
    definition.thickness.value = input_.thicknessMm;
    definition.thickness.expression = std::to_string(input_.thicknessMm);
    definition.thickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.facePoints = input_.faces;
    const auto handle = *built_;
    const auto outcome = outcome_;
    const EntityId source = input_.part;
    auto& document = window_.session_->GetDocument();
    kachakacha::v2::document::Document::Transaction transaction(document, label.c_str());
    const auto madeId = window_.AddPartFeature(kachakacha::v2::domain::FeatureType::ShellSplit,
        definition, handle, {}, label.c_str(), {source});
    if (madeId.IsNil()) {
        return;
    }
    // 元の部品は隠す(消すと作り方をたどれない)。
    const auto hidden = document.Run(kachakacha::v2::document::SetVisibilityCommand(
        {source}, kachakacha::v2::domain::Visibility::Hidden));
    if (!hidden.committed) {
        window_.ReportDiagnostics(hidden.diagnostics);
        return;
    }
    if (!transaction.Commit()) {
        return;
    }
    End();
    window_.AdoptCurrentDocument();
    window_.SetStatus(QStringLiteral("シェル: 面を %1 枚抜いて肉厚 %2 mm を残し、体積が %3 mm3 から %4 mm3 に"
                                     "なりました。元の部品は隠しました。")
            .arg(static_cast<int>(definition.facePoints.size()))
            .arg(definition.thickness.value, 0, 'f', 3)
            .arg(outcome.previousVolumeMm3, 0, 'f', 4)
            .arg(outcome.volumeMm3, 0, 'f', 4));
}

//! 確定(分割)。両側を 2 つの部品として入れる(下見に使った形と平面をそのまま)。1 回で戻せる。
void V2ShellSplitTool::ConfirmSplit()
{
    if (!builtOther_.has_value()) {
        return;
    }
    const std::string label = LabelOf(1);
    ShellSplitDefinition definition;
    definition.method = 1;
    definition.source = input_.part;
    definition.planeOrigin = builtPlane_.origin;
    definition.planeNormal = builtPlane_.normal;
    const auto positive = *built_;
    const auto negative = *builtOther_;
    const auto outcome = outcome_;
    const EntityId source = input_.part;
    auto& document = window_.session_->GetDocument();
    kachakacha::v2::document::Document::Transaction transaction(document, label.c_str());
    definition.side = 1;
    const auto positiveId = window_.AddPartFeature(kachakacha::v2::domain::FeatureType::ShellSplit,
        definition, positive, {}, "分割(法線の側)", {source});
    definition.side = -1;
    const auto negativeId = positiveId.IsNil() ? EntityId{}
        : window_.AddPartFeature(kachakacha::v2::domain::FeatureType::ShellSplit, definition,
              negative, {}, "分割(反対の側)", {source});
    if (negativeId.IsNil()) {
        return;
    }
    const auto hidden = document.Run(kachakacha::v2::document::SetVisibilityCommand(
        {source}, kachakacha::v2::domain::Visibility::Hidden));
    if (!hidden.committed) {
        window_.ReportDiagnostics(hidden.diagnostics);
        return;
    }
    if (!transaction.Commit()) {
        return;
    }
    End();
    window_.AdoptCurrentDocument();
    window_.SetStatus(QStringLiteral("分割: 2 つに分けました(法線の側 %1 mm3 / 反対の側 %2 mm3)。"
                                     "元の部品は隠しました。")
            .arg(outcome.volumeMm3, 0, 'f', 4)
            .arg(outcome.otherVolumeMm3, 0, 'f', 4));
}

bool V2ShellSplitTool::Rebuild(const kachakacha::v2::domain::Feature& feature, const EntityId& output)
{
    const auto* definition = std::get_if<ShellSplitDefinition>(&feature.definition);
    if (definition == nullptr) {
        return false;
    }
    const auto source = window_.partShapes_.find(definition->source.ToString());
    if (source == window_.partShapes_.end()) {
        return false;   // 元がまだ出来ていない。黙って作らない。
    }
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    std::optional<kachakacha::v2::modeling::KernelShapeHandle> handle;
    if (definition->method == 1) {
        const auto made = kachakacha::v2::kernel::SplitSolidByPlane(source->second,
            definition->planeOrigin, definition->planeNormal, tolerance);
        if (!made.HasValue()) {
            window_.ReportDiagnostics(made.Diagnostics());
            return false;
        }
        handle = definition->side < 0 ? made.Value().negative : made.Value().positive;
    } else {
        const auto made = kachakacha::v2::kernel::ShellSolid(source->second, definition->facePoints,
            definition->thickness.value, tolerance);
        if (!made.HasValue()) {
            window_.ReportDiagnostics(made.Diagnostics());
            return false;
        }
        handle = made.Value().handle;
    }
    window_.partShapes_[output.ToString()] = *handle;
    window_.partEdges_[output.ToString()] = {};
    return true;
}
