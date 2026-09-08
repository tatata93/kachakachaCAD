// 製作近似の土台(fabrication-contract.md §3〜§4)。
// 大事なのは「伸ばして平らにしない」こと。展開できない面は展開できないと言う。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/CurvatureAnalysis.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/fabrication/Unfold.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AnalyticSurfaceInfo;
using kachakacha::v2::fabrication::AnalyticSurfaceKind;
using kachakacha::v2::fabrication::AnalyzeCurvature;
using kachakacha::v2::fabrication::CheckLengthPreservation;
using kachakacha::v2::fabrication::DevelopableStrip;
using kachakacha::v2::fabrication::FabricationSettings;
using kachakacha::v2::fabrication::PanelGeometryClass;
using kachakacha::v2::fabrication::PanelGeometryClassNameJa;
using kachakacha::v2::fabrication::ResolveTargetMaxDeviationMm;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::fabrication::UnfoldOnCone;
using kachakacha::v2::fabrication::UnfoldOnCylinder;
using kachakacha::v2::fabrication::UnfoldOnPlane;
using kachakacha::v2::fabrication::UnfoldSamples;
using kachakacha::v2::fabrication::UnfoldStrip;
using kachakacha::v2::fabrication::ValidateFabricationSettings;
using kachakacha::v2::geometry::Point2;
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

//! 面を格子で標本化する。f(u, v) を rows × columns で。
template<class Function>
[[nodiscard]] SurfacePatchSamples Sample(std::size_t rows, std::size_t columns,
    Function&& surface)
{
    SurfacePatchSamples samples;
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            const double u = static_cast<double>(row) / static_cast<double>(rows - 1);
            const double v = static_cast<double>(column) / static_cast<double>(columns - 1);
            samples.points.push_back(surface(u, v));
        }
    }
    return samples;
}

//! 平面。
[[nodiscard]] SurfacePatchSamples PlanePatch(double width, double height)
{
    return Sample(11, 11, [&](double u, double v) {
        return Vector3{v * width, u * height, 0.0};
    });
}

//! 円筒の一部。半径 r、高さ h、掃引角 sweep。
[[nodiscard]] SurfacePatchSamples CylinderPatch(double radius, double height, double sweep)
{
    return Sample(11, 21, [&](double u, double v) {
        const double angle = v * sweep;
        return Vector3{radius * std::cos(angle), radius * std::sin(angle), u * height};
    });
}

//! 円錐の一部。頂点は原点、半頂角 halfAngle、母線長 s0..s1。
[[nodiscard]] SurfacePatchSamples ConePatch(double halfAngle, double s0, double s1,
    double sweep)
{
    return Sample(11, 21, [&](double u, double v) {
        const double slant = s0 + (s1 - s0) * u;
        const double angle = v * sweep;
        const double radius = slant * std::sin(halfAngle);
        const double along = slant * std::cos(halfAngle);
        return Vector3{radius * std::cos(angle), radius * std::sin(angle), along};
    });
}

//! 球の一部。展開できない面。
[[nodiscard]] SurfacePatchSamples SpherePatch(double radius, double extent)
{
    return Sample(11, 11, [&](double u, double v) {
        const double theta = (u - 0.5) * extent;
        const double phi = (v - 0.5) * extent;
        return Vector3{radius * std::cos(theta) * std::cos(phi),
            radius * std::cos(theta) * std::sin(phi), radius * std::sin(theta)};
    });
}

//! 標本の端どうしを帯にする。
[[nodiscard]] DevelopableStrip StripFrom(const SurfacePatchSamples& samples)
{
    DevelopableStrip strip;
    for (std::size_t column = 0; column < samples.columnCount; ++column) {
        strip.firstRail.push_back(samples.At(0, column));
        strip.secondRail.push_back(samples.At(samples.rowCount - 1, column));
    }
    return strip;
}

} // namespace

// ---------------------------------------------------------------- 再現度

KACHA_V2_TEST(fabrication, 再現度から目標偏差を出す)
{
    FabricationSettings settings;
    const double diagonal = 200.0;
    // 1 が一番粗く、10 が一番細かい。
    settings.fidelityLevel = 1;
    const double coarse = ResolveTargetMaxDeviationMm(settings, diagonal);
    settings.fidelityLevel = 10;
    const double fine = ResolveTargetMaxDeviationMm(settings, diagonal);
    RequireNear(coarse, std::max(0.50, diagonal * 0.010), 1e-12, "一番粗い値");
    RequireNear(fine, std::max(0.03, diagonal * 0.0005), 1e-12, "一番細かい値");
    Require(coarse > fine, "粗いほうが大きいこと");

    // 途中は対数で補間する。5 と 6 の間が、両端の比の1/9乗ぶんになる。
    settings.fidelityLevel = 5;
    const double five = ResolveTargetMaxDeviationMm(settings, diagonal);
    settings.fidelityLevel = 6;
    const double six = ResolveTargetMaxDeviationMm(settings, diagonal);
    RequireNear(five / six, std::pow(coarse / fine, 1.0 / 9.0), 1e-9, "対数の等比");

    // 単調であること。
    double previous = 1.0e30;
    for (int level = 1; level <= 10; ++level) {
        settings.fidelityLevel = level;
        const double value = ResolveTargetMaxDeviationMm(settings, diagonal);
        Require(value < previous, "細かくなるほど小さくなること");
        previous = value;
    }
}

KACHA_V2_TEST(fabrication, 明示した偏差が再現度より優先される)
{
    FabricationSettings settings;
    settings.fidelityLevel = 1;
    settings.explicitMaxDeviationMm = 0.07;
    RequireNear(ResolveTargetMaxDeviationMm(settings, 200.0), 0.07, 1e-12, "明示値");
}

KACHA_V2_TEST(fabrication, 小さいモデルでは下限が効く)
{
    FabricationSettings settings;
    settings.fidelityLevel = 10;
    // 対角 10mm では 10*0.0005 = 0.005 だが、下限 0.03 が効く。
    RequireNear(ResolveTargetMaxDeviationMm(settings, 10.0), 0.03, 1e-12, "下限");
    settings.fidelityLevel = 1;
    RequireNear(ResolveTargetMaxDeviationMm(settings, 10.0), 0.50, 1e-12, "粗い側の下限");
}

KACHA_V2_TEST(fabrication, おかしな設定を断る)
{
    FabricationSettings settings;
    Require(ValidateFabricationSettings(settings).empty(), "既定は通ること");

    const auto rejects = [](FabricationSettings changed, const std::string& why) {
        Require(!ValidateFabricationSettings(changed).empty(), "断ること: " + why);
    };
    FabricationSettings bad = settings;
    bad.fidelityLevel = 0;
    rejects(bad, "再現度0");
    bad = settings;
    bad.fidelityLevel = 11;
    rejects(bad, "再現度11");
    bad = settings;
    bad.panelCountLimit = 0;
    rejects(bad, "部材数0");
    bad = settings;
    bad.panelCountLimit = 500;
    rejects(bad, "部材数500");
    bad = settings;
    bad.minimumPanelWidthMm = 0.0;
    rejects(bad, "最小幅0");
    bad = settings;
    bad.maximumReliefDepthRatio = 1.0;
    rejects(bad, "深さ比1.0");
    bad = settings;
    bad.minimumLigamentMm = -1.0;
    rejects(bad, "残す幅が負");
    bad = settings;
    bad.outputThicknessMm = 0.0;
    rejects(bad, "板厚0");
    bad = settings;
    bad.explicitMaxDeviationMm = -0.1;
    rejects(bad, "明示偏差が負");
    bad = settings;
    bad.allowedPanelTypes = {false, false, false, false};
    rejects(bad, "使える種類が無い");
}

KACHA_V2_TEST(fabrication, 開口を消す設定は作れない)
{
    // §3「preserveOpenings は常にtrueであり、UIで無効化してはならない」。
    FabricationSettings settings;
    settings.preserveOpenings = false;
    const auto errors = ValidateFabricationSettings(settings);
    Require(!errors.empty(), "断ること");
    Require(errors.front().summaryJa.find("開口") != std::string::npos,
        "理由が開口であること");
}

// ---------------------------------------------------------------- 曲がり方

KACHA_V2_TEST(curvature, 平面を平面と判る)
{
    const auto analysis = AnalyzeCurvature(PlanePatch(100.0, 60.0), 0.1);
    Require(analysis.HasValue(), "測れること");
    Require(analysis.Value().classification == PanelGeometryClass::Planar,
        std::string("平面であること (実際 ")
            + std::string(PanelGeometryClassNameJa(analysis.Value().classification)) + ")");
    RequireNear(analysis.Value().maximumAbsoluteGaussian, 0.0, 1e-9, "Gauss曲率0");
}

KACHA_V2_TEST(curvature, 円筒を展開できる形と判る)
{
    for (const double radius : {10.0, 30.0, 100.0}) {
        const auto analysis = AnalyzeCurvature(CylinderPatch(radius, 50.0, 1.2), 0.1);
        Require(analysis.HasValue(), "測れること");
        const auto kind = analysis.Value().classification;
        Require(kind == PanelGeometryClass::Cylindrical,
            "半径 " + std::to_string(radius) + " は円筒 (実際 "
                + std::string(PanelGeometryClassNameJa(kind)) + ")");
        // 主曲率は 1/半径。
        RequireNear(analysis.Value().maximumAbsolutePrincipal, 1.0 / radius, 1.0 / radius * 0.05,
            "主曲率");
    }
}

KACHA_V2_TEST(curvature, 円錐も展開できる形と判る)
{
    const auto analysis = AnalyzeCurvature(ConePatch(0.4, 30.0, 90.0, 1.0), 0.1);
    Require(analysis.HasValue(), "測れること");
    const auto kind = analysis.Value().classification;
    Require(kind == PanelGeometryClass::Conical || kind == PanelGeometryClass::Cylindrical,
        "展開できる形であること (実際 " + std::string(PanelGeometryClassNameJa(kind)) + ")");
    Require(kind != PanelGeometryClass::DoubleCurved, "二重曲率ではないこと");
}

KACHA_V2_TEST(curvature, 球は二重曲率と判る)
{
    const auto analysis = AnalyzeCurvature(SpherePatch(50.0, 0.8), 0.1);
    Require(analysis.HasValue(), "測れること");
    Require(analysis.Value().classification == PanelGeometryClass::DoubleCurved,
        "二重曲率であること");
    // 球の Gauss曲率は 1/r^2。
    RequireNear(analysis.Value().maximumAbsoluteGaussian, 1.0 / (50.0 * 50.0),
        1.0 / (50.0 * 50.0) * 0.1, "Gauss曲率");
    Require(analysis.Value().doubleCurvedRatio > 0.9, "ほとんどが二重曲率");
}

KACHA_V2_TEST(curvature, 大きい球でも目標が細かければ二重曲率と判る)
{
    // 半径を大きくすると曲率は小さくなるが、目標偏差を細かくすれば見逃さない。
    const auto coarse = AnalyzeCurvature(SpherePatch(2000.0, 0.2), 5.0);
    Require(coarse.HasValue(), "測れること");
    const auto fine = AnalyzeCurvature(SpherePatch(2000.0, 0.2), 0.01);
    Require(fine.HasValue(), "測れること");
    Require(fine.Value().classification == PanelGeometryClass::DoubleCurved,
        "細かい目標では二重曲率と判ること");
}

KACHA_V2_TEST(curvature, 標本が足りなければ測らない)
{
    SurfacePatchSamples tiny;
    tiny.rowCount = 2;
    tiny.columnCount = 2;
    tiny.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    Require(!AnalyzeCurvature(tiny, 0.1).HasValue(), "2x2 では測らない");

    SurfacePatchSamples broken;
    broken.rowCount = 3;
    broken.columnCount = 3;
    broken.points.resize(5);   // 数が合っていない
    Require(!AnalyzeCurvature(broken, 0.1).HasValue(), "数が合わなければ測らない");
}

KACHA_V2_TEST(curvature, 有限でない値を断る)
{
    SurfacePatchSamples samples = PlanePatch(100.0, 60.0);
    samples.points[10].z = std::nan("");
    Require(!AnalyzeCurvature(samples, 0.1).HasValue(), "NaN を断ること");
}

// ---------------------------------------------------------------- 展開

KACHA_V2_TEST(unfold, 平面を厳密に展開する)
{
    AnalyticSurfaceInfo plane;
    plane.kind = AnalyticSurfaceKind::Plane;
    plane.origin = {0.0, 0.0, 5.0};
    plane.axis = {0.0, 0.0, 1.0};
    plane.reference = {1.0, 0.0, 0.0};
    const std::vector<Vector3> points{{0, 0, 5}, {30, 0, 5}, {30, 40, 5}, {0, 40, 5}};
    const auto flat = UnfoldOnPlane(plane, points, 1e-9);
    Require(flat.HasValue(), "展開できること");
    const auto check = CheckLengthPreservation(points, flat.Value());
    Require(check.withinTolerance, "長さが保たれること");
    RequireNear(flat.Value()[1].u, 30.0, 1e-12, "x");
    RequireNear(flat.Value()[2].v, 40.0, 1e-12, "y");
}

KACHA_V2_TEST(unfold, 平面から外れた点を断る)
{
    AnalyticSurfaceInfo plane;
    plane.kind = AnalyticSurfaceKind::Plane;
    plane.axis = {0.0, 0.0, 1.0};
    plane.reference = {1.0, 0.0, 0.0};
    Require(!UnfoldOnPlane(plane, {{0, 0, 0}, {10, 0, 0.5}}, 1e-6).HasValue(),
        "外れた点を断ること");
}

KACHA_V2_TEST(unfold, 円筒を厳密に展開する)
{
    // 半径20の円筒の上の点。展開すると弧長 = 半径 × 角度。
    AnalyticSurfaceInfo cylinder;
    cylinder.kind = AnalyticSurfaceKind::Cylinder;
    cylinder.origin = {0.0, 0.0, 0.0};
    cylinder.axis = {0.0, 0.0, 1.0};
    cylinder.reference = {1.0, 0.0, 0.0};
    cylinder.radiusMm = 20.0;

    std::vector<Vector3> points;
    for (int index = 0; index <= 20; ++index) {
        const double angle = static_cast<double>(index) / 20.0 * 1.5;
        points.push_back({20.0 * std::cos(angle), 20.0 * std::sin(angle), 7.0});
    }
    const auto flat = UnfoldOnCylinder(cylinder, points, 1e-9);
    Require(flat.HasValue(), "展開できること");
    // 全長は 半径 × 掃引角。
    double total = 0.0;
    for (std::size_t index = 1; index < flat.Value().size(); ++index) {
        const double du = flat.Value()[index].u - flat.Value()[index - 1].u;
        const double dv = flat.Value()[index].v - flat.Value()[index - 1].v;
        total += std::sqrt(du * du + dv * dv);
    }
    RequireNear(total, 20.0 * 1.5, 1e-9, "弧長");
    // 高さはそのまま。
    for (const Point2& point : flat.Value()) {
        RequireNear(point.v, 7.0, 1e-9, "高さ");
    }
}

KACHA_V2_TEST(unfold, 円筒の一周またぎで飛ばない)
{
    AnalyticSurfaceInfo cylinder;
    cylinder.kind = AnalyticSurfaceKind::Cylinder;
    cylinder.axis = {0.0, 0.0, 1.0};
    cylinder.reference = {1.0, 0.0, 0.0};
    cylinder.radiusMm = 15.0;
    // -170度から +170度までを、180度をまたいで進む。
    std::vector<Vector3> points;
    for (int index = 0; index <= 40; ++index) {
        const double angle = kPi * 0.9 + static_cast<double>(index) / 40.0 * 0.4;
        points.push_back({15.0 * std::cos(angle), 15.0 * std::sin(angle), 0.0});
    }
    const auto flat = UnfoldOnCylinder(cylinder, points, 1e-9);
    Require(flat.HasValue(), "展開できること");
    // 隣どうしが飛んでいないこと。
    for (std::size_t index = 1; index < flat.Value().size(); ++index) {
        const double step = std::abs(flat.Value()[index].u - flat.Value()[index - 1].u);
        Require(step < 1.0, "急に飛ばないこと (" + std::to_string(step) + ")");
    }
    const auto check = CheckLengthPreservation(points, flat.Value(), 1e-3);
    Require(check.withinTolerance, "長さがおおよそ保たれること");
}

KACHA_V2_TEST(unfold, 円筒から外れた点を断る)
{
    AnalyticSurfaceInfo cylinder;
    cylinder.kind = AnalyticSurfaceKind::Cylinder;
    cylinder.axis = {0.0, 0.0, 1.0};
    cylinder.reference = {1.0, 0.0, 0.0};
    cylinder.radiusMm = 20.0;
    Require(!UnfoldOnCylinder(cylinder, {{20, 0, 0}, {25, 0, 0}}, 1e-6).HasValue(),
        "半径が違う点を断ること");
    cylinder.radiusMm = 0.0;
    Require(!UnfoldOnCylinder(cylinder, {{0, 0, 0}}, 1e-6).HasValue(), "半径0を断ること");
}

KACHA_V2_TEST(unfold, 円錐を厳密に展開する)
{
    // 半頂角 30度、頂点は原点。母線長 s のところで、まわりの角度は sin(30度)=0.5 倍になる。
    const double halfAngle = kPi / 6.0;
    AnalyticSurfaceInfo cone;
    cone.kind = AnalyticSurfaceKind::Cone;
    cone.origin = {0.0, 0.0, 0.0};
    cone.axis = {0.0, 0.0, 1.0};
    cone.reference = {1.0, 0.0, 0.0};
    cone.halfAngleRad = halfAngle;

    // 母線長 60 の円周上を 120度ぶん。
    const double slant = 60.0;
    std::vector<Vector3> points;
    for (int index = 0; index <= 30; ++index) {
        const double angle = static_cast<double>(index) / 30.0 * (2.0 * kPi / 3.0);
        const double radius = slant * std::sin(halfAngle);
        const double along = slant * std::cos(halfAngle);
        points.push_back({radius * std::cos(angle), radius * std::sin(angle), along});
    }
    const auto flat = UnfoldOnCone(cone, points, 1e-9);
    Require(flat.HasValue(), "展開できること");
    // 展開図でも、頂点からの距離は母線長のまま。
    for (const Point2& point : flat.Value()) {
        RequireNear(std::sqrt(point.u * point.u + point.v * point.v), slant, 1e-9,
            "頂点からの距離");
    }
    // 弧長も保たれる。立体上の円周の弧長 = 半径 × 角度。
    const double spatialArc = slant * std::sin(halfAngle) * (2.0 * kPi / 3.0);
    double flatArc = 0.0;
    for (std::size_t index = 1; index < flat.Value().size(); ++index) {
        const double du = flat.Value()[index].u - flat.Value()[index - 1].u;
        const double dv = flat.Value()[index].v - flat.Value()[index - 1].v;
        flatArc += std::sqrt(du * du + dv * dv);
    }
    Require(std::abs(flatArc - spatialArc) < spatialArc * 1e-3,
        "弧長 (" + std::to_string(flatArc) + " / " + std::to_string(spatialArc) + ")");
}

KACHA_V2_TEST(unfold, 円錐の母線方向の長さも保たれる)
{
    const double halfAngle = kPi / 5.0;
    AnalyticSurfaceInfo cone;
    cone.kind = AnalyticSurfaceKind::Cone;
    cone.axis = {0.0, 0.0, 1.0};
    cone.reference = {1.0, 0.0, 0.0};
    cone.halfAngleRad = halfAngle;
    // 同じ角度で、母線に沿って進む。
    std::vector<Vector3> points;
    for (int index = 0; index <= 20; ++index) {
        const double slant = 20.0 + static_cast<double>(index) * 3.0;
        points.push_back({slant * std::sin(halfAngle), 0.0, slant * std::cos(halfAngle)});
    }
    const auto flat = UnfoldOnCone(cone, points, 1e-9);
    Require(flat.HasValue(), "展開できること");
    const auto check = CheckLengthPreservation(points, flat.Value(), 1e-9);
    Require(check.withinTolerance,
        "長さが保たれること (" + std::to_string(check.maximumRelativeError) + ")");
}

// ---------------------------------------------------------------- 帯の展開

KACHA_V2_TEST(unfold, 平らな帯は伸縮なしで展開できる)
{
    DevelopableStrip strip;
    for (int index = 0; index <= 10; ++index) {
        const double x = static_cast<double>(index) * 5.0;
        strip.firstRail.push_back({x, 0.0, 0.0});
        strip.secondRail.push_back({x, 20.0, 0.0});
    }
    const auto unfolded = UnfoldStrip(strip, 0.1);
    Require(unfolded.HasValue(), "展開できること");
    Require(unfolded.Value().maximumLengthErrorRelative < 1e-12, "長さが変わらないこと");
    Require(unfolded.Value().maximumPlanarityErrorMm < 1e-9, "平面から外れないこと");
}

KACHA_V2_TEST(unfold, 円筒の帯を展開すると長さが保たれる)
{
    const auto samples = CylinderPatch(25.0, 40.0, 1.4);
    const auto unfolded = UnfoldSamples(samples, 0.05);
    Require(unfolded.HasValue(), "展開できること ("
            + (unfolded.Diagnostics().empty() ? std::string("診断なし")
                                              : unfolded.Diagnostics().front().detailsJa)
            + ")");
    Require(unfolded.Value().maximumLengthErrorRelative <= 1e-6,
        "§10.2 の 1e-6 を満たすこと ("
            + std::to_string(unfolded.Value().maximumLengthErrorRelative) + ")");

    // 展開後の縁の長さが、立体の上の弧長と合うこと。
    double flatLength = 0.0;
    const auto& rail = unfolded.Value().firstRail;
    for (std::size_t index = 1; index < rail.size(); ++index) {
        const double du = rail[index].u - rail[index - 1].u;
        const double dv = rail[index].v - rail[index - 1].v;
        flatLength += std::sqrt(du * du + dv * dv);
    }
    double spatialLength = 0.0;
    for (std::size_t index = 1; index < samples.columnCount; ++index) {
        spatialLength += (samples.At(0, index) - samples.At(0, index - 1)).Length();
    }
    RequireNear(flatLength, spatialLength, spatialLength * 1e-9, "縁の長さ");
}

KACHA_V2_TEST(unfold, 円錐の帯も展開できる)
{
    const auto samples = ConePatch(0.45, 40.0, 90.0, 1.1);
    const auto unfolded = UnfoldSamples(samples, 0.05);
    Require(unfolded.HasValue(), "展開できること");
    Require(unfolded.Value().maximumLengthErrorRelative <= 1e-6, "長さが保たれること");
}

KACHA_V2_TEST(unfold, 球の帯は展開できないと言う)
{
    // ここが肝。伸ばして平らにして「できた」と言ってはいけない。
    const auto samples = SpherePatch(40.0, 1.2);
    const auto unfolded = UnfoldSamples(samples, 0.05);
    Require(!unfolded.HasValue(), "断ること");
    RequireEqual(unfolded.Diagnostics().front().code, std::string("FAB-U002"), "診断コード");
    Require(unfolded.Diagnostics().front().detailsJa.find("切れ目") != std::string::npos,
        "どうすればよいかを言うこと");
}

KACHA_V2_TEST(unfold, 目標が粗ければ緩い二重曲率は通る)
{
    // 同じ球でも、目標偏差が粗ければ1枚で通してよい。基準は目標偏差ひとつ。
    const auto samples = SpherePatch(400.0, 0.3);
    Require(!UnfoldSamples(samples, 0.001).HasValue(), "細かい目標では断ること");
    Require(UnfoldSamples(samples, 5.0).HasValue(), "粗い目標では通ること");
}

KACHA_V2_TEST(unfold, 壊れた帯を断る)
{
    DevelopableStrip empty;
    Require(!UnfoldStrip(empty, 0.1).HasValue(), "空を断ること");

    DevelopableStrip mismatched;
    mismatched.firstRail = {{0, 0, 0}, {10, 0, 0}};
    mismatched.secondRail = {{0, 10, 0}};
    Require(!UnfoldStrip(mismatched, 0.1).HasValue(), "数が合わないものを断ること");

    DevelopableStrip degenerate;
    degenerate.firstRail = {{0, 0, 0}, {10, 0, 0}};
    degenerate.secondRail = {{0, 0, 0}, {10, 0, 0}};   // 母線の長さが0
    Require(!UnfoldStrip(degenerate, 0.1).HasValue(), "潰れた帯を断ること");

    DevelopableStrip infinite;
    infinite.firstRail = {{0, 0, 0}, {std::nan(""), 0, 0}};
    infinite.secondRail = {{0, 10, 0}, {10, 10, 0}};
    Require(!UnfoldStrip(infinite, 0.1).HasValue(), "NaN を断ること");
}

KACHA_V2_TEST(unfold, 何度展開しても同じ図になる)
{
    const auto samples = CylinderPatch(25.0, 40.0, 1.4);
    const auto first = UnfoldSamples(samples, 0.05);
    Require(first.HasValue(), "展開できること");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = UnfoldSamples(samples, 0.05);
        Require(again.HasValue(), "毎回展開できること");
        RequireCount(again.Value().firstRail.size(), first.Value().firstRail.size(), "点の数");
        for (std::size_t index = 0; index < first.Value().firstRail.size(); ++index) {
            RequireNear(again.Value().firstRail[index].u, first.Value().firstRail[index].u,
                0.0, "u が完全に同じ");
            RequireNear(again.Value().secondRail[index].v,
                first.Value().secondRail[index].v, 0.0, "v が完全に同じ");
        }
    }
}

KACHA_V2_TEST(unfold, 長さの検査が違いを見逃さない)
{
    const std::vector<Vector3> spatial{{0, 0, 0}, {10, 0, 0}, {20, 0, 0}};
    const std::vector<Point2> good{{0, 0}, {10, 0}, {20, 0}};
    Require(CheckLengthPreservation(spatial, good).withinTolerance, "同じなら通ること");

    const std::vector<Point2> stretched{{0, 0}, {10, 0}, {20.001, 0}};
    const auto check = CheckLengthPreservation(spatial, stretched);
    Require(!check.withinTolerance, "伸びていたら気づくこと");
    RequireCount(check.worstIndex, 2, "どこが伸びたか");
}

KACHA_V2_TEST_MAIN("fabrication_tests")
