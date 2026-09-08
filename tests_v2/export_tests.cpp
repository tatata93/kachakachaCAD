// 型紙の書き出し。曲線は曲線のまま出す。折れ線へ落とさない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/exporters/PatternExport.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::exporters::PatternCurve;
using kachakacha::v2::exporters::PatternLine;
using kachakacha::v2::exporters::PatternLineLayerName;
using kachakacha::v2::exporters::PatternPage;
using kachakacha::v2::exporters::Triangle;
using kachakacha::v2::exporters::WriteAsciiStl;
using kachakacha::v2::exporters::WriteBinaryStl;
using kachakacha::v2::exporters::WritePatternDxf;
using kachakacha::v2::exporters::WritePatternSvg;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

[[nodiscard]] std::size_t CountOf(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

//! 部材1枚ぶん。外周(直線+円弧)、丸窓、折り線、切れ目。
[[nodiscard]] PatternPage SamplePage()
{
    PatternPage page;
    page.widthMm = 210.0;
    page.heightMm = 297.0;
    page.curves.push_back(
        PatternCurve{PatternLine::Outline, Line({10, 10, 0}, {90, 10, 0}), false, "1"});
    page.curves.push_back(PatternCurve{PatternLine::Outline,
        CurveSegment::MakeCircularArc({90, 30, 0}, {0, 0, 1}, {0, -1, 0}, 20.0, 0.0,
            kPi / 2.0)
            .Value(),
        false, "1"});
    page.curves.push_back(
        PatternCurve{PatternLine::Outline, Line({110, 30, 0}, {110, 80, 0}), false, "1"});
    page.curves.push_back(PatternCurve{PatternLine::Opening,
        CurveSegment::MakeCircle({50, 45, 0}, {0, 0, 1}, {1, 0, 0}, 8.0).Value(), false,
        "窓1"});
    page.curves.push_back(
        PatternCurve{PatternLine::Fold, Line({10, 40, 0}, {90, 40, 0}), true, "F1"});
    page.curves.push_back(
        PatternCurve{PatternLine::Fold, Line({10, 60, 0}, {90, 60, 0}), false, "F2"});
    page.curves.push_back(
        PatternCurve{PatternLine::Cut, Line({30, 10, 0}, {30, 25, 0}), false, "C1"});
    return page;
}

} // namespace

// ---------------------------------------------------------------- SVG

KACHA_V2_TEST(exportSvg, 実寸で出る)
{
    const auto svg = WritePatternSvg(SamplePage(), "テスト型紙");
    Require(svg.HasValue(), "書き出せること");
    Require(svg.Value().find("width=\"210mm\"") != std::string::npos, "幅がmm");
    Require(svg.Value().find("height=\"297mm\"") != std::string::npos, "高さがmm");
    Require(svg.Value().find("viewBox=\"0 0 210 297\"") != std::string::npos,
        "viewBoxがmmと1対1");
}

KACHA_V2_TEST(exportSvg, 円弧が円弧のまま出る)
{
    // 折れ線へ落としていないこと。A コマンドが使われていること。
    const auto svg = WritePatternSvg(SamplePage(), "テスト");
    Require(svg.HasValue(), "書き出せること");
    Require(CountOf(svg.Value(), " A ") >= 1, "円弧コマンドがあること");
    // 円は半円2つ = A が2つ。
    Require(CountOf(svg.Value(), " A ") >= 3, "円弧と円で3つ以上");
}

KACHA_V2_TEST(exportSvg, レイヤーが分かれている)
{
    const auto svg = WritePatternSvg(SamplePage(), "テスト");
    Require(svg.HasValue(), "書き出せること");
    for (const char* layer : {"OUTLINE", "OPENING", "RELIEF", "FOLD"}) {
        Require(svg.Value().find(std::string("id=\"") + layer + "\"") != std::string::npos,
            std::string("レイヤーがあること: ") + layer);
    }
    // 使っていないレイヤーは出さない。
    Require(svg.Value().find("id=\"ANNOTATION\"") == std::string::npos,
        "空のレイヤーは出さないこと");
}

KACHA_V2_TEST(exportSvg, 山折りと谷折りを描き分ける)
{
    const auto svg = WritePatternSvg(SamplePage(), "テスト");
    Require(svg.HasValue(), "書き出せること");
    Require(svg.Value().find("#d02020") != std::string::npos, "山折りの色");
    Require(svg.Value().find("#2050d0") != std::string::npos, "谷折りの色");
    Require(svg.Value().find("stroke-dasharray") != std::string::npos, "破線であること");
}

KACHA_V2_TEST(exportSvg, ラベルが残る)
{
    const auto svg = WritePatternSvg(SamplePage(), "テスト");
    Require(svg.HasValue(), "書き出せること");
    Require(svg.Value().find("data-label=\"F1\"") != std::string::npos, "折り線の番号");
    Require(svg.Value().find("data-label=\"窓1\"") != std::string::npos, "開口の名前");
}

KACHA_V2_TEST(exportSvg, 同じ入力からは同じ文字列が出る)
{
    const PatternPage page = SamplePage();
    const auto first = WritePatternSvg(page, "テスト");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = WritePatternSvg(page, "テスト");
        Require(again.Value() == first.Value(), "毎回同じであること");
    }
}

KACHA_V2_TEST(exportSvg, 型紙の面に無い線を断る)
{
    PatternPage page;
    page.curves.push_back(
        PatternCurve{PatternLine::Outline, Line({0, 0, 0}, {10, 0, 5}), false, ""});
    const auto svg = WritePatternSvg(page, "テスト");
    Require(!svg.HasValue(), "断ること");
    RequireEqual(svg.Diagnostics().front().code, std::string("EXP-P002"), "診断コード");
}

KACHA_V2_TEST(exportSvg, おかしなページを断る)
{
    PatternPage page;
    page.widthMm = 0.0;
    Require(!WritePatternSvg(page, "テスト").HasValue(), "幅0");
    page = SamplePage();
    page.heightMm = -1.0;
    Require(!WritePatternSvg(page, "テスト").HasValue(), "高さが負");
}

KACHA_V2_TEST(exportSvg, yが上向きから下向きへ直る)
{
    // 型紙の y は上向き、SVG の y は下向き。ページの高さから引く。
    PatternPage page;
    page.widthMm = 100.0;
    page.heightMm = 200.0;
    page.curves.push_back(
        PatternCurve{PatternLine::Outline, Line({10, 0, 0}, {10, 50, 0}), false, ""});
    const auto svg = WritePatternSvg(page, "テスト");
    Require(svg.HasValue(), "書き出せること");
    Require(svg.Value().find("M 10,200") != std::string::npos, "y=0 が下端になること");
    Require(svg.Value().find("L 10,150") != std::string::npos, "y=50 が上へ行くこと");
}

// ---------------------------------------------------------------- DXF

KACHA_V2_TEST(exportDxf, 線と円弧と円が種類のまま出る)
{
    const auto dxf = WritePatternDxf(SamplePage());
    Require(dxf.HasValue(), "書き出せること");
    Require(CountOf(dxf.Value(), "\nLINE\n") >= 4, "LINE があること");
    Require(CountOf(dxf.Value(), "\nARC\n") == 1, "ARC が1つ");
    Require(CountOf(dxf.Value(), "\nCIRCLE\n") == 1, "CIRCLE が1つ");
    // 折れ線に落としていないこと。
    Require(CountOf(dxf.Value(), "LWPOLYLINE") == 0, "折れ線にしていないこと");
}

KACHA_V2_TEST(exportDxf, 自由曲線がSPLINEとして出る)
{
    PatternPage page;
    page.curves.push_back(PatternCurve{PatternLine::Outline,
        CurveSegment::MakeCubicBSpline({{0, 0, 0}, {10, 20, 0}, {30, 20, 0}, {40, 0, 0},
                                           {55, 10, 0}})
            .Value(),
        false, ""});
    const auto dxf = WritePatternDxf(page);
    Require(dxf.HasValue(), "書き出せること");
    Require(CountOf(dxf.Value(), "\nSPLINE\n") == 1, "SPLINE が1つ");
    Require(CountOf(dxf.Value(), "LWPOLYLINE") == 0, "折れ線にしていないこと");
}

KACHA_V2_TEST(exportDxf, レイヤー名が入る)
{
    const auto dxf = WritePatternDxf(SamplePage());
    Require(dxf.HasValue(), "書き出せること");
    for (const char* layer : {"OUTLINE", "OPENING", "RELIEF", "FOLD"}) {
        Require(dxf.Value().find(std::string("\n") + layer + "\n") != std::string::npos,
            std::string("レイヤー名: ") + layer);
    }
}

KACHA_V2_TEST(exportDxf, 円弧の向きが反時計回りに直る)
{
    // DXF の ARC は必ず反時計回り。掃引が負なら始点と終点を入れ替える。
    PatternPage page;
    page.curves.push_back(PatternCurve{PatternLine::Outline,
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0,
            -kPi / 2.0)
            .Value(),
        false, ""});
    const auto dxf = WritePatternDxf(page);
    Require(dxf.HasValue(), "書き出せること");
    // 掃引 -90度なので、始点は0度、終点は-90度。入れ替えて -90 → 0 になる。
    const std::size_t start = dxf.Value().find("\n50\n");
    Require(start != std::string::npos, "開始角があること");
    const std::string tail = dxf.Value().substr(start + 4, 10);
    Require(tail.find("-90") != std::string::npos,
        "開始角が -90 になること (" + tail + ")");
}

KACHA_V2_TEST(exportDxf, 同じ入力からは同じ文字列が出る)
{
    const PatternPage page = SamplePage();
    const auto first = WritePatternDxf(page);
    for (int repeat = 0; repeat < 5; ++repeat) {
        Require(WritePatternDxf(page).Value() == first.Value(), "毎回同じであること");
    }
}

KACHA_V2_TEST(exportDxf, 終端が正しい)
{
    const auto dxf = WritePatternDxf(SamplePage());
    Require(dxf.HasValue(), "書き出せること");
    Require(dxf.Value().find("SECTION") == 2, "SECTION で始まること");
    Require(dxf.Value().size() >= 6
            && dxf.Value().compare(dxf.Value().size() - 6, 6, "EOF\n") != 0,
        "");
    Require(dxf.Value().find("ENDSEC") != std::string::npos, "ENDSEC があること");
    Require(dxf.Value().rfind("EOF") != std::string::npos, "EOF があること");
}

// ---------------------------------------------------------------- STL

KACHA_V2_TEST(exportStl, 文字のSTLが出る)
{
    const std::vector<Triangle> triangles{
        Triangle{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}},
        Triangle{{10, 0, 0}, {10, 10, 0}, {0, 10, 0}},
    };
    const auto stl = WriteAsciiStl(triangles, "test");
    Require(stl.HasValue(), "書き出せること");
    Require(stl.Value().find("solid test") == 0, "solid で始まること");
    Require(stl.Value().find("endsolid test") != std::string::npos, "endsolid で終わること");
    RequireCount(CountOf(stl.Value(), "facet normal"), 2, "面の数");
    RequireCount(CountOf(stl.Value(), "vertex"), 6, "頂点の数");
    // 法線が +z を向くこと。
    Require(stl.Value().find("facet normal 0 0 1") != std::string::npos, "法線");
}

KACHA_V2_TEST(exportStl, バイナリのSTLが出る)
{
    const std::vector<Triangle> triangles{
        Triangle{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}},
        Triangle{{10, 0, 0}, {10, 10, 0}, {0, 10, 0}},
    };
    const auto stl = WriteBinaryStl(triangles);
    Require(stl.HasValue(), "書き出せること");
    // 80 + 4 + 面の数 × 50。
    RequireCount(stl.Value().size(), 80 + 4 + 2 * 50, "長さ");
    // 面の数が入っていること。
    const unsigned char count = static_cast<unsigned char>(stl.Value()[80]);
    RequireCount(static_cast<std::size_t>(count), 2, "面の数");
    Require(stl.Value().compare(0, 13, "kachakachaCAD") == 0, "見出し");
}

KACHA_V2_TEST(exportStl, 同じ入力からは同じバイト列が出る)
{
    const std::vector<Triangle> triangles{Triangle{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}};
    const auto first = WriteBinaryStl(triangles);
    for (int repeat = 0; repeat < 5; ++repeat) {
        Require(WriteBinaryStl(triangles).Value() == first.Value(), "毎回同じであること");
    }
    // 日付などが混ざっていないこと。見出しの残りは0のまま。
    for (std::size_t index = 13; index < 80; ++index) {
        RequireCount(static_cast<std::size_t>(
                         static_cast<unsigned char>(first.Value()[index])),
            0, "見出しの残りが0であること");
    }
}

KACHA_V2_TEST(exportStl, 潰れた三角形を断る)
{
    const std::vector<Triangle> degenerate{Triangle{{0, 0, 0}, {10, 0, 0}, {20, 0, 0}}};
    Require(!WriteAsciiStl(degenerate, "x").HasValue(), "一直線の三角形を断ること");
    Require(!WriteBinaryStl(degenerate).HasValue(), "バイナリでも断ること");
    Require(!WriteAsciiStl({}, "x").HasValue(), "空を断ること");
    Require(!WriteBinaryStl({}).HasValue(), "バイナリでも空を断ること");
}

KACHA_V2_TEST(exportStl, 大きなメッシュも出せる)
{
    std::vector<Triangle> triangles;
    for (int index = 0; index < 5000; ++index) {
        const double x = static_cast<double>(index);
        triangles.push_back(Triangle{{x, 0, 0}, {x + 1, 0, 0}, {x, 1, 0}});
    }
    const auto stl = WriteBinaryStl(triangles);
    Require(stl.HasValue(), "書き出せること");
    RequireCount(stl.Value().size(), 80 + 4 + 5000 * 50, "長さ");
}

KACHA_V2_TEST(exportStl, レイヤー名が機械向けである)
{
    // 機械が読むので日本語にしない。
    RequireEqual(std::string(PatternLineLayerName(PatternLine::Outline)),
        std::string("OUTLINE"), "外周");
    RequireEqual(std::string(PatternLineLayerName(PatternLine::Fold)), std::string("FOLD"),
        "折り線");
    RequireEqual(std::string(PatternLineLayerName(PatternLine::Cut)), std::string("RELIEF"),
        "切れ目");
    RequireEqual(std::string(PatternLineLayerName(PatternLine::Opening)),
        std::string("OPENING"), "開口");
}

KACHA_V2_TEST_MAIN("export_tests")
