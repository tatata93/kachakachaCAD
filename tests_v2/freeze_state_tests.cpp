// 任意の組立状態を固定する(AT-FAB-011 / 014)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/FreezeMaterialize.h"
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

// ---- 実体にする(AT-FAB-011 / 014 の Entity 化) ----

KACHA_V2_TEST(freeze, 固定するとFrozenのEntityになる)
{
    using kachakacha::v2::domain::EditPolicy;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::fabrication::IsDirectlyEditable;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(bundle.HasValue(), "固定できる");
    const auto made = MaterializeFrozenState(bundle.Value(), "車体");
    Require(made.HasValue(), "実体にできる");
    Require(!made.Value().entities.empty(), "何か出る");
    for (const auto& entity : made.Value().entities) {
        Require(entity.editPolicy == EditPolicy::Frozen, "固定済みになる");
        Require(IsDirectlyEditable(entity.editPolicy), "固定後は編集できる");
        Require(!entity.displayName.empty(), "名前がある");
        Require(entity.displayName.find("30%") != std::string::npos, "割合が名前に入る");
    }
    // 固定する前の派生物は直接編集できない。
    Require(!IsDirectlyEditable(EditPolicy::Derived), "派生は直接編集しない");
    Require(IsDirectlyEditable(EditPolicy::Source), "正本は編集できる");
    RequireNear(made.Value().percent, 30.0, 1e-12, "割合を覚えている");
}

KACHA_V2_TEST(freeze, 出し方でEntityの種類と数が変わる)
{
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    const Fixture fixture = MakeFixture();
    const auto count = [&](FreezeOutput output, EntityKind kind) {
        const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
            output, FabricationSettings{});
        Require(bundle.HasValue(), "固定できる");
        const auto made = MaterializeFrozenState(bundle.Value(), "車体");
        Require(made.HasValue(), "実体にできる");
        int found = 0;
        for (const auto& entity : made.Value().entities) {
            found += entity.kind == kind ? 1 : 0;
        }
        return found;
    };
    const int wiresOnlyWires = count(FreezeOutput::WiresOnly, EntityKind::Wire);
    const int wiresOnlyParts = count(FreezeOutput::WiresOnly, EntityKind::Part);
    const int partsOnlyWires = count(FreezeOutput::PartsOnly, EntityKind::Wire);
    const int partsOnlyParts = count(FreezeOutput::PartsOnly, EntityKind::Part);
    const int bothWires = count(FreezeOutput::Both, EntityKind::Wire);
    const int bothParts = count(FreezeOutput::Both, EntityKind::Part);

    Require(wiresOnlyWires > 0, "ワイヤーのみ: ワイヤーが出る");
    RequireEqual(std::to_string(wiresOnlyParts), "0", "ワイヤーのみ: 部品は出ない");
    RequireEqual(std::to_string(partsOnlyWires), "0", "部品のみ: ワイヤーは出ない");
    Require(partsOnlyParts > 0, "部品のみ: 部品が出る");
    RequireEqual(std::to_string(bothWires), std::to_string(wiresOnlyWires),
        "両方: ワイヤーの数は同じ");
    RequireEqual(std::to_string(bothParts), std::to_string(partsOnlyParts),
        "両方: 部品の数も同じ");
    // 4部材なので部品は4つ。
    RequireEqual(std::to_string(bothParts), "4", "4部材");
}

KACHA_V2_TEST(freeze, 固定したものは元を変えても変わらない)
{
    using kachakacha::v2::fabrication::IsIndependentOfSource;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    const Fixture fixture = MakeFixture();
    const auto atThirty = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(atThirty.HasValue(), "固定できる");
    const auto frozen = MaterializeFrozenState(atThirty.Value(), "車体");
    Require(frozen.HasValue(), "実体にできる");

    // 元の製作モデルを 30% から 90% へ動かす。
    const auto atNinety = FreezeAssemblyState(fixture.panels, fixture.folds, At(90.0),
        FreezeOutput::Both, FabricationSettings{});
    Require(atNinety.HasValue(), "動かせる");
    Require(IsIndependentOfSource(frozen.Value(), atNinety.Value()),
        "固定したものは動かない");

    // 同じ状態のままなら、当然一致している(試験そのものが空振りしていない証拠)。
    Require(!IsIndependentOfSource(frozen.Value(), atThirty.Value()),
        "同じ状態なら一致する");
}

KACHA_V2_TEST(freeze, 線の無いワイヤーは実体にせずに断る)
{
    using kachakacha::v2::fabrication::FreezeBundle;
    using kachakacha::v2::fabrication::FrozenWire;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    FreezeBundle bundle;
    bundle.output = FreezeOutput::WiresOnly;
    FrozenWire empty;
    empty.sourceId = "A";
    bundle.wires.push_back(empty);
    const auto refused = MaterializeFrozenState(bundle, "車体");
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "FAB-E002", "立体が作れない");
}

KACHA_V2_TEST(freeze, 輪郭が足りない部材は実体にせずに断る)
{
    using kachakacha::v2::fabrication::FreezeBundle;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    using kachakacha::v2::fabrication::PanelSolidRequest;
    FreezeBundle bundle;
    bundle.output = FreezeOutput::PartsOnly;
    PanelSolidRequest thin;
    thin.panelId = "A";
    thin.outline = {kachakacha::v2::geometry::Vector3{0, 0, 0},
        kachakacha::v2::geometry::Vector3{1, 0, 0}};
    bundle.parts.push_back(thin);
    const auto refused = MaterializeFrozenState(bundle, "車体");
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "FAB-E002", "立体が作れない");
}

KACHA_V2_TEST(freeze, 厚みが0の部材は実体にせずに断る)
{
    using kachakacha::v2::fabrication::FreezeBundle;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    using kachakacha::v2::fabrication::PanelSolidRequest;
    FreezeBundle bundle;
    bundle.output = FreezeOutput::PartsOnly;
    PanelSolidRequest flat;
    flat.panelId = "A";
    flat.outline = {kachakacha::v2::geometry::Vector3{0, 0, 0},
        kachakacha::v2::geometry::Vector3{1, 0, 0},
        kachakacha::v2::geometry::Vector3{1, 1, 0}};
    flat.thicknessMm = 0.0;
    bundle.parts.push_back(flat);
    const auto refused = MaterializeFrozenState(bundle, "車体");
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "FAB-E002", "厚みが無い");
}

KACHA_V2_TEST(freeze, もとが無ければ実体にせずに断る)
{
    using kachakacha::v2::fabrication::FreezeBundle;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    FreezeBundle empty;
    empty.output = FreezeOutput::Both;
    const auto refused = MaterializeFrozenState(empty, "車体");
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "FAB-E003", "もとが無い");

    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::Both, FabricationSettings{});
    const auto noName = MaterializeFrozenState(bundle.Value(), "");
    Require(!noName.HasValue(), "断る");
    RequireEqual(FirstCode(noName.Diagnostics()), "FAB-E003", "名前が無い");
}

KACHA_V2_TEST(freeze, 実体にしても曲線の種類が変わらない)
{
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::fabrication::MaterializeFrozenState;
    const Fixture fixture = MakeFixture();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::WiresOnly, FabricationSettings{});
    Require(bundle.HasValue(), "固定できる");
    const auto made = MaterializeFrozenState(bundle.Value(), "車体");
    Require(made.HasValue(), "実体にできる");
    std::size_t matched = 0;
    for (const auto& entity : made.Value().entities) {
        for (const auto& wire : bundle.Value().wires) {
            if (wire.sourceId != entity.sourceId) {
                continue;
            }
            RequireEqual(std::to_string(entity.segments.size()),
                std::to_string(wire.segments.size()), "線の数が同じ");
            for (std::size_t at = 0; at < wire.segments.size(); ++at) {
                RequireEqual(
                    std::string(kachakacha::v2::geometry::CurveKindName(
                        entity.segments[at].Kind())),
                    std::string(kachakacha::v2::geometry::CurveKindName(
                        wire.segments[at].Kind())),
                    "種類が同じ");
            }
            ++matched;
        }
    }
    Require(matched > 0, "突き合わせたものがある");
}

KACHA_V2_TEST_MAIN("freeze_state_tests")
