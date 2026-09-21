#include "kachakacha/modeling/GuideSurfaceInput.h"

#include "kachakacha/modeling/FourEdgeInput.h"
#include "kachakacha/modeling/GuideSurfaceSampling.h"
#include "kachakacha/modeling/LoftInput.h"
#include "kachakacha/modeling/SurfaceCardinality.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kachakacha::v2::modeling {

using base::MakeError;
using base::MakeWarning;
using base::Result;
using geometry::PlanarFrame;
using geometry::PlaneFit;
using geometry::Point2;

namespace {

using namespace detail;   // 検査の道具(GuideSurfaceSampling.h)

//! 役割ごとに index が1始まりで重複していないこと。
[[nodiscard]] std::vector<Diagnostic> CheckIndices(const GuideSurfaceRequest& request)
{
    std::vector<Diagnostic> errors;
    std::map<ChainRole, std::vector<int>> seen;
    for (const GuideChain& chain : request.chains) {
        // SourceSurface は「既にある形状ガイドを指す」入力であって、曲線ではない。
        // 線の中身が無いことを理由に断らない(面をずらす操作で使う)。
        if (chain.segments.empty() && chain.role != ChainRole::SourceSurface) {
            errors.push_back(MakeError(kBadInput, "中身の無い線が入力にあります。",
                ChainLabel(chain)));
        }
        if (chain.index < 1) {
            errors.push_back(MakeError(kBadInput, "入力の番号は1から始まります。",
                ChainLabel(chain)));
        }
        std::vector<int>& list = seen[chain.role];
        if (std::find(list.begin(), list.end(), chain.index) != list.end()) {
            errors.push_back(MakeError(kBadInput, "同じ役割で番号が重なっています。",
                ChainLabel(chain)));
        }
        list.push_back(chain.index);
    }
    return errors;
}

//! 連続条件の検査。**線だけでは成り立たない条件を、成り立つふりをしない。**
[[nodiscard]] std::vector<Diagnostic> CheckContinuity(const GuideSurfaceRequest& request)
{
    std::vector<Diagnostic> errors;
    const bool methodTakes = request.method == GuideSurfaceMethod::BoundaryFill
        || request.method == GuideSurfaceMethod::FourEdgePatch;
    for (const GuideChain& chain : request.chains) {
        if (chain.continuity == SurfaceContinuity::G0) {
            continue;
        }
        const std::string order(SurfaceContinuityName(chain.continuity));
        if (!methodTakes) {
            errors.push_back(MakeError(kBadInput,
                "この作り方では、縁の連続条件(" + order + ")を指定できません。",
                ChainLabel(request.method, chain) + "。連続条件は境界面と四辺面で指定できます。"));
            continue;
        }
        if (chain.role != ChainRole::BoundarySide) {
            errors.push_back(MakeError(kBadInput, "連続条件は境界の辺にだけ指定できます。",
                ChainLabel(request.method, chain) + " は境界ではありません。"));
            continue;
        }
        if (chain.supportSurfaceId.IsNil()) {
            errors.push_back(MakeError(kBadInput,
                order + "を指定しましたが、" + ChainLabel(request.method, chain)
                    + "には隣接する面(支持面)がありません。",
                order + " は隣の面に対して滑らかにする条件なので、隣の面が要ります。"
                        "隣の面を支持面に選ぶか、G0 にしてください。"));
        }
    }
    return errors;
}

// ---------------------------------------------------------------- PlanarBoundary

[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzePlanar(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::vector<SampledChain>& sampled)
{
    std::vector<Diagnostic> errors;
    std::vector<std::size_t> loops;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        const GuideChain& chain = request.chains[index];
        if (chain.role != ChainRole::OuterBoundary && chain.role != ChainRole::HoleBoundary) {
            errors.push_back(MakeError(kBadInput,
                "平面の面には、閉じた輪郭だけを渡します。", ChainLabel(chain)));
            continue;
        }
        if (!chain.closed) {
            errors.push_back(MakeError(kBadInput, "輪郭が閉じていません。",
                ChainLabel(chain)));
            continue;
        }
        loops.push_back(index);
    }
    if (loops.empty() && errors.empty()) {
        errors.push_back(MakeError(kBadInput, "輪郭が1つも渡されていません。", {}));
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // 全輪郭をまとめて1枚の平面へ載せる。役割の指定ではなく、実際の座標で見る。
    std::vector<Vector3> all;
    for (const std::size_t index : loops) {
        const std::vector<Vector3>& points = sampled[index].points;
        all.insert(all.end(), points.begin(), points.end());
    }
    const PlaneFit fit = geometry::FitPlane(all);
    if (!fit.valid) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kNonPlanar,
            "平面を決められません。", "輪郭が1直線上に並んでいるか、点が少なすぎます。"));
    }
    if (fit.maximumDeviationMm > tolerance.modelLinearMm) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kNonPlanar,
            "輪郭が同じ平面に載っていません。",
            "最大のずれ " + std::to_string(fit.maximumDeviationMm) + " mm。許容差は "
                + std::to_string(tolerance.modelLinearMm) + " mm。"));
    }

    const PlanarFrame frame = geometry::MakeFrame(fit);
    std::vector<std::vector<Point2>> flat(loops.size());
    for (std::size_t at = 0; at < loops.size(); ++at) {
        flat[at] = geometry::ProjectToFrame(sampled[loops[at]].points, frame);
    }

    const double planarTolerance = SamplingToleranceMm(tolerance);
    for (std::size_t at = 0; at < loops.size(); ++at) {
        if (geometry::HasSelfIntersection(flat[at], planarTolerance)) {
            errors.push_back(MakeError(kSelfIntersection, "輪郭が自分自身と交わっています。",
                ChainLabel(request.chains[loops[at]])));
        }
    }
    for (std::size_t a = 0; a < loops.size(); ++a) {
        for (std::size_t b = a + 1; b < loops.size(); ++b) {
            if (geometry::LoopsIntersect(flat[a], flat[b], planarTolerance)) {
                errors.push_back(MakeError(kSelfIntersection, "輪郭どうしが交わっています。",
                    ChainLabel(request.chains[loops[a]]) + " と "
                        + ChainLabel(request.chains[loops[b]])));
            }
        }
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // 内外は、利用者が付けた役割ではなく包含関係で決める(§6.2)。
    // 「他の輪郭にいくつ含まれているか」が奇数なら穴。
    std::vector<int> depth(loops.size(), 0);
    for (std::size_t a = 0; a < loops.size(); ++a) {
        if (flat[a].empty()) {
            continue;
        }
        for (std::size_t b = 0; b < loops.size(); ++b) {
            if (a == b) {
                continue;
            }
            if (geometry::ContainsPoint(flat[b], flat[a].front())) {
                ++depth[a];
            }
        }
    }

    GuideSurfaceAnalysis analysis;
    analysis.method = GuideSurfaceMethod::PlanarBoundary;
    analysis.planeFit = fit;
    analysis.planarLoops.resize(loops.size());
    for (std::size_t at = 0; at < loops.size(); ++at) {
        analysis.planarLoops[at].chainIndex = loops[at];
        analysis.planarLoops[at].isHole = (depth[at] % 2) == 1;
    }
    // 穴を、いちばん内側の外周へ結びつける。
    for (std::size_t hole = 0; hole < loops.size(); ++hole) {
        if (!analysis.planarLoops[hole].isHole) {
            continue;
        }
        std::size_t best = loops.size();
        int bestDepth = -1;
        for (std::size_t outer = 0; outer < loops.size(); ++outer) {
            if (outer == hole || analysis.planarLoops[outer].isHole) {
                continue;
            }
            if (!geometry::ContainsPoint(flat[outer], flat[hole].front())) {
                continue;
            }
            if (depth[outer] > bestDepth) {
                bestDepth = depth[outer];
                best = outer;
            }
        }
        if (best == loops.size()) {
            return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
                "どの外周にも属さない穴があります。",
                ChainLabel(request.chains[loops[hole]])));
        }
        analysis.planarLoops[best].holes.push_back(loops[hole]);
    }
    // 利用者の付けた役割と食い違ったら、直したことを伝える(黙って直さない)。
    for (std::size_t at = 0; at < loops.size(); ++at) {
        const GuideChain& chain = request.chains[loops[at]];
        const bool declaredHole = chain.role == ChainRole::HoleBoundary;
        if (declaredHole != analysis.planarLoops[at].isHole) {
            analysis.notes.push_back(MakeWarning("GEO-G101",
                "輪郭の内外を、実際の位置関係から決め直しました。",
                ChainLabel(chain) + " は "
                    + (analysis.planarLoops[at].isHole ? "穴" : "外周") + " として扱います。"));
        }
    }
    // 非接触の外周が複数あれば、それぞれ別の面候補になる(§6.2)。
    const std::size_t outerCount = static_cast<std::size_t>(
        std::count_if(analysis.planarLoops.begin(), analysis.planarLoops.end(),
            [](const PlanarLoopClassification& loop) { return !loop.isHole; }));
    if (outerCount == 0) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
            "外周がありません。", "穴だけでは面を作れません。"));
    }
    if (outerCount > 1) {
        analysis.notes.push_back(MakeWarning("GEO-G102",
            "離れた外周が複数あります。それぞれ別の面になります。",
            std::to_string(outerCount) + " 個"));
    }
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

// ---------------------------------------------------------------- 断面の共通検査

// ---------------------------------------------------------------- Ruled / Loft

[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzeSections(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::vector<SampledChain>& sampled,
    std::size_t minimumSections, std::size_t maximumSections)
{
    const std::vector<std::size_t> sections = IndicesWithRole(request, ChainRole::Section);
    std::vector<Diagnostic> errors;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        if (request.chains[index].role != ChainRole::Section) {
            errors.push_back(MakeError(kBadInput, "断面以外の入力が混ざっています。",
                ChainLabel(request.chains[index])));
        }
    }
    if (sections.size() < minimumSections) {
        errors.push_back(MakeError(kBadInput, "断面の数が足りません。",
            "必要 " + std::to_string(minimumSections) + " 本、実際 "
                + std::to_string(sections.size()) + " 本。"));
    }
    if (sections.size() > maximumSections) {
        errors.push_back(MakeError(kBadInput, "この方法で扱える断面の数を超えています。",
            "上限 " + std::to_string(maximumSections) + " 本、実際 "
                + std::to_string(sections.size()) + " 本。"));
    }
    std::vector<Diagnostic> mixed = CheckSectionOpenClosed(request, sections);
    errors.insert(errors.end(), mixed.begin(), mixed.end());
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    GuideSurfaceAnalysis analysis;
    analysis.method = request.method;
    if (request.keepSectionOrder) {
        // 手動固定。**渡された順をそのまま使う。**並べ替えない。
        // 人が画面で決めた順が、そのまま生成順になる。
        analysis.sectionOrdering.chainIndices = sections;
    } else {
        analysis.sectionOrdering = OrderSections(sampled, sections);
    }

    // 隣り合う断面が同じ位置なら退化。面にならない。
    const std::vector<std::size_t>& order = analysis.sectionOrdering.chainIndices;
    for (std::size_t at = 1; at < order.size(); ++at) {
        if (!SectionsAreDistinct(sampled[order[at - 1]], sampled[order[at]],
                tolerance.modelLinearMm)) {
            return Result<GuideSurfaceAnalysis>::Failure(MakeError(kSectionOrder,
                "断面どうしが重なっていて、面になりません。",
                ChainLabel(request.chains[order[at - 1]]) + " と "
                    + ChainLabel(request.chains[order[at]])));
        }
    }
    // open断面は向きの取り違えでねじれる。反転したほうが近いものは知らせる。
    if (!request.chains[order.front()].closed) {
        for (std::size_t at = 1; at < order.size(); ++at) {
            if (ShouldReverseAgainst(sampled[order[at - 1]], sampled[order[at]])) {
                analysis.notes.push_back(MakeWarning("GEO-G103",
                    "断面の向きを揃え直しました。", ChainLabel(request.chains[order[at]])));
            }
        }
    }
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

// ---------------------------------------------------------------- GordonNetwork

[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzeGordon(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::vector<SampledChain>& sampled)
{
    const std::vector<std::size_t> uChains = IndicesWithRole(request, ChainRole::GuideU);
    const std::vector<std::size_t> vChains = IndicesWithRole(request, ChainRole::GuideV);
    std::vector<Diagnostic> errors;
    if (uChains.size() < 2) {
        errors.push_back(MakeError(kBadInput, "U方向の線が2本以上必要です。",
            "実際 " + std::to_string(uChains.size()) + " 本。"));
    }
    if (vChains.size() < 2) {
        errors.push_back(MakeError(kBadInput, "V方向の線が2本以上必要です。",
            "実際 " + std::to_string(vChains.size()) + " 本。"));
    }
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        const ChainRole role = request.chains[index].role;
        if (role != ChainRole::GuideU && role != ChainRole::GuideV) {
            errors.push_back(MakeError(kBadInput, "U方向・V方向以外が混ざっています。",
                ChainLabel(request.chains[index])));
        }
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    const double joinTolerance = std::max(tolerance.interactiveJoinMm,
        tolerance.modelLinearMm * 100.0);
    GuideSurfaceAnalysis analysis;
    analysis.method = GuideSurfaceMethod::GordonNetwork;

    // 全U×全Vが、ちょうど1回ずつ交わること(§6.6)。
    for (const std::size_t u : uChains) {
        for (const std::size_t v : vChains) {
            const ChainCrossing crossing = FindClosestApproach(sampled[u], sampled[v]);
            if (crossing.distanceMm > joinTolerance) {
                errors.push_back(MakeError(kCrossingMissing, "交わっていない線があります。",
                    ChainLabel(request.chains[u]) + " と " + ChainLabel(request.chains[v])
                        + " の最短距離 " + std::to_string(crossing.distanceMm)
                        + " mm(許容 " + std::to_string(joinTolerance) + " mm)。"));
                continue;
            }
            const int approaches = CountApproaches(sampled[u], sampled[v], joinTolerance);
            if (approaches > 1) {
                errors.push_back(MakeError(kCrossingMissing, "2回以上交わっている線があります。",
                    ChainLabel(request.chains[u]) + " と " + ChainLabel(request.chains[v])
                        + " が " + std::to_string(approaches) + " 箇所で交わっています。"));
                continue;
            }
            analysis.crossings.push_back(crossing);
        }
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // 交差の順が、すべての線で同じ向きに並んでいること(§6.6)。
    // U鎖ごとに「V鎖と交わる位置」を並べ、その並びが全U鎖で一致すること。
    const auto orderAlong = [&](const std::vector<std::size_t>& along,
                                const std::vector<std::size_t>& across, bool alongIsU) {
        std::vector<std::vector<std::size_t>> orders;
        for (const std::size_t a : along) {
            std::vector<std::pair<double, std::size_t>> keyed;
            for (const ChainCrossing& crossing : analysis.crossings) {
                const std::size_t self = alongIsU ? crossing.firstChainIndex
                                                  : crossing.secondChainIndex;
                const std::size_t other = alongIsU ? crossing.secondChainIndex
                                                   : crossing.firstChainIndex;
                const double parameter = alongIsU ? crossing.firstParameter
                                                  : crossing.secondParameter;
                if (self == a) {
                    keyed.emplace_back(parameter, other);
                }
            }
            std::sort(keyed.begin(), keyed.end());
            std::vector<std::size_t> order;
            for (const auto& item : keyed) {
                order.push_back(item.second);
            }
            orders.push_back(std::move(order));
        }
        (void)across;
        return orders;
    };

    const std::vector<std::vector<std::size_t>> uOrders = orderAlong(uChains, vChains, true);
    for (std::size_t at = 1; at < uOrders.size(); ++at) {
        if (uOrders[at] != uOrders[0]) {
            return Result<GuideSurfaceAnalysis>::Failure(MakeError(kCrossingOrder,
                "V方向の線と交わる順番が、U方向の線どうしで食い違っています。",
                ChainLabel(request.chains[uChains[0]]) + " と "
                    + ChainLabel(request.chains[uChains[at]])
                    + "。網が捻れているので、このままでは面になりません。"));
        }
    }
    const std::vector<std::vector<std::size_t>> vOrders = orderAlong(vChains, uChains, false);
    for (std::size_t at = 1; at < vOrders.size(); ++at) {
        if (vOrders[at] != vOrders[0]) {
            return Result<GuideSurfaceAnalysis>::Failure(MakeError(kCrossingOrder,
                "U方向の線と交わる順番が、V方向の線どうしで食い違っています。",
                ChainLabel(request.chains[vChains[0]]) + " と "
                    + ChainLabel(request.chains[vChains[at]])));
        }
    }
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

// ---------------------------------------------------------------- BoundaryFill

[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzeBoundaryFill(
    const GuideSurfaceRequest& request, const GeometryTolerance& tolerance,
    std::vector<SampledChain>& sampled)
{
    const std::vector<std::size_t> sides = IndicesWithRole(request, ChainRole::BoundarySide);
    std::vector<Diagnostic> errors;
    std::size_t edgeCount = 0;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        if (request.chains[index].role == ChainRole::GuideU) {
            continue;   // 面が通るだけの線。輪には入れず、面を作るときに拘束する
        }
        if (request.chains[index].role != ChainRole::BoundarySide) {
            errors.push_back(MakeError(kBadInput, "境界の辺以外が混ざっています。",
                ChainLabel(request.chains[index])));
        } else {
            edgeCount += request.chains[index].segments.size();
        }
    }
    if (edgeCount < 3) {
        errors.push_back(MakeError(kBadInput, "境界の辺が足りません。",
            "閉じるには3辺以上が必要です。実際 " + std::to_string(edgeCount) + " 辺。"));
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // 辺が輪になって閉じていること。端点どうしを総当たりで繋ぐ。
    const double joinTolerance = std::max(tolerance.interactiveJoinMm,
        tolerance.modelLinearMm * 100.0);
    std::vector<std::size_t> remaining(sides.begin() + 1, sides.end());
    std::vector<std::size_t> ring{sides.front()};
    Vector3 tail = sampled[sides.front()].points.back();
    while (!remaining.empty()) {
        bool joined = false;
        for (std::size_t at = 0; at < remaining.size(); ++at) {
            const SampledChain& candidate = sampled[remaining[at]];
            const double toStart = (tail - candidate.points.front()).Length();
            const double toEnd = (tail - candidate.points.back()).Length();
            if (std::min(toStart, toEnd) <= joinTolerance) {
                tail = toStart <= toEnd ? candidate.points.back() : candidate.points.front();
                ring.push_back(remaining[at]);
                remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(at));
                joined = true;
                break;
            }
        }
        if (!joined) {
            return Result<GuideSurfaceAnalysis>::Failure(MakeError(kNotConnected,
                "境界の辺がつながっていません。",
                "残り " + std::to_string(remaining.size()) + " 辺が輪に入りませんでした。"));
        }
    }
    // SampleAll removes the duplicate closing point from a declared closed chain.
    // Its sampled tail therefore need not equal its sampled head even though the
    // underlying ordered curve segments form a closed wire.
    const bool singleClosedChain = sides.size() == 1 && request.chains[sides.front()].closed;
    const double closingGap = (tail - sampled[sides.front()].points.front()).Length();
    if (!singleClosedChain && closingGap > joinTolerance) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kNotConnected,
            "境界が閉じていません。",
            "最後の隙間 " + std::to_string(closingGap) + " mm(許容 "
                + std::to_string(joinTolerance) + " mm)。"));
    }

    GuideSurfaceAnalysis analysis;
    analysis.method = GuideSurfaceMethod::BoundaryFill;
    analysis.sectionOrdering.chainIndices = ring;

    // 非平面なら、面が一意でないことを画面で言う(§6.7)。
    std::vector<Vector3> all;
    for (const std::size_t index : sides) {
        all.insert(all.end(), sampled[index].points.begin(), sampled[index].points.end());
    }
    const PlaneFit fit = geometry::FitPlane(all);
    analysis.planeFit = fit;
    if (fit.valid && fit.maximumDeviationMm > tolerance.modelLinearMm) {
        analysis.notes.push_back(MakeWarning("GEO-G105",
            "境界が平面に載っていないため、面の形は一通りに決まりません。",
            "最大のずれ " + std::to_string(fit.maximumDeviationMm)
                + " mm。連続条件を辺ごとに指定できます。"));
    }
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

// ---------------------------------------------------------------- OffsetGuide

[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzeOffset(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance)
{
    const std::vector<std::size_t> sources =
        IndicesWithRole(request, ChainRole::SourceSurface);
    std::vector<Diagnostic> errors;
    if (sources.size() != 1) {
        errors.push_back(MakeError(kBadInput, "元にする形状ガイドを1つ選んでください。",
            "実際 " + std::to_string(sources.size()) + " 個。"));
    }
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        if (request.chains[index].role != ChainRole::SourceSurface) {
            errors.push_back(MakeError(kBadInput, "元の形状ガイド以外が混ざっています。",
                ChainLabel(request.chains[index])));
        }
    }
    if (!geometry::IsFinite(request.offsetDistanceMm)) {
        errors.push_back(MakeError(kBadInput, "距離が数になっていません。", {}));
    } else if (std::abs(request.offsetDistanceMm) <= tolerance.modelLinearMm) {
        errors.push_back(MakeError(kBadInput, "距離が0です。",
            "0だと元の面と同じものができます。"));
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    GuideSurfaceAnalysis analysis;
    analysis.method = GuideSurfaceMethod::OffsetGuide;
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

//! 回転体: 断面 1 本(役割は断面)を、軸のまわりに角度だけ回す。
[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzeRevolve(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, const std::vector<SampledChain>& sampled)
{
    std::vector<Diagnostic> errors;
    const std::vector<std::size_t> sections = IndicesWithRole(request, ChainRole::Section);
    if (sections.size() != 1 || sections.size() != request.chains.size()) {
        errors.push_back(MakeError(kBadInput, "回転体の断面は 1 本にしてください。",
            "断面 " + std::to_string(sections.size()) + " 本、入力 "
                + std::to_string(request.chains.size()) + " 本。"));
    }
    const Vector3 axis = geometry::Normalized(request.revolveAxisDirection);
    if (axis == Vector3{} || !request.revolveAxisPoint.IsFinite()) {
        errors.push_back(MakeError(kBadInput, "回転体の軸の向きが決まりません。", {}));
    }
    const double fullTurn = 2.0 * 3.14159265358979323846;
    if (!geometry::IsFinite(request.revolveAngleRad) || !(request.revolveAngleRad > 0.0)
        || request.revolveAngleRad > fullTurn + 1.0e-9) {
        errors.push_back(MakeError(kBadInput,
            "回転体の角度は 0 より大きく 360 度以下にしてください。",
            std::to_string(request.revolveAngleRad * 180.0 / 3.14159265358979323846) + " 度"));
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    // 断面が軸の上に乗っていれば、回しても面にならない。
    double farthest = 0.0;
    for (const Vector3& point : sampled[sections.front()].points) {
        const Vector3 delta = point - request.revolveAxisPoint;
        farthest = std::max(farthest, (delta - axis * geometry::Dot(delta, axis)).Length());
    }
    if (farthest <= tolerance.modelLinearMm * 10.0) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
            "断面が軸の上にあります(回しても面になりません)。",
            "断面を軸から離すか、別の線を軸にしてください。"));
    }
    GuideSurfaceAnalysis analysis;
    analysis.method = GuideSurfaceMethod::Revolve;
    analysis.sectionOrdering.chainIndices = sections;
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
}

} // namespace

Result<GuideSurfaceAnalysis> AnalyzeGuideSurfaceRequest(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance)
{
    std::vector<Diagnostic> errors = CheckIndices(request);
    if (request.chains.empty()) {
        errors.push_back(MakeError(kBadInput, "入力がありません。", {}));
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // 連続条件(G1/G2)は、受けられる作り方・役割・支持面があるときだけ。
    errors = CheckContinuity(request);
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // OffsetGuide の入力は曲線ではなく、既にある形状ガイドへの参照である。
    // 線としての中身を持たないので、点列の検査にはかけない。
    if (request.method == GuideSurfaceMethod::OffsetGuide) {
        return AnalyzeOffset(request, tolerance);
    }

    std::vector<SampledChain> sampled = SampleAll(request, SamplingToleranceMm(tolerance));
    for (const SampledChain& item : sampled) {
        if (item.points.size() < 2) {
            return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
                "点が足りない線が入力にあります。", ChainLabel(*item.chain)));
        }
        for (const Vector3& point : item.points) {
            if (!point.IsFinite()) {
                return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
                    "座標に有限でない数が入っています。", ChainLabel(*item.chain)));
            }
        }
    }

    switch (request.method) {
    case GuideSurfaceMethod::PlanarBoundary:
        return AnalyzePlanar(request, tolerance, sampled);
    case GuideSurfaceMethod::RuledSections:
        // 1 枚のルールド面は 2 本で決まる。3 本以上は隣り合う 2 本ずつの帯を順につなぐ
        // (ThruSections の ruled がそのとおりに作る)。上限は付けない。
        return AnalyzeSections(request, tolerance, sampled, 2, kUnlimitedCount);
    case GuideSurfaceMethod::LoftSections:
    case GuideSurfaceMethod::GuidedLoft:
        // 断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1(LoftInput.cpp)。
        return AnalyzeLoft(request, tolerance, sampled);
    case GuideSurfaceMethod::GordonNetwork:
        return AnalyzeGordon(request, tolerance, sampled);
    case GuideSurfaceMethod::BoundaryFill:
        return AnalyzeBoundaryFill(request, tolerance, sampled);
    case GuideSurfaceMethod::OffsetGuide:
        return AnalyzeOffset(request, tolerance);
    case GuideSurfaceMethod::Revolve:
        return AnalyzeRevolve(request, tolerance, sampled);
    case GuideSurfaceMethod::FourEdgePatch:
        return AnalyzeFourEdgePatch(request, tolerance, sampled);
    }
    return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
        "知らない作り方です。", {}));
}

SurfaceFitCheck CheckSurfaceFit(const GuideSurfaceRequest& request,
    const std::vector<Vector3>& surfacePoints, const GeometryTolerance& tolerance)
{
    SurfaceFitCheck check;
    const double limit = tolerance.modelLinearMm * 10.0;
    const double samplingTolerance = SamplingToleranceMm(tolerance);
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        const std::vector<Vector3> points =
            geometry::SampleChain(request.chains[index].segments, samplingTolerance);
        const double deviation = geometry::MaximumDeviationTo(points, surfacePoints);
        if (deviation > check.maximumDeviationMm) {
            check.maximumDeviationMm = deviation;
            check.worstChainIndex = index;
        }
    }
    check.withinTolerance = check.maximumDeviationMm <= limit;
    return check;
}

std::vector<EntityId> AdoptedSectionSources(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis)
{
    std::vector<EntityId> sources;
    for (const std::size_t index : analysis.sectionOrdering.chainIndices) {
        if (index < request.chains.size()
            && request.chains[index].role == ChainRole::Section) {
            sources.push_back(request.chains[index].sourceEntityId);
        }
    }
    return sources;
}

} // namespace kachakacha::v2::modeling
