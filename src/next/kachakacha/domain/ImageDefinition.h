#pragma once
#include "kachakacha/geometry/Vector3.h"
#include <string>
namespace kachakacha::v2::domain {
//! 独立した下絵。画像と貼付先の確定時形状を保存し、外部パスや一時面番号には依存しない。
struct CreateImageDefinition {
    std::string pngBase64;
    int pixelWidth = 0, pixelHeight = 0;
    geometry::Vector3 origin, uAxis{1,0,0}, vAxis{0,1,0};
    geometry::Vector3 anchorPixel;
    double mmPerPixel = 1, rotationRad = 0, opacity = 1;
    bool followSurface = false;
    std::string faceBrep;
    geometry::Vector3 anchorUv, uvMetric{1,1,0};
};
}
