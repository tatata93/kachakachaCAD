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
