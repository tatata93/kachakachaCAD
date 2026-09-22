// ロフトの入力検査(断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1、modeling/LoftInput.h)。
//
// 2026-09-22 まで、案内付きロフトは「ガイドちょうど 2 本」で、核は 1 本目を背骨、
// 2 本目を補助にするだけだった。3 本目以降を受ける作りになっていなかった。
// ここでは、どのガイドも形に効く作り方(LoftSolver)が選ばれることと、
// 作れない入力をどの線が悪いのかを言って断ることを押さえる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/GordonGrid.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/LoftInput.h"
#include "kachakacha/modeling/SurfaceCardinality.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest;
using kachakacha::v2::modeling::BuildGordonGrid;
using kachakacha::v2::modeling::LoftNetworkRequest;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideChain;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideSurfaceRequest;
using kachakacha::v2::modeling::LoftRailSide;
using kachakacha::v2::modeling::LoftSolver;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[nodiscard]] GuideChain Path(ChainRole role, int index, const std::vector<Vector3>& points)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    for (std::size_t at = 1; at < points.size(); ++at) {
        chain.segments.push_back(CurveSegment::MakeLine(points[at - 1], points[at]).Value());
    }
    return chain;
}

//! 格子状の前頭部もどき。断面は x 方向に並び、各断面は y = 0..40 を渡る山形。
//! ガイドは y = 一定の線で、全断面の同じ y の点を通る(頂点どうしで交わる)。
[[nodiscard]] double Height(double x, double y)
{
    return 10.0 * std::sin(3.14159265358979323846 * y / 40.0) * (1.0 - 0.3 * x / 100.0);
}

[[nodiscard]] GuideSurfaceRequest Grid(int sectionCount, const std::vector<double>& railYs,
    GuideSurfaceMethod method = GuideSurfaceMethod::LoftSections)
{
    GuideSurfaceRequest request;
    request.method = method;
    const double ys[] = {0.0, 10.0, 20.0, 30.0, 40.0};
    std::vector<double> xs;
    for (int s = 0; s < sectionCount; ++s) {
        xs.push_back(100.0 * s / (sectionCount - 1));
    }
    // 断面は y の点(0,10,20,30,40)と、ガイドの y の点を通る折れ線にする。
    for (int s = 0; s < sectionCount; ++s) {
        std::vector<double> at(std::begin(ys), std::end(ys));
        at.insert(at.end(), railYs.begin(), railYs.end());
        std::sort(at.begin(), at.end());
        at.erase(std::unique(at.begin(), at.end()), at.end());
        std::vector<Vector3> points;
        for (const double y : at) {
            points.push_back({xs[static_cast<std::size_t>(s)], y, Height(xs[s], y)});
        }
        request.chains.push_back(Path(ChainRole::Section, s + 1, points));
    }
    for (std::size_t r = 0; r < railYs.size(); ++r) {
        std::vector<Vector3> points;
        for (const double x : xs) {
            points.push_back({x, railYs[r], Height(x, railYs[r])});
        }
        request.chains.push_back(Path(ChainRole::GuideU, static_cast<int>(r) + 1, points));
    }
    return request;
}

[[nodiscard]] auto Accept(const GuideSurfaceRequest& request, const std::string& why)
{
    auto result = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(result.HasValue(), "受け入れる: " + why + " ("
            + (result.Diagnostics().empty() ? std::string()
                                            : result.Diagnostics().front().summaryJa)
            + ")");
    return result;
}

[[nodiscard]] std::string RejectWhy(const GuideSurfaceRequest& request, std::string& code)
{
    const auto result = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(!result.HasValue(), "断る");
    code = result.Diagnostics().front().code;
    return result.Diagnostics().front().summaryJa;
}

} // namespace

KACHA_V2_TEST(loft, 断面2本とガイド0本は普通のロフト)
{
    const auto result = Accept(Grid(2, {}), "断面2・ガイド0");
    Require(result.Value().loft.solver == LoftSolver::Sections, "断面をなめらかに通す");
}

KACHA_V2_TEST(loft, 断面3本とガイド1本は全部を通るように張る)
{
    const auto result = Accept(Grid(3, {20.0}), "断面3・ガイド1(真ん中)");
    Require(result.Value().loft.solver == LoftSolver::RailFilling, "張り直す作り方");
    Require(result.Value().loft.rails.size() == 1, "ガイド1本");
    Require(result.Value().loft.rails[0].side == LoftRailSide::Interior, "内側のガイド");
    Require(!result.Value().loft.rails[0].span.empty(), "断面の間に切ったガイドがある");
}

KACHA_V2_TEST(loft, 断面3本と両端のガイド2本は網にして全部の線を通す)
{
    // 2026-09-22 まで 2 本のレールで掃いていた(MakePipeShell)。断面が 3 本あると
    // 断面の間で面が波打った(PC の撮影)。いまは断面とガイドの網(Gordon)。
    const auto result = Accept(Grid(3, {0.0, 40.0}), "断面3・ガイド2(両端)");
    Require(result.Value().loft.solver == LoftSolver::RailNetwork, "網にする");
    Require(result.Value().loft.rails[0].side == LoftRailSide::Start, "1本目は始点側");
    Require(result.Value().loft.rails[1].side == LoftRailSide::End, "2本目は終点側");
    Require(!result.Value().loft.rails[0].span.empty() && !result.Value().loft.rails[1].span.empty(),
        "どちらのガイドも断面の間で切り出してある");
    Require(result.Value().virtualSectionParameters.empty()
            && result.Value().loft.virtualBefore.empty() && result.Value().loft.virtualAfter.empty(),
        "端に断面があるので仮想断面は作らない");
}

KACHA_V2_TEST(loft, 断面3本とガイド3本は3本目も使う)
{
    const auto result = Accept(Grid(3, {0.0, 20.0, 40.0}), "断面3・ガイド3");
    Require(result.Value().loft.solver == LoftSolver::RailNetwork,
        "外側が両脇にあるので、内側の 3 本目も網の線にする");
    Require(result.Value().loft.rails.size() == 3, "3本とも作り方に入る");
    Require(result.Value().loft.rails[2].side == LoftRailSide::End
            || result.Value().loft.rails[1].side == LoftRailSide::Interior,
        "内側のガイドを内側と見分ける");
    int interior = 0;
    for (const auto& rail : result.Value().loft.rails) {
        interior += rail.side == LoftRailSide::Interior ? 1 : 0;
        Require(!rail.span.empty(), "どのガイドも断面の間で切り出してある");
    }
    Require(interior == 1, "内側は1本");
}

KACHA_V2_TEST(loft, 断面5本とガイド5本も受け入れる)
{
    const auto result = Accept(Grid(5, {0.0, 10.0, 20.0, 30.0, 40.0}), "断面5・ガイド5");
    Require(result.Value().loft.rails.size() == 5, "5本とも");
    Require(result.Value().sectionOrdering.chainIndices.size() == 5, "断面5本とも");
    Require(result.Value().crossings.size() == 25, "交わりは5×5");
}

KACHA_V2_TEST(loft, ガイドが一部の断面と交わらなければどれとどれかを言って断る)
{
    GuideSurfaceRequest request = Grid(4, {0.0, 20.0, 40.0});
    // ガイド 3 を持ち上げて、x = 100 の断面(断面 4)から離す。
    GuideChain& rail = request.chains[4 + 2];
    const auto last = rail.segments.back();
    rail.segments.back() = CurveSegment::MakeLine(last.StartPoint(),
        last.EndPoint() + Vector3{0, 0, 3.0}).Value();
    std::string code;
    const std::string why = RejectWhy(request, code);
    RequireEqual(code, std::string("GEO-G004"), "つながらない");
    Require(why.find("ガイド 3") != std::string::npos && why.find("断面 4") != std::string::npos,
        "「ガイド 3が断面 4と交わっていません」と言う: " + why);
}

KACHA_V2_TEST(loft, ガイドの間で断面の順が逆転していれば断る)
{
    // ガイド 1 は x = 0 → 50 → 100 の順に断面を横切る。ガイド 2 だけ断面 2 と 3 の
    // 位置で折り返して、横切る順が 1 → 3 → 2 になるようにする。
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    request.chains.push_back(Path(ChainRole::Section, 1, {{0, 0, 0}, {0, 20, 5}, {0, 40, 0}}));
    request.chains.push_back(Path(ChainRole::Section, 2,
        {{50, 0, 0}, {50, 20, 5}, {100, 40, 0}}));
    request.chains.push_back(Path(ChainRole::Section, 3,
        {{100, 0, 0}, {100, 20, 5}, {50, 40, 0}}));
    request.chains.push_back(Path(ChainRole::GuideU, 1, {{0, 0, 0}, {50, 0, 0}, {100, 0, 0}}));
    request.chains.push_back(Path(ChainRole::GuideU, 2, {{0, 40, 0}, {50, 40, 0}, {100, 40, 0}}));
    std::string code;
    const std::string why = RejectWhy(request, code);
    RequireEqual(code, std::string("GEO-G006"), "ねじれ");
    Require(why.find("逆転") != std::string::npos, "逆転していると言う: " + why);
}

KACHA_V2_TEST(loft, ガイドが端の断面からはみ出していれば断る)
{
    GuideSurfaceRequest request = Grid(3, {20.0});
    GuideChain& rail = request.chains[3];
    const auto first = rail.segments.front();
    rail.segments.insert(rail.segments.begin(),
        CurveSegment::MakeLine(first.StartPoint() - Vector3{15, 0, 0}, first.StartPoint())
            .Value());
    std::string code;
    const std::string why = RejectWhy(request, code);
    Require(why.find("はみ出して") != std::string::npos, "はみ出していると言う: " + why);
}

KACHA_V2_TEST(loft, 手動固定の順はガイドに沿った順と合っていれば使い合わなければ断る)
{
    GuideSurfaceRequest reversed = Grid(3, {20.0});
    std::swap(reversed.chains[0], reversed.chains[2]);   // 断面を 3,2,1 の順で渡す
    reversed.keepSectionOrder = true;
    const auto result = Accept(reversed, "逆順の手動固定");
    Require(result.Value().sectionOrdering.chainIndices.front() == 0,
        "渡した順(逆順)のまま使う");

    GuideSurfaceRequest shuffled = Grid(3, {20.0});
    std::swap(shuffled.chains[0], shuffled.chains[1]);   // 2,1,3
    shuffled.keepSectionOrder = true;
    std::string code;
    const std::string why = RejectWhy(shuffled, code);
    Require(why.find("手動で固定した断面の順") != std::string::npos,
        "ガイドに沿った順と合わないと言う: " + why);
}

KACHA_V2_TEST(loft, 中心線は0か1本で1本なら中心線に沿って運ぶ)
{
    GuideSurfaceRequest request = Grid(3, {});
    request.chains.push_back(Path(ChainRole::Centerline, 1, {{-5, 20, 0}, {105, 20, 0}}));
    const auto result = Accept(request, "中心線1本");
    Require(result.Value().loft.solver == LoftSolver::Centerline, "中心線に沿って運ぶ");
    Require(result.Value().loft.hasCenterline, "中心線あり");

    request.chains.push_back(Path(ChainRole::Centerline, 2, {{-5, 25, 0}, {105, 25, 0}}));
    std::string code;
    const std::string why = RejectWhy(request, code);
    Require(why.find("中心線") != std::string::npos && why.find("1 本まで") != std::string::npos,
        "中心線は1本までと言う: " + why);
}

KACHA_V2_TEST(loft, 断面1本は両端のガイドがあるときだけ作れる)
{
    const auto sweep = Accept(Grid(3, {0.0, 40.0}), "準備");
    (void)sweep;
    GuideSurfaceRequest one = Grid(3, {0.0, 40.0});
    one.chains.erase(one.chains.begin(), one.chains.begin() + 2);   // 断面 3 だけ残す
    one.chains[0].index = 1;
    const auto accepted = Accept(one, "断面1本 + 両端のガイド2本");
    // 断面はガイドの終わり(x = 100)にある。ガイドの始まりの側に仮想断面を足して網にする。
    Require(accepted.Value().loft.solver == LoftSolver::RailNetwork, "仮想断面を足して網にする");
    Require(accepted.Value().virtualSectionParameters.size() == 1
            && !accepted.Value().loft.virtualBefore.empty(),
        "仮想断面は 1 本(断面の無い側だけ)");

    GuideSurfaceRequest noVirtual = one;
    noVirtual.createVirtualEndSections = false;
    const auto swept = Accept(noVirtual, "仮想断面を作らない設定");
    Require(swept.Value().loft.solver == LoftSolver::TwoRailSweep,
        "仮想断面を作らない設定なら、従来どおり 2 本のレールで掃く");

    GuideSurfaceRequest lonely = Grid(3, {20.0});
    lonely.chains.erase(lonely.chains.begin(), lonely.chains.begin() + 2);
    lonely.chains[0].index = 1;
    std::string code;
    const std::string why = RejectWhy(lonely, code);
    Require(why.find("2 本以上") != std::string::npos, "断面が足りないと言う: " + why);
}

KACHA_V2_TEST(loft, 片方のガイドだけ端の断面より外へ伸びていれば従来の2本レール)
{
    GuideSurfaceRequest request = Grid(3, {0.0, 40.0});
    GuideChain& rail = request.chains[3];   // ガイド 1(y = 0)だけ x = -15 まで伸ばす
    const auto first = rail.segments.front();
    rail.segments.insert(rail.segments.begin(),
        CurveSegment::MakeLine(first.StartPoint() - Vector3{15, 0, 0}, first.StartPoint())
            .Value());
    const auto result = Accept(request, "片方だけ伸びたガイド");
    // 片側だけの三角の部分は網の四角にならない。仮想断面は作らず、従来の掃き方に戻す。
    Require(result.Value().loft.solver == LoftSolver::TwoRailSweep, "2 本のレールで掃く");
    Require(result.Value().loft.virtualBefore.empty() && result.Value().loft.virtualAfter.empty(),
        "仮想断面の線は作らない");
}

KACHA_V2_TEST(loft, 両方のガイドが伸びていれば端の断面と相似な仮想断面を足す)
{
    GuideSurfaceRequest request = Grid(3, {0.0, 40.0});
    for (const std::size_t at : {std::size_t{3}, std::size_t{4}}) {
        GuideChain& rail = request.chains[at];
        const auto last = rail.segments.back();
        rail.segments.push_back(
            CurveSegment::MakeLine(last.EndPoint(), last.EndPoint() + Vector3{20, 0, 0}).Value());
    }
    const auto result = Accept(request, "両方のガイドが x = 120 まで伸びる");
    Require(result.Value().loft.solver == LoftSolver::RailNetwork, "網にする");
    const auto& added = result.Value().loft.virtualAfter;
    Require(!added.empty() && result.Value().loft.virtualBefore.empty(),
        "伸びた側(最後の断面の側)だけに仮想断面");
    // 端の断面(x = 100)をそのまま x = 120 へ運んだ形になる(ガイドは平行なので、回さず縮めない)。
    const Vector3 start = added.front().StartPoint();
    const Vector3 end = added.back().EndPoint();
    Require(std::abs(start.x - 120.0) < 1.0e-6 && std::abs(start.y) < 1.0e-6,
        "始点は始点側のガイドの端");
    Require(std::abs(end.x - 120.0) < 1.0e-6 && std::abs(end.y - 40.0) < 1.0e-6,
        "終点は終点側のガイドの端");
    double top = 0.0;
    for (const CurveSegment& segment : added) {
        top = std::max(top, segment.EndPoint().z);
        Require(std::abs(segment.EndPoint().x - 120.0) < 1.0e-6, "断面の平面ごと運ぶ");
    }
    Require(std::abs(top - Height(100.0, 20.0)) < 0.05, "高さは端の断面と同じ");
    const auto network = LoftNetworkRequest(request, result.Value());
    int us = 0;
    int vs = 0;
    for (const GuideChain& chain : network.chains) {
        us += chain.role == ChainRole::GuideU ? 1 : 0;
        vs += chain.role == ChainRole::GuideV ? 1 : 0;
    }
    Require(us == 2 && vs == 4, "網は U 2 本(ガイド)と V 4 本(断面 3 + 仮想断面 1)");
}

KACHA_V2_TEST(loft, はしご形は網にして断面の間で波打たない)
{
    // 2026-09-22 の PC の撮影: 両端のガイド 2 本と断面 3 本(高さ 12・16・10 のアーチ)を
    // 2 本のレールで掃くと、断面の間で面が波打った(山が 3 つのはずが 7 つ)。
    // 網(Gordon)の格子で、断面の真ん中の筋が断面の高さの間をなめらかに移ることを見る。
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    const double xs[3] = {0.0, 40.0, 80.0};
    const double heights[3] = {12.0, 16.0, 10.0};
    for (int s = 0; s < 3; ++s) {
        const double lift = heights[s] / 0.75;   // 3 次ベジェの山は制御点の高さの 3/4
        GuideChain chain;
        chain.role = ChainRole::Section;
        chain.index = s + 1;
        chain.segments.push_back(CurveSegment::MakeCubicBezier({Vector3{xs[s], 0.0, 0.0},
            Vector3{xs[s], 0.0, lift}, Vector3{xs[s], 40.0, lift}, Vector3{xs[s], 40.0, 0.0}})
                .Value());
        request.chains.push_back(chain);
    }
    request.chains.push_back(Path(ChainRole::GuideU, 1, {{0, 0, 0}, {80, 0, 0}}));
    request.chains.push_back(Path(ChainRole::GuideU, 2, {{0, 40, 0}, {80, 40, 0}}));
    const auto analysis = Accept(request, "はしご形");
    Require(analysis.Value().loft.solver == LoftSolver::RailNetwork, "網にする");
    const auto grid = BuildGordonGrid(LoftNetworkRequest(request, analysis.Value()), Tolerance(), 33);
    Require(grid.HasValue(), "網の格子を作れる");
    const auto& g = grid.Value();
    std::size_t middle = 0;
    for (std::size_t row = 1; row < g.rows; ++row) {
        if (std::abs(g.vParameters[row] - 0.5) < std::abs(g.vParameters[middle] - 0.5)) {
            middle = row;
        }
    }
    double lowest = 1.0e9;
    double highest = -1.0e9;
    int peaks = 0;
    for (std::size_t column = 0; column < g.columns; ++column) {
        const Vector3 here = g.At(middle, column);
        lowest = std::min(lowest, here.z);
        highest = std::max(highest, here.z);
        Require(std::abs(here.y - 20.0) < 0.05, "真ん中の筋は y = 20 を通る");
        if (column > 0) {
            Require(here.x > g.At(middle, column - 1).x, "x は折り返さない");
        }
        if (column > 0 && column + 1 < g.columns) {
            const double before = g.At(middle, column - 1).z;
            const double after = g.At(middle, column + 1).z;
            peaks += here.z > before + 1.0e-9 && here.z >= after ? 1 : 0;
        }
    }
    Require(peaks == 1, "山は 1 つ(波打たない): " + std::to_string(peaks));
    Require(highest <= 16.0 * 1.02 && lowest >= 10.0 * 0.98,
        "高さは断面の高さ(10〜16)の間をなめらかに移る: " + std::to_string(lowest) + "〜"
            + std::to_string(highest));
}

KACHA_V2_TEST(loft, 個数の約束は1か所にあり固定の理由を持つ)
{
    using kachakacha::v2::modeling::SurfaceCardinalityOf;
    const auto* guides = SurfaceCardinalityOf(GuideSurfaceMethod::LoftSections, ChainRole::GuideU);
    Require(guides != nullptr && guides->minimum == 0 && guides->Unlimited(),
        "ロフトのガイドは 0〜任意");
    const auto* centerline =
        SurfaceCardinalityOf(GuideSurfaceMethod::LoftSections, ChainRole::Centerline);
    Require(centerline != nullptr && centerline->maximum == 1
            && std::string(centerline->fixedReasonJa).size() > 0,
        "中心線は 0〜1 で、固定の理由を言える");
    const auto* ruled = SurfaceCardinalityOf(GuideSurfaceMethod::RuledSections, ChainRole::Section);
    Require(ruled != nullptr && ruled->minimum == 2 && ruled->Unlimited(), "ルールドは 2〜任意");
    const auto* sides =
        SurfaceCardinalityOf(GuideSurfaceMethod::FourEdgePatch, ChainRole::BoundarySide);
    Require(sides != nullptr && sides->minimum == 4 && sides->maximum == 4, "四辺面の辺はちょうど 4");
}

KACHA_V2_TEST_MAIN("loft_input_tests")
