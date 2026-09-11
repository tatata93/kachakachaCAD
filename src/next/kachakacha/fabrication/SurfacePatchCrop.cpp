#include "kachakacha/fabrication/SurfacePatch.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kachakacha::v2::fabrication {

using base::MakeError;
using base::Result;

Result<SurfacePatchSamples> CropSamples(const SurfacePatchSamples& samples, double uMin,
    double uMax, double vMin, double vMax)
{
    using Out = Result<SurfacePatchSamples>;
    const auto finite = [](double value) { return std::isfinite(value); };
    if (!finite(uMin) || !finite(uMax) || !finite(vMin) || !finite(vMax) || uMin < 0.0
        || uMax > 1.0 || vMin < 0.0 || vMax > 1.0 || !(uMin < uMax) || !(vMin < vMax)) {
        return Out::Failure(MakeError("FAB-M004",
            "面の範囲は 0〜1 の中で、最小を最大より小さくしてください。",
            "u " + std::to_string(uMin) + "〜" + std::to_string(uMax) + "、v "
                + std::to_string(vMin) + "〜" + std::to_string(vMax)));
    }
    if (!samples.Valid()) {
        return Out::Failure(MakeError("FAB-S002", "面の標本が壊れています。", {}));
    }
    if (uMin == 0.0 && uMax == 1.0 && vMin == 0.0 && vMax == 1.0) {
        return Out::Success(samples);
    }
    // 同じ格子の数のまま、範囲の中を双一次で読み直す。
    const auto evaluate = [&samples](double u, double v) {
        const double columnPosition = u * static_cast<double>(samples.columnCount - 1);
        const double rowPosition = v * static_cast<double>(samples.rowCount - 1);
        const std::size_t column0 = static_cast<std::size_t>(std::floor(columnPosition));
        const std::size_t row0 = static_cast<std::size_t>(std::floor(rowPosition));
        const std::size_t column1 = std::min(column0 + 1, samples.columnCount - 1);
        const std::size_t row1 = std::min(row0 + 1, samples.rowCount - 1);
        const double fu = columnPosition - static_cast<double>(column0);
        const double fv = rowPosition - static_cast<double>(row0);
        const auto& a = samples.At(row0, column0);
        const auto& b = samples.At(row0, column1);
        const auto& c = samples.At(row1, column0);
        const auto& d = samples.At(row1, column1);
        const auto lower = a + (b - a) * fu;
        const auto upper = c + (d - c) * fu;
        return lower + (upper - lower) * fv;
    };
    SurfacePatchSamples cropped;
    cropped.rowCount = samples.rowCount;
    cropped.columnCount = samples.columnCount;
    cropped.points.reserve(samples.points.size());
    for (std::size_t row = 0; row < samples.rowCount; ++row) {
        const double v = vMin
            + (vMax - vMin) * static_cast<double>(row) / static_cast<double>(samples.rowCount - 1);
        for (std::size_t column = 0; column < samples.columnCount; ++column) {
            const double u = uMin
                + (uMax - uMin) * static_cast<double>(column)
                    / static_cast<double>(samples.columnCount - 1);
            cropped.points.push_back(evaluate(std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0)));
        }
    }
    return Out::Success(std::move(cropped));
}

} // namespace kachakacha::v2::fabrication
