// 回り込み投影(fabrication/WrapProjection.h)。V1 の「複数の面へ回り込み投影」。
#include "kachakacha/fabrication/WrapProjection.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::fabrication::WrapProjectOntoSurfaces;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れる");
    return made.Value();
}

//! z = height の平らな面。x は [x0,x1]、y は [-50,50]。
[[nodiscard]] SurfacePatchSamples FlatAt(double height, double x0, double x1)
{
    SurfacePatchSamples samples;
    samples.rowCount = 5;
    samples.columnCount = 5;
    for (std::size_t row = 0; row < 5; ++row) {
        const double y = -50.0 + 100.0 * static_cast<double>(row) / 4.0;
        for (std::size_t column = 0; column < 5; ++column) {
            const double x = x0 + (x1 - x0) * static_cast<double>(column) / 4.0;
            samples.points.push_back(Vector3{x, y, height});
        }
    }
    return samples;
}

} // namespace

KACHA_V2_TEST(wrap_projection, 角をまたぐ線は面ごとの区間に分かれて落ちる)
{
    // 段違いの2枚(x<0 は z=0、x>0 は z=10)。上から落とすと、線は 2 区間に分かれる。
    const std::vector<SurfacePatchSamples> surfaces{FlatAt(0.0, -60.0, 0.0),
        FlatAt(10.0, 0.0, 60.0)};
    const auto runs = WrapProjectOntoSurfaces(surfaces,
        {Line({-40, 0, 50}, {40, 0, 50})}, Vector3{0.0, 0.0, -1.0}, 0.01);
    Require(runs.HasValue(), "落ちる");
    RequireEqual(std::to_string(runs.Value().size()), std::string("2"), "2 区間");
    RequireEqual(std::to_string(runs.Value()[0].surfaceIndex), std::string("0"), "先は 1 枚目");
    RequireEqual(std::to_string(runs.Value()[1].surfaceIndex), std::string("1"), "次は 2 枚目");
    // 区間の境目は x=0(標本の粗さではなく二分探索で詰める)。
    // 線は x=-40 から 40 なので、境目のパラメータは 0.5 のはず。
    RequireNear(runs.Value()[0].endParameter, 0.5, 0.01, "境目は真ん中");
    RequireNear(runs.Value()[1].startParameter, 0.5, 0.01, "次の区間もそこから");
    // 区間は自分の面の内側で終わる。またいだところを橋渡ししない。
    Require(runs.Value()[0].curves.back().EndPoint().x <= 0.0, "1 枚目は x<=0 で終わる");
    Require(runs.Value()[1].curves.front().StartPoint().x >= 0.0, "2 枚目は x>=0 から");
    // 落ちた高さは面のとおり。またいだところをつながない。
    RequireNear(runs.Value()[0].curves.front().StartPoint().z, 0.0, 1e-9, "1 枚目は z=0");
    RequireNear(runs.Value()[1].curves.back().EndPoint().z, 10.0, 1e-9, "2 枚目は z=10");
    Require(!runs.Value()[0].closed && !runs.Value()[1].closed, "開いたまま");
}

KACHA_V2_TEST(wrap_projection, 全部が1枚に載るなら1区間で閉じたまま落ちる)
{
    const std::vector<SurfacePatchSamples> surfaces{FlatAt(0.0, -60.0, 60.0),
        FlatAt(-100.0, -60.0, 60.0)};
    const std::vector<CurveSegment> square{Line({-10, -10, 50}, {10, -10, 50}),
        Line({10, -10, 50}, {10, 10, 50}), Line({10, 10, 50}, {-10, 10, 50}),
        Line({-10, 10, 50}, {-10, -10, 50})};
    const auto runs = WrapProjectOntoSurfaces(surfaces, square, Vector3{0.0, 0.0, -1.0}, 0.01);
    Require(runs.HasValue(), "落ちる");
    RequireEqual(std::to_string(runs.Value().size()), std::string("1"), "1 区間");
    Require(runs.Value().front().closed, "閉じたまま");
    RequireNear(runs.Value().front().curves.front().StartPoint().z, 0.0, 1e-9, "近い面へ");
}

KACHA_V2_TEST(wrap_projection, 面が1枚や線が無いときとどの面にも当たらないときは断る)
{
    const std::vector<SurfacePatchSamples> one{FlatAt(0.0, -60.0, 60.0)};
    const auto few = WrapProjectOntoSurfaces(one, {Line({-10, 0, 50}, {10, 0, 50})},
        Vector3{0.0, 0.0, -1.0}, 0.01);
    Require(!few.HasValue(), "1 枚では断る");
    RequireEqual(few.Diagnostics().front().code, std::string("FAB-J003"), "理由");
    const std::vector<SurfacePatchSamples> two{FlatAt(0.0, -60.0, 0.0), FlatAt(10.0, 0.0, 60.0)};
    Require(!WrapProjectOntoSurfaces(two, {}, Vector3{0.0, 0.0, -1.0}, 0.01).HasValue(),
        "線が無ければ断る");
    Require(!WrapProjectOntoSurfaces(two, {Line({-10, 0, 50}, {10, 0, 50})}, Vector3{},
                 0.01).HasValue(),
        "向きが無ければ断る");
    // 面から外れたところにある線。どの面にも当たらない。
    const auto missed = WrapProjectOntoSurfaces(two,
        {Line({-40, 200, 50}, {40, 200, 50})}, Vector3{0.0, 0.0, -1.0}, 0.01);
    Require(!missed.HasValue(), "当たらなければ断る");
    RequireEqual(missed.Diagnostics().front().code, std::string("FAB-J001"), "理由");
}

KACHA_V2_TEST_MAIN("wrap_projection")
