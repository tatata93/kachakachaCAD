#include "kachakacha/modeling/FourEdgeInput.h"

#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SurfaceCardinality.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

namespace kachakacha::v2::modeling::detail {

using base::MakeError;
using base::MakeWarning;
using base::Result;

namespace {

[[nodiscard]] std::string Mm(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

//! 辺の呼び名。輪の順で U0 / V1 / U1 / V0。
[[nodiscard]] const char* SideName(std::size_t position) noexcept
{
    static const char* const kNames[] = {"U0", "V1", "U1", "V0"};
    return kNames[position % 4];
}

[[nodiscard]] Vector3 StartOf(const SampledChain& chain, bool reversed)
{
    return reversed ? chain.points.back() : chain.points.front();
}

[[nodiscard]] Vector3 EndOf(const SampledChain& chain, bool reversed)
{
    return reversed ? chain.points.front() : chain.points.back();
}

//! 端点が 3 本以上集まっていないか(枝分かれした 4 辺は輪にならない)。
[[nodiscard]] std::vector<Diagnostic> CheckBranching(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& sides,
    double joinTolerance)
{
    std::vector<Vector3> ends;
    for (const std::size_t index : sides) {
        ends.push_back(sampled[index].points.front());
        ends.push_back(sampled[index].points.back());
    }
    for (std::size_t i = 0; i < ends.size(); ++i) {
        std::size_t touching = 0;
        for (std::size_t j = 0; j < ends.size(); ++j) {
            if ((ends[i] - ends[j]).Length() <= joinTolerance) {
                ++touching;
            }
        }
        if (touching > 2) {
            return {MakeError(kNotConnected, "四辺面の辺が 1 つの角に 3 本以上集まっています。",
                ChainLabel(request.method, request.chains[sides[i / 2]])
                    + " の端です。4 辺が 1 周する輪になるように選んでください。")};
        }
    }
    return {};
}

//! 端点をたどって輪の順と向きを決める。最初の辺の向きを基準にする。
[[nodiscard]] std::vector<Diagnostic> OrderRing(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& sides,
    double joinTolerance, FourEdgePlan& plan)
{
    std::vector<std::size_t> remaining(sides.begin() + 1, sides.end());
    plan.sides = {sides.front()};
    plan.reversed = {false};
    while (!remaining.empty()) {
        const SampledChain& tail = sampled[plan.sides.back()];
        const Vector3 at = EndOf(tail, plan.reversed.back());
        double bestGap = std::numeric_limits<double>::infinity();
        std::size_t best = 0;
        bool bestReversed = false;
        for (std::size_t k = 0; k < remaining.size(); ++k) {
            const SampledChain& candidate = sampled[remaining[k]];
            const double toStart = (at - candidate.points.front()).Length();
            const double toEnd = (at - candidate.points.back()).Length();
            if (std::min(toStart, toEnd) < bestGap) {
                bestGap = std::min(toStart, toEnd);
                best = k;
                bestReversed = toEnd < toStart;
            }
        }
        if (bestGap > joinTolerance) {
            const std::size_t position = plan.sides.size() - 1;
            return {MakeError(kNotConnected, "四辺面の境界が閉じていません。",
                std::string(SideName(position)) + "("
                    + ChainLabel(request.method, request.chains[plan.sides.back()])
                    + ")の終点から、次の辺までが " + Mm(bestGap) + " mm 離れています(許容 "
                    + Mm(joinTolerance) + " mm)。端点スナップで引き直すとつながります。")};
        }
        plan.sides.push_back(remaining[best]);
        plan.reversed.push_back(bestReversed);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(best));
    }
    const double closing = (EndOf(sampled[plan.sides.back()], plan.reversed.back())
        - StartOf(sampled[plan.sides.front()], plan.reversed.front())).Length();
    if (closing > joinTolerance) {
        return {MakeError(kNotConnected, "四辺面の境界が閉じていません。",
            std::string(SideName(3)) + "の終点と" + SideName(0) + "の始点が " + Mm(closing)
                + " mm 離れています(許容 " + Mm(joinTolerance) + " mm)。")};
    }
    return {};
}

} // namespace

Result<GuideSurfaceAnalysis> AnalyzeFourEdgePatch(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::vector<SampledChain>& sampled)
{
    std::vector<Diagnostic> errors;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        const ChainRole role = request.chains[index].role;
        if (role != ChainRole::BoundarySide && role != ChainRole::GuideU) {
            errors.push_back(MakeError(kBadInput,
                "四辺面には 4 辺と通る線だけを渡します。",
                ChainLabel(request.method, request.chains[index])));
        }
    }
    for (const RoleCardinality& cardinality : SurfaceCardinality(request.method)) {
        const std::string problem = SurfaceCardinalityProblemJa(request.method,
            cardinality.role, IndicesWithRole(request, cardinality.role).size(),
            ChainRoleLabelJa(request.method, cardinality.role));
        if (!problem.empty()) {
            errors.push_back(MakeError(kBadInput, problem, {}));
        }
    }
    const std::vector<std::size_t> sides = IndicesWithRole(request, ChainRole::BoundarySide);
    for (const std::size_t index : sides) {
        if (request.chains[index].closed) {
            errors.push_back(MakeError(kBadInput, "四辺面の辺に閉じた線は使えません。",
                ChainLabel(request.method, request.chains[index])));
        }
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    const double joinTolerance = JoinToleranceMm(tolerance);
    GuideSurfaceAnalysis analysis;
    analysis.method = GuideSurfaceMethod::FourEdgePatch;
    errors = CheckBranching(request, sampled, sides, joinTolerance);
    if (errors.empty()) {
        errors = OrderRing(request, sampled, sides, joinTolerance, analysis.fourEdge);
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    analysis.sectionOrdering.chainIndices = analysis.fourEdge.sides;
    analysis.fourEdge.hasInteriorConstraints =
        !IndicesWithRole(request, ChainRole::GuideU).empty();
    bool continuity = false;
    for (const std::size_t index : sides) {
        continuity = continuity || request.chains[index].continuity != SurfaceContinuity::G0;
    }
    // 4 辺の面(GeomFill)は隣の面との滑らかさを指定できない。G1/G2 か通る線があれば、
    // 4 辺の面を初めの形にして、4 辺を境界に張り直す(MakeFilling)。
    analysis.fourEdge.refill = analysis.fourEdge.hasInteriorConstraints || continuity;
    if (analysis.fourEdge.hasInteriorConstraints) {
        analysis.notes.push_back(MakeWarning("GEO-G108",
            "内側の通る線があるので、4 辺から張った面を通る線へ寄せて張り直します(近似拘束)。",
            "出来た面は、4 辺と全部の通る線からの外れを測り、外れすぎていれば採用しません。"));
    }
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

} // namespace kachakacha::v2::modeling::detail
