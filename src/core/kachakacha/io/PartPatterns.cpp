#include "kachakacha/io/PartPatterns.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kachakacha::io {

namespace {

using geometry::Vector2;
using geometry::Vector3;

[[nodiscard]] const model::NamedPlate* FindSourcePlate(
    const model::Project& project,
    const model::NamedPartModel& partModel)
{
    for (const model::NamedPlate& plate : project.Plates()) {
        if (plate.name == partModel.sourcePlateName) {
            return &plate;
        }
    }
    return nullptr;
}

//! 近似元(面 or 板材の厚み中央面)のサンプラ。板材入力なら plate も返す(開口用)。
[[nodiscard]] model::PartSource RequireSource(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    const model::NamedPlate** sourcePlate)
{
    if (!partModel.sourceSurfaceName.empty()) {
        for (const model::NamedSurface& surface : project.Surfaces()) {
            if (surface.name == partModel.sourceSurfaceName) {
                return model::PartSource(surface.surface);
            }
        }
        throw std::invalid_argument(
            "Part-model source surface is missing: " + partModel.sourceSurfaceName);
    }
    const model::NamedPlate* plate = FindSourcePlate(project, partModel);
    if (plate == nullptr) {
        throw std::invalid_argument(
            "Part-model source plate is missing: " + partModel.sourcePlateName);
    }
    if (sourcePlate != nullptr) {
        *sourcePlate = plate;
    }
    return model::PartSource(plate->plate);
}

struct MappedPoint {
    Vector2 developed;
    double distance = 0.0;
};

//! 3D点を帯メッシュの三角形へ射影し、同じ重心座標で展開側の位置を返す。
//! 三角形単位の等長対応なので、メッシュ上に載っている点は正確に写る。
[[nodiscard]] MappedPoint MapPointToDevelopment(
    const model::PartMeshDevelopment& mesh, const Vector3& point)
{
    MappedPoint best;
    best.distance = std::numeric_limits<double>::max();
    const auto consider = [&](const Vector3& a3, const Vector3& b3, const Vector3& c3,
                              const Vector2& a2, const Vector2& b2, const Vector2& c2) {
        // 三角形への最近点(重心座標)。標準的な閉形式。
        const Vector3 ab = b3 - a3;
        const Vector3 ac = c3 - a3;
        const Vector3 ap = point - a3;
        const double d1 = Dot(ab, ap);
        const double d2 = Dot(ac, ap);
        double v = 0.0;
        double w = 0.0;
        do {
            if (d1 <= 0.0 && d2 <= 0.0) {
                break;
            }
            const Vector3 bp = point - b3;
            const double d3 = Dot(ab, bp);
            const double d4 = Dot(ac, bp);
            if (d3 >= 0.0 && d4 <= d3) {
                v = 1.0;
                break;
            }
            const Vector3 cp = point - c3;
            const double d5 = Dot(ab, cp);
            const double d6 = Dot(ac, cp);
            if (d6 >= 0.0 && d5 <= d6) {
                w = 1.0;
                break;
            }
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
                v = d1 / (d1 - d3);
                break;
            }
            const double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
                w = d2 / (d2 - d6);
                break;
            }
            const double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
                const double t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                v = 1.0 - t;
                w = t;
                break;
            }
            const double denominator = va + vb + vc;
            if (std::abs(denominator) > 1.0e-18) {
                v = vb / denominator;
                w = vc / denominator;
            }
        } while (false);
        const Vector3 closest = a3 + ab * v + ac * w;
        const double distance = (point - closest).Length();
        if (distance < best.distance) {
            best.distance = distance;
            best.developed = {
                a2.x + (b2.x - a2.x) * v + (c2.x - a2.x) * w,
                a2.y + (b2.y - a2.y) * v + (c2.y - a2.y) * w,
            };
        }
    };
    for (int row = 0; row + 1 < mesh.rows; ++row) {
        for (int column = 0; column + 1 < mesh.columns; ++column) {
            const Vector3& b0 = mesh.world[row][column];
            const Vector3& b1 = mesh.world[row][column + 1];
            const Vector3& t0 = mesh.world[row + 1][column];
            const Vector3& t1 = mesh.world[row + 1][column + 1];
            const Vector2& db0 = mesh.developed[row][column];
            const Vector2& db1 = mesh.developed[row][column + 1];
            const Vector2& dt0 = mesh.developed[row + 1][column];
            const Vector2& dt1 = mesh.developed[row + 1][column + 1];
            consider(b0, b1, t0, db0, db1, dt0);
            consider(t0, b1, t1, dt0, db1, dt1);
        }
    }
    return best;
}

} // namespace

PartPatternResult BuildPartPatternWithPreview(
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

    const model::NamedPlate* sourcePlate = nullptr;
    const model::PartSource source = RequireSource(project, partModel, &sourcePlate);

    // レール(選択部材の縁+内部境界)のパラメータ列。
    std::vector<double> rails;
    rails.push_back(partModel.result.parts[numbers.front() - 1].minimumParameter);
    for (const int number : numbers) {
        rails.push_back(partModel.result.parts[number - 1].maximumParameter);
    }

    PartPatternResult result;
    result.mesh = model::DevelopPartMesh(
        source, partModel.options.splitAxis, rails,
        std::max(24, options.uSegments));

    PlateFlatPattern& pattern = result.pattern;
    pattern.plateName = partModel.name + "_部材" + std::to_string(numbers.front())
        + (numbers.size() > 1 ? "-" + std::to_string(numbers.back()) : std::string());

    const auto& mesh = result.mesh;
    // 外周: 下レール→右端→上レール(逆順)→左端。
    PlateFlatPatternPath outer;
    outer.name = "outer";
    for (int column = 0; column < mesh.columns; ++column) {
        outer.points.push_back(mesh.developed.front()[column]);
    }
    for (int row = 1; row < mesh.rows; ++row) {
        outer.points.push_back(mesh.developed[row][mesh.columns - 1]);
    }
    for (int column = mesh.columns - 2; column >= 0; --column) {
        outer.points.push_back(mesh.developed.back()[column]);
    }
    for (int row = mesh.rows - 2; row >= 1; --row) {
        outer.points.push_back(mesh.developed[row][0]);
    }
    pattern.outerBoundary = std::move(outer);

    // 部材境界=折り線(結合時のみ内部レールが存在する)。
    for (int rail = 1; rail + 1 < mesh.rows; ++rail) {
        PlateFlatPatternPath crease;
        crease.name = "part_crease_" + std::to_string(rail);
        crease.points = mesh.developed[rail];
        crease.foldDirection = mesh.creaseDirections[rail - 1];
        pattern.foldLines.push_back(std::move(crease));
    }

    // 開口(ライト穴など)の写像。部材範囲の外は含めない。
    double deviation = 0.0;
    for (const int number : numbers) {
        deviation = std::max(
            deviation,
            partModel.result.parts[number - 1].estimatedDeviationMillimeters);
    }
    const double inclusionTolerance = std::max(1.0, deviation * 3.0);
    const std::vector<std::string> sourceOpeningNames = sourcePlate != nullptr
        ? sourcePlate->openingWireNames
        : std::vector<std::string>{};
    for (const std::string& openingName : sourceOpeningNames) {
        const auto wire = std::find_if(
            project.Wires().begin(), project.Wires().end(),
            [&openingName](const model::NamedWire& candidate) {
                return candidate.name == openingName;
            });
        if (wire == project.Wires().end()) {
            continue;
        }
        PlateFlatPatternPath opening;
        opening.name = openingName;
        bool inside = true;
        const int samples = std::max(24, options.openingSamples / 2);
        for (int sample = 0; sample <= samples; ++sample) {
            const double parameter = static_cast<double>(sample) / samples;
            const MappedPoint mapped = MapPointToDevelopment(
                result.mesh, wire->wire.Evaluate(parameter));
            if (mapped.distance > inclusionTolerance) {
                inside = false;
                break;
            }
            opening.points.push_back(mapped.developed);
        }
        if (inside && opening.points.size() >= 3) {
            pattern.openings.push_back(std::move(opening));
        }
    }

    pattern.analysis.classification = model::PlateDevelopability::Developable;
    pattern.analysis.maximumEdgeDistortionMillimeters = 0.0;
    pattern.analysis.rootMeanSquareEdgeDistortionMillimeters = 0.0;
    pattern.analysis.maximumReconstructedDeviationMillimeters = deviation;
    pattern.analysis.rootMeanSquareReconstructedDeviationMillimeters = deviation;
    pattern.analysis.pieceCount = 1;
    return result;
}

PlateFlatPattern BuildPartPattern(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    const std::vector<int>& partNumbers,
    PlateFlatPatternOptions options)
{
    return BuildPartPatternWithPreview(project, partModel, partNumbers, options).pattern;
}

std::vector<PartPatternResult> BuildAllPartPatternsWithPreview(
    const model::Project& project,
    const model::NamedPartModel& partModel,
    PlateFlatPatternOptions options)
{
    std::vector<PartPatternResult> results;
    for (const model::ApproximatedPart& part : partModel.result.parts) {
        results.push_back(
            BuildPartPatternWithPreview(project, partModel, {part.number}, options));
    }
    return results;
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
