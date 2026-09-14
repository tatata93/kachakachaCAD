// 部材ごとの曲げ半径(§30・§31、Codex Q1-Q5 B2)。
//
// ここで見るのは3つ。
//   1. 半径を **測る**。外周の 1/4 のような当て推量ではなく、
//      帯の幅と折り線の角から `R = w / θ` で出す。
//   2. 固定した半径が **形を変える**。表示だけ変わって形が同じ、を通さない。
//   3. 曲げても **面内長が変わらない**。板は伸びない(§10.1)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/BandApproximation.h"
#include "kachakacha/fabrication/BandBendRadius.h"
#include "kachakacha/fabrication/BandFold.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::ApplyStoredBendRadii;
using kachakacha::v2::fabrication::ApproximateBands;
using kachakacha::v2::fabrication::BandApproximationOptions;
using kachakacha::v2::fabrication::BandMesh;
using kachakacha::v2::fabrication::BandSplitAxis;
using kachakacha::v2::fabrication::BendRadius;
using kachakacha::v2::fabrication::BendRadiusCreaseFactors;
using kachakacha::v2::fabrication::DescribeBandBendRadiiJa;
using kachakacha::v2::fabrication::DevelopBandMesh;
using kachakacha::v2::fabrication::FoldBandMesh;
using kachakacha::v2::fabrication::MeasureBandBendRadii;
using kachakacha::v2::fabrication::MeasureCreaseAngles;
using kachakacha::v2::fabrication::SampledSurface;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::fabrication::ValueLock;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

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

//! 半径 50mm の円筒の 1/4。v 方向に曲がる。答えが分かっている形。
[[nodiscard]] SurfacePatchSamples QuarterCylinder()
{
    return Grid(33, 17, [](double u, double v) {
        const double angle = v * kPi / 2.0;
        return Vector3{u * 80.0, 50.0 * std::cos(angle), 50.0 * std::sin(angle)};
    });
}

//! 帯へ切って、展開メッシュまで作る。
[[nodiscard]] BandMesh MakeCylinderMesh()
{
    const SampledSurface surface(QuarterCylinder());
    BandApproximationOptions options;
    options.maximumDeviationMm = 0.4;
    const auto bands = ApproximateBands(surface, options);
    Require(bands.HasValue(), "帯へ切れる");
    const auto mesh = DevelopBandMesh(surface, BandSplitAxis::V,
        bands.Value().railParameters, 48);
    Require(mesh.HasValue(), "展開できる");
    return mesh.Value();
}

//! その状態での、帯を横切る素線の長さ。曲げても変わってはいけない。
[[nodiscard]] double RungLength(const std::vector<std::vector<Vector3>>& rows,
    std::size_t column)
{
    double total = 0.0;
    for (std::size_t row = 0; row + 1 < rows.size(); ++row) {
        if (column >= rows[row].size() || column >= rows[row + 1].size()) {
            return total;
        }
        total += (rows[row + 1][column] - rows[row][column]).Length();
    }
    return total;
}

} // namespace

KACHA_V2_TEST(band_bend_radius, 半径は帯の幅と折り線の角から出す)
{
    const BandMesh mesh = MakeCylinderMesh();
    const auto angles = MeasureCreaseAngles(mesh);
    const auto bends = MeasureBandBendRadii(mesh, angles);
    Require(bends.size() == static_cast<std::size_t>(mesh.BandCount()),
        "部材の数だけ出る");

    // 元が半径 50mm の円筒なので、測った半径もその近くに来る。
    // 多角形で内接近似しているので厳密には一致しないが、けた違いにはならない。
    std::size_t checked = 0;
    for (std::size_t index = 0; index + 1 < bends.size(); ++index) {
        if (!(bends[index].radiusMm > 0.0)) {
            continue;
        }
        ++checked;
        Require(bends[index].radiusMm > 40.0 && bends[index].radiusMm < 62.0,
            "測った半径が元の円筒(50mm)の近くにある: "
                + std::to_string(bends[index].radiusMm));
        // `R = w / θ` そのもの。定義どおりに出ていること。
        RequireNear(bends[index].radiusMm,
            bends[index].flatLengthMm / std::abs(angles[index]), 1.0e-9,
            "半径は幅を角で割った値");
    }
    Require(checked > 0, "曲がっている部材がある");
    // 最後の帯には折り線が無い。「測れた」と嘘をつかない。
    Require(bends.back().radiusMm == 0.0, "最後の部材は平ら扱い");
}

KACHA_V2_TEST(band_bend_radius, 数が合わない古い値は自動へ戻す)
{
    const BandMesh mesh = MakeCylinderMesh();
    const auto measured = MeasureBandBendRadii(mesh, MeasureCreaseAngles(mesh));
    // 帯の数は近似をやり直すと変わる。古い値を理由に開けなくしない。
    const auto applied = ApplyStoredBendRadii(measured, {22.0}, {1});
    Require(applied.size() == measured.size(), "数は測ったぶんのまま");
    for (const auto& bend : applied) {
        Require(bend.lock == ValueLock::Auto, "全部自動へ戻る");
    }
}

KACHA_V2_TEST(band_bend_radius, 固定した半径だけが固定になる)
{
    const BandMesh mesh = MakeCylinderMesh();
    const auto measured = MeasureBandBendRadii(mesh, MeasureCreaseAngles(mesh));
    Require(measured.size() >= 2, "部材が2枚以上ある");
    std::vector<double> radii(measured.size(), 0.0);
    std::vector<int> locks(measured.size(), 0);
    radii[0] = 22.0;
    locks[0] = 1;
    const auto applied = ApplyStoredBendRadii(measured, radii, locks);
    Require(applied[0].lock == ValueLock::Locked, "1枚目は固定");
    RequireNear(applied[0].radiusMm, 22.0, 1.0e-12, "入れた値がそのまま入る");
    Require(applied[1].lock == ValueLock::Auto, "2枚目は自動のまま");
    RequireNear(applied[1].radiusMm, measured[1].radiusMm, 1.0e-12, "測った値のまま");
    // 面内長は触らない。板は伸びない。
    RequireNear(applied[0].flatLengthMm, measured[0].flatLengthMm, 1.0e-12,
        "半径を固定しても板の長さは変わらない");
}

KACHA_V2_TEST(band_bend_radius, 固定した半径が実際の形を変える)
{
    const BandMesh mesh = MakeCylinderMesh();
    const auto angles = MeasureCreaseAngles(mesh);
    const auto measured = MeasureBandBendRadii(mesh, angles);
    Require(measured.size() >= 2, "部材が2枚以上ある");

    // 1枚目を、測った半径の半分に固定する。倍きつく曲がるはずである。
    std::vector<double> radii(measured.size(), 0.0);
    std::vector<int> locks(measured.size(), 0);
    radii[0] = measured[0].radiusMm * 0.5;
    locks[0] = 1;
    const auto applied = ApplyStoredBendRadii(measured, radii, locks);
    const auto factors = BendRadiusCreaseFactors(mesh, angles, applied);
    Require(factors.size() == static_cast<std::size_t>(mesh.CreaseCount()),
        "折り線の数だけ倍率が出る");
    RequireNear(factors[0], 2.0, 1.0e-9, "半分の半径なら倍の角");
    for (std::size_t index = 1; index < factors.size(); ++index) {
        RequireNear(factors[index], 1.0, 1.0e-12, "ほかの折り線は測ったまま");
    }

    // ここが本題。形が本当に変わること。
    const auto plain = FoldBandMesh(mesh, 1.0, {});
    const auto bent = FoldBandMesh(mesh, 1.0, factors);
    Require(plain.size() == bent.size(), "行の数は同じ");
    double moved = 0.0;
    for (std::size_t row = 0; row < plain.size(); ++row) {
        for (std::size_t column = 0;
            column < plain[row].size() && column < bent[row].size(); ++column) {
            moved = std::max(moved, (bent[row][column] - plain[row][column]).Length());
        }
    }
    Require(moved > 1.0,
        "半径を固定すると形が動く(動いた量 " + std::to_string(moved) + "mm)");

    // そして、動いても板は伸びていないこと。
    const std::size_t middle = static_cast<std::size_t>(mesh.columns) / 2;
    RequireNear(RungLength(bent, middle), RungLength(plain, middle), 1.0e-6,
        "曲げ方を変えても面内長は変わらない");
}

KACHA_V2_TEST(band_bend_radius, 平らなところは半径を入れても曲げない)
{
    const BandMesh mesh = MakeCylinderMesh();
    const auto angles = MeasureCreaseAngles(mesh);
    auto measured = MeasureBandBendRadii(mesh, angles);
    // 折り線の無い最後の帯に半径を入れても、曲げようがない。
    std::vector<double> radii(measured.size(), 0.0);
    std::vector<int> locks(measured.size(), 0);
    radii.back() = 10.0;
    locks.back() = 1;
    const auto applied = ApplyStoredBendRadii(measured, radii, locks);
    const auto factors = BendRadiusCreaseFactors(mesh, angles, applied);
    for (const double factor : factors) {
        Require(factor > 0.0, "倍率は正");
    }
    const auto plain = FoldBandMesh(mesh, 1.0, {});
    const auto bent = FoldBandMesh(mesh, 1.0, factors);
    double moved = 0.0;
    for (std::size_t row = 0; row < plain.size() && row < bent.size(); ++row) {
        for (std::size_t column = 0;
            column < plain[row].size() && column < bent[row].size(); ++column) {
            moved = std::max(moved, (bent[row][column] - plain[row][column]).Length());
        }
    }
    Require(moved < 1.0e-6, "平らなところは動かない");
}

KACHA_V2_TEST(band_bend_radius, 部材ごとの半径を一文にできる)
{
    std::vector<BendRadius> bends;
    BendRadius first;
    first.flatLengthMm = 34.0;
    first.radiusMm = 21.63;
    bends.push_back(first);
    BendRadius flat;
    bends.push_back(flat);
    const std::string text = DescribeBandBendRadiiJa(bends, 100.0);
    Require(text.find("部材1") != std::string::npos, "番号が出る");
    Require(text.find("部材2") == std::string::npos, "平らな部材は出さない");
    Require(DescribeBandBendRadiiJa({}, 100.0).find("ありません") != std::string::npos,
        "何も無ければそう言う");
    Require(DescribeBandBendRadiiJa({flat}, 100.0).find("平ら") != std::string::npos,
        "全部平らならそう言う");
}

KACHA_V2_TEST_MAIN("band_bend_radius_tests")
