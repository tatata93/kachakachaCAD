#include "kachakacha/io/PartFoldState.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <stdexcept>

namespace kachakacha::io {

namespace {

using geometry::Vector3;
using model::NamedPartModel;
using model::NamedPlate;
using model::NamedWire;
using model::PartMeshDevelopment;
using model::Project;
using model::Wire;

[[nodiscard]] Vector3 Normalized(const Vector3& value)
{
    const double length = value.Length();
    if (length <= 1.0e-12) {
        return {0.0, 0.0, 0.0};
    }
    return value * (1.0 / length);
}

} // namespace

PartFoldStateResult AddPartFoldStateModel(
    Project& target,
    const Project& source,
    const NamedPartModel& partModel,
    const PartFoldStateOptions& options,
    const std::string& namePrefix)
{
    if (namePrefix.empty()) {
        throw std::invalid_argument("曲げ状態モデルの名前を指定してください。");
    }
    if (!std::isfinite(options.progress)) {
        throw std::invalid_argument("曲げ具合は0〜1で指定してください。");
    }
    const double progress = std::clamp(options.progress, 0.0, 1.0);

    // source と target が同じプロジェクトでも安全なように、必要な元データを先にコピーする。
    const NamedPartModel model = partModel;
    const bool fromSurface = !model.sourceSurfaceName.empty();
    std::optional<model::NamedPlate> sourcePlateCopy;
    std::optional<model::NamedSurface> sourceSurfaceCopy;
    if (fromSurface) {
        for (const model::NamedSurface& candidate : source.Surfaces()) {
            if (candidate.name == model.sourceSurfaceName) {
                sourceSurfaceCopy = candidate;
                break;
            }
        }
        if (!sourceSurfaceCopy.has_value()) {
            throw std::invalid_argument("元の面が見つかりません: " + model.sourceSurfaceName);
        }
        if (!std::isfinite(options.surfaceThicknessMillimeters)
            || options.surfaceThicknessMillimeters <= 0.0) {
            throw std::invalid_argument(
                "面入力の近似モデルを板材化するには板厚を正の値で指定してください。");
        }
    } else {
        for (const NamedPlate& candidate : source.Plates()) {
            if (candidate.name == model.sourcePlateName) {
                sourcePlateCopy = candidate;
                break;
            }
        }
        if (!sourcePlateCopy.has_value()) {
            throw std::invalid_argument("元の板材が見つかりません: " + model.sourcePlateName);
        }
    }
    std::vector<NamedWire> openings;
    if (sourcePlateCopy.has_value()) {
        for (const std::string& openingName : sourcePlateCopy->openingWireNames) {
            for (const NamedWire& wire : source.Wires()) {
                if (wire.name == openingName) {
                    openings.push_back(wire);
                    break;
                }
            }
        }
    }

    // 出力する部材番号(1始まり)。
    const int partCount = static_cast<int>(model.result.parts.size());
    std::vector<int> numbers = options.partNumbers;
    if (numbers.empty()) {
        for (int number = 1; number <= partCount; ++number) {
            numbers.push_back(number);
        }
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    for (const int number : numbers) {
        if (number < 1 || number > partCount) {
            throw std::invalid_argument(
                "部材番号が範囲外です: " + std::to_string(number));
        }
    }

    // レール(部材数+1本)のパラメータ列 → メッシュ → 曲げ状態。
    std::vector<double> parameters;
    parameters.push_back(0.0);
    for (std::size_t index = 1; index < model.result.parts.size(); ++index) {
        parameters.push_back(model.result.parts[index].minimumParameter);
    }
    parameters.push_back(1.0);
    const model::PartSource meshSource = fromSurface
        ? model::PartSource(sourceSurfaceCopy->surface)
        : model::PartSource(sourcePlateCopy->plate);
    const PartMeshDevelopment mesh = model::DevelopPartMesh(
        meshSource, model.options.splitAxis, parameters, options.columns);
    // 可動折り線(合意10): 折り線ごとの進行度を織り込んだ状態を目標とし、
    // 全体の曲げ具合(progress)は「展開⇄その折り状態」の補間として掛ける。
    const bool customFold = std::any_of(
        model.railFoldProgress.begin(), model.railFoldProgress.end(),
        [](double value) { return std::abs(value - 1.0) > 1.0e-12; });
    const std::vector<std::vector<Vector3>> state = customFold
        ? model::BuildFoldPreviewToState(
            mesh, model::BuildPerCreaseFoldState(mesh, model.railFoldProgress), progress)
        : model::BuildFoldPreview(mesh, progress);

    PartFoldStateResult result;

    // 必要なレールのワイヤを一度だけ作る。
    std::set<int> railRows;
    for (const int number : numbers) {
        railRows.insert(number - 1);
        railRows.insert(number);
    }
    std::vector<std::string> railNameByRow(mesh.rows);
    for (const int row : railRows) {
        const std::string railName
            = namePrefix + "_レール" + std::to_string(row + 1);
        target.AddWire(railName, Wire::Polyline(state[row]));
        railNameByRow[row] = railName;
        result.railWireNames.push_back(railName);
    }

    // 部材ごとのルールド面と厚み付き板材。
    std::vector<std::string> surfaceNameByPart(partCount);
    std::vector<std::string> plateNameByPart(partCount);
    for (const int number : numbers) {
        const std::string surfaceName
            = namePrefix + "_部材" + std::to_string(number) + "面";
        const std::string plateName
            = namePrefix + "_部材" + std::to_string(number) + "板";
        target.AddRuledSurface(
            surfaceName, railNameByRow[number - 1], railNameByRow[number]);
        if (fromSurface) {
            target.AddPlate(
                plateName,
                surfaceName,
                options.surfaceThicknessMillimeters,
                model::PlateThicknessDirection::Centered,
                "未指定");
        } else if (sourcePlateCopy->plate.HasVariableThickness()) {
            target.AddPlate(
                plateName,
                surfaceName,
                sourcePlateCopy->plate.Thickness(),
                sourcePlateCopy->plate.EndThickness(),
                sourcePlateCopy->plate.Direction(),
                sourcePlateCopy->material);
        } else {
            target.AddPlate(
                plateName,
                surfaceName,
                sourcePlateCopy->plate.Thickness(),
                sourcePlateCopy->plate.Direction(),
                sourcePlateCopy->material);
        }
        surfaceNameByPart[number - 1] = surfaceName;
        plateNameByPart[number - 1] = plateName;
        result.surfaceNames.push_back(surfaceName);
        result.plateNames.push_back(plateName);
    }

    // 開口(窓・ライト)をこの曲げ状態へ写す。
    // 開口は元板材の(真の曲)面上にあるので、角ばった近似メッシュからは
    // 板厚の半分+近似偏差ぶん浮く。その分を許容して対応付ける。
    const double meshTolerance = 1.0
        + (fromSurface
            ? options.surfaceThicknessMillimeters
            : std::max(std::abs(sourcePlateCopy->plate.Thickness()),
                std::abs(sourcePlateCopy->plate.EndThickness())))
        + model.result.maximumDeviationMillimeters;
    const int openingSamples = 64;
    for (std::size_t openingIndex = 0; openingIndex < openings.size(); ++openingIndex) {
        const NamedWire& opening = openings[openingIndex];
        std::vector<Vector3> mapped;
        mapped.reserve(openingSamples + 1);
        std::set<int> bands;
        bool onMesh = true;
        for (int sample = 0; sample < openingSamples; ++sample) {
            const double parameter
                = static_cast<double>(sample) / openingSamples;
            const auto mappedPoint = model::MapPointToPartMeshState(
                mesh, state, opening.wire.Evaluate(parameter));
            if (mappedPoint.distanceMillimeters > meshTolerance) {
                onMesh = false;
                break;
            }
            bands.insert(mappedPoint.band);
            mapped.push_back(mappedPoint.point);
        }
        if (!onMesh || mapped.size() < 3) {
            continue;
        }
        mapped.push_back(mapped.front()); // 閉じる

        const bool singleBand = bands.size() == 1;
        const int owningNumber = singleBand ? *bands.begin() + 1 : 0;
        const bool ownerSelected = singleBand
            && std::find(numbers.begin(), numbers.end(), owningNumber) != numbers.end();
        bool overlapsSelection = false;
        for (const int band : bands) {
            if (std::find(numbers.begin(), numbers.end(), band + 1) != numbers.end()) {
                overlapsSelection = true;
                break;
            }
        }
        if (!overlapsSelection) {
            continue;
        }

        if (ownerSelected) {
            // 1部材に収まる開口は実際の穴として付与する。
            // 帯の法線方向へ少し浮かせた下書きを面へ投影し直す(既存の穴機構に合わせる)。
            try {
                const int band = owningNumber - 1;
                const int centerColumn = mesh.columns / 2;
                const int nextColumn = std::min(centerColumn + 1, mesh.columns - 1);
                Vector3 normal = Normalized(Cross(
                    state[band][nextColumn] - state[band][centerColumn],
                    state[band + 1][centerColumn] - state[band][centerColumn]));
                if (normal.Length() <= 1.0e-9) {
                    normal = {0.0, 0.0, 1.0};
                }
                std::vector<Vector3> lifted = mapped;
                for (Vector3& point : lifted) {
                    point = point + normal * 2.0;
                }
                const std::string draftName = namePrefix + "_穴下書き"
                    + std::to_string(openingIndex + 1);
                const std::string holeName = namePrefix + "_部材"
                    + std::to_string(owningNumber) + "穴"
                    + std::to_string(openingIndex + 1);
                target.AddWire(draftName, Wire::Polyline(std::move(lifted)));
                target.AddProjectedWire(
                    holeName, draftName,
                    surfaceNameByPart[owningNumber - 1], normal * -1.0);
                target.AddPlateOpening(plateNameByPart[owningNumber - 1], holeName);
                target.SetWireVisible(draftName, false);
                result.openingWireNames.push_back(holeName);
                continue;
            } catch (const std::exception&) {
                // 穴にできない場合は輪郭線のみ追加する(下へフォールスルー)。
            }
        }
        const std::string outlineName = namePrefix + "_穴輪郭"
            + std::to_string(openingIndex + 1);
        target.AddWire(outlineName, Wire::Polyline(std::move(mapped)));
        result.outlineWireNames.push_back(outlineName);
    }

    return result;
}

} // namespace kachakacha::io
