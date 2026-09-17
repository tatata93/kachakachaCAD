#include "kachakacha/app/ApproxInput.h"

#include <algorithm>

namespace kachakacha::v2::app {
namespace {

[[nodiscard]] std::string Millimetres3(double value)
{
    const long long micro = static_cast<long long>(value * 1000.0 + (value < 0 ? -0.5 : 0.5));
    const long long whole = micro / 1000;
    const long long rest = std::llabs(micro % 1000);
    std::string tail = std::to_string(rest);
    while (tail.size() < 3) {
        tail = "0" + tail;
    }
    return std::to_string(whole) + "." + tail + " mm";
}

} // namespace

const std::vector<ApproxCandidateSpec>& ApproxCandidateSpecs()
{
    static const std::vector<ApproxCandidateSpec> specs{
        {"A", "面ごとに展開", "伸ばさず平らにできる面だけ。棚の「面ごとに分ける」に従う"},
        {"B", "帯で近似", "棚の分割(自動/手動)・上限・範囲のとおりに帯を切る"},
        {"C", "帯1枚で近似", "分割しない。どれだけずれるかが分かる"},
    };
    return specs;
}

int ApproxCandidateForMethod(int method)
{
    return method == 0 ? 0 : 1;
}

domain::CreateFabricationModelDefinition ApproxCandidateDefinition(
    const domain::CreateFabricationModelDefinition& base, int candidate)
{
    // 棚の欄が正本である。候補は方式を変えるだけで、分割・上限・範囲は欄のまま使う。
    // 欄のとおりの候補(A か B)は、これまでの「選んでから押す」とまったく同じ作り方になる。
    domain::CreateFabricationModelDefinition made = base;
    switch (candidate) {
    case 0:
        made.method = 0;   // V2 方式: 面を分類して展開
        break;
    case 2:
        made.method = 1;   // V1 方式、分割なし
        made.automaticBoundaries = true;
        made.manualBoundaries.clear();
        made.maximumPartCount = 1;
        break;
    default:
        made.method = 1;   // V1 方式、欄のとおりに分割
        break;
    }
    return made;
}

std::string ApproxCandidateLineJa(const ApproxCandidateSpec& spec,
    const ApproxCandidateOutcome& outcome)
{
    std::string line = spec.label + " " + spec.nameJa + " — ";
    if (!outcome.evaluated) {
        return line + "(対象を選ぶと作って比べます)";
    }
    if (!outcome.available) {
        return line + "作れません: " + outcome.refusalJa;
    }
    line += std::to_string(outcome.partCount) + "部材 / 最大 "
        + Millimetres3(outcome.maximumDeviationMm);
    if (!outcome.reachedTolerance) {
        line += "(許すずれを超えています)";
    }
    return line;
}

int PreferredApproxCandidate(const std::vector<ApproxCandidateOutcome>& outcomes,
    int baseMethod)
{
    // まず棚の方式どおりの候補。作れるなら、それが人の決めたことである。
    const int configured = ApproxCandidateForMethod(baseMethod);
    if (configured < static_cast<int>(outcomes.size())
        && outcomes[static_cast<std::size_t>(configured)].available) {
        return configured;
    }
    // 作れなければ、許すずれに収まって作れたもののうち部材が最も少ないもの。
    int best = -1;
    for (int index = 0; index < static_cast<int>(outcomes.size()); ++index) {
        const auto& outcome = outcomes[static_cast<std::size_t>(index)];
        if (!outcome.available || !outcome.reachedTolerance) {
            continue;
        }
        if (best < 0
            || outcome.partCount < outcomes[static_cast<std::size_t>(best)].partCount) {
            best = index;
        }
    }
    if (best >= 0) {
        return best;
    }
    for (int index = 0; index < static_cast<int>(outcomes.size()); ++index) {
        if (outcomes[static_cast<std::size_t>(index)].available) {
            return index;
        }
    }
    return configured;
}

ApproxInputState WithoutApproxSources(const ApproxInputState& state,
    const std::vector<base::EntityId>& ids)
{
    ApproxInputState next = state;
    for (const base::EntityId& id : ids) {
        next.sources.erase(std::remove(next.sources.begin(), next.sources.end(), id),
            next.sources.end());
    }
    return next;
}

ApproxInputState WithApproxSourcesToggled(const ApproxInputState& state,
    const std::vector<base::EntityId>& ids)
{
    ApproxInputState next = state;
    for (const base::EntityId& id : ids) {
        const auto found = std::find(next.sources.begin(), next.sources.end(), id);
        if (found != next.sources.end()) {
            next.sources.erase(found);
        } else {
            next.sources.push_back(id);
        }
    }
    return next;
}

std::vector<std::string> ApproxStatusLinesJa(const ApproxInputState& state,
    const std::vector<ApproxCandidateOutcome>& outcomes, bool previewShown)
{
    std::vector<std::string> lines;
    lines.push_back("▶ 次のクリック → 対象(" + std::to_string(state.sources.size() + 1)
        + "つ目)。もう一度押すと外れます");
    if (state.sources.empty()) {
        lines.push_back("… 面か立体を 3D で押してください");
        return lines;
    }
    lines.push_back("✓ 対象 " + std::to_string(state.sources.size()) + "つ");
    const auto& specs = ApproxCandidateSpecs();
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const ApproxCandidateOutcome outcome =
            index < outcomes.size() ? outcomes[index] : ApproxCandidateOutcome{};
        const bool selected = static_cast<int>(index) == state.selectedCandidate;
        lines.push_back(std::string(selected ? "● " : "○ ")
            + ApproxCandidateLineJa(specs[index], outcome));
    }
    const std::size_t chosen = static_cast<std::size_t>(std::max(0, state.selectedCandidate));
    const bool chosenAvailable = chosen < outcomes.size() && outcomes[chosen].available;
    if (!chosenAvailable) {
        lines.push_back("× 選んでいる候補は作れません。別の候補を選んでください");
        return lines;
    }
    lines.push_back("✓ 生成可能");
    lines.push_back(previewShown ? "✓ 下見を表示中(まだ文書へ保存していません)"
                                 : "× 下見が作れませんでした");
    return lines;
}

std::string ApproxFooterLine(const ApproxInputState& state,
    const std::vector<ApproxCandidateOutcome>& outcomes, bool previewShown)
{
    std::string line = "近似: SOURCES=" + std::to_string(state.sources.size());
    const auto& specs = ApproxCandidateSpecs();
    const std::size_t chosen = static_cast<std::size_t>(std::max(0, state.selectedCandidate));
    line += " / CANDIDATE=" + (chosen < specs.size() ? specs[chosen].label : std::string("?"));
    if (chosen < outcomes.size() && outcomes[chosen].available) {
        line += " / PARTS=" + std::to_string(outcomes[chosen].partCount);
        line += " / MAXDEV=" + Millimetres3(outcomes[chosen].maximumDeviationMm);
    }
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

} // namespace kachakacha::v2::app
