#pragma once

//! 画面への写し方。Qt にも OpenGL にも依存しない。
//!
//! スナップは「画面上で何px離れているか」で決まるので、幾何の側が
//! 画面座標を知っている必要がある。ここでは 4x4 行列1枚として持つ。
//! 平行投影でも透視投影でも同じ形で扱えるので、視点の種類で場合分けしない。

#include "kachakacha/geometry/Vector3.h"

#include <array>
#include <optional>

namespace kachakacha::v2::geometry {

struct ScreenPoint {
    double x = 0.0;
    double y = 0.0;
};

//! 世界座標 -> クリップ座標の行列と、画面の大きさ。
//! 行列は行優先で持つ。matrix[row * 4 + column]。
struct ScreenMapping {
    std::array<double, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    double widthPx = 1000.0;
    double heightPx = 1000.0;

    //! 画面座標へ。カメラの後ろにある点は値を返さない。
    [[nodiscard]] std::optional<ScreenPoint> Project(const Vector3& world) const;

    //! 画面座標から、指定した平面上の点へ戻す。
    [[nodiscard]] std::optional<Vector3> UnprojectOntoPlane(const ScreenPoint& screen,
        const Vector3& planeOrigin, const Vector3& planeNormal) const;

    //! 画面のこの位置から伸びる視線。
    struct Ray {
        Vector3 origin{};
        Vector3 direction{};
    };
    [[nodiscard]] std::optional<Ray> RayThrough(const ScreenPoint& screen) const;

    //! その点のあたりで 1mm が何px になるか。グリッドの間引きに使う。
    [[nodiscard]] double PixelsPerMillimeterAt(const Vector3& world) const;
};

//! 平行投影を作る。中心、向き、画面に収める幅(mm)から。
[[nodiscard]] ScreenMapping MakeOrthographicMapping(Vector3 center, Vector3 forward,
    Vector3 up, double visibleWidthMm, double widthPx, double heightPx);

//! 透視投影を作る。
[[nodiscard]] ScreenMapping MakePerspectiveMapping(Vector3 eye, Vector3 target, Vector3 up,
    double verticalFieldOfViewRad, double widthPx, double heightPx, double nearMm,
    double farMm);

[[nodiscard]] double ScreenDistance(const ScreenPoint& first, const ScreenPoint& second);

} // namespace kachakacha::v2::geometry
