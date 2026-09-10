// ER1/ER2 の 1/87 受入モデル(AT-FAB-013)。
//
// この試験は「実際に作るもの」に一番近い。ここが通らなければ、
// ほかがいくら通っていても、この道具は使えない。
//
// 以前は「腰部は円筒」「肩は二重曲率」と **人が書いた分類の数字** を並べて、
// 分割の判断だけを見ていた。それでは実形状を通していない(レビュー指摘 P1-7)。
// ここでは ER の前頭部を 1/87 の実寸で **面として標本化** し、近似(V1 方式の帯近似と
// V2 方式の面分類)・展開・開口の切り出し・曲げ状態まで、実際の道をそのまま通す。
//
// 契約が挙げている項目を、そのまま並べて数える。
//   - 幅基準は 3520/87 mm。
//   - 腰部・窓帯・額が、全面三角形ではなく大きな連続した部材になる。
//   - 強い二重曲率の肩にだけ切れ目か追加分割が入る。
//   - 6枚窓と中央前照灯の開口・接続を保つ。
//   - 再現度 3/6/9 で最大偏差が単調に増えない。
//   - 部材数の上限を超えない。
//   - 30% でしわ・縮尺変化・開口の消失がない。
//   - 100% の閉じ残りが目標偏差内。
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/BandFold.h"
#include "kachakacha/fabrication/FabricationSettings.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::app::EvaluateFabrication;
using kachakacha::v2::app::FabricationEvaluation;
using kachakacha::v2::app::FabricationMarkings;
using kachakacha::v2::app::FabricationMethod;
using kachakacha::v2::app::FabricationSource;
using kachakacha::v2::domain::CreateFabricationModelDefinition;
using kachakacha::v2::fabrication::BandMesh;
using kachakacha::v2::fabrication::FabricationSettings;
using kachakacha::v2::fabrication::FoldBandMesh;
using kachakacha::v2::fabrication::ResolveTargetMaxDeviationMm;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

//! 1/87 の幅基準。実車 3520mm。
constexpr double kScale = 87.0;
constexpr double kBodyWidthMm = 3520.0 / kScale;      // 約 40.46 mm
constexpr double kBodyHeightMm = 3600.0 / kScale;     // 約 41.38 mm
//! 前面のふくらみ(平面視で円筒状)と、額から屋根への回り込み。
constexpr double kPlanBulgeMm = 6.0;
constexpr double kShoulderRoundMm = 9.0;
//! 模型の対角。許容差の基準に使う。
constexpr double kDiagonalMm = 60.0;

//! ER の前頭部の面。u は幅方向(0..1、左→右)、v は高さ方向(0..1、下→上)。
//!
//! 腰部・窓帯は平面視で円筒状(1方向にしか曲がっていない = 伸ばさずに平らにできる)。
//! 額(v > 0.7)は屋根へ回り込み、その回り込みは中央では小さく肩(両端)で大きい。
//! だから **肩だけが二重に曲がる**。実車の前頭部の性質をそのまま持った形である。
[[nodiscard]] Vector3 ErFront(double u, double v)
{
    const double across = 2.0 * (u - 0.5);           // -1..1
    const double x = across * kBodyWidthMm * 0.5;
    const double bulge = kPlanBulgeMm * (1.0 - across * across);
    const double roofBlend = v > 0.7 ? (v - 0.7) / 0.3 : 0.0;
    const double shoulder = across * across;           // 中央 0、肩 1
    const double roll = kShoulderRoundMm * roofBlend * roofBlend * shoulder;
    return Vector3{x, v * kBodyHeightMm, bulge - roll};
}

[[nodiscard]] SurfacePatchSamples SampleErFront(std::size_t rows, std::size_t columns)
{
    SurfacePatchSamples samples;
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            samples.points.push_back(ErFront(static_cast<double>(column) / (columns - 1),
                static_cast<double>(row) / (rows - 1)));
        }
    }
    return samples;
}

[[nodiscard]] FabricationSource Front()
{
    FabricationSource source;
    source.name = "前頭部";
    source.samples = SampleErFront(41, 41);
    return source;
}

//! 面の上の閉じた輪郭(u,v の四角)を、面に沿った折れ線の線分にする。
[[nodiscard]] std::vector<CurveSegment> LoopOnSurface(double u0, double v0, double u1,
    double v1, int perEdge = 6)
{
    std::vector<Vector3> points;
    const auto edge = [&](double ua, double va, double ub, double vb) {
        for (int step = 0; step < perEdge; ++step) {
            const double t = static_cast<double>(step) / perEdge;
            points.push_back(ErFront(ua + (ub - ua) * t, va + (vb - va) * t));
        }
    };
    edge(u0, v0, u1, v0);
    edge(u1, v0, u1, v1);
    edge(u1, v1, u0, v1);
    edge(u0, v1, u0, v0);
    std::vector<CurveSegment> segments;
    for (std::size_t index = 0; index < points.size(); ++index) {
        segments.push_back(
            CurveSegment::MakeLine(points[index], points[(index + 1) % points.size()])
                .Value());
    }
    return segments;
}

//! 前照灯。面の上の円。
[[nodiscard]] std::vector<CurveSegment> LampOnSurface()
{
    std::vector<Vector3> points;
    constexpr int kSteps = 32;
    for (int index = 0; index < kSteps; ++index) {
        const double angle = 2.0 * kPi * index / kSteps;
        points.push_back(ErFront(0.5 + 0.06 * std::cos(angle), 0.25 + 0.06 * std::sin(angle)));
    }
    std::vector<CurveSegment> segments;
    for (std::size_t index = 0; index < points.size(); ++index) {
        segments.push_back(
            CurveSegment::MakeLine(points[index], points[(index + 1) % points.size()])
                .Value());
    }
    return segments;
}

//! 6枚窓(窓帯 v = 0.45..0.62)と中央前照灯。
[[nodiscard]] FabricationMarkings Openings()
{
    FabricationMarkings markings;
    for (int index = 0; index < 6; ++index) {
        const double left = 0.08 + index * 0.145;
        markings.openings.push_back(LoopOnSurface(left, 0.45, left + 0.11, 0.62));
    }
    markings.openings.push_back(LampOnSurface());
    return markings;
}

[[nodiscard]] CreateFabricationModelDefinition Definition(FabricationMethod method,
    double targetDeviationMm)
{
    CreateFabricationModelDefinition definition;
    definition.method = method == FabricationMethod::BandApproximation ? 1 : 0;
    definition.targetMaxDeviation.value = targetDeviationMm;
    definition.maximumPartCount = 24;
    definition.minimumPartWidthMm = 2.0;
    return definition;
}

[[nodiscard]] double DeviationForFidelity(int fidelity)
{
    FabricationSettings settings;
    settings.fidelityLevel = fidelity;
    return ResolveTargetMaxDeviationMm(settings, kDiagonalMm);
}

[[nodiscard]] FabricationEvaluation Approximate(double targetDeviationMm,
    const FabricationMarkings& markings = FabricationMarkings{})
{
    const auto made = EvaluateFabrication(
        Definition(FabricationMethod::BandApproximation, targetDeviationMm), {Front()},
        markings, 0.01);
    Require(made.HasValue(), "V1 方式は前頭部を帯へ近似できる: "
            + (made.HasValue() ? std::string() : made.Diagnostics().front().summaryJa));
    return made.Value();
}

//! 帯の幅(mm)。平面視の弧に沿った長さで測る。
[[nodiscard]] double BandWidthMm(const BandMesh& mesh, int band)
{
    const auto& lower = mesh.world[static_cast<std::size_t>(band)];
    const auto& upper = mesh.world[static_cast<std::size_t>(band) + 1];
    return (upper[upper.size() / 2] - lower[lower.size() / 2]).Length();
}

} // namespace

KACHA_V2_TEST(er, 幅基準が1_87で3520mmになる)
{
    RequireNear(kBodyWidthMm, 40.4598, 1.0e-3, "3520/87 mm");
    const auto samples = SampleErFront(5, 5);
    RequireNear((samples.At(0, 4) - samples.At(0, 0)).x, kBodyWidthMm, 1.0e-9, "面の幅");
    RequireNear((samples.At(4, 0) - samples.At(0, 0)).y, kBodyHeightMm, 1.0e-9, "面の高さ");
}

KACHA_V2_TEST(er, V2方式は肩の二重曲率を理由に断りV1方式は帯へ切る)
{
    // 同じ面、同じ目標偏差(再現度9 = 0.03mm)。答えが違うのが要。
    // 肩の角欠損を平らにすると 0.06mm ほどずれる。V2 方式はそれを見て断り、
    // V1 方式は帯へ切って収める。
    const double target = DeviationForFidelity(9);
    const auto classified = EvaluateFabrication(
        Definition(FabricationMethod::ClassifyFaces, target), {Front()},
        FabricationMarkings{}, 0.01);
    Require(!classified.HasValue(), "V2 方式は伸ばさずに平らにできないので断る");
    const auto banded = Approximate(target);
    Require(banded.method == FabricationMethod::BandApproximation, "V1 方式");
    Require(banded.bandMesh.has_value(), "帯メッシュを持つ");
    Require(banded.bandMesh->BandCount() >= 2, "帯へ切る");
    // 収まらなければ収まらないと言う(最小幅 2mm の帯でも届かないことがある)。
    if (!banded.reachedTolerance) {
        Require(banded.summaryJa.find("超えて") != std::string::npos, "一文で言う");
    } else {
        Require(banded.maximumDeviationMm <= target + 1.0e-9, "報告した偏差も目標以下");
    }
    // 再現度6(0.11mm)なら収まる。
    const auto middle = Approximate(DeviationForFidelity(6));
    Require(middle.reachedTolerance, "再現度6では目標偏差に収まる");
    Require(middle.maximumDeviationMm <= DeviationForFidelity(6) + 1.0e-9, "偏差も目標以下");
}

KACHA_V2_TEST(er, 腰部と窓帯と額が大きな連続した部材になる)
{
    // 全面三角形の寄せ集めではなく、少数の幅広い帯になる(再現度3 = 0.31mm)。
    // 平面視の半径は約 37mm。0.31mm の偏差なら帯の幅は約 9mm 取れる。
    const auto made = Approximate(DeviationForFidelity(3));
    const BandMesh& mesh = *made.bandMesh;
    Require(mesh.BandCount() >= 2, "帯に切る");
    Require(mesh.BandCount() <= 8, "帯は少数: " + std::to_string(mesh.BandCount()));
    double widest = 0.0;
    for (int band = 0; band < mesh.BandCount(); ++band) {
        widest = std::max(widest, BandWidthMm(mesh, band));
    }
    Require(widest >= 4.0, "一番広い帯は最小幅(2mm)の2倍以上: " + std::to_string(widest) + " mm");
    for (const auto& panel : made.panels) {
        Require(panel.outline.size() >= 2 * 96, "部材は帯1本ぶんの連続した外周を持つ");
    }
    RequireEqual(std::to_string(made.panels.size()), std::to_string(mesh.BandCount()),
        "帯1つが部材1枚");
}

KACHA_V2_TEST(er, 部材の数が上限を超えない)
{
    auto definition = Definition(FabricationMethod::BandApproximation,
        DeviationForFidelity(9));
    definition.maximumPartCount = 6;
    const auto made = EvaluateFabrication(definition, {Front()}, FabricationMarkings{}, 0.01);
    Require(made.HasValue(), "上限があっても作る");
    Require(made.Value().bandMesh->BandCount() <= 6, "上限 6 を超えない");
    // 上限のせいで目標に届かないなら、届かないと言う(黙って成功にしない)。
    if (!made.Value().reachedTolerance) {
        Require(made.Value().summaryJa.find("超えて") != std::string::npos, "一文で言う");
    }
}

KACHA_V2_TEST(er, 強い二重曲率の肩にだけ追加分割が入る)
{
    // 肩を丸めた面と、丸めていない面(平面視の円筒だけ)で帯の数を比べる。
    // 円筒だけなら1方向曲げなので、V2 方式でも通り、V1 方式の帯は少ない。
    FabricationSource cylinderOnly;
    cylinderOnly.name = "円筒だけ";
    SurfacePatchSamples samples;
    samples.rowCount = 41;
    samples.columnCount = 41;
    for (std::size_t row = 0; row < 41; ++row) {
        for (std::size_t column = 0; column < 41; ++column) {
            const double across = 2.0 * (static_cast<double>(column) / 40.0 - 0.5);
            samples.points.push_back(Vector3{across * kBodyWidthMm * 0.5,
                static_cast<double>(row) / 40.0 * kBodyHeightMm,
                kPlanBulgeMm * (1.0 - across * across)});
        }
    }
    cylinderOnly.samples = samples;
    const double target = DeviationForFidelity(6);
    const auto flat = EvaluateFabrication(Definition(FabricationMethod::ClassifyFaces, target),
        {cylinderOnly}, FabricationMarkings{}, 0.01);
    Require(flat.HasValue(), "肩が無ければ V2 方式でも通る");
    const auto cylinderBands = EvaluateFabrication(
        Definition(FabricationMethod::BandApproximation, target), {cylinderOnly},
        FabricationMarkings{}, 0.01);
    const auto withShoulders = Approximate(target);
    Require(cylinderBands.HasValue(), "帯にもできる");
    Require(withShoulders.bandMesh->BandCount()
            >= cylinderBands.Value().bandMesh->BandCount(),
        "肩があると帯が増える(減らない)");
}

KACHA_V2_TEST(er, 再現度を上げると目標偏差が単調に小さくなる)
{
    const double coarse = DeviationForFidelity(3);
    const double middle = DeviationForFidelity(6);
    const double fine = DeviationForFidelity(9);
    Require(coarse > middle && middle > fine, "3 > 6 > 9");
}

KACHA_V2_TEST(er, 再現度3_6_9で最大偏差が単調非増加)
{
    double previous = 1.0e300;
    int previousBands = 0;
    for (const int fidelity : {3, 6, 9}) {
        const auto made = Approximate(DeviationForFidelity(fidelity));
        Require(made.maximumDeviationMm <= previous + 1.0e-9,
            "再現度 " + std::to_string(fidelity) + " で偏差が増えない");
        Require(made.bandMesh->BandCount() >= previousBands, "帯は減らない");
        previous = made.maximumDeviationMm;
        previousBands = made.bandMesh->BandCount();
    }
}

KACHA_V2_TEST(er, 6枚窓と前照灯が型紙に残る)
{
    // 開口は帯の型紙へ切り出される。帯の境目をまたぐ窓は、またぐ全ての帯へ取り分が開く。
    const auto made = Approximate(DeviationForFidelity(6), Openings());
    int pieces = 0;
    int panelsWithOpenings = 0;
    for (const auto& panel : made.panels) {
        pieces += static_cast<int>(panel.openings.size());
        panelsWithOpenings += panel.openings.empty() ? 0 : 1;
        for (const auto& opening : panel.openings) {
            Require(opening.size() >= 3, "取り分は多角形");
        }
    }
    Require(pieces >= 7, "6枚窓と前照灯の取り分が全部ある: " + std::to_string(pieces));
    Require(panelsWithOpenings >= 2, "複数の帯に窓が開く");
}

KACHA_V2_TEST(er, 前照灯を多角形へ置き換えない)
{
    // 円い前照灯は、型紙でも円に近い(取り分の点数が保たれ、細い三角にならない)。
    const auto made = Approximate(DeviationForFidelity(6), Openings());
    bool foundLamp = false;
    for (const auto& panel : made.panels) {
        for (const auto& opening : panel.openings) {
            if (opening.size() >= 24) {
                foundLamp = true;
            }
        }
    }
    Require(foundLamp, "前照灯の取り分が円のまま(24点以上)残る");
}

KACHA_V2_TEST(er, 面に載っていない窓は断る)
{
    FabricationMarkings markings;
    std::vector<Vector3> far{{0, 0, 200}, {10, 0, 200}, {10, 10, 200}};
    std::vector<CurveSegment> loop;
    for (std::size_t index = 0; index < far.size(); ++index) {
        loop.push_back(CurveSegment::MakeLine(far[index], far[(index + 1) % 3]).Value());
    }
    markings.openings.push_back(loop);
    const auto made = EvaluateFabrication(
        Definition(FabricationMethod::BandApproximation, DeviationForFidelity(6)), {Front()},
        markings, 0.01);
    Require(!made.HasValue(), "載っていない窓は断る");
    RequireEqual(made.Diagnostics().front().code, std::string("FAB-M003"), "理由の番号");
}

//! 帯1つ(下レール・上レール)の中の辺が、world の同じ帯とどれだけ違うか。
//! 帯の中の等長 = しわも縮尺変化も無い。帯どうしのつなぎ目は剛体で置くので、ここでは見ない。
[[nodiscard]] double MaximumBandEdgeChangeMm(const BandMesh& mesh, int band,
    const std::vector<Vector3>& bottom, const std::vector<Vector3>& top)
{
    const auto& worldBottom = mesh.world[static_cast<std::size_t>(band)];
    const auto& worldTop = mesh.world[static_cast<std::size_t>(band) + 1];
    double worst = 0.0;
    for (std::size_t column = 0; column < bottom.size(); ++column) {
        worst = std::max(worst, std::abs((top[column] - bottom[column]).Length()
                                   - (worldTop[column] - worldBottom[column]).Length()));
        if (column + 1 < bottom.size()) {
            worst = std::max(worst, std::abs((bottom[column + 1] - bottom[column]).Length()
                                       - (worldBottom[column + 1] - worldBottom[column]).Length()));
            worst = std::max(worst, std::abs((top[column + 1] - top[column]).Length()
                                       - (worldTop[column + 1] - worldTop[column]).Length()));
            // 三角形の対角線も。ここが伸びると、帯の中でねじれている。
            worst = std::max(worst, std::abs((bottom[column + 1] - top[column]).Length()
                                       - (worldBottom[column + 1] - worldTop[column]).Length()));
        }
    }
    return worst;
}

[[nodiscard]] std::vector<std::vector<Vector3>> RailsAt(const FabricationEvaluation& made,
    double percent)
{
    auto definition = Definition(FabricationMethod::BandApproximation, 0.1);
    definition.masterPercent = percent;
    return kachakacha::v2::app::FoldedRailsOf(definition, made, 0.0);
}

KACHA_V2_TEST(er, 30パーセントでしわも縮尺変化も起きない)
{
    // 画面と固定が使う姿勢(FoldedRailsOf)そのもので見る。帯ごとに等長。
    const auto made = Approximate(DeviationForFidelity(6), Openings());
    const BandMesh& mesh = *made.bandMesh;
    const auto rails = RailsAt(made, 30.0);
    Require(rails.size() == static_cast<std::size_t>(mesh.BandCount()) * 2, "2×帯数");
    for (int band = 0; band < mesh.BandCount(); ++band) {
        const auto& bottom = rails[static_cast<std::size_t>(band) * 2];
        const auto& top = rails[static_cast<std::size_t>(band) * 2 + 1];
        RequireNear(MaximumBandEdgeChangeMm(mesh, band, bottom, top), 0.0, 1.0e-6,
            "帯 " + std::to_string(band + 1) + " の辺の長さは 30% でも変わらない");
    }
    // 開口は型紙(展開)に持ち、曲げ状態で消えない(型紙の取り分は姿勢と無関係)。
    int pieces = 0;
    for (const auto& panel : made.panels) {
        pieces += static_cast<int>(panel.openings.size());
    }
    Require(pieces >= 7, "30% でも開口の取り分は残る");
}

KACHA_V2_TEST(er, 100パーセントで完成形と一致し0パーセントは平ら)
{
    const auto made = Approximate(DeviationForFidelity(6));
    const BandMesh& mesh = *made.bandMesh;
    const auto full = RailsAt(made, 100.0);
    double worst = 0.0;
    for (int band = 0; band < mesh.BandCount(); ++band) {
        const auto& bottom = full[static_cast<std::size_t>(band) * 2];
        const auto& top = full[static_cast<std::size_t>(band) * 2 + 1];
        for (std::size_t column = 0; column < bottom.size(); ++column) {
            worst = std::max(worst,
                (bottom[column] - mesh.world[static_cast<std::size_t>(band)][column]).Length());
            worst = std::max(worst,
                (top[column] - mesh.world[static_cast<std::size_t>(band) + 1][column]).Length());
        }
    }
    RequireNear(worst, 0.0, 1.0e-6, "100% は完成形(閉じ残り 0)");
    const auto flat = RailsAt(made, 0.0);
    for (int band = 0; band < mesh.BandCount(); ++band) {
        const auto& bottom = flat[static_cast<std::size_t>(band) * 2];
        const auto& top = flat[static_cast<std::size_t>(band) * 2 + 1];
        // 0% では各帯が1枚の平面に載る。
        const Vector3 a = bottom.back() - bottom.front();
        const Vector3 b = top[top.size() / 2] - bottom.front();
        Vector3 normal = Cross(a, b);
        normal = normal * (1.0 / std::max(normal.Length(), 1.0e-12));
        double thickness = 0.0;
        for (const auto& point : bottom) {
            thickness = std::max(thickness, std::abs(Dot(point - bottom.front(), normal)));
        }
        for (const auto& point : top) {
            thickness = std::max(thickness, std::abs(Dot(point - bottom.front(), normal)));
        }
        RequireNear(thickness, 0.0, 1.0e-6, "帯 " + std::to_string(band + 1) + " は 0% で平ら");
        RequireNear(MaximumBandEdgeChangeMm(mesh, band, bottom, top), 0.0, 1.0e-6,
            "平らでも等長");
    }
}

KACHA_V2_TEST_MAIN("acceptance_er_tests")
