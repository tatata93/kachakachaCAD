#pragma once
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/ShapeMesh.h"
std::vector<std::vector<kachakacha::v2::geometry::Vector3>> V2MaterialLoops(
    const kachakacha::v2::app::FabricationEvaluation::MaterialPanel& panel,
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& rails);
std::vector<kachakacha::v2::geometry::CurveSegment> V2MaterialCurves(
    std::vector<kachakacha::v2::geometry::Vector3> points,bool closed,double modelToleranceMm=1e-6);
kachakacha::v2::base::Result<kachakacha::v2::modeling::GuideSurfaceResult> V2BuildMaterialSurface(
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& rails,
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& loops,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance,double boundaryTolerance,
    kachakacha::v2::geometry::Vector3 expectedNormal={});

kachakacha::v2::modeling::ShapeMesh V2MaterialMesh(
    const kachakacha::v2::app::FabricationEvaluation::MaterialPanel& panel,
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& rails);

#include "kachakacha/document/Document.h"
kachakacha::v2::base::Result<kachakacha::v2::base::EntityId> V2StoreMaterialGuide(
    kachakacha::v2::document::Document& document,kachakacha::v2::base::IdGenerator& ids,
    kachakacha::v2::modeling::KernelShapeHandle shape,const std::string& label);
using V2NamedMaterialCurves=std::vector<std::pair<std::string,std::vector<kachakacha::v2::geometry::CurveSegment>>>;
V2NamedMaterialCurves V2FixedConnectionCurves(
    const kachakacha::v2::domain::CreateFabricationModelDefinition& definition,
    const kachakacha::v2::app::FabricationEvaluation& evaluated,
    const V2NamedMaterialCurves& originals,const std::string& stateName);
