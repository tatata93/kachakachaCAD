#include "kachakacha/model/PartModel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kachakacha::model {

namespace {

constexpr int kAxisSamples = 96;   // 分割軸方向のサンプル数
constexpr int kCrossSamples = 17;  // 直交方向のサンプル数

// 板厚中央面上の点。tは分割軸方向、sは直交方向(いずれも板材ローカル0..1)。
[[nodiscard]] geometry::Vector3 EvaluateMid(
    const Plate& plate, PartSplitAxis axis, double t, double s)
{
    const double u = axis == PartSplitAxis::V ? s : t;
    const double v = axis == PartSplitAxis::V ? t : s;
    return plate.Evaluate(u, v, 0.5);
}

// 帯 [t0,t1] の「1軸曲げ近似からのずれ」を弦偏差で見積もる。
// 1軸曲げ部材は帯を横切る素線(分割軸方向の曲線)が直線になるため、
// 実曲線と弦との最大距離が近似偏差の見積もりになる。
[[nodiscard]] double EstimateChordDeviation(
    const Plate& plate, PartSplitAxis axis, double t0, double t1)
{
    double worst = 0.0;
    const int innerSamples = 9;
    for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
        const double s = static_cast<double>(crossIndex) / (kCrossSamples - 1);
        const geometry::Vector3 start = EvaluateMid(plate, axis, t0, s);
        const geometry::Vector3 end = EvaluateMid(plate, axis, t1, s);
        const geometry::Vector3 chord = end - start;
        const double chordLengthSquared = chord.LengthSquared();
        for (int inner = 1; inner < innerSamples; ++inner) {
            const double f = static_cast<double>(inner) / innerSamples;
            const double t = t0 + (t1 - t0) * f;
            const geometry::Vector3 point = EvaluateMid(plate, axis, t, s);
            geometry::Vector3 offset = point - start;
            if (chordLengthSquared > 1.0e-18) {
                const double along = Dot(offset, chord) / chordLengthSquared;
                offset = offset - chord * along;
            }
            worst = std::max(worst, offset.Length());
        }
    }
    return worst;
}

// 帯 [t0,t1] の分割軸方向の実幅(直交方向サンプルの平均)。
[[nodiscard]] double MeasureWidth(
    const Plate& plate, PartSplitAxis axis, double t0, double t1)
{
    double total = 0.0;
    const int lengthSamples = 8;
    for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
        const double s = static_cast<double>(crossIndex) / (kCrossSamples - 1);
        double length = 0.0;
        geometry::Vector3 previous = EvaluateMid(plate, axis, t0, s);
        for (int step = 1; step <= lengthSamples; ++step) {
            const double t = t0 + (t1 - t0) * static_cast<double>(step) / lengthSamples;
            const geometry::Vector3 point = EvaluateMid(plate, axis, t, s);
            length += (point - previous).Length();
            previous = point;
        }
        total += length;
    }
    return total / kCrossSamples;
}

[[nodiscard]] ApproximatedPart MakePart(
    const Plate& plate, PartSplitAxis axis, int number, double t0, double t1)
{
    ApproximatedPart part;
    part.number = number;
    part.minimumParameter = t0;
    part.maximumParameter = t1;
    part.widthMillimeters = MeasureWidth(plate, axis, t0, t1);
    part.estimatedDeviationMillimeters = EstimateChordDeviation(plate, axis, t0, t1);
    if (part.estimatedDeviationMillimeters <= 1.0e-6) {
        part.classification = PlateDevelopability::Planar;
    } else {
        part.classification = PlateDevelopability::Developable;
    }
    return part;
}

} // namespace

PartApproximationResult ApproximatePlateParts(
    const Plate& plate,
    const PartApproximationOptions& options)
{
    if (!std::isfinite(options.maximumDeviationMillimeters)
        || options.maximumDeviationMillimeters <= 0.0) {
        throw std::invalid_argument("部材近似の許容偏差は正の値で指定してください。");
    }
    if (options.maximumPartCount < 1) {
        throw std::invalid_argument("部材数の上限は1以上で指定してください。");
    }
    if (!std::isfinite(options.minimumPartWidthMillimeters)
        || options.minimumPartWidthMillimeters < 0.0) {
        throw std::invalid_argument("最小部材幅は0以上で指定してください。");
    }

    const PartSplitAxis axis = options.splitAxis;
    std::vector<double> boundaries;
    boundaries.push_back(0.0);

    if (!options.automaticBoundaries) {
        std::vector<double> manual = options.manualBoundaryParameters;
        std::sort(manual.begin(), manual.end());
        for (const double parameter : manual) {
            if (!std::isfinite(parameter) || parameter <= 0.0 || parameter >= 1.0) {
                throw std::invalid_argument("手動境界のパラメータは0と1の間で指定してください。");
            }
            if (parameter <= boundaries.back() + 1.0e-9) {
                throw std::invalid_argument("手動境界のパラメータが重複しています。");
            }
            boundaries.push_back(parameter);
        }
        boundaries.push_back(1.0);
    } else {
        // 貪欲法: 偏差が許容内に収まる限り帯を伸ばす。
        const double step = 1.0 / kAxisSamples;
        double start = 0.0;
        while (start < 1.0 - 1.0e-9) {
            double end = std::min(1.0, start + step);
            double lastGood = end;
            while (end < 1.0 - 1.0e-9) {
                const double next = std::min(1.0, end + step);
                if (EstimateChordDeviation(plate, axis, start, next)
                    > options.maximumDeviationMillimeters) {
                    break;
                }
                end = next;
                lastGood = next;
            }
            end = lastGood;
            // 最小幅を満たすまで伸ばす(公差超過よりも「作れない細さ」を避ける)。
            while (end < 1.0 - 1.0e-9
                && MeasureWidth(plate, axis, start, end)
                    < options.minimumPartWidthMillimeters) {
                end = std::min(1.0, end + step);
            }
            if (end >= 1.0 - 1.0e-9) {
                end = 1.0;
            }
            boundaries.push_back(end);
            start = end;
        }
        // 上限部材数を超えた場合は等分割へ切り替える(部材数を優先し、偏差は結果で報告)。
        if (static_cast<int>(boundaries.size()) - 1 > options.maximumPartCount) {
            boundaries.clear();
            for (int index = 0; index <= options.maximumPartCount; ++index) {
                boundaries.push_back(
                    static_cast<double>(index) / options.maximumPartCount);
            }
        }
        // 末尾の帯が細すぎる場合は手前の帯と結合する。
        if (boundaries.size() >= 3) {
            const double t0 = boundaries[boundaries.size() - 2];
            if (MeasureWidth(plate, axis, t0, 1.0)
                < options.minimumPartWidthMillimeters) {
                boundaries.erase(boundaries.end() - 2);
            }
        }
    }

    PartApproximationResult result;
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
        ApproximatedPart part = MakePart(
            plate, axis, static_cast<int>(index) + 1,
            boundaries[index], boundaries[index + 1]);
        result.maximumDeviationMillimeters = std::max(
            result.maximumDeviationMillimeters, part.estimatedDeviationMillimeters);
        result.parts.push_back(std::move(part));
    }
    result.reachedRequestedTolerance =
        result.maximumDeviationMillimeters <= options.maximumDeviationMillimeters + 1.0e-9;
    return result;
}

Wire BuildPartBoundaryWire(
    const Plate& plate,
    PartSplitAxis splitAxis,
    double parameter,
    int samples)
{
    if (samples < 2) {
        throw std::invalid_argument("部材境界のサンプル数は2以上で指定してください。");
    }
    std::vector<geometry::Vector3> points;
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int index = 0; index <= samples; ++index) {
        const double s = static_cast<double>(index) / samples;
        points.push_back(EvaluateMid(plate, splitAxis, parameter, s));
    }
    return Wire::Polyline(std::move(points));
}

} // namespace kachakacha::model

namespace kachakacha::model {

namespace {

using geometry::Vector2;
using geometry::Vector3;

[[nodiscard]] Vector3 Normalized(const Vector3& value)
{
    const double length = value.Length();
    if (length <= 1.0e-12) {
        return {0.0, 0.0, 0.0};
    }
    return value * (1.0 / length);
}

//! 既知の2点 a,b(2D)と、その2点からの距離 ra,rb で三角形の第3点を置く。
//! orientation(+1/-1)は cross(b-a, p-a) の符号。三角形の辺長を厳密に保存する。
[[nodiscard]] Vector2 PlaceThirdPoint(
    const Vector2& a, const Vector2& b, double ra, double rb, double orientation)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d <= 1.0e-12) {
        return {a.x + ra, a.y};
    }
    // 円の交点(標準形)。数値誤差で交わらない場合は clamp する。
    const double along = (ra * ra - rb * rb + d * d) / (2.0 * d);
    double perpendicularSquared = ra * ra - along * along;
    if (perpendicularSquared < 0.0) {
        perpendicularSquared = 0.0;
    }
    const double perpendicular = std::sqrt(perpendicularSquared);
    const double ux = dx / d;
    const double uy = dy / d;
    const double sign = orientation >= 0.0 ? 1.0 : -1.0;
    return {
        a.x + ux * along - uy * perpendicular * sign,
        a.y + uy * along + ux * perpendicular * sign,
    };
}

[[nodiscard]] double Distance3(const Vector3& a, const Vector3& b)
{
    return (b - a).Length();
}

} // namespace

PartMeshDevelopment DevelopPartMesh(
    const Plate& plate,
    PartSplitAxis splitAxis,
    const std::vector<double>& railParameters,
    int columns)
{
    if (railParameters.size() < 2) {
        throw std::invalid_argument("展開にはレールが2本以上必要です。");
    }
    if (columns < 2) {
        throw std::invalid_argument("展開の列数は2以上で指定してください。");
    }
    for (std::size_t index = 1; index < railParameters.size(); ++index) {
        if (railParameters[index] <= railParameters[index - 1] + 1.0e-12) {
            throw std::invalid_argument("レールのパラメータは昇順で指定してください。");
        }
    }

    PartMeshDevelopment mesh;
    mesh.rows = static_cast<int>(railParameters.size());
    mesh.columns = columns + 1;

    // 近似の実形状: 各レールは実面上の曲線、レール間は直線補間(ルールド)。
    // メッシュ自体はレール点列で表す(レール間の直線は展開時の三角形が担う)。
    const auto evaluateMid = [&plate, splitAxis](double t, double s) {
        const double u = splitAxis == PartSplitAxis::V ? s : t;
        const double v = splitAxis == PartSplitAxis::V ? t : s;
        return plate.Evaluate(u, v, 0.5);
    };
    mesh.world.resize(mesh.rows);
    for (int row = 0; row < mesh.rows; ++row) {
        mesh.world[row].reserve(mesh.columns);
        for (int column = 0; column < mesh.columns; ++column) {
            const double s = static_cast<double>(column) / columns;
            mesh.world[row].push_back(evaluateMid(railParameters[row], s));
        }
    }

    // 展開: 帯(隣り合うレール間)ごとに三角形単位の等長配置で厳密に展開する。
    // 1帯のストリップは全頂点が輪郭上にあるため、辺長を保存した展開が常に存在する。
    // 複数帯は帯ごとに展開し、共有レールの形で剛体合わせして積む(角度欠損=
    // 折り線まわりの誤差は局所に留まり、蓄積しない)。
    mesh.developed.assign(mesh.rows, std::vector<Vector2>(mesh.columns, {0.0, 0.0}));

    const auto developBand = [&mesh](int bottomRow) {
        // 帯 bottomRow → bottomRow+1 を、左端の素線を基準にクアッド行進で展開する。
        const auto& bottom3 = mesh.world[bottomRow];
        const auto& top3 = mesh.world[bottomRow + 1];
        std::vector<Vector2> bottom2(mesh.columns);
        std::vector<Vector2> top2(mesh.columns);
        bottom2[0] = {0.0, 0.0};
        top2[0] = {0.0, Distance3(bottom3[0], top3[0])};
        for (int column = 1; column < mesh.columns; ++column) {
            // 三角形 (B_{c-1}, T_{c-1}, B_c): 下辺と対角線を保存。
            bottom2[column] = PlaceThirdPoint(
                bottom2[column - 1], top2[column - 1],
                Distance3(bottom3[column - 1], bottom3[column]),
                Distance3(top3[column - 1], bottom3[column]),
                -1.0);
            // 三角形 (T_{c-1}, B_c, T_c): 上辺と素線を保存。
            top2[column] = PlaceThirdPoint(
                top2[column - 1], bottom2[column],
                Distance3(top3[column - 1], top3[column]),
                Distance3(bottom3[column], top3[column]),
                +1.0);
        }
        return std::pair<std::vector<Vector2>, std::vector<Vector2>>{
            std::move(bottom2), std::move(top2)};
    };

    // 2Dの剛体合わせ(回転+平行移動、鏡映なし)。
    const auto rigidAlign = [](const std::vector<Vector2>& from,
                               const std::vector<Vector2>& to,
                               std::vector<Vector2>& apply) {
        double fromCenterX = 0.0, fromCenterY = 0.0, toCenterX = 0.0, toCenterY = 0.0;
        const double count = static_cast<double>(from.size());
        for (std::size_t index = 0; index < from.size(); ++index) {
            fromCenterX += from[index].x;
            fromCenterY += from[index].y;
            toCenterX += to[index].x;
            toCenterY += to[index].y;
        }
        fromCenterX /= count; fromCenterY /= count;
        toCenterX /= count; toCenterY /= count;
        double dotSum = 0.0, crossSum = 0.0;
        for (std::size_t index = 0; index < from.size(); ++index) {
            const double ax = from[index].x - fromCenterX;
            const double ay = from[index].y - fromCenterY;
            const double bx = to[index].x - toCenterX;
            const double by = to[index].y - toCenterY;
            dotSum += ax * bx + ay * by;
            crossSum += ax * by - ay * bx;
        }
        const double angle = std::atan2(crossSum, dotSum);
        const double cosAngle = std::cos(angle);
        const double sinAngle = std::sin(angle);
        for (Vector2& point : apply) {
            const double px = point.x - fromCenterX;
            const double py = point.y - fromCenterY;
            point = {
                toCenterX + px * cosAngle - py * sinAngle,
                toCenterY + px * sinAngle + py * cosAngle,
            };
        }
    };

    for (int band = 0; band + 1 < mesh.rows; ++band) {
        auto [bottom2, top2] = developBand(band);
        if (band == 0) {
            mesh.developed[0] = bottom2;
            mesh.developed[1] = top2;
            continue;
        }
        // 共有レール(この帯の下辺)を、既に置かれたレールへ剛体で合わせる。
        std::vector<Vector2> alignedTop = top2;
        rigidAlign(bottom2, mesh.developed[band], alignedTop);
        mesh.developed[band + 1] = std::move(alignedTop);
    }

    // 内部レールの山谷: 前後の帯の面法線の二面角の符号。
    mesh.creaseDirections.assign(std::max(0, mesh.rows - 2), 0);
    for (int rail = 1; rail + 1 < mesh.rows; ++rail) {
        double accumulated = 0.0;
        for (int column = 1; column + 1 < mesh.columns; column += 4) {
            const Vector3 axis = Normalized(
                mesh.world[rail][column + 1] - mesh.world[rail][column - 1]);
            const Vector3 toPrevious = mesh.world[rail - 1][column] - mesh.world[rail][column];
            const Vector3 toNext = mesh.world[rail + 1][column] - mesh.world[rail][column];
            const Vector3 previousNormal = Normalized(Cross(axis, toPrevious));
            const Vector3 nextNormal = Normalized(Cross(toNext, axis));
            // 折れの符号: 隣り合う帯が法線側へ谷になるか山になるか。
            const double bend = Dot(Cross(previousNormal, nextNormal), axis);
            accumulated += bend;
        }
        if (accumulated > 1.0e-9) {
            mesh.creaseDirections[rail - 1] = 1;
        } else if (accumulated < -1.0e-9) {
            mesh.creaseDirections[rail - 1] = -1;
        }
    }
    return mesh;
}

std::vector<std::vector<Vector3>> BuildFoldPreview(
    const PartMeshDevelopment& mesh,
    double progress)
{
    const double t = std::clamp(progress, 0.0, 1.0);
    std::vector<std::vector<Vector3>> result(
        mesh.rows, std::vector<Vector3>(mesh.columns, {0.0, 0.0, 0.0}));
    if (mesh.rows == 0 || mesh.columns == 0) {
        return result;
    }

    // 展開形状(z=0)を実形状の位置・向きへ剛体で合わせてから、頂点ごとに補間する。
    // 厳密な等長変形ではないが、平面状態と折り曲げ状態の対応確認には十分。
    // 合わせ込み: 展開の行0中央付近と実形状の同位置・接線方向を一致させる。
    const int anchorColumn = mesh.columns / 2;
    const int nextColumn = std::min(anchorColumn + 1, mesh.columns - 1);
    const Vector3 worldAnchor = mesh.world[0][anchorColumn];
    const Vector3 worldTangent = Normalized(mesh.world[0][nextColumn] - mesh.world[0][anchorColumn]);
    Vector3 worldUp{0.0, 0.0, 0.0};
    if (mesh.rows > 1) {
        worldUp = mesh.world[1][anchorColumn] - mesh.world[0][anchorColumn];
    }
    Vector3 worldNormal = Normalized(Cross(worldTangent, worldUp));
    if (worldNormal.Length() <= 1.0e-9) {
        worldNormal = {0.0, 0.0, 1.0};
    }
    const Vector3 worldSide = Normalized(Cross(worldNormal, worldTangent));

    const Vector2 developedAnchor = mesh.developed[0][anchorColumn];
    const Vector2 developedNext = mesh.developed[0][nextColumn];
    double axisX = developedNext.x - developedAnchor.x;
    double axisY = developedNext.y - developedAnchor.y;
    const double axisLength = std::sqrt(axisX * axisX + axisY * axisY);
    if (axisLength > 1.0e-12) {
        axisX /= axisLength;
        axisY /= axisLength;
    } else {
        axisX = 1.0;
        axisY = 0.0;
    }

    for (int row = 0; row < mesh.rows; ++row) {
        for (int column = 0; column < mesh.columns; ++column) {
            const Vector2& flat = mesh.developed[row][column];
            const double dx = flat.x - developedAnchor.x;
            const double dy = flat.y - developedAnchor.y;
            const double along = dx * axisX + dy * axisY;
            const double side = -dx * axisY + dy * axisX;
            const Vector3 flatWorld = worldAnchor
                + worldTangent * along + worldSide * side;
            const Vector3& folded = mesh.world[row][column];
            result[row][column] = flatWorld * (1.0 - t) + folded * t;
        }
    }
    return result;
}

} // namespace kachakacha::model
