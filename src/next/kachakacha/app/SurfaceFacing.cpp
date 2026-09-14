#include "kachakacha/app/SurfaceFacing.h"

#include <cstddef>

namespace kachakacha::v2::app {
namespace {

using geometry::Cross;
using geometry::Vector3;

//! 格子の (row, column) の点。範囲の外なら値を返さない。
[[nodiscard]] const Vector3* At(const fabrication::SurfacePatchSamples& samples,
    std::size_t row, std::size_t column)
{
    if (row >= samples.rowCount || column >= samples.columnCount) {
        return nullptr;
    }
    const std::size_t index = row * samples.columnCount + column;
    return index < samples.points.size() ? &samples.points[index] : nullptr;
}

//! (row, column) を中心に、行方向と列方向の接線から向きを出す。
[[nodiscard]] std::optional<SurfacePose> PoseAt(
    const fabrication::SurfacePatchSamples& samples, std::size_t row, std::size_t column)
{
    const Vector3* center = At(samples, row, column);
    const Vector3* nextRow = At(samples, row + 1, column);
    const Vector3* nextColumn = At(samples, row, column + 1);
    if (center == nullptr || nextRow == nullptr || nextColumn == nullptr) {
        return std::nullopt;
    }
    const Vector3 alongRow = *nextRow - *center;
    const Vector3 alongColumn = *nextColumn - *center;
    const Vector3 normal = Cross(alongColumn, alongRow);
    if (!(normal.LengthSquared() > 1.0e-18)) {
        return std::nullopt;   // 潰れている。向きが決まらない。
    }
    SurfacePose pose;
    pose.point = *center;
    pose.normal = normal;
    pose.uAxis = alongColumn;
    return pose;
}

} // namespace

std::optional<SurfacePose> SurfaceFacingPose(
    const fabrication::SurfacePatchSamples& samples)
{
    if (samples.rowCount < 2 || samples.columnCount < 2) {
        return std::nullopt;
    }
    const std::size_t middleRow = samples.rowCount / 2;
    const std::size_t middleColumn = samples.columnCount / 2;
    if (const auto pose = PoseAt(samples, middleRow, middleColumn); pose.has_value()) {
        return pose;
    }
    // 真ん中が潰れている(円錐の頂点など)。周りを順に見る。
    for (std::size_t row = 0; row + 1 < samples.rowCount; ++row) {
        for (std::size_t column = 0; column + 1 < samples.columnCount; ++column) {
            if (const auto pose = PoseAt(samples, row, column); pose.has_value()) {
                return pose;
            }
        }
    }
    return std::nullopt;
}

} // namespace kachakacha::v2::app
