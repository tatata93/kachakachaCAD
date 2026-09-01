#include "kachakacha/io/FabricationPanelPapercraft.h"

#include "kachakacha/io/BentSheetPapercraft.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace kachakacha::io {

using geometry::Vector2;
using geometry::Vector3;
using model::NamedPlate;
using model::NamedWire;
using model::Plate;
using model::PlateDevelopability;
using model::PlateSurfaceRange;
using model::Project;

namespace {

struct FeatureBounds {
    std::string name;
    double minimumU = 0.0;
    double maximumU = 0.0;
    double minimumV = 0.0;
    double maximumV = 0.0;
};

const NamedWire& RequireWire(const Project& project, std::string_view name)
{
    const auto position = std::find_if(
        project.Wires().begin(), project.Wires().end(),
        [&](const NamedWire& wire) { return wire.name == name; });
    if (position == project.Wires().end()) {
        throw std::invalid_argument(
            "Fabrication-panel source wire was not found: "
            + std::string(name));
    }
    return *position;
}

FeatureBounds MeasureFeatureBounds(
    const Project& project,
    const NamedPlate& plate,
    std::string_view wireName)
{
    const NamedWire& projected = RequireWire(project, wireName);
    if (!projected.projection.has_value()
        || projected.projection->targetSurfaceName != plate.sourceSurfaceName) {
        throw std::invalid_argument(
            "Fabrication-panel openings must be projected onto the selected plate.");
    }
    const NamedWire& source = RequireWire(
        project, projected.projection->sourceWireName);
    std::vector<Vector3> points;
    constexpr int kSamples = 192;
    points.reserve(kSamples + 1);
    for (int sample = 0; sample <= kSamples; ++sample) {
        points.push_back(source.wire.Evaluate(
            static_cast<double>(sample) / kSamples));
    }
    const auto projections = plate.plate.SourceSurface().ProjectPointsAlongDirection(
        points, projected.projection->direction);
    FeatureBounds bounds;
    bounds.name = std::string(wireName);
    bounds.minimumU = bounds.minimumV = std::numeric_limits<double>::infinity();
    bounds.maximumU = bounds.maximumV = -std::numeric_limits<double>::infinity();
    for (const auto& projection : projections) {
        bounds.minimumU = std::min(bounds.minimumU, projection.u);
        bounds.maximumU = std::max(bounds.maximumU, projection.u);
        bounds.minimumV = std::min(bounds.minimumV, projection.v);
        bounds.maximumV = std::max(bounds.maximumV, projection.v);
    }
    return bounds;
}

std::vector<FeatureBounds> ReadOpeningBounds(
    const Project& project,
    const NamedPlate& plate)
{
    std::vector<FeatureBounds> result;
    result.reserve(plate.openingWireNames.size());
    for (const std::string& name : plate.openingWireNames) {
        result.push_back(MeasureFeatureBounds(project, plate, name));
    }
    return result;
}

bool Contains(const PlateSurfaceRange& range, const FeatureBounds& bounds)
{
    constexpr double tolerance = 1.0e-6;
    return bounds.minimumU >= range.minimumU - tolerance
        && bounds.maximumU <= range.maximumU + tolerance
        && bounds.minimumV >= range.minimumV - tolerance
        && bounds.maximumV <= range.maximumV + tolerance;
}

NamedPlate MakePanelPlate(
    const Project& project,
    const NamedPlate& source,
    PlateSurfaceRange range,
    const std::vector<FeatureBounds>& openings,
    bool includeFeatures)
{
    const Plate& original = source.plate;
    const double originalVSpan
        = original.Range().maximumV - original.Range().minimumV;
    const double localMinimumV = (range.minimumV - original.Range().minimumV)
        / originalVSpan;
    const double localMaximumV = (range.maximumV - original.Range().minimumV)
        / originalVSpan;
    NamedPlate panel = source;
    panel.plate = Plate(
        original.SourceSurface(),
        original.Thickness(localMinimumV),
        original.Thickness(localMaximumV),
        original.Direction(),
        range);
    panel.openingWireNames.clear();
    panel.reliefCutWireNames.clear();
    panel.splitWireNames.clear();
    if (includeFeatures) {
        for (const FeatureBounds& opening : openings) {
            if (Contains(range, opening)) {
                panel.openingWireNames.push_back(opening.name);
            }
        }
        for (const std::string& name : source.reliefCutWireNames) {
            if (Contains(range, MeasureFeatureBounds(project, source, name))) {
                panel.reliefCutWireNames.push_back(name);
            }
        }
    }
    return panel;
}

double DirectionNormalChange(const Plate& plate, bool alongU)
{
    constexpr int kAcrossSamples = 7;
    constexpr int kAlongSamples = 18;
    double maximum = 0.0;
    for (int across = 0; across < kAcrossSamples; ++across) {
        const double fixed = static_cast<double>(across) / (kAcrossSamples - 1);
        double sum = 0.0;
        std::optional<Vector3> previous;
        for (int along = 0; along < kAlongSamples; ++along) {
            const double varying = static_cast<double>(along) / (kAlongSamples - 1);
            const double u = alongU ? varying : fixed;
            const double v = alongU ? fixed : varying;
            const Vector3 normal = plate.SourceSurface().Normal(
                plate.SourceU(u), plate.SourceV(v));
            if (previous.has_value()) {
                sum += std::acos(std::clamp(
                    geometry::Dot(*previous, normal), -1.0, 1.0));
            }
            previous = normal;
        }
        maximum = std::max(maximum, sum);
    }
    return maximum;
}

bool ResolveLongDirection(
    const Plate& plate,
    FabricationPanelDirection requested)
{
    if (requested == FabricationPanelDirection::LongAlongU) {
        return true;
    }
    if (requested == FabricationPanelDirection::LongAlongV) {
        return false;
    }
    return DirectionNormalChange(plate, true)
        >= DirectionNormalChange(plate, false);
}

double PanelWidth(
    const Plate& plate,
    const PlateSurfaceRange& range,
    bool longDirectionIsU)
{
    double minimumWidth = std::numeric_limits<double>::infinity();
    // A naturally tapered panel may end at a point. Evaluate its usable
    // interior width instead of rejecting it solely because of that endpoint.
    for (int sample = 1; sample < 12; ++sample) {
        const double parameter = static_cast<double>(sample) / 12.0;
        const Vector3 first = longDirectionIsU
            ? plate.SourceSurface().Evaluate(
                  range.minimumU + (range.maximumU - range.minimumU) * parameter,
                  range.minimumV)
            : plate.SourceSurface().Evaluate(
                  range.minimumU,
                  range.minimumV + (range.maximumV - range.minimumV) * parameter);
        const Vector3 second = longDirectionIsU
            ? plate.SourceSurface().Evaluate(
                  range.minimumU + (range.maximumU - range.minimumU) * parameter,
                  range.maximumV)
            : plate.SourceSurface().Evaluate(
                  range.maximumU,
                  range.minimumV + (range.maximumV - range.minimumV) * parameter);
        minimumWidth = std::min(minimumWidth, (second - first).Length());
    }
    return minimumWidth;
}

std::pair<PlateSurfaceRange, PlateSurfaceRange> SplitRange(
    PlateSurfaceRange range,
    bool longDirectionIsU,
    double parameter)
{
    PlateSurfaceRange first = range;
    PlateSurfaceRange second = range;
    if (longDirectionIsU) {
        first.maximumV = parameter;
        second.minimumV = parameter;
    } else {
        first.maximumU = parameter;
        second.minimumU = parameter;
    }
    return {first, second};
}

std::optional<double> ChooseSplitParameter(
    const Plate& plate,
    const PlateSurfaceRange& range,
    bool longDirectionIsU,
    const std::vector<FeatureBounds>& features,
    double minimumWidth)
{
    const double minimum = longDirectionIsU ? range.minimumV : range.minimumU;
    const double maximum = longDirectionIsU ? range.maximumV : range.maximumU;
    double candidate = (minimum + maximum) * 0.5;
    for (int iteration = 0; iteration < 8; ++iteration) {
        bool moved = false;
        for (const FeatureBounds& feature : features) {
            if (!Contains(range, feature)) {
                continue;
            }
            const double featureMinimum
                = longDirectionIsU ? feature.minimumV : feature.minimumU;
            const double featureMaximum
                = longDirectionIsU ? feature.maximumV : feature.maximumU;
            if (candidate > featureMinimum - 1.0e-6
                && candidate < featureMaximum + 1.0e-6) {
                const double before = featureMinimum - (maximum - minimum) * 0.02;
                const double after = featureMaximum + (maximum - minimum) * 0.02;
                candidate = std::abs(candidate - before)
                        <= std::abs(after - candidate)
                    ? before : after;
                moved = true;
                break;
            }
        }
        if (!moved) {
            break;
        }
    }
    if (candidate <= minimum + 1.0e-6
        || candidate >= maximum - 1.0e-6) {
        return std::nullopt;
    }
    const auto [first, second] = SplitRange(
        range, longDirectionIsU, candidate);
    if (PanelWidth(plate, first, longDirectionIsU) < minimumWidth
        || PanelWidth(plate, second, longDirectionIsU) < minimumWidth) {
        return std::nullopt;
    }
    return candidate;
}

FabricationPanel EvaluatePanel(
    const Project& project,
    const NamedPlate& source,
    PlateSurfaceRange range,
    PlateFlatPatternOptions options)
{
    NamedPlate panel = MakePanelPlate(project, source, range, {}, false);
    const PlateAssemblyMotion motion = BuildBentSheetPapercraftMotion(
        project, panel, 1.0, options);
    FabricationPanel result;
    result.range = range;
    result.maximumDeviationMillimeters
        = motion.maximumTargetMismatchMillimeters;
    result.rootMeanSquareDeviationMillimeters
        = motion.rootMeanSquareTargetMismatchMillimeters;
    const auto developability = panel.plate.AnalyzeDevelopability();
    result.fit = developability.classification == PlateDevelopability::Planar
        ? FabricationPanelFit::Plane
        : motion.reliefCutPaths.empty()
        ? FabricationPanelFit::DevelopableStrip
        : FabricationPanelFit::ReliefAssistedStrip;
    return result;
}

void TranslatePath(PlateFlatPatternPath& path, Vector2 delta)
{
    for (Vector2& point : path.points) {
        point = point + delta;
    }
}

void TranslatePiece(PlateFlatPatternPiece& piece, Vector2 delta)
{
    TranslatePath(piece.outerBoundary, delta);
    for (PlateFlatPatternPath& path : piece.openings) {
        TranslatePath(path, delta);
    }
    for (PlateFlatPatternPath& path : piece.foldLines) {
        TranslatePath(path, delta);
    }
    for (PlateFlatPatternPath& path : piece.reliefCuts) {
        TranslatePath(path, delta);
    }
}

std::pair<Vector2, Vector2> Bounds(const PlateFlatPatternPath& path)
{
    Vector2 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    Vector2 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    for (Vector2 point : path.points) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
    }
    return {minimum, maximum};
}

void AppendPiecePaths(
    PlateFlatPattern& result,
    const PlateFlatPatternPiece& piece)
{
    result.openings.insert(
        result.openings.end(), piece.openings.begin(), piece.openings.end());
    result.foldLines.insert(
        result.foldLines.end(), piece.foldLines.begin(), piece.foldLines.end());
    result.reliefCuts.insert(
        result.reliefCuts.end(), piece.reliefCuts.begin(), piece.reliefCuts.end());
}

void AppendGuidePaths(
    std::vector<PlateAssemblyGuidePath>& target,
    const std::vector<PlateAssemblyGuidePath>& source,
    std::string_view prefix)
{
    for (const PlateAssemblyGuidePath& path : source) {
        PlateAssemblyGuidePath copy = path;
        copy.name = std::string(prefix) + copy.name;
        target.push_back(std::move(copy));
    }
}

PlateAssemblyGuide BuildGuideFromLayout(
    const Project& project,
    const NamedPlate& plate,
    const FabricationPanelLayout& layout,
    const std::vector<FeatureBounds>& openings,
    PlateFlatPatternOptions options)
{
    PlateAssemblyGuide result;
    result.plateName = plate.name;
    for (std::size_t index = 0; index < layout.panels.size(); ++index) {
        const NamedPlate panelPlate = MakePanelPlate(
            project, plate, layout.panels[index].range, openings, true);
        const PlateAssemblyGuide guide = BuildBentSheetPapercraftGuide(
            project, panelPlate, options);
        const std::string prefix = "P" + std::to_string(index + 1) + "_";
        AppendGuidePaths(result.foldLines, guide.foldLines, prefix);
        AppendGuidePaths(result.reliefCuts, guide.reliefCuts, prefix);
        AppendGuidePaths(result.splitLines, guide.splitLines, prefix);
    }
    return result;
}

PlateAssemblyMotion BuildMotionFromLayout(
    const Project& project,
    const NamedPlate& plate,
    const FabricationPanelLayout& layout,
    const std::vector<FeatureBounds>& openings,
    double progress,
    PlateFlatPatternOptions options)
{
    PlateAssemblyMotion result;
    result.plateName = plate.name;
    result.progress = std::clamp(progress, 0.0, 1.0);
    result.pieceCount = static_cast<int>(layout.panels.size());
    result.preferContinuousModel = true;
    double targetSquared = 0.0;
    double edgeSquared = 0.0;
    for (std::size_t index = 0; index < layout.panels.size(); ++index) {
        const NamedPlate panelPlate = MakePanelPlate(
            project, plate, layout.panels[index].range, openings, true);
        PlateAssemblyMotion motion = BuildBentSheetPapercraftMotion(
            project, panelPlate, progress, options);
        result.panels.insert(
            result.panels.end(), motion.panels.begin(), motion.panels.end());
        result.pieceIndices.insert(
            result.pieceIndices.end(), motion.panels.size(),
            static_cast<int>(index));
        result.panelThicknessMillimeters.insert(
            result.panelThicknessMillimeters.end(),
            motion.panelThicknessMillimeters.begin(),
            motion.panelThicknessMillimeters.end());
        result.panelDeviationMillimeters.insert(
            result.panelDeviationMillimeters.end(),
            motion.panelDeviationMillimeters.begin(),
            motion.panelDeviationMillimeters.end());
        PlateAssemblyContinuousPiece continuous;
        continuous.pieceIndex = static_cast<int>(index);
        continuous.sections = std::move(motion.continuousSections);
        continuous.openingPaths = std::move(motion.openingPaths);
        continuous.reliefCutPaths = std::move(motion.reliefCutPaths);
        result.continuousPieces.push_back(std::move(continuous));
        result.maximumPanelDeviationMillimeters = std::max(
            result.maximumPanelDeviationMillimeters,
            motion.maximumPanelDeviationMillimeters);
        result.maximumTargetMismatchMillimeters = std::max(
            result.maximumTargetMismatchMillimeters,
            motion.maximumTargetMismatchMillimeters);
        result.maximumMaterialEdgeErrorMillimeters = std::max(
            result.maximumMaterialEdgeErrorMillimeters,
            motion.maximumMaterialEdgeErrorMillimeters);
        result.maximumSeamMismatchMillimeters = std::max(
            result.maximumSeamMismatchMillimeters,
            motion.maximumSeamMismatchMillimeters);
        result.maximumPanelConnectionMismatchMillimeters = std::max(
            result.maximumPanelConnectionMismatchMillimeters,
            motion.maximumPanelConnectionMismatchMillimeters);
        result.materialAreaSquareMillimeters
            += motion.materialAreaSquareMillimeters;
        targetSquared += motion.rootMeanSquareTargetMismatchMillimeters
            * motion.rootMeanSquareTargetMismatchMillimeters;
        edgeSquared += motion.rootMeanSquareMaterialEdgeErrorMillimeters
            * motion.rootMeanSquareMaterialEdgeErrorMillimeters;
    }
    result.rootMeanSquareTargetMismatchMillimeters = std::sqrt(
        targetSquared / static_cast<double>(layout.panels.size()));
    result.rootMeanSquareMaterialEdgeErrorMillimeters = std::sqrt(
        edgeSquared / static_cast<double>(layout.panels.size()));
    return result;
}

} // namespace

FabricationPanelLayout BuildFabricationPanelLayout(
    const Project& project,
    const NamedPlate& plate,
    PlateFlatPatternOptions options)
{
    if (options.maximumFabricationPanelCount < 1
        || options.maximumFabricationPanelCount > 64
        || !std::isfinite(options.maximumShapeErrorMillimeters)
        || options.maximumShapeErrorMillimeters <= 0.0
        || !std::isfinite(options.minimumPartWidthMillimeters)
        || options.minimumPartWidthMillimeters <= 0.0) {
        throw std::invalid_argument(
            "Fabrication-panel options are invalid.");
    }
    const bool longDirectionIsU = ResolveLongDirection(
        plate.plate, options.fabricationPanelDirection);
    const std::vector<FeatureBounds> openings
        = ReadOpeningBounds(project, plate);
    std::vector<double> manualSplits;
    for (const std::string& splitName : plate.splitWireNames) {
        const FeatureBounds bounds = MeasureFeatureBounds(
            project, plate, splitName);
        const double position = longDirectionIsU
            ? (bounds.minimumV + bounds.maximumV) * 0.5
            : (bounds.minimumU + bounds.maximumU) * 0.5;
        const double minimum = longDirectionIsU
            ? plate.plate.Range().minimumV : plate.plate.Range().minimumU;
        const double maximum = longDirectionIsU
            ? plate.plate.Range().maximumV : plate.plate.Range().maximumU;
        if (position > minimum + 1.0e-6 && position < maximum - 1.0e-6) {
            for (const FeatureBounds& opening : openings) {
                const double openingMinimum = longDirectionIsU
                    ? opening.minimumV : opening.minimumU;
                const double openingMaximum = longDirectionIsU
                    ? opening.maximumV : opening.maximumU;
                if (position > openingMinimum + 1.0e-6
                    && position < openingMaximum - 1.0e-6) {
                    throw std::invalid_argument(
                        "A manual fabrication split crosses a window or light opening. Move the split to an opening edge.");
                }
            }
            manualSplits.push_back(position);
        }
    }
    std::sort(manualSplits.begin(), manualSplits.end());
    manualSplits.erase(std::unique(
        manualSplits.begin(), manualSplits.end(), [](double first, double second) {
            return std::abs(first - second) <= 1.0e-6;
        }), manualSplits.end());

    std::vector<FabricationPanel> panels;
    PlateSurfaceRange nextRange = plate.plate.Range();
    for (double split : manualSplits) {
        PlateSurfaceRange panelRange = nextRange;
        if (longDirectionIsU) {
            panelRange.maximumV = split;
            nextRange.minimumV = split;
        } else {
            panelRange.maximumU = split;
            nextRange.minimumU = split;
        }
        if (PanelWidth(plate.plate, panelRange, longDirectionIsU)
            < options.minimumPartWidthMillimeters) {
            throw std::invalid_argument(
                "A manual fabrication split creates a part narrower than the minimum part width.");
        }
        panels.push_back(EvaluatePanel(project, plate, panelRange, options));
    }
    if (PanelWidth(plate.plate, nextRange, longDirectionIsU)
        < options.minimumPartWidthMillimeters) {
        throw std::invalid_argument(
            "A manual fabrication split creates a part narrower than the minimum part width.");
    }
    panels.push_back(EvaluatePanel(project, plate, nextRange, options));
    while (static_cast<int>(panels.size())
        < options.maximumFabricationPanelCount) {
        std::vector<std::size_t> candidates(panels.size());
        std::iota(candidates.begin(), candidates.end(), 0);
        std::sort(candidates.begin(), candidates.end(), [&](std::size_t first, std::size_t second) {
            return panels[first].maximumDeviationMillimeters
                > panels[second].maximumDeviationMillimeters;
        });
        std::optional<std::size_t> selectedIndex;
        std::optional<double> selectedSplit;
        for (std::size_t candidate : candidates) {
            if (panels[candidate].maximumDeviationMillimeters
                <= options.maximumShapeErrorMillimeters) {
                break;
            }
            const auto split = ChooseSplitParameter(
                plate.plate, panels[candidate].range, longDirectionIsU,
                openings, options.minimumPartWidthMillimeters);
            if (split.has_value()) {
                selectedIndex = candidate;
                selectedSplit = split;
                break;
            }
        }
        if (!selectedIndex.has_value() || !selectedSplit.has_value()) {
            break;
        }
        const auto [firstRange, secondRange] = SplitRange(
            panels[*selectedIndex].range, longDirectionIsU, *selectedSplit);
        const std::size_t insertionIndex = *selectedIndex;
        panels.erase(panels.begin() + static_cast<std::ptrdiff_t>(insertionIndex));
        auto second = EvaluatePanel(project, plate, secondRange, options);
        auto first = EvaluatePanel(project, plate, firstRange, options);
        panels.insert(panels.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
            std::move(first));
        panels.insert(panels.begin() + static_cast<std::ptrdiff_t>(insertionIndex + 1),
            std::move(second));
    }
    std::sort(panels.begin(), panels.end(), [&](const auto& first, const auto& second) {
        return longDirectionIsU
            ? first.range.minimumV < second.range.minimumV
            : first.range.minimumU < second.range.minimumU;
    });

    FabricationPanelLayout result;
    result.plateName = plate.name;
    result.longDirectionIsU = longDirectionIsU;
    result.panels = std::move(panels);
    double squaredRms = 0.0;
    for (const FabricationPanel& panel : result.panels) {
        result.maximumDeviationMillimeters = std::max(
            result.maximumDeviationMillimeters,
            panel.maximumDeviationMillimeters);
        squaredRms += panel.rootMeanSquareDeviationMillimeters
            * panel.rootMeanSquareDeviationMillimeters;
    }
    result.rootMeanSquareDeviationMillimeters = std::sqrt(
        squaredRms / static_cast<double>(result.panels.size()));
    result.reachedRequestedTolerance
        = result.maximumDeviationMillimeters
        <= options.maximumShapeErrorMillimeters;
    return result;
}

PlateFlatPattern BuildFabricationPanelPapercraftPattern(
    const Project& project,
    const NamedPlate& plate,
    PlateFlatPatternOptions options)
{
    const FabricationPanelLayout layout = BuildFabricationPanelLayout(
        project, plate, options);
    const std::vector<FeatureBounds> openings = ReadOpeningBounds(project, plate);
    PlateFlatPattern result;
    result.plateName = plate.name;
    double cursorX = 0.0;
    double cursorY = 0.0;
    double edgeRmsSquared = 0.0;
    for (std::size_t index = 0; index < layout.panels.size(); ++index) {
        const FabricationPanel& panel = layout.panels[index];
        NamedPlate panelPlate = MakePanelPlate(
            project, plate, panel.range, openings, true);
        PlateFlatPattern pattern = BuildBentSheetPapercraftPattern(
            project, panelPlate, options);
        if (pattern.pieces.size() != 1) {
            throw std::logic_error(
                "A fabrication panel must unfold as one continuous part.");
        }
        PlateFlatPatternPiece piece = std::move(pattern.pieces.front());
        piece.name = "fabrication_panel_" + std::to_string(index + 1);
        piece.outerBoundary.name = piece.name;
        const auto [minimum, maximum] = Bounds(piece.outerBoundary);
        if (layout.longDirectionIsU) {
            // Horizontal body bands are easiest to identify and cut when their
            // assembly order is preserved from bottom to top on the sheet.
            TranslatePiece(piece, {-minimum.x, cursorY - minimum.y});
            cursorY += maximum.y - minimum.y + options.marginMillimeters;
        } else {
            TranslatePiece(piece, {cursorX - minimum.x, -minimum.y});
            cursorX += maximum.x - minimum.x + options.marginMillimeters;
        }
        AppendPiecePaths(result, piece);
        result.pieces.push_back(std::move(piece));

        result.analysis.maximumEdgeDistortionMillimeters = std::max(
            result.analysis.maximumEdgeDistortionMillimeters,
            pattern.analysis.maximumEdgeDistortionMillimeters);
        result.analysis.maximumBoundaryApproximationMillimeters = std::max(
            result.analysis.maximumBoundaryApproximationMillimeters,
            pattern.analysis.maximumBoundaryApproximationMillimeters);
        result.analysis.maximumMaterialEdgeErrorMillimeters = std::max(
            result.analysis.maximumMaterialEdgeErrorMillimeters,
            pattern.analysis.maximumMaterialEdgeErrorMillimeters);
        result.analysis.maximumSeamMismatchMillimeters = std::max(
            result.analysis.maximumSeamMismatchMillimeters,
            pattern.analysis.maximumSeamMismatchMillimeters);
        result.analysis.maximumPanelConnectionMismatchMillimeters = std::max(
            result.analysis.maximumPanelConnectionMismatchMillimeters,
            pattern.analysis.maximumPanelConnectionMismatchMillimeters);
        result.analysis.totalCutLengthMillimeters
            += pattern.analysis.totalCutLengthMillimeters;
        result.analysis.reliefCutLengthMillimeters
            += pattern.analysis.reliefCutLengthMillimeters;
        result.analysis.nonSeparatingReliefCutCount
            += pattern.analysis.nonSeparatingReliefCutCount;
        result.analysis.automaticNotchCount
            += pattern.analysis.automaticNotchCount;
        edgeRmsSquared += pattern.analysis.rootMeanSquareEdgeDistortionMillimeters
            * pattern.analysis.rootMeanSquareEdgeDistortionMillimeters;
    }
    result.outerBoundary = result.pieces.front().outerBoundary;
    result.analysis.classification = layout.panels.size() == 1
        && layout.panels.front().fit == FabricationPanelFit::Plane
        ? PlateDevelopability::Planar
        : PlateDevelopability::Developable;
    result.analysis.pieceCount = static_cast<int>(layout.panels.size());
    result.analysis.separatingSeamCount
        = std::max(0, result.analysis.pieceCount - 1);
    result.analysis.maximumReconstructedDeviationMillimeters
        = layout.maximumDeviationMillimeters;
    result.analysis.rootMeanSquareReconstructedDeviationMillimeters
        = layout.rootMeanSquareDeviationMillimeters;
    result.analysis.rootMeanSquareEdgeDistortionMillimeters
        = std::sqrt(edgeRmsSquared / static_cast<double>(layout.panels.size()));
    return result;
}

PlateAssemblyGuide BuildFabricationPanelPapercraftGuide(
    const Project& project,
    const NamedPlate& plate,
    PlateFlatPatternOptions options)
{
    const FabricationPanelLayout layout = BuildFabricationPanelLayout(
        project, plate, options);
    const std::vector<FeatureBounds> openings = ReadOpeningBounds(project, plate);
    return BuildGuideFromLayout(project, plate, layout, openings, options);
}

PlateAssemblyMotion BuildFabricationPanelPapercraftMotion(
    const Project& project,
    const NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options)
{
    const FabricationPanelLayout layout = BuildFabricationPanelLayout(
        project, plate, options);
    const std::vector<FeatureBounds> openings = ReadOpeningBounds(project, plate);
    return BuildMotionFromLayout(
        project, plate, layout, openings, progress, options);
}

FabricationPanelPapercraftPreview BuildFabricationPanelPapercraftPreview(
    const Project& project,
    const NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options)
{
    FabricationPanelPapercraftPreview result;
    result.layout = BuildFabricationPanelLayout(project, plate, options);
    const std::vector<FeatureBounds> openings = ReadOpeningBounds(project, plate);
    result.guide = BuildGuideFromLayout(
        project, plate, result.layout, openings, options);
    result.motion = BuildMotionFromLayout(
        project, plate, result.layout, openings, progress, std::move(options));
    return result;
}

} // namespace kachakacha::io
