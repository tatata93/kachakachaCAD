// 「近似」の入力と候補(app/ApproxInput.h、引継ぎ 2026-09-17 の 3)。
#include "kachakacha/app/ApproxInput.h"
#include "kachakacha/base/TestHarness.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using kachakacha::v2::app::ApproxCandidateDefinition;
using kachakacha::v2::app::ApproxCandidateForMethod;
using kachakacha::v2::app::ApproxCandidateLineJa;
using kachakacha::v2::app::ApproxCandidateOutcome;
using kachakacha::v2::app::ApproxCandidateSpecs;
using kachakacha::v2::app::ApproxPolicy;
using kachakacha::v2::app::ApproxPolicySpecs;
using kachakacha::v2::app::CandidateForPolicy;
using kachakacha::v2::app::ApproxFooterLine;
using kachakacha::v2::app::ApproxInputState;
using kachakacha::v2::app::ApproxStatusLinesJa;
using kachakacha::v2::app::PreferredApproxCandidate;
using kachakacha::v2::app::WithApproxSourcesToggled;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(std::uint8_t value)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

} // namespace

KACHA_V2_TEST(approx_input, 候補は同じ作り方に方式の違いを重ねるだけ)
{
    // backend を複製しない。候補ごとの違いは方式(と C の上限)だけで、
    // 板厚も許すずれも開口も分割の欄も、棚から来た基本のまま。
    kachakacha::v2::domain::CreateFabricationModelDefinition base;
    base.parts = {Id(1)};
    base.fidelity = 9;
    base.maximumPartCount = 6;
    base.automaticBoundaries = false;
    base.manualBoundaries = {0.3, 0.6};
    base.splitSolidFaces = false;
    base.openingWires = {Id(7)};
    const auto a = ApproxCandidateDefinition(base, 0);
    const auto b = ApproxCandidateDefinition(base, 1);
    const auto c = ApproxCandidateDefinition(base, 2);
    Require(a.method == 0 && !a.splitSolidFaces, "A は面ごとに展開、分けるかは欄のまま");
    Require(b.method == 1 && !b.automaticBoundaries && b.manualBoundaries.size() == 2
            && b.maximumPartCount == 6,
        "B は帯、手動境界も上限も欄のまま");
    Require(c.method == 1 && c.automaticBoundaries && c.maximumPartCount == 1, "C は帯1枚");
    for (const auto* made : {&a, &b, &c}) {
        Require(made->fidelity == 9 && made->openingWires.size() == 1
                && made->parts.size() == 1,
            "基本の作り方は変えない");
    }
    Require(ApproxCandidateSpecs().size() == 3, "候補は3つ");
    Require(ApproxCandidateForMethod(0) == 0 && ApproxCandidateForMethod(1) == 1,
        "棚の方式は A か B に当たる");
}

KACHA_V2_TEST(approx_input, 既定は棚の方式どおり_作れなければ収まる最少部材)
{
    std::vector<ApproxCandidateOutcome> outcomes(3);
    outcomes[0] = {true, true, 3, 0.0, true, ""};
    outcomes[1] = {true, true, 4, 0.18, true, ""};
    outcomes[2] = {true, true, 1, 0.90, false, ""};
    Require(PreferredApproxCandidate(outcomes, 1) == 1, "棚が帯なら B");
    Require(PreferredApproxCandidate(outcomes, 0) == 0, "棚が面ごとなら A");
    outcomes[0] = {true, false, 0, 0.0, true, "展開できない面があります"};
    Require(PreferredApproxCandidate(outcomes, 0) == 1, "A が作れなければ収まって作れた B");
    outcomes[1].reachedTolerance = false;
    Require(PreferredApproxCandidate(outcomes, 0) == 1, "収まるものが無ければ作れた最初");
    outcomes[1].available = false;
    Require(PreferredApproxCandidate(outcomes, 0) == 2, "残っているのは C");
}

KACHA_V2_TEST(approx_input, 候補の行は部材数とずれを言う)
{
    const auto& specs = ApproxCandidateSpecs();
    ApproxCandidateOutcome ok{true, true, 4, 0.18, true, ""};
    const auto line = ApproxCandidateLineJa(specs[1], ok);
    Require(line.find("4部材") != std::string::npos && line.find("0.180 mm") != std::string::npos,
        std::string("部材数とずれ: ") + line);
    Require(line.find("方式 帯") != std::string::npos, std::string("Bは帯: ") + line);
    // 平均誤差は backend に無いので常に「—」(F-03、BLOCKED_BACKEND: 作らない)。
    const auto lineA = ApproxCandidateLineJa(specs[0], ok);
    Require(lineA.find("平均 —") != std::string::npos, std::string("平均は—: ") + lineA);
    Require(lineA.find("方式 面ごと") != std::string::npos, std::string("Aは面ごと: ") + lineA);
    ApproxCandidateOutcome refused{true, false, 0, 0.0, true, "二重曲面です"};
    Require(ApproxCandidateLineJa(specs[0], refused).find("作れません: 二重曲面")
            != std::string::npos,
        "理由を言う");
    ApproxCandidateOutcome over{true, true, 1, 0.9, false, ""};
    Require(ApproxCandidateLineJa(specs[2], over).find("超えて") != std::string::npos,
        "許すずれを超えたと言う");
}

KACHA_V2_TEST(approx_input, 押したものは入り再び押すと外れる)
{
    ApproxInputState state;
    state = WithApproxSourcesToggled(state, {Id(1)});
    state = WithApproxSourcesToggled(state, {Id(2)});
    Require(state.sources.size() == 2, "2つ");
    state = WithApproxSourcesToggled(state, {Id(1)});
    Require(state.sources.size() == 1 && state.sources[0] == Id(2), "外れる");
    const auto lines = ApproxStatusLinesJa(state, {}, false);
    Require(!lines.empty() && lines.front().find("次のクリック") != std::string::npos,
        "次にどこへ入るかが一番上");
    const auto footer = ApproxFooterLine(state, {}, false);
    Require(footer.find("SOURCES=1") != std::string::npos
            && footer.find("CANDIDATE=B") != std::string::npos,
        std::string("一番下の一行: ") + footer);
}

KACHA_V2_TEST(approx_input, 作り方は候補の既定を決める)
{
    std::vector<ApproxCandidateOutcome> outcomes(3);
    outcomes[0] = {true, true, 3, 0.0, true, ""};
    outcomes[1] = {true, true, 4, 0.18, true, ""};
    outcomes[2] = {true, true, 1, 0.90, false, ""};
    Require(ApproxPolicySpecs().size() == 4, "標準 / 少部品優先 / 精度優先 / 手動条件");
    Require(CandidateForPolicy(ApproxPolicy::Standard, outcomes, 1) == 1, "標準は棚どおり(B)");
    Require(CandidateForPolicy(ApproxPolicy::Manual, outcomes, 0) == 0, "手動条件も棚どおり(A)");
    Require(CandidateForPolicy(ApproxPolicy::FewerParts, outcomes, 1) == 2, "少部品優先は 1 部材の C");
    Require(CandidateForPolicy(ApproxPolicy::Precision, outcomes, 1) == 0, "精度優先はずれ 0 の A");
    outcomes[0].available = false;
    Require(CandidateForPolicy(ApproxPolicy::Precision, outcomes, 1) == 1, "作れないものは飛ばす");
    for (auto& outcome : outcomes) {
        outcome.available = false;
    }
    Require(CandidateForPolicy(ApproxPolicy::FewerParts, outcomes, 1) == 1, "何も作れなければ棚の方式どおり");
}

KACHA_V2_TEST_MAIN("approx_input_tests")
