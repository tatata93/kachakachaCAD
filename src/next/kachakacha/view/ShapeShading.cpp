#include "kachakacha/view/ShapeShading.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kachakacha::v2::view {

using geometry::Dot;
using geometry::Normalized;

namespace {

//! 明るさの下限。裏を向いた面を真っ黒にしない。
constexpr double kMinimumShade = 0.25;

} // namespace

Vector3 StandardLightDirection() noexcept
{
    // 左上・手前から当てる(V1 と同じ)。単位ベクトルにしておく。
    return Normalized(Vector3{0.4, 0.35, -0.85});
}

double LambertShade(const Vector3& normal, const Vector3& lightDirection) noexcept
{
    const Vector3 unitNormal = Normalized(normal);
    const Vector3 unitLight = Normalized(lightDirection);
    if (unitNormal == Vector3{} || unitLight == Vector3{}) {
        return kMinimumShade;
    }
    // 光は目からモデルへ向かう向きなので、面が光を向いているとき内積は負。
    // 裏表は問わない ── 開いた面を裏から見ても同じ濃さで出す。
    const double facing = std::abs(Dot(unitNormal, unitLight));
    return kMinimumShade + (1.0 - kMinimumShade) * std::clamp(facing, 0.0, 1.0);
}

bool BackFacing(const MeshTriangle& triangle, const Vector3& viewDirection) noexcept
{
    const Vector3 unitNormal = Normalized(triangle.normal);
    if (unitNormal == Vector3{}) {
        return false;   // 潰れた三角形。裏とは言えない。
    }
    // 視線と同じ向きを向いている = 向こう側を向いている。
    return Dot(unitNormal, Normalized(viewDirection)) > 0.0;
}

std::vector<std::size_t> PainterOrder(const std::vector<MeshTriangle>& triangles,
    const Vector3& viewDirection)
{
    std::vector<std::size_t> order(triangles.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    const Vector3 forward = Normalized(viewDirection);
    if (forward == Vector3{}) {
        return order;
    }
    // 視線に沿った位置が大きいほど奥。奥から先に塗る。
    // stable_sort なので、同じ奥行きのものは入っている順のまま。毎回同じ絵になる。
    std::stable_sort(order.begin(), order.end(),
        [&](std::size_t left, std::size_t right) {
            return Dot(triangles[left].Center(), forward)
                > Dot(triangles[right].Center(), forward);
        });
    return order;
}

} // namespace kachakacha::v2::view
