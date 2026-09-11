// 製作の棚の欄(app/FabricationOptions.h)。V1 の近似モデル画面の項目。
#include "kachakacha/app/FabricationOptions.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::ApplyFabricationChoice;
using kachakacha::v2::app::CheckFabricationChoice;
using kachakacha::v2::app::FabricationChoice;
using kachakacha::v2::app::FabricationChoiceOf;
using kachakacha::v2::app::FabricationMethod;
using kachakacha::v2::app::FormatBoundaryList;
using kachakacha::v2::app::ParseBoundaryList;
using kachakacha::v2::app::ParsePartNumberList;
using kachakacha::v2::app::UpdateBandProgress;
using kachakacha::v2::domain::CreateFabricationModelDefinition;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

KACHA_V2_TEST(fabrication_options, 手動境界はカンマ区切りで読めて全角も通り読めなければ断る)
{
    const auto parsed = ParseBoundaryList(" 0.3, 0.6 、0.9");
    Require(parsed.HasValue(), "読める");
    RequireEqual(std::to_string(parsed.Value().size()), std::string("3"), "3つ");
    RequireNear(parsed.Value()[1], 0.6, 1e-12, "2つ目");
    Require(ParseBoundaryList("").HasValue() && ParseBoundaryList("").Value().empty(), "空は空");
    const auto bad = ParseBoundaryList("0.3, abc");
    Require(!bad.HasValue(), "読めない");
    RequireEqual(bad.Diagnostics().front().code, std::string("UI-F001"), "理由");
    RequireEqual(FormatBoundaryList({0.25, 0.5}), std::string("0.25, 0.5"), "戻し");
}

KACHA_V2_TEST(fabrication_options, 欄の値は範囲の外を断り作り方と往復する)
{
    FabricationChoice choice;
    choice.method = FabricationMethod::BandApproximation;
    choice.splitAxis = 1;
    choice.automaticBoundaries = false;
    choice.maximumPartCount = 20;
    choice.minimumPartWidthMm = 2.5;
    choice.fidelity = 8;
    choice.manualBoundaries = {0.5};
    Require(CheckFabricationChoice(choice).HasValue(), "通る");
    CreateFabricationModelDefinition definition;
    ApplyFabricationChoice(definition, choice);
    RequireEqual(std::to_string(definition.method), std::string("1"), "方式");
    RequireEqual(std::to_string(definition.splitAxis), std::string("1"), "分割軸");
    Require(!definition.automaticBoundaries, "手動");
    RequireEqual(std::to_string(definition.maximumPartCount), std::string("20"), "上限");
    RequireNear(definition.minimumPartWidthMm, 2.5, 1e-12, "最小幅");
    RequireEqual(std::to_string(definition.fidelity), std::string("8"), "再現度");
    RequireEqual(std::to_string(definition.manualBoundaries.size()), std::string("1"), "境界");
    const auto back = FabricationChoiceOf(definition);
    Require(back.method == FabricationMethod::BandApproximation && back.splitAxis == 1
            && !back.automaticBoundaries && back.maximumPartCount == 20 && back.fidelity == 8,
        "戻る");

    FabricationChoice tooMany = choice;
    tooMany.maximumPartCount = 0;
    RequireEqual(CheckFabricationChoice(tooMany).Diagnostics().front().code,
        std::string("UI-F002"), "上限 0");
    FabricationChoice thin = choice;
    thin.minimumPartWidthMm = 0.0;
    RequireEqual(CheckFabricationChoice(thin).Diagnostics().front().code,
        std::string("UI-F003"), "最小幅 0");
    FabricationChoice rough = choice;
    rough.fidelity = 21;
    RequireEqual(CheckFabricationChoice(rough).Diagnostics().front().code,
        std::string("UI-F004"), "再現度 21");
}

KACHA_V2_TEST(fabrication_options, 面の範囲は作り方と往復し壊れていれば断る)
{
    FabricationChoice choice;
    choice.rangeUMin = 0.2;
    choice.rangeUMax = 0.8;
    choice.rangeVMin = 0.0;
    choice.rangeVMax = 0.5;
    Require(CheckFabricationChoice(choice).HasValue(), "通る");
    CreateFabricationModelDefinition definition;
    ApplyFabricationChoice(definition, choice);
    RequireNear(definition.rangeUMin, 0.2, 1e-12, "u 最小");
    RequireNear(definition.rangeVMax, 0.5, 1e-12, "v 最大");
    RequireNear(FabricationChoiceOf(definition).rangeUMax, 0.8, 1e-12, "戻る");
    FabricationChoice reversed = choice;
    reversed.rangeUMin = 0.9;
    RequireEqual(CheckFabricationChoice(reversed).Diagnostics().front().code,
        std::string("UI-F005"), "最小 > 最大");
    FabricationChoice outside = choice;
    outside.rangeVMax = 1.5;
    RequireEqual(CheckFabricationChoice(outside).Diagnostics().front().code,
        std::string("UI-F005"), "1 を超える");
}

KACHA_V2_TEST(fabrication_options, 部材を挙げるとその帯だけが曲がり空なら全体に従う)
{
    // V1 の part_model_part_assembly と同じ:「選んだ部材だけが曲がる」。
    CreateFabricationModelDefinition definition;
    definition.masterPercent = 100.0;
    const auto one = UpdateBandProgress(definition, 4, {2}, 30.0);
    Require(one.HasValue(), "2 番だけ動かせる");
    RequireNear(one.Value().masterPercent, 100.0, 1e-12, "全体は変わらない");
    RequireEqual(std::to_string(one.Value().bandProgress.size()), std::string("4"), "帯の数だけ");
    RequireNear(one.Value().bandProgress[0], 1.0, 1e-12, "1 番は完成形のまま");
    RequireNear(one.Value().bandProgress[1], 0.3, 1e-12, "2 番だけ 30%");
    RequireNear(one.Value().bandProgress[3], 1.0, 1e-12, "4 番も変わらない");

    // 既にある個別値の上に重ねる。挙げていない番号は保つ。
    CreateFabricationModelDefinition mixed = definition;
    mixed.bandProgress = one.Value().bandProgress;
    const auto second = UpdateBandProgress(mixed, 4, {4}, 0.0);
    Require(second.HasValue(), "4 番も動かせる");
    RequireNear(second.Value().bandProgress[1], 0.3, 1e-12, "2 番は残る");
    RequireNear(second.Value().bandProgress[3], 0.0, 1e-12, "4 番が平ら");

    // 空なら全体。個別値は捨てる(V1 と同じ)。
    const auto all = UpdateBandProgress(mixed, 4, {}, 50.0);
    Require(all.HasValue(), "全体を動かせる");
    RequireNear(all.Value().masterPercent, 50.0, 1e-12, "全体が 50%");
    Require(all.Value().bandProgress.empty(), "個別値は捨てる");

    // 範囲の外は断る。
    RequireEqual(UpdateBandProgress(definition, 4, {5}, 30.0).Diagnostics().front().code,
        std::string("UI-F006"), "5 番は無い");
    RequireEqual(UpdateBandProgress(definition, 0, {1}, 30.0).Diagnostics().front().code,
        std::string("UI-F006"), "部材が無ければ断る");
    Require(!UpdateBandProgress(definition, 4, {1}, 120.0).HasValue(), "120% は断る");
}

KACHA_V2_TEST(fabrication_options, 部材番号の欄はカンマ区切りで読めて整数でなければ断る)
{
    const auto parsed = ParsePartNumberList("1, 3");
    Require(parsed.HasValue(), "読める");
    RequireEqual(std::to_string(parsed.Value().size()), std::string("2"), "2つ");
    RequireEqual(std::to_string(parsed.Value()[1]), std::string("3"), "2つ目");
    Require(ParsePartNumberList("").HasValue() && ParsePartNumberList("").Value().empty(),
        "空は空");
    RequireEqual(ParsePartNumberList("1.5").Diagnostics().front().code, std::string("UI-F006"),
        "小数は断る");
    RequireEqual(ParsePartNumberList("0").Diagnostics().front().code, std::string("UI-F006"),
        "0 は断る");
    RequireEqual(ParsePartNumberList("abc").Diagnostics().front().code, std::string("UI-F006"),
        "字は断る");
}

KACHA_V2_TEST_MAIN("fabrication_options")
