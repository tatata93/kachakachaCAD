// 面の解析の色と、製作性(可展性)の診断(app/SurfaceAnalysis.h)。
//
// 平面・円筒・円錐は K = 0 なので「ほぼ可展」、球は大きさと半径の比で
// 「二重曲率あり」「強い二重曲率」に分かれる。基準は伸び縮みの目安 ε ≈ |K|·a²/6。
// 断定しない一文(診断材料であること)も出す。
#include "kachakacha/app/SurfaceAnalysis.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

using kachakacha::v2::app::AnalysisLegendJa;
using kachakacha::v2::app::ClassifyDevelopability;
using kachakacha::v2::app::ContinuityGrade;
using kachakacha::v2::app::CurvatureScale;
using kachakacha::v2::app::DevelopabilityClass;
using kachakacha::v2::app::DivergingColor;
using kachakacha::v2::app::GradeContinuity;
using kachakacha::v2::app::Rgb;
using kachakacha::v2::app::SurfaceAnalysisMode;
using kachakacha::v2::app::WorstDevelopability;
using kachakacha::v2::app::ZebraDark;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::AnalysisTriangle;
using kachakacha::v2::modeling::EdgeContinuitySample;
using kachakacha::v2::modeling::SurfaceAnalysisData;
using kachakacha::v2::test::Require;

namespace {

//! 大きさ size、角の曲率がどこも (K, H) の標本。
[[nodiscard]] SurfaceAnalysisData Uniform(double gaussian, double mean, double size)
{
    SurfaceAnalysisData data;
    data.sizeMm = size;
    for (int k = 0; k < 20; ++k) {
        AnalysisTriangle triangle;
        triangle.triangle.points = {Vector3{k * 1.0, 0, 0}, Vector3{k + 1.0, 0, 0}, Vector3{k * 1.0, 1, 0}};
        triangle.triangle.normal = Vector3{0, 0, 1};
        triangle.normals = {Vector3{0, 0, 1}, Vector3{0, 0, 1}, Vector3{0, 0, 1}};
        triangle.gaussian = {gaussian, gaussian, gaussian};
        triangle.mean = {mean, mean, mean};
        data.triangles.push_back(triangle);
    }
    return data;
}

} // namespace

KACHA_V2_TEST(surface_analysis, 平面と円筒と円錐はほぼ可展)
{
    // 平面(K = 0、H = 0)、半径 20 の円筒(K = 0、H = 1/40)、円錐(K = 0)。
    for (const auto& [name, data] : {std::pair<const char*, SurfaceAnalysisData>{"平面", Uniform(0, 0, 80)},
             std::pair<const char*, SurfaceAnalysisData>{"円筒", Uniform(0, 1.0 / 40.0, 80)},
             std::pair<const char*, SurfaceAnalysisData>{"円錐", Uniform(0, 0.013, 80)}}) {
        const auto summary = ClassifyDevelopability(data);
        Require(summary.kind == DevelopabilityClass::NearlyDevelopable && summary.strain < 1.0e-12,
            std::string(name) + " はほぼ可展");
    }
}

KACHA_V2_TEST(surface_analysis, 球は大きさと半径の比で二重曲率の強さが分かれる)
{
    // 大きさ 40 mm(a = 20)。R = 500 → ε ≈ 0.027 %、R = 150 → 0.30 %、R = 50 → 2.7 %。
    const auto gentle = ClassifyDevelopability(Uniform(1.0 / (500.0 * 500.0), 1.0 / 500.0, 40));
    const auto moderate = ClassifyDevelopability(Uniform(1.0 / (150.0 * 150.0), 1.0 / 150.0, 40));
    const auto strong = ClassifyDevelopability(Uniform(1.0 / (50.0 * 50.0), 1.0 / 50.0, 40));
    Require(gentle.kind == DevelopabilityClass::NearlyDevelopable, "ゆるい球はほぼ可展");
    Require(moderate.kind == DevelopabilityClass::DoubleCurved, "中くらいは二重曲率あり");
    Require(strong.kind == DevelopabilityClass::StronglyDoubleCurved, "きつい球は強い二重曲率");
    Require(std::abs(strong.strain - 400.0 / (6.0 * 2500.0)) < 1.0e-9, "伸び縮みの目安の式");
    Require(strong.explanationJa.find("診断材料") != std::string::npos
            && strong.explanationJa.find("1.0 % 以上") != std::string::npos,
        "数値の基準と、断定しないことを言う: " + strong.explanationJa);
}

KACHA_V2_TEST(surface_analysis, 曲率の色は白を中心に赤と青へ分かれる)
{
    Require(DivergingColor(0.0, 1.0) == Rgb{245, 245, 245}, "0 は白");
    const Rgb red = DivergingColor(1.0, 1.0);
    const Rgb blue = DivergingColor(-1.0, 1.0);
    Require(red.r > red.b && blue.b > blue.r, "正は赤、負は青");
    Require(DivergingColor(5.0, 1.0) == red, "目盛りの外は端の色");
    Require(DivergingColor(0.3, 0.0) == Rgb{245, 245, 245}, "目盛りが無ければ白");
}

KACHA_V2_TEST(surface_analysis, ゼブラは法線の傾きで縞が変わり見る向きで動く)
{
    const Vector3 view{0, 0, -1};
    const Vector3 up{0, 1, 0};
    int changes = 0;
    bool previous = ZebraDark(Vector3{0, 0, 1}, view, up, 12);
    for (int k = 1; k <= 40; ++k) {
        const double angle = k * 0.02;
        const bool dark = ZebraDark(Vector3{0, std::sin(angle), std::cos(angle)}, view, up, 12);
        changes += dark != previous ? 1 : 0;
        previous = dark;
    }
    Require(changes >= 4, "法線が傾くと縞が何本も入れ替わる(" + std::to_string(changes) + ")");
    Require(ZebraDark(Vector3{0, 0, 1}, view, up, 12)
            == ZebraDark(Vector3{0, 0, 1}, Vector3{0, 0, -1}, up, 12),
        "同じ入力なら同じ縞");
}

KACHA_V2_TEST(surface_analysis, 境目の連続は離れと折れ目と曲率の差で段階が決まる)
{
    EdgeContinuitySample sample;
    Require(GradeContinuity(sample) == ContinuityGrade::Open, "隣が無ければ開いた縁");
    sample.hasNeighbor = true;
    Require(GradeContinuity(sample) == ContinuityGrade::G2, "全部そろえば G2");
    sample.curvatureDifference = 0.3;
    Require(GradeContinuity(sample) == ContinuityGrade::G1, "曲率が違えば G1");
    sample.angleDeg = 4.0;
    Require(GradeContinuity(sample) == ContinuityGrade::G0, "折れていれば G0");
    sample.gapMm = 0.2;
    Require(GradeContinuity(sample) == ContinuityGrade::Gap, "離れていれば G0 でもない");
}

KACHA_V2_TEST(surface_analysis, ガウス曲率の凡例は製作性の目安と基準を出す)
{
    const auto lines = AnalysisLegendJa(SurfaceAnalysisMode::GaussianCurvature,
        Uniform(1.0 / 2500.0, 0.02, 40));
    std::string all;
    for (const auto& line : lines) {
        all += line;
    }
    Require(all.find("製作性の目安: 強い二重曲率") != std::string::npos
            && all.find("鞍形") != std::string::npos,
        "目安と色の読み方: " + all);
}

KACHA_V2_TEST(surface_analysis, 何枚かは同じ目盛りで塗りいちばん作りにくい面で製作性を言う)
{
    // 平面(80 mm)と、半径 50 の球の帽子(40 mm)を一緒に見る。
    const SurfaceAnalysisData plane = Uniform(0, 0, 80);
    const SurfaceAnalysisData sphere = Uniform(1.0 / 2500.0, 0.02, 40);
    const std::vector<const SurfaceAnalysisData*> both{&plane, &sphere};
    Require(std::abs(CurvatureScale(both, true) - CurvatureScale(sphere, true)) < 1.0e-15,
        "目盛りは大きい方にそろえる(色が面どうしで見比べられる)");
    Require(std::abs(CurvatureScale(both, false) - 0.02) < 1.0e-12, "平均曲率の目盛りも共通");
    const auto worst = WorstDevelopability(both);
    Require(worst.kind == DevelopabilityClass::StronglyDoubleCurved,
        "作りにくい方(球)で言う: " + worst.labelJa);
    Require(WorstDevelopability({}).labelJa.empty(), "面が無ければ言わない(ほぼ可展と言わない)");
    std::string all;
    for (const auto& line : AnalysisLegendJa(SurfaceAnalysisMode::GaussianCurvature, both)) {
        all += line;
    }
    Require(all.find("2 枚のうち") != std::string::npos && all.find("強い二重曲率") != std::string::npos,
        "何枚のうちの一番かを言う: " + all);
    std::string none;
    for (const auto& line : AnalysisLegendJa(SurfaceAnalysisMode::Zebra,
             std::vector<const SurfaceAnalysisData*>{})) {
        none += line;
    }
    Require(none.find("解析できる面がありません") != std::string::npos, "面が無いと言う: " + none);
}

KACHA_V2_TEST_MAIN("surface_analysis_tests")
