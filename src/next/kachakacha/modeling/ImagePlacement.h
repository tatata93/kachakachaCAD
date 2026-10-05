#pragma once
#include "kachakacha/domain/ImageDefinition.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/base/Diagnostic.h"
namespace kachakacha::v2::modeling {
struct ImageTriangle { MeshTriangle mesh; std::array<geometry::Vector3,3> pixels; };
[[nodiscard]] geometry::Vector3 ImagePixelAt(const domain::CreateImageDefinition& image, const geometry::Vector3& position, const geometry::Vector3& uv = {});
[[nodiscard]] std::vector<ImageTriangle> FlatImageTriangles(const domain::CreateImageDefinition& image);
[[nodiscard]] base::Result<double> ImageScaleFromPoints(const geometry::Vector3& pixelA,const geometry::Vector3& pixelB,const geometry::Vector3& targetA,const geometry::Vector3& targetB);
struct ImageFit { double mmPerPixel; double rotationRad; };
//! Target delta is expressed in the placement plane (or UV metric) in mm.
[[nodiscard]] base::Result<ImageFit> FitImagePoints(const geometry::Vector3& pixelDelta,
    const geometry::Vector3& targetDelta, bool mirror, double rotationRad, bool rotate);
}
