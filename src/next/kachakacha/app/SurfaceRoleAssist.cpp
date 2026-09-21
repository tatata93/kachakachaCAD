#include "kachakacha/app/SurfaceRoleAssist.h"

#include "kachakacha/app/SurfaceRoleTopology.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <numeric>
#include <optional>

namespace kachakacha::v2::app {

namespace {

using detail::Check;
using detail::Contact;
using detail::Draft;
using detail::Examine;
using detail::FindLoops;
using detail::InsideLoop;
using detail::Ladder;
using detail::Loop;
using detail::Probe;
using detail::SplitIntoLadder;
using detail::Topology;
using geometry::Vector3;
using modeling::ChainRole;
using modeling::GuideSurfaceMethod;

[[nodiscard]] std::string Count(std::size_t value)
{
    return std::to_string(value) + "本";
}

} // namespace

namespace {

// ---------------------------------------------------------------- 場面ごとの候補

struct Scenario {
    //! 先頭ほど薦めたい候補。検査に通って成り立つ最初のものを薦める。
    std::vector<Draft> drafts;
    std::vector<std::string> facts;
    std::vector<std::string> problems;
};

[[nodiscard]] Draft EmptyDraft(const Topology& topology, GuideSurfaceMethod method)
{
    Draft draft;
    draft.method = method;
    draft.roles.assign(topology.probes.size(), WireRoleChoice::Section);
    draft.reasons.assign(topology.probes.size(), std::string());
    return draft;
}

//! 人が決めた役割を、候補の上へかぶせる(つながりより人の決定を先にする)。
void ApplyFixed(Draft& draft, const std::vector<WireRoleChoice>& fixed)
{
    for (std::size_t index = 0; index < fixed.size(); ++index) {
        if (fixed[index] != WireRoleChoice::Auto) {
            draft.roles[index] = fixed[index];
            draft.reasons[index] = "あなたが決めた役割";
        }
    }
}

[[nodiscard]] std::string SectionNumber(std::size_t position)
{
    return "断面" + std::to_string(position + 1);
}

//! ガイドに沿った断面の並び(交わる位置の順)。
[[nodiscard]] std::vector<std::size_t> OrderOnRail(const Topology& topology, std::size_t rail,
    const std::vector<std::size_t>& sections)
{
    std::vector<std::size_t> order = sections;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t l, std::size_t r) {
        return topology.PositionOn(rail, l) < topology.PositionOn(rail, r);
    });
    return order;
}

//! どのガイドでも断面の並びが同じ(向きは問わない)かを見て、事実と問題を足す。
void CheckRailOrder(const Topology& topology, const Ladder& ladder, Scenario& scenario)
{
    if (!ladder.complete || ladder.rails.size() < 2 || ladder.sections.size() < 3) {
        if (ladder.complete && ladder.sections.size() >= 2) {
            scenario.facts.push_back("順序の矛盾なし");
        }
        return;
    }
    const std::vector<std::size_t> reference = OrderOnRail(topology, ladder.rails.front(),
        ladder.sections);
    for (std::size_t r = 1; r < ladder.rails.size(); ++r) {
        const std::vector<std::size_t> other = OrderOnRail(topology, ladder.rails[r],
            ladder.sections);
        const std::vector<std::size_t> flipped(other.rbegin(), other.rend());
        if (other == reference || flipped == reference) {
            continue;
        }
        // 向きをそろえる(基準の最初の断面が先に来る向き)。そのうえで、基準の並びで
        // 前後が逆になっている最初の 2 つを言う。
        const auto firstAt = std::find(other.begin(), other.end(), reference.front());
        const auto lastAt = std::find(other.begin(), other.end(), reference.back());
        const std::vector<std::size_t>& compare = firstAt < lastAt ? other : flipped;
        for (std::size_t i = 0; i < reference.size(); ++i) {
            for (std::size_t j = i + 1; j < reference.size(); ++j) {
                const auto pi = std::find(compare.begin(), compare.end(), reference[i]);
                const auto pj = std::find(compare.begin(), compare.end(), reference[j]);
                if (pi > pj) {
                    scenario.problems.push_back(SectionNumber(i) + "と" + SectionNumber(j)
                        + "の順序がガイド1とガイド" + std::to_string(r + 1) + "で逆転しています");
                    return;
                }
            }
        }
    }
    scenario.facts.push_back("順序の矛盾なし");
}

//! 閉じた断面の真ん中を通り、断面と交わらない開いた線(中心線の候補)。無ければ -1。
[[nodiscard]] std::optional<std::size_t> FindCenterline(const Topology& topology,
    const std::vector<std::size_t>& closedSections, const std::vector<std::size_t>& openCandidates)
{
    if (closedSections.size() < 2) {
        return std::nullopt;
    }
    std::optional<std::size_t> best;
    double bestRatio = std::numeric_limits<double>::infinity();
    for (const std::size_t candidate : openCandidates) {
        const Probe& line = topology.probes[candidate];
        double worst = 0.0;
        for (const std::size_t section : closedSections) {
            const Probe& ring = topology.probes[section];
            const double distance = geometry::MinimumDistanceBetween({ring.centroid}, line.points);
            worst = std::max(worst, distance / std::max(ring.sizeMm, 1.0e-9));
        }
        if (worst <= 0.25 && worst < bestRatio) {
            bestRatio = worst;
            best = candidate;
        }
    }
    return best;
}

//! 交わりの無い線だけ: 断面(2 本ならルールド、3 本以上ならロフト)。中心線があれば中心線。
[[nodiscard]] Scenario SectionsScenario(const Topology& topology,
    const std::vector<WireRoleChoice>& fixed, const std::vector<std::size_t>& members)
{
    Scenario scenario;
    std::vector<std::size_t> closed;
    std::vector<std::size_t> open;
    std::optional<std::size_t> centerline;
    for (const std::size_t index : members) {
        if (fixed[index] == WireRoleChoice::Centerline) {
            centerline = index;
        } else if (fixed[index] == WireRoleChoice::Auto || fixed[index] == WireRoleChoice::Section) {
            (topology.probes[index].closed ? closed : open).push_back(index);
        }
    }
    if (!centerline.has_value()) {
        std::vector<std::size_t> auto_open;
        for (const std::size_t index : open) {
            if (fixed[index] == WireRoleChoice::Auto) {
                auto_open.push_back(index);
            }
        }
        centerline = FindCenterline(topology, closed, auto_open);
    }
    Draft loft = EmptyDraft(topology, GuideSurfaceMethod::LoftSections);
    std::size_t sections = 0;
    for (const std::size_t index : members) {
        if (centerline.has_value() && index == *centerline) {
            loft.roles[index] = WireRoleChoice::Centerline;
            loft.reasons[index] = "閉じた断面の真ん中を通り、断面とは交わらない";
            continue;
        }
        loft.roles[index] = WireRoleChoice::Section;
        loft.reasons[index] = "ほかの線と交わらない";
        ++sections;
    }
    for (const std::size_t index : members) {
        if (fixed[index] == WireRoleChoice::Guide) {
            scenario.problems.push_back("ガイドに決めた線が、どの断面とも交わっていません");
        }
    }
    ApplyFixed(loft, fixed);
    scenario.facts.push_back("断面候補: " + Count(sections));
    if (centerline.has_value()) {
        scenario.facts.push_back("中心線候補: 1本(閉じた断面の真ん中を通る)");
        loft.reasonJa = "閉じた断面" + Count(sections)
            + "の真ん中を通る線が1本あります。中心線に沿って断面を運びます。";
        scenario.drafts.push_back(loft);
        return scenario;
    }
    Draft ruled = loft;
    ruled.method = GuideSurfaceMethod::RuledSections;
    if (sections == 2) {
        ruled.reasonJa = "交わらない線が2本あります。2本のあいだを直線で渡します。";
        loft.reasonJa = "2本の断面をなめらかに通します(2本ならルールドと同じ形)。";
        scenario.drafts.push_back(ruled);
        scenario.drafts.push_back(loft);
        return scenario;
    }
    loft.reasonJa = sections >= 3
        ? "交わらない線が" + Count(sections) + "あります。断面として順になめらかに通します。"
        : std::string("断面がもう1本要ります(断面は2本以上)。");
    ruled.refusalJa = "ルールドは断面2本です(いま" + Count(sections) + ")。";
    scenario.drafts.push_back(loft);
    if (sections >= 3) {
        scenario.drafts.push_back(ruled);
    }
    return scenario;
}

//! 外側の 4 本(最初と最後の断面・ガイド)が端どうしでつながって輪になるか。
[[nodiscard]] bool ExtremesFormLoop(const Topology& topology, std::size_t s0, std::size_t s1,
    std::size_t r0, std::size_t r1)
{
    for (const std::size_t s : {s0, s1}) {
        for (const std::size_t r : {r0, r1}) {
            const Contact* contact = topology.Between(s, r);
            if (contact == nullptr || !contact->atEndOfA || !contact->atEndOfB) {
                return false;
            }
        }
    }
    return true;
}

//! 断面とガイドの組(はしご形)。U/V 網・外周 + 通る線の候補も作る。
[[nodiscard]] Scenario LadderScenario(const Topology& topology,
    const std::vector<WireRoleChoice>& fixed, const std::vector<std::size_t>& connected,
    const std::vector<std::size_t>& isolated)
{
    const Ladder ladder = SplitIntoLadder(topology, connected, fixed);
    if (!ladder.found) {
        std::vector<std::size_t> all = connected;
        all.insert(all.end(), isolated.begin(), isolated.end());
        Scenario fallback = SectionsScenario(topology, fixed, all);
        fallback.problems.insert(fallback.problems.begin(), ladder.problems.begin(),
            ladder.problems.end());
        fallback.problems.push_back("役割を決めきれませんでした。右の棚で線ごとに役割を選んでください。");
        return fallback;
    }
    Scenario scenario;
    const std::size_t sCount = ladder.sections.size();
    const std::size_t rCount = ladder.rails.size();
    scenario.facts.push_back("ガイド候補: " + Count(rCount));
    scenario.facts.push_back("断面候補: " + Count(sCount));
    if (ladder.complete) {
        scenario.facts.push_back(rCount == 1 ? std::string("すべての断面がガイドへ接続")
                : rCount == 2           ? std::string("すべての断面が両ガイドへ接続")
                                        : "すべての断面が" + Count(rCount) + "のガイドすべてへ接続");
    } else {
        for (std::size_t s = 0; s < sCount; ++s) {
            for (std::size_t r = 0; r < rCount; ++r) {
                const Contact* contact = topology.Between(ladder.sections[s], ladder.rails[r]);
                if (contact == nullptr) {
                    scenario.problems.push_back(SectionNumber(s) + "がガイド" + std::to_string(r + 1)
                        + "と交わっていません");
                } else if (contact->count > 1) {
                    scenario.problems.push_back(SectionNumber(s) + "がガイド" + std::to_string(r + 1)
                        + "と " + std::to_string(contact->count) + " か所で交わっています");
                }
            }
        }
    }
    CheckRailOrder(topology, ladder, scenario);
    // 閉じた断面の真ん中を通る、交わらない線は中心線。
    std::vector<std::size_t> closedSections;
    for (const std::size_t s : ladder.sections) {
        if (topology.probes[s].closed) {
            closedSections.push_back(s);
        }
    }
    std::vector<std::size_t> openIsolated;
    for (const std::size_t index : isolated) {
        if (!topology.probes[index].closed && fixed[index] == WireRoleChoice::Auto) {
            openIsolated.push_back(index);
        }
    }
    const std::optional<std::size_t> centerline =
        FindCenterline(topology, closedSections, openIsolated);

    Draft loft = EmptyDraft(topology, GuideSurfaceMethod::LoftSections);
    for (const std::size_t s : ladder.sections) {
        loft.roles[s] = WireRoleChoice::Section;
        loft.reasons[s] = rCount == 1 ? std::string("ガイドと交わる")
            : rCount == 2             ? std::string("2本のガイドの両方と交わる")
                                      : Count(rCount) + "のガイドと交わる";
    }
    for (const std::size_t r : ladder.rails) {
        loft.roles[r] = WireRoleChoice::Guide;
        loft.reasons[r] = Count(sCount) + "の断面を横切る";
    }
    for (const std::size_t index : isolated) {
        if (centerline.has_value() && index == *centerline) {
            loft.roles[index] = WireRoleChoice::Centerline;
            loft.reasons[index] = "閉じた断面の真ん中を通り、断面とは交わらない";
            continue;
        }
        loft.roles[index] = WireRoleChoice::Section;
        loft.reasons[index] = "どの線とも交わらない(役割を決められません)";
        if (fixed[index] == WireRoleChoice::Auto) {
            scenario.problems.push_back("どの線とも交わらない線が1本あります(役割を決められません)");
        }
    }
    if (centerline.has_value()) {
        scenario.facts.push_back("中心線候補: 1本(閉じた断面の真ん中を通る)");
    }
    ApplyFixed(loft, fixed);
    loft.reasonJa = (rCount == 1 ? std::string("1本のガイドと、それを横切る")
                                 : Count(rCount) + "の長手ガイドと、それらを横切る")
        + Count(sCount) + "の断面から構成されています。";

    Draft network = loft;
    network.method = GuideSurfaceMethod::CurveNetworkExact;
    Draft filling = loft;
    filling.method = GuideSurfaceMethod::GordonNetwork;
    if (sCount >= 2 && rCount >= 2 && ladder.complete) {
        network.reasonJa = "U " + Count(sCount) + " × V " + Count(rCount)
            + "の線が全部互いに交わる網です。全部の線を通る面にします(Gordon)。";
        filling.reasonJa = "U/V の網を通るように近似で張ります(Filling)。";
    } else {
        network.refusalJa = "現在の選択ではU/V網が不足しています(U・V とも2本以上で、"
                            "すべての U がすべての V と1か所で交わる必要があります)。";
        filling.refusalJa = network.refusalJa;
    }
    // 外側の 4 本を縁にし、内側の線を通る線にする作り方(四辺面・境界面)。
    Draft four = EmptyDraft(topology, GuideSurfaceMethod::FourEdgePatch);
    Draft fill = EmptyDraft(topology, GuideSurfaceMethod::BoundaryFill);
    bool outerLoop = false;
    if (sCount >= 2 && rCount >= 2 && ladder.complete) {
        const auto sectionsInOrder = OrderOnRail(topology, ladder.rails.front(), ladder.sections);
        const auto railsInOrder = OrderOnRail(topology, ladder.sections.front(), ladder.rails);
        const std::size_t s0 = sectionsInOrder.front();
        const std::size_t s1 = sectionsInOrder.back();
        const std::size_t r0 = railsInOrder.front();
        const std::size_t r1 = railsInOrder.back();
        outerLoop = ExtremesFormLoop(topology, s0, s1, r0, r1);
        for (Draft* draft : {&four, &fill}) {
            for (std::size_t index = 0; index < topology.probes.size(); ++index) {
                const bool edge = index == s0 || index == s1 || index == r0 || index == r1;
                draft->roles[index] = edge ? WireRoleChoice::Boundary : WireRoleChoice::PassThrough;
                draft->reasons[index] = edge ? "外側の線(外周の一辺)" : "外周の内側を通る";
            }
            ApplyFixed(*draft, fixed);
        }
    }
    const std::size_t inner = topology.probes.size() >= 4 ? topology.probes.size() - 4 : 0;
    if (outerLoop) {
        four.reasonJa = "外側の4本を縁にして、内側の" + Count(inner) + "を面が通る線にします。";
        fill.reasonJa = "外周+内部拘束としても作れますが、中間形状の自由度が高くなります。";
    } else {
        four.refusalJa = "外側の線どうしが端でつながっていないので、外周の輪になりません。";
        fill.refusalJa = four.refusalJa;
    }
    if (ladder.complete && sCount >= 3 && rCount >= 3) {
        scenario.drafts = {network, loft, filling, four, fill};
    } else {
        scenario.drafts = {loft, network, filling, four, fill};
    }
    scenario.problems.insert(scenario.problems.end(), ladder.problems.begin(), ladder.problems.end());
    return scenario;
}

//! 閉じた輪(外周)がある: 輪が境界、輪に届く・内側の線は通る線。はしご形ならロフトも。
[[nodiscard]] Scenario BoundaryScenario(const Topology& topology,
    const std::vector<WireRoleChoice>& fixed, const Loop& outer)
{
    Scenario scenario;
    const std::size_t n = topology.probes.size();
    std::vector<bool> inOuter(n, false);
    for (const std::size_t index : outer.members) {
        inOuter[index] = true;
    }
    std::vector<std::size_t> holes;
    std::vector<std::size_t> through;
    std::size_t stray = 0;
    for (std::size_t index = 0; index < n; ++index) {
        const Probe& probe = topology.probes[index];
        if (inOuter[index] || !probe.valid) {
            continue;
        }
        bool meets = false;
        for (const std::size_t member : outer.members) {
            meets = meets || topology.Between(index, member) != nullptr;
        }
        const bool inside = InsideLoop(topology, outer, probe.centroid);
        const bool coplanar = outer.planar
            && std::abs(geometry::Dot(probe.centroid - outer.plane.origin, outer.plane.normal))
                <= topology.joinMm;
        if (fixed[index] == WireRoleChoice::Boundary
            || (fixed[index] == WireRoleChoice::Auto && probe.closed && inside && coplanar && !meets)) {
            holes.push_back(index);
        } else {
            through.push_back(index);
            stray += !meets && !inside && fixed[index] == WireRoleChoice::Auto ? 1 : 0;
        }
    }
    const std::size_t k = outer.members.size();
    const bool single = k == 1;
    scenario.facts.push_back(single ? std::string("閉じた線: 1本")
                                    : "端どうしでつながった閉じた輪: " + Count(k));
    scenario.facts.push_back(outer.planar ? "同じ平面に載っています" : "同じ平面に載っていません");
    if (!holes.empty()) {
        scenario.facts.push_back("外形の内側の閉じた線: " + Count(holes.size()) + "(平面では穴)");
    }
    if (!through.empty()) {
        scenario.facts.push_back("輪に届く・内側の線: " + Count(through.size()) + "(面が通る線)");
    }
    if (stray > 0) {
        scenario.problems.push_back("輪に届かず内側にもない線が" + Count(stray) + "あります");
    }
    const auto build = [&](GuideSurfaceMethod method, bool holesAreBoundary) {
        Draft draft = EmptyDraft(topology, method);
        for (std::size_t index = 0; index < n; ++index) {
            if (inOuter[index]) {
                draft.roles[index] = WireRoleChoice::Boundary;
                draft.reasons[index] = single ? "閉じた線(外形)" : "端どうしでつながった閉じた輪の一辺";
            }
        }
        for (const std::size_t index : holes) {
            draft.roles[index] = holesAreBoundary ? WireRoleChoice::Boundary : WireRoleChoice::PassThrough;
            draft.reasons[index] = holesAreBoundary ? "外形の内側の閉じた線(穴)" : "輪の内側にある";
        }
        for (const std::size_t index : through) {
            draft.roles[index] = WireRoleChoice::PassThrough;
            draft.reasons[index] = "輪に届く・輪の内側を通る";
        }
        ApplyFixed(draft, fixed);
        return draft;
    };
    const std::string throughNote = through.empty() ? std::string()
        : "内側の" + Count(through.size()) + "は面が通る線にします。";
    Draft planar = build(GuideSurfaceMethod::PlanarBoundary, true);
    planar.reasonJa = (single ? std::string("閉じた線が同じ平面に載っています。")
                              : Count(k) + "の線が端どうしでつながり、同じ平面の閉じた輪になっています。")
        + (holes.empty() ? std::string() : "内側の閉じた線" + Count(holes.size()) + "は穴にします。");
    if (!outer.planar) {
        planar.refusalJa = "同じ平面に載っていません。";
    } else if (!through.empty()) {
        planar.refusalJa = "通る線が" + Count(through.size()) + "あります。平面では通る線を使えません。";
    }
    Draft four = build(GuideSurfaceMethod::FourEdgePatch, false);
    four.reasonJa = "4本の線が端どうしでつながった輪です。4辺をそのまま縁にします。" + throughNote;
    if (k != 4) {
        four.refusalJa = "辺が" + Count(k) + "です(四辺面は、端でつながった4本の線で作ります)。";
    }
    Draft fill = build(GuideSurfaceMethod::BoundaryFill, false);
    fill.reasonJa = (single ? std::string("閉じた線を縁にして張ります")
                            : Count(k) + "の線が端どうしでつながった輪を縁にして張ります")
        + (outer.planar ? std::string("。") : std::string("(同じ平面ではない輪)。")) + throughNote;
    // 内側の線が 2 本の辺のあいだを全部渡している(はしご形)なら、ロフトを先に薦める。
    std::vector<Draft> ladderDrafts;
    if (!through.empty()) {
        std::vector<std::size_t> all;
        for (std::size_t index = 0; index < n; ++index) {
            if (topology.probes[index].valid) {
                all.push_back(index);
            }
        }
        const Ladder ladder = SplitIntoLadder(topology, all, fixed);
        if (ladder.found && ladder.complete && ladder.sections.size() >= 3) {
            Scenario asLadder = LadderScenario(topology, fixed, all, {});
            for (const Draft& draft : asLadder.drafts) {
                if (draft.method == GuideSurfaceMethod::LoftSections
                    || draft.method == GuideSurfaceMethod::CurveNetworkExact) {
                    ladderDrafts.push_back(draft);
                }
            }
            scenario.facts.insert(scenario.facts.end(), asLadder.facts.begin(), asLadder.facts.end());
        }
    }
    if (!ladderDrafts.empty()) {
        // はしご形にも読めるときは、外周 + 通る線の読み方を「他の候補」として同じ言葉で言う。
        four.reasonJa = "外側の4本を縁にして、内側の" + Count(through.size())
            + "を面が通る線にします。";
        fill.reasonJa = "外周+内部拘束としても作れますが、中間形状の自由度が高くなります。";
    }
    scenario.drafts = ladderDrafts;
    if (through.empty() && outer.planar) {
        scenario.drafts.insert(scenario.drafts.end(), {planar, four, fill});
    } else if (k == 4) {
        scenario.drafts.insert(scenario.drafts.end(), {four, fill, planar});
    } else {
        scenario.drafts.insert(scenario.drafts.end(), {fill, four, planar});
    }
    return scenario;
}

//! 閉じた線が 2 本以上、全部同じ平面: 外形と穴(離れた外形がいくつあってもよい)で平面。
[[nodiscard]] Scenario ClosedPlanarScenario(const Topology& topology,
    const std::vector<WireRoleChoice>& fixed, const std::vector<std::size_t>& members)
{
    Scenario scenario;
    Draft planar = EmptyDraft(topology, GuideSurfaceMethod::PlanarBoundary);
    for (const std::size_t index : members) {
        planar.roles[index] = WireRoleChoice::Boundary;
        planar.reasons[index] = "同じ平面の閉じた線";
    }
    ApplyFixed(planar, fixed);
    planar.reasonJa = "閉じた線" + Count(members.size())
        + "が同じ平面に載っています。外形と穴として平らな面にします。";
    scenario.facts.push_back("閉じた線: " + Count(members.size()) + "(同じ平面)");
    scenario.drafts.push_back(planar);
    Draft fill = planar;
    fill.method = GuideSurfaceMethod::BoundaryFill;
    fill.refusalJa = "境界面は閉じた輪1つを縁にします(いま" + Count(members.size()) + ")。";
    scenario.drafts.push_back(fill);
    return scenario;
}

[[nodiscard]] std::string MethodNameJa(GuideSurfaceMethod method,
    const std::vector<ClassifiedWire>& wires)
{
    std::size_t guides = 0;
    bool centerline = false;
    for (const ClassifiedWire& wire : wires) {
        guides += wire.role == WireRoleChoice::Guide ? 1 : 0;
        centerline = centerline || wire.role == WireRoleChoice::Centerline;
    }
    switch (method) {
    case GuideSurfaceMethod::LoftSections:
        return guides > 0 ? "ガイド付きロフト(ガイド" + Count(guides) + ")"
            : centerline  ? std::string("中心線ロフト")
                          : std::string("ロフト");
    case GuideSurfaceMethod::RuledSections:     return "ルールド";
    case GuideSurfaceMethod::PlanarBoundary:    return "平面";
    case GuideSurfaceMethod::BoundaryFill:      return "境界面";
    case GuideSurfaceMethod::FourEdgePatch:     return "四辺面";
    case GuideSurfaceMethod::GordonNetwork:     return "曲線網(近似)";
    case GuideSurfaceMethod::CurveNetworkExact: return "曲線網(Gordon)";
    case GuideSurfaceMethod::GuidedLoft:        return "案内付きロフト";
    case GuideSurfaceMethod::OffsetGuide:       return "離した面";
    case GuideSurfaceMethod::Revolve:           return "回転体";
    }
    return "面";
}

} // namespace

std::size_t SurfaceRoleAnalysis::Count(WireRoleChoice role) const noexcept
{
    return static_cast<std::size_t>(std::count_if(wires.begin(), wires.end(),
        [role](const ClassifiedWire& wire) { return wire.role == role; }));
}

WireRoleChoice SurfaceRoleAnalysis::RoleOf(const base::EntityId& id) const noexcept
{
    for (const ClassifiedWire& wire : wires) {
        if (wire.id == id) {
            return wire.role;
        }
    }
    return WireRoleChoice::Auto;
}

namespace {

[[nodiscard]] std::vector<WireRoleChoice> FixedRoles(const Topology& topology,
    const std::vector<WireRoleOverride>& overrides)
{
    std::vector<WireRoleChoice> fixed(topology.probes.size(), WireRoleChoice::Auto);
    for (std::size_t index = 0; index < topology.probes.size(); ++index) {
        for (const WireRoleOverride& item : overrides) {
            if (item.wire == topology.probes[index].id) {
                fixed[index] = item.role;
            }
        }
    }
    return fixed;
}

[[nodiscard]] bool BoundaryFamily(WireRoleChoice role) noexcept
{
    return role == WireRoleChoice::Boundary || role == WireRoleChoice::PassThrough;
}

[[nodiscard]] bool LoftFamily(WireRoleChoice role) noexcept
{
    return role == WireRoleChoice::Section || role == WireRoleChoice::Guide
        || role == WireRoleChoice::Centerline;
}

//! 断面・ガイドの場面(交わりの有る線は、はしご形として分け、無い線だけなら断面)。
[[nodiscard]] Scenario SectionOrLadder(const Topology& topology,
    const std::vector<WireRoleChoice>& fixed, const std::vector<std::size_t>& members)
{
    std::vector<std::size_t> connected;
    std::vector<std::size_t> isolated;
    for (const std::size_t index : members) {
        bool meets = false;
        for (const std::size_t other : topology.Neighbors(index)) {
            meets = meets || std::find(members.begin(), members.end(), other) != members.end();
        }
        (meets ? connected : isolated).push_back(index);
    }
    if (connected.empty()) {
        return SectionsScenario(topology, fixed, members);
    }
    return LadderScenario(topology, fixed, connected, isolated);
}

[[nodiscard]] const Loop* LargestLoop(const std::vector<Loop>& loops, bool openOnly)
{
    const Loop* best = nullptr;
    for (const Loop& loop : loops) {
        if (openOnly && loop.members.size() < 2) {
            continue;
        }
        if (best == nullptr || loop.sizeMm > best->sizeMm + 1.0e-9) {
            best = &loop;
        }
    }
    return best;
}

[[nodiscard]] Scenario ChooseScenario(const Topology& topology,
    const std::vector<WireRoleChoice>& fixed, std::vector<std::string>& problems)
{
    const std::size_t n = topology.probes.size();
    std::size_t boundaryVotes = 0;
    std::size_t loftVotes = 0;
    for (const WireRoleChoice role : fixed) {
        boundaryVotes += BoundaryFamily(role) ? 1 : 0;
        loftVotes += LoftFamily(role) ? 1 : 0;
    }
    if (boundaryVotes > 0 && loftVotes > 0) {
        problems.push_back("境界・通る線と、断面・ガイド・中心線は、同じ面で一緒には使えません。"
                           "役割を見直してください。");
    }
    const bool boundaryChosen = boundaryVotes > loftVotes;
    const bool loftChosen = loftVotes > 0 && !boundaryChosen;
    std::vector<bool> eligible(n, false);
    std::vector<std::size_t> valid;
    for (std::size_t index = 0; index < n; ++index) {
        if (!topology.probes[index].valid) {
            continue;
        }
        valid.push_back(index);
        eligible[index] = !LoftFamily(fixed[index]) && fixed[index] != WireRoleChoice::PassThrough;
    }
    std::vector<std::size_t> loftMembers;
    for (const std::size_t index : valid) {
        if (!BoundaryFamily(fixed[index])) {
            loftMembers.push_back(index);
        }
    }
    if (loftChosen) {
        return SectionOrLadder(topology, fixed, valid);
    }
    const std::vector<Loop> loops = FindLoops(topology, eligible);
    const Loop* openLoop = LargestLoop(loops, true);
    if (boundaryChosen) {
        // 人が境界に決めた線を含む輪があれば、それを外周にする。
        for (const Loop& loop : loops) {
            for (const std::size_t member : loop.members) {
                if (fixed[member] == WireRoleChoice::Boundary) {
                    return BoundaryScenario(topology, fixed, loop);
                }
            }
        }
        const Loop* any = openLoop != nullptr ? openLoop : LargestLoop(loops, false);
        if (any != nullptr) {
            return BoundaryScenario(topology, fixed, *any);
        }
        problems.push_back("境界が閉じた輪になっていません(線の端どうしがつながっていません)。");
        Scenario open;
        Draft fill = EmptyDraft(topology, GuideSurfaceMethod::BoundaryFill);
        ApplyFixed(fill, fixed);
        fill.refusalJa = "境界が閉じた輪になっていません。";
        open.drafts.push_back(fill);
        return open;
    }
    if (openLoop != nullptr) {
        return BoundaryScenario(topology, fixed, *openLoop);
    }
    const bool allClosed = !valid.empty() && std::all_of(valid.begin(), valid.end(),
        [&topology](std::size_t index) { return topology.probes[index].closed; });
    if (allClosed && valid.size() == 1) {
        return BoundaryScenario(topology, fixed, loops.front());
    }
    if (allClosed) {
        std::vector<Vector3> points;
        for (const std::size_t index : valid) {
            points.insert(points.end(), topology.probes[index].points.begin(),
                topology.probes[index].points.end());
        }
        const geometry::PlaneFit plane = geometry::FitPlane(points);
        bool touching = false;
        for (const std::size_t index : valid) {
            touching = touching || !topology.Neighbors(index).empty();
        }
        if (plane.valid && plane.maximumDeviationMm <= topology.joinMm && !touching) {
            return ClosedPlanarScenario(topology, fixed, valid);
        }
    }
    return SectionOrLadder(topology, fixed, loftMembers);
}

//! 候補を検査し、成り立つ最初のものを薦める。同じ作り方は 1 度だけ。
void Evaluate(const Topology& topology, const Scenario& scenario,
    const std::vector<WireRoleChoice>& fixed, const geometry::GeometryTolerance& tolerance,
    SurfaceRoleAnalysis& out)
{
    std::vector<SurfaceMethodCandidate> candidates;
    for (const Draft& draft : scenario.drafts) {
        const bool seen = std::any_of(candidates.begin(), candidates.end(),
            [&draft](const SurfaceMethodCandidate& c) { return c.method == draft.method; });
        if (!seen) {
            candidates.push_back(Check(topology, draft, tolerance));
        }
    }
    if (candidates.empty()) {
        return;
    }
    std::size_t chosen = 0;
    while (chosen < candidates.size() && !candidates[chosen].feasible) {
        ++chosen;
    }
    out.recommendedFeasible = chosen < candidates.size();
    if (!out.recommendedFeasible) {
        chosen = 0;
        out.problemsJa.push_back("おすすめの作り方でも、このままでは作れません: "
            + candidates.front().reasonJa);
    }
    const SurfaceMethodCandidate& best = candidates[chosen];
    out.recommended = best.method;
    out.recommendedReasonJa = best.reasonJa;
    if (!out.recommendedFeasible) {
        // 成り立たないときも、なぜその形に見えたか(つながりからの理由)を言う。
        for (const Draft& draft : scenario.drafts) {
            if (draft.method == best.method && !draft.reasonJa.empty()) {
                out.recommendedReasonJa = draft.reasonJa;
            }
        }
    }
    out.wires = best.wires;
    for (std::size_t index = 0; index < out.wires.size() && index < fixed.size(); ++index) {
        out.wires[index].fixedByUser = fixed[index] != WireRoleChoice::Auto;
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != chosen) {
            out.alternatives.push_back(candidates[index]);
        }
    }
}

} // namespace

SurfaceRoleAnalysis AnalyzeSurfaceRoles(const std::vector<RoleWire>& wires,
    const std::vector<WireRoleOverride>& overrides, const geometry::GeometryTolerance& tolerance)
{
    SurfaceRoleAnalysis out;
    if (wires.empty()) {
        out.recommended = GuideSurfaceMethod::PlanarBoundary;
        out.factsJa.push_back("線をまだ選んでいません。3D で線を押してください(押し直すと外れます)。");
        return out;
    }
    const Topology topology = Examine(wires, tolerance);
    const std::vector<WireRoleChoice> fixed = FixedRoles(topology, overrides);
    std::vector<std::string> problems;
    const Scenario scenario = ChooseScenario(topology, fixed, problems);
    out.factsJa.push_back("選んだ" + Count(wires.size())
        + "を調べました(つながり・交わり・閉じ方・同じ平面・順番から決めています)");
    Evaluate(topology, scenario, fixed, tolerance, out);
    // 役割ごとの本数(薦めた作り方の役割で)。はしご形の事実は場面が足す。
    const std::vector<std::pair<WireRoleChoice, std::string>> roleNames{
        {WireRoleChoice::Boundary, "境界候補: "}, {WireRoleChoice::PassThrough, "通る線候補: "}};
    for (const auto& [role, name] : roleNames) {
        if (const std::size_t count = out.Count(role); count > 0) {
            out.factsJa.push_back(name + Count(count));
        }
    }
    out.factsJa.insert(out.factsJa.end(), scenario.facts.begin(), scenario.facts.end());
    const std::size_t fixedCount = static_cast<std::size_t>(std::count_if(fixed.begin(),
        fixed.end(), [](WireRoleChoice role) { return role != WireRoleChoice::Auto; }));
    if (fixedCount > 0) {
        out.factsJa.push_back("あなたが決めた役割: " + Count(fixedCount) + "(そのまま使います)");
    }
    for (const Probe& probe : topology.probes) {
        if (!probe.valid) {
            problems.push_back("形を読めない線が1本あります");
        }
    }
    out.problemsJa.insert(out.problemsJa.begin(), problems.begin(), problems.end());
    out.problemsJa.insert(out.problemsJa.end(), scenario.problems.begin(), scenario.problems.end());
    return out;
}

namespace {

[[nodiscard]] SurfaceInputState ApplyRoles(const SurfaceInputState& state,
    const std::vector<ClassifiedWire>& wires, GuideSurfaceMethod method)
{
    SurfaceInputState next = state;
    next.method = method;
    next.sections.clear();
    next.guides.clear();
    next.boundaries.clear();
    next.centerlines.clear();
    const auto push = [](std::vector<base::EntityId>& list, const base::EntityId& id) {
        if (std::find(list.begin(), list.end(), id) == list.end()) {
            list.push_back(id);
        }
    };
    for (const ClassifiedWire& wire : wires) {
        switch (wire.role) {
        case WireRoleChoice::Section:     push(next.sections, wire.id); break;
        case WireRoleChoice::Guide:
        case WireRoleChoice::PassThrough: push(next.guides, wire.id); break;
        case WireRoleChoice::Boundary:    push(next.boundaries, wire.id); break;
        case WireRoleChoice::Centerline:  push(next.centerlines, wire.id); break;
        case WireRoleChoice::Auto:        push(next.sections, wire.id); break;
        }
    }
    // もう入っていない線の向き反転・連続条件・並びは残さない。
    const auto present = [&next](const base::EntityId& id) {
        const auto all = AllSurfaceEntries(next);
        return std::find(all.begin(), all.end(), id) != all.end();
    };
    next.reversed.erase(std::remove_if(next.reversed.begin(), next.reversed.end(),
                            [&](const base::EntityId& id) { return !present(id); }),
        next.reversed.end());
    next.edgeConditions.erase(std::remove_if(next.edgeConditions.begin(),
                                  next.edgeConditions.end(),
                                  [&](const SurfaceEdgeCondition& c) { return !present(c.wire); }),
        next.edgeConditions.end());
    for (auto* order : {&next.explicitOrder, &next.adoptedOrder}) {
        order->erase(std::remove_if(order->begin(), order->end(),
                         [&next](const base::EntityId& id) {
                             return std::find(next.sections.begin(), next.sections.end(), id)
                                 == next.sections.end();
                         }),
            order->end());
    }
    return WithActiveSlotSettled(next);
}

} // namespace

SurfaceInputState WithClassifiedRoles(const SurfaceInputState& state,
    const SurfaceRoleAnalysis& analysis)
{
    if (analysis.wires.empty()) {
        return state;
    }
    return ApplyRoles(state, analysis.wires,
        state.methodChosenByUser ? state.method : analysis.recommended);
}

SurfaceInputState WithCandidateRoles(const SurfaceInputState& state,
    const SurfaceMethodCandidate& candidate)
{
    SurfaceInputState next = ApplyRoles(state, candidate.wires, candidate.method);
    next.methodChosenByUser = true;
    next.autoRoles = false;
    next.slotChosenByUser = false;
    return next;
}

std::string SurfaceCandidateNameJa(const SurfaceMethodCandidate& candidate)
{
    return MethodNameJa(candidate.method, candidate.wires);
}

std::vector<std::string> SurfaceRoleSummaryJa(const SurfaceRoleAnalysis& analysis)
{
    std::vector<std::string> lines = analysis.factsJa;
    if (analysis.wires.empty()) {
        return lines;
    }
    lines.push_back("おすすめ: " + MethodNameJa(analysis.recommended, analysis.wires)
        + (analysis.recommendedFeasible ? std::string() : std::string("(このままでは作れません)")));
    if (!analysis.recommendedReasonJa.empty()) {
        lines.push_back("理由: " + analysis.recommendedReasonJa);
    }
    if (!analysis.alternatives.empty()) {
        lines.push_back("他の候補:");
        for (const SurfaceMethodCandidate& candidate : analysis.alternatives) {
            lines.push_back("・" + MethodNameJa(candidate.method, candidate.wires)
                + (candidate.feasible ? ": " : "(作れません): ") + candidate.reasonJa);
        }
    }
    for (const std::string& problem : analysis.problemsJa) {
        lines.push_back("! " + problem);
    }
    return lines;
}

std::string_view WireRoleLabelJa(WireRoleChoice role) noexcept
{
    switch (role) {
    case WireRoleChoice::Auto:        return "自動";
    case WireRoleChoice::Section:     return "断面";
    case WireRoleChoice::Guide:       return "ガイド";
    case WireRoleChoice::Boundary:    return "境界";
    case WireRoleChoice::PassThrough: return "通る線";
    case WireRoleChoice::Centerline:  return "中心線";
    }
    return "自動";
}

Rgb WireRoleColor(WireRoleChoice role) noexcept
{
    switch (role) {
    case WireRoleChoice::Guide:       return Rgb{30, 110, 235};    // 青
    case WireRoleChoice::Section:     return Rgb{240, 135, 20};    // 橙
    case WireRoleChoice::Boundary:    return Rgb{150, 60, 205};    // 紫
    case WireRoleChoice::PassThrough: return Rgb{30, 160, 70};     // 緑
    case WireRoleChoice::Centerline:  return Rgb{0, 150, 170};     // 青緑
    case WireRoleChoice::Auto:        break;
    }
    return Rgb{140, 140, 140};
}

const std::vector<WireRoleChoice>& WireRoleChoices()
{
    static const std::vector<WireRoleChoice> choices{WireRoleChoice::Auto, WireRoleChoice::Section,
        WireRoleChoice::Guide, WireRoleChoice::Boundary, WireRoleChoice::PassThrough,
        WireRoleChoice::Centerline};
    return choices;
}

namespace {

[[nodiscard]] bool TakesPassThrough(GuideSurfaceMethod method) noexcept
{
    return method == GuideSurfaceMethod::BoundaryFill || method == GuideSurfaceMethod::FourEdgePatch;
}

[[nodiscard]] bool Holds(const std::vector<base::EntityId>& list, const base::EntityId& id)
{
    return std::find(list.begin(), list.end(), id) != list.end();
}

} // namespace

WireRoleChoice RoleOfEntry(const SurfaceInputState& state, const base::EntityId& id) noexcept
{
    if (Holds(state.sections, id)) {
        return WireRoleChoice::Section;
    }
    if (Holds(state.guides, id)) {
        return TakesPassThrough(state.method) ? WireRoleChoice::PassThrough : WireRoleChoice::Guide;
    }
    if (Holds(state.boundaries, id)) {
        return WireRoleChoice::Boundary;
    }
    if (Holds(state.centerlines, id)) {
        return WireRoleChoice::Centerline;
    }
    return WireRoleChoice::Auto;
}

ChainRole SlotForWireRole(WireRoleChoice role) noexcept
{
    switch (role) {
    case WireRoleChoice::Guide:
    case WireRoleChoice::PassThrough: return ChainRole::GuideU;
    case WireRoleChoice::Boundary:    return ChainRole::BoundarySide;
    case WireRoleChoice::Centerline:  return ChainRole::Centerline;
    case WireRoleChoice::Section:
    case WireRoleChoice::Auto:        break;
    }
    return ChainRole::Section;
}

WireRoleChoice WireRoleForSlot(GuideSurfaceMethod method, ChainRole slot) noexcept
{
    switch (slot) {
    case ChainRole::Section:      return WireRoleChoice::Section;
    case ChainRole::GuideU:
    case ChainRole::GuideV:
        return TakesPassThrough(method) ? WireRoleChoice::PassThrough : WireRoleChoice::Guide;
    case ChainRole::BoundarySide:
    case ChainRole::OuterBoundary:
    case ChainRole::HoleBoundary: return WireRoleChoice::Boundary;
    case ChainRole::Centerline:   return WireRoleChoice::Centerline;
    case ChainRole::SourceSurface: break;
    }
    return WireRoleChoice::Auto;
}

SurfaceInputState WithWireRoleChoice(const SurfaceInputState& state, const base::EntityId& id,
    WireRoleChoice role)
{
    SurfaceInputState next = state;
    next.roleOverrides.erase(std::remove_if(next.roleOverrides.begin(), next.roleOverrides.end(),
                                 [&id](const WireRoleOverride& item) { return item.wire == id; }),
        next.roleOverrides.end());
    if (role != WireRoleChoice::Auto) {
        next.roleOverrides.push_back(WireRoleOverride{id, role});
    }
    return next;
}

WireRoleChoice WireRoleOverrideOf(const SurfaceInputState& state, const base::EntityId& id) noexcept
{
    for (const WireRoleOverride& item : state.roleOverrides) {
        if (item.wire == id) {
            return item.role;
        }
    }
    return WireRoleChoice::Auto;
}

std::vector<std::pair<base::EntityId, Rgb>> SurfaceRoleColors(const SurfaceInputState& state)
{
    std::vector<std::pair<base::EntityId, Rgb>> colors;
    for (const base::EntityId& id : AllSurfaceEntries(state)) {
        const WireRoleChoice role = RoleOfEntry(state, id);
        if (role != WireRoleChoice::Auto) {
            colors.emplace_back(id, WireRoleColor(role));
        }
    }
    return colors;
}

} // namespace kachakacha::v2::app
