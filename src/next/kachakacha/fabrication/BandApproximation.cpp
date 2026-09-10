#include "kachakacha/fabrication/BandApproximation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;

constexpr int kAxisSamples = 96;  //!< 分割軸方向の刻み
constexpr int kCrossSamples = 17; //!< 直交方向の刻み

//! 帯 [t0,t1] の「1軸曲げ近似からのずれ」を弦偏差で見積もる。
//! 1軸曲げの帯は、帯を横切る素線(分割軸方向の曲線)が直線になる。
//! だから実曲線と弦との最大距離が、近似偏差の見積もりになる。
[[nodiscard]] double EstimateChordDeviation(const SampledSurface& source,
    BandSplitAxis axis, double t0, double t1)
{
    double worst = 0.0;
    const int innerSamples = 9;
    for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
        const double s = static_cast<double>(crossIndex) / (kCrossSamples - 1);
        const Vector3 start = source.EvaluateSplit(axis, t0, s);
        const Vector3 end = source.EvaluateSplit(axis, t1, s);
        const Vector3 chord = end - start;
        const double chordLengthSquared = chord.LengthSquared();
        for (int inner = 1; inner < innerSamples; ++inner) {
            const double f = static_cast<double>(inner) / innerSamples;
            const Vector3 point = source.EvaluateSplit(axis, t0 + (t1 - t0) * f, s);
            Vector3 offset = point - start;
            if (chordLengthSquared > 1.0e-18) {
                offset = offset - chord * (Dot(offset, chord) / chordLengthSquared);
            }
            worst = std::max(worst, offset.Length());
        }
    }
    return worst;
}

//! 帯 [t0,t1] の分割軸方向の実幅(直交方向の標本の平均)。
[[nodiscard]] double MeasureWidth(const SampledSurface& source, BandSplitAxis axis,
    double t0, double t1)
{
    double total = 0.0;
    const int lengthSamples = 8;
    for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
        const double s = static_cast<double>(crossIndex) / (kCrossSamples - 1);
        double length = 0.0;
        Vector3 previous = source.EvaluateSplit(axis, t0, s);
        for (int step = 1; step <= lengthSamples; ++step) {
            const double t = t0 + (t1 - t0) * static_cast<double>(step) / lengthSamples;
            const Vector3 point = source.EvaluateSplit(axis, t, s);
            length += (point - previous).Length();
            previous = point;
        }
        total += length;
    }
    return total / kCrossSamples;
}

[[nodiscard]] ApproximatedBand MakeBand(const SampledSurface& source, BandSplitAxis axis,
    int number, double t0, double t1)
{
    ApproximatedBand band;
    band.number = number;
    band.minimumParameter = t0;
    band.maximumParameter = t1;
    band.widthMm = MeasureWidth(source, axis, t0, t1);
    band.estimatedDeviationMm = EstimateChordDeviation(source, axis, t0, t1);
    band.planar = band.estimatedDeviationMm <= 1.0e-6;
    return band;
}

//! 貪欲法で境界を置く。偏差が許容内に収まる限り帯を伸ばす。
[[nodiscard]] std::vector<double> GreedyBoundaries(const SampledSurface& source,
    const BandApproximationOptions& options)
{
    const BandSplitAxis axis = options.splitAxis;
    std::vector<double> boundaries{0.0};
    const double step = 1.0 / kAxisSamples;
    double start = 0.0;
    while (start < 1.0 - 1.0e-9) {
        double end = std::min(1.0, start + step);
        double lastGood = end;
        while (end < 1.0 - 1.0e-9) {
            const double next = std::min(1.0, end + step);
            if (EstimateChordDeviation(source, axis, start, next)
                > options.maximumDeviationMm) {
                break;
            }
            end = next;
            lastGood = next;
        }
        end = lastGood;
        // 最小幅を満たすまで伸ばす。公差超過よりも「作れない細さ」を避ける。
        while (end < 1.0 - 1.0e-9
            && MeasureWidth(source, axis, start, end) < options.minimumPartWidthMm) {
            end = std::min(1.0, end + step);
        }
        if (end >= 1.0 - 1.0e-9) {
            end = 1.0;
        }
        boundaries.push_back(end);
        start = end;
    }
    // 上限部材数を超えたら等分割へ切り替える。部材数を優先し、偏差は結果で報告する。
    if (static_cast<int>(boundaries.size()) - 1 > options.maximumPartCount) {
        boundaries.clear();
        for (int index = 0; index <= options.maximumPartCount; ++index) {
            boundaries.push_back(static_cast<double>(index) / options.maximumPartCount);
        }
    }
    // 末尾の帯が細すぎたら、手前の帯と結合する。
    if (boundaries.size() >= 3) {
        const double t0 = boundaries[boundaries.size() - 2];
        if (MeasureWidth(source, axis, t0, 1.0) < options.minimumPartWidthMm) {
            boundaries.erase(boundaries.end() - 2);
        }
    }
    return boundaries;
}

//! 手動境界を検査して並べる。
[[nodiscard]] Result<std::vector<double>> ManualBoundaries(
    const BandApproximationOptions& options)
{
    using Out = Result<std::vector<double>>;
    std::vector<double> manual = options.manualBoundaries;
    std::sort(manual.begin(), manual.end());
    std::vector<double> boundaries{0.0};
    for (const double parameter : manual) {
        if (!std::isfinite(parameter) || parameter <= 0.0 || parameter >= 1.0) {
            return Out::Failure(MakeError(kBandBadBoundary,
                "手動境界のパラメータは 0 と 1 の間で指定してください。", {}));
        }
        if (parameter <= boundaries.back() + 1.0e-9) {
            return Out::Failure(MakeError(kBandBadBoundary,
                "手動境界のパラメータが重複しています。", {}));
        }
        boundaries.push_back(parameter);
    }
    boundaries.push_back(1.0);
    return Out::Success(std::move(boundaries));
}

} // namespace

std::string_view BandSplitAxisNameJa(BandSplitAxis axis) noexcept
{
    return axis == BandSplitAxis::U ? "u 方向で切る" : "v 方向で切る";
}

SampledSurface::SampledSurface(const SurfacePatchSamples& samples)
    : samples_(samples)
{
}

Vector3 SampledSurface::Evaluate(double u, double v) const
{
    if (!samples_.Valid()) {
        return Vector3{};
    }
    const double clampedU = std::clamp(u, 0.0, 1.0);
    const double clampedV = std::clamp(v, 0.0, 1.0);
    const double columnPosition = clampedU * static_cast<double>(samples_.columnCount - 1);
    const double rowPosition = clampedV * static_cast<double>(samples_.rowCount - 1);
    const std::size_t column0 = static_cast<std::size_t>(std::floor(columnPosition));
    const std::size_t row0 = static_cast<std::size_t>(std::floor(rowPosition));
    const std::size_t column1 = std::min(column0 + 1, samples_.columnCount - 1);
    const std::size_t row1 = std::min(row0 + 1, samples_.rowCount - 1);
    const double fu = columnPosition - static_cast<double>(column0);
    const double fv = rowPosition - static_cast<double>(row0);
    const Vector3 a = samples_.At(row0, column0);
    const Vector3 b = samples_.At(row0, column1);
    const Vector3 c = samples_.At(row1, column0);
    const Vector3 d = samples_.At(row1, column1);
    const Vector3 lower = a + (b - a) * fu;
    const Vector3 upper = c + (d - c) * fu;
    return lower + (upper - lower) * fv;
}

Vector3 SampledSurface::EvaluateSplit(BandSplitAxis axis, double t, double s) const
{
    const double u = axis == BandSplitAxis::V ? s : t;
    const double v = axis == BandSplitAxis::V ? t : s;
    return Evaluate(u, v);
}

Result<BandApproximationResult> ApproximateBands(const SampledSurface& source,
    const BandApproximationOptions& options)
{
    using Out = Result<BandApproximationResult>;
    if (!source.Valid()) {
        return Out::Failure(MakeError(kBandBadSamples, "面の標本が足りません。",
            "行と列がそれぞれ2つ以上要ります。"));
    }
    if (!std::isfinite(options.maximumDeviationMm) || options.maximumDeviationMm <= 0.0) {
        return Out::Failure(MakeError(kBandBadOptions,
            "帯近似の許容偏差は正の値で指定してください。", {}));
    }
    if (options.maximumPartCount < 1) {
        return Out::Failure(MakeError(kBandBadOptions,
            "部材数の上限は 1 以上で指定してください。", {}));
    }
    if (!std::isfinite(options.minimumPartWidthMm) || options.minimumPartWidthMm < 0.0) {
        return Out::Failure(MakeError(kBandBadOptions,
            "最小部材幅は 0 以上で指定してください。", {}));
    }
    std::vector<double> boundaries;
    if (options.automaticBoundaries) {
        boundaries = GreedyBoundaries(source, options);
    } else {
        const auto manual = ManualBoundaries(options);
        if (!manual.HasValue()) {
            return Out::Failure(manual.Diagnostics());
        }
        boundaries = manual.Value();
    }
    BandApproximationResult result;
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
        ApproximatedBand band = MakeBand(source, options.splitAxis,
            static_cast<int>(index) + 1, boundaries[index], boundaries[index + 1]);
        result.maximumDeviationMm =
            std::max(result.maximumDeviationMm, band.estimatedDeviationMm);
        result.bands.push_back(std::move(band));
    }
    result.reachedRequestedTolerance =
        result.maximumDeviationMm <= options.maximumDeviationMm + 1.0e-9;
    result.railParameters = std::move(boundaries);
    return Out::Success(std::move(result));
}

double MeasureAxisDeviation(const SampledSurface& source, BandSplitAxis axis)
{
    return source.Valid() ? EstimateChordDeviation(source, axis, 0.0, 1.0) : 0.0;
}

BandSplitAxis ChooseSplitAxis(const SampledSurface& source)
{
    // 曲がっている方向を横切るように切る。切る軸に沿って面が真っ直ぐなら、
    // いくら切っても帯は曲がらないまま(1枚で済んでしまう)。
    const double alongU = MeasureAxisDeviation(source, BandSplitAxis::U);
    const double alongV = MeasureAxisDeviation(source, BandSplitAxis::V);
    return alongU > alongV + 1.0e-9 ? BandSplitAxis::U : BandSplitAxis::V;
}

std::vector<Vector3> BuildBandBoundary(const SampledSurface& source, BandSplitAxis axis,
    double parameter, int samples)
{
    std::vector<Vector3> points;
    if (samples < 2) {
        return points;
    }
    points.reserve(static_cast<std::size_t>(samples) + 1);
    for (int index = 0; index <= samples; ++index) {
        const double s = static_cast<double>(index) / samples;
        points.push_back(source.EvaluateSplit(axis, parameter, s));
    }
    return points;
}

} // namespace kachakacha::v2::fabrication
