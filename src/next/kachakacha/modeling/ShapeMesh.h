#pragma once

//! 核が作った形を、画面に出せる形にしたもの(WP-08 の穴埋め、棚卸し A-1)。
//!
//! V2 の 3D 画面は、線と点しか描いていなかった。押し出しても、面を作っても、
//! 厚みを付けても、画面には何も増えなかった。できた物は一覧に名前が出るだけで、
//! 形は見えない。**工程5から先が目で確かめられない。**
//! V1 は面・板材・立体を塗って描いていた(`CadViewport.cpp` に42か所)。
//!
//! ここは OCCT を知らない。核(`kernel/OcctTessellate`)が三角形にしたものを
//! この形で受け取り、画面はこれだけを見る。こうすると:
//!   - 画面は OCCT に触らない(architecture-and-data.md DOC-002)
//!   - 塗る順・明るさ・当たり判定を、画面を出さずに試験できる
//!
//! 三角形のほかに **稜線** を持つ。三角形の網だけを描くと、面の継ぎ目が
//! 三角形の辺として全部出てしまい、形が読めない。V1 と同じく、
//! 元の辺(TopoDS_Edge)を折れ線にして、その上から描く。

#include "kachakacha/geometry/Vector3.h"

#include <array>
#include <cstddef>
#include <vector>

namespace kachakacha::v2::modeling {

using geometry::Vector3;

//! 三角形1枚。法線は作るときに決めて持ち回る。描くたびに外積を取り直さない。
struct MeshTriangle {
    std::array<Vector3, 3> points{};
    //! 外向きの単位法線。長さ0なら潰れた三角形(描かない)。
    Vector3 normal{};

    //! 重心。奥行きを測るのに使う。
    [[nodiscard]] Vector3 Center() const noexcept
    {
        return (points[0] + points[1] + points[2]) * (1.0 / 3.0);
    }
};

//! 1つの形。立体でも面でもよい。
struct ShapeMesh {
    std::vector<MeshTriangle> triangles;
    //! 稜線。1本が折れ線(2点以上)。
    std::vector<std::vector<Vector3>> edges;
    //! 外接箱。空なら minimum > maximum になる。
    Vector3 minimum{};
    Vector3 maximum{};
    //! 閉じた立体か。閉じていれば裏を向いた三角形は描かなくてよい。
    bool closed = false;

    [[nodiscard]] bool Empty() const noexcept { return triangles.empty() && edges.empty(); }
};

//! 外接箱を数え直す。核から受け取ったあとに1度だけ呼ぶ。
void RefreshBounds(ShapeMesh& mesh);

//! 3点から法線を作って入れる。潰れていれば長さ0のまま。
void RefreshNormals(ShapeMesh& mesh);

} // namespace kachakacha::v2::modeling
