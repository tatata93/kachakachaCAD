//! 面にする(線から面)。見出しは V2LoopFacesTool.h。

#include "V2LoopFacesTool.h"

#include "V2LoopFacesDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Feature.h"

#include <QString>
#include <Qt>

#include <string>
#include <utility>
#include <vector>

using kachakacha::v2::app::LoopFace;
using kachakacha::v2::app::LoopFaceMethod;
using kachakacha::v2::app::LoopGap;
using kachakacha::v2::app::LoopSplit;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::CurveSegment;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

constexpr const char* kLabelJa = "面にする";

[[nodiscard]] std::string MethodLabel(LoopFaceMethod method)
{
    switch (method) {
    case LoopFaceMethod::Planar:       return "平面";
    case LoopFaceMethod::FourEdge:     return "四辺面";
    case LoopFaceMethod::BoundaryFill: return "境界面";
    case LoopFaceMethod::Loft:         return "ロフト";
    }
    return "面";
}

} // namespace

V2LoopFacesTool::V2LoopFacesTool(V2MainWindow& window) : window_(window)
{
    dock_ = new V2LoopFacesDock(&window);
    dock_->SetMethodHandler([this](int face, int methodIndex) {
        if (!plan_.has_value() || face < 0 || static_cast<std::size_t>(face) >= plan_->faces.size()) {
            return;
        }
        const LoopFace& item = plan_->faces[static_cast<std::size_t>(face)];
        const auto choices = kachakacha::v2::app::LoopFaceMethodChoices(item.selections.size(),
            item.method == LoopFaceMethod::Planar, item.method == LoopFaceMethod::Loft);
        if (methodIndex >= 0 && static_cast<std::size_t>(methodIndex) < choices.size()) {
            methodOverride_[static_cast<std::size_t>(face)] = choices[static_cast<std::size_t>(methodIndex)];
            ShowPreview();
        }
    });
    dock_->SetMakeHandler([this](int face, bool make) {
        if (plan_.has_value() && face >= 0 && static_cast<std::size_t>(face) < make_.size()) {
            make_[static_cast<std::size_t>(face)] = make;
            ShowPreview();
        }
    });
    dock_->SetGapHandlers(
        [this](int gap) { CloseOneGap(gap); },
        [this](int gap) {
            if (plan_.has_value() && gap >= 0 && static_cast<std::size_t>(gap) < leaveGap_.size()) {
                leaveGap_[static_cast<std::size_t>(gap)] = !leaveGap_[static_cast<std::size_t>(gap)];
                ShowPreview();
            }
        });
    dock_->SetToleranceHandler([this](double joinMm) {
        joinMm_ = joinMm;
        if (plan_.has_value() && Replan()) {
            ShowPreview();
        }
    });
    dock_->SetActionHandlers([this] { (void)Confirm(); },
        [this] {
            Clear();
            window_.SetStatus(QStringLiteral("面にする: やめました。何も変えていません。"));
        });
}

void V2LoopFacesTool::Start()
{
    Clear();
    selections_.clear();
    joinMm_.reset();
    for (const EntityId& id : window_.viewport_->Selection().entityIds) {
        const auto chosen = kachakacha::v2::app::GuideSelectionOf(
            window_.session_->GetDocument(), window_.session_->Scene(), id);
        if (chosen.has_value()) {
            selections_.push_back(*chosen);
        }
    }
    if (selections_.empty()) {
        window_.SetStatus(QStringLiteral("面にする: 先に線を選んでください。"));
        return;
    }
    if (!Replan()) {
        return;
    }
    ShowPreview();
    window_.RefreshRightShelves();
}

kachakacha::v2::geometry::GeometryTolerance V2LoopFacesTool::ToleranceNow() const
{
    auto tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    if (joinMm_.has_value()) {
        tolerance.interactiveJoinMm = *joinMm_;
    }
    return tolerance;
}

bool V2LoopFacesTool::Replan()
{
    auto planned = kachakacha::v2::app::PlanLoopFaces(selections_, ToleranceNow());
    if (!planned.HasValue()) {
        plan_.reset();
        window_.ReportDiagnostics(planned.Diagnostics());
        return false;
    }
    plan_ = planned.Value();
    // 輪やずれの数が同じなら、人が決めたこと(作り方・作るか・そのまま)は持ち越す。
    if (methodOverride_.size() != plan_->faces.size()) {
        methodOverride_.assign(plan_->faces.size(), std::nullopt);
        make_.assign(plan_->faces.size(), true);
    }
    if (leaveGap_.size() != plan_->gaps.size()) {
        leaveGap_.assign(plan_->gaps.size(), false);
    }
    return true;
}

LoopFaceMethod V2LoopFacesTool::MethodOf(std::size_t face) const
{
    if (!plan_.has_value() || face >= plan_->faces.size()) {
        return LoopFaceMethod::Planar;
    }
    return methodOverride_[face].value_or(plan_->faces[face].method);
}

//! 輪は実線の下見、ずれは赤系の破線と × 印。棚に輪の表、一番下の一行に内訳、案内に次の手。
void V2LoopFacesTool::ShowPreview()
{
    if (!plan_.has_value()) {
        return;
    }
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines;
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    for (std::size_t at = 0; at < plan_->faces.size(); ++at) {
        if (!make_[at]) {
            continue;
        }
        const LoopFace& face = plan_->faces[at];
        for (const auto& line : face.previewLines) {
            lines.push_back(line);
        }
        // 輪の番号の札は、輪の最初の線に付ける(片なら元の線)。
        if (!face.selections.empty()) {
            const std::size_t piece = face.selections.front();
            const std::size_t source = piece < plan_->pieces.size() ? plan_->pieces[piece].source : piece;
            if (source < selections_.size()) {
                labels.push_back({selections_[source].sourceWireId, std::to_string(at + 1)});
            }
        }
    }
    window_.viewport_->ShowToolPreview(lines);
    window_.ShowRoleLabels(labels);
    V2Viewport::EditPreview gaps;
    gaps.removing = true;
    for (const LoopGap& gap : plan_->gaps) {
        const auto& first = plan_->pieces[gap.firstSelection].segments;
        const auto& second = plan_->pieces[gap.secondSelection].segments;
        const auto a = gap.firstAtEnd ? first.back().EndPoint() : first.front().StartPoint();
        const auto b = gap.secondAtEnd ? second.back().EndPoint() : second.front().StartPoint();
        gaps.lines.push_back({a, b});
        gaps.markers.push_back(a);
        gaps.markers.push_back(b);
    }
    for (const LoopSplit& split : plan_->splits) {
        for (const auto& point : split.points) {
            gaps.markers.push_back(point);
        }
    }
    if (gaps.lines.empty() && gaps.markers.empty()) {
        window_.viewport_->HideEditPreview();
    } else {
        window_.viewport_->ShowEditPreview(std::move(gaps));
    }
    ShowDock();
    window_.ShowToolFooter(QStringLiteral("面にする: %1 / Preview only").arg(Text(plan_->summaryJa)));
    bool pending = false;
    for (std::size_t at = 0; at < plan_->gaps.size(); ++at) {
        pending = pending || (plan_->gaps[at].movable && !leaveGap_[at]);
    }
    if (pending) {
        window_.SetStatus(QStringLiteral("面にする: 端が離れているところがあります(棚の 3.)。"
                                         "Enter で寄せてから作ります。寄せたくなければ[そのまま]を押してください。Esc でやめます。"));
        return;
    }
    window_.SetStatus(QStringLiteral("面にする: %1 の輪が見つかりました(%2)。"
                                     "Enter で作ります(元の線は残ります)。Esc でやめます。")
            .arg(static_cast<int>(plan_->faces.size()))
            .arg(Text(plan_->summaryJa)));
}

//! 棚に映す。作り方の選択肢は core が決める(平面 ↔ 境界面など)。
void V2LoopFacesTool::ShowDock()
{
    V2LoopFacesView view;
    for (std::size_t at = 0; at < plan_->faces.size(); ++at) {
        const LoopFace& face = plan_->faces[at];
        V2LoopFaceRow row;
        row.number = static_cast<int>(at + 1);
        const auto choices = kachakacha::v2::app::LoopFaceMethodChoices(face.selections.size(),
            face.method == LoopFaceMethod::Planar, face.method == LoopFaceMethod::Loft);
        const LoopFaceMethod current = MethodOf(at);
        for (std::size_t index = 0; index < choices.size(); ++index) {
            row.methodChoicesJa.push_back(Text(kachakacha::v2::app::LoopFaceMethodLabelJa(choices[index])));
            if (choices[index] == current) {
                row.methodIndex = static_cast<int>(index);
            }
        }
        row.edgeCount = static_cast<int>(face.selections.size());
        row.statusJa = face.method == LoopFaceMethod::Planar || face.method == LoopFaceMethod::Loft
            ? QStringLiteral("✓")
            : QStringLiteral("平面から %1 mm").arg(face.planeDeviationMm, 0, 'f', 3);
        row.make = make_[at];
        view.faces.push_back(row);
    }
    for (std::size_t at = 0; at < plan_->gaps.size(); ++at) {
        const LoopGap& gap = plan_->gaps[at];
        V2LoopGapRow row;
        row.textJa = Text(kachakacha::v2::app::LoopPieceLabelJa(selections_, *plan_, gap.firstSelection)
            + " と " + kachakacha::v2::app::LoopPieceLabelJa(selections_, *plan_, gap.secondSelection)
            + " の端が " + QString::number(gap.distanceMm, 'f', 3).toStdString() + " mm 離れています"
            + (gap.movable ? "" : "(どちらも直線でないので寄せられません)"));
        row.movable = gap.movable;
        row.leave = leaveGap_[at];
        view.gaps.push_back(row);
    }
    for (const LoopSplit& split : plan_->splits) {
        view.splitsJa.push_back(Text(kachakacha::v2::app::LoopSplitTextJa(selections_, split)));
    }
    QString unused;
    for (const std::size_t index : plan_->unused) {
        unused += (unused.isEmpty() ? QStringLiteral("") : QStringLiteral("、")) + Text(selections_[index].label);
    }
    view.unusedJa = unused.isEmpty() ? QString() : QStringLiteral("使わない線: %1").arg(unused);
    view.joinMm = ToleranceNow().interactiveJoinMm;
    view.summaryJa = Text(plan_->summaryJa);
    bool any = false;
    for (const bool make : make_) {
        any = any || make;
    }
    view.canConfirm = any;
    dock_->ShowView(view);
}

bool V2LoopFacesTool::HandleKey(int key)
{
    if (!plan_.has_value()) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        Clear();
        window_.SetStatus(QStringLiteral("面にする: やめました。何も変えていません。"));
        return true;
    }
    if (key != Qt::Key_Return && key != Qt::Key_Enter) {
        return false;
    }
    return Confirm();
}

//! Enter: T 字を分け、寄せると決めたずれを寄せ、計画し直してから作る。全部 1 回で戻る。
bool V2LoopFacesTool::Confirm()
{
    if (!plan_.has_value()) {
        return false;
    }
    auto& document = window_.session_->GetDocument();
    kachakacha::v2::document::Document::Transaction transaction(document, kLabelJa);
    // T 字で分ける → 計画し直す → 寄せる → 計画し直す → 作る。
    if (ApplySplits() && !Replan()) {
        Clear();
        window_.AdoptCurrentDocument();
        return true;
    }
    if (CloseGaps() && !Replan()) {
        Clear();
        window_.AdoptCurrentDocument();
        return true;
    }
    if (plan_->faces.empty()) {
        window_.SetStatus(QStringLiteral("面にする: 閉じた輪が無いので、面は作りません。"));
        Clear();
        window_.AdoptCurrentDocument();
        return true;
    }
    const int made = BuildFaces();
    if (made == 0) {
        Clear();
        window_.AdoptCurrentDocument();
        return true;
    }
    const QString summary = Text(plan_->summaryJa);
    QString unused;
    for (const std::size_t index : plan_->unused) {
        unused += (unused.isEmpty() ? QStringLiteral("") : QStringLiteral("、")) + Text(selections_[index].label);
    }
    if (!transaction.Commit()) {
        window_.SetStatus(QStringLiteral("面にする: 途中で失敗したので、何も変えていません。"));
        Clear();
        window_.AdoptCurrentDocument();
        return true;
    }
    Clear();
    window_.AdoptCurrentDocument();
    window_.RefreshShapeViews();
    window_.RefreshEntityList();
    window_.SetStatus(QStringLiteral("面にする: %1 枚作りました(%2)。元の線は残しています。%3")
            .arg(made)
            .arg(summary)
            .arg(unused.isEmpty() ? QString() : QStringLiteral("使わなかった線: %1。").arg(unused)));
    return true;
}

void V2LoopFacesTool::Clear()
{
    if (!plan_.has_value()) {
        return;
    }
    plan_.reset();
    if (window_.viewport_ != nullptr) {
        window_.viewport_->HideToolPreview();
        window_.viewport_->HideEditPreview();
        window_.viewport_->HideToolRoleLabels();
    }
    window_.ShowToolFooter(QString());
    window_.RefreshRightShelves();
}

//! 1 本の線を片(1 本以上)に置き換える。元の線は入力にしない(形をそのまま持つので消してよい。
//! 隠れた線を残さない)。名前は元のまま(2 本目からは番号)。
std::vector<EntityId> V2LoopFacesTool::ReplaceWire(std::size_t selection,
    const std::vector<std::vector<CurveSegment>>& parts, const std::string& label)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    auto& document = window_.session_->GetDocument();
    const EntityId sourceId = selections_[selection].sourceWireId;
    const auto* source = document.FindEntity(sourceId);
    if (source == nullptr) {
        return {};
    }
    const std::string baseName = source->displayName;
    const bool construction = source->construction;
    const auto groupId = source->groupId;
    const bool datum = source->datum;
    std::vector<EntityId> made;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        Feature feature;
        feature.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
        feature.type = FeatureType::TransformWire;
        feature.displayName = label;
        kachakacha::v2::domain::CreateWireDefinition wire;
        wire.segments = parts[index];
        wire.construction = construction;
        for (std::size_t k = 0; k < wire.segments.size(); ++k) {
            wire.segmentIds.push_back(window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
        }
        feature.definition = std::move(wire);
        Entity entity;
        entity.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
        entity.kind = EntityKind::Wire;
        entity.displayName = index == 0 ? baseName
            : kachakacha::v2::app::UniqueDisplayName(document.Snapshot(), EntityKind::Wire, baseName);
        entity.groupId = groupId;
        entity.datum = datum;
        entity.construction = construction;
        entity.createdBy = feature.id;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        const auto added = document.Run(AddFeatureCommand(feature, {entity}, label));
        if (!added.committed) {
            window_.ReportDiagnostics(added.diagnostics);
            return {};
        }
        made.push_back(entity.id);
    }
    window_.RemoveConsumedWires({sourceId});
    return made;
}

bool V2LoopFacesTool::ApplySplits()
{
    if (plan_->splits.empty()) {
        return false;
    }
    const std::vector<LoopSplit> splits = plan_->splits;
    std::vector<kachakacha::v2::modeling::GuideTableSelection> next = selections_;
    bool changed = false;
    for (const LoopSplit& split : splits) {
        const auto parts = kachakacha::v2::app::SplitLoopSource(selections_, split);
        if (!parts.HasValue()) {
            window_.ReportDiagnostics(parts.Diagnostics());
            return changed;
        }
        const std::string label = std::string(kLabelJa) + "(T 字で分ける)";
        const auto ids = ReplaceWire(split.source, parts.Value(), label);
        if (ids.size() != parts.Value().size()) {
            return changed;
        }
        // 元の選択を 1 つ目の片に差し替え、残りの片を後ろに足す。
        const std::string baseLabel = selections_[split.source].label;
        next[split.source].sourceWireId = ids.front();
        next[split.source].segments = parts.Value().front();
        for (std::size_t index = 1; index < ids.size(); ++index) {
            kachakacha::v2::modeling::GuideTableSelection piece;
            piece.sourceWireId = ids[index];
            piece.label = baseLabel + "(片 " + std::to_string(index + 1) + ")";
            piece.segments = parts.Value()[index];
            next.push_back(piece);
        }
        changed = true;
    }
    selections_ = next;
    return changed;
}

bool V2LoopFacesTool::CloseGaps()
{
    const std::vector<LoopGap> gaps = plan_->gaps;
    bool changed = false;
    for (std::size_t at = 0; at < gaps.size(); ++at) {
        const LoopGap& gap = gaps[at];
        if (!gap.movable || (at < leaveGap_.size() && leaveGap_[at])) {
            continue;
        }
        if (gap.firstSelection >= selections_.size() || gap.secondSelection >= selections_.size()) {
            continue;   // 片(まだ分けていない)の端。分けたあとに計画し直してから寄せる
        }
        const auto fix = kachakacha::v2::app::CloseLoopGap(selections_, gap);
        if (!fix.HasValue()) {
            window_.ReportDiagnostics(fix.Diagnostics());
            return changed;
        }
        const std::string label = std::string(kLabelJa) + "(端を寄せる)";
        const auto replace = [&](std::size_t selection, const CurveSegment& segment) {
            const auto ids = ReplaceWire(selection, {{segment}}, label);
            if (ids.empty()) {
                return false;
            }
            selections_[selection].sourceWireId = ids.front();
            selections_[selection].segments = {segment};
            return true;
        };
        if (fix.Value().first.has_value() && !replace(gap.firstSelection, *fix.Value().first)) {
            return changed;
        }
        if (fix.Value().second.has_value() && !replace(gap.secondSelection, *fix.Value().second)) {
            return changed;
        }
        changed = true;
    }
    return changed;
}

//! [寄せる] を 1 つ押した: そのずれだけ寄せて計画し直す(作るのは Enter)。T 字が残っていれば
//! 先に分ける(分けると番号が変わるので、端の位置で同じずれを探し直す)。
void V2LoopFacesTool::CloseOneGap(int index)
{
    if (!plan_.has_value() || index < 0 || static_cast<std::size_t>(index) >= plan_->gaps.size()) {
        return;
    }
    const LoopGap chosen = plan_->gaps[static_cast<std::size_t>(index)];
    const auto endOf = [&](std::size_t piece, bool atEnd) {
        const auto& segments = plan_->pieces[piece].segments;
        return atEnd ? segments.back().EndPoint() : segments.front().StartPoint();
    };
    const auto a = endOf(chosen.firstSelection, chosen.firstAtEnd);
    const auto b = endOf(chosen.secondSelection, chosen.secondAtEnd);
    kachakacha::v2::document::Document::Transaction transaction(
        window_.session_->GetDocument(), std::string(kLabelJa) + "(端を寄せる)");
    if (ApplySplits() && !Replan()) {
        Clear();
        window_.AdoptCurrentDocument();
        return;
    }
    // 同じずれを端の位置で探す。
    const double joinMm = ToleranceNow().interactiveJoinMm;
    std::optional<LoopGap> target;
    for (const LoopGap& gap : plan_->gaps) {
        const auto ga = endOf(gap.firstSelection, gap.firstAtEnd);
        const auto gb = endOf(gap.secondSelection, gap.secondAtEnd);
        if (((ga - a).Length() <= joinMm && (gb - b).Length() <= joinMm)
            || ((ga - b).Length() <= joinMm && (gb - a).Length() <= joinMm)) {
            target = gap;
            break;
        }
    }
    if (!target.has_value() || target->firstSelection >= selections_.size()
        || target->secondSelection >= selections_.size()) {
        window_.SetStatus(QStringLiteral("面にする: そのずれが見つからなくなりました。計画し直します。"));
        (void)transaction.Commit();
        window_.AdoptCurrentDocument();
        if (Replan()) {
            ShowPreview();
        }
        return;
    }
    const auto fix = kachakacha::v2::app::CloseLoopGap(selections_, *target);
    if (!fix.HasValue()) {
        window_.ReportDiagnostics(fix.Diagnostics());
        return;   // Transaction が捨てる
    }
    const std::string label = std::string(kLabelJa) + "(端を寄せる)";
    bool ok = true;
    if (fix.Value().first.has_value()) {
        const auto ids = ReplaceWire(target->firstSelection, {{*fix.Value().first}}, label);
        ok = ok && !ids.empty();
        if (ok) {
            selections_[target->firstSelection].sourceWireId = ids.front();
            selections_[target->firstSelection].segments = {*fix.Value().first};
        }
    }
    if (ok && fix.Value().second.has_value()) {
        const auto ids = ReplaceWire(target->secondSelection, {{*fix.Value().second}}, label);
        ok = ok && !ids.empty();
        if (ok) {
            selections_[target->secondSelection].sourceWireId = ids.front();
            selections_[target->secondSelection].segments = {*fix.Value().second};
        }
    }
    if (!ok || !transaction.Commit()) {
        Clear();
        window_.AdoptCurrentDocument();
        return;
    }
    window_.AdoptCurrentDocument();
    if (Replan()) {
        ShowPreview();
    }
}

int V2LoopFacesTool::BuildFaces()
{
    const auto tolerance = ToleranceNow();
    int made = 0;
    for (std::size_t at = 0; at < plan_->faces.size(); ++at) {
        if (at < make_.size() && !make_[at]) {
            continue;
        }
        LoopFace face = plan_->faces[at];
        face.method = MethodOf(at);
        const auto table = kachakacha::v2::app::LoopFaceTable(selections_, face, tolerance);
        if (!table.HasValue()) {
            window_.ReportDiagnostics(table.Diagnostics());
            return 0;
        }
        // 作る前に調べ、作れないものは理由を言って全部やめる(半分だけ作らない)。
        const auto built = window_.BuildSurfaceFromTable(table.Value(), true);
        if (!built.has_value()) {
            return 0;
        }
        std::vector<EntityId> inputs;
        for (const std::size_t selection : face.selections) {
            inputs.push_back(selections_[selection].sourceWireId);
        }
        if (window_.AdoptGuideSurface(table.Value(), *built, inputs, MethodLabel(face.method)).IsNil()) {
            return 0;
        }
        ++made;
    }
    return made;
}
