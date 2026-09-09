// 曲がった面を型紙にする(AT-FAB-001/002)。
//
// 見ているのは「伸縮させないか」である。
// 近い形へ均して成功にすると、切ってから合わないことに、
// 材料を使い切ったあとで気づくことになる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/CurvedPanel.h"

#include <cmath>
#include <set>
#include <string>

using kachakacha::v2::fabrication::BuildCurvedPanel;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;

namespace {

//! 半径 r の円筒の一部を、格子で標本にする。
//! 円筒は伸ばさずに平らにできる面の代表である。
[[nodiscard]] SurfacePatchSamples Cylinder(double radius, double sweepRad, double height,
    std::size_t rows, std::size_t columns)
{
    SurfacePatchSamples samples;
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        const double z = height * static_cast<double>(row)
            / static_cast<double>(rows - 1);
        for (std::size_t column = 0; column < columns; ++column) {
            const double angle = sweepRad * static_cast<double>(column)
                / static_cast<double>(columns - 1);
            samples.points.push_back(Vector3{radius * std::cos(angle),
                radius * std::sin(angle), z});
        }
    }
    return samples;
}

//! 球の一部。伸ばさずには平らにできない。
//! span を広げるほど、平らにしたときのずれが大きくなる。
[[nodiscard]] SurfacePatchSamples SpherePatch(double radius, double span,
    std::size_t rows, std::size_t columns)
{
    SurfacePatchSamples samples;
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        const double polar = 0.2 + span * static_cast<double>(row)
            / static_cast<double>(rows - 1);
        for (std::size_t column = 0; column < columns; ++column) {
            const double azimuth = span * static_cast<double>(column)
                / static_cast<double>(columns - 1);
            samples.points.push_back(Vector3{
                radius * std::sin(polar) * std::cos(azimuth),
                radius * std::sin(polar) * std::sin(azimuth),
                radius * std::cos(polar)});
        }
    }
    return samples;
}

//! 平らな長方形。展開しても形が変わらないはず。
[[nodiscard]] SurfacePatchSamples FlatPatch(double width, double height,
    std::size_t rows, std::size_t columns)
{
    SurfacePatchSamples samples;
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        const double y = height * static_cast<double>(row)
            / static_cast<double>(rows - 1);
        for (std::size_t column = 0; column < columns; ++column) {
            const double x = width * static_cast<double>(column)
                / static_cast<double>(columns - 1);
            samples.points.push_back(Vector3{x, y, 0.0});
        }
    }
    return samples;
}

} // namespace

KACHA_V2_TEST(curved_panel, 円筒は展開できる)
{
    const auto made = BuildCurvedPanel("屋根", Cylinder(20.0, 1.2, 30.0, 5, 9), 0.1);
    Require(made.HasValue(), "展開できる");
    Require(made.Value().panel.outline.size() >= 3, "外周ができる");
    Require(made.Value().lengthErrorRelative < 1.0e-6, "長さが変わっていない");
}

KACHA_V2_TEST(curved_panel, 球は断る)
{
    // 球は伸ばさずには平らにできない。通ってしまうほうが困る。
    // 半径20mm を 1.4rad ぶん切り取ると、平らにしたとき 0.42mm ずれる。
    const auto refused = BuildCurvedPanel("球", SpherePatch(20.0, 1.4, 9, 9), 0.1);
    Require(!refused.HasValue(), "断る");
    Require(refused.Diagnostics().front().code == "FAB-P005", "展開できないと言う");
    // 何mmずれるかを言う。言わないと、どれだけ無理なのかが分からない。
    Require(refused.Diagnostics().front().detailsJa.find("mm") != std::string::npos,
        "ずれを mm で言う");
}

KACHA_V2_TEST(curved_panel, 許すずれが答えを決める)
{
    // ここは好みではなく、決めた許容差が答えを決める。
    // 同じ面でも、0.5mm まで許すなら通り、0.01mm しか許さないなら通らない。
    // どちらも正しい。**どちらかを黙って選ばない。**
    const auto samples = SpherePatch(20.0, 1.0, 9, 9);
    Require(BuildCurvedPanel("球", samples, 0.5).HasValue(), "緩ければ通る");
    Require(!BuildCurvedPanel("球", samples, 0.01).HasValue(), "厳しければ通らない");
}

KACHA_V2_TEST(curved_panel, 平らな面も通る)
{
    // 曲がっていない面をここへ渡しても壊れない。
    const auto made = BuildCurvedPanel("平ら", FlatPatch(40.0, 20.0, 4, 6), 0.1);
    Require(made.HasValue(), "展開できる");
    Require(made.Value().distortionMm < 1.0e-6, "ずれない");
}

KACHA_V2_TEST(curved_panel, 標本が足りなければ断る)
{
    SurfacePatchSamples broken;
    broken.rowCount = 1;
    broken.columnCount = 1;
    broken.points.push_back(Vector3{});
    const auto refused = BuildCurvedPanel("こわれ", broken, 0.1);
    Require(!refused.HasValue(), "断る");
    Require(refused.Diagnostics().front().code == "FAB-P006", "標本が足りないと言う");
}

KACHA_V2_TEST(curved_panel, 許すずれが0以下なら断る)
{
    const auto refused = BuildCurvedPanel("屋根", Cylinder(20.0, 1.2, 30.0, 5, 9), 0.0);
    Require(!refused.HasValue(), "断る");
}

KACHA_V2_TEST(curved_panel, 大きく曲がった球はどの厳しさでも断る)
{
    // 緩めれば何でも通る、ということにはならない。
    for (const double allowed : {0.01, 0.1, 0.3}) {
        const auto refused = BuildCurvedPanel("球", SpherePatch(20.0, 1.4, 9, 9),
            allowed);
        Require(!refused.HasValue(), "どの厳しさでも断る");
    }
}

KACHA_V2_TEST(curved_panel, 外周は行きと帰りでつながる)
{
    // 帰りを逆に並べないと、8の字になって型紙にならない。
    const auto made = BuildCurvedPanel("屋根", Cylinder(20.0, 1.0, 10.0, 4, 6), 0.1);
    Require(made.HasValue(), "展開できる");
    const auto& outline = made.Value().panel.outline;
    // 帯の両縁は列の数ぶんの点を持つ。行き6点、帰り6点で12点。
    Require(outline.size() == 12, "両縁で12点");
    // 端どうしが近いこと。8の字なら、ここが大きく離れる。
    const double du = outline.front().u - outline.back().u;
    const double dv = outline.front().v - outline.back().v;
    const double gap = std::sqrt(du * du + dv * dv);
    Require(gap < 40.0, "始めと終わりが近い(8の字になっていない)");
}

KACHA_V2_TEST(curved_panel, 内側の点が無い標本は確かめられないと言う)
{
    // 2列しかない帯には内側の点が無く、角欠損の検査が何も見ずに通る。
    // 球を細く切って出しても通ってしまうので、そこで断る。
    // 通ったことにならない検査を通すくらいなら、確かめられないと言うほうがよい。
    SurfacePatchSamples narrow;
    narrow.rowCount = 5;
    narrow.columnCount = 2;
    for (std::size_t row = 0; row < 5; ++row) {
        for (std::size_t column = 0; column < 2; ++column) {
            const double polar = 0.2 + 1.4 * static_cast<double>(row) / 4.0;
            const double azimuth = 1.4 * static_cast<double>(column);
            narrow.points.push_back(Vector3{
                20.0 * std::sin(polar) * std::cos(azimuth),
                20.0 * std::sin(polar) * std::sin(azimuth),
                20.0 * std::cos(polar)});
        }
    }
    const auto refused = BuildCurvedPanel("細い帯", narrow, 0.1);
    Require(!refused.HasValue(), "断る");
    Require(refused.Diagnostics().front().code == "FAB-P006", "確かめられないと言う");
}

KACHA_V2_TEST_MAIN("curved_panel_tests")
