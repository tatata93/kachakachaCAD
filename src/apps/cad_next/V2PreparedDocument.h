#pragma once
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <string>

// Ephemeral calculation result, never part of the kcd2 file format.
struct V2PreparedDocument {
    using Curves = std::map<std::string, std::vector<kachakacha::v2::geometry::CurveSegment>>;
    std::string archive;
    std::map<std::string, std::string> parts, guides;
    Curves partEdges, flatBoundaries, guideEdges;
    std::map<std::string, kachakacha::v2::modeling::ShapeMesh> meshes;
    std::map<std::string, kachakacha::v2::fabrication::SurfacePatchSamples> samples;
    std::map<std::string, kachakacha::v2::app::FabricationEvaluation> fabrication;
    kachakacha::v2::modeling::GuideTable guideTable;
};

bool WritePreparedDocument(const QString& path, const QString& token, V2PreparedDocument& data);
kachakacha::v2::base::Result<std::unique_ptr<V2PreparedDocument>> ReadPreparedDocument(
    const QString& path, const QString& token, const std::function<bool()>& pulse);
