#pragma once
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/app/DrawingSession.h"
class QObject;
class V2Viewport;
bool V2UseLogicalCurvePicks(const V2Viewport&,const kachakacha::v2::app::DrawingSession&);
std::vector<kachakacha::v2::base::EntityId> V2LogicalCurveSelectionOwners(const QObject&,
    kachakacha::v2::app::DrawingSession&,const kachakacha::v2::app::SelectionSet&);

// A wire consisting only of one smooth curved run is one editing target.
// Explicit endpoint/control-point/individual-edge tools bypass this promotion.
void V2PickWholeSmoothCurves(const QObject& owner,kachakacha::v2::app::DrawingSession& session,
    std::vector<kachakacha::v2::app::PickCandidate>& candidates);
std::vector<kachakacha::v2::base::SegmentId> V2SmoothCurvePickSegments(const QObject& owner,
    kachakacha::v2::app::DrawingSession& session,const kachakacha::v2::app::PickCandidate& candidate);
kachakacha::v2::app::SelectionSet V2ApplySmoothCurvePick(const QObject& owner,
    kachakacha::v2::app::DrawingSession& session,const kachakacha::v2::app::SelectionSet& current,
    const std::optional<kachakacha::v2::app::PickCandidate>& picked,kachakacha::v2::app::SelectionMode mode);
