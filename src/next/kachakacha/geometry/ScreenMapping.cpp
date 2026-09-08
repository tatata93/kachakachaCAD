#include "kachakacha/geometry/ScreenMapping.h"

#include <cmath>

namespace kachakacha::v2::geometry {

namespace {

struct Clip {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
};

[[nodiscard]] Clip Multiply(const std::array<double, 16>& matrix, const Vector3& point,
    double w)
{
    Clip clip;
    clip.x = matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + matrix[3] * w;
    clip.y = matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z + matrix[7] * w;
    clip.z = matrix[8] * point.x + matrix[9] * point.y + matrix[10] * point.z + matrix[11] * w;
    clip.w = matrix[12] * point.x + matrix[13] * point.y + matrix[14] * point.z
        + matrix[15] * w;
    return clip;
}

//! 4x4 の逆行列。ガウスの消去法。特異なら値を返さない。
[[nodiscard]] std::optional<std::array<double, 16>> Invert(const std::array<double, 16>& m)
{
    double a[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            a[row][column] = m[static_cast<std::size_t>(row * 4 + column)];
        }
        a[row][4 + row] = 1.0;
    }
    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
            if (std::abs(a[row][column]) > std::abs(a[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(a[pivot][column]) < 1.0e-15) {
            return std::nullopt;
        }
        if (pivot != column) {
            for (int index = 0; index < 8; ++index) {
                std::swap(a[column][index], a[pivot][index]);
            }
        }
        const double scale = a[column][column];
        for (int index = 0; index < 8; ++index) {
            a[column][index] /= scale;
        }
        for (int row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = a[row][column];
            if (factor == 0.0) {
                continue;
            }
            for (int index = 0; index < 8; ++index) {
                a[row][index] -= factor * a[column][index];
            }
        }
    }
    std::array<double, 16> inverse{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            inverse[static_cast<std::size_t>(row * 4 + column)] = a[row][4 + column];
        }
    }
    return inverse;
}

} // namespace

std::optional<ScreenPoint> ScreenMapping::Project(const Vector3& world) const
{
    const Clip clip = Multiply(matrix, world, 1.0);
    if (!(clip.w > 1.0e-12)) {
        return std::nullopt;   // カメラの後ろ、または面上
    }
    const double ndcX = clip.x / clip.w;
    const double ndcY = clip.y / clip.w;
    ScreenPoint screen;
    screen.x = (ndcX * 0.5 + 0.5) * widthPx;
    screen.y = (0.5 - ndcY * 0.5) * heightPx;   // 画面のyは下向き
    if (!IsFinite(screen.x) || !IsFinite(screen.y)) {
        return std::nullopt;
    }
    return screen;
}

std::optional<ScreenMapping::Ray> ScreenMapping::RayThrough(const ScreenPoint& screen) const
{
    const auto inverse = Invert(matrix);
    if (!inverse.has_value()) {
        return std::nullopt;
    }
    const double ndcX = (screen.x / widthPx) * 2.0 - 1.0;
    const double ndcY = 1.0 - (screen.y / heightPx) * 2.0;
    const auto unproject = [&](double ndcZ) -> std::optional<Vector3> {
        const Clip clip = Multiply(*inverse, Vector3{ndcX, ndcY, ndcZ}, 1.0);
        if (std::abs(clip.w) < 1.0e-15) {
            return std::nullopt;
        }
        return Vector3{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
    };
    const auto nearPoint = unproject(-1.0);
    const auto farPoint = unproject(1.0);
    if (!nearPoint.has_value() || !farPoint.has_value()) {
        return std::nullopt;
    }
    Ray ray;
    ray.origin = *nearPoint;
    ray.direction = geometry::Normalized(*farPoint - *nearPoint);
    return ray;
}

std::optional<Vector3> ScreenMapping::UnprojectOntoPlane(const ScreenPoint& screen,
    const Vector3& planeOrigin, const Vector3& planeNormal) const
{
    const auto ray = RayThrough(screen);
    if (!ray.has_value()) {
        return std::nullopt;
    }
    const double denominator = Dot(ray->direction, planeNormal);
    if (std::abs(denominator) < 1.0e-12) {
        return std::nullopt;   // 視線が平面と平行
    }
    const double t = Dot(planeOrigin - ray->origin, planeNormal) / denominator;
    return ray->origin + ray->direction * t;
}

double ScreenMapping::PixelsPerMillimeterAt(const Vector3& world) const
{
    const auto center = Project(world);
    if (!center.has_value()) {
        return 0.0;
    }
    // 画面の横方向へ 1mm ずらした点との距離。向きは行列の第1行から取る。
    const Vector3 right{matrix[0], matrix[1], matrix[2]};
    const Vector3 direction = geometry::Normalized(right);
    const auto shifted = Project(world + direction);
    if (!shifted.has_value()) {
        return 0.0;
    }
    return ScreenDistance(*center, *shifted);
}

ScreenMapping MakeOrthographicMapping(Vector3 center, Vector3 forward, Vector3 up,
    double visibleWidthMm, double widthPx, double heightPx)
{
    const Vector3 f = geometry::Normalized(forward);
    Vector3 r = Cross(f, up);
    if (!(r.Length() > 0.0)) {
        r = Cross(f, Vector3{0.0, 0.0, 1.0});
    }
    r = geometry::Normalized(r);
    const Vector3 u = geometry::Normalized(Cross(r, f));
    const double halfWidth = visibleWidthMm * 0.5;
    const double aspect = heightPx > 0.0 ? widthPx / heightPx : 1.0;
    const double halfHeight = aspect > 0.0 ? halfWidth / aspect : halfWidth;
    const double depth = std::max(visibleWidthMm * 10.0, 1.0);

    ScreenMapping mapping;
    mapping.widthPx = widthPx;
    mapping.heightPx = heightPx;
    // 行1: 右方向 / halfWidth、行2: 上方向 / halfHeight、行3: 前方向 / depth、行4: w=1。
    mapping.matrix = {
        r.x / halfWidth, r.y / halfWidth, r.z / halfWidth, -Dot(r, center) / halfWidth,
        u.x / halfHeight, u.y / halfHeight, u.z / halfHeight, -Dot(u, center) / halfHeight,
        f.x / depth, f.y / depth, f.z / depth, -Dot(f, center) / depth,
        0.0, 0.0, 0.0, 1.0,
    };
    return mapping;
}

ScreenMapping MakePerspectiveMapping(Vector3 eye, Vector3 target, Vector3 up,
    double verticalFieldOfViewRad, double widthPx, double heightPx, double nearMm,
    double farMm)
{
    const Vector3 f = geometry::Normalized(target - eye);
    Vector3 r = Cross(f, up);
    if (!(r.Length() > 0.0)) {
        r = Cross(f, Vector3{0.0, 0.0, 1.0});
    }
    r = geometry::Normalized(r);
    const Vector3 u = geometry::Normalized(Cross(r, f));
    const double aspect = heightPx > 0.0 ? widthPx / heightPx : 1.0;
    const double focal = 1.0 / std::tan(verticalFieldOfViewRad * 0.5);
    const double range = nearMm - farMm;

    ScreenMapping mapping;
    mapping.widthPx = widthPx;
    mapping.heightPx = heightPx;
    const double sx = focal / aspect;
    const double sy = focal;
    const double sz = (farMm + nearMm) / -range;
    const double tz = 2.0 * farMm * nearMm / -range;
    mapping.matrix = {
        sx * r.x, sx * r.y, sx * r.z, -sx * Dot(r, eye),
        sy * u.x, sy * u.y, sy * u.z, -sy * Dot(u, eye),
        sz * f.x, sz * f.y, sz * f.z, -sz * Dot(f, eye) + tz,
        f.x, f.y, f.z, -Dot(f, eye),
    };
    return mapping;
}

double ScreenDistance(const ScreenPoint& first, const ScreenPoint& second)
{
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace kachakacha::v2::geometry
