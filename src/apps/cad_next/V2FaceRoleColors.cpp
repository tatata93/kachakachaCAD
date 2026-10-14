#include "V2Viewport.h"
#include <QColor>
#include <algorithm>
#include <utility>
void V2Viewport::SetSegmentRoleColors(std::vector<SegmentRoleColor> colors) {
    segmentRoleColors_=std::move(colors);update();
}
std::optional<QColor> V2Viewport::SegmentRoleColorOf(
    const kachakacha::v2::base::EntityId& id,const kachakacha::v2::base::SegmentId& segmentId) const {
    for(const auto& role:segmentRoleColors_)if(role.entityId==id&&role.segmentId==segmentId)return role.color;
    return RoleColorOf(id);
}

std::optional<QColor> V2Viewport::FaceRoleColorOf(
    const kachakacha::v2::base::EntityId& id, std::size_t faceIndex) const
{
    if (profileRegionPicking_ && !referencePickHandler_ && !hoveredProfileRegion_
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
    roleShapePieces_.clear();
    // Split display triangles once when inputs change, never on every frame.
    // CPU picking and the native topology retain their original face indices.
    for (const auto& shape : shapeViews_) {
        if (!(profileRegionPicking_ && hoveredFaceIndex_ && shape.entityId == hoveredEntityId_)
            && std::none_of(faceRoleColors_.begin(), faceRoleColors_.end(),
            [&](const auto& role) { return role.entityId == shape.entityId; })) continue;
        std::vector<RoleShapePiece> pieces;
        pieces.push_back({shape.entityId, {}, {}});
        pieces.front().mesh.edges = shape.mesh.edges;
        for (const auto& triangle : shape.mesh.triangles) {
            const auto color = FaceRoleColorOf(shape.entityId, triangle.faceIndex).value_or(QColor{});
            auto piece = std::find_if(pieces.begin(), pieces.end(),
                [&](const auto& value) { return value.color == color; });
            if (piece == pieces.end()) { pieces.push_back({shape.entityId, {}, color}); piece = pieces.end()-1; }
            piece->mesh.triangles.push_back(triangle);
        }
        for (auto& piece : pieces) {
            piece.mesh.closed = shape.mesh.closed;
            kachakacha::v2::modeling::RefreshBounds(piece.mesh);
            roleShapePieces_.push_back(std::move(piece));
        }
    }
    ++shapeViewRevision_;
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
    if (!referencePickHandler_) return;
    auto handler = std::move(referencePickHandler_);
    referencePickIds_.clear();
    ForgetPickCycle();
    handler(std::nullopt);
    update();
}
