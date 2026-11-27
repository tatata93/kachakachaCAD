#include "V2PointPicking.h"
#include "V2Viewport.h"
#include "V2ToolSelection.h"
#include "V2PointTarget.h"
#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QPen>
#include <QString>
#include <QPointF>
#include <utility>

namespace v2ui {
kachakacha::v2::app::HoverResult ReferencePointAt(const V2Viewport& view,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance,
    const QPointF& position, bool suppressed,
    std::optional<kachakacha::v2::app::PickCandidate>* curve, bool useGrid)
{
    using namespace kachakacha::v2;
    modeling::SnapScene visible=scene;
    if (!useGrid) visible.grid.visible = false;
    std::erase_if(visible.curves,[&](const auto& item){return !view.EntityShown(item.entityId);});
    std::erase_if(visible.points,[&](const auto& item){return !view.EntityShown(item.entityId);});
    modeling::SnapSettings settings;settings.suppressed=suppressed;
    const geometry::ScreenPoint pointer{position.x(),position.y()};
    app::HoverResult result;
    const auto target=suppressed?std::nullopt:view.ExplicitPointTarget();
    auto candidates=modeling::CollectSnapCandidates(visible,view.Mapping(),pointer,settings,tolerance);
    if (target) std::erase_if(candidates,[&](const auto& candidate){return !SnapMatchesPointTarget(*target,candidate);});
    result.snap=modeling::ChooseSnap(candidates,settings);
    result.position=result.snap?std::optional{result.snap->position}:
        target?std::nullopt:view.Mapping().UnprojectOntoPlane(pointer,view.WorkPlane().origin,view.WorkPlane().normal);
    if (curve) {
        *curve=std::nullopt;
        const auto entity=target?target->entityId:result.snap?result.snap->entityId:base::EntityId{};
        auto segment=target?target->segmentId:result.snap?result.snap->segmentId:base::SegmentId{};
        if (segment.IsNil() && result.snap) {
            if (result.snap->entityId==entity) segment=result.snap->segmentId;
            else if (result.snap->otherEntityId==entity) segment=result.snap->otherSegmentId;
        }
        if (result.position) for (const auto& source:visible.curves) {
            if (source.entityId!=entity || (!segment.IsNil() && source.segmentId!=segment)) continue;
            app::PickCandidate picked;
            picked.entityId=source.entityId; picked.segmentId=source.segmentId;
            picked.kind=app::SelectionElementKind::Edge; picked.hitPoint=*result.position;
            picked.curveParameter=source.segment.ClosestPoint(*result.position).parameter;
            *curve=picked;break;
        }
        if (!*curve && !target) *curve=app::PickCurve(visible,view.Mapping(),pointer,tolerance);
    }
    return result;
}
}

void V2Viewport::BeginPointPick(std::function<void(const PickedPoint&)> handler,
    const std::string& promptJa)
{
    if(V2ToolSelection::Active(*this))V2ToolSelection::End(*this);
    CancelReferencePick();
    directionPick_.reset();
    pickHandler_=std::move(handler);
    setProperty("inputPickGeneration",property("inputPickGeneration").toULongLong()+1);
    setProperty("pointPickPrompt",QString::fromStdString(promptJa));
    ForgetPickCycle();hoveredProfileRegion_.reset();
    status_=promptJa;
    if(statusCallback_)statusCallback_(status_);
    RefreshCursorShape();update();
}

void V2Viewport::CancelPointPick()
{
    if(!PickPending())return;
    pickHandler_=nullptr;
    ForgetPickCycle();
    directionPick_.reset();
    setProperty("inputPickGeneration",property("inputPickGeneration").toULongLong()+1);
    setProperty("pointPickPrompt",{});
    status_="点の指定を取り消しました。";
    if(statusCallback_)statusCallback_(status_);
    RefreshCursorShape();update();
}

void V2Viewport::BeginDirectionPick(DirectionPickPlane plane, std::string label,
    std::function<void(const kachakacha::v2::geometry::Vector3&)> handler)
{
    if (V2ToolSelection::Active(*this)) V2ToolSelection::End(*this);
    CancelReferencePick();
    CancelPointPick();
    directionPick_ = DirectionPick{plane, std::move(label), std::move(handler), std::nullopt};
    setProperty("inputPickGeneration", property("inputPickGeneration").toULongLong() + 1);
    ForgetPickCycle();
    hoveredProfileRegion_.reset();
    setFocus(Qt::OtherFocusReason);
    UpdateDirectionPrompt();
    HoverAt(cursorPosition_);
}

void V2Viewport::UpdateDirectionPrompt()
{
    status_ = directionPick_->label + (directionPick_->first
        ? "終点を3Dビューか左ペインで指定してください。Escで取消。"
        : "始点を3Dビューか左ペインで指定してください。Escで取消。");
    setProperty("pointPickPrompt", QString::fromStdString(status_));
}

kachakacha::v2::app::HoverResult V2Viewport::DirectionPointAt(const QPointF& position) const
{
    if (directionPick_->plane == DirectionPickPlane::WorkPlane) {
        return v2ui::ReferencePointAt(*this, session_->Scene(),
            session_->GetDocument().Snapshot().settings.tolerance, position,
            snapSuppressedByKey_ || snapSuppressedBySetting_, nullptr, false);
    }
    kachakacha::v2::app::HoverResult hit;
    hit.position = mapping_.UnprojectOntoPlane({position.x(), position.y()}, center_,
        kachakacha::v2::view::ForwardOf(orientation_));
    return hit;
}

void V2Viewport::ClickDirectionPoint(const QPointF& position)
{
    AcceptDirectionPoint(DirectionPointAt(position).position, position);
}

void V2Viewport::AcceptListedDirectionPoint(const std::optional<kachakacha::v2::geometry::Vector3>& point)
{
    const auto screen = point ? ToScreen(*point) : std::nullopt;
    if (!screen) {
        status_ = directionPick_->label + "作図点か、左ペインの「部分を指定」から端点・交点を選んでください。";
        if (statusCallback_) statusCallback_(status_);
        update();
        return;
    }
    // For screen alignment, use the same view plane as a click at this pixel.
    // For grid alignment, the shared acceptance code projects the exact delta.
    AcceptDirectionPoint(directionPick_->plane == DirectionPickPlane::Screen
        ? DirectionPointAt(*screen).position : point, *screen);
}

void V2Viewport::AcceptDirectionPoint(const std::optional<kachakacha::v2::geometry::Vector3>& point,
    const QPointF& position)
{
    using kachakacha::v2::geometry::Dot;
    auto& pick = *directionPick_;
    if (point && !pick.first) {
        pick.first = point;
        UpdateDirectionPrompt();
        HoverAt(position);
        return;
    }
    if (point && pick.first) {
        const auto delta = *point - *pick.first;
        const auto normal = pick.plane == DirectionPickPlane::WorkPlane ? workPlane_.normal
            : kachakacha::v2::view::ForwardOf(orientation_);
        const auto direction = delta - normal * Dot(delta, normal);
        const auto from = ToScreen(*pick.first);
        const auto to = ToScreen(*point);
        if (from && to && std::hypot(to->x() - from->x(), to->y() - from->y()) >= 2.0
            && direction.Length() > std::max(1.0, delta.Length()) * 1.0e-9) {
            auto handler = std::move(pick.handler);
            directionPick_.reset();
            setProperty("inputPickGeneration", property("inputPickGeneration").toULongLong() + 1);
            setProperty("pointPickPrompt", {});
            DiscardHoverState();
            handler(direction);
            RefreshCursorShape();
            update();
            return;
        }
    }
    status_ = pick.label + (point ? "始点と離れた終点を指定してください。"
        : "作業面が見える方向へ回すか、端点・交点を指定してください。");
    if (statusCallback_) statusCallback_(status_);
    update();
}

void V2Viewport::DrawDirectionGuide(QPainter& painter) const
{
    if (!directionPick_ || !directionPick_->first) return;
    const auto start = ToScreen(*directionPick_->first);
    if (!start) return;
    const auto end = hover_.position ? ToScreen(*hover_.position).value_or(cursorPosition_) : cursorPosition_;
    painter.save();
    painter.setPen(QPen(palette_.background, 5.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(*start, end);
    painter.setPen(QPen(palette_.selected, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(*start, end);
    painter.setBrush(palette_.selected);
    painter.drawEllipse(*start, 4.0, 4.0);
    painter.restore();
}
