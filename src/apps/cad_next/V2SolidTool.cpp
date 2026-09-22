//! 「立体を作る」の道具。V2SolidTool.h の頭の注記を見よ。

#include "V2SolidTool.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/SolidInput.h"
#include "kachakacha/modeling/ToolController.h"

#include <QKeyEvent>
#include <QString>

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using kachakacha::v2::app::RevolveMode;
using kachakacha::v2::app::SolidPickKind;
using kachakacha::v2::app::SolidSlot;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::CreateSolidDefinition;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::kernel::SolidBuildResult;
using kachakacha::v2::modeling::SolidMethod;

namespace {

using SolidResult = kachakacha::v2::base::Result<SolidBuildResult>;

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

[[nodiscard]] std::string MethodLabel(SolidMethod method)
{
    return std::string(kachakacha::v2::modeling::SolidMethodNameJa(method));
}

//! 何を押してほしいかの一文(道具を構えたとき)。
[[nodiscard]] std::string GuideFor(SolidMethod method)
{
    switch (method) {
    case SolidMethod::Revolve:
        return "閉じた輪郭(外周と穴。何個でも)と、回転軸にする直線を 3D で押してください。";
    case SolidMethod::Loft:
        return "閉じた断面を、通す順に 3D で押してください(2 つ以上、何個でも)。";
    case SolidMethod::Sweep:
        return "閉じた輪郭と、経路の線(何本でも。1 本につながるように)を 3D で押してください。";
    }
    return "";
}

[[nodiscard]] std::vector<EntityId> Missing(const std::vector<EntityId>& left,
    const std::vector<EntityId>& right)
{
    std::vector<EntityId> out;
    for (const EntityId& id : left) {
        if (std::find(right.begin(), right.end(), id) == right.end()) {
            out.push_back(id);
        }
    }
    return out;
}

//! 断った理由の一文(細かい説明があれば括弧で添える)。
[[nodiscard]] std::string RefusalOf(const SolidResult& made)
{
    std::string text = made.FirstSummaryJa();
    const auto first = made.FirstDiagnostic();
    if (!first.detailsJa.empty()) {
        text += "(" + first.detailsJa + ")";
    }
    return text;
}

} // namespace

V2SolidTool::V2SolidTool(V2MainWindow& window)
    : window_(window)
{
    dock_ = new V2SolidDock(&window);
    dock_->SetRevolveModeHandler([this](RevolveMode mode) { ChooseRevolveMode(mode); });
    dock_->SetAngleHandler([this](double degrees) {
        input_.angleDeg = degrees;
        Refresh();
    });
    dock_->SetBooleanHandler([this](int mode) {
        input_ = kachakacha::v2::app::WithSolidBoolean(input_, mode);
        MirrorToSelection();
        Refresh();
    });
    dock_->SetActivateHandler([this](SolidSlot slot) {
        input_ = kachakacha::v2::app::WithActiveSolidSlot(input_, slot);
        RefreshDock();
        window_.SetStatus(Text(MethodLabel(input_.method) + ": 次のクリックは「"
            + std::string(kachakacha::v2::app::SolidSlotNameJa(slot, input_.method))
            + "」へ入ります。"));
    });
    dock_->SetClearHandler([this](SolidSlot slot) {
        input_ = kachakacha::v2::app::WithSolidSlotCleared(input_, slot);
        MirrorToSelection();
        Refresh();
    });
    dock_->SetActionHandlers([this] { Confirm(); },
        [this] {
            const std::string label = MethodLabel(input_.method);
            End();
            window_.SetStatus(Text(label + ": やめました。何も作っていません。"));
        });
}

bool V2SolidTool::Handles(std::string_view commandId)
{
    SolidMethod method = SolidMethod::Revolve;
    return kachakacha::v2::app::SolidMethodForCommand(commandId, method);
}

void V2SolidTool::Begin(std::string_view commandId)
{
    SolidMethod method = SolidMethod::Revolve;
    if (!kachakacha::v2::app::SolidMethodForCommand(commandId, method)) {
        return;
    }
    if (active_) {
        // 構えている間の2度目は確定(厚み・近似と同じ文法)。違う作り方なら作り方だけ替える
        // (入っている輪郭と相手はそのまま)。
        if (method == input_.method) {
            Confirm();
            return;
        }
        input_ = kachakacha::v2::app::WithSolidMethod(input_, method);
        MirrorToSelection();
        Refresh();
        window_.SetStatus(Text(MethodLabel(method) + ": " + GuideFor(method) + "\n"
            + kachakacha::v2::app::SolidHintJa(input_)));
        return;
    }
    // 線を引く道具のままだと、3D の押しが点になってしまう。選ぶ道具に戻して構える。
    if (window_.session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select) {
        window_.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    }
    input_ = kachakacha::v2::app::SolidInputState{};
    input_.method = method;
    // 選んであったものは、選んだ順に種類で欄へ入れる(選んでから押す道も残す)。
    for (const EntityId& id : window_.viewport_->Selection().entityIds) {
        input_ = kachakacha::v2::app::WithSolidPick(input_, id, KindOf(id));
    }
    active_ = true;
    window_.RefreshRightShelves();
    window_.viewport_->SetToolPickActive(true);
    window_.viewport_->SetToolPickToggle(true);
    MirrorToSelection();
    Refresh();
    window_.SetStatus(Text(MethodLabel(method) + ": " + GuideFor(method)
        + "押し直すと外れます。Enter で確定、Esc でやめます。\n"
        + kachakacha::v2::app::SolidHintJa(input_)));
}

void V2SolidTool::ChooseRevolveMode(RevolveMode mode)
{
    input_.revolveMode = mode;
    Refresh();
    window_.SetStatus(Text(MethodLabel(input_.method) + ": "
        + std::string(kachakacha::v2::app::RevolveModeNameJa(mode))
        + (mode == RevolveMode::Full ? "(360°)" : "(角度は「3. 設定」で打てます)")));
}

void V2SolidTool::End()
{
    active_ = false;
    built_.reset();
    outcome_ = kachakacha::v2::app::SolidPreviewOutcome{};
    mirror_.clear();
    if (window_.viewport_ != nullptr) {
        window_.viewport_->HideToolPreview();
        window_.viewport_->HideToolRoleLabels();
        window_.viewport_->SetToolPickActive(false);
        window_.viewport_->SetToolPickToggle(false);
        // 欄の印を選択に残さない。残すと、次に構えたときに勝手に欄へ入る。
        mirroring_ = true;
        window_.viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
        mirroring_ = false;
    }
    window_.ShowToolFooter(QString());
    window_.RefreshRightShelves();
}

bool V2SolidTool::HandleKey(int key)
{
    if (!active_) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        const std::string label = MethodLabel(input_.method);
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

//! 3D の選択の印を、欄の中身に合わせる。欄が正本。
void V2SolidTool::MirrorToSelection()
{
    mirror_ = kachakacha::v2::app::SolidEntries(input_);
    kachakacha::v2::app::SelectionSet mirrored;
    for (const EntityId& id : mirror_) {
        if (std::find(mirrored.entityIds.begin(), mirrored.entityIds.end(), id)
            != mirrored.entityIds.end()) {
            continue;
        }
        mirrored.entityIds.push_back(id);
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = id;
        mirrored.ordered.push_back(ref);
    }
    mirroring_ = true;
    window_.viewport_->SetSelection(std::move(mirrored));
    mirroring_ = false;
}

//! 3D の選択が変わった。押した当人が分かればそれを、分からなければ(一覧で選んだなど)
//! 選択の差分を欄へ移す。受けなかったもの(面など)は選択に残さない。
void V2SolidTool::HandleSelectionChanged()
{
    if (!active_ || mirroring_ || window_.viewport_ == nullptr) {
        return;
    }
    if (const auto pick = window_.viewport_->TakeLastToolPick(); pick.has_value()) {
        ApplyPick(*pick);
    } else {
        const auto& now = window_.viewport_->Selection().entityIds;
        for (const EntityId& id : Missing(now, mirror_)) {
            ApplyPick(id);
        }
        const auto removed = Missing(mirror_, now);
        if (!removed.empty()) {
            input_ = kachakacha::v2::app::WithoutSolidEntries(input_, removed);
        }
    }
    MirrorToSelection();
    Refresh();
}

void V2SolidTool::ApplyPick(const EntityId& id)
{
    std::string why;
    input_ = kachakacha::v2::app::WithSolidPick(input_, id, KindOf(id), &why);
    if (!why.empty()) {
        window_.SetStatus(Text(MethodLabel(input_.method) + ": " + why));
    }
}

std::vector<kachakacha::v2::geometry::CurveSegment> V2SolidTool::SegmentsOf(
    const EntityId& id) const
{
    std::vector<kachakacha::v2::geometry::CurveSegment> segments;
    for (const auto& curve : window_.session_->Scene().curves) {
        if (curve.entityId == id) {
            segments.push_back(curve.segment);
        }
    }
    return segments;
}

//! 押したものの種類。閉じているかは押し出しと同じ判断(SegmentsFormClosedLoop)を使う。
SolidPickKind V2SolidTool::KindOf(const EntityId& id) const
{
    const auto* entity = window_.session_->GetDocument().FindEntity(id);
    if (entity == nullptr) {
        return SolidPickKind::Other;
    }
    if (entity->kind == EntityKind::Part) {
        return SolidPickKind::Part;
    }
    if (entity->kind != EntityKind::Wire) {
        return SolidPickKind::Other;
    }
    const auto segments = SegmentsOf(id);
    if (segments.empty()) {
        return SolidPickKind::Other;
    }
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    if (kachakacha::v2::geometry::SegmentsFormClosedLoop(segments, tolerance)) {
        return SolidPickKind::ClosedWire;
    }
    if (segments.size() == 1
        && segments.front().Kind() == kachakacha::v2::geometry::CurveKind::Line) {
        return SolidPickKind::LineWire;
    }
    return SolidPickKind::OpenWire;
}

std::string V2SolidTool::NameOf(const EntityId& id) const
{
    const auto* entity = id.IsNil() ? nullptr : window_.session_->GetDocument().FindEntity(id);
    return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                              : std::string("名前のないもの");
}

std::string V2SolidTool::NamesOf(const std::vector<EntityId>& ids) const
{
    std::string names;
    for (const EntityId& id : ids) {
        names += (names.empty() ? "" : " / ") + NameOf(id);
    }
    return names;
}

CreateSolidDefinition V2SolidTool::DefinitionNow() const
{
    CreateSolidDefinition definition;
    definition.method = static_cast<int>(input_.method);
    definition.profiles = input_.profiles;
    if (input_.method == SolidMethod::Revolve && !input_.axis.IsNil()) {
        definition.axis = input_.axis;
    }
    if (input_.method == SolidMethod::Sweep) {
        definition.path = input_.path;
    }
    definition.angleRad = kachakacha::v2::app::SolidAngleRad(input_);
    definition.symmetric = input_.method == SolidMethod::Revolve
        && input_.revolveMode == RevolveMode::Symmetric;
    definition.booleanMode = input_.booleanMode;
    if (input_.booleanMode != 0 && !input_.target.IsNil()) {
        definition.targets.push_back(input_.target);
    }
    return definition;
}

SolidResult V2SolidTool::BuildShape(const CreateSolidDefinition& definition) const
{
    using namespace kachakacha::v2::modeling;
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    switch (static_cast<SolidMethod>(definition.method)) {
    case SolidMethod::Revolve: {
        RevolveSolidRequest request;
        request.profiles = window_.ExtrudeProfilesFor(definition.profiles);
        const auto axis = definition.axis.has_value() ? SegmentsOf(*definition.axis)
                                                      : std::vector<CurveSegment>{};
        if (axis.size() != 1 || axis.front().Kind() != kachakacha::v2::geometry::CurveKind::Line) {
            return SolidResult::Failure(kachakacha::v2::base::MakeError(kSolidBadInput,
                "回転軸は直線 1 本にしてください。"));
        }
        request.axisPoint = axis.front().StartPoint();
        request.axisDirection = axis.front().EndPoint() - axis.front().StartPoint();
        request.angleRad = definition.angleRad;
        request.symmetric = definition.symmetric;
        const auto analysis = AnalyzeRevolveSolid(request, tolerance);
        if (!analysis.HasValue()) {
            return SolidResult::Failure(analysis.Diagnostics());
        }
        return kachakacha::v2::kernel::BuildRevolveSolid(request, analysis.Value(), tolerance);
    }
    case SolidMethod::Loft: {
        // 断面は 1 つずつ読む(まとめて読むと、重なった断面が 1 つの領域に畳まれる)。
        LoftSolidRequest request;
        for (const EntityId& id : definition.profiles) {
            auto profiles = window_.ExtrudeProfilesFor({id});
            if (profiles.size() != 1) {
                return SolidResult::Failure(kachakacha::v2::base::MakeError(kSolidBadInput,
                    "ロフト立体の断面は、それぞれ 1 つの閉じた線にしてください。",
                    NameOf(id) + " は " + std::to_string(profiles.size()) + " つの輪郭に分かれます。"));
            }
            request.sections.push_back(std::move(profiles.front()));
        }
        const auto analysis = AnalyzeLoftSolid(request, tolerance);
        if (!analysis.HasValue()) {
            return SolidResult::Failure(analysis.Diagnostics());
        }
        return kachakacha::v2::kernel::BuildLoftSolid(request, analysis.Value(), tolerance);
    }
    case SolidMethod::Sweep: {
        SweepSolidRequest request;
        request.profiles = window_.ExtrudeProfilesFor(definition.profiles);
        for (const EntityId& id : definition.path) {
            const auto segments = SegmentsOf(id);
            request.path.insert(request.path.end(), segments.begin(), segments.end());
        }
        const auto analysis = AnalyzeSweepSolid(request, tolerance);
        if (!analysis.HasValue()) {
            return SolidResult::Failure(analysis.Diagnostics());
        }
        return kachakacha::v2::kernel::BuildSweepSolid(request, analysis.Value(), tolerance);
    }
    }
    return SolidResult::Failure(kachakacha::v2::base::MakeError(kSolidBadInput,
        "知らない作り方です。"));
}

SolidResult V2SolidTool::Build(const CreateSolidDefinition& definition) const
{
    auto made = BuildShape(definition);
    if (!made.HasValue() || definition.booleanMode == 0) {
        return made;
    }
    // 足す・引く。相手は窓が覚えている部品の形(作り直しでは評価順で先に出来ている)。
    const auto target = definition.targets.empty()
        ? window_.partShapes_.end()
        : window_.partShapes_.find(definition.targets.front().ToString());
    if (target == window_.partShapes_.end()) {
        return SolidResult::Failure(kachakacha::v2::base::MakeError(
            kachakacha::v2::modeling::kSolidBadInput, "足す・引くの相手の部品の立体がまだありません。"));
    }
    const double joinMm =
        window_.session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto combined = kachakacha::v2::kernel::BuildBoolean(definition.booleanMode == 2
            ? kachakacha::v2::kernel::BooleanOperation::Difference
            : kachakacha::v2::kernel::BooleanOperation::Union,
        target->second, made.Value().handle, joinMm);
    if (!combined.HasValue()) {
        return SolidResult::Failure(combined.Diagnostics());
    }
    return SolidResult::Success(SolidBuildResult{combined.Value().handle, combined.Value().volumeMm3});
}

void V2SolidTool::Refresh()
{
    RefreshPreview();
    RefreshDock();
}

//! そろっていれば **実際に核で作って** 稜線を下見に出す。文書へは書かない。
void V2SolidTool::RefreshPreview()
{
    outcome_ = kachakacha::v2::app::SolidPreviewOutcome{};
    built_.reset();
    window_.viewport_->HideToolPreview();
    if (!kachakacha::v2::app::SolidReady(input_)) {
        return;
    }
    outcome_.evaluated = true;
    const auto made = Build(DefinitionNow());
    if (!made.HasValue()) {
        outcome_.refusalJa = RefusalOf(made);
        return;
    }
    outcome_.available = true;
    outcome_.volumeMm3 = made.Value().volumeMm3;
    built_ = made.Value().handle;
    // 出来上がりの稜線。画面に出すのは、確定で文書へ入るその形。
    const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(made.Value().handle);
    if (mesh.HasValue()) {
        window_.viewport_->ShowToolPreview(mesh.Value().edges);
    }
}

//! 棚・札・一番下の一行を、いまの入力に合わせる。
void V2SolidTool::RefreshDock()
{
    const bool previewShown = !window_.viewport_->ToolPreview().empty();
    std::vector<QString> lines;
    for (const std::string& line :
        kachakacha::v2::app::SolidStatusLinesJa(input_, outcome_, previewShown)) {
        lines.push_back(Text(line));
    }
    const std::string profiles = NamesOf(input_.profiles);
    const std::string second = input_.method == SolidMethod::Sweep ? NamesOf(input_.path)
                                                                    : NameOf(input_.axis);
    dock_->ShowInput(input_, Text(profiles), Text(second), Text(NameOf(input_.target)), lines,
        built_.has_value());
    window_.ShowToolFooter(active_
            ? Text(kachakacha::v2::app::SolidFooterLine(input_, profiles, second, outcome_,
                  previewShown))
            : QString());
    if (!active_) {
        return;
    }
    // 3D の札。輪郭(ロフト立体は通す順の番号つき)・軸・経路・相手。
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    const auto numbered = [&labels](const std::vector<EntityId>& ids, SolidSlot slot) {
        const std::string key(kachakacha::v2::app::SolidSlotKey(slot));
        for (std::size_t index = 0; index < ids.size(); ++index) {
            labels.push_back({ids[index],
                ids.size() == 1 ? key : key + " " + std::to_string(index + 1)});
        }
    };
    numbered(input_.profiles, SolidSlot::Profiles);
    if (input_.method == SolidMethod::Revolve && !input_.axis.IsNil()) {
        labels.push_back({input_.axis, std::string(kachakacha::v2::app::SolidSlotKey(SolidSlot::Axis))});
    }
    if (input_.method == SolidMethod::Sweep) {
        numbered(input_.path, SolidSlot::Path);
    }
    if (input_.booleanMode != 0 && !input_.target.IsNil()) {
        labels.push_back({input_.target, std::string(kachakacha::v2::app::SolidSlotKey(SolidSlot::Target))});
    }
    window_.ShowRoleLabels(labels);
}

//! 確定。**下見に使った形をそのまま**文書へ入れる。相手を隠すのも含めて 1 回で戻せる。
void V2SolidTool::Confirm()
{
    const std::string label = MethodLabel(input_.method);
    if (!built_.has_value()) {
        window_.SetStatus(Text(label + ": まだ作れません。"
            + (outcome_.refusalJa.empty() ? kachakacha::v2::app::SolidHintJa(input_)
                                          : outcome_.refusalJa)));
        RefreshDock();
        return;
    }
    const CreateSolidDefinition definition = DefinitionNow();
    std::vector<EntityId> used = definition.profiles;
    if (definition.axis.has_value()) {
        used.push_back(*definition.axis);
    }
    used.insert(used.end(), definition.path.begin(), definition.path.end());
    used.insert(used.end(), definition.targets.begin(), definition.targets.end());
    const auto handle = *built_;
    const double volume = outcome_.volumeMm3;
    auto& document = window_.session_->GetDocument();
    kachakacha::v2::document::Document::Transaction transaction(document, label.c_str());
    // 出来た形の辺は、いまは持たない(画面は形そのものの網で出る)。
    const auto madeId = window_.AddPartFeature(kachakacha::v2::domain::FeatureType::CreateSolid,
        definition, handle, {}, label.c_str(), used);
    if (madeId.IsNil()) {
        return;   // まとまりは戻る(Transaction が捨てる)。
    }
    // 足す・引くの相手は隠す。消すと、作り方をたどれなくなる。
    if (!definition.targets.empty()) {
        const auto hidden = document.Run(kachakacha::v2::document::SetVisibilityCommand(
            definition.targets, kachakacha::v2::domain::Visibility::Hidden));
        if (!hidden.committed) {
            window_.ReportDiagnostics(hidden.diagnostics);
            return;
        }
    }
    if (!transaction.Commit()) {
        return;
    }
    End();
    window_.AdoptCurrentDocument();
    window_.SetStatus(QStringLiteral("%1: 部品を作りました(体積 %2 mm3)。1 回の取り消しで戻ります。")
            .arg(Text(label))
            .arg(volume, 0, 'f', 4));
}

bool V2SolidTool::Rebuild(const kachakacha::v2::domain::Feature& feature, const EntityId& output)
{
    const auto* definition = std::get_if<CreateSolidDefinition>(&feature.definition);
    if (definition == nullptr) {
        return false;
    }
    const auto made = Build(*definition);
    if (!made.HasValue()) {
        window_.ReportDiagnostics(made.Diagnostics());
        return false;
    }
    window_.partShapes_[output.ToString()] = made.Value().handle;
    window_.partEdges_[output.ToString()] = {};
    return true;
}
