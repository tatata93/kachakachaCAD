// 近似モデルの作り方から部材と曲げ状態を作る(app/FabricationEvaluate.h)。
//
// 作るときも開き直すときも同じ道を通すことと、V1 方式と V2 方式が同じ入力で
// 違う答え(断る / 切る)を返すことを押さえる。
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::app::AdaptConnectionWires;
using kachakacha::v2::app::EvaluateFabrication;
using kachakacha::v2::app::FabricationMethod;
using kachakacha::v2::app::FabricationMarkings;
using kachakacha::v2::app::FabricationSource;
using kachakacha::v2::app::FoldStateSummaryJa;
using kachakacha::v2::app::FoldedRailsOf;
using kachakacha::v2::app::ResolveFoldState;
using kachakacha::v2::domain::CreateFabricationModelDefinition;
using kachakacha::v2::fabrication::SurfacePatchSamples;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

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
            samples.points.push_back(f(static_cast<double>(column) / (columns - 1),
                static_cast<double>(row) / (rows - 1)));
        }
    }
    return samples;
}

[[nodiscard]] FabricationSource Sphere()
{
    FabricationSource source;
    source.name = "球";
    source.samples = Grid(25, 25, [](double u, double v) {
        const double lon = (u - 0.5) * kPi / 2.0;
        const double lat = (v - 0.5) * kPi / 2.0;
        return Vector3{60.0 * std::cos(lat) * std::sin(lon), 60.0 * std::sin(lat),
            60.0 * std::cos(lat) * std::cos(lon)};
    });
    return source;
}

[[nodiscard]] FabricationSource Cylinder()
{
    FabricationSource source;
    source.name = "筒";
    source.samples = Grid(25, 9, [](double u, double v) {
        const double angle = v * kPi / 2.0;
        return Vector3{u * 80.0, 50.0 * std::cos(angle), 50.0 * std::sin(angle)};
    });
    return source;
}

[[nodiscard]] CreateFabricationModelDefinition Definition(int method)
{
    CreateFabricationModelDefinition definition;
    definition.method = method;
    definition.targetMaxDeviation.value = 0.5;
    return definition;
}

} // namespace

KACHA_V2_TEST(fabrication_evaluate, 二重曲面はV2方式が断りV1方式が切る)
{
    // ここが「両方できるようにしろ」の要。同じ球、同じ許容(0.1mm)で答えが違う。
    // この球を1枚で平らにすると 0.22mm ずれる。V2 方式はそれを見て断り、
    // V1 方式は帯へ切って許容に収める。
    auto tight0 = Definition(0);
    tight0.targetMaxDeviation.value = 0.1;
    const auto refused = EvaluateFabrication(tight0, {Sphere()}, FabricationMarkings{}, 0.01);
    Require(!refused.HasValue(), "V2 方式は断る");
    auto tight1 = Definition(1);
    tight1.targetMaxDeviation.value = 0.1;
    const auto cut = EvaluateFabrication(tight1, {Sphere()}, FabricationMarkings{}, 0.01);
    Require(cut.HasValue(), "V1 方式は切る");
    Require(cut.Value().method == FabricationMethod::BandApproximation, "方式");
    Require(cut.Value().panels.size() >= 2, "帯ごとに部材になる");
    Require(cut.Value().bandMesh.has_value(), "帯メッシュを持つ");
    Require(cut.Value().summaryJa.find("帯へ近似") != std::string::npos, "一文に出る");
}

KACHA_V2_TEST(fabrication_evaluate, 展開できる面はどちらの方式でも通る)
{
    const auto classified = EvaluateFabrication(Definition(0), {Cylinder()}, FabricationMarkings{}, 0.01);
    Require(classified.HasValue(), "V2 方式で通る");
    Require(classified.Value().panels.size() == 1, "1枚のまま");
    Require(!classified.Value().bandMesh.has_value(), "帯メッシュは持たない");
    const auto banded = EvaluateFabrication(Definition(1), {Cylinder()}, FabricationMarkings{}, 0.01);
    Require(banded.HasValue(), "V1 方式でも通る");
    Require(banded.Value().panels.size() >= 2, "帯へ切る");
    // 帯の部材には折り線が付く(最後の帯を除く)。
    Require(!banded.Value().panels.front().folds.empty(), "折り線がある");
    Require(banded.Value().panels.back().folds.empty(), "最後の帯には無い");
}

KACHA_V2_TEST(fabrication_evaluate, 面の範囲を狭めると標本が範囲の中だけになり壊れた範囲は断る)
{
    // V1 の板材の「範囲」。u 0.5〜1.0 だけを使うと、筒の半分だけが近似される。
    const auto whole = EvaluateFabrication(Definition(1), {Cylinder()}, FabricationMarkings{}, 0.01);
    Require(whole.HasValue(), "全体で通る");
    auto half = Definition(1);
    half.rangeUMin = 0.5;
    const auto cropped = EvaluateFabrication(half, {Cylinder()}, FabricationMarkings{}, 0.01);
    Require(cropped.HasValue(), "半分でも通る");
    Require(cropped.Value().panels.size() <= whole.Value().panels.size(), "帯は増えない");
    // 標本そのものも範囲の中だけ。
    const auto samples = kachakacha::v2::fabrication::CropSamples(*Cylinder().samples, 0.5, 1.0,
        0.0, 1.0);
    Require(samples.HasValue(), "切り出せる");
    Require(samples.Value().rowCount == Cylinder().samples->rowCount
            && samples.Value().columnCount == Cylinder().samples->columnCount,
        "格子の数は同じ");
    const auto& original = *Cylinder().samples;
    const auto first = samples.Value().At(0, 0);
    const auto middle = original.At(0, (original.columnCount - 1) / 2);
    Require((first - middle).Length() < 1.0e-6, "左端が元の真ん中になる");
    const auto last = samples.Value().At(0, samples.Value().columnCount - 1);
    Require((last - original.At(0, original.columnCount - 1)).Length() < 1.0e-9, "右端は同じ");
    // 壊れた範囲は断る。
    auto broken = Definition(1);
    broken.rangeUMin = 0.8;
    broken.rangeUMax = 0.2;
    RequireEqual(EvaluateFabrication(broken, {Cylinder()}, FabricationMarkings{}, 0.01)
                     .Diagnostics().front().code,
        std::string("FAB-M004"), "最小 > 最大は FAB-M004");
}

KACHA_V2_TEST(fabrication_evaluate, 元が無ければ断る)
{
    const auto made = EvaluateFabrication(Definition(1), {}, FabricationMarkings{}, 0.01);
    Require(!made.HasValue(), "断る");
    RequireEqual(made.Diagnostics().front().code, std::string("FAB-M001"), "FAB-M001");
    auto bad = Definition(1);
    bad.targetMaxDeviation.value = 0.0;
    RequireEqual(EvaluateFabrication(bad, {Cylinder()}, FabricationMarkings{}, 0.01).Diagnostics().front().code,
        std::string("FAB-M002"), "許容0は FAB-M002");
}

KACHA_V2_TEST(fabrication_evaluate, 曲げ状態は帯数に合わせて解ける)
{
    const auto made = EvaluateFabrication(Definition(1), {Cylinder()}, FabricationMarkings{}, 0.01);
    const auto& mesh = *made.Value().bandMesh;
    auto definition = Definition(1);
    definition.masterPercent = 50.0;
    const auto state = ResolveFoldState(definition, mesh);
    Require(std::abs(state.masterProgress - 0.5) < 1e-12, "50% は 0.5");
    Require(state.creaseProgress.size() == static_cast<std::size_t>(mesh.CreaseCount()),
        "折り線の数");
    Require(state.bandProgress.size() == static_cast<std::size_t>(mesh.BandCount()), "帯の数");
    for (const double value : state.bandProgress) {
        Require(std::abs(value - 0.5) < 1e-12, "個別値が無ければ master");
    }
    // 数が合わない個別値は master で埋め直す(古い文書でも開ける)。
    definition.bandProgress = {1.0};
    const auto refilled = ResolveFoldState(definition, mesh);
    if (mesh.BandCount() != 1) {
        Require(std::abs(refilled.bandProgress.front() - 0.5) < 1e-12, "合わなければ master");
    }
}

KACHA_V2_TEST(fabrication_evaluate, 曲げ状態の姿勢は0で平ら100で完成形)
{
    const auto made = EvaluateFabrication(Definition(1), {Cylinder()}, FabricationMarkings{}, 0.01);
    const auto& mesh = *made.Value().bandMesh;
    auto definition = Definition(1);
    definition.masterPercent = 100.0;
    const auto full = FoldedRailsOf(definition, made.Value(), 5.0);
    Require(full.size() == static_cast<std::size_t>(mesh.BandCount() * 2), "2×帯数");
    Require((full[0][0] - mesh.world[0][0]).Length() < 1e-6, "100% は完成形");
    definition.masterPercent = 0.0;
    const auto flat = FoldedRailsOf(definition, made.Value(), 5.0);
    Require(flat.size() == full.size(), "本数は同じ");
    // 0% では持ち上げがあるので完成形とは離れる。長さは保つ。
    double lengthFull = 0.0, lengthFlat = 0.0;
    for (std::size_t index = 1; index < full[0].size(); ++index) {
        lengthFull += (full[0][index] - full[0][index - 1]).Length();
        lengthFlat += (flat[0][index] - flat[0][index - 1]).Length();
    }
    Require(std::abs(lengthFull - lengthFlat) < 1e-6, "0% でも長さは保つ");
    const auto none = FoldedRailsOf(definition, EvaluateFabrication(Definition(0),
        {Cylinder()}, FabricationMarkings{}, 0.01).Value(), 5.0);
    Require(none.empty(), "V2 方式には曲げ状態の姿勢が無い");
}

KACHA_V2_TEST(fabrication_evaluate, 接続スコープの線は近似の形へ寄り離れた点は残る)
{
    // V1 の接続スコープ(合意13)。元の線は変えず、寄せた「_接続」の線を別に作る。
    const auto made = EvaluateFabrication(Definition(1), {Cylinder()}, FabricationMarkings{},
        0.01);
    const auto& mesh = *made.Value().bandMesh;
    // 円筒の上に載っている線(角度 45 度の母線)と、遠く離れた線。
    const double angle = kPi / 4.0;
    const auto onSurface = kachakacha::v2::geometry::CurveSegment::MakeLine(
        Vector3{0.0, 50.0 * std::cos(angle), 50.0 * std::sin(angle)},
        Vector3{80.0, 50.0 * std::cos(angle), 50.0 * std::sin(angle)});
    const auto farAway = kachakacha::v2::geometry::CurveSegment::MakeLine(
        Vector3{0.0, 200.0, 200.0}, Vector3{80.0, 200.0, 200.0});
    const auto adapted = AdaptConnectionWires(mesh, mesh.world,
        {{"母線", {onSurface.Value()}}, {"遠い線", {farAway.Value()}}}, 0.6, 8);
    Require(adapted.size() == 2, "2本");
    RequireEqual(adapted[0].name, std::string("母線_接続"), "名前に _接続 が付く");
    Require(adapted[0].snappedPoints == 9, "載っている線は全点が寄る");
    Require(adapted[1].snappedPoints == 0, "離れた線は1点も寄らない");
    Require((adapted[1].points.front() - Vector3{0.0, 200.0, 200.0}).Length() < 1e-9,
        "離れた点は元のまま");
    // 寄せた点は近似メッシュの上(ルールド面)に載る。円筒そのものではなく、
    // 近似した角ばった形へ寄るのが要。
    const auto onMesh = kachakacha::v2::fabrication::MapPointToBandState(mesh, mesh.world,
        adapted[0].points[4]);
    Require(onMesh.HasValue() && onMesh.Value().distanceMm < 1e-6, "寄せた点はメッシュ上");
}

KACHA_V2_TEST(fabrication_evaluate, 曲げ状態の一文)
{
    auto definition = Definition(1);
    definition.masterPercent = 42.0;
    Require(FoldStateSummaryJa(definition).find("42%") != std::string::npos, "率が出る");
    definition.bandProgress = {1.0, 0.0};
    Require(FoldStateSummaryJa(definition).find("帯ごと") != std::string::npos,
        "個別指定が出る");
}

KACHA_V2_TEST_MAIN("fabrication_evaluate_tests")
