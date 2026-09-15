#include "kachakacha/app/SurfacePreview.h"

#include <algorithm>

namespace kachakacha::v2::app {
namespace {

//! 0 から count-1 までのうち、端を必ず含めて最大 maxLines 本を選ぶ。
[[nodiscard]] std::vector<std::size_t> PickIndices(std::size_t count, std::size_t maxLines)
{
    std::vector<std::size_t> picked;
    if (count == 0) {
        return picked;
    }
    if (maxLines < 2 || count <= maxLines) {
        picked.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            picked.push_back(index);
        }
        return picked;
    }
    picked.reserve(maxLines);
    const std::size_t last = count - 1;
    for (std::size_t step = 0; step < maxLines; ++step) {
        const std::size_t index = (step * last) / (maxLines - 1);
        if (picked.empty() || picked.back() != index) {
            picked.push_back(index);
        }
    }
    return picked;
}

} // namespace

PreviewLoops SurfaceGridLines(const SurfacePatchSamples& samples, std::size_t maxLines)
{
    PreviewLoops lines;
    if (!samples.Valid()) {
        return lines;   // 標本が足りない。描かない。
    }
    for (const std::size_t row : PickIndices(samples.rowCount, maxLines)) {
        std::vector<Vector3> line;
        line.reserve(samples.columnCount);
        for (std::size_t column = 0; column < samples.columnCount; ++column) {
            line.push_back(samples.At(row, column));
        }
        lines.push_back(std::move(line));
    }
    for (const std::size_t column : PickIndices(samples.columnCount, maxLines)) {
        std::vector<Vector3> line;
        line.reserve(samples.rowCount);
        for (std::size_t row = 0; row < samples.rowCount; ++row) {
            line.push_back(samples.At(row, column));
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

PreviewLoops SurfaceBoundaryLines(const std::vector<CurveSegment>& boundary, int stepsPerCurve)
{
    PreviewLoops lines;
    const int steps = std::max(1, stepsPerCurve);
    for (const auto& curve : boundary) {
        std::vector<Vector3> line;
        line.reserve(static_cast<std::size_t>(steps) + 1U);
        for (int step = 0; step <= steps; ++step) {
            line.push_back(curve.Evaluate(static_cast<double>(step) / static_cast<double>(steps)));
        }
        if (line.size() >= 2) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

PreviewLoops SurfacePreviewLines(const SurfacePatchSamples& samples,
    const std::vector<CurveSegment>& boundary, std::size_t maxLines)
{
    PreviewLoops lines = SurfaceBoundaryLines(boundary);
    PreviewLoops grid = SurfaceGridLines(samples, maxLines);
    lines.insert(lines.end(), std::make_move_iterator(grid.begin()),
        std::make_move_iterator(grid.end()));
    return lines;
}

} // namespace kachakacha::v2::app
