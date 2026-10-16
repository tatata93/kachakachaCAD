#include "V2Viewport.h"

#include <QBrush>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>

using kachakacha::v2::app::ApplySelection;
using kachakacha::v2::app::IsSelected;
using kachakacha::v2::app::PickCandidate;
using kachakacha::v2::app::ProfileRegionEntityIds;
using kachakacha::v2::app::SelectionElementKind;
using kachakacha::v2::app::SelectionMode;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::ScreenPoint;

namespace {

double DistanceToSegment(const QPointF& point, const QPointF& first, const QPointF& second)
{
    const QPointF delta = second - first;
    const double lengthSquared = delta.x() * delta.x() + delta.y() * delta.y();
    if (lengthSquared <= 1.0e-12) {
        return std::hypot(point.x() - first.x(), point.y() - first.y());
    }
    const QPointF offset = point - first;
    const double parameter = std::clamp(
        (offset.x() * delta.x() + offset.y() * delta.y()) / lengthSquared, 0.0, 1.0);
    const QPointF closest = first + delta * parameter;
    return std::hypot(point.x() - closest.x(), point.y() - closest.y());
}

bool BoundaryNearScreen(const kachakacha::v2::app::ProfileBoundary& boundary,
    const kachakacha::v2::geometry::ScreenMapping& mapping, const QPointF& pointer,
    double tolerancePx)
{
    if (boundary.sampled.size() < 2) {
        return false;
    }
    for (std::size_t index = 0; index < boundary.sampled.size(); ++index) {
        const auto first = mapping.Project(boundary.sampled[index]);
        const auto second = mapping.Project(boundary.sampled[(index + 1)
            % boundary.sampled.size()]);
        if (first.has_value() && second.has_value()
            && DistanceToSegment(pointer, {first->x, first->y}, {second->x, second->y})
                <= tolerancePx) {
            return true;
        }
    }
    return false;
}

void AddBoundary(QPainterPath& path, const V2Viewport& viewport,
    const kachakacha::v2::app::ProfileBoundary& boundary)
{
    QPolygonF polygon;
    polygon.reserve(static_cast<int>(boundary.sampled.size()));
    for (const auto& point : boundary.sampled) {
        const auto screen = viewport.Mapping().Project(point);
        if (screen.has_value()) {
            polygon.push_back(QPointF(screen->x, screen->y));
        }
    }
    if (polygon.size() >= 3) {
        path.addPolygon(polygon);
        path.closeSubpath();
    }
}

} // namespace

void V2Viewport::SetProfileRegionPicking(bool active, bool wiresFirst)
{
    profileRegionWiresFirst_ = active && wiresFirst;
    if (profileRegionPicking_ == active) {
        return;
    }
    profileRegionPicking_ = active;
    RefreshFaceRolePieces();
    hoveredProfileRegion_.reset();
    if (active) {
        RebuildProfileRegions();
    } else {
        profileRegions_.clear();
    }
    update();
}

void V2Viewport::RebuildProfileRegions()
{
    profileRegions_.clear();
    hoveredProfileRegion_.reset();
    if (!profileRegionPicking_ || session_ == nullptr) {
        return;
    }
    auto wires = session_->Scene();
    wires.curves.erase(std::remove_if(wires.curves.begin(), wires.curves.end(), [&](const auto& c) {
        const auto* entity = session_->GetDocument().FindEntity(c.entityId);
        return !entity || entity->kind != kachakacha::v2::domain::EntityKind::Wire;
    }), wires.curves.end());
    profileRegions_ = kachakacha::v2::app::DetectProfileRegions(wires,
        session_->GetDocument().Snapshot().settings.tolerance);
    if(!profileRegionWiresFirst_) {
        const auto tolerance=session_->GetDocument().Snapshot().settings.tolerance;
        const auto boundaries=kachakacha::v2::app::DetectSpatialBoundaries(wires,tolerance);
        for(auto boundary:boundaries) {
            const auto plane=kachakacha::v2::geometry::FitPlane(boundary.sampled);
            if(!plane.valid || plane.maximumDeviationMm<=tolerance.modelLinearMm)continue;
            kachakacha::v2::app::ProfileRegion region;
            region.plane=plane;region.frame=kachakacha::v2::geometry::MakeFrame(plane);
            boundary.planar=kachakacha::v2::geometry::ProjectToFrame(boundary.sampled,region.frame);
            region.outer=std::move(boundary);profileRegions_.push_back(std::move(region));
        }
    }
}

std::optional<std::size_t> V2Viewport::ProfileRegionAt(const QPointF& position) const
{
    if (!profileRegionPicking_ || profileRegions_.empty()) {
        return std::nullopt;
    }
    // 面を作る: 線の上を押したら、その線 1 本だけを入れる/外す。縁の線を押すたびに
    // 輪郭がまとめて入ったり外れたりすると、線を 1 本ずつ選べない(おまかせで起きた)。
    if (profileRegionWiresFirst_ && WireUnderCursor(position)) {
        return std::nullopt;
    }
    const auto ray = mapping_.RayThrough(ScreenPoint{position.x(), position.y()});
    if (!ray.has_value()) {
        return std::nullopt;
    }
    const double tolerance = session_->GetDocument().Snapshot().settings.tolerance
        .interactiveJoinMm;
    const double edgePickPx = session_->GetDocument().Snapshot().settings.tolerance.edgePickPx;
    double nearest = std::numeric_limits<double>::infinity();
    double smallestSpatialArea=std::numeric_limits<double>::infinity();
    const auto shape=PickShapeAt(position);
    const auto* shapeEntity=shape?session_->GetDocument().FindEntity(shape->entityId):nullptr;
    const bool solidFirst=shape.has_value()&&!SelectionHasPart();
    std::optional<std::size_t> result;
    for (std::size_t index = 0; index < profileRegions_.size(); ++index) {
        const auto& region = profileRegions_[index];
        const auto ids = ProfileRegionEntityIds(region);
        if (std::any_of(ids.begin(),ids.end(),[this](auto id){return !EntityShown(id);})) continue;
        const auto point = mapping_.UnprojectOntoPlane(ScreenPoint{position.x(), position.y()},
            region.plane.origin, region.plane.normal);
        const bool curved=region.plane.maximumDeviationMm>session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm;
        // Ordinary solid selection and planar face push/pull retain their priority.
        // An explicit profile slot, a spatial contour, or a guide surface can use its interior.
        if(solidFirst && !(toolPickActive_&&pickSlot_==kachakacha::v2::app::ExtrudeSlot::Profile
            &&(curved||property("explicitProfilePicking").toBool()
                ||(shapeEntity&&shapeEntity->kind==kachakacha::v2::domain::EntityKind::GuideSurface))))continue;
        if (!point.has_value() && !curved) {
            continue;
        }
        QPainterPath spatial;if(curved)AddBoundary(spatial,*this,region.outer);
        bool taken = (curved ? spatial.contains(position) : kachakacha::v2::app::ProfileRegionContains(region, *point, tolerance))
            || BoundaryNearScreen(region.outer, mapping_, position, edgePickPx);
        for (const auto& hole : region.holes) {
            taken = taken || BoundaryNearScreen(hole, mapping_, position, edgePickPx);
        }
        if (!taken) {
            continue;
        }
        // A spatial loop has no picking plane. A least-squares plane can be
        // parallel to the view even when its projected interior is visible.
        const double distance = Dot((curved ? region.plane.origin : *point) - ray->origin, ray->direction);
        const auto polygon=curved?spatial.toFillPolygon():QPolygonF{};double area=0;
        for(int i=0;i<polygon.size();++i)area+=polygon[i].x()*polygon[(i+1)%polygon.size()].y()-polygon[i].y()*polygon[(i+1)%polygon.size()].x();
        area=std::abs(area);
        const bool previousSpatial=result&&profileRegions_[*result].plane.maximumDeviationMm>session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm;
        if (distance >= 0.0 && ((curved&&previousSpatial)?area<smallestSpatialArea:distance<nearest)) {
            nearest = distance;
            smallestSpatialArea=area;
            result = index;
        }
    }
    return result;
}

bool V2Viewport::ProfileRegionSelected(std::size_t index) const
{
    if (index >= profileRegions_.size()) {
        return false;
    }
    const auto ids = ProfileRegionEntityIds(profileRegions_[index]);
    const auto seeds = kachakacha::v2::app::SelectedProfileSeeds(selection_);
    if (!seeds.empty() && profileRegions_[index].plane.maximumDeviationMm <= 1e-6) {
        return std::any_of(seeds.begin(), seeds.end(), [&](const auto& seed) {
            return kachakacha::v2::app::ProfileRegionContains(profileRegions_[index], seed, 1e-6);
        });
    }
    return !ids.empty() && std::all_of(ids.begin(), ids.end(), [this](const auto& id) {
        return IsSelected(selection_, id);
    });
}

bool V2Viewport::WireUnderCursor(const QPointF& position) const
{
    const auto candidates = kachakacha::v2::app::CollectPickCandidates(session_->Scene(), mapping_,
        ScreenPoint{position.x(), position.y()},
        session_->GetDocument().Snapshot().settings.tolerance, PickFocusNow());
    return std::any_of(candidates.begin(),candidates.end(),[this](const auto& c){return EntityShown(c.entityId);});
}

bool V2Viewport::SelectionHasPart() const
{
    const auto& document = session_->GetDocument();
    return std::any_of(selection_.entityIds.begin(), selection_.entityIds.end(),
        [&document](const auto& id) {
            const auto* entity = document.FindEntity(id);
            return entity != nullptr
                && entity->kind == kachakacha::v2::domain::EntityKind::Part;
        });
}

bool V2Viewport::ToggleProfileRegionAt(const QPointF& position)
{
    if (toolPickActive_ && pickSlot_ == kachakacha::v2::app::ExtrudeSlot::Target) return false;
    // 塗った立体と未選択の輪郭が重なるとき、最初のクリックは対象立体を取る。
    // 対象が決まった後は、同じ場所でも輪郭領域を取れる。
    const auto index = ProfileRegionAt(position);
    if (!index.has_value()) {
        return false;
    }
    const bool remove = ProfileRegionSelected(*index);
    auto next = selection_;
    for (const auto& id : ProfileRegionEntityIds(profileRegions_[*index])) {
        PickCandidate candidate;
        candidate.entityId = id;
        candidate.kind = SelectionElementKind::Object;
        if(profileRegions_[*index].plane.maximumDeviationMm <= 1e-6)
            candidate.profileSeed = kachakacha::v2::app::ProfileRegionInterior(profileRegions_[*index]);
        next = ApplySelection(next, candidate,
            remove ? SelectionMode::Subtract : SelectionMode::Add);
    }
    SetSelection(std::move(next));
    status_ = remove
        ? "輪郭領域を入力から外しました。別の内側をクリックして追加できます。"
        : "輪郭領域を入力へ追加しました。別の内側も続けてクリックできます。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
    return true;
}

void V2Viewport::DrawProfileRegions(QPainter& painter) const
{
    if (!profileRegionPicking_) {
        return;
    }
    painter.save();
    for (std::size_t index = 0; index < profileRegions_.size(); ++index) {
        const auto ids = ProfileRegionEntityIds(profileRegions_[index]);
        if (std::any_of(ids.begin(),ids.end(),[this](auto id){return !EntityShown(id);})) continue;
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        AddBoundary(path, *this, profileRegions_[index].outer);
        for (const auto& hole : profileRegions_[index].holes) {
            AddBoundary(path, *this, hole);
        }
        const bool hovered = hoveredProfileRegion_ == index;
        const bool selected = ProfileRegionSelected(index);
        QColor ink = selected ? palette_.selected : palette_.preview;
        if (hovered) {
            ink = palette_.hover;
        }
        QColor fill = ink;
        fill.setAlpha(hovered ? 82 : (selected ? 62 : 30));
        painter.setPen(QPen(ink, hovered ? 2.5 : 1.5, Qt::DashLine));
        painter.setBrush(QBrush(fill));
        painter.drawPath(path);
    }
    painter.restore();
}
