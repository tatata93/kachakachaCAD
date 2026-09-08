// 切れ目(§7)と型紙(§9)。
// 「勝手に縮めない」「勝手に裏返さない」「小片を落とさない」を確かめる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/PatternLayout.h"
#include "kachakacha/fabrication/ReliefCut.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::ApplyPlacement;
using kachakacha::v2::fabrication::AssignPartNumbers;
using kachakacha::v2::fabrication::CheckReliefRequired;
using kachakacha::v2::fabrication::FabricationSettings;
using kachakacha::v2::fabrication::FindPatternOverlaps;
using kachakacha::v2::fabrication::FoldSense;
using kachakacha::v2::fabrication::LayoutPattern;
using kachakacha::v2::fabrication::PaperSize;
using kachakacha::v2::fabrication::PatternLayerNameJa;
using kachakacha::v2::fabrication::PatternLayer;
using kachakacha::v2::fabrication::PatternPanel;
using kachakacha::v2::fabrication::PatternPlacement;
using kachakacha::v2::fabrication::ReliefCut;
using kachakacha::v2::fabrication::ReliefPanel;
using kachakacha::v2::fabrication::ReliefShape;
using kachakacha::v2::fabrication::StandardPaper;
using kachakacha::v2::fabrication::ValidateReliefCuts;
using kachakacha::v2::geometry::Point2;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] std::vector<Point2> Rectangle(double u0, double u1, double v0, double v1)
{
    return {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
}

//! 幅40、高さ60の部材。切れ目は下の縁から上へ入れる。
[[nodiscard]] ReliefPanel MakeReliefPanel()
{
    ReliefPanel panel;
    panel.panelId = "P1";
    panel.outline = Rectangle(0.0, 40.0, 0.0, 60.0);
    return panel;
}

[[nodiscard]] ReliefCut MakeCut(const std::string& id, double u, double depth)
{
    ReliefCut cut;
    cut.cutId = id;
    cut.centerPath = {{u, 0.0}, {u, depth}};
    cut.shape = ReliefShape::StraightSlit;
    return cut;
}

[[nodiscard]] FabricationSettings Settings()
{
    FabricationSettings settings;
    settings.maximumReliefDepthRatio = 0.55;
    settings.minimumLigamentMm = 0.5;
    return settings;
}

void RequireRejects(const ReliefPanel& panel, const std::vector<ReliefCut>& cuts,
    const FabricationSettings& settings, double target, const std::string& code,
    const std::string& why)
{
    const auto result = ValidateReliefCuts(panel, cuts, settings, target);
    Require(!result.HasValue(), "断ること: " + why);
    const bool found = std::any_of(result.Diagnostics().begin(), result.Diagnostics().end(),
        [&](const auto& diagnostic) { return diagnostic.code == code; });
    Require(found, "診断コード " + code + ": " + why + " (実際 "
            + result.Diagnostics().front().code + ")");
}

} // namespace

// ---------------------------------------------------------------- 切れ目

KACHA_V2_TEST(relief, 浅い切れ目は通る)
{
    const auto result =
        ValidateReliefCuts(MakeReliefPanel(), {MakeCut("c1", 20.0, 20.0)}, Settings(), 0.1);
    Require(result.HasValue(), "通ること ("
            + (result.Diagnostics().empty() ? std::string("診断なし")
                                            : result.Diagnostics().front().detailsJa)
            + ")");
    RequireCount(result.Value().acceptedCutIds.size(), 1, "通った数");
    // 幅60(下から上まで)に対して深さ20 → 1/3。
    RequireNear(result.Value().maximumDepthRatio, 1.0 / 3.0, 1e-6, "深さの比");
}

KACHA_V2_TEST(relief, 深すぎる切れ目を断る)
{
    // 幅60に対して上限55% = 33mm。40mm は深すぎる。
    RequireRejects(MakeReliefPanel(), {MakeCut("c1", 20.0, 40.0)}, Settings(), 0.1,
        "FAB-C002", "深さ40mm");
}

KACHA_V2_TEST(relief, 残す幅が足りない切れ目を断る)
{
    FabricationSettings settings = Settings();
    settings.maximumReliefDepthRatio = 0.99;   // 深さの上限は外しておく
    settings.minimumLigamentMm = 5.0;
    // 60mm の板に 57mm 入れると、先に残るのは 3mm。
    RequireRejects(MakeReliefPanel(), {MakeCut("c1", 20.0, 57.0)}, settings, 0.1,
        "FAB-C005", "残り3mm");
}

KACHA_V2_TEST(relief, 開口へ入り込む切れ目を断る)
{
    ReliefPanel panel = MakeReliefPanel();
    panel.openings.push_back(Rectangle(15.0, 25.0, 10.0, 25.0));
    RequireRejects(panel, {MakeCut("c1", 20.0, 20.0)}, Settings(), 0.1, "FAB-C003",
        "開口を突っ切る");
}

KACHA_V2_TEST(relief, 開口を避ける切れ目は通る)
{
    ReliefPanel panel = MakeReliefPanel();
    panel.openings.push_back(Rectangle(15.0, 25.0, 10.0, 25.0));
    const auto result =
        ValidateReliefCuts(panel, {MakeCut("c1", 5.0, 20.0)}, Settings(), 0.1);
    Require(result.HasValue(), "避けていれば通ること");
}

KACHA_V2_TEST(relief, 近すぎる切れ目どうしを断る)
{
    FabricationSettings settings = Settings();
    settings.minimumLigamentMm = 3.0;
    RequireRejects(MakeReliefPanel(),
        {MakeCut("c1", 20.0, 20.0), MakeCut("c2", 21.0, 20.0)}, settings, 0.1, "FAB-C004",
        "1mm しか離れていない");
}

KACHA_V2_TEST(relief, 十分離れた切れ目は通る)
{
    const auto result = ValidateReliefCuts(MakeReliefPanel(),
        {MakeCut("c1", 10.0, 20.0), MakeCut("c2", 30.0, 20.0)}, Settings(), 0.1);
    Require(result.HasValue(), "通ること");
    RequireCount(result.Value().acceptedCutIds.size(), 2, "通った数");
}

KACHA_V2_TEST(relief, 組立後に合わない切れ目を断る)
{
    ReliefCut cut = MakeCut("c1", 20.0, 20.0);
    cut.shape = ReliefShape::CurvedVNotch;
    cut.mateGapMm = 0.8;
    RequireRejects(MakeReliefPanel(), {cut}, Settings(), 0.1, "FAB-C006", "隙間0.8mm");

    cut.mateGapMm = 0.05;
    Require(ValidateReliefCuts(MakeReliefPanel(), {cut}, Settings(), 0.1).HasValue(),
        "目標以内なら通ること");
}

KACHA_V2_TEST(relief, 切れ目が必要なのに止めていたら知らせる)
{
    FabricationSettings settings = Settings();
    settings.reliefCutsEnabled = false;
    const auto diagnostic = CheckReliefRequired(settings, true);
    Require(diagnostic.has_value(), "知らせること");
    RequireEqual(diagnostic->code, std::string("FAB-C001"), "診断コード");

    Require(!CheckReliefRequired(settings, false).has_value(), "要らなければ黙っていること");
    settings.reliefCutsEnabled = true;
    Require(!CheckReliefRequired(settings, true).has_value(), "使えるなら黙っていること");
}

KACHA_V2_TEST(relief, 壊れた切れ目を断る)
{
    ReliefPanel broken;
    broken.panelId = "X";
    broken.outline = {{0.0, 0.0}, {1.0, 0.0}};
    Require(!ValidateReliefCuts(broken, {}, Settings(), 0.1).HasValue(), "外周が足りない");

    ReliefCut single;
    single.cutId = "c";
    single.centerPath = {{5.0, 0.0}};
    Require(!ValidateReliefCuts(MakeReliefPanel(), {single}, Settings(), 0.1).HasValue(),
        "点が1つの切れ目");

    ReliefCut nan = MakeCut("c", 20.0, 20.0);
    nan.centerPath[1].v = std::nan("");
    Require(!ValidateReliefCuts(MakeReliefPanel(), {nan}, Settings(), 0.1).HasValue(),
        "NaN を断ること");
}

// ---------------------------------------------------------------- 型紙

namespace {

[[nodiscard]] PatternPanel MakePatternPanel(const std::string& id, double width,
    double height)
{
    PatternPanel panel;
    panel.panelId = id;
    panel.outline = Rectangle(0.0, width, 0.0, height);
    return panel;
}

} // namespace

KACHA_V2_TEST(pattern, 部材を用紙へ並べる)
{
    const std::vector<PatternPanel> panels{MakePatternPanel("a", 80.0, 60.0),
        MakePatternPanel("b", 80.0, 60.0), MakePatternPanel("c", 50.0, 40.0)};
    const auto paper = StandardPaper("a4");
    Require(paper.has_value(), "A4が引けること");
    const auto layout = LayoutPattern(panels, *paper);
    Require(layout.HasValue(), "並べられること");
    RequireCount(layout.Value().placements.size(), 3, "配置の数");
    RequireCount(static_cast<std::size_t>(layout.Value().pageCount), 1, "ページ数");

    // すべて用紙の中に収まっていること。
    for (const auto& placement : layout.Value().placements) {
        const auto found = std::find_if(panels.begin(), panels.end(),
            [&](const PatternPanel& panel) { return panel.panelId == placement.panelId; });
        const std::vector<Point2> moved = ApplyPlacement(found->outline, placement);
        for (const Point2& point : moved) {
            Require(point.u >= paper->marginMm - 1e-9 && point.u <= paper->widthMm,
                placement.panelId + " が横に収まること");
            Require(point.v >= paper->marginMm - 1e-9 && point.v <= paper->heightMm,
                placement.panelId + " が縦に収まること");
        }
    }
}

KACHA_V2_TEST(pattern, 並べても重ならない)
{
    std::vector<PatternPanel> panels;
    for (int index = 0; index < 8; ++index) {
        panels.push_back(MakePatternPanel("p" + std::to_string(index), 60.0, 40.0));
    }
    const auto layout = LayoutPattern(panels, *StandardPaper("a4"));
    Require(layout.HasValue(), "並べられること");
    const auto overlaps = FindPatternOverlaps(panels, layout.Value());
    Require(overlaps.hits.empty(),
        "重なりが無いこと (" + std::to_string(overlaps.hits.size()) + " 件)");
}

KACHA_V2_TEST(pattern, 入りきらなければページを足す)
{
    std::vector<PatternPanel> panels;
    for (int index = 0; index < 12; ++index) {
        panels.push_back(MakePatternPanel("p" + std::to_string(index), 190.0, 90.0));
    }
    const auto layout = LayoutPattern(panels, *StandardPaper("a4"));
    Require(layout.HasValue(), "並べられること");
    Require(layout.Value().pageCount > 1,
        "ページが増えること (" + std::to_string(layout.Value().pageCount) + ")");
    const auto overlaps = FindPatternOverlaps(panels, layout.Value());
    Require(overlaps.hits.empty(), "重なりが無いこと");
}

KACHA_V2_TEST(pattern, 用紙に入らない部材を縮めずに断る)
{
    // §9「自動配置で部材を拡大縮小、鏡像反転してはならない」。
    const std::vector<PatternPanel> panels{MakePatternPanel("huge", 500.0, 500.0)};
    const auto layout = LayoutPattern(panels, *StandardPaper("a4"));
    Require(!layout.HasValue(), "断ること");
    RequireEqual(layout.Diagnostics().front().code, std::string("FAB-P001"), "診断コード");
    Require(layout.Diagnostics().front().detailsJa.find("縮小はしません")
            != std::string::npos,
        "縮めないと言うこと");
}

KACHA_V2_TEST(pattern, 縦長の部材は90度回して入れる)
{
    // A4 の使える幅は 200mm、高さは 287mm。
    // 250 × 100 の部材は、そのままでは横に入らないが、回せば入る。
    const std::vector<PatternPanel> panels{MakePatternPanel("tall", 250.0, 100.0)};
    const auto layout = LayoutPattern(panels, *StandardPaper("a4"));
    Require(layout.HasValue(), "並べられること");
    RequireNear(std::abs(layout.Value().placements.front().rotationRad), 1.5707963267948966,
        1e-9, "90度回すこと");
}

KACHA_V2_TEST(pattern, 配置で大きさが変わらない)
{
    // 回転と平行移動だけなので、辺の長さは1つも変わらない。
    const PatternPanel panel = MakePatternPanel("a", 80.0, 60.0);
    for (const double rotation : {0.0, 0.3, 1.5707963267948966, 2.0, 5.5}) {
        PatternPlacement placement;
        placement.panelId = "a";
        placement.rotationRad = rotation;
        placement.translationMm = Point2{13.0, -7.0};
        const std::vector<Point2> moved = ApplyPlacement(panel.outline, placement);
        for (std::size_t index = 0; index < panel.outline.size(); ++index) {
            const std::size_t next = (index + 1) % panel.outline.size();
            const double before = std::sqrt(
                std::pow(panel.outline[next].u - panel.outline[index].u, 2.0)
                + std::pow(panel.outline[next].v - panel.outline[index].v, 2.0));
            const double after = std::sqrt(std::pow(moved[next].u - moved[index].u, 2.0)
                + std::pow(moved[next].v - moved[index].v, 2.0));
            RequireNear(after, before, 1e-9, "辺の長さ");
        }
    }
}

KACHA_V2_TEST(pattern, 配置で鏡像にならない)
{
    // 向き(符号つき面積)が変わらないこと。
    const PatternPanel panel = MakePatternPanel("a", 80.0, 60.0);
    const double reference = kachakacha::v2::geometry::SignedArea(panel.outline);
    for (const double rotation : {0.0, 0.7, 2.5, 4.0}) {
        PatternPlacement placement;
        placement.panelId = "a";
        placement.rotationRad = rotation;
        const double area =
            kachakacha::v2::geometry::SignedArea(ApplyPlacement(panel.outline, placement));
        RequireNear(area, reference, 1e-9, "符号つき面積が変わらないこと");
    }
}

KACHA_V2_TEST(pattern, 部材番号が安定している)
{
    const std::vector<PatternPanel> panels{MakePatternPanel("roof", 80.0, 60.0),
        MakePatternPanel("side", 70.0, 50.0), MakePatternPanel("front", 60.0, 40.0)};
    const auto first = AssignPartNumbers(panels);

    // 渡す順を変えても同じ番号。
    const std::vector<PatternPanel> shuffled{panels[2], panels[0], panels[1]};
    const auto second = AssignPartNumbers(shuffled);
    Require(first == second, "順番に依らないこと");
    // 1から始まる連番。
    for (std::size_t index = 0; index < first.size(); ++index) {
        RequireCount(static_cast<std::size_t>(first[index].second), index + 1, "連番");
    }
}

KACHA_V2_TEST(pattern, 何度並べても同じ結果になる)
{
    std::vector<PatternPanel> panels;
    for (int index = 0; index < 6; ++index) {
        panels.push_back(MakePatternPanel("p" + std::to_string(index),
            50.0 + index * 5.0, 40.0));
    }
    const auto first = LayoutPattern(panels, *StandardPaper("a4"));
    Require(first.HasValue(), "並べられること");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = LayoutPattern(panels, *StandardPaper("a4"));
        Require(again.HasValue(), "毎回並べられること");
        for (std::size_t index = 0; index < first.Value().placements.size(); ++index) {
            RequireEqual(again.Value().placements[index].panelId,
                first.Value().placements[index].panelId, "並び");
            RequireNear(again.Value().placements[index].translationMm.u,
                first.Value().placements[index].translationMm.u, 0.0, "位置が完全に同じ");
        }
    }
}

KACHA_V2_TEST(pattern, 渡す順を変えても同じ配置になる)
{
    std::vector<PatternPanel> panels;
    for (int index = 0; index < 6; ++index) {
        panels.push_back(MakePatternPanel("p" + std::to_string(index),
            50.0 + index * 5.0, 40.0));
    }
    const auto first = LayoutPattern(panels, *StandardPaper("a4"));
    std::vector<PatternPanel> shuffled(panels.rbegin(), panels.rend());
    const auto second = LayoutPattern(shuffled, *StandardPaper("a4"));
    Require(first.HasValue() && second.HasValue(), "どちらも並べられること");
    RequireCount(second.Value().placements.size(), first.Value().placements.size(), "数");
    for (std::size_t index = 0; index < first.Value().placements.size(); ++index) {
        RequireEqual(second.Value().placements[index].panelId,
            first.Value().placements[index].panelId, "同じ部材が同じ位置");
        RequireNear(second.Value().placements[index].translationMm.v,
            first.Value().placements[index].translationMm.v, 1e-12, "位置");
    }
}

KACHA_V2_TEST(pattern, 重なりを見つけられる)
{
    const std::vector<PatternPanel> panels{MakePatternPanel("a", 80.0, 60.0),
        MakePatternPanel("b", 80.0, 60.0)};
    kachakacha::v2::fabrication::PatternLayoutResult layout;
    layout.pageCount = 1;
    layout.placements.push_back({"a", Point2{10.0, 10.0}, 0.0, 0, 1});
    layout.placements.push_back({"b", Point2{50.0, 30.0}, 0.0, 0, 2});   // わざと重ねる
    const auto overlaps = FindPatternOverlaps(panels, layout);
    Require(!overlaps.hits.empty(), "重なりを見つけること");
    RequireEqual(overlaps.hits.front().firstPanelId, std::string("a"), "1つ目");
    RequireEqual(overlaps.hits.front().secondPanelId, std::string("b"), "2つ目");
}

KACHA_V2_TEST(pattern, 別のページなら重なり扱いしない)
{
    const std::vector<PatternPanel> panels{MakePatternPanel("a", 80.0, 60.0),
        MakePatternPanel("b", 80.0, 60.0)};
    kachakacha::v2::fabrication::PatternLayoutResult layout;
    layout.pageCount = 2;
    layout.placements.push_back({"a", Point2{10.0, 10.0}, 0.0, 0, 1});
    layout.placements.push_back({"b", Point2{10.0, 10.0}, 0.0, 1, 2});
    Require(FindPatternOverlaps(panels, layout).hits.empty(), "別ページなら重ならない");
}

KACHA_V2_TEST(pattern, 用紙の種類を引ける)
{
    Require(StandardPaper("a4").has_value(), "A4");
    Require(StandardPaper("a3").has_value(), "A3");
    Require(StandardPaper("b4").has_value(), "B4");
    Require(StandardPaper("letter").has_value(), "レター");
    Require(!StandardPaper("unknown").has_value(), "知らない用紙は無い");
    RequireNear(StandardPaper("a4")->widthMm, 210.0, 1e-9, "A4の幅");
    RequireNear(StandardPaper("a3")->heightMm, 420.0, 1e-9, "A3の高さ");
}

KACHA_V2_TEST(pattern, おかしな用紙を断る)
{
    const std::vector<PatternPanel> panels{MakePatternPanel("a", 10.0, 10.0)};
    PaperSize bad;
    bad.widthMm = 0.0;
    Require(!LayoutPattern(panels, bad).HasValue(), "幅0の用紙");
    bad = *StandardPaper("a4");
    bad.marginMm = 200.0;
    Require(!LayoutPattern(panels, bad).HasValue(), "余白が用紙より大きい");
    Require(!LayoutPattern({}, *StandardPaper("a4")).HasValue(), "部材が無い");
}

KACHA_V2_TEST(pattern, レイヤーの名前がある)
{
    const PatternLayer layers[]{PatternLayer::Outline, PatternLayer::Fold, PatternLayer::Cut,
        PatternLayer::Opening, PatternLayer::Annotation};
    for (const PatternLayer layer : layers) {
        Require(!PatternLayerNameJa(layer).empty(), "名前があること");
    }
}

KACHA_V2_TEST(relief, 指定した向きはそのまま使う)
{
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ChooseReliefDirection;
    using kachakacha::v2::fabrication::PanelCurvature;
    PanelCurvature curvature;
    curvature.alongUPerMm = 0.05;
    curvature.alongVPerMm = 0.001;
    curvature.widthUMm = 100.0;
    curvature.widthVMm = 50.0;
    for (BendDirection wanted : {BendDirection::U, BendDirection::V}) {
        kachakacha::v2::fabrication::FabricationSettings settings;
        settings.reliefDirection = wanted;
        const auto choice = ChooseReliefDirection(settings, curvature);
        Require(choice.HasValue(), "決まる");
        Require(choice.Value().direction == wanted, "指定どおり");
        Require(!choice.Value().reasonJa.empty(), "理由が出る");
        Require(!choice.Value().evaluatesBoth, "片方だけ");
    }
}

KACHA_V2_TEST(relief, 両方を指定したら両方を候補にする)
{
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ChooseReliefDirection;
    using kachakacha::v2::fabrication::PanelCurvature;
    using kachakacha::v2::fabrication::ReliefAdvanceDirections;
    kachakacha::v2::fabrication::FabricationSettings settings;
    settings.reliefDirection = BendDirection::Both;
    PanelCurvature curvature;
    curvature.alongUPerMm = 0.05;
    curvature.alongVPerMm = 0.001;
    const auto choice = ChooseReliefDirection(settings, curvature);
    Require(choice.HasValue(), "決まる");
    Require(choice.Value().direction == BendDirection::Both, "両方");
    Require(choice.Value().evaluatesBoth, "両方を評価する");
    // 片方へ固定されていないこと。進行方向が2本出る。
    const auto directions = ReliefAdvanceDirections(choice.Value());
    Require(directions.HasValue(), "向きが出る");
    RequireCount(directions.Value().size(), 2, "U と V の2本");
    Require(directions.Value()[0].u != directions.Value()[1].u
            || directions.Value()[0].v != directions.Value()[1].v,
        "2本は別の向き");
}

KACHA_V2_TEST(relief, 自動は曲率の大きい向きへ直交して切る)
{
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ChooseReliefDirection;
    using kachakacha::v2::fabrication::PanelCurvature;
    kachakacha::v2::fabrication::FabricationSettings settings;
    settings.reliefDirection = BendDirection::Auto;

    PanelCurvature curvedInU;
    curvedInU.alongUPerMm = 0.05;
    curvedInU.alongVPerMm = 0.001;
    const auto forU = ChooseReliefDirection(settings, curvedInU);
    Require(forU.HasValue(), "決まる");
    Require(forU.Value().direction == BendDirection::V, "U に曲がるなら V へ切る");
    Require(forU.Value().reasonJa.find("直交") != std::string::npos, "理由を言う");

    PanelCurvature curvedInV;
    curvedInV.alongUPerMm = 0.001;
    curvedInV.alongVPerMm = 0.05;
    const auto forV = ChooseReliefDirection(settings, curvedInV);
    Require(forV.HasValue(), "決まる");
    Require(forV.Value().direction == BendDirection::U, "V に曲がるなら U へ切る");
}

KACHA_V2_TEST(relief, 自動は縦だけに固定しない)
{
    // 契約が名指しで禁じている「縦だけに固定」を、入力を変えて確かめる。
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ChooseReliefDirection;
    using kachakacha::v2::fabrication::PanelCurvature;
    kachakacha::v2::fabrication::FabricationSettings settings;
    settings.reliefDirection = BendDirection::Auto;
    std::set<int> seen;
    for (double ratio : {0.01, 0.1, 0.5, 0.8, 1.0, 1.25, 2.0, 10.0, 100.0}) {
        PanelCurvature curvature;
        curvature.alongUPerMm = 0.02 * ratio;
        curvature.alongVPerMm = 0.02;
        curvature.widthUMm = 80.0;
        curvature.widthVMm = 60.0;
        const auto choice = ChooseReliefDirection(settings, curvature);
        Require(choice.HasValue(), "決まる");
        seen.insert(static_cast<int>(choice.Value().direction));
        Require(choice.Value().direction != BendDirection::Auto, "自動のままにしない");
    }
    Require(seen.size() >= 3, "U と V と Both のどれもが出ること");
}

KACHA_V2_TEST(relief, 二重曲率が同じくらいなら両方を候補にする)
{
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ChooseReliefDirection;
    using kachakacha::v2::fabrication::PanelCurvature;
    kachakacha::v2::fabrication::FabricationSettings settings;
    settings.reliefDirection = BendDirection::Auto;
    PanelCurvature curvature;
    curvature.alongUPerMm = 0.030;
    curvature.alongVPerMm = 0.028;
    const auto choice = ChooseReliefDirection(settings, curvature);
    Require(choice.HasValue(), "決まる");
    Require(choice.Value().direction == BendDirection::Both, "両方");
    Require(choice.Value().evaluatesBoth, "両方を評価する");
}

KACHA_V2_TEST(relief, 平らな部材では幅の広い方向を選ぶ)
{
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ChooseReliefDirection;
    using kachakacha::v2::fabrication::PanelCurvature;
    kachakacha::v2::fabrication::FabricationSettings settings;
    settings.reliefDirection = BendDirection::Auto;
    PanelCurvature flat;
    flat.widthUMm = 30.0;
    flat.widthVMm = 90.0;
    const auto choice = ChooseReliefDirection(settings, flat);
    Require(choice.HasValue(), "決まる");
    Require(choice.Value().direction == BendDirection::V, "広い方");
    Require(choice.Value().reasonJa.find("曲がっていない") != std::string::npos,
        "理由を言う");
}

KACHA_V2_TEST(relief, 壊れた曲率は断る)
{
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ChooseReliefDirection;
    using kachakacha::v2::fabrication::PanelCurvature;
    kachakacha::v2::fabrication::FabricationSettings settings;
    settings.reliefDirection = BendDirection::Auto;
    PanelCurvature broken;
    broken.alongUPerMm = -1.0;
    const auto refused = ChooseReliefDirection(settings, broken);
    Require(!refused.HasValue(), "断る");
    RequireEqual(refused.Diagnostics().front().code, "FAB-C007", "向きが決まらない");
}

KACHA_V2_TEST(relief, 自動のまま向きを聞いたら断る)
{
    using kachakacha::v2::fabrication::BendDirection;
    using kachakacha::v2::fabrication::ReliefAdvanceDirections;
    using kachakacha::v2::fabrication::ReliefDirectionChoice;
    ReliefDirectionChoice choice;
    choice.direction = BendDirection::Auto;
    const auto refused = ReliefAdvanceDirections(choice);
    Require(!refused.HasValue(), "断る");
    RequireEqual(refused.Diagnostics().front().code, "FAB-C007", "自動のまま");
}

namespace {

using kachakacha::v2::fabrication::CorrespondReliefSides;
using kachakacha::v2::fabrication::ReliefMateCorrespondence;

//! まっすぐな側辺。長さを指定して作る。
[[nodiscard]] std::vector<Point2> StraightSide(double startU, double length, double v)
{
    std::vector<Point2> side;
    for (int at = 0; at <= 8; ++at) {
        side.push_back(Point2{startU + length * at / 8.0, v});
    }
    return side;
}

} // namespace

KACHA_V2_TEST(relief, 直線の切れ目は隙間なく閉じる)
{
    kachakacha::v2::fabrication::ReliefCut cut;
    cut.cutId = "c1";
    cut.shape = ReliefShape::StraightSlit;
    cut.centerPath = StraightSide(0.0, 20.0, 0.0);
    const auto result = CorrespondReliefSides(cut, 0.05);
    Require(result.HasValue(), "対応が取れる");
    RequireNear(result.Value().maximumGapMm, 0.0, 1e-12, "隙間0");
    RequireNear(result.Value().leftLengthMm, result.Value().rightLengthMm, 1e-12,
        "左右同じ長さ");
    RequireCount(result.Value().leftParameters.size(),
        result.Value().rightParameters.size(), "対応の数が同じ");
}

KACHA_V2_TEST(relief, 直線の切れ目に側辺を入れたら断る)
{
    kachakacha::v2::fabrication::ReliefCut cut;
    cut.shape = ReliefShape::StraightSlit;
    cut.centerPath = StraightSide(0.0, 20.0, 0.0);
    cut.leftSide = StraightSide(0.0, 20.0, 0.5);
    const auto refused = CorrespondReliefSides(cut, 0.05);
    Require(!refused.HasValue(), "断る");
    RequireEqual(refused.Diagnostics().front().code, "FAB-C006", "側辺が余計");
}

KACHA_V2_TEST(relief, V字の左右が同じ長さなら閉じる)
{
    kachakacha::v2::fabrication::ReliefCut cut;
    cut.shape = ReliefShape::VNotch;
    cut.centerPath = StraightSide(0.0, 20.0, 0.0);
    cut.leftSide = StraightSide(0.0, 20.0, 0.0);
    cut.rightSide = StraightSide(0.0, 20.0, 0.0);
    const auto result = CorrespondReliefSides(cut, 0.05);
    Require(result.HasValue(), "対応が取れる");
    RequireNear(result.Value().maximumGapMm, 0.0, 1e-9, "隙間0");
    Require(result.Value().shape == ReliefShape::VNotch, "形を覚えている");
}

KACHA_V2_TEST(relief, 左右の長さが違えば隙間として出る)
{
    kachakacha::v2::fabrication::ReliefCut cut;
    cut.shape = ReliefShape::VNotch;
    cut.centerPath = StraightSide(0.0, 20.0, 0.0);
    cut.leftSide = StraightSide(0.0, 20.0, 0.0);
    cut.rightSide = StraightSide(0.0, 20.4, 0.0);
    // 目標を大きくすれば通る。隙間は長さの差(0.4mm)になる。
    const auto measured = CorrespondReliefSides(cut, 1.0);
    Require(measured.HasValue(), "測れる");
    RequireNear(measured.Value().maximumGapMm, 0.4, 1e-9, "長さの差がそのまま隙間");
    RequireNear(measured.Value().leftLengthMm, 20.0, 1e-9, "左辺の長さは変えない");
    RequireNear(measured.Value().rightLengthMm, 20.4, 1e-9, "右辺の長さも変えない");
}

KACHA_V2_TEST(relief, 隙間が目標を超えたら片辺を縮めずに断る)
{
    kachakacha::v2::fabrication::ReliefCut cut;
    cut.shape = ReliefShape::VNotch;
    cut.centerPath = StraightSide(0.0, 20.0, 0.0);
    cut.leftSide = StraightSide(0.0, 20.0, 0.0);
    cut.rightSide = StraightSide(0.0, 22.0, 0.0);
    const auto refused = CorrespondReliefSides(cut, 0.05);
    Require(!refused.HasValue(), "断る");
    RequireEqual(refused.Diagnostics().front().code, "FAB-C006", "隙間が目標超え");
    const std::string& details = refused.Diagnostics().front().detailsJa;
    Require(details.find("左辺") != std::string::npos, "左辺の長さを言う");
    Require(details.find("右辺") != std::string::npos, "右辺の長さを言う");
    Require(details.find("縮めて") != std::string::npos, "縮めないと言う");
}

KACHA_V2_TEST(relief, 曲がったV字でも同じ決め方で対応が取れる)
{
    kachakacha::v2::fabrication::ReliefCut cut;
    cut.shape = ReliefShape::CurvedVNotch;
    cut.centerPath = StraightSide(0.0, 20.0, 0.0);
    // 左右とも同じ弧を描く。点の数は左右で違う。
    for (int at = 0; at <= 12; ++at) {
        const double t = at / 12.0;
        cut.leftSide.push_back(Point2{20.0 * t, 3.0 * std::sin(t * 3.14159265358979)});
    }
    for (int at = 0; at <= 40; ++at) {
        const double t = at / 40.0;
        cut.rightSide.push_back(Point2{20.0 * t, 3.0 * std::sin(t * 3.14159265358979)});
    }
    const auto result = CorrespondReliefSides(cut, 0.5);
    Require(result.HasValue(), "対応が取れる");
    // 点の数が違っても、正規化弧長で対応するので大きくはずれない。
    Require(result.Value().maximumGapMm < 0.5, "点の数が違っても収まる");
    RequireCount(result.Value().leftParameters.size(),
        result.Value().rightParameters.size(), "対応の数が同じ");
    for (std::size_t at = 0; at < result.Value().leftParameters.size(); ++at) {
        RequireNear(result.Value().leftParameters[at],
            result.Value().rightParameters[at], 1e-12, "同じ割合の点どうし");
    }
}

KACHA_V2_TEST(relief, 3つの形すべてで対応が取れる)
{
    for (ReliefShape shape : {ReliefShape::StraightSlit, ReliefShape::VNotch,
             ReliefShape::CurvedVNotch}) {
        kachakacha::v2::fabrication::ReliefCut cut;
        cut.shape = shape;
        cut.centerPath = StraightSide(0.0, 15.0, 0.0);
        if (shape != ReliefShape::StraightSlit) {
            cut.leftSide = StraightSide(0.0, 15.0, 0.0);
            cut.rightSide = StraightSide(0.0, 15.0, 0.0);
        }
        const auto result = CorrespondReliefSides(cut, 0.05);
        Require(result.HasValue(), "対応が取れる");
        RequireNear(result.Value().maximumGapMm, 0.0, 1e-9, "100%で隙間が目標以内");
    }
}

KACHA_V2_TEST(relief, 点が足りない側辺は断る)
{
    kachakacha::v2::fabrication::ReliefCut cut;
    cut.shape = ReliefShape::VNotch;
    cut.leftSide = {Point2{0.0, 0.0}};
    cut.rightSide = StraightSide(0.0, 10.0, 0.0);
    const auto refused = CorrespondReliefSides(cut, 0.05);
    Require(!refused.HasValue(), "断る");
    RequireEqual(refused.Diagnostics().front().code, "FAB-C006", "点が足りない");
}

KACHA_V2_TEST_MAIN("pattern_tests")
