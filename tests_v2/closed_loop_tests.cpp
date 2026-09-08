// 閉じた輪の組立(AT-FAB-010)。
//
// 貼り合わせで輪になっている型紙は、折り角を勝手に決めると閉じない。
// 折り角を解いて閉じること、閉じないときは板を歪めずに断ることを見る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/ClosedLoop.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AssemblyFold;
using kachakacha::v2::fabrication::AssemblyPanel;
using kachakacha::v2::fabrication::AssemblyState;
using kachakacha::v2::fabrication::CheckMateEdgeLengths;
using kachakacha::v2::fabrication::EvaluateAssembly;
using kachakacha::v2::fabrication::MatePair;
using kachakacha::v2::fabrication::SolveClosedLoop;
using kachakacha::v2::geometry::Point2;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] AssemblyPanel Rectangle(const std::string& id, double fromU, double toU,
    double height)
{
    AssemblyPanel panel;
    panel.panelId = id;
    panel.flatOutline = {Point2{fromU, 0.0}, Point2{toU, 0.0}, Point2{toU, height},
        Point2{fromU, height}};
    return panel;
}

[[nodiscard]] AssemblyFold Fold(const std::string& id, const std::string& parent,
    const std::string& child, double atU, double height, double angleRad)
{
    AssemblyFold fold;
    fold.foldId = id;
    fold.parentPanelId = parent;
    fold.childPanelId = child;
    fold.hingeFrom = Point2{atU, 0.0};
    fold.hingeTo = Point2{atU, height};
    fold.targetAngleRad = angleRad;
    return fold;
}

//! 幅 40 / 30 / 40 / 30 の角筒。90度ずつ折ると閉じる。
struct Tube {
    std::vector<AssemblyPanel> panels;
    std::vector<AssemblyFold> folds;
    std::vector<MatePair> mates;
};

[[nodiscard]] Tube MakeTube(double startAngleRad, double thirdWidth = 40.0)
{
    const double height = 20.0;
    const double a = 40.0;
    const double b = 30.0;
    const double c = thirdWidth;
    const double d = 30.0;
    Tube tube;
    tube.panels = {
        Rectangle("A", 0.0, a, height),
        Rectangle("B", a, a + b, height),
        Rectangle("C", a + b, a + b + c, height),
        Rectangle("D", a + b + c, a + b + c + d, height),
    };
    tube.folds = {
        Fold("f1", "A", "B", a, height, startAngleRad),
        Fold("f2", "B", "C", a + b, height, startAngleRad),
        Fold("f3", "C", "D", a + b + c, height, startAngleRad),
    };
    MatePair mate;
    mate.matePairId = "mate/closing";
    mate.firstPanelIndex = 3;   // D の右端
    mate.firstFromVertex = 1;
    mate.firstToVertex = 2;
    mate.secondPanelIndex = 0;  // A の左端
    mate.secondFromVertex = 0;
    mate.secondToVertex = 3;
    tube.mates.push_back(mate);
    return tube;
}

[[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

} // namespace

KACHA_V2_TEST(closed_loop, 直角で作った角筒はそのまま閉じている)
{
    const Tube tube = MakeTube(kPi / 2.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(solved.HasValue(), "解けること");
    Require(solved.Value().converged, "閉じている");
    RequireNear(solved.Value().maximumGapMm, 0.0, 1.0e-6, "隙間");
    RequireEqual(std::to_string(solved.Value().iterations), "0",
        "はじめから閉じているので繰り返さない");
}

KACHA_V2_TEST(closed_loop, 角度がずれていても解いて閉じる)
{
    const Tube tube = MakeTube(80.0 * kPi / 180.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(solved.HasValue(), "解けること");
    Require(solved.Value().converged, "閉じた");
    Require(solved.Value().maximumGapMm <= 1.0e-6, "隙間が許容内");
    Require(solved.Value().iterations > 0, "繰り返して解いた");
}

KACHA_V2_TEST(closed_loop, 大きくずれていても解ける)
{
    const Tube tube = MakeTube(50.0 * kPi / 180.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-5);
    Require(solved.HasValue(), "解けること");
    Require(solved.Value().maximumGapMm <= 1.0e-5, "隙間が許容内");
}

KACHA_V2_TEST(closed_loop, 解いても板の形は変えない)
{
    const Tube tube = MakeTube(70.0 * kPi / 180.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(solved.HasValue(), "解けること");
    // 折りの数も、どの板を繋ぐかも変わらない。角度だけが変わる。
    RequireEqual(std::to_string(solved.Value().folds.size()),
        std::to_string(tube.folds.size()), "折りの数");
    for (std::size_t index = 0; index < tube.folds.size(); ++index) {
        RequireEqual(solved.Value().folds[index].foldId, tube.folds[index].foldId, "名前");
        RequireEqual(solved.Value().folds[index].parentPanelId,
            tube.folds[index].parentPanelId, "親");
        RequireEqual(solved.Value().folds[index].childPanelId,
            tube.folds[index].childPanelId, "子");
    }
}

KACHA_V2_TEST(closed_loop, 解いた後も辺の長さが変わっていない)
{
    const Tube tube = MakeTube(65.0 * kPi / 180.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(solved.HasValue(), "解けること");
    AssemblyState state;
    state.masterPercent = 100.0;
    const auto placed = EvaluateAssembly(tube.panels, solved.Value().folds, state);
    Require(placed.HasValue(), "置けること");
    const auto metric =
        kachakacha::v2::fabrication::CheckAssemblyMetric(tube.panels, placed.Value());
    Require(metric.withinTolerance, "伸び縮みしていない");
    RequireNear(metric.maximumEdgeLengthChangeRelative, 0.0, 1.0e-9, "辺の長さ");
}

KACHA_V2_TEST(closed_loop, 閉じない型紙は歪めずに断る)
{
    // 3枚目だけを、他の3枚の合計より長くする。
    // 一辺が残りの合計より長い四辺形は存在しないので、どんな角度にしても閉じない。
    const Tube tube = MakeTube(kPi / 2.0, 200.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(!solved.HasValue(), "解けたことにしない");
    RequireEqual(FirstCode(solved.Diagnostics()), "FAB-A001", "診断コード");
    Require(solved.Diagnostics().front().detailsJa.find("mate/closing")
            != std::string::npos,
        "いちばん開いている貼り合わせを名指しする");
}

KACHA_V2_TEST(closed_loop, 貼り合わせの辺の長さが違えば先に断る)
{
    Tube tube = MakeTube(kPi / 2.0);
    // A の左端を短くする。貼り合わせる相手と長さが合わなくなる。
    tube.panels[0].flatOutline[3] = Point2{0.0, 10.0};
    const auto checked = CheckMateEdgeLengths(tube.panels, tube.mates, 1.0e-6);
    Require(!checked.HasValue(), "断る");
    RequireEqual(FirstCode(checked.Diagnostics()), "FAB-A004", "診断コード");
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(!solved.HasValue(), "解こうとせずに断る");
}

KACHA_V2_TEST(closed_loop, 貼り合わせごとの残り隙間が取れる)
{
    const Tube tube = MakeTube(kPi / 2.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(solved.HasValue(), "解けること");
    RequireEqual(std::to_string(solved.Value().residuals.size()), "1", "1組");
    RequireEqual(solved.Value().residuals.front().matePairId,
        std::string("mate/closing"), "名前");
    Require(solved.Value().residuals.front().gapMm <= 1.0e-6, "隙間");
}

KACHA_V2_TEST(closed_loop, 折りが無ければ断る)
{
    const Tube tube = MakeTube(kPi / 2.0);
    const auto solved = SolveClosedLoop(tube.panels, {}, tube.mates, 1.0e-6);
    Require(!solved.HasValue(), "断る");
    RequireEqual(FirstCode(solved.Diagnostics()), "FAB-A004", "診断コード");
}

KACHA_V2_TEST(closed_loop, 貼り合わせが無ければ断る)
{
    const Tube tube = MakeTube(kPi / 2.0);
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, {}, 1.0e-6);
    Require(!solved.HasValue(), "断る");
}

KACHA_V2_TEST(closed_loop, 無い板を指す貼り合わせは断る)
{
    Tube tube = MakeTube(kPi / 2.0);
    tube.mates.front().firstPanelIndex = 99;
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(!solved.HasValue(), "断る");
    RequireEqual(FirstCode(solved.Diagnostics()), "FAB-A004", "診断コード");
}

KACHA_V2_TEST(closed_loop, 無い頂点を指す貼り合わせは断る)
{
    Tube tube = MakeTube(kPi / 2.0);
    tube.mates.front().firstFromVertex = 99;
    const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
    Require(!solved.HasValue(), "断る");
}

KACHA_V2_TEST(closed_loop, 同じ入力からは毎回同じ角度が出る)
{
    const Tube tube = MakeTube(75.0 * kPi / 180.0);
    std::vector<double> reference;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto solved = SolveClosedLoop(tube.panels, tube.folds, tube.mates, 1.0e-6);
        Require(solved.HasValue(), "解けること");
        std::vector<double> angles;
        for (const auto& fold : solved.Value().folds) {
            angles.push_back(fold.targetAngleRad);
        }
        if (attempt == 0) {
            reference = angles;
        } else {
            RequireEqual(std::to_string(angles.size()),
                std::to_string(reference.size()), "折りの数");
            for (std::size_t index = 0; index < angles.size(); ++index) {
                RequireNear(angles[index], reference[index], 0.0, "毎回同じ角度");
            }
        }
    }
}

KACHA_V2_TEST(closed_loop, 六角の筒でも解ける)
{
    const double height = 15.0;
    const double side = 25.0;
    std::vector<AssemblyPanel> panels;
    std::vector<AssemblyFold> folds;
    double at = 0.0;
    for (int index = 0; index < 6; ++index) {
        const std::string id = "p" + std::to_string(index);
        panels.push_back(Rectangle(id, at, at + side, height));
        if (index > 0) {
            folds.push_back(Fold("f" + std::to_string(index),
                "p" + std::to_string(index - 1), id, at, height, 50.0 * kPi / 180.0));
        }
        at += side;
    }
    MatePair mate;
    mate.matePairId = "mate/hex";
    mate.firstPanelIndex = 5;
    mate.firstFromVertex = 1;
    mate.firstToVertex = 2;
    mate.secondPanelIndex = 0;
    mate.secondFromVertex = 0;
    mate.secondToVertex = 3;
    const auto solved = SolveClosedLoop(panels, folds, {mate}, 1.0e-5);
    Require(solved.HasValue(), "解けること");
    Require(solved.Value().maximumGapMm <= 1.0e-5, "閉じた");
    // 辺の長さが同じ六角形は、正六角形とは限らない。
    // 求めるのは「閉じること」と「板が伸び縮みしないこと」であって、
    // 特定の角度ではない。角度を決め打ちで確かめると、正しい解を落とす。
    AssemblyState state;
    state.masterPercent = 100.0;
    const auto placed = EvaluateAssembly(panels, solved.Value().folds, state);
    Require(placed.HasValue(), "置けること");
    const auto metric =
        kachakacha::v2::fabrication::CheckAssemblyMetric(panels, placed.Value());
    Require(metric.withinTolerance, "伸び縮みしていない");
    RequireEqual(std::to_string(solved.Value().folds.size()), "5", "折りは5本");
}

KACHA_V2_TEST_MAIN("closed_loop_tests")
