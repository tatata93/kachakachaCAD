#pragma once
#include "kachakacha/domain/Entity.h"
#include "kachakacha/view/SurfaceRaster.h"
struct V2ImageView {
    kachakacha::v2::base::EntityId entityId;
    kachakacha::v2::view::RasterImage image;
    std::vector<kachakacha::v2::modeling::ImageTriangle> triangles;
};
