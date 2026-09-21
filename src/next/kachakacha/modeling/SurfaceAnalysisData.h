#pragma once

//! 面の解析に使う標本(核が面から取り、画面が色を塗る)。OCCT の型を出さない(AT-ARC-001)。
//!
//! 面を UV の格子で細かく取り、各点で「本当の面の」法線と曲率を持つ。三角形の平らな
//! 法線ではなく面の法線を使うので、ゼブラの縞が三角形の段々にならない。

#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ShapeMesh.h"

#include <array>
#include <vector>

namespace kachakacha::v2::modeling {

//! 解析の三角形 1 枚。角ごとに面の法線と曲率を持つ。
struct AnalysisTriangle {
    //! 位置と平らな法線(奥行きの並べ替え・裏向きの判定に使う)。
    MeshTriangle triangle;
    //! 角での面の単位法線(面の向きどおり)。
    std::array<Vector3, 3> normals{};
    //! 角でのガウス曲率 K(1/mm²)と平均曲率 H(1/mm)。
    std::array<double, 3> gaussian{};
    std::array<double, 3> mean{};
};

//! 曲率コームの歯 1 本(縁の上の点)。
struct CombSample {
    Vector3 point{};
    //! 曲率の中心へ向く単位ベクトル(曲がっていなければ 0)。
    Vector3 towardCenter{};
    //! 曲率(1/mm)。
    double curvature = 0.0;
};

struct SurfaceAnalysisData {
    std::vector<AnalysisTriangle> triangles;
    //! U 方向・V 方向の線(面の内側だけ)。
    std::vector<std::vector<Vector3>> isoLines;
    //! 縁ごとの曲率コーム。
    std::vector<std::vector<CombSample>> combs;
    //! 面の大きさの目安(外接箱の対角、mm)。
    double sizeMm = 0.0;

    [[nodiscard]] bool Empty() const noexcept { return triangles.empty(); }
};

//! 縁 1 本と隣の面とのつながり(境目の連続の表示)。
struct EdgeContinuitySample {
    std::vector<Vector3> polyline;
    //! 隣の面が見つかったか。見つからなければ縁は外周(開いた縁)。
    bool hasNeighbor = false;
    //! 縁と隣の面の縁の離れ(mm)、法線の角度の最大(度)、法曲率の差の最大(1/mm)。
    double gapMm = 0.0;
    double angleDeg = 0.0;
    double curvatureDifference = 0.0;
};

//! 入力の線 1 本に沿った、面からの離れ(入力線からのずれの表示)。
struct DeviationSample {
    std::vector<Vector3> points;
    std::vector<double> distancesMm;
};

} // namespace kachakacha::v2::modeling
