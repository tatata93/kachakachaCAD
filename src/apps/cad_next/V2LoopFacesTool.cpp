//! 線から面。見出しは V2LoopFacesTool.h。

#include "V2LoopFacesTool.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Feature.h"

#include <QString>
#include <Qt>

#include <string>
#include <vector>

using kachakacha::v2::app::LoopFace;
using kachakacha::v2::app::LoopFaceMethod;
using kachakacha::v2::app::LoopGap;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

constexpr const char* kLabelJa = "線から面";

} // namespace

V2LoopFacesTool::V2LoopFacesTool(V2MainWindow& window) : window_(window) {}

void V2LoopFacesTool::Start()
{
    Clear();
    selections_.clear();
    for (const EntityId& id : window_.viewport_->Selection().entityIds) {
        const auto chosen = kachakacha::v2::app::GuideSelectionOf(
            window_.session_->GetDocument(), window_.session_->Scene(), id);
        if (chosen.has_value()) {
            selections_.push_back(*chosen);
        }
    }
    if (selections_.empty()) {
        window_.SetStatus(QStringLiteral("線から面: 先に線を選んでください。"));
        return;
    }
    if (!Replan()) {
        return;
    }
    ShowPreview();
}

bool V2LoopFacesTool::Replan()
{
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    auto planned = kachakacha::v2::app::PlanLoopFaces(selections_, tolerance);
    if (!planned.HasValue()) {
        plan_.reset();
        window_.ReportDiagnostics(planned.Diagnostics());
        return false;
    }
    plan_ = planned.Value();
    return true;
}

//! 輪は実線の下見、ずれは赤系の破線と × 印。一番下の一行に内訳、案内に次の手。
void V2LoopFacesTool::ShowPreview()
{
    if (!plan_.has_value()) {
        return;
    }
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines;
    for (const LoopFace& face : plan_->faces) {
        auto ring = face.ring;
        if (!ring.empty()) {
            ring.push_back(ring.front());
        }
        lines.push_back(std::move(ring));
    }
    window_.viewport_->ShowToolPreview(lines);
    V2Viewport::EditPreview gaps;
    gaps.removing = true;
    for (const LoopGap& gap : plan_->gaps) {
        const auto& first = selections_[gap.firstSelection].segments;
        const auto& second = selections_[gap.secondSelection].segments;
        const auto a = gap.firstAtEnd ? first.back().EndPoint() : first.front().StartPoint();
        const auto b = gap.secondAtEnd ? second.back().EndPoint() : second.front().StartPoint();
        gaps.lines.push_back({a, b});
        gaps.markers.push_back(a);
        gaps.markers.push_back(b);
    }
    if (gaps.lines.empty()) {
        window_.viewport_->HideEditPreview();
    } else {
        window_.viewport_->ShowEditPreview(std::move(gaps));
    }
    window_.ShowToolFooter(QStringLiteral("線から面: %1 / Preview only").arg(Text(plan_->summaryJa)));
    if (!plan_->gaps.empty()) {
        bool movable = false;
        QString where;
        for (const LoopGap& gap : plan_->gaps) {
            movable = movable || gap.movable;
            if (!where.isEmpty()) {
                where += QStringLiteral("、");
            }
            where += Text(kachakacha::v2::app::LoopGapTextJa(selections_, gap));
        }
        window_.SetStatus(movable
                ? QStringLiteral("線から面: %1。Enter で直線の端を寄せてから面を作ります(元の線は残ります)。"
                                 "Esc でやめます(寄せません)。")
                      .arg(where)
                : QStringLiteral("線から面: %1。寄せられる直線が無いので、線を引き直してください。"
                                 "Enter で閉じている輪だけ作ります。Esc でやめます。")
                      .arg(where));
        return;
    }
    window_.SetStatus(QStringLiteral("線から面: %1 の輪が見つかりました(%2)。"
                                     "Enter で作ります(元の線は残ります)。Esc でやめます。")
            .arg(static_cast<int>(plan_->faces.size()))
            .arg(Text(plan_->summaryJa)));
}

bool V2LoopFacesTool::HandleKey(int key)
{
    if (!plan_.has_value()) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        Clear();
        window_.SetStatus(QStringLiteral("線から面: やめました。何も変えていません。"));
        return true;
    }
    if (key != Qt::Key_Return && key != Qt::Key_Enter) {
        return false;
    }
    auto& document = window_.session_->GetDocument();
    kachakacha::v2::document::Document::Transaction transaction(document, kLabelJa);
    bool movable = false;
    for (const LoopGap& gap : plan_->gaps) {
        movable = movable || gap.movable;
    }
    if (movable) {
        if (!CloseGaps() || !Replan()) {
            // 理由は出した。Transaction が捨てるので、寄せた線も元へ戻る。構えは解く。
            Clear();
            window_.AdoptCurrentDocument();
            return true;
        }
    }
    if (plan_->faces.empty()) {
        window_.SetStatus(QStringLiteral("線から面: 閉じた輪が無いので、面は作りません。"));
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
        unused += (unused.isEmpty() ? QStringLiteral("") : QStringLiteral("、"))
            + Text(selections_[index].label);
    }
    if (!transaction.Commit()) {
        window_.SetStatus(QStringLiteral("線から面: 途中で失敗したので、何も変えていません。"));
        Clear();
        window_.AdoptCurrentDocument();
        return true;
    }
    Clear();
    window_.AdoptCurrentDocument();
    window_.RefreshShapeViews();
    window_.RefreshEntityList();
    window_.SetStatus(QStringLiteral("線から面: %1 枚作りました(%2)。元の線は残しています。%3")
            .arg(made)
            .arg(summary)
            .arg(unused.isEmpty() ? QString()
                                  : QStringLiteral("使わなかった線: %1。").arg(unused)));
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
    }
    window_.ShowToolFooter(QString());
}

//! 寄せた線を新しいワイヤーとして入れ、元の線を消す(名前は元のまま)。
EntityId V2LoopFacesTool::ReplaceWire(std::size_t selection,
    const kachakacha::v2::geometry::CurveSegment& segment, const std::string& label)
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
        return EntityId{};
    }
    Feature feature;
    feature.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::TransformWire;
    feature.displayName = label;
    // 元の線は入力にしない(形をそのまま持つので、元の線は消してよい。隠れた線を残さない)。
    kachakacha::v2::domain::CreateWireDefinition wire;
    wire.segments = {segment};
    wire.construction = source->construction;
    wire.segmentIds.push_back(window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
    feature.definition = std::move(wire);
    Entity entity;
    entity.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Wire;
    entity.displayName = source->displayName;
    entity.groupId = source->groupId;
    entity.datum = source->datum;
    entity.construction = source->construction;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
    const auto added = document.Run(AddFeatureCommand(feature, {entity}, label));
    if (!added.committed) {
        window_.ReportDiagnostics(added.diagnostics);
        return EntityId{};
    }
    window_.RemoveConsumedWires({sourceId});
    selections_[selection].sourceWireId = entity.id;
    selections_[selection].segments = {segment};
    return entity.id;
}

bool V2LoopFacesTool::CloseGaps()
{
    const std::vector<LoopGap> gaps = plan_->gaps;
    int closed = 0;
    for (const LoopGap& gap : gaps) {
        if (!gap.movable) {
            continue;
        }
        const auto fix = kachakacha::v2::app::CloseLoopGap(selections_, gap);
        if (!fix.HasValue()) {
            window_.ReportDiagnostics(fix.Diagnostics());
            return false;
        }
        const std::string label = std::string(kLabelJa) + "(端を寄せる)";
        if (fix.Value().first.has_value()
            && ReplaceWire(gap.firstSelection, *fix.Value().first, label).IsNil()) {
            return false;
        }
        if (fix.Value().second.has_value()
            && ReplaceWire(gap.secondSelection, *fix.Value().second, label).IsNil()) {
            return false;
        }
        ++closed;
    }
    return closed > 0;
}

int V2LoopFacesTool::BuildFaces()
{
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    int made = 0;
    for (const LoopFace& face : plan_->faces) {
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
        const std::string label = face.method == LoopFaceMethod::Planar ? "平面"
            : (face.method == LoopFaceMethod::FourEdge ? "四辺面" : "境界面");
        if (window_.AdoptGuideSurface(table.Value(), *built, inputs, label).IsNil()) {
            return 0;
        }
        ++made;
    }
    return made;
}
