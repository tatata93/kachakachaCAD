// 開口を帯の型紙へ切り出す(app/FabricationOpenings.h)。またぐ窓の切り口が境目に沿うこと。
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/app/FabricationOpenings.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/BandFold.h"

#include <algorithm>
#include <cmath>
#include <string>

using kachakacha::v2::app::BandOpeningTarget;
using kachakacha::v2::app::ClipOpeningIntoBandPanels;
using kachakacha::v2::fabrication::BandMesh;
using kachakacha::v2::fabrication::PatternPanel;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

constexpr double kPi = 3.14159265358979323846;

//! 半径 50 の円筒の 1/4。x は 0..80(列)、角度は 0..π/2(行)。
[[nodiscard]] Vector3 Cylinder(double u, double v)
{
    const double angle = v * kPi / 2.0;
    return Vector3{u * 80.0, 50.0 * std::cos(angle), 50.0 * std::sin(angle)};
}

[[nodiscard]] SurfacePatchSamples Samples()
{
    SurfacePatchSamples samples;
    samples.rowCount = 25;
    samples.columnCount = 9;
    for (std::size_t row = 0; row < 25; ++row) {
        for (std::size_t column = 0; column < 9; ++column) {
            samples.points.push_back(Cylinder(column / 8.0, row / 24.0));
        }
    }
    return samples;
}

//! 面の上の四角(u,v)。
[[nodiscard]] std::vector<CurveSegment> Window(double u0, double v0, double u1, double v1)
{
    std::vector<Vector3> points;
    const auto edge = [&](double ua, double va, double ub, double vb) {
        for (int step = 0; step < 8; ++step) {
            const double t = step / 8.0;
            points.push_back(Cylinder(ua + (ub - ua) * t, va + (vb - va) * t));
        }
    };
    edge(u0, v0, u1, v0);
    edge(u1, v0, u1, v1);
    edge(u1, v1, u0, v1);
    edge(u0, v1, u0, v0);
    std::vector<CurveSegment> segments;
    for (std::size_t index = 0; index < points.size(); ++index) {
        segments.push_back(
            CurveSegment::MakeLine(points[index], points[(index + 1) % points.size()]).Value());
    }
    return segments;
}

struct Made {
    BandMesh mesh;
    std::vector<double> rails;
    std::vector<PatternPanel> panels;
    //! 近似の偏差。載っているかの許容は「偏差 + 0.35mm」(EvaluateByBands と同じ)。
    double deviationMm = 0.0;
};

//! 手動境界 v = 0.5 で2つの帯に切る(分割軸 V)。
[[nodiscard]] Made TwoBands()
{
    kachakacha::v2::fabrication::SampledSurface surface(Samples());
    kachakacha::v2::fabrication::BandApproximationOptions options;
    options.splitAxis = kachakacha::v2::fabrication::BandSplitAxis::V;
    options.automaticBoundaries = false;
    options.manualBoundaries = {0.5};
    const auto bands = kachakacha::v2::fabrication::ApproximateBands(surface, options);
    Require(bands.HasValue(), "帯にできる");
    const auto mesh = kachakacha::v2::fabrication::DevelopBandMesh(surface,
        options.splitAxis, bands.Value().railParameters, 48);
    Require(mesh.HasValue(), "展開できる");
    Made made;
    made.mesh = mesh.Value();
    made.rails = bands.Value().railParameters;
    made.deviationMm = bands.Value().maximumDeviationMm;
    made.panels = kachakacha::v2::app::PanelsFromBandMesh("筒", made.mesh,
        kachakacha::v2::fabrication::MeasureCreaseAngles(made.mesh));
    return made;
}

[[nodiscard]] double DistanceToRail(const BandMesh& mesh, std::size_t row, const Vector3& point)
{
    double best = 1.0e300;
    const auto& rail = mesh.world[row];
    for (std::size_t column = 0; column + 1 < rail.size(); ++column) {
        const Vector3 a = rail[column];
        const Vector3 b = rail[column + 1];
        const Vector3 ab = b - a;
        const double length2 = Dot(ab, ab);
        const double t = length2 > 0.0 ? std::clamp(Dot(point - a, ab) / length2, 0.0, 1.0)
                                       : 0.0;
        best = std::min(best, (point - (a + ab * t)).Length());
    }
    return best;
}

} // namespace

KACHA_V2_TEST(fabrication_openings, 帯の中に収まる窓は1つの帯にだけ開く)
{
    Made made = TwoBands();
    BandOpeningTarget target;
    target.mesh = &made.mesh;
    target.railParameters = made.rails;
    target.snapToleranceMm = made.deviationMm + 0.35;
    Require(ClipOpeningIntoBandPanels(target, Window(0.2, 0.1, 0.4, 0.3), 0.05, made.panels),
        "載っている");
    RequireEqual(std::to_string(made.panels[0].openings.size()), std::string("1"), "帯1に1つ");
    RequireEqual(std::to_string(made.panels[1].openings.size()), std::string("0"), "帯2には無い");
}

KACHA_V2_TEST(fabrication_openings, またぐ窓は両方の帯に開き切り口は境目に沿う)
{
    Made made = TwoBands();
    BandOpeningTarget target;
    target.mesh = &made.mesh;
    target.railParameters = made.rails;
    target.snapToleranceMm = made.deviationMm + 0.35;
    const auto window = Window(0.2, 0.35, 0.6, 0.65);
    Require(ClipOpeningIntoBandPanels(target, window, 0.05, made.panels), "載っている");
    RequireEqual(std::to_string(made.panels[0].openings.size()), std::string("1"), "帯1に取り分");
    RequireEqual(std::to_string(made.panels[1].openings.size()), std::string("1"), "帯2に取り分");
    // 切り口は境目(レール1)に沿う。取り分の点のうち、境目の上に載る点が
    // 交点2つだけでなく、その間のレールの点も含む(直線1本の弦にしない)。
    // 型紙では確かめられないので、同じ切り出しを 3D で数える。
    const auto& developedLower = made.panels[0].openings.front();
    const auto& developedUpper = made.panels[1].openings.front();
    const auto onRail1 = [&made](const std::vector<kachakacha::v2::geometry::Point2>& piece) {
        const auto& rail = made.mesh.developed[1];
        int count = 0;
        for (const auto& point : piece) {
            double best = 1.0e300;
            for (std::size_t column = 0; column + 1 < rail.size(); ++column) {
                const double ax = rail[column].u, ay = rail[column].v;
                const double bx = rail[column + 1].u, by = rail[column + 1].v;
                const double dx = bx - ax, dy = by - ay;
                const double length2 = dx * dx + dy * dy;
                const double t = length2 > 0.0
                    ? std::clamp(((point.u - ax) * dx + (point.v - ay) * dy) / length2, 0.0, 1.0)
                    : 0.0;
                const double ex = ax + dx * t - point.u, ey = ay + dy * t - point.v;
                best = std::min(best, std::sqrt(ex * ex + ey * ey));
            }
            count += best < 1.0e-6 ? 1 : 0;
        }
        return count;
    };
    // 境目の上に載る点が、交点2つだけではなく、間のレールの点も含む(弦にしない)。
    Require(onRail1(developedLower) >= 3,
        "帯1の切り口は境目に沿う: " + std::to_string(onRail1(developedLower)) + " 点");
    Require(onRail1(developedUpper) >= 3,
        "帯2の切り口は境目に沿う: " + std::to_string(onRail1(developedUpper)) + " 点");
    // 取り分の点が、その帯の展開範囲の中にある(隣の帯へはみ出さない)。
    const auto& rail0 = made.mesh.developed[0];
    const auto& rail1 = made.mesh.developed[1];
    const auto& rail2 = made.mesh.developed[2];
    const double lowMin = std::min(rail0.front().v, rail1.front().v) - 1.0e-6;
    const double lowMax = std::max(rail0.front().v, rail1.front().v) + 1.0e-6;
    const double highMin = std::min(rail1.front().v, rail2.front().v) - 1.0e-6;
    const double highMax = std::max(rail1.front().v, rail2.front().v) + 1.0e-6;
    for (const auto& point : developedLower) {
        Require(point.v >= lowMin && point.v <= lowMax, "帯1の範囲の中");
    }
    for (const auto& point : developedUpper) {
        Require(point.v >= highMin && point.v <= highMax, "帯2の範囲の中");
    }
}

KACHA_V2_TEST(fabrication_openings, 載っていない窓は載っていないと言う)
{
    Made made = TwoBands();
    BandOpeningTarget target;
    target.mesh = &made.mesh;
    target.railParameters = made.rails;
    target.snapToleranceMm = made.deviationMm + 0.35;
    std::vector<Vector3> far{{0, 300, 0}, {10, 300, 0}, {10, 310, 0}};
    std::vector<CurveSegment> loop;
    for (std::size_t index = 0; index < 3; ++index) {
        loop.push_back(CurveSegment::MakeLine(far[index], far[(index + 1) % 3]).Value());
    }
    Require(!ClipOpeningIntoBandPanels(target, loop, 0.05, made.panels), "載っていない");
    Require(made.panels[0].openings.empty() && made.panels[1].openings.empty(), "何も開かない");
}

KACHA_V2_TEST_MAIN("fabrication_openings")
