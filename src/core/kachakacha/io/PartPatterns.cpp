#include "kachakacha/io/PartPatterns.h"

#include <algorithm>
#include <stdexcept>

namespace kachakacha::io {

namespace {

[[nodiscard]] const model::NamedPlate& RequireSourcePlate(
    const model::Project& project,
    const model::NamedPartModel& partModel)
{
    for (const model::NamedPlate& plate : project.Plates()) {
        if (plate.name == partModel.sourcePlateName) {
            return plate;
        }
    }
    throw std::invalid_argument(
        "Part-model source plate is missing: " + partModel.sourcePlateName);
}

[[nodiscard]] double Interpolate(double minimum, double maximum, double t)
{
    return minimum + (maximum - minimum) * t;
}

} // namespace

PlateFlatPattern BuildPartPattern(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    const std::vector<int>& partNumbers,
    PlateFlatPatternOptions options)
{
    if (partNumbers.empty()) {
        throw std::invalid_argument("部材番号を1つ以上指定してください。");
    }
    std::vector<int> numbers = partNumbers;
    std::sort(numbers.begin(), numbers.end());
    const int partCount = static_cast<int>(partModel.result.parts.size());
    for (std::size_t index = 0; index < numbers.size(); ++index) {
        if (numbers[index] < 1 || numbers[index] > partCount) {
            throw std::invalid_argument("部材番号が範囲外です。");
        }
        if (index > 0 && numbers[index] != numbers[index - 1] + 1) {
            throw std::invalid_argument(
                "結合できるのは隣接した部材だけです(番号が連続していません)。");
        }
    }

    const model::NamedPlate& sourcePlate = RequireSourcePlate(project, partModel);
    const model::Plate& plate = sourcePlate.plate;
    const double t0 = partModel.result.parts[numbers.front() - 1].minimumParameter;
    const double t1 = partModel.result.parts[numbers.back() - 1].maximumParameter;

    // 部材範囲(板材ローカル)を元面パラメータの範囲へ写す。
    model::PlateSurfaceRange range = plate.Range();
    if (partModel.options.splitAxis == model::PartSplitAxis::V) {
        const double minimumV = Interpolate(range.minimumV, range.maximumV, t0);
        const double maximumV = Interpolate(range.minimumV, range.maximumV, t1);
        range.minimumV = minimumV;
        range.maximumV = maximumV;
    } else {
        const double minimumU = Interpolate(range.minimumU, range.maximumU, t0);
        const double maximumU = Interpolate(range.minimumU, range.maximumU, t1);
        range.minimumU = minimumU;
        range.maximumU = maximumU;
    }

    model::NamedPlate partPlate = sourcePlate;
    partPlate.name = partModel.name + "_部材" + std::to_string(numbers.front())
        + (numbers.size() > 1 ? "-" + std::to_string(numbers.back()) : std::string());
    partPlate.plate = model::Plate(
        plate.SourceSurface(),
        plate.Thickness(),
        plate.EndThickness(),
        plate.Direction(),
        range);

    try {
        return BuildPlateFlatPattern(project, partPlate, options);
    } catch (const std::exception&) {
        // 開口・切れ目が部材範囲の外にある場合は、それらを除いて展開する。
        partPlate.openingWireNames.clear();
        partPlate.reliefCutWireNames.clear();
        partPlate.splitWireNames.clear();
        return BuildPlateFlatPattern(project, partPlate, options);
    }
}

std::vector<PlateFlatPattern> BuildAllPartPatterns(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    PlateFlatPatternOptions options)
{
    std::vector<PlateFlatPattern> patterns;
    for (const model::ApproximatedPart& part : partModel.result.parts) {
        patterns.push_back(
            BuildPartPattern(project, partModel, {part.number}, options));
    }
    return patterns;
}

} // namespace kachakacha::io
