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
[[nodiscard]] base::Result<modeling::KernelShapeHandle> OutputFace(modeling::KernelShapeHandle shape,std::size_t faceIndex);
[[nodiscard]] base::Result<geometry::CurveSegment> OutputEdge(modeling::KernelShapeHandle shape,const geometry::Vector3& point);
// Extract each topological edge once; fail rather than silently dropping an edge.
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> OutputEdges(
    modeling::KernelShapeHandle shape, double tolerance);
[[nodiscard]] base::Result<std::string> OutputStep(const std::vector<modeling::KernelShapeHandle>& shapes);
[[nodiscard]] base::Result<std::string> OutputStl(const std::vector<modeling::KernelShapeHandle>& shapes,double deflection);
}
