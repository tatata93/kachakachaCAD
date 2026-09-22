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

//! ガイドの、端の断面より外の長さ(mm)。before = 最初の断面の側、after = 最後の断面の側。
struct Overhang {
    double before = 0.0;
    double after = 0.0;
    bool beforeIsStart = true;   //!< 最初の断面の側が、ガイドを引いた始点の側か
};

//! ガイドの始点の側が、最初の断面の側か。断面が 1 本だけだと交わりの順では決まらないので、
//! 1 本目のガイドの向きにそろえる(端どうしが近い組み合わせ)。
[[nodiscard]] bool BeforeIsStart(const GuideSurfaceRequest& request, const LoftPlan& plan,
    const RailGrid& grid, std::size_t r, std::size_t first, std::size_t last)
{
    if (first != last) {
        return grid.crossings[r][first].firstParameter <= grid.crossings[r][last].firstParameter;
    }
    const auto& reference = request.chains[plan.rails.front().chainIndex].segments;
    const auto& here = request.chains[plan.rails[r].chainIndex].segments;
    const double same = geometry::Distance(here.front().StartPoint(), reference.front().StartPoint())
        + geometry::Distance(here.back().EndPoint(), reference.back().EndPoint());
    const double crossed =
        geometry::Distance(here.front().StartPoint(), reference.back().EndPoint())
        + geometry::Distance(here.back().EndPoint(), reference.front().StartPoint());
    return same <= crossed;
}

[[nodiscard]] Overhang OverhangOf(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const LoftPlan& plan, const RailGrid& grid,
    std::size_t r, std::size_t first, std::size_t last)
{
    Overhang out;
    out.beforeIsStart = BeforeIsStart(request, plan, grid, r, first, last);
    const double length = sampled[plan.rails[r].chainIndex].lengthMm;
    const double a = grid.crossings[r][first].firstParameter;
    const double b = grid.crossings[r][last].firstParameter;
    out.before = (out.beforeIsStart ? a : 1.0 - a) * length;
    out.after = (out.beforeIsStart ? 1.0 - b : b) * length;
    return out;
}

//! ガイドの端(引いた始点の側か、終点の側か)。
[[nodiscard]] Vector3 RailEnd(const std::vector<CurveSegment>& chain, bool start)
{
    return start ? chain.front().StartPoint() : chain.back().EndPoint();
}

//! 張り直す・網にするときのガイドは、最初の断面から最後の断面までの部分。はみ出しは断る
//! (はみ出した部分は面に乗らない。黙って捨てると「通る線」を無視したことになる)。
//! 仮想断面を作る側(toBefore / toAfter)だけは、ガイドの端まで使う。
[[nodiscard]] std::vector<Diagnostic> SpanRails(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& sections,
    const RailGrid& grid, const std::vector<std::size_t>& order, double joinTolerance,
    LoftPlan& plan, bool toBefore = false, bool toAfter = false)
{
    std::vector<Diagnostic> errors;
    const std::size_t first = PositionOf(sections, order.front());
    const std::size_t last = PositionOf(sections, order.back());
    for (std::size_t r = 0; r < plan.rails.size(); ++r) {
        const Overhang overhang = OverhangOf(request, sampled, plan, grid, r, first, last);
        LoftRail& rail = plan.rails[r];
        const double beyond =
            std::max(toBefore ? 0.0 : overhang.before, toAfter ? 0.0 : overhang.after);
        if (beyond > joinTolerance) {
            errors.push_back(MakeError(kBadInput,
                Label(request, rail.chainIndex) + "が端の断面より外へ "
                    + Mm(beyond) + " mm はみ出しています。",
                "面は最初の断面から最後の断面までの間に張ります。はみ出した部分は面に乗りません。"
                "ガイドを断面のところで切るか、端に断面を足してください。"));
            continue;
        }
        const auto& chain = request.chains[rail.chainIndex].segments;
        const Vector3 from = toBefore ? RailEnd(chain, overhang.beforeIsStart)
                                      : grid.crossings[r][first].position;
        const Vector3 to = toAfter ? RailEnd(chain, !overhang.beforeIsStart)
                                   : grid.crossings[r][last].position;
        const auto span = geometry::TrimChainBetween(chain, from, to, joinTolerance);
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

//! 外側のガイドが始点側と終点側に 1 本ずつ(内側のガイドは何本でも)。網にできる形。
[[nodiscard]] bool OuterPair(const LoftPlan& plan)
{
    const auto count = [&plan](LoftRailSide side) {
        return std::count_if(plan.rails.begin(), plan.rails.end(),
            [side](const LoftRail& rail) { return rail.side == side; });
    };
    return count(LoftRailSide::Start) == 1 && count(LoftRailSide::End) == 1;
}

//! v を、単位ベクトル from を to へ重ねるいちばん小さい回転で回す(ロドリゲスの式)。
[[nodiscard]] Vector3 RotateOnto(const Vector3& v, const Vector3& from, const Vector3& to)
{
    const Vector3 axis = geometry::Cross(from, to);
    const double sine = axis.Length();
    const double cosine = geometry::Dot(from, to);
    if (sine < 1.0e-12) {
        if (cosine > 0.0) {
            return v;
        }
        // ちょうど逆向き: from に直角な軸で半回転する。
        const Vector3 other = std::abs(from.x) < 0.9 ? Vector3{1.0, 0.0, 0.0}
                                                     : Vector3{0.0, 1.0, 0.0};
        const Vector3 k = geometry::Normalized(geometry::Cross(from, other));
        return k * (2.0 * geometry::Dot(k, v)) - v;
    }
    const Vector3 k = axis * (1.0 / sine);
    return v * cosine + geometry::Cross(k, v) * sine + k * (geometry::Dot(k, v) * (1.0 - cosine));
}

//! 仮想断面: 端の断面を、2 本のガイドの端へ運んだ折れ線(相似 = 移動・いちばん小さい回転・
//! 一様な拡大縮小)。断面の始点が始点側のガイドの端へ、終点が終点側のガイドの端へ来る。
//! 作れなければ空(呼ぶ側は従来の 2 本レールへ戻す)。
[[nodiscard]] std::vector<CurveSegment> VirtualSection(const SampledChain& section, bool reversed,
    const Vector3& startEnd, const Vector3& endEnd)
{
    std::vector<Vector3> points = section.points;
    if (reversed) {
        std::reverse(points.begin(), points.end());
    }
    if (points.size() < 2) {
        return {};
    }
    const Vector3 origin = points.front();
    const Vector3 chord = points.back() - origin;
    const Vector3 target = endEnd - startEnd;
    if (chord.Length() < 1.0e-9 || target.Length() < 1.0e-9) {
        return {};
    }
    const double scale = target.Length() / chord.Length();
    const Vector3 from = chord * (1.0 / chord.Length());
    const Vector3 to = target * (1.0 / target.Length());
    // 点が多すぎると網の計算が重い。弧長で等間隔に 129 点まで間引く。
    constexpr std::size_t kMaximumPoints = 129;
    if (points.size() > kMaximumPoints) {
        const std::vector<double> t = geometry::NormalizedArcLength(points);
        std::vector<Vector3> thinned;
        for (std::size_t k = 0; k < kMaximumPoints; ++k) {
            thinned.push_back(geometry::PointAtNormalizedArcLength(points, t,
                static_cast<double>(k) / static_cast<double>(kMaximumPoints - 1)));
        }
        points = std::move(thinned);
    }
    std::vector<CurveSegment> out;
    Vector3 previous = startEnd;
    for (std::size_t k = 1; k < points.size(); ++k) {
        const Vector3 next = k + 1 == points.size()
            ? endEnd
            : startEnd + RotateOnto(points[k] - origin, from, to) * scale;
        const auto line = CurveSegment::MakeLine(previous, next);
        if (!line.HasValue()) {
            continue;   // 重なった点は飛ばす
        }
        out.push_back(line.Value());
        previous = next;
    }
    return out;
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

//! 外側のガイドが両脇に 1 本ずつ: 断面とガイドを網(Gordon)にする。
//! 外側の 2 本だけで、両方のガイドが端の断面より外へ伸びている側には仮想断面を作る。
//! 片方だけが伸びている・仮想断面を作らない設定なら、従来の 2 本レールで掃く。
[[nodiscard]] Result<GuideSurfaceAnalysis> NetworkAlongRails(const GuideSurfaceRequest& request,
    const std::vector<SampledChain>& sampled, const std::vector<std::size_t>& sections,
    const RailGrid& grid, const std::vector<std::size_t>& order, double joinTolerance,
    GuideSurfaceAnalysis analysis)
{
    LoftPlan& plan = analysis.loft;
    const std::size_t first = PositionOf(sections, order.front());
    const std::size_t last = PositionOf(sections, order.back());
    std::vector<Overhang> overhangs;
    bool anyBefore = false;
    bool allBefore = true;
    bool anyAfter = false;
    bool allAfter = true;
    for (std::size_t r = 0; r < plan.rails.size(); ++r) {
        overhangs.push_back(OverhangOf(request, sampled, plan, grid, r, first, last));
        const bool before = overhangs.back().before > joinTolerance;
        const bool after = overhangs.back().after > joinTolerance;
        anyBefore = anyBefore || before;
        allBefore = allBefore && before;
        anyAfter = anyAfter || after;
        allAfter = allAfter && after;
    }
    const auto railOn = [&plan](LoftRailSide side) {
        for (std::size_t r = 0; r < plan.rails.size(); ++r) {
            if (plan.rails[r].side == side) {
                return r;
            }
        }
        return std::size_t{0};
    };
    const std::size_t startRail = railOn(LoftRailSide::Start);
    const std::size_t endRail = railOn(LoftRailSide::End);
    const auto endOf = [&](std::size_t r, bool beforeSide) {
        return RailEnd(request.chains[plan.rails[r].chainIndex].segments,
            beforeSide == overhangs[r].beforeIsStart);
    };
    const bool outerOnly = plan.rails.size() == 2;
    if (outerOnly && request.createVirtualEndSections && allBefore) {
        plan.virtualBefore = VirtualSection(sampled[order.front()], plan.reverseSections.front(),
            endOf(startRail, true), endOf(endRail, true));
    }
    if (outerOnly && request.createVirtualEndSections && allAfter) {
        plan.virtualAfter = VirtualSection(sampled[order.back()], plan.reverseSections.back(),
            endOf(startRail, false), endOf(endRail, false));
    }
    const bool before = !plan.virtualBefore.empty();
    const bool after = !plan.virtualAfter.empty();
    const std::size_t lines = sections.size() + (before ? 1 : 0) + (after ? 1 : 0);
    if (outerOnly && ((anyBefore && !before) || (anyAfter && !after) || lines < 2)) {
        plan.virtualBefore.clear();
        plan.virtualAfter.clear();
        plan.solver = LoftSolver::TwoRailSweep;
        PlanVirtualEndSections(request, grid, startRail, analysis);
        return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
    }
    if (lines < 2) {
        return Result<GuideSurfaceAnalysis>::Failure(MakeError(kBadInput,
            "断面が 2 本以上必要です(いま 1 本)。",
            "1 本で作れるのは、断面の両端にガイドが 1 本ずつあるときだけです。"));
    }
    std::vector<Diagnostic> errors =
        SpanRails(request, sampled, sections, grid, order, joinTolerance, plan, before, after);
    if (!errors.empty()) {
        return Result<GuideSurfaceAnalysis>::Failure(std::move(errors));
    }
    plan.solver = LoftSolver::RailNetwork;
    if (before || after) {
        // 位置は始点側のガイドの上(0 = 引いた始点、1 = 終点)。従来の仮想断面と同じ言い方。
        const bool startFirst = overhangs[startRail].beforeIsStart;
        if (before) {
            analysis.virtualSectionParameters.push_back(startFirst ? 0.0 : 1.0);
        }
        if (after) {
            analysis.virtualSectionParameters.push_back(startFirst ? 1.0 : 0.0);
        }
        analysis.notes.push_back(MakeWarning("GEO-G104",
            "端に断面が無いので、仮想断面を作ります。",
            std::to_string(analysis.virtualSectionParameters.size())
                + " 本。設定で作らないようにもできます。"));
    }
    return Result<GuideSurfaceAnalysis>::Success(std::move(analysis));
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
    if (centerlines.empty() && OuterPair(analysis.loft)) {
        return NetworkAlongRails(request, sampled, sections, grid, order, joinTolerance,
            std::move(analysis));
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

namespace kachakacha::v2::modeling {

GuideSurfaceRequest LoftNetworkRequest(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis)
{
    GuideSurfaceRequest network;
    network.method = GuideSurfaceMethod::CurveNetworkExact;
    int vs = 0;
    const auto addV = [&](const std::vector<CurveSegment>& segments) {
        GuideChain chain;
        chain.role = ChainRole::GuideV;
        chain.index = ++vs;
        chain.segments = segments;
        network.chains.push_back(std::move(chain));
    };
    if (!analysis.loft.virtualBefore.empty()) {
        addV(analysis.loft.virtualBefore);
    }
    for (const std::size_t index : analysis.sectionOrdering.chainIndices) {
        addV(request.chains[index].segments);
    }
    if (!analysis.loft.virtualAfter.empty()) {
        addV(analysis.loft.virtualAfter);
    }
    int us = 0;
    for (const LoftRail& rail : analysis.loft.rails) {
        GuideChain chain;
        chain.role = ChainRole::GuideU;
        chain.index = ++us;
        chain.segments = rail.span;
        network.chains.push_back(std::move(chain));
    }
    return network;
}

} // namespace kachakacha::v2::modeling
