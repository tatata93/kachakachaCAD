#include "kachakacha/modeling/SurfaceCardinality.h"

#include <algorithm>

namespace kachakacha::v2::modeling {
namespace {

constexpr std::size_t kAny = kUnlimitedCount;

[[nodiscard]] RoleCardinality Role(ChainRole role, std::size_t minimum, std::size_t maximum,
    bool ordered = false, bool reorderable = false, const char* fixedReasonJa = "",
    const char* batchJa = "", std::size_t perBuildMaximum = kUnlimitedCount)
{
    RoleCardinality cardinality;
    cardinality.role = role;
    cardinality.minimum = minimum;
    cardinality.maximum = maximum;
    cardinality.ordered = ordered;
    cardinality.reorderable = reorderable;
    cardinality.fixedReasonJa = fixedReasonJa;
    cardinality.batchJa = batchJa;
    cardinality.perBuildMaximum = perBuildMaximum;
    return cardinality;
}

} // namespace

const std::vector<RoleCardinality>& SurfaceCardinality(GuideSurfaceMethod method)
{
    static const std::vector<RoleCardinality> planar{
        Role(ChainRole::OuterBoundary, 1, kAny, false, false, "",
            "同じ平面にある外周をまとめて 1 つの面(複数の島)にする"),
        Role(ChainRole::HoleBoundary, 0, kAny)};
    // ルールド: 1 枚のルールド面は 2 曲線で決まる。3 本以上は隣り合う 2 本ずつの
    // 帯を順につなぐ(1-2、2-3、3-4…)。
    static const std::vector<RoleCardinality> ruled{
        Role(ChainRole::Section, 2, kAny, true, true, "",
            "隣り合う 2 本ずつを直線で渡し、帯を順につなぐ")};
    // ロフト: 断面 2〜任意、ガイド 0〜任意、中心線 0〜1。
    // 中心線は「断面を運ぶ道筋」なので 1 本(2 本あると、どちらに沿うか決まらない)。
    // 断面の下限は 1。ただし 1 本で作れるのは、断面の両端にガイドが 1 本ずつある
    // (ガイドが断面より外へ伸びた側に仮想断面を足して網にする)ときだけ。それ以外は 2 本以上要る。
    // この条件は役割をまたぐので、表ではなく LoftSectionRuleProblemJa で見る。
    static const std::vector<RoleCardinality> loft{
        Role(ChainRole::Section, 1, kAny, true, true),
        Role(ChainRole::GuideU, 0, kAny, false, false, "",
            "すべてのガイドを面が通る(全部が形に効く)"),
        Role(ChainRole::Centerline, 0, 1, false, false,
            "中心線は断面を運ぶ 1 本の道筋です。2 本あると沿う先が決まりません。")};
    // 案内付きロフト(互換の入口)。中身はロフトと同じで、ガイドが 1 本以上要る。
    static const std::vector<RoleCardinality> guided{
        Role(ChainRole::Section, 1, kAny, true, true),
        Role(ChainRole::GuideU, 1, kAny, false, false, "",
            "すべてのガイドを面が通る(全部が形に効く)"),
        Role(ChainRole::Centerline, 0, 1, false, false,
            "中心線は断面を運ぶ 1 本の道筋です。2 本あると沿う先が決まりません。")};
    // 曲線網: U 2〜任意、V 2〜任意。2 は「網」の最小(外側の 2 本ずつが要る)。
    static const std::vector<RoleCardinality> gordon{
        Role(ChainRole::GuideU, 2, kAny, true, false,
            "網の外側に U が 2 本要ります。"),
        Role(ChainRole::GuideV, 2, kAny, true, false,
            "網の外側に V が 2 本要ります。")};
    // 境界面: 外周 1 輪(線は何本でもよい)+ 通る線 0〜任意。
    static const std::vector<RoleCardinality> fill{
        Role(ChainRole::BoundarySide, 1, kAny, false, false, "",
            "つながった線をまとめて 1 つの外周にする"),
        Role(ChainRole::GuideU, 0, kAny, false, false, "",
            "すべての通る線を面が通る")};
    // 離した面: 元の面 1〜任意。1 つずつ別の面を作る。
    static const std::vector<RoleCardinality> offset{
        Role(ChainRole::SourceSurface, 1, kAny, false, false, "",
            "元の面ごとに 1 枚ずつ離した面を作る", 1)};
    // 回転体: 断面 1〜任意(断面ごとに 1 つ)、軸 1(軸は定義上 1 本)。
    // 軸は表の行ではなく、軸の点と向きとして渡る(GuideTable.revolveAxis*)。
    static const std::vector<RoleCardinality> revolve{
        Role(ChainRole::Section, 1, kAny, false, false, "",
            "断面ごとに同じ軸で回した面を 1 つずつ作る", 1)};
    // 四辺面: 4 辺(定義上 4)+ 内部の通る線 0〜任意。
    static const std::vector<RoleCardinality> fourEdge{
        Role(ChainRole::BoundarySide, 4, 4, true, false,
            "四辺面は U0・U1・V0・V1 の 4 辺で囲う面です。"),
        Role(ChainRole::GuideU, 0, kAny, false, false, "",
            "すべての通る線を面が通る")};
    switch (method) {
    case GuideSurfaceMethod::PlanarBoundary: return planar;
    case GuideSurfaceMethod::RuledSections:  return ruled;
    case GuideSurfaceMethod::LoftSections:   return loft;
    case GuideSurfaceMethod::GuidedLoft:     return guided;
    case GuideSurfaceMethod::GordonNetwork:  return gordon;
    case GuideSurfaceMethod::BoundaryFill:   return fill;
    case GuideSurfaceMethod::OffsetGuide:    return offset;
    case GuideSurfaceMethod::Revolve:        return revolve;
    case GuideSurfaceMethod::FourEdgePatch:  return fourEdge;
    case GuideSurfaceMethod::CurveNetworkExact: return gordon;
    }
    return loft;
}

const RoleCardinality* SurfaceCardinalityOf(GuideSurfaceMethod method,
    ChainRole role) noexcept
{
    const auto& list = SurfaceCardinality(method);
    const auto found = std::find_if(list.begin(), list.end(),
        [&](const RoleCardinality& item) { return item.role == role; });
    return found == list.end() ? nullptr : &*found;
}

std::string CardinalityRangeJa(const RoleCardinality& cardinality)
{
    if (cardinality.minimum == cardinality.maximum) {
        return "ちょうど " + std::to_string(cardinality.minimum);
    }
    const std::string low = std::to_string(cardinality.minimum);
    if (cardinality.Unlimited()) {
        return low + "〜任意";
    }
    return low + "〜" + std::to_string(cardinality.maximum);
}

std::string SurfaceCardinalityProblemJa(GuideSurfaceMethod method, ChainRole role,
    std::size_t count, const std::string& roleNameJa)
{
    const RoleCardinality* cardinality = SurfaceCardinalityOf(method, role);
    if (cardinality == nullptr) {
        return count == 0 ? std::string()
                          : roleNameJa + "はこの作り方では使いません(入れたものは残しています)。";
    }
    const std::string now = "いま " + std::to_string(count) + " 本。";
    if (count < cardinality->minimum) {
        if (cardinality->minimum == cardinality->maximum) {
            return roleNameJa + "はちょうど " + std::to_string(cardinality->minimum)
                + " 本必要です。" + now;
        }
        return roleNameJa + "が " + std::to_string(cardinality->minimum) + " 本以上必要です。"
            + now;
    }
    if (count > cardinality->maximum) {
        std::string reason = cardinality->fixedReasonJa;
        return roleNameJa + "は " + std::to_string(cardinality->maximum) + " 本までです。" + now
            + (reason.empty() ? std::string() : reason);
    }
    return {};
}

bool SurfaceNeedsBatch(GuideSurfaceMethod method, ChainRole role, std::size_t count) noexcept
{
    const RoleCardinality* cardinality = SurfaceCardinalityOf(method, role);
    return cardinality != nullptr && count > cardinality->perBuildMaximum;
}

std::string LoftSectionRuleProblemJa(std::size_t sections, std::size_t rails)
{
    if (sections >= 2 || sections == 0) {
        return {};   // 0 本は個数の約束の側で言う
    }
    if (rails >= 2) {
        return {};   // 両端のガイドで 1 つの断面を掃ける(検査が両端かどうかを確かめる)
    }
    return "断面が 2 本以上必要です(いま 1 本)。1 本で作れるのは、断面の両端にガイドが 1 本ずつあるときだけです。";
}

std::string SurfaceSolverNoteJa(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis)
{
    std::size_t rails = 0;
    std::size_t interior = 0;
    for (const GuideChain& chain : request.chains) {
        rails += chain.role == ChainRole::GuideU ? 1 : 0;
    }
    switch (request.method) {
    case GuideSurfaceMethod::LoftSections:
    case GuideSurfaceMethod::GuidedLoft: {
        for (const LoftRail& rail : analysis.loft.rails) {
            interior += rail.side == LoftRailSide::Interior ? 1 : 0;
        }
        std::string note(LoftSolverLabelJa(analysis.loft.solver));
        if (rails > 0) {
            note += "(ガイド " + std::to_string(rails) + " 本";
            if (interior > 0) {
                note += "、うち内側 " + std::to_string(interior) + " 本";
            }
            note += "。全部が形に効きます)";
        }
        return note;
    }
    case GuideSurfaceMethod::FourEdgePatch:
        return std::string("4 辺から張る(") + std::string(FourEdgeStyleLabelJa(request.fourEdgeStyle))
            + ")" + (analysis.fourEdge.hasInteriorConstraints
                    ? "。通る線 " + std::to_string(rails) + " 本へ寄せて張り直します(近似拘束)"
                    : std::string());
    case GuideSurfaceMethod::GordonNetwork:
        return "曲線網(近似 / Filling): 外側の U・V を境界に、内側の線を点の拘束にして張ります";
    case GuideSurfaceMethod::CurveNetworkExact:
        return "曲線網(Gordon): U 線と V 線を全部通る面を網から直接組み立てます";
    case GuideSurfaceMethod::BoundaryFill:
        return rails == 0 ? std::string("外周の輪の内側を張ります")
                          : "外周の輪を境界に、通る線 " + std::to_string(rails)
                              + " 本を面が通る拘束にして張ります";
    default:
        break;
    }
    return {};
}

std::string NetworkAlternativeNoteJa(const GuideSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance)
{
    const bool exactChosen = request.method == GuideSurfaceMethod::CurveNetworkExact;
    if (!exactChosen && request.method != GuideSurfaceMethod::GordonNetwork) {
        return {};
    }
    GuideSurfaceRequest exact = request;
    exact.method = GuideSurfaceMethod::CurveNetworkExact;
    GuideSurfaceRequest approximate = request;
    approximate.method = GuideSurfaceMethod::GordonNetwork;
    const auto exactAnalysis = AnalyzeGuideSurfaceRequest(exact, tolerance);
    const auto approximateAnalysis = AnalyzeGuideSurfaceRequest(approximate, tolerance);
    const auto firstReason = [](const auto& analysis) {
        if (analysis.Diagnostics().empty()) {
            return std::string();
        }
        const auto& first = analysis.Diagnostics().front();
        return first.summaryJa + (first.detailsJa.empty() ? std::string() : "(" + first.detailsJa + ")");
    };
    if (exactAnalysis.HasValue() && approximateAnalysis.HasValue()) {
        return exactChosen
            ? "この網は曲線網(近似 / Filling)でも作れます(外側の線を縁にし、内側は点で近づける)"
            : "この網は曲線網(Gordon)でも作れます(U 線と V 線を全部 0.02 mm 以内で通す)";
    }
    if (exactChosen && !exactAnalysis.HasValue() && approximateAnalysis.HasValue()) {
        return "曲線網(Gordon)では作れません: " + firstReason(exactAnalysis)
            + " 曲線網(近似 / Filling)なら作れます";
    }
    if (!exactChosen && !approximateAnalysis.HasValue() && exactAnalysis.HasValue()) {
        return "曲線網(近似 / Filling)では作れません: " + firstReason(approximateAnalysis)
            + " 曲線網(Gordon)なら作れます";
    }
    return {};
}

} // namespace kachakacha::v2::modeling
