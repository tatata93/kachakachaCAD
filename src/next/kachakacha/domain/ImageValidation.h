#pragma once
#include "kachakacha/domain/ImageDefinition.h"
#include <cmath>
namespace kachakacha::v2::domain {
inline bool ValidImagePlacement(const domain::CreateImageDefinition& d) {
    using namespace geometry;
    return d.pixelWidth>0 && d.pixelHeight>0 && d.pixelWidth<=16384 && d.pixelHeight<=16384
        && double(d.pixelWidth)*d.pixelHeight<=16777216 && !d.pngBase64.empty() && d.pngBase64.size()<=67108864
        && d.origin.IsFinite() && d.uAxis.IsFinite() && d.vAxis.IsFinite() && d.anchorPixel.IsFinite()
        && std::abs(d.uAxis.Length()-1)<1e-6 && std::abs(d.vAxis.Length()-1)<1e-6 && std::abs(Dot(d.uAxis,d.vAxis))<1e-6
        && std::isfinite(d.mmPerPixel) && d.mmPerPixel>1e-9 && std::isfinite(d.rotationRad)
        && std::isfinite(d.opacity) && d.opacity>=0 && d.opacity<=1 && d.anchorUv.IsFinite()
        && d.uvMetric.IsFinite() && d.uvMetric.x>1e-12 && d.uvMetric.y>1e-12
        && d.anchorPixel.x>=0 && d.anchorPixel.x<=d.pixelWidth && d.anchorPixel.y>=0 && d.anchorPixel.y<=d.pixelHeight;
}
}
