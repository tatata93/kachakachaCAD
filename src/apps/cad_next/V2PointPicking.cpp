#include "V2PointPicking.h"
#include "V2Viewport.h"
#include "V2ToolSelection.h"
#include <algorithm>
#include <QString>
#include <QPointF>
#include <utility>

namespace v2ui {
kachakacha::v2::app::HoverResult ReferencePointAt(const V2Viewport& view,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance,
    const QPointF& position, bool suppressed,
    std::optional<kachakacha::v2::app::PickCandidate>* curve)
{
    using namespace kachakacha::v2;
    modeling::SnapScene visible=scene;
    std::erase_if(visible.curves,[&](const auto& item){return !view.EntityShown(item.entityId);});
    std::erase_if(visible.points,[&](const auto& item){return !view.EntityShown(item.entityId);});
    modeling::SnapSettings settings;settings.suppressed=suppressed;
    const geometry::ScreenPoint pointer{position.x(),position.y()};
    app::HoverResult result;
    result.snap=modeling::ChooseSnap(modeling::CollectSnapCandidates(
        visible,view.Mapping(),pointer,settings,tolerance),settings);
    result.position=result.snap?std::optional{result.snap->position}:
        view.Mapping().UnprojectOntoPlane(pointer,view.WorkPlane().origin,view.WorkPlane().normal);
    if(curve)*curve=app::PickCurve(visible,view.Mapping(),pointer,tolerance);
    return result;
}
}

void V2Viewport::BeginPointPick(std::function<void(const PickedPoint&)> handler,
    const std::string& promptJa)
{
    if(V2ToolSelection::Active(*this))V2ToolSelection::End(*this);
    CancelReferencePick();
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
    if(!pickHandler_)return;
    pickHandler_=nullptr;
    setProperty("inputPickGeneration",property("inputPickGeneration").toULongLong()+1);
    setProperty("pointPickPrompt",{});
    status_="点の指定を取り消しました。";
    if(statusCallback_)statusCallback_(status_);
    RefreshCursorShape();update();
}
