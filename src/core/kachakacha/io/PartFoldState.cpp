#include "kachakacha/io/PartFoldState.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

//! 面の上へ点を「置き直す」。帯の境目で切った辺は元の曲面から弦のように離れるため、
//! そのままだと投影線が面を外れて穴が作れない。いちばん近い場所を探して面の上へ寄せ、
//! ほんの少しだけ内側へ入れておくと、真上から下ろした投影が必ず当たる。
[[nodiscard]] Vector3 SnapPointOntoSurface(
    const model::Surface& surface,
    const Vector3& point,
    double inset)
{
    double bestU = 0.5;
    double bestV = 0.5;
    double lowU = 0.0;
    double highU = 1.0;
    double lowV = 0.0;
    double highV = 1.0;
    for (int pass = 0; pass < 4; ++pass) {
        const int steps = pass == 0 ? 24 : 8;
        double best = std::numeric_limits<double>::max();
        for (int iu = 0; iu <= steps; ++iu) {
            const double u = lowU + (highU - lowU) * iu / steps;
            for (int iv = 0; iv <= steps; ++iv) {
                const double v = lowV + (highV - lowV) * iv / steps;
                const double distance = (surface.Evaluate(u, v) - point).LengthSquared();
                if (distance < best) {
                    best = distance;
                    bestU = u;
                    bestV = v;
                }
            }
        }
        const double spanU = (highU - lowU) / steps;
        const double spanV = (highV - lowV) / steps;
        lowU = std::max(0.0, bestU - spanU);
        highU = std::min(1.0, bestU + spanU);
        lowV = std::max(0.0, bestV - spanV);
        highV = std::min(1.0, bestV + spanV);
    }
    const double clampedU = std::clamp(bestU, inset, 1.0 - inset);
    const double clampedV = std::clamp(bestV, inset, 1.0 - inset);
    return surface.Evaluate(clampedU, clampedV);
}

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
    const std::vector<std::string>& sourceOpeningNames = fromSurface
        ? sourceSurfaceCopy->openingWireNames
        : sourcePlateCopy->openingWireNames;
    for (const std::string& openingName : sourceOpeningNames) {
        for (const NamedWire& wire : source.Wires()) {
            if (wire.name == openingName) {
                openings.push_back(wire);
                break;
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
    // 可動折り線(合意10)・剛体折り: 進行度>0 では各帯そのものは変形させず、
    // 帯ごとの剛体変換で折り角だけを変えた「常に正しい」形状を実体化する。
    // 進行度=0 だけは従来どおり平面に置いた展開状態(型紙そのもの)。
    // 完成形(全ての実効進行度が1)はレールを隣の部材と共有し、それ以外は
    // 帯ごとの縁を持つ(曲がった折り線では隙間が出るが形は正確)。
    std::vector<double> effective(
        static_cast<std::size_t>(std::max(0, mesh.rows - 2)), progress);
    for (std::size_t index = 0;
         index < effective.size() && index < model.railFoldProgress.size(); ++index) {
        effective[index] = progress * model.railFoldProgress[index];
    }
    const bool flatState = progress <= 1.0e-12;
    const bool worldState = !flatState && std::all_of(
        effective.begin(), effective.end(),
        [](double value) { return std::abs(value - 1.0) <= 1.0e-12; });
    const bool detached = !flatState && !worldState;
    std::vector<std::vector<Vector3>> state;
    std::vector<model::PartBandTransform> bandTransforms;
    if (flatState) {
        state = model::BuildFoldPreview(mesh, 0.0);
    } else if (worldState) {
        state = mesh.world;
    } else {
        bandTransforms = model::BuildRigidBandTransforms(mesh, effective);
    }
    // 帯 band の行 row(=band または band+1)・列 column の点。
    // 画面に出ている帯レールが渡されていれば、それをそのまま使う
    // (見えている形と出てくる形を一致させる)。
    const int bandCount = std::max(0, mesh.rows - 1);
    const bool useRails = bandCount > 0
        && static_cast<int>(options.bandRails.size()) == bandCount * 2;
    const auto statePoint = [&](int band, int row, int column) {
        if (useRails) {
            return options.bandRails[
                static_cast<std::size_t>(band * 2 + (row - band))][
                    static_cast<std::size_t>(column)];
        }
        return detached
            ? bandTransforms[static_cast<std::size_t>(band)].Apply(mesh.world[row][column])
            : state[row][column];
    };
    const auto stateRail = [&](int band, int row) {
        if (useRails) {
            return options.bandRails[static_cast<std::size_t>(band * 2 + (row - band))];
        }
        if (!detached) {
            return state[row];
        }
        std::vector<Vector3> rail;
        rail.reserve(mesh.columns);
        for (const Vector3& point : mesh.world[row]) {
            rail.push_back(bandTransforms[static_cast<std::size_t>(band)].Apply(point));
        }
        return rail;
    };

    PartFoldStateResult result;

    // レールのワイヤを作る。実体化した部材は物理的に別々の部品なので、
    // 完成形・平面でも隣とワイヤを共有せず、常に部材ごとの縁2本を持つ
    // (オーナー指示: 近似して分割した部材類は独立して扱えるように)。
    std::vector<std::string> bottomRailByPart(partCount);
    std::vector<std::string> topRailByPart(partCount);
    for (const int number : numbers) {
        const int band = number - 1;
        const std::string bottomName = namePrefix + "_部材"
            + std::to_string(number) + "縁1";
        const std::string topName = namePrefix + "_部材"
            + std::to_string(number) + "縁2";
        target.AddWire(bottomName, Wire::Polyline(stateRail(band, band)));
        target.AddWire(topName, Wire::Polyline(stateRail(band, band + 1)));
        bottomRailByPart[band] = bottomName;
        topRailByPart[band] = topName;
        result.railWireNames.push_back(bottomName);
        result.railWireNames.push_back(topName);
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
            surfaceName, bottomRailByPart[number - 1], topRailByPart[number - 1]);
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
        // 元の面の上での「分割方向の位置」を測り、帯の範囲で多角形として切り出す。
        // 区間ごとに切ると、3つ以上の帯にまたがる窓の真ん中が切り抜かれずに
        // 板が残る(オーナー報告「内側に謎の面」)。
        std::vector<Vector3> sourceLoop;
        std::vector<double> sourceParameters;
        sourceLoop.reserve(openingSamples);
        sourceParameters.reserve(openingSamples);
        const model::Surface& referenceSurface = fromSurface
            ? sourceSurfaceCopy->surface
            : sourcePlateCopy->plate.SourceSurface();
        const bool splitAlongV = model.options.splitAxis == model::PartSplitAxis::V;
        bool measured = opening.projection.has_value();
        if (measured) {
            for (int sample = 0; sample < openingSamples; ++sample) {
                const Vector3 point
                    = opening.wire.Evaluate(static_cast<double>(sample) / openingSamples);
                try {
                    const model::SurfaceProjection hit
                        = referenceSurface.ProjectPointAlongDirection(
                            point, opening.projection->direction);
                    sourceLoop.push_back(point);
                    sourceParameters.push_back(splitAlongV ? hit.v : hit.u);
                } catch (const std::exception&) {
                    measured = false;
                    break;
                }
            }
        }
        if (!measured || sourceLoop.size() < 3) {
            continue;
        }
        const std::vector<model::BandLoopPiece> sourcePieces
            = model::ClipClosedLoopIntoBands(sourceLoop, sourceParameters, parameters);
        // 切り出した取り分を、いまの曲げ状態の上へ移す。
        std::vector<model::BandLoopPiece> pieces;
        for (const model::BandLoopPiece& sourcePiece : sourcePieces) {
            if (std::find(numbers.begin(), numbers.end(), sourcePiece.band + 1)
                == numbers.end()) {
                continue; // 出力しない部材の取り分は作らない。
            }
            model::BandLoopPiece moved;
            moved.band = sourcePiece.band;
            moved.points.reserve(sourcePiece.points.size());
            bool onMesh = true;
            std::vector<std::vector<Vector3>> railState;
            if (useRails) {
                railState = mesh.world;
                railState[static_cast<std::size_t>(sourcePiece.band)]
                    = options.bandRails[static_cast<std::size_t>(sourcePiece.band * 2)];
                railState[static_cast<std::size_t>(sourcePiece.band + 1)]
                    = options.bandRails[static_cast<std::size_t>(sourcePiece.band * 2 + 1)];
            }
            // 境目で切った辺は元の曲面から弦のように離れるので、
            // 遠い点が少しあるだけで取り分ごと捨ててはいけない
            //(捨てると窓そのものが出力から消える)。
            int farPoints = 0;
            for (const Vector3& point : sourcePiece.points) {
                auto mappedPoint = model::MapPointToPartMeshState(mesh, mesh.world, point);
                if (useRails) {
                    mappedPoint = model::MapPointToPartMeshState(mesh, railState, point);
                } else if (detached) {
                    mappedPoint.point = bandTransforms[
                        static_cast<std::size_t>(mappedPoint.band)].Apply(mappedPoint.point);
                } else {
                    mappedPoint = model::MapPointToPartMeshState(mesh, state, point);
                }
                if (mappedPoint.distanceMillimeters > meshTolerance) {
                    ++farPoints;
                }
                moved.points.push_back(mappedPoint.point);
            }
            onMesh = moved.points.size() >= 3
                && farPoints * 2 <= static_cast<int>(moved.points.size());
            if (onMesh) {
                pieces.push_back(std::move(moved));
            }
        }
        if (pieces.empty()) {
            continue;
        }
        int pieceNumber = 0;
        bool anyHole = false;
        for (const model::BandLoopPiece& piece : pieces) {
            const int owningNumber = piece.band + 1;
            ++pieceNumber;
            if (piece.points.size() < 3) {
                continue;
            }
            std::vector<Vector3> closed = piece.points;
            closed.push_back(closed.front());
            // 帯の法線方向へ少し浮かせた下書きを面へ投影し直す(既存の穴機構に合わせる)。
            try {
                const int band = piece.band;
                const int centerColumn = mesh.columns / 2;
                const int nextColumn = std::min(centerColumn + 1, mesh.columns - 1);
                Vector3 normal = Normalized(Cross(
                    statePoint(band, band, nextColumn) - statePoint(band, band, centerColumn),
                    statePoint(band, band + 1, centerColumn)
                        - statePoint(band, band, centerColumn)));
                if (normal.Length() <= 1.0e-9) {
                    normal = {0.0, 0.0, 1.0};
                }
                const std::string suffix = std::to_string(openingIndex + 1)
                    + (pieces.size() > 1 ? "_" + std::to_string(pieceNumber) : std::string());
                const std::string draftName = namePrefix + "_穴下書き" + suffix;
                const std::string holeName = namePrefix + "_部材"
                    + std::to_string(owningNumber) + "穴" + suffix;
                // 境目で切った取り分は端が帯の縁ぎりぎりに来るので、
                // 投影線が面を外れることがある。少しだけ内側へ縮めて作り直す。
                Vector3 centroid{0.0, 0.0, 0.0};
                for (const Vector3& point : piece.points) {
                    centroid = centroid + point;
                }
                centroid = centroid * (1.0 / static_cast<double>(piece.points.size()));
                bool made = false;
                std::string lastFailure;
                const model::Surface* targetSurface = nullptr;
                for (const auto& candidate : target.Surfaces()) {
                    if (candidate.name == surfaceNameByPart[owningNumber - 1]) {
                        targetSurface = &candidate.surface;
                        break;
                    }
                }
                for (const double shrink : {0.0, 0.004, 0.012, 0.03, 0.08}) {
                    std::vector<Vector3> lifted;
                    lifted.reserve(closed.size());
                    for (const Vector3& point : closed) {
                        Vector3 onSurface = centroid + (point - centroid) * (1.0 - shrink);
                        if (targetSurface != nullptr) {
                            onSurface = SnapPointOntoSurface(*targetSurface, onSurface, 0.004 + shrink);
                        }
                        lifted.push_back(onSurface + normal * 2.0);
                    }
                    try {
                        target.AddWire(draftName, Wire::Polyline(std::move(lifted)));
                        target.AddProjectedWire(
                            holeName, draftName,
                            surfaceNameByPart[owningNumber - 1], normal * -1.0);
                        target.AddPlateOpening(plateNameByPart[owningNumber - 1], holeName);
                        target.SetWireVisible(draftName, false);
                        made = true;
                        break;
                    } catch (const std::exception& error) {
                        lastFailure = error.what();
                        // 作りかけの下書き・穴を片付けてから次の縮み具合で試す。
                        try {
                            (void)target.RemoveWire(holeName);
                        } catch (const std::exception&) {
                        }
                        try {
                            (void)target.RemoveWire(draftName);
                        } catch (const std::exception&) {
                        }
                    }
                }
                if (!made) {
                    throw std::runtime_error(lastFailure.empty()
                            ? std::string("hole could not be projected")
                            : lastFailure);
                }
                result.openingWireNames.push_back(holeName);
                anyHole = true;
            } catch (const std::exception&) {
                // 穴にできない断片は輪郭線だけ足す。
                const std::string outlineName = namePrefix + "_穴輪郭"
                    + std::to_string(openingIndex + 1) + "_"
                    + std::to_string(pieceNumber);
                target.AddWire(outlineName, Wire::Polyline(std::move(closed)));
                result.outlineWireNames.push_back(outlineName);
            }
        }
        (void)anyHole;
    }

    return result;
}

} // namespace kachakacha::io
