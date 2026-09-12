#pragma once

//! 形を画面で掴む(棚卸し A-2)。
//!
//! 立体と面が画面に出るようになると、次は「それを押して選べるか」である。
//! 線は線の当たり判定(`app/Selection`)で拾えるが、塗った面の**内側**を押しても
//! 何も起きないと、見えているのに掴めないことになる。
//!
//! やり方は光線と三角形の交わり(Möller–Trumbore)。目から画面の点へ伸ばした
//! 光線が、どの形のどの三角形に、どれだけ手前で当たるかを見る。
//!
//! 通常選択にはいちばん手前だけを返す。Tab / Altで明示的に選び直せるよう、
//! 形ごとの命中候補を手前から並べる入口も持つ。

#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ShapeMesh.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace kachakacha::v2::modeling {

//! 当たったところ。
struct MeshHit {
    //! 渡した並びの何番目の形か。
    std::size_t shapeIndex = 0;
    //! その形の何番目の三角形か。面を選ぶ足がかりになる。
    std::size_t triangleIndex = 0;
    //! 目からの距離(mm)。手前ほど小さい。
    double distanceMm = 0.0;
    //! 当たった場所。
    Vector3 point{};
};

//! 光線が三角形に当たるか。当たれば目からの距離。裏からでも当たる。
//!
//! 裏を拾うのは、開いた面(立体でないもの)を裏から見て押すことがあるためである。
//! 表だけにすると、裏返っている面は見えているのに掴めない。
[[nodiscard]] std::optional<double> RayHitsTriangle(const Vector3& origin,
    const Vector3& direction, const MeshTriangle& triangle);

//! いちばん手前で当たる形。当たらなければ値を持たない。
[[nodiscard]] std::optional<MeshHit> PickMesh(const std::vector<ShapeMesh>& shapes,
    const Vector3& origin, const Vector3& direction);

//! 光線に当たる形を、各形のいちばん手前の命中点で代表し、手前から順に返す。
//! 同じ形の三角形を大量の候補にしない。
[[nodiscard]] std::vector<MeshHit> CollectMeshHits(const std::vector<ShapeMesh>& shapes,
    const Vector3& origin, const Vector3& direction);

} // namespace kachakacha::v2::modeling
