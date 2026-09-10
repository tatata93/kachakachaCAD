// 帯近似(V1 方式)の移植を確かめる(fabrication/BandApproximation.h、BandFold.h)。
//
// V1 の tests/part_model_tests.cpp が押さえていたことを、同じ順で押さえる。
// 「二重曲面を断らずに帯へ切る」「展開は辺長を厳密に保つ」
// 「曲げ具合 0 は平ら、1 は完成形と一致、途中も辺長を保つ」の3つが要である。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/BandApproximation.h"
#include "kachakacha/fabrication/BandFold.h"

#include <cmath>
#include <string>

using kachakacha::v2::fabrication::ApproximateBands;
using kachakacha::v2::fabrication::BandApproximationOptions;
using kachakacha::v2::fabrication::BandMesh;
using kachakacha::v2::fabrication::BandSplitAxis;
using kachakacha::v2::fabrication::BuildBandFoldRails;
using kachakacha::v2::fabrication::BuildRigidBandTransforms;
using kachakacha::v2::fabrication::ClipLoopIntoBands;
using kachakacha::v2::fabrication::DevelopBandMesh;
using kachakacha::v2::fabrication::FoldBandMesh;
using kachakacha::v2::fabrication::MapPointToBandState;
using kachakacha::v2::fabrication::MeasureCreaseAngles;
using kachakacha::v2::fabrication::SampledSurface;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

constexpr double kPi = 3.14159265358979323846;

//! 格子を関数から作る。行が v、列が u。
template<class Function>
[[nodiscard]] SurfacePatchSamples Grid(std::size_t rows, std::size_t columns, Function f)
{
    SurfacePatchSamples samples;
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            const double u = static_cast<double>(column) / static_cast<double>(columns - 1);
            const double v = static_cast<double>(row) / static_cast<double>(rows - 1);
            samples.points.push_back(f(u, v));
        }
    }
    return samples;
}

//! 半径 50mm の円筒の 1/4。v 方向に曲がり、u 方向は直線。展開できる面。
[[nodiscard]] SurfacePatchSamples QuarterCylinder()
{
    return Grid(33, 17, [](double u, double v) {
        const double angle = v * kPi / 2.0;
        return Vector3{u * 80.0, 50.0 * std::cos(angle), 50.0 * std::sin(angle)};
    });
}

//! 半径 60mm の球の一部。二重曲面。V2 の CurvedPanel は断るが、帯近似は切る。
[[nodiscard]] SurfacePatchSamples SphereCap()
{
    return Grid(33, 33, [](double u, double v) {
        const double lon = (u - 0.5) * kPi / 2.0;
        const double lat = (v - 0.5) * kPi / 2.0;
        return Vector3{60.0 * std::cos(lat) * std::sin(lon), 60.0 * std::sin(lat),
            60.0 * std::cos(lat) * std::cos(lon)};
    });
}

[[nodiscard]] SurfacePatchSamples FlatSheet()
{
    return Grid(9, 9, [](double u, double v) { return Vector3{u * 100.0, v * 60.0, 0.0}; });
}

[[nodiscard]] double Length3(const std::vector<Vector3>& rail)
{
    double total = 0.0;
    for (std::size_t index = 1; index < rail.size(); ++index) {
        total += (rail[index] - rail[index - 1]).Length();
    }
    return total;
}

[[nodiscard]] double Length2(const std::vector<kachakacha::v2::geometry::Point2>& rail)
{
    double total = 0.0;
    for (std::size_t index = 1; index < rail.size(); ++index) {
        const double du = rail[index].u - rail[index - 1].u;
        const double dv = rail[index].v - rail[index - 1].v;
        total += std::sqrt(du * du + dv * dv);
    }
    return total;
}

} // namespace

KACHA_V2_TEST(band_approximation, 格子の補間は角と中央で正しい)
{
    const SampledSurface surface(FlatSheet());
    Require((surface.Evaluate(0.0, 0.0) - Vector3{0.0, 0.0, 0.0}).Length() < 1e-9, "原点");
    Require((surface.Evaluate(1.0, 1.0) - Vector3{100.0, 60.0, 0.0}).Length() < 1e-9, "対角");
    Require((surface.Evaluate(0.5, 0.5) - Vector3{50.0, 30.0, 0.0}).Length() < 1e-9, "中央");
    Require((surface.Evaluate(0.3, 0.7) - Vector3{30.0, 42.0, 0.0}).Length() < 1e-9, "途中");
}

KACHA_V2_TEST(band_approximation, 平らな面は1枚のまま)
{
    const SampledSurface surface(FlatSheet());
    const auto made = ApproximateBands(surface, BandApproximationOptions{});
    Require(made.HasValue(), "通る");
    Require(made.Value().bands.size() == 1, "切らない");
    Require(made.Value().bands.front().planar, "平ら");
    Require(made.Value().reachedRequestedTolerance, "許容に収まる");
}

KACHA_V2_TEST(band_approximation, 曲がった面は許容偏差に収まるまで切る)
{
    // V1: curved loft splits into multiple parts / reachedRequestedTolerance
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.maximumDeviationMm = 0.4;
    const auto made = ApproximateBands(surface, options);
    Require(made.HasValue(), "通る");
    Require(made.Value().bands.size() >= 2, "2枚以上に切れる");
    Require(made.Value().reachedRequestedTolerance, "許容に収まる");
    for (const auto& band : made.Value().bands) {
        Require(band.estimatedDeviationMm <= 0.4 + 1e-9, "どの帯も許容以下");
    }
    Require(made.Value().railParameters.size() == made.Value().bands.size() + 1,
        "境目は帯数+1本");
}

KACHA_V2_TEST(band_approximation, 二重曲面も断らずに切って偏差を報告する)
{
    // ここが V2 のもう一方の方式との違い。球は「伸ばさずには平らにできない」が、
    // 帯近似は帯へ切って、どれだけずれたかを言う。
    const SampledSurface surface(SphereCap());
    BandApproximationOptions options;
    options.maximumDeviationMm = 0.5;
    const auto made = ApproximateBands(surface, options);
    Require(made.HasValue(), "断らない");
    Require(made.Value().bands.size() >= 2, "帯へ切る");
    Require(made.Value().maximumDeviationMm > 0.0, "偏差を報告する");
}

KACHA_V2_TEST(band_approximation, 部材数の上限を超えたら等分割にして偏差を報告する)
{
    // V1: part count stays within the limit
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.maximumDeviationMm = 0.01; // 厳しすぎる許容
    options.maximumPartCount = 3;
    options.minimumPartWidthMm = 0.0;
    const auto made = ApproximateBands(surface, options);
    Require(made.HasValue(), "断らない");
    Require(made.Value().bands.size() == 3, "上限の3枚");
    Require(!made.Value().reachedRequestedTolerance, "収まらなかったと言う");
}

KACHA_V2_TEST(band_approximation, 最小幅より細い帯は作らない)
{
    // V1: minimum width respected
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.maximumDeviationMm = 0.05;
    options.minimumPartWidthMm = 12.0;
    options.maximumPartCount = 100;
    const auto made = ApproximateBands(surface, options);
    Require(made.HasValue(), "通る");
    for (const auto& band : made.Value().bands) {
        Require(band.widthMm >= 12.0 - 1e-6, "最小幅以上");
    }
}

KACHA_V2_TEST(band_approximation, 手動境界は3枚にする)
{
    // V1: manual boundaries make three parts
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.automaticBoundaries = false;
    options.manualBoundaries = {0.7, 0.3};
    const auto made = ApproximateBands(surface, options);
    Require(made.HasValue(), "通る");
    Require(made.Value().bands.size() == 3, "3枚");
    Require(std::abs(made.Value().bands[1].minimumParameter - 0.3) < 1e-12, "並べ直す");
    options.manualBoundaries = {0.3, 0.3};
    Require(!ApproximateBands(surface, options).HasValue(), "重複は断る");
    options.manualBoundaries = {1.5};
    Require(!ApproximateBands(surface, options).HasValue(), "範囲外は断る");
}

KACHA_V2_TEST(band_approximation, 無理な指定は断る)
{
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.maximumDeviationMm = 0.0;
    Require(!ApproximateBands(surface, options).HasValue(), "許容0は断る");
    options.maximumDeviationMm = 0.25;
    options.maximumPartCount = 0;
    Require(!ApproximateBands(surface, options).HasValue(), "上限0は断る");
}

KACHA_V2_TEST(band_approximation, 展開はレールの長さを厳密に保つ)
{
    // V1: 展開の厳密さ ── レールの2D長は近似形状の3D長と一致する
    const SampledSurface surface(QuarterCylinder());
    const auto bands = ApproximateBands(surface, BandApproximationOptions{});
    Require(bands.HasValue(), "近似できる");
    const auto mesh = DevelopBandMesh(surface, BandSplitAxis::V,
        bands.Value().railParameters, 48);
    Require(mesh.HasValue(), "展開できる");
    const BandMesh& made = mesh.Value();
    Require(made.rows == static_cast<int>(bands.Value().railParameters.size()), "レール数");
    for (int row = 0; row < made.rows; ++row) {
        const double length3 = Length3(made.world[static_cast<std::size_t>(row)]);
        const double length2 = Length2(made.developed[static_cast<std::size_t>(row)]);
        Require(std::abs(length3 - length2) < 1e-6 * std::max(1.0, length3),
            "レールの2D長 = 3D長");
    }
    // 素線(帯を横切る線)も保つ。
    for (int band = 0; band + 1 < made.rows; ++band) {
        for (int column = 0; column < made.columns; column += 7) {
            const auto& b3 = made.world[static_cast<std::size_t>(band)];
            const auto& t3 = made.world[static_cast<std::size_t>(band) + 1];
            const auto& b2 = made.developed[static_cast<std::size_t>(band)];
            const auto& t2 = made.developed[static_cast<std::size_t>(band) + 1];
            const std::size_t c = static_cast<std::size_t>(column);
            const double d3 = (t3[c] - b3[c]).Length();
            const double du = t2[c].u - b2[c].u;
            const double dv = t2[c].v - b2[c].v;
            Require(std::abs(d3 - std::sqrt(du * du + dv * dv)) < 1e-6, "素線の長さ");
        }
    }
}

KACHA_V2_TEST(band_approximation, 曲げ具合0は平らで1は完成形と一致する)
{
    // V1: fold preview at 0 is planar / at 1 equals world
    const SampledSurface surface(QuarterCylinder());
    const auto bands = ApproximateBands(surface, BandApproximationOptions{});
    const auto mesh = DevelopBandMesh(surface, BandSplitAxis::V,
        bands.Value().railParameters, 32);
    Require(mesh.HasValue(), "展開できる");
    const auto folded = FoldBandMesh(mesh.Value(), 1.0);
    for (std::size_t row = 0; row < folded.size(); ++row) {
        for (std::size_t column = 0; column < folded[row].size(); ++column) {
            Require((folded[row][column] - mesh.Value().world[row][column]).Length() < 1e-9,
                "1 は完成形");
        }
    }
    const auto flat = FoldBandMesh(mesh.Value(), 0.0);
    // 全点が1つの平面に載る。
    const Vector3 origin = flat[0][0];
    const Vector3 a = flat[0][flat[0].size() - 1] - origin;
    const Vector3 b = flat[1][0] - origin;
    const Vector3 normal = Normalized(Cross(a, b));
    Require(normal.Length() > 0.5, "平面が決まる");
    for (const auto& row : flat) {
        for (const Vector3& point : row) {
            Require(std::abs(Dot(point - origin, normal)) < 1e-6, "0 は平ら");
        }
    }
}

KACHA_V2_TEST(band_approximation, 途中の曲げ具合でも辺長を保つ)
{
    // 等長の曲げ。「三角形は剛体、二面角だけ progress 倍」なので、
    // どの瞬間もレールの長さと素線の長さが変わらない。
    const SampledSurface surface(QuarterCylinder());
    const auto bands = ApproximateBands(surface, BandApproximationOptions{});
    const auto mesh = DevelopBandMesh(surface, BandSplitAxis::V,
        bands.Value().railParameters, 32);
    for (const double progress : {0.25, 0.5, 0.8}) {
        const auto state = FoldBandMesh(mesh.Value(), progress);
        for (std::size_t row = 0; row < state.size(); ++row) {
            const double expected = Length3(mesh.Value().world[row]);
            Require(std::abs(Length3(state[row]) - expected) < 1e-6 * std::max(1.0, expected),
                "レールの長さが変わらない");
        }
    }
}

KACHA_V2_TEST(band_approximation, 折り角を測れて剛体変換は帯の形を変えない)
{
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.automaticBoundaries = false;
    options.manualBoundaries = {0.5};
    const auto bands = ApproximateBands(surface, options);
    const auto mesh = DevelopBandMesh(surface, BandSplitAxis::V,
        bands.Value().railParameters, 24);
    const auto angles = MeasureCreaseAngles(mesh.Value());
    Require(angles.size() == 1, "折り線は1本");
    Require(std::abs(angles.front()) > 0.1, "曲がっているので折り角がある");
    // 進行度1なら恒等変換、0なら帯1が平らへ回る。どちらも帯の中は変えない。
    const auto identity = BuildRigidBandTransforms(mesh.Value(), {1.0});
    Require(identity.HasValue(), "作れる");
    Require((identity.Value()[1].Apply(Vector3{1.0, 2.0, 3.0}) - Vector3{1.0, 2.0, 3.0})
                .Length() < 1e-9,
        "進行度1は恒等");
    const auto flat = BuildRigidBandTransforms(mesh.Value(), {0.0});
    Require(flat.HasValue(), "作れる");
    const auto& top = mesh.Value().world[2];
    const double before = Length3(top);
    std::vector<Vector3> moved;
    for (const Vector3& point : top) {
        moved.push_back(flat.Value()[1].Apply(point));
    }
    Require(std::abs(Length3(moved) - before) < 1e-6, "剛体なので長さは変わらない");
    Require(!BuildRigidBandTransforms(mesh.Value(), {0.0, 0.0}).HasValue(),
        "本数が合わなければ断る");
}

KACHA_V2_TEST(band_approximation, 帯ごとの姿勢は1で完成形に一致し0でも退化しない)
{
    // V1: 0% animation band is not degenerate / rails at 1 equal world
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.automaticBoundaries = false;
    options.manualBoundaries = {0.33, 0.66};
    const auto bands = ApproximateBands(surface, options);
    const auto mesh = DevelopBandMesh(surface, BandSplitAxis::V,
        bands.Value().railParameters, 24);
    const int bandCount = mesh.Value().BandCount();
    const std::vector<double> creases(static_cast<std::size_t>(mesh.Value().CreaseCount()),
        1.0);
    const auto atWorld = BuildBandFoldRails(mesh.Value(), creases, {1.0}, 10.0);
    Require(atWorld.HasValue(), "作れる");
    Require(atWorld.Value().size() == static_cast<std::size_t>(bandCount * 2), "2×帯数");
    for (int band = 0; band < bandCount; ++band) {
        const auto& bottom = atWorld.Value()[static_cast<std::size_t>(band) * 2];
        for (std::size_t column = 0; column < bottom.size(); ++column) {
            Require((bottom[column] - mesh.Value().world[static_cast<std::size_t>(band)][column])
                        .Length() < 1e-6,
                "1 は完成形");
        }
    }
    const auto flat = BuildBandFoldRails(mesh.Value(), creases, {0.0}, 10.0);
    Require(flat.HasValue(), "作れる");
    for (std::size_t index = 0; index < flat.Value().size(); ++index) {
        const double expected = Length3(mesh.Value().world[index / 2 + (index % 2)]);
        Require(std::abs(Length3(flat.Value()[index]) - expected) < 1e-6, "0 でも長さを保つ");
    }
    // 帯ごとに違う進行度: 選んだ帯だけが曲がる。
    const auto mixed = BuildBandFoldRails(mesh.Value(), creases, {1.0, 0.0, 1.0}, 0.0);
    Require(mixed.HasValue(), "作れる");
    Require((mixed.Value()[0][0] - mesh.Value().world[0][0]).Length() < 1e-6, "帯0は完成形");
}

KACHA_V2_TEST(band_approximation, メッシュ上の点は同じ帯へ写る)
{
    // V1: MapPointToPartMeshState。接続部分の変形の土台。
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.automaticBoundaries = false;
    options.manualBoundaries = {0.5};
    const auto bands = ApproximateBands(surface, options);
    const auto mesh = DevelopBandMesh(surface, BandSplitAxis::V,
        bands.Value().railParameters, 24);
    const auto flat = FoldBandMesh(mesh.Value(), 0.0);
    const Vector3 onMesh = (mesh.Value().world[0][5] + mesh.Value().world[1][5]) * 0.5;
    const auto mapped = MapPointToBandState(mesh.Value(), flat, onMesh);
    Require(mapped.HasValue(), "写せる");
    Require(mapped.Value().band == 0, "帯0");
    Require(mapped.Value().distanceMm < 1e-6, "メッシュ上なので距離0");
    const Vector3 expected = (flat[0][5] + flat[1][5]) * 0.5;
    Require((mapped.Value().point - expected).Length() < 1e-6, "同じ位相の点へ写る");
    Require(!MapPointToBandState(mesh.Value(), {}, onMesh).HasValue(), "位相違いは断る");
}

KACHA_V2_TEST(band_approximation, 帯をまたぐ窓は帯ごとに切り出される)
{
    // V1 のオーナー報告「開口した穴が近似して分割した部品にまたがっているときに
    // 適応されてない」の対策。3帯にまたぐ四角い窓が3片になり、真ん中が残らない。
    const std::vector<Vector3> loop{Vector3{0.0, 0.0, 0.0}, Vector3{10.0, 0.0, 0.0},
        Vector3{10.0, 3.0, 0.0}, Vector3{0.0, 3.0, 0.0}};
    const std::vector<double> parameters{0.1, 0.9, 0.9, 0.1};
    const auto pieces = ClipLoopIntoBands(loop, parameters, {0.0, 0.4, 0.6, 1.0});
    Require(pieces.size() == 3, "3片");
    RequireEqual(std::to_string(pieces[1].band), std::string("1"), "真ん中の帯にも片がある");
    Require(pieces[1].points.size() >= 4, "真ん中は四角");
    Require(ClipLoopIntoBands(loop, {0.1, 0.9}, {0.0, 1.0}).empty(), "長さ違いは空");
}

KACHA_V2_TEST_MAIN("band_approximation_tests")
