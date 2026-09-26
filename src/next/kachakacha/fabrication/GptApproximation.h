#pragma once
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/PatternLayout.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ShapeMesh.h"

namespace kachakacha::v2::fabrication {
struct GptApproxSource {
    std::vector<geometry::Vector3> samples;
    std::vector<geometry::Vector3> boundary;
    std::vector<std::vector<geometry::Vector3>> holes;
};
struct GptApproxOptions {
    double toleranceMm = .25;
    double minimumWidthMm = .5;
    int maximumPanels = 12;
    int direction = 2; // 0/1: local directions, 2: compare four orientations
};
//! A generalized cylinder: a cross section extruded along v. Never a triangle mesh.
struct GptApproxPanel {
    geometry::Vector3 origin, u, v, normal;
    std::vector<geometry::Point2> profile;
    std::vector<double> lengths;
    PatternPanel pattern;
    double maximumMm = 0, squaredMm = 0;
    std::size_t sampleCount = 0;
    geometry::Vector3 Point(geometry::Point2 flat, double progress) const;
};
struct GptApproxResult {
    std::vector<GptApproxPanel> panels;
    double maximumMm = 0, rmsMm = 0, seamGapMm = 0;
    bool reached = false;
    int direction = 0;
};
base::Result<GptApproxResult> ApproximateGpt(const GptApproxSource& source, const GptApproxOptions& options);
std::vector<geometry::Vector3> GptPanelLoop(const GptApproxPanel& panel,
    const std::vector<geometry::Point2>& loop, double progress);
modeling::ShapeMesh GptPanelMesh(const GptApproxPanel& panel, double progress);
}
