#include "kachakacha/app/DrawingSession.h"

#include <algorithm>

namespace kachakacha::v2::app {

using base::EntityId;
using base::FeatureId;
using base::IdKind;
using base::SegmentId;
using document::AddFeatureCommand;
using domain::CreatePointDefinition;
using domain::CreateWireDefinition;
using domain::Entity;
using domain::EntityKind;
using domain::Feature;
using domain::FeatureOutput;
using domain::FeatureType;
using modeling::SnapCurve;
using modeling::SnapDrawingPoint;
using modeling::ToolOutput;
using modeling::ToolSession;

namespace {

//! ツールから、作られるものの種類を決める。
[[nodiscard]] EntityKind KindFor(DrawingTool tool)
{
    switch (tool) {
    case DrawingTool::Point:
    case DrawingTool::SetGridOrigin:
        return EntityKind::Point;
    default:
        return EntityKind::Wire;
    }
}

[[nodiscard]] FeatureType FeatureTypeFor(DrawingTool tool)
{
    switch (tool) {
    case DrawingTool::Point:
    case DrawingTool::SetGridOrigin:
        return FeatureType::CreatePoint;
    default:
        return FeatureType::CreateWire;
    }
}

} // namespace

DrawingSession::DrawingSession(base::DocumentId documentId, base::IdGenerator& ids)
    : document_(documentId), ids_(&ids)
{
    session_ = std::make_unique<ToolSession>(tool_, toolSettings_,
        document_.Snapshot().settings.tolerance);
}

void DrawingSession::SelectTool(DrawingTool tool)
{
    // ツールを変えたら途中の点は捨てる。持ち越すと、前のツールの点が混ざる。
    tool_ = tool;
    session_ = std::make_unique<ToolSession>(tool_, toolSettings_,
        document_.Snapshot().settings.tolerance);
}

void DrawingSession::SetToolSettings(ToolSettings settings)
{
    toolSettings_ = std::move(settings);
    session_ = std::make_unique<ToolSession>(tool_, toolSettings_,
        document_.Snapshot().settings.tolerance);
}

HoverResult DrawingSession::Hover(const ScreenPoint& pointer)
{
    HoverResult result;
    SnapSettings settings = snapSettings_;
    // 直前に置いた点があれば、接点と垂足の基準にする。
    if (!session_->Points().empty()) {
        settings.referencePoint = session_->Points().back();
    }
    const auto candidates = modeling::CollectSnapCandidates(scene_, mapping_, pointer,
        settings, document_.Snapshot().settings.tolerance);
    result.snap = modeling::ChooseSnap(candidates, settings);
    if (result.snap.has_value()) {
        result.position = result.snap->position;
    } else if (scene_.workPlane.active) {
        result.position = mapping_.UnprojectOntoPlane(pointer, scene_.workPlane.origin,
            scene_.workPlane.normal);
    }
    if (result.position.has_value() && adjustPoint_) {
        // 吸着したあとに寄せる。先に寄せると、寄せた先へまた吸着して元へ戻る。
        result.position = adjustPoint_(*result.position);
    }
    if (result.position.has_value()) {
        result.preview = session_->Preview(*result.position);
    }
    result.messageJa = session_->Prompt().messageJa;
    if (result.snap.has_value()) {
        result.messageJa += "  [" + std::string(modeling::SnapKindLabelJa(result.snap->kind))
            + "]";
    } else if (snapSettings_.suppressed) {
        result.messageJa += "  [スナップなし]";
    }
    return result;
}

ClickResult DrawingSession::Click(const ScreenPoint& pointer)
{
    ClickResult result;
    const HoverResult hover = Hover(pointer);
    if (!hover.position.has_value()) {
        result.diagnostics.push_back(base::MakeError("UI-S001",
            "その場所では点を置けません。",
            "作業平面が選ばれていないか、視線が平面と平行です。"));
        return result;
    }
    auto placed = session_->AddPoint(*hover.position);
    if (!placed.HasValue()) {
        result.diagnostics = placed.Diagnostics();
        return result;
    }
    result.placedPoint = true;
    if (!placed.Value().has_value()) {
        return result;   // まだ確定していない
    }
    return Commit(*placed.Value());
}

ClickResult DrawingSession::FinishTool()
{
    ClickResult result;
    auto finished = session_->Finish();
    if (!finished.HasValue()) {
        result.diagnostics = finished.Diagnostics();
        return result;
    }
    return Commit(finished.Value());
}

std::size_t DrawingSession::PlacedPointCount() const noexcept
{
    return session_ == nullptr ? 0 : session_->Points().size();
}

geometry::Vector3 DrawingSession::ConstraintAnchor() const noexcept
{
    if (session_ == nullptr || session_->Points().empty()) {
        return geometry::Vector3{};
    }
    const bool fromLast = tool_ == DrawingTool::Polyline || tool_ == DrawingTool::Spline;
    return fromLast ? session_->Points().back() : session_->Points().front();
}

void DrawingSession::CancelTool()
{
    session_->Cancel();
}

bool DrawingSession::UndoLastPoint()
{
    return session_->UndoLastPoint();
}

ClickResult DrawingSession::Commit(const ToolOutput& output)
{
    ClickResult result;
    result.placedPoint = true;
    if (output.segments.empty() && output.points.empty()) {
        // 変換ツールなど、点を集めるだけのもの。文書は変わらない。
        result.commandLabel = std::string(modeling::DrawingToolNameJa(tool_));
        return result;
    }

    Feature feature;
    feature.id = ids_->NextTyped<IdKind::Feature>();
    feature.type = FeatureTypeFor(tool_);
    feature.displayName = std::string(modeling::DrawingToolNameJa(tool_));

    Entity entity;
    entity.id = ids_->NextTyped<IdKind::Entity>();
    entity.kind = KindFor(tool_);
    entity.displayName = feature.displayName;
    entity.createdBy = feature.id;
    entity.construction = output.construction;

    if (!output.points.empty()) {
        CreatePointDefinition definition;
        definition.positionMm = output.points.front();
        definition.xExpression = {"", definition.positionMm.x,
            geometry::QuantityKind::Length};
        definition.yExpression = {"", definition.positionMm.y,
            geometry::QuantityKind::Length};
        definition.zExpression = {"", definition.positionMm.z,
            geometry::QuantityKind::Length};
        feature.definition = std::move(definition);
        feature.outputs.push_back(FeatureOutput{"point", entity.id, EntityKind::Point});
    } else {
        CreateWireDefinition definition;
        definition.segments = output.segments;
        for (std::size_t index = 0; index < output.segments.size(); ++index) {
            definition.segmentIds.push_back(ids_->NextTyped<IdKind::Segment>());
        }
        definition.construction = output.construction;
        feature.definition = std::move(definition);
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
    }

    const EntityId createdId = entity.id;
    const auto commandResult = document_.Run(
        AddFeatureCommand(feature, {entity}, feature.displayName));
    result.committed = commandResult.committed;
    result.diagnostics = commandResult.diagnostics;
    result.commandLabel = commandResult.delta.label;
    if (commandResult.committed) {
        result.createdEntityIds.push_back(createdId);
        AddToScene(output, createdId);
    }
    return result;
}

void DrawingSession::AddToScene(const ToolOutput& output, EntityId entityId)
{
    // 作ったものは、すぐ次のスナップの相手になる。
    // V1はここが繋がっていなかったので、引いたばかりの線の端点へ吸着できなかった。
    for (const Vector3& point : output.points) {
        scene_.points.push_back(SnapDrawingPoint{entityId, point});
    }
    for (const geometry::CurveSegment& segment : output.segments) {
        scene_.curves.push_back(SnapCurve{entityId, ids_->NextTyped<IdKind::Segment>(),
            segment, output.construction});
    }
}

} // namespace kachakacha::v2::app
