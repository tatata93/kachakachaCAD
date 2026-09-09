// コマンドが使う数(板厚・面取り量・型紙の余白)。
//
// 押し出しの厚みが 0.5mm の決め打ちだったころ、プラ板を使い分けられなかった。
// 変えられないものは、使えないのと同じである。
#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <set>
#include <string>

using kachakacha::v2::app::DefaultParameters;
using kachakacha::v2::app::FindParameter;
using kachakacha::v2::app::ParameterDefinitions;
using kachakacha::v2::app::ParameterId;
using kachakacha::v2::app::ParameterSet;
using kachakacha::v2::app::ParameterTextOf;
using kachakacha::v2::app::ParameterValueOf;
using kachakacha::v2::app::SetParameter;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(parameters, 既定は板厚0_5mm)
{
    const ParameterSet set = DefaultParameters();
    Require(std::abs(ParameterValueOf(set, ParameterId::ExtrudeDistance) - 0.5) < 1e-12,
        "プラ板の定番の厚み");
}

KACHA_V2_TEST(parameters, 式で書ける)
{
    // 電卓を出して打ち直すと、打ち間違いが混ざる。
    const auto changed = SetParameter(DefaultParameters(),
        ParameterId::ExtrudeDistance, "0.3*2");
    Require(changed.HasValue(), "式が通る");
    Require(std::abs(ParameterValueOf(changed.Value(), ParameterId::ExtrudeDistance) - 0.6)
            < 1e-12,
        "評価した値が入る");
    RequireEqual(ParameterTextOf(changed.Value(), ParameterId::ExtrudeDistance),
        std::string("0.3*2"), "書いたものがそのまま残る");
}

KACHA_V2_TEST(parameters, 範囲の外は断る)
{
    // 黙って近い値へ寄せない。寄せると、頼んだ値と違う物が出来る。
    const ParameterSet set = DefaultParameters();
    for (const char* text : {"0", "100"}) {
        const auto refused = SetParameter(set, ParameterId::ExtrudeDistance, text);
        Require(!refused.HasValue(), std::string("断る: ") + text);
        Require(refused.Diagnostics().front().code == "UI-P002", "範囲の外だと言う");
        // どこからどこまでかを言う。言わないと、いくつにすればよいか分からない。
        Require(refused.Diagnostics().front().detailsJa.find("まで")
                != std::string::npos,
            "範囲を言う");
    }
}

KACHA_V2_TEST(parameters, 断っても前の値は消えない)
{
    // 打ち間違えた瞬間に値が消えると、何だったか思い出せなくなる。
    const auto first = SetParameter(DefaultParameters(),
        ParameterId::ExtrudeDistance, "1.0");
    Require(first.HasValue(), "入る");
    const auto refused = SetParameter(first.Value(), ParameterId::ExtrudeDistance, "999");
    Require(!refused.HasValue(), "断る");
    Require(std::abs(ParameterValueOf(first.Value(), ParameterId::ExtrudeDistance) - 1.0)
            < 1e-12,
        "前の集合はそのまま");
}

KACHA_V2_TEST(parameters, 数でない字は断る)
{
    const auto refused = SetParameter(DefaultParameters(),
        ParameterId::ExtrudeDistance, "あつさ");
    Require(!refused.HasValue(), "断る");
    Require(!refused.Diagnostics().empty(), "理由が出る");
}

KACHA_V2_TEST(parameters, 端の値は入る)
{
    // 両端を含む。含まないと、いちばん薄い板が使えない。
    const auto* definition = FindParameter(ParameterId::ExtrudeDistance);
    Require(definition != nullptr, "定義がある");
    for (const double value : {definition->minimum, definition->maximum}) {
        const auto ok = SetParameter(DefaultParameters(), ParameterId::ExtrudeDistance,
            std::to_string(value));
        Require(ok.HasValue(), "端も入る");
    }
}

KACHA_V2_TEST(parameters, 名前と鍵が重ならない)
{
    // 鍵が重なると、どちらを直したのか分からなくなる。
    std::set<std::string> keys;
    std::set<std::string> names;
    for (const auto& definition : ParameterDefinitions()) {
        Require(!definition.key.empty(), "鍵がある");
        Require(!definition.nameJa.empty(), "名前がある");
        Require(!definition.reasonJa.empty(), "なぜその範囲かを書いてある");
        Require(definition.minimum < definition.maximum, "範囲が正しい向き");
        keys.insert(std::string(definition.key));
        names.insert(std::string(definition.nameJa));
    }
    Require(keys.size() == ParameterDefinitions().size(), "鍵が重ならない");
    Require(names.size() == ParameterDefinitions().size(), "名前も重ならない");
}

KACHA_V2_TEST(parameters, 別の数は互いに影響しない)
{
    const auto changed = SetParameter(DefaultParameters(), ParameterId::CornerSize, "2.5");
    Require(changed.HasValue(), "面取り量が入る");
    Require(std::abs(ParameterValueOf(changed.Value(), ParameterId::ExtrudeDistance) - 0.5)
            < 1e-12,
        "板厚は動かない");
}

KACHA_V2_TEST(parameters, 縮尺で割った寸法が出る)
{
    // 手で計算していると、桁を1つ間違えても気づけない。
    ParameterSet set = DefaultParameters();
    Require(std::abs(kachakacha::v2::app::ScaledSizeMm(set) - 20000.0 / 87.0) < 1e-9,
        "既定は 1/87 の 20000mm");
    const auto changed = SetParameter(set, ParameterId::ScaleDenominator, "150");
    Require(changed.HasValue(), "N も入る");
    Require(std::abs(kachakacha::v2::app::ScaledSizeMm(changed.Value())
                - 20000.0 / 150.0) < 1e-9,
        "分母を変えると寸法も変わる");
    const std::string text = kachakacha::v2::app::ScaledSizeTextJa(changed.Value());
    Require(text.find("1/150") != std::string::npos, "縮尺を言う");
    Require(text.find("mm") != std::string::npos, "単位を言う");
}

KACHA_V2_TEST(parameters, 縮尺の分母は0にできない)
{
    // 0 で割ると寸法が決まらない。黙って1へ寄せない。
    const auto refused = SetParameter(DefaultParameters(),
        ParameterId::ScaleDenominator, "0");
    Require(!refused.HasValue(), "断る");
}

KACHA_V2_TEST(parameters, 実寸に式が書ける)
{
    // 図面から読んだ寸法をそのまま足し引きできる。
    const auto changed = SetParameter(DefaultParameters(), ParameterId::RealSizeMm,
        "17500+2500");
    Require(changed.HasValue(), "式が通る");
    Require(std::abs(ParameterValueOf(changed.Value(), ParameterId::RealSizeMm) - 20000.0)
            < 1e-9,
        "20000mm になる");
}

KACHA_V2_TEST_MAIN("command_parameter_tests")
