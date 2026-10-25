#pragma once
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/exporters/PatternExport.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <optional>
namespace V2PlanarCurves {
using namespace kachakacha::v2;
std::optional<geometry::PlanarFrame> SourceFrame(const fabrication::PatternPanel&,const app::FabricationSource&,double tolerance);
geometry::Vector3 PoseVector(geometry::Vector3,const geometry::PlanarFrame&,double progress);
std::vector<geometry::CurveSegment> PoseCurves(const std::vector<geometry::CurveSegment>&,const geometry::PlanarFrame&,double progress);
struct Path {exporters::PatternLine layer;std::vector<geometry::CurveSegment> curves;bool mountain;std::string label;};
base::Result<std::vector<exporters::PatternCurve>> Place(const fabrication::PatternPanel&,const fabrication::PatternPlacement&,const app::FabricationSource&,const app::FabricationMarkings&,const geometry::PlanarFrame&,double tolerance);
std::vector<Path> Paths(const fabrication::PatternPanel&,const app::FabricationSource&,const app::FabricationMarkings&,const geometry::PlanarFrame&,double tolerance);
}
