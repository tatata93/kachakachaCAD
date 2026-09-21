#include "kachakacha/modeling/GuideSurfaceSampling.h"

#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::modeling::detail {

using base::MakeError;

//! 検査用の点列を作るときの粗さ。細かすぎると遅く、粗いと交差を見逃す。
double SamplingToleranceMm(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
}

std::vector<SampledChain> SampleAll(const GuideSurfaceRequest& request,
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

std::vector<std::size_t> IndicesWithRole(const GuideSurfaceRequest& request,
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

std::string ChainLabel(const GuideChain& chain)
{
    return ChainRoleLabelJa(chain.role) + " " + std::to_string(chain.index);
}

std::string ChainLabel(GuideSurfaceMethod method, const GuideChain& chain)
{
    return ChainRoleLabelJa(method, chain.role) + " " + std::to_string(chain.index);
}

//! 断面が全部openか全部closedかを見る。混ざっていたら GEO-G002。
std::vector<Diagnostic> CheckSectionOpenClosed(
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
SectionOrdering OrderSections(const std::vector<SampledChain>& sampled,
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
bool SectionsAreDistinct(const SampledChain& first, const SampledChain& second,
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
bool ShouldReverseAgainst(const SampledChain& reference,
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
ChainCrossing FindClosestApproach(const SampledChain& first,
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
namespace {

int CountApproachesAlong(const std::vector<Vector3>& along,
    const std::vector<Vector3>& across, double toleranceMm)
{
    int count = 0;
    bool inside = false;
    for (std::size_t a = 0; a + 1 < along.size(); ++a) {
        const std::vector<Vector3> edge{along[a], along[a + 1]};
        const geometry::PolylineApproach approach =
            geometry::ClosestApproachBetween(edge, across);
        const bool touching = approach.valid && approach.distanceMm <= toleranceMm;
        if (touching && !inside) {
            ++count;
        }
        inside = touching;
    }
    return count;
}

} // namespace

//! 許容差内で近づく区間がいくつあるか。2箇所以上なら「2重交差」。
//!
//! 片側だけを辿ると数え落とす。長い1本の線が相手を2回またぐとき、
//! その2回が同じ区間に入ってしまい、1回に見える。
//! 相手の側から辿れば、離れている区間が間に挟まるので2回だと分かる。
//! そこで両方から数えて、多いほうを採る。見逃すより多めに疑うほうが安全である。
int CountApproaches(const SampledChain& first, const SampledChain& second,
    double toleranceMm)
{
    return std::max(CountApproachesAlong(first.points, second.points, toleranceMm),
        CountApproachesAlong(second.points, first.points, toleranceMm));
}

double JoinToleranceMm(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm * 100.0);
}

} // namespace kachakacha::v2::modeling::detail
