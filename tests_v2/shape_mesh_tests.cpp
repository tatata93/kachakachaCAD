// 核の形を画面に出すための網(三角形・稜線・塗る順・当たり判定)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/MeshPick.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/view/ShapeShading.h"

#include <cmath>

using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::MeshTriangle;
using kachakacha::v2::modeling::PickMesh;
using kachakacha::v2::modeling::RayHitsTriangle;
using kachakacha::v2::modeling::RefreshBounds;
using kachakacha::v2::modeling::RefreshNormals;
using kachakacha::v2::modeling::ShapeMesh;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;
using namespace kachakacha::v2::view;

namespace {

//! XY 平面に置いた 10×10 の正方形(三角形2枚)。法線は +Z。
[[nodiscard]] ShapeMesh FlatSquare(double height = 0.0)
{
    ShapeMesh mesh;
    mesh.triangles.push_back(MeshTriangle{{Vector3{0.0, 0.0, height},
        Vector3{10.0, 0.0, height}, Vector3{10.0, 10.0, height}}, {}});
    mesh.triangles.push_back(MeshTriangle{{Vector3{0.0, 0.0, height},
        Vector3{10.0, 10.0, height}, Vector3{0.0, 10.0, height}}, {}});
    RefreshNormals(mesh);
    RefreshBounds(mesh);
    return mesh;
}

} // namespace

KACHA_V2_TEST(shape_mesh, 法線を3点から作る)
{
    const ShapeMesh mesh = FlatSquare();
    RequireNear(mesh.triangles.front().normal.z, 1.0, 1.0e-9, "+Z を向く");
    RequireNear(mesh.triangles.front().normal.x, 0.0, 1.0e-9, "X は混ざらない");
}

KACHA_V2_TEST(shape_mesh, 潰れた三角形の法線は長さ0のまま)
{
    // 勝手に上向きへ丸めない。丸めると、潰れた面が普通の面として塗られる。
    ShapeMesh mesh;
    mesh.triangles.push_back(MeshTriangle{{Vector3{}, Vector3{1.0, 0.0, 0.0},
        Vector3{2.0, 0.0, 0.0}}, {}});
    RefreshNormals(mesh);
    Require(mesh.triangles.front().normal == Vector3{}, "長さ0のまま");
}

KACHA_V2_TEST(shape_mesh, 外接箱を数える)
{
    const ShapeMesh mesh = FlatSquare(3.0);
    RequireNear(mesh.minimum.x, 0.0, 1.0e-9, "左端");
    RequireNear(mesh.maximum.x, 10.0, 1.0e-9, "右端");
    RequireNear(mesh.minimum.z, 3.0, 1.0e-9, "高さ");
    RequireNear(mesh.maximum.z, 3.0, 1.0e-9, "高さ");
}

KACHA_V2_TEST(shape_mesh, 稜線も外接箱に入る)
{
    // 三角形が1枚も無い形(線だけの稜線)でも箱が出る。
    ShapeMesh mesh;
    mesh.edges.push_back({Vector3{-5.0, 0.0, 0.0}, Vector3{5.0, 0.0, 0.0}});
    RefreshBounds(mesh);
    RequireNear(mesh.minimum.x, -5.0, 1.0e-9, "左端");
    RequireNear(mesh.maximum.x, 5.0, 1.0e-9, "右端");
}

KACHA_V2_TEST(shape_mesh, 空の網は空と分かる)
{
    Require(ShapeMesh{}.Empty(), "三角形も稜線も無ければ空");
    Require(!FlatSquare().Empty(), "三角形があれば空でない");
}

KACHA_V2_TEST(shading, 光を向いた面は明るい)
{
    const Vector3 light = StandardLightDirection();
    // 光の向きにまっすぐ向いた面(法線が光の逆)がいちばん明るい。
    const double facing = LambertShade(light * -1.0, light);
    const double sideways = LambertShade(Vector3{light.y, -light.x, 0.0}, light);
    Require(facing > sideways, "正面のほうが明るい");
    RequireNear(facing, 1.0, 1.0e-9, "真正面は 1");
}

KACHA_V2_TEST(shading, 裏を向いた面も真っ黒にしない)
{
    // 開いた面は裏から見ることがある。真っ黒だと消えたようにしか見えない。
    const Vector3 light = StandardLightDirection();
    const double back = LambertShade(light, light);
    Require(back > 0.2, "下限がある");
    RequireNear(back, 1.0, 1.0e-9, "裏表は問わない(同じ濃さ)");
}

KACHA_V2_TEST(shading, 法線が決まらなければ下限の明るさ)
{
    RequireNear(LambertShade(Vector3{}, StandardLightDirection()), 0.25, 1.0e-9, "0.25");
}

KACHA_V2_TEST(shading, 向こうを向いた三角形が分かる)
{
    const ShapeMesh mesh = FlatSquare();
    // 真上(+Z)から見下ろすと、+Z を向く面は手前を向いている。
    Require(!BackFacing(mesh.triangles.front(), Vector3{0.0, 0.0, -1.0}), "表");
    // 真下から見上げると、同じ面は向こうを向いている。
    Require(BackFacing(mesh.triangles.front(), Vector3{0.0, 0.0, 1.0}), "裏");
}

KACHA_V2_TEST(shading, 潰れた三角形は裏と言わない)
{
    MeshTriangle flat{{Vector3{}, Vector3{1.0, 0.0, 0.0}, Vector3{2.0, 0.0, 0.0}}, {}};
    Require(!BackFacing(flat, Vector3{0.0, 0.0, -1.0}), "裏ではない");
}

KACHA_V2_TEST(shading, 奥から手前へ並べる)
{
    // 深度バッファが無いので、塗る順を間違えると奥の面が手前に乗る。
    std::vector<MeshTriangle> triangles;
    for (const double height : {0.0, 20.0, 10.0}) {
        MeshTriangle one{{Vector3{0.0, 0.0, height}, Vector3{1.0, 0.0, height},
            Vector3{0.0, 1.0, height}}, Vector3{0.0, 0.0, 1.0}};
        triangles.push_back(one);
    }
    // 真上(+Z)から見下ろす = 視線は -Z。z が小さいほど奥。
    const auto order = PainterOrder(triangles, Vector3{0.0, 0.0, -1.0});
    Require(order.size() == 3, "3枚");
    RequireNear(triangles[order[0]].points[0].z, 0.0, 1.0e-9, "いちばん奥が先");
    RequireNear(triangles[order[1]].points[0].z, 10.0, 1.0e-9, "次");
    RequireNear(triangles[order[2]].points[0].z, 20.0, 1.0e-9, "手前が最後");
}

KACHA_V2_TEST(shading, 同じ奥行きなら入っている順のまま)
{
    // 毎回同じ絵になること。順が揺れると、撮った画像が比べられない。
    std::vector<MeshTriangle> triangles;
    for (int index = 0; index < 4; ++index) {
        triangles.push_back(MeshTriangle{{Vector3{static_cast<double>(index), 0.0, 0.0},
            Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}}, Vector3{0.0, 0.0, 1.0}});
    }
    const auto order = PainterOrder(triangles, Vector3{0.0, 0.0, -1.0});
    for (std::size_t index = 0; index < order.size(); ++index) {
        Require(order[index] == index, "順が変わらない");
    }
}

KACHA_V2_TEST(mesh_pick, 面の真ん中に当たる)
{
    const ShapeMesh mesh = FlatSquare();
    const auto hit = PickMesh({mesh}, Vector3{5.0, 5.0, 50.0}, Vector3{0.0, 0.0, -1.0});
    Require(hit.has_value(), "当たる");
    RequireNear(hit->distanceMm, 50.0, 1.0e-6, "目からの距離");
    RequireNear(hit->point.z, 0.0, 1.0e-6, "面の上");
}

KACHA_V2_TEST(mesh_pick, 外れたところは当たらない)
{
    const ShapeMesh mesh = FlatSquare();
    Require(!PickMesh({mesh}, Vector3{50.0, 50.0, 50.0}, Vector3{0.0, 0.0, -1.0}).has_value(),
        "外は当たらない");
}

KACHA_V2_TEST(mesh_pick, 裏からでも当たる)
{
    // 開いた面は裏から見て押すことがある。表だけにすると、見えているのに掴めない。
    const ShapeMesh mesh = FlatSquare();
    const auto hit = PickMesh({mesh}, Vector3{5.0, 5.0, -50.0}, Vector3{0.0, 0.0, 1.0});
    Require(hit.has_value(), "裏からも当たる");
}

KACHA_V2_TEST(mesh_pick, いちばん手前を返す)
{
    // 奥のものを返すと、手前の部品をいつまでも掴めない。
    const std::vector<ShapeMesh> shapes{FlatSquare(0.0), FlatSquare(20.0)};
    const auto hit = PickMesh(shapes, Vector3{5.0, 5.0, 50.0}, Vector3{0.0, 0.0, -1.0});
    Require(hit.has_value(), "当たる");
    Require(hit->shapeIndex == 1, "上(手前)のほう");
    RequireNear(hit->distanceMm, 30.0, 1.0e-6, "手前までの距離");
}

KACHA_V2_TEST(mesh_pick, 目の後ろは当たらない)
{
    const ShapeMesh mesh = FlatSquare();
    Require(!PickMesh({mesh}, Vector3{5.0, 5.0, 50.0}, Vector3{0.0, 0.0, 1.0}).has_value(),
        "後ろは見ない");
}

KACHA_V2_TEST(mesh_pick, 平行な光線は当たらない)
{
    const ShapeMesh mesh = FlatSquare();
    const auto hit = RayHitsTriangle(Vector3{-5.0, 5.0, 0.0}, Vector3{1.0, 0.0, 0.0},
        mesh.triangles.front());
    Require(!hit.has_value(), "面と平行なら拾わない");
}

KACHA_V2_TEST(mesh_pick, 向きが長さ0なら当たらない)
{
    const ShapeMesh mesh = FlatSquare();
    Require(!RayHitsTriangle(Vector3{5.0, 5.0, 50.0}, Vector3{}, mesh.triangles.front())
                 .has_value(),
        "向きが決まらない");
}

KACHA_V2_TEST_MAIN("shape_mesh_tests")
