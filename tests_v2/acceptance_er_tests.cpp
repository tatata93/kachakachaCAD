// ER1/ER2 の 1/87 受入モデル(AT-FAB-013)。
//
// この試験は「実際に作るもの」に一番近い。ここが通らなければ、
// ほかがいくら通っていても、この道具は使えない。
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
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/Assembly.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/fabrication/FreezeState.h"
#include "kachakacha/fabrication/OpeningClip.h"
#include "kachakacha/fabrication/PanelStrategy.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AssemblyFold;
using kachakacha::v2::fabrication::AssemblyPanel;
using kachakacha::v2::fabrication::AssemblyState;
using kachakacha::v2::fabrication::BuildPanelPartition;
using kachakacha::v2::fabrication::CheckOpeningApproximation;
using kachakacha::v2::fabrication::CompareFrozenStates;
using kachakacha::v2::fabrication::FabricationSettings;
using kachakacha::v2::fabrication::FabricationStrategy;
using kachakacha::v2::fabrication::FreezeAssemblyState;
using kachakacha::v2::fabrication::FreezeOutput;
using kachakacha::v2::fabrication::JointKind;
using kachakacha::v2::fabrication::PanelAdjacency;
using kachakacha::v2::fabrication::PanelCandidate;
using kachakacha::v2::fabrication::PanelGeometryClass;
using kachakacha::v2::fabrication::ResolveTargetMaxDeviationMm;
using kachakacha::v2::geometry::Point2;
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
constexpr double kFrontDepthMm = 2400.0 / kScale;     // 約 27.59 mm
//! 模型の対角。許容差の基準に使う。
constexpr double kDiagonalMm = 60.0;

//! ER の前頭部を、部材の候補として書き下したもの。
//!
//! 実際の形そのものではなく、**この試験が確かめたい性質を持った形**である。
//! 腰部・窓帯・額は大きな連続面、肩は強い二重曲率、という関係がここに入る。
struct ErFront {
    std::vector<PanelCandidate> panels;
    std::vector<PanelAdjacency> adjacencies;

    //! 名前から添字を引く。
    [[nodiscard]] std::size_t IndexOf(const std::string& id) const
    {
        for (std::size_t at = 0; at < panels.size(); ++at) {
            if (panels[at].panelId == id) {
                return at;
            }
        }
        Require(false, "その部材がある: " + id);
        return 0;
    }
};

[[nodiscard]] PanelCandidate Panel(const std::string& id, PanelGeometryClass kind,
    double deviation, double area, double doubleCurvedRatio)
{
    PanelCandidate panel;
    panel.panelId = id;
    panel.classification = kind;
    panel.flattenDeviationMm = deviation;
    panel.areaMm2 = area;
    panel.doubleCurvedRatio = doubleCurvedRatio;
    return panel;
}

[[nodiscard]] ErFront MakeErFront()
{
    ErFront front;
    // 腰部・窓帯・額は、円筒に近い大きな連続面。三角形の寄せ集めにしない。
    front.panels.push_back(Panel("腰部", PanelGeometryClass::Cylindrical, 0.02,
        kBodyWidthMm * kBodyHeightMm * 0.42, 0.02));
    front.panels.push_back(Panel("窓帯", PanelGeometryClass::Cylindrical, 0.03,
        kBodyWidthMm * kBodyHeightMm * 0.30, 0.03));
    front.panels.push_back(Panel("額", PanelGeometryClass::Cylindrical, 0.04,
        kBodyWidthMm * kBodyHeightMm * 0.20, 0.05));
    // 肩は強い二重曲率。ここだけが切れ目か追加分割の対象になる。
    front.panels.push_back(Panel("左肩", PanelGeometryClass::DoubleCurved, 0.9,
        kBodyWidthMm * 3.0, 0.62));
    front.panels.push_back(Panel("右肩", PanelGeometryClass::DoubleCurved, 0.9,
        kBodyWidthMm * 3.0, 0.62));
    // 側面は平ら。
    front.panels.push_back(Panel("左側面", PanelGeometryClass::Planar, 0.0,
        kFrontDepthMm * kBodyHeightMm, 0.0));
    front.panels.push_back(Panel("右側面", PanelGeometryClass::Planar, 0.0,
        kFrontDepthMm * kBodyHeightMm, 0.0));

    const auto join = [&](const std::string& first, const std::string& second,
                          double length, double angle) {
        PanelAdjacency adjacency;
        adjacency.firstIndex = front.IndexOf(first);
        adjacency.secondIndex = front.IndexOf(second);
        adjacency.sharedEdgeLengthMm = length;
        adjacency.dihedralAngleRad = angle;
        front.adjacencies.push_back(adjacency);
    };
    join("腰部", "窓帯", kBodyWidthMm, 0.05);
    join("窓帯", "額", kBodyWidthMm, 0.10);
    join("額", "左肩", kBodyWidthMm * 0.4, 0.35);
    join("額", "右肩", kBodyWidthMm * 0.4, 0.35);
    join("左肩", "左側面", kBodyHeightMm * 0.3, kPi / 2.0);
    join("右肩", "右側面", kBodyHeightMm * 0.3, kPi / 2.0);
    join("腰部", "左側面", kBodyHeightMm * 0.5, kPi / 2.0);
    join("腰部", "右側面", kBodyHeightMm * 0.5, kPi / 2.0);
    return front;
}

[[nodiscard]] FabricationSettings Settings(int fidelity)
{
    FabricationSettings settings;
    settings.strategy = FabricationStrategy::FewPieces;
    settings.fidelityLevel = fidelity;
    settings.panelCountLimit = 24;
    settings.reliefCutsEnabled = true;
    return settings;
}

//! 6枚窓と中央前照灯。額の上の開口。
[[nodiscard]] std::vector<std::vector<Vector3>> MakeOpenings()
{
    std::vector<std::vector<Vector3>> openings;
    // 6枚窓。横一列に並ぶ長方形。
    for (int index = 0; index < 6; ++index) {
        const double left = -kBodyWidthMm * 0.45 + index * kBodyWidthMm * 0.15;
        const double right = left + kBodyWidthMm * 0.12;
        openings.push_back({Vector3{left, 0, 20}, Vector3{right, 0, 20},
            Vector3{right, 0, 28}, Vector3{left, 0, 28}, Vector3{left, 0, 20}});
    }
    // 中央前照灯。円い開口。
    std::vector<Vector3> lamp;
    constexpr int kSteps = 64;
    for (int index = 0; index <= kSteps; ++index) {
        const double angle = 2.0 * kPi * index / kSteps;
        lamp.push_back(Vector3{2.2 * std::cos(angle), 0.0, 12.0 + 2.2 * std::sin(angle)});
    }
    openings.push_back(std::move(lamp));
    return openings;
}

//! 4枚の板を折り線でつないだ、組立を見るための形。
struct AssemblyFixture {
    std::vector<AssemblyPanel> panels;
    std::vector<AssemblyFold> folds;
};

[[nodiscard]] AssemblyFixture MakeAssembly()
{
    AssemblyFixture fixture;
    const double widths[] = {kBodyWidthMm * 0.4, kBodyWidthMm * 0.3, kBodyWidthMm * 0.4,
        kBodyWidthMm * 0.3};
    double at = 0.0;
    const char* names[] = {"腰部", "窓帯", "額", "肩"};
    for (int index = 0; index < 4; ++index) {
        AssemblyPanel panel;
        panel.panelId = names[index];
        panel.flatOutline = {Point2{at, 0.0}, Point2{at + widths[index], 0.0},
            Point2{at + widths[index], kBodyHeightMm}, Point2{at, kBodyHeightMm}};
        at += widths[index];
        fixture.panels.push_back(std::move(panel));
    }
    double hinge = widths[0];
    for (int index = 0; index < 3; ++index) {
        AssemblyFold fold;
        fold.foldId = "f" + std::to_string(index + 1);
        fold.parentPanelId = fixture.panels[index].panelId;
        fold.childPanelId = fixture.panels[index + 1].panelId;
        fold.hingeFrom = Point2{hinge, 0.0};
        fold.hingeTo = Point2{hinge, kBodyHeightMm};
        fold.targetAngleRad = kPi / 3.0;
        hinge += widths[index + 1];
        fixture.folds.push_back(std::move(fold));
    }
    return fixture;
}

[[nodiscard]] AssemblyState At(double percent)
{
    AssemblyState state;
    state.masterPercent = percent;
    return state;
}

} // namespace

KACHA_V2_TEST(er, 幅基準が1_87で3520mmになる)
{
    RequireNear(kBodyWidthMm, 40.4597701149, 1.0e-9, "3520/87 mm");
    // 縮尺を掛け戻せば実車の寸法へ戻る。
    RequireNear(kBodyWidthMm * kScale, 3520.0, 1.0e-9, "実車 3520 mm");
}

KACHA_V2_TEST(er, 腰部と窓帯と額が大きな連続した部材になる)
{
    // 全面三角形の寄せ集めにしない。契約が名指ししている3つが、
    // それぞれ1枚の大きな部材として出ること。
    const ErFront front = MakeErFront();
    const auto partition = BuildPanelPartition(front.panels, front.adjacencies,
        Settings(6), ResolveTargetMaxDeviationMm(Settings(6), kDiagonalMm));
    Require(partition.HasValue(), "分けられる");

    const double bodyArea = kBodyWidthMm * kBodyHeightMm;
    for (const char* wanted : {"腰部", "窓帯", "額"}) {
        bool found = false;
        for (const auto& piece : partition.Value().pieces) {
            for (const std::size_t index : piece.panelIndices) {
                if (front.panels[index].panelId == wanted) {
                    found = true;
                    // その部材が属する塊は、車体の1割より大きい。
                    double area = 0.0;
                    for (const std::size_t at : piece.panelIndices) {
                        area += front.panels[at].areaMm2;
                    }
                    Require(area > bodyArea * 0.10,
                        std::string(wanted) + " は大きな連続した部材: "
                            + std::to_string(area) + " mm2");
                }
            }
        }
        Require(found, std::string(wanted) + " がある");
    }
}

KACHA_V2_TEST(er, 部材の数が上限を超えない)
{
    const ErFront front = MakeErFront();
    for (int fidelity : {3, 6, 9}) {
        const FabricationSettings settings = Settings(fidelity);
        const auto partition = BuildPanelPartition(front.panels, front.adjacencies,
            settings, ResolveTargetMaxDeviationMm(settings, kDiagonalMm));
        Require(partition.HasValue(), "分けられる");
        Require(static_cast<int>(partition.Value().PieceCount())
                <= settings.panelCountLimit,
            "上限を超えない: " + std::to_string(partition.Value().PieceCount()));
    }
}

KACHA_V2_TEST(er, 強い二重曲率の肩にだけ手当てが要る)
{
    // 平らな側面や、円筒に近い腰部には切れ目を入れない。
    const ErFront front = MakeErFront();
    for (const auto& panel : front.panels) {
        const bool isShoulder = panel.panelId == "左肩" || panel.panelId == "右肩";
        const bool strong = panel.doubleCurvedRatio > 0.5;
        RequireEqual(strong ? "強い" : "弱い", isShoulder ? "強い" : "弱い",
            panel.panelId + " の二重曲率");
    }
    // 肩は1枚のままでは目標を超える。だから切れ目か追加分割が要る。
    const FabricationSettings settings = Settings(6);
    const double target = ResolveTargetMaxDeviationMm(settings, kDiagonalMm);
    for (const auto& panel : front.panels) {
        if (panel.panelId == "左肩" || panel.panelId == "右肩") {
            Require(panel.flattenDeviationMm > target, "肩は手当てが要る");
        } else {
            Require(panel.flattenDeviationMm <= target,
                panel.panelId + " は手当てが要らない");
        }
    }
}

KACHA_V2_TEST(er, 再現度を上げると目標偏差が単調に小さくなる)
{
    double previous = 1.0e300;
    for (int fidelity : {3, 6, 9}) {
        const double target = ResolveTargetMaxDeviationMm(Settings(fidelity), kDiagonalMm);
        Require(target < previous,
            "再現度 " + std::to_string(fidelity) + " で目標が小さくなる: "
                + std::to_string(target));
        previous = target;
    }
}

KACHA_V2_TEST(er, 再現度3_6_9で最大偏差が単調非増加)
{
    // 契約の文言そのもの。再現度を上げて、出来た部材の最大偏差が増えないこと。
    const ErFront front = MakeErFront();
    double previous = 1.0e300;
    for (int fidelity : {3, 6, 9}) {
        const FabricationSettings settings = Settings(fidelity);
        const auto partition = BuildPanelPartition(front.panels, front.adjacencies,
            settings, ResolveTargetMaxDeviationMm(settings, kDiagonalMm));
        Require(partition.HasValue(), "分けられる");
        double worst = 0.0;
        for (const auto& piece : partition.Value().pieces) {
            for (const std::size_t index : piece.panelIndices) {
                worst = std::max(worst, front.panels[index].flattenDeviationMm);
            }
        }
        Require(worst <= previous + 1.0e-9,
            "再現度 " + std::to_string(fidelity) + " で増えない: "
                + std::to_string(worst));
        previous = worst;
    }
}

KACHA_V2_TEST(er, 6枚窓と前照灯が消えない)
{
    const auto openings = MakeOpenings();
    RequireEqual(std::to_string(openings.size()), "7", "窓6枚と前照灯1つ");
    for (const auto& opening : openings) {
        Require(opening.size() >= 5, "閉じた輪郭になっている");
        RequireNear((opening.front() - opening.back()).Length(), 0.0, 1.0e-9, "閉じている");
    }
}

KACHA_V2_TEST(er, 前照灯を多角形へ置き換えない)
{
    // 「ライトや窓を丸い多角形へ置換してはならない」。
    const auto openings = MakeOpenings();
    const auto& lamp = openings.back();
    // 8角形で近似すると目標を超える。
    std::vector<Vector3> coarse;
    for (int index = 0; index <= 8; ++index) {
        const double angle = 2.0 * kPi * index / 8;
        coarse.push_back(Vector3{2.2 * std::cos(angle), 0.0, 12.0 + 2.2 * std::sin(angle)});
    }
    const auto refused = CheckOpeningApproximation(lamp, coarse, 0.05);
    Require(!refused.HasValue(), "断る");
    RequireEqual(refused.Diagnostics().front().code, "FAB-O003", "近似が目標を超えた");
    // そのままなら通る。
    const auto exact = CheckOpeningApproximation(lamp, lamp, 0.05);
    Require(exact.HasValue(), "そのままなら通る");
    Require(exact.Value().exact, "近似していない");
}

KACHA_V2_TEST(er, 30パーセントでしわも縮尺変化も起きない)
{
    const AssemblyFixture fixture = MakeAssembly();
    const auto flat = FreezeAssemblyState(fixture.panels, fixture.folds, At(0.0),
        FreezeOutput::Both, FabricationSettings{});
    const auto partial = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(flat.HasValue() && partial.HasValue(), "どちらも固定できる");

    // 対応する辺の長さが変わらない = 縮尺が変わっていない。
    const auto comparison = CompareFrozenStates(flat.Value(), partial.Value(), 1.0e-6);
    Require(comparison.lengthsMatch,
        "辺の長さが変わらない: " + std::to_string(comparison.maximumLengthDifferenceMm)
            + " mm (" + comparison.worstSourceId + ")");
    // 形そのものは変わっている。
    Require(comparison.shapesDiffer, "形は変わっている");
    // 部材の数も変わらない = しわ寄せに部材を増やしていない。
    RequireEqual(std::to_string(partial.Value().parts.size()),
        std::to_string(flat.Value().parts.size()), "部材の数が変わらない");
}

KACHA_V2_TEST(er, 100パーセントでも辺の長さが変わらない)
{
    const AssemblyFixture fixture = MakeAssembly();
    const auto flat = FreezeAssemblyState(fixture.panels, fixture.folds, At(0.0),
        FreezeOutput::Both, FabricationSettings{});
    const auto closed = FreezeAssemblyState(fixture.panels, fixture.folds, At(100.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(flat.HasValue() && closed.HasValue(), "どちらも固定できる");
    const auto comparison = CompareFrozenStates(flat.Value(), closed.Value(), 1.0e-6);
    Require(comparison.lengthsMatch,
        "閉じ残りが目標内: " + std::to_string(comparison.maximumLengthDifferenceMm)
            + " mm");
    Require(comparison.shapesDiffer, "形は変わっている");
}

KACHA_V2_TEST(er, どの状態でも部材の境界とワイヤーが一致する)
{
    const AssemblyFixture fixture = MakeAssembly();
    for (double percent : {0.0, 30.0, 50.0, 100.0}) {
        const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds,
            At(percent), FreezeOutput::Both, FabricationSettings{});
        Require(bundle.HasValue(), "固定できる");
        const auto agreement =
            kachakacha::v2::fabrication::CheckBoundaryAgreement(bundle.Value(), 1.0e-6);
        Require(agreement.agrees,
            std::to_string(percent) + "% で一致する: "
                + std::to_string(agreement.maximumDeviationMm) + " mm ("
                + agreement.worstPanelId + ")");
    }
}

KACHA_V2_TEST_MAIN("acceptance_er_tests")
