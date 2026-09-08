// 組立スライダー(fabrication-contract.md §10)。
// 「頂点を個別に補間しない」が守られているか。守られていれば、しわは出ようがない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/Assembly.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AssemblyFold;
using kachakacha::v2::fabrication::AssemblyPanel;
using kachakacha::v2::fabrication::AssemblyState;
using kachakacha::v2::fabrication::CheckAssemblyMetric;
using kachakacha::v2::fabrication::EvaluateAssembly;
using kachakacha::v2::fabrication::FindAssemblyIntersections;
using kachakacha::v2::geometry::Point2;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] AssemblyPanel Rectangle(const std::string& id, double u0, double u1, double v0,
    double v1)
{
    AssemblyPanel panel;
    panel.panelId = id;
    panel.flatOutline = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    return panel;
}

//! 箱の展開図。底と、そこから四方へ伸びる4枚の側面。
struct BoxLayout {
    std::vector<AssemblyPanel> panels;
    std::vector<AssemblyFold> folds;
};

[[nodiscard]] BoxLayout MakeBox(double width, double depth, double height)
{
    BoxLayout layout;
    layout.panels.push_back(Rectangle("bottom", 0.0, width, 0.0, depth));
    layout.panels.push_back(Rectangle("front", 0.0, width, -height, 0.0));
    layout.panels.push_back(Rectangle("back", 0.0, width, depth, depth + height));
    layout.panels.push_back(Rectangle("left", -height, 0.0, 0.0, depth));
    layout.panels.push_back(Rectangle("right", width, width + height, 0.0, depth));

    const auto fold = [&](const std::string& id, const std::string& child, Point2 from,
                          Point2 to, double angle) {
        AssemblyFold made;
        made.foldId = id;
        made.parentPanelId = "bottom";
        made.childPanelId = child;
        made.hingeFrom = from;
        made.hingeTo = to;
        made.targetAngleRad = angle;
        layout.folds.push_back(made);
    };
    // 底の各辺を軸にして 90 度立てる。向きは軸の取り方で決まる。
    fold("f-front", "front", {0.0, 0.0}, {width, 0.0}, -kPi / 2.0);
    fold("f-back", "back", {width, depth}, {0.0, depth}, -kPi / 2.0);
    fold("f-left", "left", {0.0, depth}, {0.0, 0.0}, -kPi / 2.0);
    fold("f-right", "right", {width, 0.0}, {width, depth}, -kPi / 2.0);
    return layout;
}

//! 曲げる板。母線を等間隔に置き、全体で halfTurn ラジアン曲げる。
[[nodiscard]] AssemblyPanel BendingPanel(const std::string& id, double length, double width,
    int rulings, double totalAngle)
{
    AssemblyPanel panel;
    panel.panelId = id;
    panel.flatOutline = {{0.0, 0.0}, {length, 0.0}, {length, width}, {0.0, width}};
    for (int index = 1; index <= rulings; ++index) {
        panel.rulingUCoordinates.push_back(length * static_cast<double>(index)
            / static_cast<double>(rulings + 1));
    }
    // 母線1本がヒンジ1つ。合計で totalAngle になるように割る。
    const double perHinge = totalAngle / static_cast<double>(rulings);
    for (int index = 0; index < rulings; ++index) {
        panel.targetBendAngleRad.push_back(perHinge);
    }
    return panel;
}

} // namespace

KACHA_V2_TEST(assembly, ゼロパーセントは型紙のまま)
{
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{0.0});
    Require(result.HasValue(), "組み立てられること");
    RequireCount(result.Value().panels.size(), 5, "板の数");
    // 全部が平ら(z = 0)。
    for (const auto& panel : result.Value().panels) {
        for (const Vector3& point : panel.outline) {
            RequireNear(point.z, 0.0, 1e-12, panel.panelId + " が平らであること");
        }
    }
}

KACHA_V2_TEST(assembly, 百パーセントで箱になる)
{
    const double width = 40.0;
    const double depth = 30.0;
    const double height = 20.0;
    const BoxLayout box = MakeBox(width, depth, height);
    const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{100.0});
    Require(result.HasValue(), "組み立てられること");

    // 側面の上端が、すべて高さ height のところへ来ること。
    for (const auto& panel : result.Value().panels) {
        if (panel.panelId == "bottom") {
            for (const Vector3& point : panel.outline) {
                RequireNear(point.z, 0.0, 1e-9, "底は平ら");
            }
            continue;
        }
        double highest = panel.outline.front().z;
        for (const Vector3& point : panel.outline) {
            highest = std::max(highest, point.z);
        }
        RequireNear(highest, height, 1e-9, panel.panelId + " の高さ");
    }
}

KACHA_V2_TEST(assembly, 途中の状態でも辺の長さが変わらない)
{
    // §10.2「edgeLengthChangeRelative <= 1e-6」。
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    for (const double percent : {0.0, 1.0, 15.0, 30.0, 50.0, 73.5, 99.0, 100.0}) {
        const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{percent});
        Require(result.HasValue(), "組み立てられること");
        const auto check = CheckAssemblyMetric(box.panels, result.Value());
        Require(check.withinTolerance,
            std::to_string(percent) + "% で条件を満たすこと (辺 "
                + std::to_string(check.maximumEdgeLengthChangeRelative) + " / 大きさ "
                + std::to_string(check.maximumScaleError) + " / 裏返り "
                + std::to_string(check.flippedTriangleCount) + ")");
    }
}

KACHA_V2_TEST(assembly, 板の大きさがごくわずかも変わらない)
{
    // §10.2「rigidPanelScaleError <= 1e-9」。
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{42.0});
    Require(result.HasValue(), "組み立てられること");
    const auto check = CheckAssemblyMetric(box.panels, result.Value());
    Require(check.maximumScaleError <= 1e-9,
        "大きさが変わらないこと (" + std::to_string(check.maximumScaleError) + ")");
}

KACHA_V2_TEST(assembly, 進み方が角度に比例する)
{
    // §10.1「FoldLineは目標角度へ比例進行させる」。
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    const auto pick = [&](double percent) {
        const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{percent});
        Require(result.HasValue(), "組み立てられること");
        for (const auto& panel : result.Value().panels) {
            if (panel.panelId == "front") {
                // 前面の下端の高さ。折り角 a のとき height * sin(a)。
                double highest = panel.outline.front().z;
                for (const Vector3& point : panel.outline) {
                    highest = std::max(highest, point.z);
                }
                return highest;
            }
        }
        Require(false, "前面が見つかること");
        return 0.0;
    };
    for (const double percent : {10.0, 25.0, 50.0, 80.0}) {
        const double angle = (kPi / 2.0) * percent / 100.0;
        RequireNear(pick(percent), 20.0 * std::sin(angle), 1e-9,
            std::to_string(percent) + "% の高さ");
    }
}

KACHA_V2_TEST(assembly, 個別に折り角を指定できる)
{
    // §10.4「個別overrideがないFoldだけを更新する」。
    BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    for (auto& fold : box.folds) {
        if (fold.foldId == "f-front") {
            fold.progressPercentOverride = 100.0;
        }
    }
    const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{0.0});
    Require(result.HasValue(), "組み立てられること");
    for (const auto& panel : result.Value().panels) {
        double highest = panel.outline.front().z;
        for (const Vector3& point : panel.outline) {
            highest = std::max(highest, point.z);
        }
        if (panel.panelId == "front") {
            RequireNear(highest, 20.0, 1e-9, "前面だけ立っていること");
        } else {
            RequireNear(highest, 0.0, 1e-9, panel.panelId + " は平らなまま");
        }
    }
}

KACHA_V2_TEST(assembly, 曲げる板でも面内の距離が保たれる)
{
    // 母線で区切った帯を順に回すので、帯の中は剛体のまま。
    const std::vector<AssemblyPanel> panels{
        BendingPanel("curved", 100.0, 30.0, 9, kPi)};
    for (const double percent : {0.0, 20.0, 55.0, 100.0}) {
        const auto result = EvaluateAssembly(panels, {}, AssemblyState{percent});
        Require(result.HasValue(), "組み立てられること");
        const auto check = CheckAssemblyMetric(panels, result.Value());
        Require(check.maximumScaleError <= 1e-9,
            std::to_string(percent) + "% で帯が剛体であること ("
                + std::to_string(check.maximumScaleError) + ")");
        RequireCount(check.flippedTriangleCount, 0, "裏返りが無いこと");
    }
}

KACHA_V2_TEST(assembly, 曲げる板の展開長が変わらない)
{
    // 帯の長さの合計は、曲げても型紙のままであること。
    const AssemblyPanel panel = BendingPanel("curved", 100.0, 30.0, 9, kPi);
    const auto result = EvaluateAssembly({panel}, {}, AssemblyState{100.0});
    Require(result.HasValue(), "組み立てられること");
    const auto& strips = result.Value().panels.front().strips;
    RequireCount(strips.size(), 10, "帯の数");
    double total = 0.0;
    for (const auto& strip : strips) {
        // 各帯の長手方向の辺。
        total += (strip[1] - strip[0]).Length();
    }
    RequireNear(total, 100.0, 1e-9, "展開長");
}

KACHA_V2_TEST(assembly, 半周曲げると端が反対を向く)
{
    const AssemblyPanel panel = BendingPanel("curved", 100.0, 30.0, 19, kPi);
    const auto result = EvaluateAssembly({panel}, {}, AssemblyState{100.0});
    Require(result.HasValue(), "組み立てられること");
    const auto& strips = result.Value().panels.front().strips;
    const Vector3 firstDirection =
        kachakacha::v2::geometry::Normalized(strips.front()[1] - strips.front()[0]);
    const Vector3 lastDirection =
        kachakacha::v2::geometry::Normalized(strips.back()[1] - strips.back()[0]);
    RequireNear(Dot(firstDirection, lastDirection), -1.0, 1e-6, "向きが反対になること");
}

KACHA_V2_TEST(assembly, 板がぶつかっていれば知らせる)
{
    // 90度どころか、180度以上折ってしまい、板どうしが重なる場合。
    std::vector<AssemblyPanel> panels{Rectangle("a", 0.0, 40.0, 0.0, 30.0),
        Rectangle("b", 0.0, 40.0, -30.0, 0.0), Rectangle("c", 0.0, 40.0, 30.0, 60.0)};
    std::vector<AssemblyFold> folds;
    AssemblyFold first;
    first.foldId = "f1";
    first.parentPanelId = "a";
    first.childPanelId = "b";
    first.hingeFrom = {0.0, 0.0};
    first.hingeTo = {40.0, 0.0};
    first.targetAngleRad = -kPi * 0.98;   // ほとんど折り返す
    folds.push_back(first);
    AssemblyFold second;
    second.foldId = "f2";
    second.parentPanelId = "a";
    second.childPanelId = "c";
    second.hingeFrom = {40.0, 30.0};
    second.hingeTo = {0.0, 30.0};
    second.targetAngleRad = -kPi * 0.98;
    folds.push_back(second);

    const auto result = EvaluateAssembly(panels, folds, AssemblyState{100.0});
    Require(result.HasValue(), "組み立てられること");
    const auto report = FindAssemblyIntersections(result.Value(), folds, 1.0);
    Require(!report.hits.empty(), "重なりを見つけること");
    Require(report.hits.front().firstPanelId != report.hits.front().secondPanelId,
        "別の板どうしであること");
}

KACHA_V2_TEST(assembly, 隣り合う板は重なり扱いしない)
{
    // 折り線でつながっている板は境目で必ず接する。それを重なりと言ってはいけない。
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{100.0});
    Require(result.HasValue(), "組み立てられること");
    const auto report = FindAssemblyIntersections(result.Value(), box.folds, 0.01);
    for (const auto& hit : report.hits) {
        Require(!(hit.firstPanelId == "bottom" || hit.secondPanelId == "bottom"),
            "底と側面は重なり扱いしないこと");
    }
}

KACHA_V2_TEST(assembly, 壊れた入力を断る)
{
    Require(!EvaluateAssembly({}, {}, AssemblyState{50.0}).HasValue(), "板が無い");

    AssemblyPanel nameless = Rectangle("", 0.0, 10.0, 0.0, 10.0);
    Require(!EvaluateAssembly({nameless}, {}, AssemblyState{50.0}).HasValue(), "名前が無い");

    AssemblyPanel thin;
    thin.panelId = "x";
    thin.flatOutline = {{0.0, 0.0}, {10.0, 0.0}};
    Require(!EvaluateAssembly({thin}, {}, AssemblyState{50.0}).HasValue(), "輪郭が足りない");

    const std::vector<AssemblyPanel> two{Rectangle("a", 0.0, 10.0, 0.0, 10.0),
        Rectangle("a", 20.0, 30.0, 0.0, 10.0)};
    Require(!EvaluateAssembly(two, {}, AssemblyState{50.0}).HasValue(), "同じ名前が2つ");

    AssemblyFold missing;
    missing.foldId = "f";
    missing.parentPanelId = "a";
    missing.childPanelId = "nowhere";
    missing.hingeFrom = {0.0, 0.0};
    missing.hingeTo = {10.0, 0.0};
    Require(!EvaluateAssembly({Rectangle("a", 0.0, 10.0, 0.0, 10.0)}, {missing},
                 AssemblyState{50.0})
                 .HasValue(),
        "無い板を指す折り線");

    AssemblyFold degenerate;
    degenerate.foldId = "f";
    degenerate.parentPanelId = "a";
    degenerate.childPanelId = "b";
    degenerate.hingeFrom = {5.0, 5.0};
    degenerate.hingeTo = {5.0, 5.0};
    Require(!EvaluateAssembly({Rectangle("a", 0.0, 10.0, 0.0, 10.0),
                                  Rectangle("b", 0.0, 10.0, -10.0, 0.0)},
                 {degenerate}, AssemblyState{50.0})
                 .HasValue(),
        "長さ0の折り線");
}

KACHA_V2_TEST(assembly, 範囲外の値でも落ちない)
{
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    for (const double percent : {-50.0, 150.0, 1000.0}) {
        const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{percent});
        Require(result.HasValue(), "組み立てられること");
        const auto check = CheckAssemblyMetric(box.panels, result.Value());
        Require(check.withinTolerance, "条件を満たすこと");
    }
    // -50% は 0% と、150% は 100% と同じになる(挟み込む)。
    const auto low = EvaluateAssembly(box.panels, box.folds, AssemblyState{-50.0});
    const auto zero = EvaluateAssembly(box.panels, box.folds, AssemblyState{0.0});
    RequireNear(low.Value().panels[1].outline[0].z, zero.Value().panels[1].outline[0].z,
        1e-12, "下は0%と同じ");
}

KACHA_V2_TEST(assembly, 何度組み立てても同じ結果になる)
{
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    const auto first = EvaluateAssembly(box.panels, box.folds, AssemblyState{37.5});
    Require(first.HasValue(), "組み立てられること");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = EvaluateAssembly(box.panels, box.folds, AssemblyState{37.5});
        Require(again.HasValue(), "毎回組み立てられること");
        for (std::size_t panel = 0; panel < first.Value().panels.size(); ++panel) {
            RequireEqual(again.Value().panels[panel].panelId,
                first.Value().panels[panel].panelId, "板の並び");
            for (std::size_t index = 0;
                index < first.Value().panels[panel].outline.size(); ++index) {
                RequireNear(again.Value().panels[panel].outline[index].z,
                    first.Value().panels[panel].outline[index].z, 0.0, "完全に同じ");
            }
        }
    }
}

KACHA_V2_TEST(assembly, ゼロと三十と百で面内の寸法が変わらない)
{
    // §11「0%、30%、100%の出力で、対応する辺長とパネル面内寸法が許容差内で同じ」。
    const BoxLayout box = MakeBox(40.0, 30.0, 20.0);
    std::vector<double> reference;
    for (const double percent : {0.0, 30.0, 100.0}) {
        const auto result = EvaluateAssembly(box.panels, box.folds, AssemblyState{percent});
        Require(result.HasValue(), "組み立てられること");
        std::vector<double> lengths;
        for (const auto& panel : result.Value().panels) {
            for (std::size_t index = 0; index < panel.outline.size(); ++index) {
                const std::size_t next = (index + 1) % panel.outline.size();
                lengths.push_back((panel.outline[next] - panel.outline[index]).Length());
            }
        }
        if (reference.empty()) {
            reference = lengths;
            continue;
        }
        RequireCount(lengths.size(), reference.size(), "辺の数");
        for (std::size_t index = 0; index < lengths.size(); ++index) {
            RequireNear(lengths[index], reference[index], 1e-9,
                std::to_string(percent) + "% の辺 " + std::to_string(index));
        }
    }
}

KACHA_V2_TEST_MAIN("assembly_tests")
