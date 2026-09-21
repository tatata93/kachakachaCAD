#include "kachakacha/modeling/LoftInput.h"

#include "kachakacha/geometry/ChainTrim.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SurfaceCardinality.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace kachakacha::v2::modeling::detail {

using base::MakeError;
using base::MakeWarning;
using base::Result;

namespace {

//! 「0.420」の形(小数第 3 位まで)。std::to_string の 6 桁は読みにくい。
[[nodiscard]] std::string Mm(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

[[nodiscard]] std::string Label(const GuideSurfaceRequest& request, std::size_t index)
{
    return ChainLabel(request.method, request.chains[index]);
}

//! 役割・個数・開閉の検査。ここで落ちたら幾何は見ない。
[[nodiscard]] std::vector<Diagnostic> CheckLoftInputs(const GuideSurfaceRequest& request,
    const std::vector<std::size_t>& sections, const std::vector<std::size_t>& rails)
{
    std::vector<Diagnostic> errors;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        const ChainRole role = request.chains[index].role;
        if (role != ChainRole::Section && role != ChainRole::GuideU
            && role != ChainRole::Centerline) {
            errors.push_back(MakeError(kBadInput,
                "ロフトには断面・ガイド・中心線だけを渡します。", Label(request, index)));
        }
    }
    for (const RoleCardinality& cardinality : SurfaceCardinality(request.method)) {
        const std::size_t count = IndicesWithRole(request, cardinality.role).size();
        const std::string problem = SurfaceCardinalityProblemJa(request.method,
            cardinality.role, count, ChainRoleLabelJa(request.method, cardinality.role));
        if (!problem.empty()) {
            errors.push_back(MakeError(kBadInput, problem, {}));
        }
    }
    if (const std::string rule = LoftSectionRuleProblemJa(sections.size(), rails.size());
        !rule.empty()) {
        errors.push_back(MakeError(kBadInput, rule, {}));
    }
    const std::vector<Diagnostic> mixed = CheckSectionOpenClosed(request, sections);
    errors.insert(errors.end(), mixed.begin(), mixed.end());
    if (!rails.empty() && !sections.empty() && request.chains[sections.front()].closed) {
        errors.push_back(MakeError(kBadInput,
            "閉じた断面にガイドを付けたロフトは、まだ作れません。",
            "ガイドを外して断面だけでロフトするか、断面を開いた線にしてください。"));
    }
    return errors;
}

//! 隣り合う断面が重なっていないか。
[[nodiscard]] std::vector<Diagnostic> CheckDistinct(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& order,
    const GeometryTolerance& tolerance)
{
    for (std::size_t at = 1; at < order.size(); ++at) {
        if (!SectionsAreDistinct(sampled[order[at - 1]], sampled[order[at]],
                tolerance.modelLinearMm)) {
            return {MakeError(kSectionOrder, "断面どうしが重なっていて、面になりません。",
                Label(request, order[at - 1]) + " と " + Label(request, order[at]))};
        }
    }
    return {};
}

//! ガイドが無いときの断面の順。手動固定 > 中心線に沿った順 > 重心の並び。
[[nodiscard]] std::vector<std::size_t> OrderWithoutRails(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& sections,
    const std::vector<std::size_t>& centerlines)
{
    if (request.keepSectionOrder) {
        return sections;   // 人が画面で決めた順をそのまま使う
    }
    if (!centerlines.empty()) {
        std::vector<std::pair<double, std::size_t>> keyed;
        for (const std::size_t section : sections) {
            const ChainCrossing closest =
                FindClosestApproach(sampled[centerlines.front()], sampled[section]);
            keyed.emplace_back(closest.firstParameter, section);
        }
        std::stable_sort(keyed.begin(), keyed.end(),
            [](const auto& l, const auto& r) { return l.first < r.first; });
        std::vector<std::size_t> order;
        for (const auto& item : keyed) {
            order.push_back(item.second);
        }
        return order;
    }
    return OrderSections(sampled, sections).chainIndices;
}

//! ガイド × 断面の交わり。crossings[r][s] は rails[r] と sections[s]。
struct RailGrid {
    std::vector<std::vector<ChainCrossing>> crossings;
};

[[nodiscard]] std::vector<Diagnostic> FindRailCrossings(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& rails,
    const std::vector<std::size_t>& sections, double joinTolerance, RailGrid& grid)
{
    std::vector<Diagnostic> errors;
    grid.crossings.assign(rails.size(), std::vector<ChainCrossing>(sections.size()));
    for (std::size_t r = 0; r < rails.size(); ++r) {
        for (std::size_t s = 0; s < sections.size(); ++s) {
            const ChainCrossing crossing =
                FindClosestApproach(sampled[rails[r]], sampled[sections[s]]);
            if (crossing.distanceMm < 0.0 || crossing.distanceMm > joinTolerance) {
                errors.push_back(MakeError(kNotConnected,
                    Label(request, rails[r]) + "が" + Label(request, sections[s])
                        + "と交わっていません。",
                    "いちばん近いところで " + Mm(std::max(crossing.distanceMm, 0.0))
                        + " mm 離れています(許容 " + Mm(joinTolerance) + " mm)。"
                        + "ガイドは、すべての断面と交わるように引いてください。"));
                continue;
            }
            const int count =
                CountApproaches(sampled[rails[r]], sampled[sections[s]], joinTolerance);
            if (count > 1) {
                errors.push_back(MakeError(kCrossingMissing,
                    Label(request, rails[r]) + "が" + Label(request, sections[s]) + "と "
                        + std::to_string(count) + " か所で交わっています。",
                    "1 本のガイドは、それぞれの断面と 1 か所で交わるようにしてください。"));
                continue;
            }
            grid.crossings[r][s] = crossing;
        }
    }
    return errors;
}

//! 並びが同じか、ちょうど逆か。
[[nodiscard]] bool SameOrReversed(const std::vector<std::size_t>& a,
    const std::vector<std::size_t>& b, bool& reversed)
{
    reversed = false;
    if (a == b) {
        return true;
    }
    std::vector<std::size_t> flipped(b.rbegin(), b.rend());
    if (a == flipped) {
        reversed = true;
        return true;
    }
    return false;
}

//! a の中で前後が、b では逆になっている最初の 2 つ。
[[nodiscard]] std::pair<std::size_t, std::size_t> FirstSwappedPair(
    const std::vector<std::size_t>& a, const std::vector<std::size_t>& b)
{
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = i + 1; j < a.size(); ++j) {
            const auto pi = std::find(b.begin(), b.end(), a[i]);
            const auto pj = std::find(b.begin(), b.end(), a[j]);
            if (pi > pj) {
                return {a[i], a[j]};
            }
        }
    }
    return {a.front(), a.back()};
}

//! ガイドに沿った断面の順。すべてのガイドで同じ(向きは問わない)でなければねじれる。
[[nodiscard]] std::vector<Diagnostic> OrderAlongRails(const GuideSurfaceRequest& request,
    const std::vector<std::size_t>& rails, const std::vector<std::size_t>& sections,
    const RailGrid& grid, std::vector<std::size_t>& order)
{
    const auto orderOf = [&](std::size_t r) {
        std::vector<std::pair<double, std::size_t>> keyed;
        for (std::size_t s = 0; s < sections.size(); ++s) {
            keyed.emplace_back(grid.crossings[r][s].firstParameter, sections[s]);
        }
        std::sort(keyed.begin(), keyed.end());
        std::vector<std::size_t> list;
        for (const auto& item : keyed) {
            list.push_back(item.second);
        }
        return list;
    };
    order = orderOf(0);
    for (std::size_t r = 1; r < rails.size(); ++r) {
        std::vector<std::size_t> other = orderOf(r);
        bool reversed = false;
        if (!SameOrReversed(order, other, reversed)) {
            const auto pair = FirstSwappedPair(order, other);
            return {MakeError(kCrossingOrder,
                Label(request, pair.first) + "と" + Label(request, pair.second)
                    + "の順序が、" + Label(request, rails[0]) + "と"
                    + Label(request, rails[r]) + "で逆転しています。",
                "このままでは面がねじれます。断面かガイドの引き方を見直してください。")};
        }
    }
    if (request.keepSectionOrder) {
        // 手動固定は、ガイドに沿った順(どちら向きでも)と合っていなければならない。
        bool reversed = false;
        if (!SameOrReversed(order, sections, reversed)) {
            std::string along;
            for (const std::size_t index : order) {
                along += (along.empty() ? "" : " → ") + Label(request, index);
            }
            return {MakeError(kSectionOrder,
                "手動で固定した断面の順が、ガイドに沿った順と合っていません。",
                "ガイドに沿った順: " + along + "。")};
        }
        order = sections;
    }
    return {};
}

//! sections の中での位置。
[[nodiscard]] std::size_t PositionOf(const std::vector<std::size_t>& sections, std::size_t chain)
{
    return static_cast<std::size_t>(
        std::find(sections.begin(), sections.end(), chain) - sections.begin());
}

//! 断面の向き。ガイド 2 本以上なら、断面に沿ったガイドの並びがちょうど逆の断面を
//! 逆向きに使う(引いた向きが違うだけ)。並びが入れ替わっていればねじれとして断る。
//! ガイド 1 本なら、隣の断面と端どうしが近くなる向きを採る。
[[nodiscard]] std::vector<Diagnostic> OrientSections(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& rails,
    const std::vector<std::size_t>& sections, const RailGrid& grid,
    const std::vector<std::size_t>& order, std::vector<bool>& reverse)
{
    reverse.assign(order.size(), false);
    const auto railsAlong = [&](std::size_t chain) {
        const std::size_t s = PositionOf(sections, chain);
        std::vector<std::pair<double, std::size_t>> keyed;
        for (std::size_t r = 0; r < rails.size(); ++r) {
            keyed.emplace_back(grid.crossings[r][s].secondParameter, rails[r]);
        }
        std::sort(keyed.begin(), keyed.end());
        std::vector<std::size_t> list;
        for (const auto& item : keyed) {
            list.push_back(item.second);
        }
        return list;
    };
    if (rails.size() >= 2) {
        const std::vector<std::size_t> reference = railsAlong(order.front());
        for (std::size_t at = 1; at < order.size(); ++at) {
            const std::vector<std::size_t> here = railsAlong(order[at]);
            bool reversed = false;
            if (!SameOrReversed(reference, here, reversed)) {
                const auto pair = FirstSwappedPair(reference, here);
                return {MakeError(kCrossingOrder,
                    Label(request, pair.first) + "と" + Label(request, pair.second)
                        + "の並びが、" + Label(request, order.front()) + "と"
                        + Label(request, order[at]) + "で入れ替わっています。",
                    "このままでは面がねじれます。ガイドが断面の上を同じ並びで通るようにしてください。")};
            }
            reverse[at] = reversed;
        }
        return {};
    }
    for (std::size_t at = 1; at < order.size(); ++at) {
        // 直前の断面を、決めた向きにした点列と比べる。
        SampledChain previous = sampled[order[at - 1]];
        if (reverse[at - 1]) {
            std::reverse(previous.points.begin(), previous.points.end());
        }
        reverse[at] = ShouldReverseAgainst(previous, sampled[order[at]]);
    }
    return {};
}

//! ガイドが断面のどちら側にあるか。向きをそろえた断面の上の位置で決める。
void ClassifyRails(const std::vector<SampledChain>& sampled,
    const std::vector<std::size_t>& rails, const std::vector<std::size_t>& sections,
    const RailGrid& grid, const std::vector<std::size_t>& order,
    const std::vector<bool>& reverse, double joinTolerance, LoftPlan& plan)
{
    plan.rails.clear();
    for (std::size_t r = 0; r < rails.size(); ++r) {
        bool atStart = true;
        bool atEnd = true;
        for (std::size_t at = 0; at < order.size(); ++at) {
            const std::size_t s = PositionOf(sections, order[at]);
            const double length = std::max(sampled[order[at]].lengthMm, 1.0e-9);
            double t = grid.crossings[r][s].secondParameter;
            if (reverse[at]) {
                t = 1.0 - t;
            }
            atStart = atStart && t * length <= joinTolerance;
            atEnd = atEnd && (1.0 - t) * length <= joinTolerance;
        }
        LoftRail rail;
        rail.chainIndex = rails[r];
        rail.side = atStart ? LoftRailSide::Start
                            : (atEnd ? LoftRailSide::End : LoftRailSide::Interior);
        plan.rails.push_back(std::move(rail));
    }
}

//! 張り直すときのガイドは、最初の断面から最後の断面までの部分。はみ出しは断る
//! (はみ出した部分は面に乗らない。黙って捨てると「通る線」を無視したことになる)。
[[nodiscard]] std::vector<Diagnostic> SpanRails(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& sections,
    const RailGrid& grid, const std::vector<std::size_t>& order, double joinTolerance,
    LoftPlan& plan)
{
    std::vector<Diagnostic> errors;
    const std::size_t first = PositionOf(sections, order.front());
    const std::size_t last = PositionOf(sections, order.back());
    for (std::size_t r = 0; r < plan.rails.size(); ++r) {
        LoftRail& rail = plan.rails[r];
        const SampledChain& along = sampled[rail.chainIndex];
        const double a = grid.crossings[r][first].firstParameter;
        const double b = grid.crossings[r][last].firstParameter;
        const double before = std::min(a, b) * along.lengthMm;
        const double after = (1.0 - std::max(a, b)) * along.lengthMm;
        if (before > joinTolerance || after > joinTolerance) {
            errors.push_back(MakeError(kBadInput,
                Label(request, rail.chainIndex) + "が端の断面より外へ "
                    + Mm(std::max(before, after)) + " mm はみ出しています。",
                "面は最初の断面から最後の断面までの間に張ります。はみ出した部分は面に乗りません。"
                "ガイドを断面のところで切るか、端に断面を足してください。"));
            continue;
        }
        const auto span = geometry::TrimChainBetween(request.chains[rail.chainIndex].segments,
            grid.crossings[r][first].position, grid.crossings[r][last].position,
            joinTolerance);
        if (!span.HasValue()) {
            errors.push_back(MakeError(kBadInput,
                Label(request, rail.chainIndex) + "を断面の間で切り出せませんでした。",
                span.Diagnostics().empty() ? std::string()
                                           : span.Diagnostics().front().summaryJa));
            continue;
        }
        rail.span = span.Value();
    }
    return errors;
}

//! 2 本のレールで掃く近道が使えるか: 外側のガイドが始点側と終点側に 1 本ずつだけ。
[[nodiscard]] bool TwoOuterRails(const LoftPlan& plan, bool centerline)
{
    if (centerline || plan.rails.size() != 2) {
        return false;
    }
    const LoftRailSide a = plan.rails[0].side;
    const LoftRailSide b = plan.rails[1].side;
    return (a == LoftRailSide::Start && b == LoftRailSide::End)
        || (a == LoftRailSide::End && b == LoftRailSide::Start);
}

//! 従来の案内付きロフトと同じ: 端に断面が無ければ仮想断面を作る位置を決める。
void PlanVirtualEndSections(const GuideSurfaceRequest& request, const RailGrid& grid,
    std::size_t startRail, GuideSurfaceAnalysis& analysis)
{
    if (!request.createVirtualEndSections || grid.crossings.empty()) {
        return;
    }
    std::vector<double> along;
    for (const ChainCrossing& crossing : grid.crossings[startRail]) {
        along.push_back(crossing.firstParameter);
    }
    std::sort(along.begin(), along.end());
    const double edgeTolerance = 1.0e-3;
    if (along.front() > edgeTolerance) {
        analysis.virtualSectionParameters.push_back(0.0);
    }
    if (1.0 - along.back() > edgeTolerance) {
        analysis.virtualSectionParameters.push_back(1.0);
    }
    if (!analysis.virtualSectionParameters.empty()) {
        analysis.notes.push_back(MakeWarning("GEO-G104",
            "端に断面が無いので、仮想断面を作ります。",
            std::to_string(analysis.virtualSectionParameters.size())
                + " 本。設定で作らないようにもできます。"));
    }
}

//! ガイドがあるときの残り(交わり → 順 → 向き → 側 → 作り方)。
[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzeWithRails(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::vector<SampledChain>& sampled,
    const std::vector<std::size_t>& sections, const std::vector<std::size_t>& rails,
    const std::vector<std::size_t>& centerlines, GuideSurfaceAnalysis analysis)
{
    const double joinTolerance = JoinToleranceMm(tolerance);
    RailGrid grid;
    std::vector<Diagnostic> errors =
        FindRailCrossings(request, sampled, rails, sections, joinTolerance, grid);
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    std::vector<std::size_t> order;
    errors = OrderAlongRails(request, rails, sections, grid, order);
    if (errors.empty()) {
        errors = CheckDistinct(request, sampled, order, tolerance);
    }
    if (errors.empty()) {
        errors = OrientSections(request, sampled, rails, sections, grid, order,
            analysis.loft.reverseSections);
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    analysis.sectionOrdering.chainIndices = order;
    for (const auto& row : grid.crossings) {
        analysis.crossings.insert(analysis.crossings.end(), row.begin(), row.end());
    }
    ClassifyRails(sampled, rails, sections, grid, order, analysis.loft.reverseSections,
        joinTolerance, analysis.loft);
    if (TwoOuterRails(analysis.loft, !centerlines.empty())) {
        analysis.loft.solver = LoftSolver::TwoRailSweep;
        const std::size_t startRail =
            analysis.loft.rails[0].side == LoftRailSide::Start ? 0 : 1;
        PlanVirtualEndSections(request, grid, startRail, analysis);
        return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
    }
    if (sections.size() < 2) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
            "断面が 2 本以上必要です(いま 1 本)。",
            "1 本で作れるのは、断面の両端にガイドが 1 本ずつあるときだけです。"));
    }
    analysis.loft.solver = LoftSolver::RailFilling;
    errors = SpanRails(request, sampled, sections, grid, order, joinTolerance, analysis.loft);
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    analysis.notes.push_back(MakeWarning("GEO-G106",
        "断面とガイドを全部通るように張る面です(近似)。",
        "出来た面は、全部の断面とガイドからの外れを測り、外れすぎていれば採用しません。"));
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

} // namespace

Result<GuideSurfaceAnalysis> AnalyzeLoft(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::vector<SampledChain>& sampled)
{
    const std::vector<std::size_t> sections = IndicesWithRole(request, ChainRole::Section);
    const std::vector<std::size_t> rails = IndicesWithRole(request, ChainRole::GuideU);
    const std::vector<std::size_t> centerlines =
        IndicesWithRole(request, ChainRole::Centerline);
    std::vector<Diagnostic> errors = CheckLoftInputs(request, sections, rails);
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    GuideSurfaceAnalysis analysis;
    analysis.method = request.method;
    analysis.loft.hasCenterline = !centerlines.empty();
    if (analysis.loft.hasCenterline) {
        analysis.loft.centerlineChainIndex = centerlines.front();
    }
    if (sections.size() + rails.size() > kSurfaceInputSoftWarningCount) {
        analysis.notes.push_back(MakeWarning("GEO-G107",
            "入力が多いので、面を作るのに時間がかかることがあります。",
            "断面 " + std::to_string(sections.size()) + " 本、ガイド "
                + std::to_string(rails.size()) + " 本。"));
    }
    if (!rails.empty()) {
        return AnalyzeWithRails(request, tolerance, sampled, sections, rails, centerlines,
            std::move(analysis));
    }
    const std::vector<std::size_t> order =
        OrderWithoutRails(request, sampled, sections, centerlines);
    errors = CheckDistinct(request, sampled, order, tolerance);
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    analysis.sectionOrdering.chainIndices = order;
    analysis.loft.solver =
        analysis.loft.hasCenterline ? LoftSolver::Centerline : LoftSolver::Sections;
    // 開いた断面は向きの取り違えでねじれる。反転したほうが近いものは知らせる(従来どおり)。
    if (!request.chains[order.front()].closed) {
        for (std::size_t at = 1; at < order.size(); ++at) {
            if (ShouldReverseAgainst(sampled[order[at - 1]], sampled[order[at]])) {
                analysis.notes.push_back(MakeWarning("GEO-G103",
                    "断面の向きを揃え直しました。", Label(request, order[at])));
            }
        }
    }
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

} // namespace kachakacha::v2::modeling::detail
