// 面の下見の線(app/SurfacePreview.h)。
//
// 「面を作る」は確定するまで文書へ何も書かない(オーナー指示 2026-09-15 §12)。
// 書かずに見せるための線を、ここで作る。
// 標本が足りないのに線を出すと、**無い形を見せて作らせる**ことになる。
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/base/TestHarness.h"

#include <cstddef>
#include <string>
#include <vector>

using kachakacha::v2::app::SurfaceBoundaryLines;
using kachakacha::v2::app::SurfaceGridLines;
using kachakacha::v2::app::SurfacePreviewLines;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;

namespace {

//! rows × columns の平らな格子。x が列、y が行。
[[nodiscard]] SurfacePatchSamples Grid(std::size_t rows, std::size_t columns)
{
    SurfacePatchSamples samples;
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            samples.points.push_back(Vector3{static_cast<double>(column),
                static_cast<double>(row), 0.0});
        }
    }
    return samples;
}

//! 直線を1本。作れないはずがないので、作れなければそこで落とす。
[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    const auto made = CurveSegment::MakeLine(start, end);
    Require(made.HasValue(), "直線が作れる");
    return made.Value();
}

} // namespace

KACHA_V2_TEST(surface_preview, 標本が足りないときは線を出さない)
{
    SurfacePatchSamples empty;
    Require(SurfaceGridLines(empty).empty(), "空の標本からは何も出ない");
    SurfacePatchSamples oneRow = Grid(1, 5);
    Require(!oneRow.Valid(), "1行は格子ではない");
    Require(SurfaceGridLines(oneRow).empty(), "1行からは何も出ない");
}

KACHA_V2_TEST(surface_preview, 小さい格子は全部の行と列を出す)
{
    const auto lines = SurfaceGridLines(Grid(3, 4), 9);
    Require(lines.size() == 3U + 4U, "3行 + 4列");
    for (std::size_t index = 0; index < 3U; ++index) {
        Require(lines[index].size() == 4U, "行の線は列の数だけ点を持つ");
    }
    for (std::size_t index = 3U; index < lines.size(); ++index) {
        Require(lines[index].size() == 3U, "列の線は行の数だけ点を持つ");
    }
}

KACHA_V2_TEST(surface_preview, 細かい格子は間引くが端は必ず残す)
{
    const auto lines = SurfaceGridLines(Grid(40, 40), 5);
    Require(lines.size() == 10U, "5本 + 5本");
    // 最初の行の線は y = 0、5本目の行の線は y = 39。端が残っている。
    Require(lines[0].front().y == 0.0, "最初の行");
    Require(lines[4].front().y == 39.0, "最後の行");
    Require(lines[5].front().x == 0.0, "最初の列");
    Require(lines[9].front().x == 39.0, "最後の列");
}

KACHA_V2_TEST(surface_preview, 境界は曲線ごとに折れ線になる)
{
    std::vector<CurveSegment> boundary;
    boundary.push_back(Line(Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}));
    boundary.push_back(Line(Vector3{10.0, 0.0, 0.0}, Vector3{10.0, 5.0, 0.0}));
    const auto lines = SurfaceBoundaryLines(boundary, 4);
    Require(lines.size() == 2U, "曲線2本");
    Require(lines[0].size() == 5U, "4分割は点5つ");
    Require(lines[0].front().x == 0.0 && lines[0].back().x == 10.0, "端から端まで");
}

KACHA_V2_TEST(surface_preview, 下見は境界を先に格子を後に並べる)
{
    std::vector<CurveSegment> boundary;
    boundary.push_back(Line(Vector3{0.0, 0.0, 0.0}, Vector3{3.0, 0.0, 0.0}));
    const auto lines = SurfacePreviewLines(Grid(3, 3), boundary, 9);
    Require(lines.size() == 1U + 6U, "境界1本 + 3行 + 3列");
    Require(lines.front().front().x == 0.0 && lines.front().back().x == 3.0, "先頭が境界");
}

KACHA_V2_TEST_MAIN("surface_preview_tests")
