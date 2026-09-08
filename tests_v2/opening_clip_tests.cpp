// パネルをまたぐ開口(fabrication-contract.md §8.2)。
// V1がいちばん派手に壊れたところ。窓が境目をまたぐと切り口が弦になり、
// 断片ごと消えることもあった。ここでは「消さない」「弦にしない」を確かめる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/OpeningClip.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::CheckOpeningClosure;
using kachakacha::v2::fabrication::ClipOpeningAcrossPanels;
using kachakacha::v2::fabrication::OpeningPiece;
using kachakacha::v2::fabrication::PanelRegion;
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

//! 円い窓。z = 0 の面の上。
[[nodiscard]] std::vector<Vector3> CircularOpening(Vector3 center, double radius,
    int steps = 72)
{
    std::vector<Vector3> points;
    for (int index = 0; index < steps; ++index) {
        const double angle = static_cast<double>(index) / static_cast<double>(steps)
            * 2.0 * kPi;
        points.push_back({center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle), center.z});
    }
    return points;
}

//! 平らな長方形の部材。
[[nodiscard]] PanelRegion FlatPanel(const std::string& id, double x0, double x1, double y0,
    double y1, int steps = 40)
{
    PanelRegion panel;
    panel.panelId = id;
    const auto edge = [&](Vector3 from, Vector3 to) {
        for (int index = 0; index < steps; ++index) {
            const double t = static_cast<double>(index) / static_cast<double>(steps);
            panel.boundary.push_back(from + (to - from) * t);
        }
    };
    edge({x0, y0, 0.0}, {x1, y0, 0.0});
    edge({x1, y0, 0.0}, {x1, y1, 0.0});
    edge({x1, y1, 0.0}, {x0, y1, 0.0});
    edge({x0, y1, 0.0}, {x0, y0, 0.0});
    return panel;
}

//! 曲がった面の上の部材。境目が円弧になっている。
//! ここでV1は「境目を1本の直線で結んで」形を崩した。
[[nodiscard]] PanelRegion CurvedPanel(const std::string& id, double radius,
    double angleFrom, double angleTo, double zFrom, double zTo, int steps = 60)
{
    PanelRegion panel;
    panel.panelId = id;
    const auto arc = [&](double z, double from, double to) {
        for (int index = 0; index < steps; ++index) {
            const double t = static_cast<double>(index) / static_cast<double>(steps);
            const double angle = from + (to - from) * t;
            panel.boundary.push_back(
                {radius * std::cos(angle), radius * std::sin(angle), z});
        }
    };
    arc(zFrom, angleFrom, angleTo);
    // 端の縦線。
    for (int index = 0; index < 10; ++index) {
        const double t = static_cast<double>(index) / 10.0;
        panel.boundary.push_back({radius * std::cos(angleTo), radius * std::sin(angleTo),
            zFrom + (zTo - zFrom) * t});
    }
    arc(zTo, angleTo, angleFrom);
    for (int index = 0; index < 10; ++index) {
        const double t = static_cast<double>(index) / 10.0;
        panel.boundary.push_back({radius * std::cos(angleFrom),
            radius * std::sin(angleFrom), zTo + (zFrom - zTo) * t});
    }
    return panel;
}

//! 点列の中で、いちばん長い一歩。弦になっていないかを見る物差し。
[[nodiscard]] double LargestStep(const std::vector<Vector3>& points)
{
    double largest = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        largest = std::max(largest, (points[index] - points[index - 1]).Length());
    }
    if (points.size() >= 2) {
        largest = std::max(largest, (points.front() - points.back()).Length());
    }
    return largest;
}

} // namespace

KACHA_V2_TEST(opening, またいでいない窓はそのまま1枚に載る)
{
    const auto opening = CircularOpening({30.0, 30.0, 0.0}, 8.0);
    const std::vector<PanelRegion> panels{FlatPanel("A", 0.0, 100.0, 0.0, 60.0)};
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.1);
    Require(result.HasValue(), "切り分けられること");
    RequireCount(result.Value().pieces.size(), 1, "断片の数");
    RequireEqual(result.Value().pieces[0].panelId, std::string("A"), "所属");
    RequireCount(result.Value().pieces[0].outline.size(), opening.size(), "点の数");
}

KACHA_V2_TEST(opening, 境目をまたぐ窓が2つの断片になる)
{
    // 境目は x = 50。窓は x = 50 をまたぐ。
    const auto opening = CircularOpening({50.0, 30.0, 0.0}, 10.0);
    const std::vector<PanelRegion> panels{
        FlatPanel("A", 0.0, 50.0, 0.0, 60.0),
        FlatPanel("B", 50.0, 100.0, 0.0, 60.0),
    };
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.5);
    Require(result.HasValue(), "切り分けられること ("
            + (result.Diagnostics().empty() ? std::string("診断なし")
                                            : result.Diagnostics().front().detailsJa)
            + ")");
    RequireCount(result.Value().pieces.size(), 2, "断片の数");
    // 両方の部材に1つずつ。
    std::set<std::string> ids;
    for (const auto& piece : result.Value().pieces) {
        ids.insert(piece.panelId);
    }
    RequireCount(ids.size(), 2, "両方の部材に載ること");
}

KACHA_V2_TEST(opening, 窓が消えない)
{
    // §8「開口を消して生成を成功扱いにしてはならない」。
    const auto opening = CircularOpening({50.0, 30.0, 0.0}, 10.0);
    const std::vector<PanelRegion> panels{
        FlatPanel("A", 0.0, 50.0, 0.0, 60.0),
        FlatPanel("B", 50.0, 100.0, 0.0, 60.0),
    };
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.5);
    Require(result.HasValue(), "切り分けられること");
    const auto check = CheckOpeningClosure(result.Value(), 1.0);
    Require(check.closed,
        "元の周長へ戻ること (差 " + std::to_string(check.perimeterDifferenceMm) + " mm)");
    Require(check.maximumGapMm < 1.0,
        "断片の端が合うこと (" + std::to_string(check.maximumGapMm) + " mm)");
}

KACHA_V2_TEST(opening, 切り口が弦にならない)
{
    // ここがV1の壊れどころ。曲がった面の境目を、1本の直線で結んではいけない。
    const double radius = 60.0;
    const auto panels = std::vector<PanelRegion>{
        CurvedPanel("A", radius, -0.9, 0.0, 0.0, 40.0),
        CurvedPanel("B", radius, 0.0, 0.9, 0.0, 40.0),
    };
    // 境目(角度0)をまたぐ窓。円筒の面の上に置く。
    std::vector<Vector3> opening;
    for (int index = 0; index < 72; ++index) {
        const double t = static_cast<double>(index) / 72.0 * 2.0 * kPi;
        const double angle = 0.0 + 0.25 * std::cos(t);
        const double z = 20.0 + 8.0 * std::sin(t);
        opening.push_back({radius * std::cos(angle), radius * std::sin(angle), z});
    }
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 1.0);
    Require(result.HasValue(), "切り分けられること");
    RequireCount(result.Value().pieces.size(), 2, "断片の数");

    // 切り口の刻みが細かいこと。1本の直線なら、ここが窓の高さぶん(約16mm)になる。
    for (const auto& piece : result.Value().pieces) {
        const double step = LargestStep(piece.outline);
        Require(step <= 2.0,
            "刻みが細かいこと (" + piece.panelId + " の最大の一歩 "
                + std::to_string(step) + " mm)");
    }
}

KACHA_V2_TEST(opening, 切り口の刻みを指定できる)
{
    const auto opening = CircularOpening({50.0, 30.0, 0.0}, 10.0);
    const std::vector<PanelRegion> panels{
        FlatPanel("A", 0.0, 50.0, 0.0, 60.0),
        FlatPanel("B", 50.0, 100.0, 0.0, 60.0),
    };
    for (const double step : {0.5, 1.0, 2.0}) {
        const auto result = ClipOpeningAcrossPanels(opening, panels, step, 0.5);
        Require(result.HasValue(), "切り分けられること");
        for (const auto& piece : result.Value().pieces) {
            Require(LargestStep(piece.outline) <= step * 2.0 + 1.0,
                "指定した刻みに従うこと (" + std::to_string(step) + ")");
        }
    }
}

KACHA_V2_TEST(opening, 断片どうしが同じ切り口のIDを持つ)
{
    const auto opening = CircularOpening({50.0, 30.0, 0.0}, 10.0);
    const std::vector<PanelRegion> panels{
        FlatPanel("A", 0.0, 50.0, 0.0, 60.0),
        FlatPanel("B", 50.0, 100.0, 0.0, 60.0),
    };
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.5);
    Require(result.HasValue(), "切り分けられること");
    std::set<std::string> keys;
    for (const auto& piece : result.Value().pieces) {
        Require(!piece.junctions.empty(), "切り口があること");
        Require(piece.entryJunctionId.find("A|B") != std::string::npos,
            "組の名前が入ること: " + piece.entryJunctionId);
        keys.insert(piece.entryJunctionId);
        keys.insert(piece.exitJunctionId);
    }
    RequireCount(keys.size(), 2, "横切る点は2箇所");
    // 2つの断片が、同じ2つのIDを共有していること。
    RequireEqual(result.Value().pieces[0].exitJunctionId,
        result.Value().pieces[1].entryJunctionId, "出口と入口が繋がること");
    RequireEqual(result.Value().pieces[1].exitJunctionId,
        result.Value().pieces[0].entryJunctionId, "反対側も繋がること");
}

KACHA_V2_TEST(opening, 部材を渡す順を変えても同じ結果になる)
{
    const auto opening = CircularOpening({50.0, 30.0, 0.0}, 10.0);
    const auto first = ClipOpeningAcrossPanels(opening,
        {FlatPanel("A", 0.0, 50.0, 0.0, 60.0), FlatPanel("B", 50.0, 100.0, 0.0, 60.0)}, 1.0,
        0.5);
    const auto second = ClipOpeningAcrossPanels(opening,
        {FlatPanel("B", 50.0, 100.0, 0.0, 60.0), FlatPanel("A", 0.0, 50.0, 0.0, 60.0)}, 1.0,
        0.5);
    Require(first.HasValue() && second.HasValue(), "どちらも切り分けられること");
    RequireCount(second.Value().pieces.size(), first.Value().pieces.size(), "断片の数");
    // 切り口のIDは部材名を並べて作るので、順番に依らない。
    std::set<std::string> firstKeys;
    std::set<std::string> secondKeys;
    for (const auto& piece : first.Value().pieces) {
        firstKeys.insert(piece.entryJunctionId);
        firstKeys.insert(piece.exitJunctionId);
    }
    for (const auto& piece : second.Value().pieces) {
        secondKeys.insert(piece.entryJunctionId);
        secondKeys.insert(piece.exitJunctionId);
    }
    Require(firstKeys == secondKeys, "横切る点のIDが同じであること");
}

KACHA_V2_TEST(opening, 3枚にまたがる窓も扱える)
{
    // 横長の窓が、3枚の部材にまたがる。
    std::vector<Vector3> opening;
    for (int index = 0; index < 80; ++index) {
        const double t = static_cast<double>(index) / 80.0 * 2.0 * kPi;
        opening.push_back({50.0 + 40.0 * std::cos(t), 30.0 + 6.0 * std::sin(t), 0.0});
    }
    const std::vector<PanelRegion> panels{
        FlatPanel("A", 0.0, 35.0, 0.0, 60.0),
        FlatPanel("B", 35.0, 65.0, 0.0, 60.0),
        FlatPanel("C", 65.0, 100.0, 0.0, 60.0),
    };
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 1.0);
    Require(result.HasValue(), "切り分けられること");
    Require(result.Value().pieces.size() >= 3,
        "3枚以上に分かれること (" + std::to_string(result.Value().pieces.size()) + ")");
    std::set<std::string> ids;
    for (const auto& piece : result.Value().pieces) {
        ids.insert(piece.panelId);
    }
    RequireCount(ids.size(), 3, "3枚すべてに載ること");
}

KACHA_V2_TEST(opening, どの部材にも載らない窓を断る)
{
    const auto opening = CircularOpening({500.0, 500.0, 0.0}, 8.0);
    const std::vector<PanelRegion> panels{FlatPanel("A", 0.0, 100.0, 0.0, 60.0)};
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.1);
    Require(!result.HasValue(), "断ること");
    RequireEqual(result.Diagnostics().front().code, std::string("FAB-O001"), "診断コード");
}

KACHA_V2_TEST(opening, 一部だけはみ出した窓は消さずに知らせる)
{
    // 窓の一部が部材の外へ出ている。消してはいけない。知らせて、残す。
    const auto opening = CircularOpening({95.0, 30.0, 0.0}, 10.0);
    const std::vector<PanelRegion> panels{FlatPanel("A", 0.0, 100.0, 0.0, 60.0)};
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.5);
    Require(result.HasValue(), "断片を返すこと");
    Require(!result.Value().pieces.empty(), "窓が消えていないこと");
    const bool told = std::any_of(result.Value().notes.begin(), result.Value().notes.end(),
        [](const auto& note) { return note.code == "FAB-O004"; });
    Require(told, "はみ出したことを知らせること");
}

KACHA_V2_TEST(opening, 壊れた入力を断る)
{
    const std::vector<PanelRegion> panels{FlatPanel("A", 0.0, 100.0, 0.0, 60.0)};
    Require(!ClipOpeningAcrossPanels({}, panels, 1.0, 0.1).HasValue(), "空の窓");
    Require(!ClipOpeningAcrossPanels({{0, 0, 0}, {1, 0, 0}}, panels, 1.0, 0.1).HasValue(),
        "点が2つの窓");
    Require(!ClipOpeningAcrossPanels(CircularOpening({30, 30, 0}, 8.0), {}, 1.0, 0.1)
                 .HasValue(),
        "部材が無い");

    PanelRegion broken;
    broken.panelId = "X";
    broken.boundary = {{0, 0, 0}, {1, 0, 0}};
    Require(!ClipOpeningAcrossPanels(CircularOpening({30, 30, 0}, 8.0), {broken}, 1.0, 0.1)
                 .HasValue(),
        "輪郭が足りない部材");

    std::vector<Vector3> withNan = CircularOpening({30, 30, 0}, 8.0);
    withNan[3].x = std::nan("");
    Require(!ClipOpeningAcrossPanels(withNan, panels, 1.0, 0.1).HasValue(), "NaN を断ること");
}

KACHA_V2_TEST(opening, 何度切り分けても同じ結果になる)
{
    const auto opening = CircularOpening({50.0, 30.0, 0.0}, 10.0);
    const std::vector<PanelRegion> panels{
        FlatPanel("A", 0.0, 50.0, 0.0, 60.0),
        FlatPanel("B", 50.0, 100.0, 0.0, 60.0),
    };
    const auto first = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.5);
    Require(first.HasValue(), "切り分けられること");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.5);
        Require(again.HasValue(), "毎回切り分けられること");
        RequireCount(again.Value().pieces.size(), first.Value().pieces.size(), "断片の数");
        for (std::size_t at = 0; at < first.Value().pieces.size(); ++at) {
            RequireEqual(again.Value().pieces[at].panelId,
                first.Value().pieces[at].panelId, "所属");
            RequireCount(again.Value().pieces[at].outline.size(),
                first.Value().pieces[at].outline.size(), "点の数");
        }
    }
}

KACHA_V2_TEST(opening, 丸い窓が多角形へ置き換わらない)
{
    // §8「ライトや窓を丸い多角形へ置換してはならない」。
    // 元の点をすべて含んでいること(間引いていないこと)を確かめる。
    const auto opening = CircularOpening({50.0, 30.0, 0.0}, 10.0, 120);
    const std::vector<PanelRegion> panels{
        FlatPanel("A", 0.0, 50.0, 0.0, 60.0),
        FlatPanel("B", 50.0, 100.0, 0.0, 60.0),
    };
    const auto result = ClipOpeningAcrossPanels(opening, panels, 1.0, 0.5);
    Require(result.HasValue(), "切り分けられること");
    std::size_t found = 0;
    for (const Vector3& original : opening) {
        for (const auto& piece : result.Value().pieces) {
            const bool here = std::any_of(piece.outline.begin(), piece.outline.end(),
                [&](const Vector3& point) { return (point - original).Length() < 1e-9; });
            if (here) {
                ++found;
                break;
            }
        }
    }
    RequireCount(found, opening.size(), "元の点がすべて残ること");
}

KACHA_V2_TEST_MAIN("opening_clip_tests")
