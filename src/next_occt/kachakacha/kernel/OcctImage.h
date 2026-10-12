#pragma once
#include "kachakacha/modeling/ImagePlacement.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <cstddef>
namespace kachakacha::v2::kernel {
struct ImageSurfacePoint { geometry::Vector3 point,uv,metric; };
// Capture a face with the parent's display tessellation, without meshing the authoritative shape.
[[nodiscard]] base::Result<modeling::KernelShapeHandle> ImageFace(modeling::KernelShapeHandle shape,std::size_t faceIndex);
[[nodiscard]] base::Result<ImageSurfacePoint> ImagePointOnSurface(modeling::KernelShapeHandle face,const geometry::Vector3& point);
[[nodiscard]] base::Result<std::vector<modeling::ImageTriangle>> ImageSurfaceTriangles(const domain::CreateImageDefinition& image);
}
