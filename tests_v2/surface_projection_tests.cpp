// 線を曲面へ落とす(fabrication/SurfaceProjection.h)。曲がった面に窓を開けるための道。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/SurfaceProjection.h"

#include <cmath>
#include <string>

using kachakacha::v2::fabrication::ProjectCurvesOntoSampledSurface;
using kachakacha::v2::fabrication::ProjectPointOntoSampledSurface;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

//! 半径 50 の円筒の上半分。x は 0..80、角度は 0..π。
[[nodiscard]] SurfacePatchSamples Cylinder()
{
    SurfacePatchSamples samples;
    samples.rowCount = 17;
    samples.columnCount = 9;
    for (std::size_t row = 0; row < 17; ++row) {
        for (std::size_t column = 0; column < 9; ++column) {
            const double angle = kPi * static_cast<double>(row) / 16.0;
            samples.points.push_back(Vector3{static_cast<double>(column) * 10.0,
                50.0 * std::cos(angle), 50.0 * std::sin(angle)});
        }
    }
    return samples;
}

} // namespace

KACHA_V2_TEST(surface_projection, 点は向きに沿って面へ当たる)
{
    const auto samples = Cylinder();
    // 真上から z 方向に落とすと、y=0 の点は z≈50 へ当たる(折れ線近似なので少し低い)。
    const auto landed = ProjectPointOntoSampledSurface(samples, Vector3{40.0, 0.0, 200.0},
        Vector3{0.0, 0.0, -1.0});
    Require(landed.HasValue(), "当たる");
    RequireNear(landed.Value().x, 40.0, 1.0e-9, "x は変わらない");
    RequireNear(landed.Value().y, 0.0, 1.0e-9, "y は変わらない");
    Require(landed.Value().z > 49.0 && landed.Value().z <= 50.0 + 1.0e-9, "面の上");
    // 向きの前後はどちらでもよい。下から上へ向けても同じ点。
    const auto reversed = ProjectPointOntoSampledSurface(samples, Vector3{40.0, 0.0, 200.0},
        Vector3{0.0, 0.0, 1.0});
    Require(reversed.HasValue(), "逆向きでも当たる");
    RequireNear(reversed.Value().z, landed.Value().z, 1.0e-9, "同じ点");
}

KACHA_V2_TEST(surface_projection, 当たらない点は理由を出して断る)
{
    const auto samples = Cylinder();
    const auto missed = ProjectPointOntoSampledSurface(samples, Vector3{40.0, 200.0, 200.0},
        Vector3{0.0, 0.0, -1.0});
    Require(!missed.HasValue(), "面の外は当たらない");
    RequireEqual(missed.Diagnostics().front().code, std::string("FAB-J001"), "理由の番号");
    const auto bad = ProjectPointOntoSampledSurface(samples, Vector3{40.0, 0.0, 200.0},
        Vector3{0.0, 0.0, 0.0});
    RequireEqual(bad.Diagnostics().front().code, std::string("FAB-J002"), "向き 0 は断る");
}

KACHA_V2_TEST(surface_projection, 閉じた四角は閉じた折れ線として面に落ちる)
{
    const auto samples = Cylinder();
    const Vector3 corners[] = {{20.0, -20.0, 100.0}, {60.0, -20.0, 100.0},
        {60.0, 20.0, 100.0}, {20.0, 20.0, 100.0}};
    std::vector<CurveSegment> square;
    for (int index = 0; index < 4; ++index) {
        square.push_back(CurveSegment::MakeLine(corners[index], corners[(index + 1) % 4]).Value());
    }
    const auto landed = ProjectCurvesOntoSampledSurface(samples, square,
        Vector3{0.0, 0.0, -1.0}, 0.05);
    Require(landed.HasValue(), "落ちる");
    Require(landed.Value().size() >= 4, "折れ線になる");
    RequireNear((landed.Value().front().StartPoint() - landed.Value().back().EndPoint()).Length(),
        0.0, 1.0e-9, "閉じたまま");
    for (const auto& segment : landed.Value()) {
        // 落ちた点は円筒の上(半径 50 の折れ線近似なので 49 以上)。
        const Vector3 p = segment.StartPoint();
        const double radius = std::sqrt(p.y * p.y + p.z * p.z);
        Require(radius > 49.0 && radius <= 50.0 + 1.0e-9, "面の上に載る");
    }
    // 一部でも当たらなければ丸ごと断る。
    const Vector3 outside[] = {{20.0, -20.0, 100.0}, {60.0, 200.0, 100.0}, {60.0, 20.0, 100.0}};
    std::vector<CurveSegment> partly;
    for (int index = 0; index < 3; ++index) {
        partly.push_back(CurveSegment::MakeLine(outside[index], outside[(index + 1) % 3]).Value());
    }
    Require(!ProjectCurvesOntoSampledSurface(samples, partly, Vector3{0.0, 0.0, -1.0}, 0.05)
                 .HasValue(),
        "半分だけ落ちた形は返さない");
}

KACHA_V2_TEST_MAIN("surface_projection")
