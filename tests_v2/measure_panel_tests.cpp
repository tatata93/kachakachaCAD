// 測る棚(PRD-070〜072、V1同等性)。
//
// 見ているのは「選んだものから、測れることが全部出るか」である。
// V1 は測り方を先に選ばせたので、選び間違えると拾い直しだった。
#include "kachakacha/app/MeasurePanel.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>

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

KACHA_V2_TEST(measure, 2点間モードは押した2点で距離と成分と軸の角度を出す)
{
    MeasureRequest request;
    request.mode = kachakacha::v2::app::MeasureMode::TwoPoints;
    request.pickedPoints = {{1.0, 2.0, 3.0}};
    const auto waiting = BuildMeasureRows(request);
    Require(waiting.size() == 1 && waiting.front().valueJa.find("あと 1") != std::string::npos,
        "あと1つ押せと言う");
    request.pickedPoints.push_back({4.0, 6.0, 15.0});
    const auto rows = BuildMeasureRows(request);
    bool distance = false;
    bool dz = false;
    bool axis = false;
    for (const auto& row : rows) {
        distance = distance || (row.labelJa == "距離" && row.valueJa == "13.000 mm");
        dz = dz || (row.labelJa == "dZ" && row.valueJa == "12.000 mm");
        axis = axis || row.labelJa == "X軸との角度";
    }
    Require(distance && dz && axis, "距離 13、dZ 12、軸との角度が出る");
    const auto primary = kachakacha::v2::app::MeasurePrimary(request);
    Require(primary.has_value() && primary->kind == "two_points"
            && std::abs(primary->value - 13.0) < 1e-9,
        "残す値は距離");
}

KACHA_V2_TEST(measure, 3点角度モードは2点目を頂点にして測る)
{
    MeasureRequest request;
    request.mode = kachakacha::v2::app::MeasureMode::ThreePointAngle;
    request.pickedPoints = {{10.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}};
    const auto rows = BuildMeasureRows(request);
    bool angle = false;
    for (const auto& row : rows) {
        angle = angle || (row.labelJa == "角度" && row.valueJa == FormatDegreesJa(3.14159265358979323846 / 2.0));
    }
    Require(angle, "90 度");
    Require(kachakacha::v2::app::MeasurePointCount(request.mode) == 3, "3点押す");
}

KACHA_V2_TEST(measure, 要素モードは線1本と点で接線と法線を出し2本で角度を出す)
{
    MeasureRequest request;
    request.mode = kachakacha::v2::app::MeasureMode::Element;
    const auto rowsEmpty = BuildMeasureRows(request);
    Require(rowsEmpty.size() == 1, "何を選ぶか言う");
    request.curves.push_back(CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 5.0).Value());
    request.pickedPoints = {{7.0, 0.0, 0.0}};
    const auto rows = BuildMeasureRows(request);
    bool tangent = false;
    bool normal = false;
    bool radius = false;
    for (const auto& row : rows) {
        tangent = tangent || row.labelJa == "接線の向き";
        normal = normal || (row.labelJa == "法線(曲率)の向き" && row.valueJa.find("決まりません") == std::string::npos);
        radius = radius || (row.labelJa == "半径" && row.valueJa == "5.000 mm");
    }
    Require(tangent && normal && radius, "接線・法線・半径");
    request.curves.push_back(CurveSegment::MakeLine({-10, 5, 0}, {10, 5, 0}).Value());
    const auto two = BuildMeasureRows(request);
    bool tangentAngle = false;
    for (const auto& row : two) {
        tangentAngle = tangentAngle || (row.labelJa == "接線どうしの角度" && row.valueJa == FormatDegreesJa(0.0));
    }
    Require(tangentAngle, "円の頂点で直線と接線が平行(0 度)");
}

KACHA_V2_TEST(measure, 寸法を残すには値と相手が要る)
{
    using kachakacha::v2::app::MeasureDimensionOf;
    MeasureRequest request;
    request.mode = kachakacha::v2::app::MeasureMode::TwoPoints;
    request.pickedPoints = {{0, 0, 0}};
    kachakacha::v2::base::DimensionId id;
    const auto none = MeasureDimensionOf(request, "幅", id);
    Require(!none.HasValue(), "測り終えていなければ断る");
    RequireEqual(none.Diagnostics().front().code, std::string("UI-M001"), "理由の番号");
    request.pickedPoints.push_back({3, 4, 0});
    const auto noTarget = MeasureDimensionOf(request, "幅", id);
    Require(!noTarget.HasValue(), "相手が文書の線でなければ断る");
    kachakacha::v2::base::DeterministicIdGenerator ids{9};
    request.targetIds.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    const auto made = MeasureDimensionOf(request, "幅",
        ids.NextTyped<kachakacha::v2::base::IdKind::Dimension>());
    Require(made.HasValue(), "残せる");
    RequireEqual(made.Value().label, std::string("幅"), "名前");
    RequireEqual(made.Value().unit, std::string("mm"), "単位");
    Require(std::abs(made.Value().recordedValue - 5.0) < 1e-9, "値");
    const auto unnamed = MeasureDimensionOf(request, "", id);
    Require(unnamed.HasValue() && unnamed.Value().label == "2点間", "名前が空なら測り方の名前");
}

KACHA_V2_TEST_MAIN("measure_panel_tests")
