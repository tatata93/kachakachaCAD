#include "V2Viewport.h"
#include <QColor>
#include <algorithm>
#include <utility>
#include <map>
void V2Viewport::SetSegmentRoleColors(std::vector<SegmentRoleColor> colors) {
    segmentRoleColors_=std::move(colors);update();
}
std::optional<QColor> V2Viewport::SegmentRoleColorOf(
    const kachakacha::v2::base::EntityId& id,const kachakacha::v2::base::SegmentId& segmentId) const {
    if(DeletionPreviewContains(id))return QColor(235,65,55);
    for(const auto& role:segmentRoleColors_)if(role.entityId==id&&role.segmentId==segmentId)return role.color;
    return RoleColorOf(id);
}

std::optional<QColor> V2Viewport::FaceRoleColorOf(
    const kachakacha::v2::base::EntityId& id, std::size_t faceIndex) const
{
    if(DeletionPreviewContains(id)) return QColor(235,65,55);
    if (((profileRegionPicking_ && !referencePickHandler_) || elementPickHandler_) && !hoveredProfileRegion_
        && id == hoveredEntityId_ && hoveredFaceIndex_ == faceIndex)
        return SemanticColor(kachakacha::v2::app::SemanticState::Hover);
    for (const auto& role : faceRoleColors_) {
        if (role.entityId == id && (!role.faceIndex || *role.faceIndex == faceIndex)) return role.color;
    }
    return std::nullopt;
}

void V2Viewport::SetFaceRoleColors(std::vector<FaceRoleColor> colors)
{
    faceRoleColors_ = std::move(colors);
    RefreshFaceRolePieces();
}

void V2Viewport::RefreshFaceRolePieces()
{
    // Face partitions depend on geometry only. Hover and role changes supply colors
    // at draw time, without copying triangles or invalidating the GPU buffers.
    if (facePiecesRevision_ == shapeViewRevision_) { update(); return; }
    roleShapePieces_.clear();
    for (const auto& shape : shapeViews_) {
        std::vector<RoleShapePiece> pieces;
        std::map<std::size_t,std::size_t> indices;
        for (const auto& triangle : shape.mesh.triangles) {
            const auto [entry,inserted]=indices.emplace(triangle.faceIndex,pieces.size());
            if(inserted)pieces.push_back({{},triangle.faceIndex});
            pieces[entry->second].mesh.triangles.push_back(triangle);
        }
        if(pieces.empty())pieces.push_back({});
        pieces.front().mesh.edges=shape.mesh.edges;
        for (auto& piece : pieces) {
            piece.mesh.closed = shape.mesh.closed;
            kachakacha::v2::modeling::RefreshBounds(piece.mesh);
        }
        roleShapePieces_.push_back(std::move(pieces));
    }
    facePiecesRevision_=shapeViewRevision_;
    ++interactionMetrics_.facePartitions;
    update();
}

void V2Viewport::BeginReferencePick(std::vector<kachakacha::v2::base::EntityId> ids,
    std::function<void(std::optional<kachakacha::v2::base::EntityId>)> handler)
{
    CancelReferencePick();
    referencePickIds_ = std::move(ids);
    referencePickHandler_ = std::move(handler);
    hoveredProfileRegion_.reset();
    ForgetPickCycle();
    update();
}

void V2Viewport::CancelReferencePick()
{
    if (elementPickHandler_) {
        auto handler = std::move(elementPickHandler_);
        elementPickFilter_ = {};
        ForgetPickCycle();
        handler(std::nullopt);
        RefreshFaceRolePieces();
    }
    if (!referencePickHandler_) return;
    auto handler = std::move(referencePickHandler_);
    referencePickIds_.clear();
    ForgetPickCycle();
    handler(std::nullopt);
    update();
}

void V2Viewport::BeginElementPick(
    std::function<bool(const kachakacha::v2::app::PickCandidate&)> filter,
    std::function<void(std::optional<kachakacha::v2::app::PickCandidate>)> handler)
{
    CancelReferencePick();
    elementPickFilter_ = std::move(filter);
    elementPickHandler_ = std::move(handler);
    hoveredProfileRegion_.reset();
    ForgetPickCycle();
    RefreshFaceRolePieces();
}

bool V2Viewport::AcceptElementPick(const kachakacha::v2::app::PickCandidate& candidate)
{
    if (!elementPickHandler_ || !elementPickFilter_(candidate)) return false;
    auto handler = std::move(elementPickHandler_);
    elementPickFilter_ = {};
    ForgetPickCycle();
    handler(candidate);
    RefreshFaceRolePieces();
    return true;
}
