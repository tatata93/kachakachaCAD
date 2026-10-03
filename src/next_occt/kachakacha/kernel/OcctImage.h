#pragma once
#include "kachakacha/modeling/ImagePlacement.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
namespace kachakacha::v2::kernel {
struct ImageSurfacePoint { geometry::Vector3 point,uv,metric; };
[[nodiscard]] base::Result<ImageSurfacePoint> ImagePointOnSurface(modeling::KernelShapeHandle face,const geometry::Vector3& point);
[[nodiscard]] base::Result<std::vector<modeling::ImageTriangle>> ImageSurfaceTriangles(const domain::CreateImageDefinition& image);
}
