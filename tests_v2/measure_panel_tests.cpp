// 測る棚(PRD-070〜072、V1同等性)。
//
// 見ているのは「選んだものから、測れることが全部出るか」である。
// V1 は測り方を先に選ばせたので、選び間違えると拾い直しだった。
#include "kachakacha/app/MeasurePanel.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::BuildMeasureRows;
using kachakacha::v2::app::FormatDegreesJa;
using kachakacha::v2::app::FormatMillimetersJa;
using kachakacha::v2::app::FormatPointJa;
using kachakacha::v2::app::MeasureRequest;
using kachakacha::v2::app::MeasureRow;
using kachakacha::v2::app::MeasureSummaryJa;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    const auto made = CurveSegment::MakeLine(start, end);
    Require(made.HasValue(), "直線が作れる");
    return made.Value();
}

[[nodiscard]] CurveSegment Circle(double radius)
{
    const auto made = CurveSegment::MakeCircle(Vector3{0.0, 0.0, 0.0},
        Vector3{0.0, 0.0, 1.0}, Vector3{1.0, 0.0, 0.0}, radius);
    Require(made.HasValue(), "円が作れる");
    return made.Value();
}

[[nodiscard]] bool HasRow(const std::vector<MeasureRow>& rows, const std::string& label)
{
    for (const MeasureRow& row : rows) {
        if (row.labelJa == label) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string ValueOf(const std::vector<MeasureRow>& rows,
    const std::string& label)
{
    for (const MeasureRow& row : rows) {
        if (row.labelJa == label) {
            return row.valueJa;
        }
    }
    return std::string();
}

} // namespace

KACHA_V2_TEST(measure, 何も選んでいなければ何を選ぶかを言う)
{
    // 空の表は、壊れているのか選び忘れなのかが分からない。
    MeasureRequest request;
    const auto rows = BuildMeasureRows(request);
    Require(rows.size() == 1, "1行だけ出る");
    Require(rows.front().valueJa.find("選んでください") != std::string::npos,
        "何をすればよいかを言う");
    Require(MeasureSummaryJa(request).find("選ばれていません") != std::string::npos,
        "見出しも同じことを言う");
}

KACHA_V2_TEST(measure, 直線を1本選ぶと長さと両端が出る)
{
    MeasureRequest request;
    request.curves.push_back(Line(Vector3{0.0, 0.0, 0.0}, Vector3{30.0, 40.0, 0.0}));
    const auto rows = BuildMeasureRows(request);
    RequireEqual(ValueOf(rows, "種類"), std::string("直線"), "直線と出る");
    RequireEqual(ValueOf(rows, "長さ"), std::string("50.000 mm"), "3:4:5 で50mm");
    RequireEqual(ValueOf(rows, "dX"), std::string("30.000 mm"), "dXが出る");
    RequireEqual(ValueOf(rows, "dY"), std::string("40.000 mm"), "dYが出る");
    RequireEqual(ValueOf(rows, "dZ"), std::string("0.000 mm"), "dZが出る");
    RequireEqual(ValueOf(rows, "始点"), std::string("(0.000, 0.000, 0.000)"), "始点が出る");
}

KACHA_V2_TEST(measure, 直線には半径が出ない)
{
    // 半径を持たないものに半径を出すと、その値を信じてしまう。
    MeasureRequest request;
    request.curves.push_back(Line(Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}));
    const auto rows = BuildMeasureRows(request);
    Require(!HasRow(rows, "半径"), "直線に半径の行は出ない");
    Require(!HasRow(rows, "直径"), "直線に直径の行は出ない");
}

KACHA_V2_TEST(measure, 円には半径と直径が出る)
{
    MeasureRequest request;
    request.curves.push_back(Circle(7.5));
    const auto rows = BuildMeasureRows(request);
    RequireEqual(ValueOf(rows, "種類"), std::string("円"), "円と出る");
    RequireEqual(ValueOf(rows, "半径"), std::string("7.500 mm"), "半径が出る");
    RequireEqual(ValueOf(rows, "直径"), std::string("15.000 mm"), "直径が出る");
}

KACHA_V2_TEST(measure, 曲線は長さと両端の距離が違う)
{
    // 曲がっているぶん、長さのほうが長い。両方出さないと確かめられない。
    MeasureRequest request;
    request.curves.push_back(Circle(10.0));
    const auto rows = BuildMeasureRows(request);
    const std::string length = ValueOf(rows, "長さ");
    const std::string span = ValueOf(rows, "両端の距離");
    Require(!length.empty() && !span.empty(), "どちらも出る");
    Require(length != span, "長さと両端の距離は違う");
}

KACHA_V2_TEST(measure, 2本選ぶと最短距離と合計が出る)
{
    MeasureRequest request;
    request.curves.push_back(Line(Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}));
    request.curves.push_back(Line(Vector3{0.0, 4.0, 0.0}, Vector3{10.0, 4.0, 0.0}));
    const auto rows = BuildMeasureRows(request);
    RequireEqual(ValueOf(rows, "いちばん近いところ"), std::string("4.000 mm"),
        "平行な2本の間は4mm");
    RequireEqual(ValueOf(rows, "長さの合計"), std::string("20.000 mm"), "合計が出る");
    Require(HasRow(rows, "1. 長さ") && HasRow(rows, "2. 長さ"),
        "どちらの線かが分かるよう番号が付く");
}

KACHA_V2_TEST(measure, 触れている2本はそう言う)
{
    // 0.000 mm とだけ出しても分かりにくい。
    MeasureRequest request;
    request.curves.push_back(Line(Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}));
    request.curves.push_back(Line(Vector3{10.0, 0.0, 0.0}, Vector3{10.0, 10.0, 0.0}));
    const auto rows = BuildMeasureRows(request);
    Require(ValueOf(rows, "触れているか").find("触れています") != std::string::npos,
        "触れていると言う");
}

KACHA_V2_TEST(measure, 直交する2本の角度は90度)
{
    MeasureRequest request;
    request.curves.push_back(Line(Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0}));
    request.curves.push_back(Line(Vector3{10.0, 0.0, 0.0}, Vector3{10.0, 10.0, 0.0}));
    const auto rows = BuildMeasureRows(request);
    RequireEqual(ValueOf(rows, "中ほどの接線の角度"), std::string("90.000 度"),
        "直交は90度");
}

KACHA_V2_TEST(measure, 3本以上でも全部出る)
{
    MeasureRequest request;
    for (int index = 0; index < 3; ++index) {
        request.curves.push_back(Line(Vector3{0.0, static_cast<double>(index), 0.0},
            Vector3{5.0, static_cast<double>(index), 0.0}));
    }
    const auto rows = BuildMeasureRows(request);
    Require(HasRow(rows, "3. 長さ"), "3本目も出る");
    RequireEqual(ValueOf(rows, "長さの合計"), std::string("15.000 mm"), "合計が出る");
    // 3本のときは、どの2本の間かが決まらないので距離は出さない。
    Require(!HasRow(rows, "いちばん近いところ"), "2本のときだけ間を測る");
}

KACHA_V2_TEST(measure, 桁は3桁でそろう)
{
    // そろっていないと、変わった桁に気づけない。
    RequireEqual(FormatMillimetersJa(1.0), std::string("1.000 mm"), "3桁");
    RequireEqual(FormatMillimetersJa(-0.0000001), std::string("0.000 mm"),
        "マイナスゼロは出さない");
    RequireEqual(FormatDegreesJa(0.0), std::string("0.000 度"), "度も3桁");
    RequireEqual(FormatPointJa(kachakacha::v2::geometry::Vector3{1.5, -2.25, 0.0}),
        std::string("(1.500, -2.250, 0.000)"), "座標も3桁");
}

KACHA_V2_TEST_MAIN("measure_panel_tests")
