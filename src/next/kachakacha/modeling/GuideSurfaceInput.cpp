#include "kachakacha/modeling/GuideSurfaceInput.h"

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

constexpr const char* kNonPlanar = "GEO-G001";
constexpr const char* kMixedOpenClosed = "GEO-G002";
constexpr const char* kSectionOrder = "GEO-G003";
constexpr const char* kNotConnected = "GEO-G004";
constexpr const char* kCrossingMissing = "GEO-G005";
constexpr const char* kCrossingOrder = "GEO-G006";
constexpr const char* kSelfIntersection = "GEO-G007";
constexpr const char* kFitExceeded = "GEO-G008";
//! 入力の数や種類がそもそも足りない。上の8つはどれも「幾何が悪い」話なので分ける。
constexpr const char* kBadInput = "GEO-G009";

//! 検査用の点列を作るときの粗さ。細かすぎると遅く、粗いと交差を見逃す。
[[nodiscard]] double SamplingToleranceMm(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
}

struct SampledChain {
    std::size_t chainIndex = 0;
    const GuideChain* chain = nullptr;
    std::vector<Vector3> points;
    std::vector<double> parameters;   //!< 正規化弧長
    Vector3 centroid{};
    double lengthMm = 0.0;
};

[[nodiscard]] std::vector<SampledChain> SampleAll(const GuideSurfaceRequest& request,
    double toleranceMm)
{
    std::vector<SampledChain> sampled;
    sampled.reserve(request.chains.size());
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        SampledChain item;
        item.chainIndex = index;
        item.chain = &request.chains[index];
        item.points = geometry::SampleChain(request.chains[index].segments, toleranceMm);
        if (request.chains[index].closed) {
            // 閉じた鎖は最後の点が最初と同じ。多角形として扱う前に落とす。
            geometry::RemoveClosingDuplicate(item.points, toleranceMm * 0.5);
        }
        item.parameters = geometry::NormalizedArcLength(item.points);
        item.centroid = geometry::Centroid(item.points);
        for (std::size_t at = 1; at < item.points.size(); ++at) {
            item.lengthMm += (item.points[at] - item.points[at - 1]).Length();
        }
        sampled.push_back(std::move(item));
    }
    return sampled;
}

[[nodiscard]] std::vector<std::size_t> IndicesWithRole(const GuideSurfaceRequest& request,
    ChainRole role)
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        if (request.chains[index].role == role) {
            indices.push_back(index);
        }
    }
    return indices;
}

[[nodiscard]] std::string ChainLabel(const GuideChain& chain)
{
    return std::string(ChainRoleName(chain.role)) + " #" + std::to_string(chain.index);
}

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

//! 断面が全部openか全部closedかを見る。混ざっていたら GEO-G002。
[[nodiscard]] std::vector<Diagnostic> CheckSectionOpenClosed(
    const GuideSurfaceRequest& request, const std::vector<std::size_t>& sections)
{
    std::vector<Diagnostic> errors;
    if (sections.empty()) {
        return errors;
    }
    const bool first = request.chains[sections.front()].closed;
    for (const std::size_t index : sections) {
        if (request.chains[index].closed != first) {
            errors.push_back(MakeError(kMixedOpenClosed,
                "開いた断面と閉じた断面が混ざっています。",
                ChainLabel(request.chains[sections.front()]) + " と "
                    + ChainLabel(request.chains[index])));
            break;
        }
    }
    return errors;
}

//! 断面の重心を主成分軸へ落として並べる(§6.4)。同値ならID順。
[[nodiscard]] SectionOrdering OrderSections(const std::vector<SampledChain>& sampled,
    const std::vector<std::size_t>& sections)
{
    SectionOrdering ordering;
    std::vector<Vector3> centroids;
    for (const std::size_t index : sections) {
        centroids.push_back(sampled[index].centroid);
    }
    // 主成分軸 = 重心の散らばりが最大になる向き。べき乗法を数回だけ回す。
    const Vector3 mean = geometry::Centroid(centroids);
    double xx = 0, xy = 0, xz = 0, yy = 0, yz = 0, zz = 0;
    for (const Vector3& point : centroids) {
        const Vector3 d = point - mean;
        xx += d.x * d.x; xy += d.x * d.y; xz += d.x * d.z;
        yy += d.y * d.y; yz += d.y * d.z; zz += d.z * d.z;
    }
    Vector3 axis{1.0, 0.0, 0.0};
    if (centroids.size() >= 2) {
        Vector3 current{1.0, 1.0, 1.0};
        for (int iteration = 0; iteration < 64; ++iteration) {
            const Vector3 next{xx * current.x + xy * current.y + xz * current.z,
                xy * current.x + yy * current.y + yz * current.z,
                xz * current.x + yz * current.y + zz * current.z};
            const double length = next.Length();
            if (!(length > 0.0)) {
                break;
            }
            current = next * (1.0 / length);
        }
        if (current.Length() > 0.0) {
            axis = current;
        }
    }
    // 向きは決定的にする。最大成分が負なら反転する。
    const double ax = std::abs(axis.x), ay = std::abs(axis.y), az = std::abs(axis.z);
    const double largest = std::max({ax, ay, az});
    if ((largest == ax && axis.x < 0.0) || (largest == ay && axis.y < 0.0)
        || (largest == az && axis.z < 0.0)) {
        axis = -axis;
    }
    ordering.axisDirection = axis;

    std::vector<std::pair<double, std::size_t>> keyed;
    for (const std::size_t index : sections) {
        keyed.emplace_back(Dot(sampled[index].centroid - mean, axis), index);
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& l, const auto& r) {
        if (l.first != r.first) {
            return l.first < r.first;
        }
        return l.second < r.second;   // 同値ならID順(ここでは入力順)
    });
    for (const auto& item : keyed) {
        ordering.chainIndices.push_back(item.second);
    }
    return ordering;
}

//! 隣り合う断面が離れているか。全域で許容差以下なら退化(§6.3)。
[[nodiscard]] bool SectionsAreDistinct(const SampledChain& first, const SampledChain& second,
    double toleranceMm)
{
    double largest = 0.0;
    const std::size_t steps = 32;
    for (std::size_t step = 0; step <= steps; ++step) {
        const double t = static_cast<double>(step) / static_cast<double>(steps);
        const Vector3 a =
            geometry::PointAtNormalizedArcLength(first.points, first.parameters, t);
        const Vector3 b =
            geometry::PointAtNormalizedArcLength(second.points, second.parameters, t);
        largest = std::max(largest, (a - b).Length());
    }
    return largest > toleranceMm;
}

//! open断面の向きを揃える。反転したほうが端点どうし近ければ、そちらを使う。
//! (§6.3「ねじれが少ない向きを既定にする」)
[[nodiscard]] bool ShouldReverseAgainst(const SampledChain& reference,
    const SampledChain& candidate)
{
    if (reference.points.empty() || candidate.points.empty()) {
        return false;
    }
    const double straight = (reference.points.front() - candidate.points.front()).Length()
        + (reference.points.back() - candidate.points.back()).Length();
    const double flipped = (reference.points.front() - candidate.points.back()).Length()
        + (reference.points.back() - candidate.points.front()).Length();
    return flipped < straight;
}

// ---------------------------------------------------------------- 交差の検出

//! 2本の鎖が最も近づく場所。線分どうしで測る。
//! 点どうしで測ると、点の間で交差している線を「離れている」と誤判定する。
[[nodiscard]] ChainCrossing FindClosestApproach(const SampledChain& first,
    const SampledChain& second)
{
    ChainCrossing crossing;
    crossing.firstChainIndex = first.chainIndex;
    crossing.secondChainIndex = second.chainIndex;
    const geometry::PolylineApproach approach =
        geometry::ClosestApproachBetween(first.points, second.points);
    if (!approach.valid) {
        crossing.distanceMm = -1.0;
        return crossing;
    }
    crossing.distanceMm = approach.distanceMm;
    crossing.firstParameter = approach.firstParameter;
    crossing.secondParameter = approach.secondParameter;
    crossing.position = (approach.firstPoint + approach.secondPoint) * 0.5;
    return crossing;
}

//! 片方の鎖を辿りながら、相手までの距離が許容差の内側へ入る回数を数える。
[[nodiscard]] int CountApproachesAlong(const std::vector<Vector3>& along,
    const std::vector<Vector3>& across, double toleranceMm)
{
    int count = 0;
    bool inside = false;
    for (std::size_t a = 0; a + 1 < along.size(); ++a) {
        const std::vector<Vector3> edge{along[a], along[a + 1]};
        const geometry::PolylineApproach approach =
            geometry::ClosestApproachBetween(edge, across);
        const bool near = approach.valid && approach.distanceMm <= toleranceMm;
        if (near && !inside) {
            ++count;
        }
        inside = near;
    }
    return count;
}

//! 許容差内で近づく区間がいくつあるか。2箇所以上なら「2重交差」。
//!
//! 片側だけを辿ると数え落とす。長い1本の線が相手を2回またぐとき、
//! その2回が同じ区間に入ってしまい、1回に見える。
//! 相手の側から辿れば、離れている区間が間に挟まるので2回だと分かる。
//! そこで両方から数えて、多いほうを採る。見逃すより多めに疑うほうが安全である。
[[nodiscard]] int CountApproaches(const SampledChain& first, const SampledChain& second,
    double toleranceMm)
{
    return std::max(CountApproachesAlong(first.points, second.points, toleranceMm),
        CountApproachesAlong(second.points, first.points, toleranceMm));
}

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
    analysis.sectionOrdering = OrderSections(sampled, sections);

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

// ---------------------------------------------------------------- GuidedLoft

[[nodiscard]] Result<GuideSurfaceAnalysis> AnalyzeGuidedLoft(
    const GuideSurfaceRequest& request, const GeometryTolerance& tolerance,
    std::vector<SampledChain>& sampled)
{
    const std::vector<std::size_t> guides = IndicesWithRole(request, ChainRole::GuideU);
    const std::vector<std::size_t> sections = IndicesWithRole(request, ChainRole::Section);
    std::vector<Diagnostic> errors;
    if (guides.size() != 2) {
        errors.push_back(MakeError(kBadInput, "外形ガイドはちょうど2本必要です。",
            "実際 " + std::to_string(guides.size()) + " 本。"));
    }
    if (sections.empty()) {
        errors.push_back(MakeError(kBadInput, "断面が1本もありません。", {}));
    }
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        const ChainRole role = request.chains[index].role;
        if (role != ChainRole::GuideU && role != ChainRole::Section) {
            errors.push_back(MakeError(kBadInput, "ガイドと断面以外が混ざっています。",
                ChainLabel(request.chains[index])));
        }
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // 各断面の両端が、それぞれのガイドへ接していること(§6.5)。
    // Segment内部で接する場合も許す。端点どうしに限らない。
    const double joinTolerance = std::max(tolerance.interactiveJoinMm,
        tolerance.modelLinearMm * 100.0);
    GuideSurfaceAnalysis analysis;
    analysis.method = GuideSurfaceMethod::GuidedLoft;

    for (const std::size_t sectionIndex : sections) {
        const SampledChain& section = sampled[sectionIndex];
        if (request.chains[sectionIndex].closed) {
            errors.push_back(MakeError(kBadInput,
                "外形ガイドを使う面では、断面は開いていなければなりません。",
                ChainLabel(request.chains[sectionIndex])));
            continue;
        }
        for (std::size_t at = 0; at < guides.size(); ++at) {
            const SampledChain& guide = sampled[guides[at]];
            const Vector3 end = at == 0 ? section.points.front() : section.points.back();
            const double distance =
                geometry::MinimumDistanceBetween(std::vector<Vector3>{end}, guide.points);
            if (distance > joinTolerance) {
                errors.push_back(MakeError(kNotConnected,
                    "断面の端がガイドへつながっていません。",
                    ChainLabel(request.chains[sectionIndex]) + " の"
                        + (at == 0 ? "始点" : "終点") + " と "
                        + ChainLabel(request.chains[guides[at]]) + " の距離 "
                        + std::to_string(distance) + " mm(許容 "
                        + std::to_string(joinTolerance) + " mm)。"));
                continue;
            }
            const geometry::PolylineApproach approach =
                geometry::ClosestApproachBetween(std::vector<Vector3>{end}, guide.points);
            ChainCrossing crossing;
            crossing.firstChainIndex = sectionIndex;
            crossing.secondChainIndex = guides[at];
            crossing.distanceMm = approach.distanceMm;
            // 断面側は端点なので、始点なら0、終点なら1。
            crossing.firstParameter = at == 0 ? 0.0 : 1.0;
            crossing.secondParameter = approach.secondParameter;
            crossing.position = approach.secondPoint;
            analysis.crossings.push_back(crossing);
        }
    }
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }

    // 断面がガイドを横切る順が、2本のガイドで一致していること(§6.5)。
    // 一致しなければ、面はどこかでねじれる。
    std::vector<std::pair<double, std::size_t>> firstOrder;
    std::vector<std::pair<double, std::size_t>> secondOrder;
    for (const ChainCrossing& crossing : analysis.crossings) {
        if (crossing.secondChainIndex == guides[0]) {
            firstOrder.emplace_back(crossing.secondParameter, crossing.firstChainIndex);
        } else {
            secondOrder.emplace_back(crossing.secondParameter, crossing.firstChainIndex);
        }
    }
    std::sort(firstOrder.begin(), firstOrder.end());
    std::sort(secondOrder.begin(), secondOrder.end());
    if (firstOrder.size() != secondOrder.size()) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kNotConnected,
            "2本のガイドで、つながっている断面の数が違います。",
            std::to_string(firstOrder.size()) + " と " + std::to_string(secondOrder.size())));
    }
    for (std::size_t at = 0; at < firstOrder.size(); ++at) {
        if (firstOrder[at].second != secondOrder[at].second) {
            return Result<GuideSurfaceAnalysis>::Failure(MakeError(kCrossingOrder,
                "断面がガイドを横切る順番が、2本のガイドで食い違っています。",
                "この順のままでは面がねじれます。断面の向きか対応を見直してください。"));
        }
    }

    // 端に断面が無ければ、仮想断面を作る位置を決める(§6.5、既定は作る)。
    if (request.createVirtualEndSections && !firstOrder.empty()) {
        const double startGap = firstOrder.front().first;
        const double endGap = 1.0 - firstOrder.back().first;
        const double edgeTolerance = 1.0e-3;
        if (startGap > edgeTolerance) {
            analysis.virtualSectionParameters.push_back(0.0);
        }
        if (endGap > edgeTolerance) {
            analysis.virtualSectionParameters.push_back(1.0);
        }
        if (!analysis.virtualSectionParameters.empty()) {
            analysis.notes.push_back(MakeWarning("GEO-G104",
                "端に断面が無いので、仮想断面を作ります。",
                std::to_string(analysis.virtualSectionParameters.size())
                    + " 本。設定で作らないようにもできます。"));
        }
    }

    analysis.sectionOrdering.chainIndices.clear();
    for (const auto& item : firstOrder) {
        analysis.sectionOrdering.chainIndices.push_back(item.second);
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
    for (std::size_t index = 0; index < request.chains.size(); ++index) {
        if (request.chains[index].role != ChainRole::BoundarySide) {
            errors.push_back(MakeError(kBadInput, "境界の辺以外が混ざっています。",
                ChainLabel(request.chains[index])));
        }
    }
    if (sides.size() < 3) {
        errors.push_back(MakeError(kBadInput, "境界の辺が足りません。",
            "3辺または4辺が必要です。実際 " + std::to_string(sides.size()) + " 辺。"));
    } else if (sides.size() > 4) {
        // 勝手に三角分割しない(§6.7)。分ける案を示して断る。
        errors.push_back(MakeError(kBadInput, "5辺以上の境界は、このまま面にできません。",
            std::to_string(sides.size())
                + " 辺あります。3辺か4辺へ分けるか、曲線網(GordonNetwork)を使ってください。"));
    }
    if (!request.tangentContinuity.empty()
        && request.tangentContinuity.size() != sides.size()) {
        errors.push_back(MakeError(kBadInput, "辺の数と、連続条件の数が合いません。",
            std::to_string(sides.size()) + " 辺 / "
                + std::to_string(request.tangentContinuity.size()) + " 個の条件。"));
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
    const double closingGap = (tail - sampled[sides.front()].points.front()).Length();
    if (closingGap > joinTolerance) {
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
        return AnalyzeSections(request, tolerance, sampled, 2, 2);
    case GuideSurfaceMethod::LoftSections:
        return AnalyzeSections(request, tolerance, sampled, 3, 1000);
    case GuideSurfaceMethod::GuidedLoft:
        return AnalyzeGuidedLoft(request, tolerance, sampled);
    case GuideSurfaceMethod::GordonNetwork:
        return AnalyzeGordon(request, tolerance, sampled);
    case GuideSurfaceMethod::BoundaryFill:
        return AnalyzeBoundaryFill(request, tolerance, sampled);
    case GuideSurfaceMethod::OffsetGuide:
        return AnalyzeOffset(request, tolerance);
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

} // namespace kachakacha::v2::modeling
