// 押し出しの下見に敷くうすい面(app/ExtrudePreview.h)。
//
// 線だけの下見では、厚みがついたのかどうかが読めなかった(オーナー指示 §8)。
#include "kachakacha/app/ExtrudePreview.h"
#include "kachakacha/base/TestHarness.h"

#include <string>
#include <vector>

using kachakacha::v2::app::ExtrudeSweptFaces;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;

namespace {

//! 一辺 10mm の正方形。始点に戻る5点。
[[nodiscard]] std::vector<Vector3> Square()
{
    return {Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}, Vector3{10.0, 10.0, 0.0},
        Vector3{0.0, 10.0, 0.0}, Vector3{0.0, 0.0, 0.0}};
}

} // namespace

KACHA_V2_TEST(extrude_preview, 側面とふたが出る)
{
    const auto faces = ExtrudeSweptFaces(Square(), Vector3{0.0, 0.0, 5.0});
    Require(faces.size() == 5U, "側面4枚 + ふた1枚");
    Require(faces[0].size() == 4U, "側面は四角");
    Require(faces.back().size() == 5U, "ふたは輪郭と同じ点数");
    Require(faces.back().front().z == 5.0, "ふたは押し出した先にある");
}

KACHA_V2_TEST(extrude_preview, 厚みが無いときは塗らない)
{
    // 距離0でワイヤーだけ作ることがある。そこで面を塗ると、
    // **作られないものが見える。**
    Require(ExtrudeSweptFaces(Square(), Vector3{}).empty(), "距離0では塗らない");
}

KACHA_V2_TEST(extrude_preview, 点が足りなければ塗らない)
{
    Require(ExtrudeSweptFaces({}, Vector3{0.0, 0.0, 5.0}).empty(), "空の輪郭");
    Require(ExtrudeSweptFaces({Vector3{}}, Vector3{0.0, 0.0, 5.0}).empty(), "1点だけ");
}

KACHA_V2_TEST(extrude_preview, 始めの輪郭は塗らない)
{
    // そこにはもう線がある。塗ると元の図が沈む。
    const auto faces = ExtrudeSweptFaces(Square(), Vector3{0.0, 0.0, 5.0});
    for (const auto& face : faces) {
        bool allAtStart = true;
        for (const auto& point : face) {
            if (point.z != 0.0) {
                allAtStart = false;
                break;
            }
        }
        Require(!allAtStart, "始めの面そのものは入っていない");
    }
}

KACHA_V2_TEST_MAIN("extrude_preview_tests")
