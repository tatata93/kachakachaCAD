#pragma once
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/OutputPlacement.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <string>
#include <vector>
namespace kachakacha::v2::kernel {
[[nodiscard]] base::Result<std::string> CaptureOutputShape(modeling::KernelShapeHandle shape);
[[nodiscard]] base::Result<modeling::KernelShapeHandle> RestoreOutputShape(const std::string& brep);
[[nodiscard]] base::Result<modeling::KernelShapeHandle> PlaceOutputShape(modeling::KernelShapeHandle shape,const geometry::OutputPlacement& placement);
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> PlaceOutputCurves(const std::vector<geometry::CurveSegment>& curves,const geometry::OutputPlacement& placement,double tolerance);
[[nodiscard]] base::Result<modeling::KernelShapeHandle> OutputCurveShape(const std::vector<geometry::CurveSegment>& curves);
[[nodiscard]] base::Result<geometry::OutputFrame> OutputSurfaceFrame(modeling::KernelShapeHandle shape,const geometry::Vector3& point);
[[nodiscard]] base::Result<std::string> OutputStep(const std::vector<modeling::KernelShapeHandle>& shapes);
[[nodiscard]] base::Result<std::string> OutputStl(const std::vector<modeling::KernelShapeHandle>& shapes,double deflection);
}
