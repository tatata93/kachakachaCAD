#pragma once

#include "kachakacha/app/Selection.h"
#include <QObject>
#include <map>
#include <set>
#include <cstdint>
#include <vector>

// View-only index. It never normalizes or rewrites the ordered editing selection.
class V2RenderSelection final : public QObject {
public:
    explicit V2RenderSelection(QObject* parent);
    void Update(const kachakacha::v2::app::SelectionSet& selection);
    bool Entity(kachakacha::v2::base::EntityId id) const;
    bool Curve(kachakacha::v2::base::EntityId id, kachakacha::v2::base::SegmentId segment) const;
    void UpdateHover(kachakacha::v2::base::EntityId id,kachakacha::v2::base::SegmentId segment,
        const std::vector<kachakacha::v2::base::SegmentId>& smoothSegments);
    bool HoverCurve(kachakacha::v2::base::EntityId id,kachakacha::v2::base::SegmentId segment) const;
    const std::vector<kachakacha::v2::geometry::Vector3>& ProfileSeeds() const { return profileSeeds_; }
    std::uint64_t Builds() const { return builds_; }
private:
    std::set<kachakacha::v2::base::EntityId> entities_, wholeCurves_;
    std::map<kachakacha::v2::base::EntityId,std::set<kachakacha::v2::base::SegmentId>> segments_;
    std::uint64_t builds_=0;
    std::vector<kachakacha::v2::geometry::Vector3> profileSeeds_;
    kachakacha::v2::base::EntityId hoverEntity_;
    kachakacha::v2::base::SegmentId hoverSegment_;
    std::set<kachakacha::v2::base::SegmentId> hoverSegments_;
};

V2RenderSelection& V2RenderSelectionOf(const QObject& owner);
