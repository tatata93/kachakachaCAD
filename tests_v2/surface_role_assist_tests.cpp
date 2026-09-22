// 初心者の「面を作る」: 選んだ線の役割と作り方を、線のつながりから決定的に決める
// (app/SurfaceRoleAssist.h、プロンプト beginner_workflow)。
//
// はしご形(ガイド 2 本 + 断面 3 本)はガイド付きロフト、閉じた輪は平面・四辺面・境界面、
// 交わらない線は断面(2 本ならルールド)、閉じた断面の真ん中を通る線は中心線。
// 候補の作り方は幾何の検査に実際に通してから言う。選んだ順を変えても答えは同じ。
// 人が決めた役割はそのまま使う。
#include "kachakacha/app/SurfaceRoleAssist.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <string>
#include <vector>

using kachakacha::v2::app::AnalyzeSurfaceRoles;
using kachakacha::v2::app::RoleOfEntry;
using kachakacha::v2::app::RoleWire;
using kachakacha::v2::app::SurfaceInputState;
using kachakacha::v2::app::SurfaceRoleAnalysis;
using kachakacha::v2::app::SurfaceRoleSummaryJa;
using kachakacha::v2::app::WireRoleChoice;
using kachakacha::v2::app::WireRoleColor;
using kachakacha::v2::app::WireRoleOverride;
using kachakacha::v2::app::WithCandidateRoles;
using kachakacha::v2::app::WithClassifiedRoles;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

//! 点を順に結んだ線 1 本。
[[nodiscard]] RoleWire Wire(DeterministicIdGenerator& ids, const std::vector<Vector3>& points)
{
    RoleWire wire;
    wire.id = ids.NextTyped<IdKind::Entity>();
    for (std::size_t at = 1; at < points.size(); ++at) {
        wire.segments.push_back(CurveSegment::MakeLine(points[at - 1], points[at]).Value());
    }
    return wire;
}

[[nodiscard]] std::string Joined(const SurfaceRoleAnalysis& analysis)
{
    std::string all;
    for (const std::string& line : SurfaceRoleSummaryJa(analysis)) {
        all += line + "\n";
    }
    return all;
}

[[nodiscard]] bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

//! 山形の断面(y = 0..20 を渡る)。x の位置に置く。
[[nodiscard]] std::vector<Vector3> Arch(double x, double height)
{
    return {{x, 0, 0}, {x, 5, height * 0.7}, {x, 10, height}, {x, 15, height * 0.7}, {x, 20, 0}};
}

} // namespace

KACHA_V2_TEST(role_assist, ガイド2本と断面3本はガイド付きロフトで理由と他の候補を言う)
{
    DeterministicIdGenerator ids;
    // 断面は x = 0, 30, 60 の山形、ガイドは y = 0 と y = 20 の長手の線(断面の両端を通る)。
    const std::vector<RoleWire> wires{Wire(ids, Arch(0, 8)), Wire(ids, {{0, 0, 0}, {60, 0, 0}}),
        Wire(ids, Arch(30, 10)), Wire(ids, {{0, 20, 0}, {60, 20, 0}}), Wire(ids, Arch(60, 8))};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    const std::string text = Joined(analysis);
    Require(analysis.recommended == GuideSurfaceMethod::LoftSections && analysis.recommendedFeasible,
        "ガイド付きロフトを薦め、検査を通る: " + text);
    Require(analysis.Count(WireRoleChoice::Guide) == 2 && analysis.Count(WireRoleChoice::Section) == 3,
        "ガイド 2 本・断面 3 本: " + text);
    Require(analysis.RoleOf(wires[1].id) == WireRoleChoice::Guide
            && analysis.RoleOf(wires[3].id) == WireRoleChoice::Guide,
        "長手の 2 本がガイド");
    Require(Contains(text, "選んだ5本を調べました") && Contains(text, "ガイド候補: 2本")
            && Contains(text, "断面候補: 3本") && Contains(text, "すべての断面が両ガイドへ接続")
            && Contains(text, "順序の矛盾なし"),
        "調べた事実を言う: " + text);
    Require(Contains(text, "おすすめ: ガイド付きロフト") && Contains(text, "理由: 2本の長手ガイド"),
        "おすすめと理由: " + text);
    Require(Contains(text, "他の候補:") && Contains(text, "境界面")
            && Contains(text, "中間形状の自由度が高くなります"),
        "他の候補(境界面)も理由つきで言う: " + text);
}

KACHA_V2_TEST(role_assist, 平らなはしご形もガイド付きロフトで外周の輪は他の候補)
{
    // 画面の試験(HP-SF-13)と同じ形: 断面は x = 0, 30, 60 の直線、ガイドは y = 0 と y = 20。
    // 全部が同じ平面に載り、外側の 4 本は端どうしで輪にもなる。はしごの読み方を先にし、
    // 輪(平面・境界面)は他の候補として言う。5 本とも役割を持つ。
    DeterministicIdGenerator ids;
    const std::vector<RoleWire> wires{Wire(ids, {{0, 0, 0}, {0, 20, 0}}),
        Wire(ids, {{30, 0, 0}, {30, 20, 0}}), Wire(ids, {{60, 0, 0}, {60, 20, 0}}),
        Wire(ids, {{0, 0, 0}, {60, 0, 0}}), Wire(ids, {{0, 20, 0}, {60, 20, 0}})};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    const std::string text = Joined(analysis);
    Require(analysis.recommended == GuideSurfaceMethod::LoftSections && analysis.recommendedFeasible,
        "ガイド付きロフトを薦める: " + text);
    Require(analysis.Count(WireRoleChoice::Guide) == 2 && analysis.Count(WireRoleChoice::Section) == 3,
        "ガイド 2 本・断面 3 本: " + text);
    Require(Contains(text, "他の候補:") && Contains(text, "境界面"), "境界面も候補: " + text);
}

KACHA_V2_TEST(role_assist, 選んだ順が違っても同じ役割と作り方になる)
{
    DeterministicIdGenerator ids;
    std::vector<RoleWire> wires{Wire(ids, Arch(0, 8)), Wire(ids, {{0, 0, 0}, {60, 0, 0}}),
        Wire(ids, Arch(30, 10)), Wire(ids, {{0, 20, 0}, {60, 20, 0}}), Wire(ids, Arch(60, 8))};
    const SurfaceRoleAnalysis first = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    std::reverse(wires.begin(), wires.end());
    const SurfaceRoleAnalysis second = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    Require(first.recommended == second.recommended, "作り方が同じ");
    for (const RoleWire& wire : wires) {
        Require(first.RoleOf(wire.id) == second.RoleOf(wire.id), "線ごとの役割が同じ");
    }
}

KACHA_V2_TEST(role_assist, 端でつながった平らな4本は平面で四辺面と境界面も候補)
{
    DeterministicIdGenerator ids;
    const std::vector<RoleWire> wires{Wire(ids, {{0, 0, 0}, {40, 0, 0}}),
        Wire(ids, {{40, 0, 0}, {40, 30, 0}}), Wire(ids, {{40, 30, 0}, {0, 30, 0}}),
        Wire(ids, {{0, 30, 0}, {0, 0, 0}})};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    const std::string text = Joined(analysis);
    Require(analysis.recommended == GuideSurfaceMethod::PlanarBoundary && analysis.recommendedFeasible,
        "平面を薦める: " + text);
    Require(analysis.Count(WireRoleChoice::Boundary) == 4, "4 本とも境界: " + text);
    Require(Contains(text, "端どうしでつながった閉じた輪: 4本") && Contains(text, "同じ平面に載っています"),
        "輪と平面の事実: " + text);
    const bool fourFeasible = std::any_of(analysis.alternatives.begin(), analysis.alternatives.end(),
        [](const auto& c) { return c.method == GuideSurfaceMethod::FourEdgePatch && c.feasible; });
    Require(fourFeasible, "四辺面も作れる候補: " + text);
}

KACHA_V2_TEST(role_assist, 平らでない4辺の輪は四辺面で5辺なら四辺面は作れないと言う)
{
    DeterministicIdGenerator ids;
    const std::vector<RoleWire> four{Wire(ids, {{0, 0, 0}, {40, 0, 5}}),
        Wire(ids, {{40, 0, 5}, {40, 30, 0}}), Wire(ids, {{40, 30, 0}, {0, 30, 5}}),
        Wire(ids, {{0, 30, 5}, {0, 0, 0}})};
    const SurfaceRoleAnalysis bent = AnalyzeSurfaceRoles(four, {}, Tolerance());
    Require(bent.recommended == GuideSurfaceMethod::FourEdgePatch && bent.recommendedFeasible,
        "平らでない 4 辺は四辺面: " + Joined(bent));
    const std::vector<RoleWire> five{Wire(ids, {{0, 0, 0}, {40, 0, 0}}),
        Wire(ids, {{40, 0, 0}, {50, 20, 0}}), Wire(ids, {{50, 20, 0}, {20, 35, 0}}),
        Wire(ids, {{20, 35, 0}, {-10, 20, 0}}), Wire(ids, {{-10, 20, 0}, {0, 0, 0}})};
    const SurfaceRoleAnalysis pentagon = AnalyzeSurfaceRoles(five, {}, Tolerance());
    const std::string text = Joined(pentagon);
    Require(pentagon.recommended == GuideSurfaceMethod::PlanarBoundary, "平らな 5 辺は平面: " + text);
    Require(Contains(text, "四辺面(作れません): 辺が5本です"), "四辺面は作れないと言う: " + text);
}

KACHA_V2_TEST(role_assist, 輪と内側の線は通る線でロフトの読み方も確かめる)
{
    DeterministicIdGenerator ids;
    // 平らでない 4 辺の輪と、向かい合う辺の中ほどを渡す 1 本(端が辺に触れる)。
    const std::vector<RoleWire> wires{Wire(ids, {{0, 0, 0}, {40, 0, 0}}),
        Wire(ids, {{40, 0, 0}, {40, 30, 6}}), Wire(ids, {{40, 30, 6}, {0, 30, 6}}),
        Wire(ids, {{0, 30, 6}, {0, 0, 0}}), Wire(ids, {{20, 0, 0}, {20, 15, 6}, {20, 30, 6}})};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    const std::string text = Joined(analysis);
    Require(analysis.recommendedFeasible, "作れる作り方を薦める: " + text);
    // はしご形(ガイド 2 本 × 断面 3 本)にも、輪 + 通る線にも読める。どちらでも入力を捨てない。
    const bool usesAll = analysis.Count(WireRoleChoice::Section) + analysis.Count(WireRoleChoice::Guide)
            + analysis.Count(WireRoleChoice::Boundary) + analysis.Count(WireRoleChoice::PassThrough)
        == 5;
    Require(usesAll, "5 本とも役割がある: " + text);
    const bool passThroughCandidate = std::any_of(analysis.alternatives.begin(),
        analysis.alternatives.end(), [](const auto& c) {
            return c.method == GuideSurfaceMethod::FourEdgePatch
                || c.method == GuideSurfaceMethod::BoundaryFill;
        });
    Require(passThroughCandidate || analysis.recommended == GuideSurfaceMethod::FourEdgePatch,
        "外周 + 通る線の読み方も出す: " + text);
}

KACHA_V2_TEST(role_assist, 交わらない線は断面で2本ならルールド3本ならロフト)
{
    DeterministicIdGenerator ids;
    const std::vector<RoleWire> two{Wire(ids, {{0, 0, 0}, {40, 0, 0}}),
        Wire(ids, {{0, 20, 10}, {40, 20, 10}})};
    const SurfaceRoleAnalysis ruled = AnalyzeSurfaceRoles(two, {}, Tolerance());
    Require(ruled.recommended == GuideSurfaceMethod::RuledSections && ruled.recommendedFeasible,
        "2 本はルールド: " + Joined(ruled));
    const std::vector<RoleWire> three{Wire(ids, Arch(0, 5)), Wire(ids, Arch(20, 9)),
        Wire(ids, Arch(40, 6))};
    const SurfaceRoleAnalysis loft = AnalyzeSurfaceRoles(three, {}, Tolerance());
    Require(loft.recommended == GuideSurfaceMethod::LoftSections && loft.recommendedFeasible
            && loft.Count(WireRoleChoice::Section) == 3,
        "3 本はロフト(断面 3): " + Joined(loft));
}

KACHA_V2_TEST(role_assist, 閉じた断面の真ん中を通る線は中心線)
{
    DeterministicIdGenerator ids;
    const auto square = [](double z, double half) {
        return std::vector<Vector3>{{-half, -half, z}, {half, -half, z}, {half, half, z},
            {-half, half, z}, {-half, -half, z}};
    };
    const std::vector<RoleWire> wires{Wire(ids, square(0, 10)), Wire(ids, square(30, 8)),
        Wire(ids, square(60, 6)), Wire(ids, {{0, 0, -5}, {0, 0, 65}})};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    const std::string text = Joined(analysis);
    Require(analysis.RoleOf(wires[3].id) == WireRoleChoice::Centerline, "真ん中の線は中心線: " + text);
    Require(analysis.Count(WireRoleChoice::Section) == 3
            && analysis.recommended == GuideSurfaceMethod::LoftSections,
        "閉じた断面 3 つのロフト: " + text);
}

KACHA_V2_TEST(role_assist, ガイドどうしで断面の順が逆転していれば言う)
{
    DeterministicIdGenerator ids;
    // 断面は x = 0, 20, 40 の縦線(y = 0..30)。ガイド 1 は x の順に通る。ガイド 2 は
    // 断面 2 の上を回って先に断面 3 と交わり、戻って断面 2 と交わる(どの断面とも 1 か所)。
    const std::vector<RoleWire> wires{Wire(ids, {{0, 0, 0}, {0, 30, 0}}),
        Wire(ids, {{20, 0, 0}, {20, 30, 0}}), Wire(ids, {{40, 0, 0}, {40, 30, 0}}),
        Wire(ids, {{-5, 5, 0}, {45, 5, 0}}),
        Wire(ids, {{-5, 25, 0}, {0, 25, 0}, {10, 35, 0}, {45, 35, 0}, {35, 15, 0}, {15, 15, 0}})};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    const std::string text = Joined(analysis);
    Require(Contains(text, "順序がガイド1とガイド2で逆転しています"), "逆転を言う: " + text);
}

KACHA_V2_TEST(role_assist, 人が決めた役割はそのまま使い検査で作れなければ理由を言う)
{
    DeterministicIdGenerator ids;
    const std::vector<RoleWire> wires{Wire(ids, Arch(0, 5)), Wire(ids, Arch(20, 9)),
        Wire(ids, Arch(40, 6))};
    const std::vector<WireRoleOverride> overrides{{wires[1].id, WireRoleChoice::Guide}};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, overrides, Tolerance());
    const std::string text = Joined(analysis);
    Require(analysis.RoleOf(wires[1].id) == WireRoleChoice::Guide, "人の決めたガイドのまま: " + text);
    Require(analysis.wires[1].fixedByUser, "人が決めた印");
    Require(!analysis.recommendedFeasible && Contains(text, "このままでは作れません"),
        "交わらないガイドでは作れないと言う(作れたことにしない): " + text);
    Require(Contains(text, "あなたが決めた役割: 1本"), "人が決めた本数を言う: " + text);
}

KACHA_V2_TEST(role_assist, 分類を欄へ写し他の候補でも入れ直せる)
{
    DeterministicIdGenerator ids;
    const std::vector<RoleWire> wires{Wire(ids, Arch(0, 8)), Wire(ids, {{0, 0, 0}, {60, 0, 0}}),
        Wire(ids, Arch(30, 10)), Wire(ids, {{0, 20, 0}, {60, 20, 0}}), Wire(ids, Arch(60, 8))};
    const SurfaceRoleAnalysis analysis = AnalyzeSurfaceRoles(wires, {}, Tolerance());
    SurfaceInputState state;
    state.autoRoles = true;
    const SurfaceInputState placed = WithClassifiedRoles(state, analysis);
    Require(placed.method == GuideSurfaceMethod::LoftSections && placed.sections.size() == 3
            && placed.guides.size() == 2,
        "断面 3 本とガイド 2 本が欄に入る");
    Require(RoleOfEntry(placed, wires[1].id) == WireRoleChoice::Guide
            && RoleOfEntry(placed, wires[0].id) == WireRoleChoice::Section,
        "欄から役割が読める");
    const auto fill = std::find_if(analysis.alternatives.begin(), analysis.alternatives.end(),
        [](const auto& c) { return c.method == GuideSurfaceMethod::BoundaryFill && c.feasible; });
    Require(fill != analysis.alternatives.end(), "境界面の候補がある");
    const SurfaceInputState refilled = WithCandidateRoles(placed, *fill);
    Require(refilled.method == GuideSurfaceMethod::BoundaryFill && refilled.boundaries.size() == 4
            && refilled.guides.size() == 1 && refilled.methodChosenByUser && !refilled.autoRoles,
        "境界 4 本 + 通る線 1 本に入れ直し、作り方は人が選んだことになる");
    Require(RoleOfEntry(refilled, wires[2].id) == WireRoleChoice::PassThrough,
        "境界面のガイドの欄は通る線");
}

KACHA_V2_TEST(role_assist, 役割の色は見分けられる)
{
    Require(!(WireRoleColor(WireRoleChoice::Guide) == WireRoleColor(WireRoleChoice::Section))
            && !(WireRoleColor(WireRoleChoice::Boundary) == WireRoleColor(WireRoleChoice::PassThrough))
            && !(WireRoleColor(WireRoleChoice::Guide) == WireRoleColor(WireRoleChoice::Boundary)),
        "ガイド・断面・境界・通る線の色が違う");
    const auto guide = WireRoleColor(WireRoleChoice::Guide);
    const auto section = WireRoleColor(WireRoleChoice::Section);
    const auto boundary = WireRoleColor(WireRoleChoice::Boundary);
    const auto through = WireRoleColor(WireRoleChoice::PassThrough);
    Require(guide.b > guide.r && section.r > section.b && boundary.r > boundary.g
            && boundary.b > boundary.g && through.g > through.r,
        "ガイド = 青、断面 = 橙、境界 = 紫、通る線 = 緑");
}

KACHA_V2_TEST_MAIN("surface_role_assist_tests")
