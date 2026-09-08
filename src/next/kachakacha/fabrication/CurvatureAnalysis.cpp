#include "kachakacha/fabrication/CurvatureAnalysis.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::fabrication {

using base::MakeError;
using base::Result;
using geometry::Normalized;

namespace {

constexpr const char* kBadSamples = "FAB-S002";

//! 中央差分。端は片側差分にする。
[[nodiscard]] Vector3 Derivative(const Vector3& previous, const Vector3& next, double step)
{
    return (next - previous) * (1.0 / (2.0 * step));
}

} // namespace

std::string_view PanelGeometryClassNameJa(PanelGeometryClass value) noexcept
{
    switch (value) {
    case PanelGeometryClass::Planar:             return "平面";
    case PanelGeometryClass::Cylindrical:        return "円筒";
    case PanelGeometryClass::Conical:            return "円錐";
    case PanelGeometryClass::TangentDevelopable: return "接線曲面";
    case PanelGeometryClass::DoubleCurved:       return "二重曲率";
    }
    return "不明";
}

double GaussianToleranceFor(double targetMaxDeviationMm, double representativeLengthMm)
{
    const double deviation = targetMaxDeviationMm > 0.0 ? targetMaxDeviationMm : 1.0e-3;
    const double length = representativeLengthMm > 0.0 ? representativeLengthMm : 1.0;
    return 8.0 * deviation / (length * length * length);
}

Result<CurvatureAnalysis> AnalyzeCurvature(const SurfacePatchSamples& samples,
    double targetMaxDeviationMm)
{
    if (!samples.Valid()) {
        return Result<CurvatureAnalysis>::Failure(MakeError(kBadSamples,
            "面の標本が壊れています。",
            "行 " + std::to_string(samples.rowCount) + " × 列 "
                + std::to_string(samples.columnCount) + "、点 "
                + std::to_string(samples.points.size()) + " 個。"));
    }
    if (samples.rowCount < 3 || samples.columnCount < 3) {
        return Result<CurvatureAnalysis>::Failure(MakeError(kBadSamples,
            "曲がり方を測るには標本が足りません。",
            "3×3 以上が必要です。"));
    }
    for (const Vector3& point : samples.points) {
        if (!point.IsFinite()) {
            return Result<CurvatureAnalysis>::Failure(MakeError(kBadSamples,
                "面の標本に有限でない数が入っています。", {}));
        }
    }

    // 代表長さ。対角線の長さで測る。
    const Vector3 corner00 = samples.At(0, 0);
    const Vector3 cornerNM = samples.At(samples.rowCount - 1, samples.columnCount - 1);
    const Vector3 corner0M = samples.At(0, samples.columnCount - 1);
    const Vector3 cornerN0 = samples.At(samples.rowCount - 1, 0);
    const double representative =
        std::max((cornerNM - corner00).Length(), (cornerN0 - corner0M).Length());
    const double gaussianTolerance =
        GaussianToleranceFor(targetMaxDeviationMm, representative);
    // 主曲率をどこまで0とみなすか。半径がこの長さの100倍を超えたら平らとみなす。
    const double principalTolerance = representative > 0.0 ? 1.0 / (representative * 100.0)
                                                           : 1.0e-9;

    CurvatureAnalysis analysis;
    analysis.samples.reserve((samples.rowCount - 2) * (samples.columnCount - 2));
    std::size_t doubleCurved = 0;
    std::size_t counted = 0;

    for (std::size_t row = 1; row + 1 < samples.rowCount; ++row) {
        for (std::size_t column = 1; column + 1 < samples.columnCount; ++column) {
            const Vector3 center = samples.At(row, column);
            const Vector3 up = samples.At(row - 1, column);
            const Vector3 down = samples.At(row + 1, column);
            const Vector3 left = samples.At(row, column - 1);
            const Vector3 right = samples.At(row, column + 1);

            // 格子の刻みは一定とは限らないので、実距離を刻みにする。
            const double stepU = 0.5 * ((down - center).Length() + (center - up).Length());
            const double stepV = 0.5 * ((right - center).Length() + (center - left).Length());
            if (!(stepU > 0.0) || !(stepV > 0.0)) {
                continue;
            }
            const Vector3 du = Derivative(up, down, stepU);
            const Vector3 dv = Derivative(left, right, stepV);
            const Vector3 normal = Normalized(Cross(du, dv));
            if (!(normal.Length() > 0.0)) {
                continue;   // 退化。ここでは測らない
            }
            // 2階差分。
            const Vector3 duu = (down - center * 2.0 + up) * (1.0 / (stepU * stepU));
            const Vector3 dvv = (right - center * 2.0 + left) * (1.0 / (stepV * stepV));
            const Vector3 upLeft = samples.At(row - 1, column - 1);
            const Vector3 upRight = samples.At(row - 1, column + 1);
            const Vector3 downLeft = samples.At(row + 1, column - 1);
            const Vector3 downRight = samples.At(row + 1, column + 1);
            const Vector3 duv = (downRight - downLeft - upRight + upLeft)
                * (1.0 / (4.0 * stepU * stepV));

            // 第1基本形式と第2基本形式。
            const double E = Dot(du, du);
            const double F = Dot(du, dv);
            const double G = Dot(dv, dv);
            const double L = Dot(duu, normal);
            const double M = Dot(duv, normal);
            const double N = Dot(dvv, normal);
            const double denominator = E * G - F * F;
            if (!(std::abs(denominator) > 1.0e-18)) {
                continue;
            }
            CurvatureSample sample;
            sample.row = row;
            sample.column = column;
            sample.gaussian = (L * N - M * M) / denominator;
            sample.mean = (E * N - 2.0 * F * M + G * L) / (2.0 * denominator);
            const double discriminant = sample.mean * sample.mean - sample.gaussian;
            const double root = discriminant > 0.0 ? std::sqrt(discriminant) : 0.0;
            const double k1 = sample.mean + root;
            const double k2 = sample.mean - root;
            if (std::abs(k1) >= std::abs(k2)) {
                sample.firstPrincipal = k1;
                sample.secondPrincipal = k2;
            } else {
                sample.firstPrincipal = k2;
                sample.secondPrincipal = k1;
            }
            sample.valid = true;
            ++counted;
            if (std::abs(sample.gaussian) > gaussianTolerance) {
                ++doubleCurved;
            }
            if (std::abs(sample.gaussian) > analysis.maximumAbsoluteGaussian) {
                analysis.maximumAbsoluteGaussian = std::abs(sample.gaussian);
                analysis.worstRow = row;
                analysis.worstColumn = column;
            }
            analysis.maximumAbsolutePrincipal = std::max(analysis.maximumAbsolutePrincipal,
                std::abs(sample.firstPrincipal));
            analysis.samples.push_back(sample);
        }
    }
    if (counted == 0) {
        return Result<CurvatureAnalysis>::Failure(MakeError(kBadSamples,
            "曲がり方を測れませんでした。",
            "面が潰れているか、標本が一直線に並んでいます。"));
    }
    analysis.doubleCurvedRatio =
        static_cast<double>(doubleCurved) / static_cast<double>(counted);

    // 種類を決める。
    if (analysis.maximumAbsoluteGaussian > gaussianTolerance) {
        analysis.classification = PanelGeometryClass::DoubleCurved;
        return Result<CurvatureAnalysis>::Success(std::move(analysis));
    }
    if (analysis.maximumAbsolutePrincipal <= principalTolerance) {
        analysis.classification = PanelGeometryClass::Planar;
        return Result<CurvatureAnalysis>::Success(std::move(analysis));
    }
    // Gauss曲率は0。曲がっているほうの主曲率が場所によってどう変わるかで分ける。
    // 一定なら円筒、母線に沿って一次に変わるなら円錐、それ以外は接線曲面。
    double smallest = analysis.samples.front().firstPrincipal;
    double largest = smallest;
    for (const CurvatureSample& sample : analysis.samples) {
        smallest = std::min(smallest, std::abs(sample.firstPrincipal));
        largest = std::max(largest, std::abs(sample.firstPrincipal));
    }
    const double spread = largest > 0.0 ? (largest - smallest) / largest : 0.0;
    if (spread <= 0.05) {
        analysis.classification = PanelGeometryClass::Cylindrical;
    } else {
        // 円錐は 1/半径 が母線方向へ一次に変わる。半径そのものが一次に変わるので、
        // 曲率の逆数が直線に乗るかどうかで見る。
        double minimumRadius = 0.0;
        double maximumRadius = 0.0;
        bool first = true;
        double sumRadius = 0.0;
        for (const CurvatureSample& sample : analysis.samples) {
            if (!(std::abs(sample.firstPrincipal) > 0.0)) {
                continue;
            }
            const double radius = 1.0 / std::abs(sample.firstPrincipal);
            sumRadius += radius;
            if (first) {
                minimumRadius = radius;
                maximumRadius = radius;
                first = false;
            } else {
                minimumRadius = std::min(minimumRadius, radius);
                maximumRadius = std::max(maximumRadius, radius);
            }
        }
        analysis.classification = maximumRadius > minimumRadius
            ? PanelGeometryClass::Conical
            : PanelGeometryClass::TangentDevelopable;
        (void)sumRadius;
    }
    return Result<CurvatureAnalysis>::Success(std::move(analysis));
}

} // namespace kachakacha::v2::fabrication
