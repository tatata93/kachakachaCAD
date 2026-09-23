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
#include "kachakacha/geometry/CurveSampling.h"

#include <QString>
#include <Qt>

#include <algorithm>
#include <optional>
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
using kachakacha::v2::modeling::SurfaceContinuity;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

constexpr const char* kLabelJa = "面にする";

//! 作った面(形状ガイド)の名前。「平面」だけだと作業平面と紛れる(オーナー指摘 2026-09-23)ので、
//! 「面を作る」と同じく「面」を頭に付け、作り方は括弧で言う。
[[nodiscard]] std::string MethodLabel(LoopFaceMethod method)
{
    switch (method) {
    case LoopFaceMethod::Planar:       return "面(平面)";
    case LoopFaceMethod::FourEdge:     return "面(四辺面)";
    case LoopFaceMethod::BoundaryFill: return "面(境界面)";
    case LoopFaceMethod::Loft:         return "面(ロフト)";
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
    dock_->SetContinuityHandler([this](int face, int edge) {
        if (face >= 0 && edge >= 0
            && CycleContinuity(static_cast<std::size_t>(face), static_cast<std::size_t>(edge))) {
            ShowPreview();
        }
    });
    dock_->SetToleranceHandler([this](double joinMm) {
        joinMm_ = joinMm;
        if (plan_.has_value() && Replan()) {
            ShowPreview();
        }
    });
    dock_->SetReopenHandler([this] { (void)ReopenRecent(); });
    dock_->SetActionHandlers([this] { (void)Confirm(); },
        [this] {
            Clear();
            window_.SetStatus(QStringLiteral("面にする: やめました。何も変えていません。"));
        });
}

void V2LoopFacesTool::Start()
{
    Clear();
    recent_.reset();
    dock_->HideRecent();
    selections_.clear();
    joinMm_.reset();
    methodOverride_.clear();
    make_.clear();
    continuity_.clear();
    leaveGap_.clear();
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
    auto planned = kachakacha::v2::app::PlanLoopFaces(selections_, ToleranceNow(), Neighbors());
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
        continuity_.assign(plan_->faces.size(), {});
    }
    for (std::size_t at = 0; at < plan_->faces.size(); ++at) {
        if (continuity_[at].size() != plan_->faces[at].edges.size()) {
            continuity_[at].assign(plan_->faces[at].edges.size(), SurfaceContinuity::G0);
        }
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

SurfaceContinuity V2LoopFacesTool::ContinuityOf(std::size_t face, std::size_t edge) const
{
    if (!plan_.has_value() || face >= continuity_.size() || edge >= continuity_[face].size()) {
        return SurfaceContinuity::G0;
    }
    const LoopFaceMethod method = MethodOf(face);
    if (method != LoopFaceMethod::FourEdge && method != LoopFaceMethod::BoundaryFill) {
        return SurfaceContinuity::G0;   // 平面・ロフトに辺の連続は付かない
    }
    const auto& item = plan_->faces[face].edges[edge];
    if (!item.neighborSurface.has_value() && !item.neighborFace.has_value()) {
        return SurfaceContinuity::G0;   // 隣が無ければ相手が無い
    }
    return continuity_[face][edge];
}

bool V2LoopFacesTool::CycleContinuity(std::size_t face, std::size_t edge)
{
    if (!plan_.has_value() || face >= continuity_.size() || edge >= continuity_[face].size()) {
        return false;
    }
    const auto& item = plan_->faces[face].edges[edge];
    if (!item.neighborSurface.has_value() && !item.neighborFace.has_value()) {
        window_.SetStatus(QStringLiteral("面にする: 輪 %1 の辺 %2 には隣の面が無いので、連続は付けられません。")
                .arg(static_cast<int>(face + 1)).arg(static_cast<int>(edge + 1)));
        return false;
    }
    const LoopFaceMethod method = MethodOf(face);
    if (method != LoopFaceMethod::FourEdge && method != LoopFaceMethod::BoundaryFill) {
        window_.SetStatus(QStringLiteral("面にする: 輪 %1 は平面なので辺の連続は付けられません。"
                                         "作り方を境界面に変えると付けられます。")
                .arg(static_cast<int>(face + 1)));
        return false;
    }
    auto& value = continuity_[face][edge];
    value = value == SurfaceContinuity::G0 ? SurfaceContinuity::G1
        : value == SurfaceContinuity::G1  ? SurfaceContinuity::G2
                                          : SurfaceContinuity::G0;
    window_.SetStatus(QStringLiteral("面にする: 輪 %1 の辺 %2 を %3 にしました。")
            .arg(static_cast<int>(face + 1)).arg(static_cast<int>(edge + 1))
            .arg(QString::fromUtf8(std::string(kachakacha::v2::modeling::SurfaceContinuityName(value)).c_str())));
    return true;
}

//! 3D で置いている点にいちばん近い、隣のある辺。無ければ偽。
bool V2LoopFacesTool::CycleContinuityAtHover()
{
    if (!plan_.has_value()) {
        return false;
    }
    const auto hover = window_.viewport_->HoverPosition();
    if (!hover.has_value()) {
        window_.SetStatus(QStringLiteral("面にする: Tab は、3D で辺の上に置いてから押します。"));
        return true;
    }
    std::optional<std::pair<std::size_t, std::size_t>> best;
    double bestMm = 0.0;
    for (std::size_t f = 0; f < plan_->faces.size(); ++f) {
        const LoopFace& face = plan_->faces[f];
        for (std::size_t e = 0; e < face.edges.size(); ++e) {
            if (!face.edges[e].neighborSurface.has_value() && !face.edges[e].neighborFace.has_value()) {
                continue;
            }
            const std::size_t piece = face.selections[e];
            if (piece >= plan_->pieces.size()) {
                continue;
            }
            for (const CurveSegment& segment : plan_->pieces[piece].segments) {
                const double d = segment.ClosestPoint(*hover).distance;
                if (!best.has_value() || d < bestMm) {
                    best = std::make_pair(f, e);
                    bestMm = d;
                }
            }
        }
    }
    if (!best.has_value()) {
        window_.SetStatus(QStringLiteral("面にする: 隣の面がある辺が無いので、連続を付ける辺がありません。"));
        return true;
    }
    if (CycleContinuity(best->first, best->second)) {
        ShowPreview();
    }
    return true;
}

std::vector<kachakacha::v2::app::LoopNeighborCurve> V2LoopFacesTool::Neighbors() const
{
    std::vector<kachakacha::v2::app::LoopNeighborCurve> curves;
    const double tol = std::max(ToleranceNow().interactiveJoinMm, 0.01) * 0.5;
    for (const auto& [key, boundary] : window_.guideEdges_) {
        const auto id = EntityId::Parse(key);
        if (!id.has_value()) {
            continue;
        }
        for (const CurveSegment& segment : boundary) {
            kachakacha::v2::app::LoopNeighborCurve curve;
            curve.surface = *id;
            curve.polyline = kachakacha::v2::geometry::SampleChain({segment}, tol);
            curves.push_back(std::move(curve));
        }
    }
    return curves;
}

std::vector<std::size_t> V2LoopFacesTool::BuildOrder() const
{
    std::vector<std::size_t> order;
    std::vector<bool> placed(plan_->faces.size(), false);
    bool progress = true;
    while (progress) {
        progress = false;
        for (std::size_t f = 0; f < plan_->faces.size(); ++f) {
            if (placed[f] || !make_[f]) {
                continue;
            }
            bool ready = true;
            const LoopFace& face = plan_->faces[f];
            for (std::size_t e = 0; e < face.edges.size() && ready; ++e) {
                const auto& other = face.edges[e].neighborFace;
                if (other.has_value() && *other < placed.size() && make_[*other] && !placed[*other]
                    && ContinuityOf(f, e) != SurfaceContinuity::G0) {
                    ready = false;   // 相手の輪を先に作る
                }
            }
            if (ready) {
                order.push_back(f);
                placed[f] = true;
                progress = true;
            }
        }
    }
    for (std::size_t f = 0; f < plan_->faces.size(); ++f) {
        if (!placed[f] && make_[f]) {
            order.push_back(f);   // 互いに相手(輪どうし)。番号順に作り、後の辺は G0 になる
        }
    }
    return order;
}

void V2LoopFacesTool::EdgeSupports(std::size_t face, const std::vector<EntityId>& builtIds,
    std::vector<SurfaceContinuity>& continuity, std::vector<EntityId>& supports) const
{
    const LoopFace& item = plan_->faces[face];
    continuity.assign(item.edges.size(), SurfaceContinuity::G0);
    supports.assign(item.edges.size(), EntityId{});
    for (std::size_t e = 0; e < item.edges.size(); ++e) {
        const SurfaceContinuity order = ContinuityOf(face, e);
        if (order == SurfaceContinuity::G0) {
            continue;
        }
        const auto& edge = item.edges[e];
        if (edge.neighborSurface.has_value()) {
            supports[e] = *edge.neighborSurface;
        } else if (edge.neighborFace.has_value() && *edge.neighborFace < builtIds.size()) {
            supports[e] = builtIds[*edge.neighborFace];   // まだ作っていなければ Nil → G0 のまま
        }
        continuity[e] = order;
    }
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
        // 辺の連続(G1/G2)は、その辺の線に札で出す。
        for (std::size_t e = 0; e < face.edges.size(); ++e) {
            const SurfaceContinuity order = ContinuityOf(at, e);
            if (order == SurfaceContinuity::G0) {
                continue;
            }
            const std::size_t piece = face.selections[e];
            const std::size_t source = piece < plan_->pieces.size() ? plan_->pieces[piece].source : piece;
            if (source < selections_.size()) {
                labels.push_back({selections_[source].sourceWireId,
                    std::string(kachakacha::v2::modeling::SurfaceContinuityName(order))});
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
        const bool allowed = current == LoopFaceMethod::FourEdge || current == LoopFaceMethod::BoundaryFill;
        for (std::size_t e = 0; e < face.edges.size(); ++e) {
            const auto& edge = face.edges[e];
            if (!edge.neighborSurface.has_value() && !edge.neighborFace.has_value()) {
                continue;
            }
            V2LoopEdgeCell cell;
            cell.edge = static_cast<int>(e);
            std::string neighborLabel;
            if (edge.neighborSurface.has_value()) {
                const auto* entity = window_.session_->GetDocument().FindEntity(*edge.neighborSurface);
                neighborLabel = entity != nullptr ? entity->displayName : std::string("?");
            }
            cell.neighborJa = Text(kachakacha::v2::app::LoopFaceEdgeTextJa(edge, neighborLabel));
            cell.continuityIndex = static_cast<int>(ContinuityOf(at, e));
            cell.allowed = allowed;
            row.edges.push_back(cell);
        }
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
    if (key == Qt::Key_Tab) {
        return CycleContinuityAtHover();
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
    Recent recent;
    for (const auto& selection : selections_) {
        recent.wireIds.push_back(selection.sourceWireId);
    }
    recent.methods = methodOverride_;
    recent.make = make_;
    recent.continuity = continuity_;
    recent.leaveGap = leaveGap_;
    recent.joinMm = joinMm_;
    recent.made = made;
    Clear();
    window_.AdoptCurrentDocument();
    window_.RefreshShapeViews();
    window_.RefreshEntityList();
    recent.revision = document.Revision();
    recent_ = std::move(recent);
    ShowRecent();
    window_.SetStatus(QStringLiteral("面にする: %1 枚作りました(%2)。元の線は残しています。%3")
            .arg(made)
            .arg(summary)
            .arg(unused.isEmpty() ? QString() : QStringLiteral("使わなかった線: %1。").arg(unused)));
    return true;
}

//! 作った直後の棚: 「直前: 面にする(n 枚)」と [開いて直す]。別の道具を持つと消える。
void V2LoopFacesTool::ShowRecent()
{
    if (!recent_.has_value()) {
        dock_->HideRecent();
        return;
    }
    dock_->ShowRecent(QStringLiteral("直前: 面にする(%1 枚)").arg(recent_->made));
    window_.RefreshRightShelves();
}

void V2LoopFacesTool::End()
{
    Clear();
    if (recent_.has_value()) {
        recent_.reset();
        dock_->HideRecent();
        window_.RefreshRightShelves();
    }
}

bool V2LoopFacesTool::ReopenRecent()
{
    if (!recent_.has_value()) {
        return false;
    }
    const Recent recent = *recent_;
    if (window_.session_->GetDocument().Revision() != recent.revision) {
        recent_.reset();
        dock_->HideRecent();
        window_.RefreshRightShelves();
        window_.SetStatus(QStringLiteral("面にする: そのあとに別の変更があるので、直前の操作は開けません。"
                                         "元に戻す(Ctrl+Z)で戻ってから、線を選んで面にし直してください。"));
        return false;
    }
    // 作ったものを 1 回の取り消しで戻し、同じ線で構え直す(選んだ作り方・作るか・連続・そのまま も戻す)。
    window_.RunCommand("edit.undo");
    kachakacha::v2::app::SelectionSet same;
    same.entityIds = recent.wireIds;
    window_.viewport_->SetSelection(same);
    Start();
    if (!plan_.has_value()) {
        return false;
    }
    joinMm_ = recent.joinMm;
    if (recent.methods.size() == methodOverride_.size()) {
        methodOverride_ = recent.methods;
        make_ = recent.make;
        continuity_ = recent.continuity;
    }
    if (recent.leaveGap.size() == leaveGap_.size()) {
        leaveGap_ = recent.leaveGap;
    }
    if (joinMm_.has_value() && !Replan()) {
        return false;
    }
    ShowPreview();
    window_.SetStatus(QStringLiteral("面にする: 直前の操作を開きました(作ったものは戻しました)。"
                                     "作り方・連続・許容を変えて Enter で作り直します。Esc でやめます。"));
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
    std::vector<EntityId> builtIds(plan_->faces.size());
    for (const std::size_t at : BuildOrder()) {
        LoopFace face = plan_->faces[at];
        face.method = MethodOf(at);
        std::vector<SurfaceContinuity> continuity;
        std::vector<EntityId> supports;
        EdgeSupports(at, builtIds, continuity, supports);
        const auto table = kachakacha::v2::app::LoopFaceTable(selections_, face, tolerance,
            continuity, supports);
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
        bool smooth = false;
        for (const auto& row : table.Value().rows) {
            smooth = smooth || row.continuity != SurfaceContinuity::G0;
        }
        const std::string label = MethodLabel(face.method) + (smooth ? "(辺の連続あり)" : "");
        const EntityId id = window_.AdoptGuideSurface(table.Value(), *built, inputs, label);
        if (id.IsNil()) {
            return 0;
        }
        builtIds[at] = id;
        ++made;
    }
    return made;
}
