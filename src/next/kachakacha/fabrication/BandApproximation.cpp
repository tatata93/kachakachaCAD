#include "kachakacha/fabrication/BandApproximation.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

//! 貪欲法で区間 [from, to] に境界を置く。偏差が許容内に収まる限り帯を伸ばす。
//! 返す並びは from で始まり to で終わる。
[[nodiscard]] std::vector<double> GreedyBoundaries(const SampledSurface& source,
    const BandApproximationOptions& options, double from, double to)
{
    const BandSplitAxis axis = options.splitAxis;
    std::vector<double> boundaries{from};
    const double step = (to - from) / kAxisSamples;
    double start = from;
    while (start < to - 1.0e-9) {
        double end = std::min(to, start + step);
        double lastGood = end;
        while (end < to - 1.0e-9) {
            const double next = std::min(to, end + step);
            if (EstimateChordDeviation(source, axis, start, next)
                > options.maximumDeviationMm) {
                break;
            }
            end = next;
            lastGood = next;
        }
        end = lastGood;
        // 最小幅を満たすまで伸ばす。公差超過よりも「作れない細さ」を避ける。
        while (end < to - 1.0e-9
            && MeasureWidth(source, axis, start, end) < options.minimumPartWidthMm) {
            end = std::min(to, end + step);
        }
        if (end >= to - 1.0e-9) {
            end = to;
        }
        boundaries.push_back(end);
        start = end;
    }
    // 上限部材数を超えたら等分割へ切り替える。部材数を優先し、偏差は結果で報告する。
    if (static_cast<int>(boundaries.size()) - 1 > options.maximumPartCount) {
        boundaries.clear();
        for (int index = 0; index <= options.maximumPartCount; ++index) {
            boundaries.push_back(from + (to - from) * index / options.maximumPartCount);
        }
    }
    // 末尾の帯が細すぎたら、手前の帯と結合する。
    if (boundaries.size() >= 3) {
        const double t0 = boundaries[boundaries.size() - 2];
        if (MeasureWidth(source, axis, t0, to) < options.minimumPartWidthMm) {
            boundaries.erase(boundaries.end() - 2);
        }
    }
    return boundaries;
}

//! 区間 [from, to] を実幅で count 等分する境界(from と to を含む)。
[[nodiscard]] std::vector<double> EqualWidthBoundaries(const SampledSurface& source,
    BandSplitAxis axis, double from, double to, int count)
{
    std::vector<double> boundaries{from};
    if (count <= 1 || to - from <= 1.0e-12) {
        boundaries.push_back(to);
        return boundaries;
    }
    // 細かい刻みで幅を積み、分位点でパラメータを決める(パラメータの等分では幅がそろわない)。
    constexpr int kSteps = 192;
    std::vector<double> cumulative(kSteps + 1, 0.0);
    for (int step = 1; step <= kSteps; ++step) {
        const double t0 = from + (to - from) * (step - 1) / kSteps;
        const double t1 = from + (to - from) * step / kSteps;
        cumulative[static_cast<std::size_t>(step)] =
            cumulative[static_cast<std::size_t>(step) - 1] + MeasureWidth(source, axis, t0, t1);
    }
    const double total = cumulative.back();
    for (int k = 1; k < count; ++k) {
        const double target = total * k / count;
        std::size_t index = 1;
        while (index < cumulative.size() - 1 && cumulative[index] < target) {
            ++index;
        }
        const double before = cumulative[index - 1];
        const double after = cumulative[index];
        const double f = after > before ? (target - before) / (after - before) : 0.0;
        boundaries.push_back(from + (to - from) * (static_cast<double>(index) - 1.0 + f) / kSteps);
    }
    boundaries.push_back(to);
    return boundaries;
}

//! 区間ごとに枚数を配る(幅に応じて。各区間 1 枚以上)。
[[nodiscard]] std::vector<int> DistributeCount(const std::vector<double>& widths, int total)
{
    const int segments = static_cast<int>(widths.size());
    std::vector<int> counts(static_cast<std::size_t>(segments), 1);
    if (total <= segments) {
        return counts;
    }
    double sum = 0.0;
    for (const double width : widths) {
        sum += width;
    }
    int remaining = total;
    for (std::size_t k = 0; k < widths.size(); ++k) {
        const int want = sum > 0.0 ? std::max(1, static_cast<int>(std::lround(total * widths[k] / sum))) : 1;
        counts[k] = want;
        remaining -= want;
    }
    // 丸めで合計がずれたら、いちばん幅の広い区間で調整する(1 枚は割らない)。
    while (remaining != 0) {
        std::size_t pick = 0;
        for (std::size_t k = 1; k < widths.size(); ++k) {
            const double ratioPick = widths[pick] / counts[pick];
            const double ratioK = widths[k] / counts[k];
            if (remaining > 0 ? ratioK > ratioPick : (counts[k] > 1 && (counts[pick] <= 1 || ratioK < ratioPick))) {
                pick = k;
            }
        }
        if (remaining < 0 && counts[pick] <= 1) {
            break;
        }
        counts[pick] += remaining > 0 ? 1 : -1;
        remaining += remaining > 0 ? -1 : 1;
    }
    return counts;
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
    if (!std::isfinite(options.cornerAngleDeg) || options.cornerAngleDeg <= 0.0
        || options.cornerAngleDeg >= 180.0) {
        return Out::Failure(MakeError(kBandBadOptions,
            "角とみなす折れの角度は 0 より大きく 180 未満で指定してください。", {}));
    }
    // 角(縁の折れ)で必ず割る。区間 [角_i, 角_i+1] ごとに、貪欲 / 枚数 / 手動で境界を置く。
    std::vector<double> corners;
    if (options.splitAtCorners) {
        corners = CornerParameters(source, options.splitAxis, options.cornerAngleDeg);
    }
    std::vector<double> segments{0.0};
    segments.insert(segments.end(), corners.begin(), corners.end());
    segments.push_back(1.0);
    std::vector<double> boundaries;
    if (options.equalPartCount > 0) {
        std::vector<double> widths;
        for (std::size_t k = 0; k + 1 < segments.size(); ++k) {
            widths.push_back(MeasureWidth(source, options.splitAxis, segments[k], segments[k + 1]));
        }
        const std::vector<int> counts = DistributeCount(widths, options.equalPartCount);
        for (std::size_t k = 0; k + 1 < segments.size(); ++k) {
            auto piece = EqualWidthBoundaries(source, options.splitAxis, segments[k],
                segments[k + 1], counts[k]);
            if (!boundaries.empty()) {
                piece.erase(piece.begin());
            }
            boundaries.insert(boundaries.end(), piece.begin(), piece.end());
        }
    } else if (options.automaticBoundaries) {
        for (std::size_t k = 0; k + 1 < segments.size(); ++k) {
            auto piece = GreedyBoundaries(source, options, segments[k], segments[k + 1]);
            if (!boundaries.empty()) {
                piece.erase(piece.begin());
            }
            boundaries.insert(boundaries.end(), piece.begin(), piece.end());
        }
    } else {
        const auto manual = ManualBoundaries(options);
        if (!manual.HasValue()) {
            return Out::Failure(manual.Diagnostics());
        }
        boundaries = manual.Value();
        // 角は手動境界に足す(ほぼ同じ所にあれば足さない)。
        for (const double corner : corners) {
            bool alreadyThere = false;
            for (const double existing : boundaries) {
                alreadyThere = alreadyThere || std::abs(existing - corner) < 1.0e-3;
            }
            if (!alreadyThere) {
                boundaries.push_back(corner);
            }
        }
        std::sort(boundaries.begin(), boundaries.end());
    }
    BandApproximationResult result;
    result.cornerParameters = corners;
    result.narrowestWidthMm = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
        ApproximatedBand band = MakeBand(source, options.splitAxis,
            static_cast<int>(index) + 1, boundaries[index], boundaries[index + 1]);
        result.maximumDeviationMm =
            std::max(result.maximumDeviationMm, band.estimatedDeviationMm);
        result.narrowestWidthMm = std::min(result.narrowestWidthMm, band.widthMm);
        result.bands.push_back(std::move(band));
    }
    if (!std::isfinite(result.narrowestWidthMm)) {
        result.narrowestWidthMm = 0.0;
    }
    result.narrowerThanMinimum = result.narrowestWidthMm < options.minimumPartWidthMm - 1.0e-9;
    result.reachedRequestedTolerance =
        result.maximumDeviationMm <= options.maximumDeviationMm + 1.0e-9;
    result.railParameters = std::move(boundaries);
    return Out::Success(std::move(result));
}

std::string_view BandSplitDirectionNameJa(BandSplitDirection direction) noexcept
{
    switch (direction) {
    case BandSplitDirection::U:          return "U 方向で切る";
    case BandSplitDirection::V:          return "V 方向で切る";
    case BandSplitDirection::Auto:       return "自動(曲がっている方向を横切る)";
    case BandSplitDirection::Vertical:   return "縦に割る(上下に走る線で)";
    case BandSplitDirection::Horizontal: return "横に割る(水平に走る線で)";
    }
    return "自動";
}

double MeasureRailVerticality(const SampledSurface& source, BandSplitAxis axis)
{
    if (!source.Valid()) {
        return 0.0;
    }
    // レール t = 一定 を s に沿ってたどり、上下の動き / 道のり を平均する。
    constexpr int kRails = 7;
    constexpr int kSteps = 24;
    double total = 0.0;
    int counted = 0;
    for (int r = 0; r < kRails; ++r) {
        const double t = static_cast<double>(r) / (kRails - 1);
        double climb = 0.0;
        double length = 0.0;
        Vector3 previous = source.EvaluateSplit(axis, t, 0.0);
        for (int step = 1; step <= kSteps; ++step) {
            const Vector3 point = source.EvaluateSplit(axis, t, static_cast<double>(step) / kSteps);
            climb += std::abs(point.z - previous.z);
            length += (point - previous).Length();
            previous = point;
        }
        if (length > 1.0e-9) {
            total += climb / length;
            ++counted;
        }
    }
    return counted > 0 ? total / counted : 0.0;
}

BandSplitAxis ResolveSplitAxis(const SampledSurface& source, BandSplitDirection direction)
{
    switch (direction) {
    case BandSplitDirection::U:
        return BandSplitAxis::U;
    case BandSplitDirection::V:
        return BandSplitAxis::V;
    case BandSplitDirection::Auto:
        return ChooseSplitAxis(source);
    case BandSplitDirection::Vertical:
    case BandSplitDirection::Horizontal: {
        const double alongU = MeasureRailVerticality(source, BandSplitAxis::U);
        const double alongV = MeasureRailVerticality(source, BandSplitAxis::V);
        if (std::abs(alongU - alongV) < 0.05) {
            return ChooseSplitAxis(source);   // どちらも同じくらい: 曲がりで決める
        }
        const bool uMoreVertical = alongU > alongV;
        return (direction == BandSplitDirection::Vertical) == uMoreVertical ? BandSplitAxis::U
                                                                            : BandSplitAxis::V;
    }
    }
    return ChooseSplitAxis(source);
}

std::vector<double> CornerParameters(const SampledSurface& source, BandSplitAxis axis,
    double cornerAngleDeg)
{
    std::vector<double> corners;
    if (!source.Valid()) {
        return corners;
    }
    constexpr int kSteps = 96;
    const double limit = std::cos(cornerAngleDeg * 3.14159265358979323846 / 180.0);
    for (const double s : {0.0, 1.0}) {
        std::vector<Vector3> edge;
        for (int step = 0; step <= kSteps; ++step) {
            edge.push_back(source.EvaluateSplit(axis, static_cast<double>(step) / kSteps, s));
        }
        double total = 0.0;
        for (std::size_t k = 1; k < edge.size(); ++k) {
            total += (edge[k] - edge[k - 1]).Length();
        }
        const double minimumChord = total * 1.0e-3;
        for (std::size_t k = 1; k + 1 < edge.size(); ++k) {
            const Vector3 in = edge[k] - edge[k - 1];
            const Vector3 out = edge[k + 1] - edge[k];
            const double a = in.Length();
            const double b = out.Length();
            if (a < minimumChord || b < minimumChord) {
                continue;
            }
            if (Dot(in, out) / (a * b) < limit) {
                corners.push_back(static_cast<double>(k) / kSteps);
            }
        }
    }
    std::sort(corners.begin(), corners.end());
    // 同じ角が両縁で見つかる、または隣の刻みで 2 度見つかる → 1 つにまとめる。
    std::vector<double> merged;
    for (const double corner : corners) {
        if (corner <= 1.0e-6 || corner >= 1.0 - 1.0e-6) {
            continue;
        }
        if (!merged.empty() && corner - merged.back() <= 2.0 / kSteps + 1.0e-9) {
            continue;
        }
        merged.push_back(corner);
    }
    return merged;
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
