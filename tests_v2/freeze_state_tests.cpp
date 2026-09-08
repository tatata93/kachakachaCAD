// 任意の組立状態を固定する(AT-FAB-011 / 014)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/FreezeState.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AssemblyFold;
using kachakacha::v2::fabrication::AssemblyPanel;
using kachakacha::v2::fabrication::AssemblyState;
using kachakacha::v2::fabrication::CheckBoundaryAgreement;
using kachakacha::v2::fabrication::CompareFrozenStates;
using kachakacha::v2::fabrication::FabricationSettings;
using kachakacha::v2::fabrication::FreezeAssemblyState;
using kachakacha::v2::fabrication::FreezeOutput;
using kachakacha::v2::fabrication::FrozenWireKind;
using kachakacha::v2::geometry::Point2;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] AssemblyPanel Rectangle(const std::string& id, double fromU, double toU)
{
    AssemblyPanel panel;
    panel.panelId = id;
    panel.flatOutline = {Point2{fromU, 0.0}, Point2{toU, 0.0}, Point2{toU, 20.0},
        Point2{fromU, 20.0}};
    return panel;
}

//! 4枚の板を折り線でつないだ、ふつうの型紙。
struct Fixture {
    std::vector<AssemblyPanel> panels;
    std::vector<AssemblyFold> folds;
};

[[nodiscard]] Fixture MakeFixture()
{
    Fixture fixture;
    fixture.panels = {Rectangle("A", 0, 40), Rectangle("B", 40, 70),
        Rectangle("C", 70, 110), Rectangle("D", 110, 140)};
    const double angle = kPi / 2.0;
    for (int index = 0; index < 3; ++index) {
        AssemblyFold fold;
        fold.foldId = "f" + std::to_string(index + 1);
        fold.parentPanelId = fixture.panels[index].panelId;
        fold.childPanelId = fixture.panels[index + 1].panelId;
        const double at = fixture.panels[index + 1].flatOutline.front().u;
        fold.hingeFrom = Point2{at, 0.0};
        fold.hingeTo = Point2{at, 20.0};
        fold.targetAngleRad = angle;
        fixture.folds.push_back(fold);
    }
    return fixture;
}

[[nodiscard]] AssemblyState At(double percent)
{
    AssemblyState state;
    state.masterPercent = percent;
    return state;
}

[[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

} // namespace

KACHA_V2_TEST(freeze, ワイヤーだけを固定できる)
{
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    Require(bundle.HasValue(), "作れること");
    Require(!bundle.Value().wires.empty(), "ワイヤーがある");
    Require(bundle.Value().parts.empty(), "部品は作らない");
}

KACHA_V2_TEST(freeze, 部品だけを固定できる)
{
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::PartsOnly, FabricationSettings{});
    Require(bundle.HasValue(), "作れること");
    Require(bundle.Value().wires.empty(), "ワイヤーは作らない");
    RequireEqual(std::to_string(bundle.Value().parts.size()), "4", "部品は4つ");
}

KACHA_V2_TEST(freeze, 両方を固定できる)
{
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(bundle.HasValue(), "作れること");
    RequireEqual(std::to_string(bundle.Value().parts.size()), "4", "部品は4つ");
    std::size_t boundaries = 0;
    std::size_t foldLines = 0;
    for (const auto& wire : bundle.Value().wires) {
        if (wire.kind == FrozenWireKind::PanelBoundary) {
            ++boundaries;
        } else if (wire.kind == FrozenWireKind::FoldLine) {
            ++foldLines;
        }
    }
    RequireEqual(std::to_string(boundaries), "4", "境界は4本");
    RequireEqual(std::to_string(foldLines), "3", "折り線は3本");
}

KACHA_V2_TEST(freeze, 両方のとき部品の境界がワイヤーと一致する)
{
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(bundle.HasValue(), "作れること");
    const auto agreement = CheckBoundaryAgreement(bundle.Value(), 1.0e-9);
    Require(agreement.agrees, "一致している");
    RequireNear(agreement.maximumDeviationMm, 0.0, 1.0e-12,
        "同じ束から作っているのでぴったり同じ");
}

KACHA_V2_TEST(freeze, 状態が違っても辺の長さは変わらない)
{
    const Fixture fixture = MakeFixture();
    const auto zero = FreezeAssemblyState(fixture.panels, fixture.folds, At(0.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    const auto thirty = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    const auto full = FreezeAssemblyState(fixture.panels, fixture.folds, At(100.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    Require(zero.HasValue() && thirty.HasValue() && full.HasValue(), "3つとも作れる");

    const auto first = CompareFrozenStates(zero.Value(), thirty.Value(), 1.0e-9);
    Require(first.lengthsMatch, "0%と30%で辺の長さが同じ");
    const auto second = CompareFrozenStates(thirty.Value(), full.Value(), 1.0e-9);
    Require(second.lengthsMatch, "30%と100%で辺の長さが同じ");
    const auto third = CompareFrozenStates(zero.Value(), full.Value(), 1.0e-9);
    Require(third.lengthsMatch, "0%と100%で辺の長さが同じ");
}

KACHA_V2_TEST(freeze, 状態が違えば形は違う)
{
    const Fixture fixture = MakeFixture();
    const auto zero = FreezeAssemblyState(fixture.panels, fixture.folds, At(0.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    const auto thirty = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    const auto full = FreezeAssemblyState(fixture.panels, fixture.folds, At(100.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    Require(zero.HasValue() && thirty.HasValue() && full.HasValue(), "3つとも作れる");
    const auto first = CompareFrozenStates(zero.Value(), thirty.Value(), 1.0e-6);
    Require(first.shapesDiffer, "0%と30%で形が違う");
    const auto second = CompareFrozenStates(thirty.Value(), full.Value(), 1.0e-6);
    Require(second.shapesDiffer, "30%と100%で形が違う");
    // 折るほど小さくまとまる。
    Require(first.firstDiagonalMm > first.secondDiagonalMm, "0%のほうが広がっている");
}

KACHA_V2_TEST(freeze, 固定した状態が残る)
{
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(bundle.HasValue(), "作れること");
    RequireNear(bundle.Value().percent, 30.0, 0.0, "30%のまま");
    Require(bundle.Value().output == FreezeOutput::Both, "両方のまま");
}

KACHA_V2_TEST(freeze, 曲線の種類を保ったまま出る)
{
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(50.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    Require(bundle.HasValue(), "作れること");
    for (const auto& wire : bundle.Value().wires) {
        Require(!wire.segments.empty(), "線がある");
        for (const auto& segment : wire.segments) {
            Require(segment.Kind() == kachakacha::v2::geometry::CurveKind::Line,
                "直線のまま");
        }
    }
}

KACHA_V2_TEST(freeze, 厚みが無ければ部品は作れない)
{
    const Fixture fixture = MakeFixture();
    FabricationSettings settings;
    settings.outputThicknessMm = 0.0;
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::PartsOnly, settings);
    Require(!bundle.HasValue(), "断る");
    RequireEqual(FirstCode(bundle.Diagnostics()), "FAB-E001", "診断コード");
}

KACHA_V2_TEST(freeze, 厚みが無くてもワイヤーは作れる)
{
    const Fixture fixture = MakeFixture();
    FabricationSettings settings;
    settings.outputThicknessMm = 0.0;
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::WiresOnly, settings);
    Require(bundle.HasValue(), "作れること");
}

KACHA_V2_TEST(freeze, 厚みと置き方が部品へ渡る)
{
    const Fixture fixture = MakeFixture();
    FabricationSettings settings;
    settings.outputThicknessMm = 0.35;
    settings.thicknessPlacement =
        kachakacha::v2::fabrication::ThicknessPlacement::Outside;
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::PartsOnly, settings);
    Require(bundle.HasValue(), "作れること");
    for (const auto& part : bundle.Value().parts) {
        RequireNear(part.thicknessMm, 0.35, 0.0, "厚み");
        Require(part.placement
                == kachakacha::v2::fabrication::ThicknessPlacement::Outside,
            "置き方");
    }
}

KACHA_V2_TEST(freeze, 板が無ければ断る)
{
    const auto bundle = FreezeAssemblyState({}, {}, At(30.0), FreezeOutput::Both,
        FabricationSettings{});
    Require(!bundle.HasValue(), "断る");
    RequireEqual(FirstCode(bundle.Diagnostics()), "FAB-F001", "診断コード");
}

KACHA_V2_TEST(freeze, 同じ入力からは毎回同じ束が出る)
{
    const Fixture fixture = MakeFixture();
    std::string reference;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
            FreezeOutput::Both, FabricationSettings{});
        Require(bundle.HasValue(), "作れること");
        std::string text;
        for (const auto& wire : bundle.Value().wires) {
            text += wire.sourceId + ":" + std::to_string(wire.lengthMm) + ";";
        }
        if (attempt == 0) {
            reference = text;
        } else {
            RequireEqual(text, reference, "毎回同じ");
        }
    }
}

KACHA_V2_TEST(freeze, 出力の名前がすべてそろっている)
{
    for (const FreezeOutput value : {FreezeOutput::WiresOnly, FreezeOutput::PartsOnly,
             FreezeOutput::Both}) {
        Require(kachakacha::v2::fabrication::FreezeOutputNameJa(value) != "不明",
            "名前がある");
    }
    for (const FrozenWireKind value : {FrozenWireKind::PanelBoundary,
             FrozenWireKind::FoldLine, FrozenWireKind::ReliefCut,
             FrozenWireKind::Opening}) {
        Require(kachakacha::v2::fabrication::FrozenWireKindNameJa(value) != "不明",
            "名前がある");
    }
}

KACHA_V2_TEST_MAIN("freeze_state_tests")
