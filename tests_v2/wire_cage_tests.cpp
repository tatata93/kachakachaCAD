// 閉じたワイヤー群から部品を作る(AT-GEO-010/011)。
// 線の並び順に頼らず、囲めているかを判定する。囲めていないものは断る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/SubshapeKey.h"
#include "kachakacha/modeling/WireCage.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::AnalyzeWireCage;
using kachakacha::v2::modeling::CageDeclaredPatch;
using kachakacha::v2::modeling::CageEdgeInput;
using kachakacha::v2::modeling::MakeBooleanProvenance;
using kachakacha::v2::modeling::MakeExtrudeCapEnd;
using kachakacha::v2::modeling::MakeExtrudeCapStart;
using kachakacha::v2::modeling::MakeExtrudeSide;
using kachakacha::v2::modeling::MakeLoftSpan;
using kachakacha::v2::modeling::ParseSubshapeKey;
using kachakacha::v2::modeling::SubshapeKind;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

//! 線を1本足す道具。IDは決定的に振る。
struct Builder {
    DeterministicIdGenerator ids{11};
    EntityId wireId;
    std::vector<CageEdgeInput> edges;

    Builder() { wireId = ids.NextTyped<IdKind::Entity>(); }

    void Add(Vector3 a, Vector3 b)
    {
        const auto made = CurveSegment::MakeLine(a, b);
        Require(made.HasValue(), "直線が作れること");
        edges.push_back(
            CageEdgeInput{wireId, ids.NextTyped<IdKind::Segment>(), made.Value()});
    }

    void AddArc(Vector3 center, Vector3 normal, Vector3 reference, double radius,
        double start, double sweep)
    {
        const auto made =
            CurveSegment::MakeCircularArc(center, normal, reference, radius, start, sweep);
        Require(made.HasValue(), "円弧が作れること");
        edges.push_back(
            CageEdgeInput{wireId, ids.NextTyped<IdKind::Segment>(), made.Value()});
    }
};

//! 直方体の12辺。
[[nodiscard]] std::vector<CageEdgeInput> Box(double sx, double sy, double sz)
{
    Builder builder;
    const Vector3 corner[8]{
        {0, 0, 0}, {sx, 0, 0}, {sx, sy, 0}, {0, sy, 0},
        {0, 0, sz}, {sx, 0, sz}, {sx, sy, sz}, {0, sy, sz},
    };
    // 下面
    builder.Add(corner[0], corner[1]);
    builder.Add(corner[1], corner[2]);
    builder.Add(corner[2], corner[3]);
    builder.Add(corner[3], corner[0]);
    // 上面
    builder.Add(corner[4], corner[5]);
    builder.Add(corner[5], corner[6]);
    builder.Add(corner[6], corner[7]);
    builder.Add(corner[7], corner[4]);
    // 縦
    builder.Add(corner[0], corner[4]);
    builder.Add(corner[1], corner[5]);
    builder.Add(corner[2], corner[6]);
    builder.Add(corner[3], corner[7]);
    return builder.edges;
}

//! 三角柱の9辺。
[[nodiscard]] std::vector<CageEdgeInput> TriangularPrism(double height)
{
    Builder builder;
    const Vector3 bottom[3]{{0, 0, 0}, {10, 0, 0}, {5, 8, 0}};
    const Vector3 top[3]{{0, 0, height}, {10, 0, height}, {5, 8, height}};
    for (int index = 0; index < 3; ++index) {
        builder.Add(bottom[index], bottom[(index + 1) % 3]);
    }
    for (int index = 0; index < 3; ++index) {
        builder.Add(top[index], top[(index + 1) % 3]);
    }
    for (int index = 0; index < 3; ++index) {
        builder.Add(bottom[index], top[index]);
    }
    return builder.edges;
}

void RequireRejects(const std::vector<CageEdgeInput>& edges, const std::string& expectedCode,
    const std::string& why)
{
    const auto result = AnalyzeWireCage(edges, Tolerance());
    Require(!result.HasValue(), "断ること: " + why);
    Require(!result.Diagnostics().empty(), "診断が付くこと: " + why);
    const bool found = std::any_of(result.Diagnostics().begin(), result.Diagnostics().end(),
        [&](const auto& diagnostic) { return diagnostic.code == expectedCode; });
    Require(found, "診断コード " + expectedCode + ": " + why + " (実際 "
            + result.Diagnostics().front().code + " / "
            + result.Diagnostics().front().summaryJa + ")");
}

} // namespace

// ---------------------------------------------------------------- AT-GEO-010

KACHA_V2_TEST(wireCage, 直方体の12辺から6面1体を見つける)
{
    const auto result = AnalyzeWireCage(Box(20.0, 30.0, 40.0), Tolerance());
    Require(result.HasValue(), "見つかること");
    RequireCount(result.Value().shells.size(), 1, "立体の数");
    RequireCount(result.Value().shells[0].patches.size(), 6, "面の数");
    RequireNear(result.Value().shells[0].volumeMm3, 20.0 * 30.0 * 40.0, 1e-6, "体積");
    Require(!result.Value().shells[0].volumeIsApproximate, "平面だけなら体積は正確");
    Require(result.Value().unusedEdges.empty(), "余る線が無いこと");
}

KACHA_V2_TEST(wireCage, 線の順番を変えても同じ結果になる)
{
    const auto normal = Box(20.0, 30.0, 40.0);
    const auto reference = AnalyzeWireCage(normal, Tolerance());
    Require(reference.HasValue(), "土台が見つかること");

    // 順番を何通りか入れ替えて、毎回同じ答えになること。
    const std::size_t orders[3][12] = {
        {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
        {8, 0, 9, 4, 1, 10, 5, 2, 11, 6, 3, 7},
        {3, 7, 11, 2, 6, 10, 1, 5, 9, 0, 4, 8},
    };
    for (const auto& order : orders) {
        std::vector<CageEdgeInput> shuffled;
        for (const std::size_t index : order) {
            shuffled.push_back(normal[index]);
        }
        const auto result = AnalyzeWireCage(shuffled, Tolerance());
        Require(result.HasValue(), "順不同でも見つかること");
        RequireCount(result.Value().shells.size(), 1, "立体の数");
        RequireCount(result.Value().shells[0].patches.size(), 6, "面の数");
        RequireNear(result.Value().shells[0].volumeMm3,
            reference.Value().shells[0].volumeMm3, 1e-9, "体積が同じ");
    }
}

KACHA_V2_TEST(wireCage, 三角柱を見つける)
{
    const auto result = AnalyzeWireCage(TriangularPrism(25.0), Tolerance());
    Require(result.HasValue(), "見つかること");
    RequireCount(result.Value().shells[0].patches.size(), 5, "面の数(底2・側3)");
    RequireNear(result.Value().shells[0].volumeMm3, 0.5 * 10.0 * 8.0 * 25.0, 1e-6, "体積");
}

KACHA_V2_TEST(wireCage, 細長い形でも正しい体積になる)
{
    for (const double thickness : {0.1, 0.3, 1.0}) {
        const auto result = AnalyzeWireCage(Box(200.0, 60.0, thickness), Tolerance());
        Require(result.HasValue(), "厚み " + std::to_string(thickness) + " で見つかること");
        RequireNear(result.Value().shells[0].volumeMm3, 200.0 * 60.0 * thickness, 1e-6,
            "体積");
    }
}

KACHA_V2_TEST(wireCage, 面の向きが外向きに揃う)
{
    const auto result = AnalyzeWireCage(Box(10.0, 10.0, 10.0), Tolerance());
    Require(result.HasValue(), "見つかること");
    Require(result.Value().shells[0].outwardOriented, "外向き");
    // 6面の法線が、6方向すべてを1回ずつ向いていること。
    int plusX = 0, minusX = 0, plusY = 0, minusY = 0, plusZ = 0, minusZ = 0;
    for (const auto& patch : result.Value().shells[0].patches) {
        if (patch.normal.x > 0.5) ++plusX;
        if (patch.normal.x < -0.5) ++minusX;
        if (patch.normal.y > 0.5) ++plusY;
        if (patch.normal.y < -0.5) ++minusY;
        if (patch.normal.z > 0.5) ++plusZ;
        if (patch.normal.z < -0.5) ++minusZ;
    }
    RequireCount(static_cast<std::size_t>(plusX + minusX + plusY + minusY + plusZ + minusZ),
        6, "法線が6方向");
    RequireCount(static_cast<std::size_t>(plusX), 1, "+x が1枚");
    RequireCount(static_cast<std::size_t>(minusZ), 1, "-z が1枚");
}

KACHA_V2_TEST(wireCage, 面の意味的キーが並べ替えで変わらない)
{
    const auto normal = Box(10.0, 10.0, 10.0);
    const auto first = AnalyzeWireCage(normal, Tolerance());
    Require(first.HasValue(), "見つかること");
    std::vector<std::string> keys;
    for (const auto& patch : first.Value().shells[0].patches) {
        keys.push_back(patch.key.ToString());
    }
    std::sort(keys.begin(), keys.end());

    std::vector<CageEdgeInput> reversed(normal.rbegin(), normal.rend());
    const auto second = AnalyzeWireCage(reversed, Tolerance());
    Require(second.HasValue(), "逆順でも見つかること");
    std::vector<std::string> otherKeys;
    for (const auto& patch : second.Value().shells[0].patches) {
        otherKeys.push_back(patch.key.ToString());
    }
    std::sort(otherKeys.begin(), otherKeys.end());
    Require(keys == otherKeys, "同じキーの集合になること");
}

KACHA_V2_TEST(wireCage, 面の各辺がちょうど2回使われる)
{
    const auto result = AnalyzeWireCage(Box(10.0, 20.0, 30.0), Tolerance());
    Require(result.HasValue(), "見つかること");
    std::vector<int> usage(12, 0);
    for (const auto& patch : result.Value().shells[0].patches) {
        for (const std::size_t index : patch.edgeIndices) {
            ++usage[index];
        }
    }
    for (std::size_t index = 0; index < usage.size(); ++index) {
        RequireCount(static_cast<std::size_t>(usage[index]), 2,
            "線 " + std::to_string(index) + " の使われ方");
    }
}

KACHA_V2_TEST(wireCage, 共有する辺は互いに逆向きに使われる)
{
    const auto result = AnalyzeWireCage(Box(10.0, 20.0, 30.0), Tolerance());
    Require(result.HasValue(), "見つかること");
    std::vector<std::vector<bool>> directions(12);
    for (const auto& patch : result.Value().shells[0].patches) {
        for (std::size_t at = 0; at < patch.edgeIndices.size(); ++at) {
            directions[patch.edgeIndices[at]].push_back(patch.reversed[at]);
        }
    }
    for (std::size_t index = 0; index < directions.size(); ++index) {
        RequireCount(directions[index].size(), 2, "2回使われる");
        Require(directions[index][0] != directions[index][1],
            "線 " + std::to_string(index) + " は互いに逆向き");
    }
}

// ---------------------------------------------------------------- AT-GEO-011

KACHA_V2_TEST(wireCage, 1辺欠けた直方体を断る)
{
    for (std::size_t missing = 0; missing < 12; ++missing) {
        std::vector<CageEdgeInput> edges = Box(10.0, 10.0, 10.0);
        edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(missing));
        const auto result = AnalyzeWireCage(edges, Tolerance());
        Require(!result.HasValue(),
            std::to_string(missing) + " 番の線が欠けたら断ること");
    }
}

KACHA_V2_TEST(wireCage, 重複した辺を断る)
{
    std::vector<CageEdgeInput> edges = Box(10.0, 10.0, 10.0);
    CageEdgeInput duplicate = edges[3];
    duplicate.segmentId = edges[0].segmentId;   // IDだけ別でも位置が同じ
    edges.push_back(edges[3]);
    RequireRejects(edges, "GEO-S009", "同じ線が2本");
}

KACHA_V2_TEST(wireCage, T字に飛び出た辺を断る)
{
    // 直方体の1辺の途中から、外へ棒が1本出ている形。
    std::vector<CageEdgeInput> edges = Box(10.0, 10.0, 10.0);
    Builder extra;
    extra.Add({5.0, 0.0, 0.0}, {5.0, -8.0, 0.0});
    edges.push_back(extra.edges.front());
    const auto result = AnalyzeWireCage(edges, Tolerance());
    Require(!result.HasValue(), "断ること");
    RequireEqual(result.Diagnostics().front().code, std::string("GEO-S003"),
        "端がつながっていないと言うこと");
}

KACHA_V2_TEST(wireCage, 長さ0の線を断る)
{
    std::vector<CageEdgeInput> edges = Box(10.0, 10.0, 10.0);
    Builder extra;
    // 長さ0の直線は CurveSegment が作らせないので、極端に短いものを入れる。
    extra.Add({5.0, 5.0, 5.0}, {5.0, 5.0, 5.0 + 1e-9});
    edges.push_back(extra.edges.front());
    const auto result = AnalyzeWireCage(edges, Tolerance());
    Require(!result.HasValue(), "断ること");
}

KACHA_V2_TEST(wireCage, 平らに潰れた形を断る)
{
    // 四角形1枚(4辺)。面はできるが立体にならない。
    Builder builder;
    builder.Add({0, 0, 0}, {10, 0, 0});
    builder.Add({10, 0, 0}, {10, 10, 0});
    builder.Add({10, 10, 0}, {0, 10, 0});
    builder.Add({0, 10, 0}, {0, 0, 0});
    const auto result = AnalyzeWireCage(builder.edges, Tolerance());
    Require(!result.HasValue(), "断ること");
}

KACHA_V2_TEST(wireCage, 線が少なすぎたら断る)
{
    Builder builder;
    builder.Add({0, 0, 0}, {10, 0, 0});
    builder.Add({10, 0, 0}, {10, 10, 0});
    builder.Add({10, 10, 0}, {0, 0, 0});
    RequireRejects(builder.edges, "GEO-S001", "3本しかない");
}

KACHA_V2_TEST(wireCage, 離れた2つの箱は2つの候補にならず断るか分ける)
{
    // 直方体2つを離して置く。つながっていないので、片方だけでは全辺が使われない。
    std::vector<CageEdgeInput> edges = Box(10.0, 10.0, 10.0);
    Builder second;
    const Vector3 offset{50.0, 0.0, 0.0};
    const Vector3 corner[8]{
        offset + Vector3{0, 0, 0}, offset + Vector3{10, 0, 0},
        offset + Vector3{10, 10, 0}, offset + Vector3{0, 10, 0},
        offset + Vector3{0, 0, 10}, offset + Vector3{10, 0, 10},
        offset + Vector3{10, 10, 10}, offset + Vector3{0, 10, 10},
    };
    second.Add(corner[0], corner[1]);
    second.Add(corner[1], corner[2]);
    second.Add(corner[2], corner[3]);
    second.Add(corner[3], corner[0]);
    second.Add(corner[4], corner[5]);
    second.Add(corner[5], corner[6]);
    second.Add(corner[6], corner[7]);
    second.Add(corner[7], corner[4]);
    second.Add(corner[0], corner[4]);
    second.Add(corner[1], corner[5]);
    second.Add(corner[2], corner[6]);
    second.Add(corner[3], corner[7]);
    for (const auto& edge : second.edges) {
        edges.push_back(edge);
    }
    const auto result = AnalyzeWireCage(edges, Tolerance());
    // 全辺が2回使われる組合せは「両方の箱」なので、面12枚の候補になる。
    // 黙って片方だけを採ってはいけない。
    if (result.HasValue()) {
        RequireCount(result.Value().shells[0].patches.size(), 12, "両方ぶんの面");
    } else {
        Require(!result.Diagnostics().empty(), "断るなら理由を言うこと");
    }
}

KACHA_V2_TEST(wireCage, 平面でない面は根拠が無ければ埋めない)
{
    // かまぼこ形。曲面の根拠(形状ガイド)が無いので、勝手に埋めてはいけない。
    Builder builder;
    const double radius = 10.0;
    const double height = 20.0;
    builder.Add({-radius, 0, 0}, {radius, 0, 0});
    builder.AddArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, radius, 0.0, 3.14159265358979323846);
    builder.Add({-radius, 0, height}, {radius, 0, height});
    builder.AddArc({0, 0, height}, {0, 0, 1}, {1, 0, 0}, radius, 0.0,
        3.14159265358979323846);
    builder.Add({-radius, 0, 0}, {-radius, 0, height});
    builder.Add({radius, 0, 0}, {radius, 0, height});
    const auto result = AnalyzeWireCage(builder.edges, Tolerance());
    Require(!result.HasValue(), "根拠の無い曲面を埋めないこと");
    RequireEqual(result.Diagnostics().front().code, std::string("GEO-S003"),
        "囲めていないと言うこと");
}

KACHA_V2_TEST(wireCage, 根拠を示した曲面があれば立体になる)
{
    Builder builder;
    const double radius = 10.0;
    const double height = 20.0;
    builder.Add({-radius, 0, 0}, {radius, 0, 0});                                  // 0 下の直径
    builder.AddArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, radius, 0.0, 3.14159265358979323846); // 1 下の半円
    builder.Add({-radius, 0, height}, {radius, 0, height});                        // 2 上の直径
    builder.AddArc({0, 0, height}, {0, 0, 1}, {1, 0, 0}, radius, 0.0,
        3.14159265358979323846);                                                   // 3 上の半円
    builder.Add({-radius, 0, 0}, {-radius, 0, height});                            // 4 縦
    builder.Add({radius, 0, 0}, {radius, 0, height});                              // 5 縦

    CageDeclaredPatch curved;
    curved.edgeIndices = {1, 3, 4, 5};   // 半円2本と縦2本で囲まれた曲面
    const auto result = AnalyzeWireCage(builder.edges, {curved}, Tolerance());
    Require(result.HasValue(), "見つかること ("
            + (result.Diagnostics().empty() ? std::string("診断なし")
                                            : result.Diagnostics().front().detailsJa)
            + ")");
    RequireCount(result.Value().shells[0].patches.size(), 4, "面の数(上下・平面・曲面)");
    const std::size_t declaredCount = static_cast<std::size_t>(
        std::count_if(result.Value().shells[0].patches.begin(),
            result.Value().shells[0].patches.end(),
            [](const auto& patch) { return patch.declared; }));
    RequireCount(declaredCount, 1, "根拠を示した面が1枚");
    // 平面でない面が混ざるので、ここでの体積は概算。正しい値は OCCT が出す。
    // この層の役目は「潰れていないか」の足切りなので、概算であることを明示する。
    Require(result.Value().shells[0].volumeIsApproximate, "概算だと言うこと");
    Require(result.Value().shells[0].volumeMm3 > 0.0, "潰れていないこと");
    const double bounding = 2.0 * radius * radius * height;
    Require(result.Value().shells[0].volumeMm3 < bounding,
        "外接する箱より小さいこと (" + std::to_string(result.Value().shells[0].volumeMm3)
            + " / " + std::to_string(bounding) + ")");
}

KACHA_V2_TEST(wireCage, 輪になっていない面の指定を断る)
{
    Builder builder;
    const double radius = 10.0;
    const double height = 20.0;
    builder.Add({-radius, 0, 0}, {radius, 0, 0});
    builder.AddArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, radius, 0.0, 3.14159265358979323846);
    builder.Add({-radius, 0, height}, {radius, 0, height});
    builder.AddArc({0, 0, height}, {0, 0, 1}, {1, 0, 0}, radius, 0.0,
        3.14159265358979323846);
    builder.Add({-radius, 0, 0}, {-radius, 0, height});
    builder.Add({radius, 0, 0}, {radius, 0, height});

    CageDeclaredPatch broken;
    broken.edgeIndices = {1, 3, 4};   // 縦が1本足りない
    const auto missing = AnalyzeWireCage(builder.edges, {broken}, Tolerance());
    Require(!missing.HasValue(), "輪になっていなければ断ること");

    CageDeclaredPatch outside;
    outside.edgeIndices = {1, 3, 4, 99};   // 選ばれていない線
    const auto unknown = AnalyzeWireCage(builder.edges, {outside}, Tolerance());
    Require(!unknown.HasValue(), "選ばれていない線を指したら断ること");
}

KACHA_V2_TEST(wireCage, 何度呼んでも同じ答えを返す)
{
    const auto edges = Box(13.0, 17.0, 19.0);
    const auto first = AnalyzeWireCage(edges, Tolerance());
    Require(first.HasValue(), "見つかること");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = AnalyzeWireCage(edges, Tolerance());
        Require(again.HasValue(), "毎回見つかること");
        RequireCount(again.Value().shells.size(), first.Value().shells.size(), "立体の数");
        RequireNear(again.Value().shells[0].volumeMm3, first.Value().shells[0].volumeMm3,
            0.0, "体積が完全に同じ");
    }
}

// ---------------------------------------------------------------- 意味的キー

KACHA_V2_TEST(subshapeKey, 押し出しのキーを往復できる)
{
    DeterministicIdGenerator ids{3};
    const SegmentId segment = ids.NextTyped<IdKind::Segment>();

    RequireEqual(MakeExtrudeCapStart().ToString(), std::string("extrude/cap/start"), "始端");
    RequireEqual(MakeExtrudeCapEnd().ToString(), std::string("extrude/cap/end"), "終端");
    RequireEqual(MakeExtrudeSide(segment).ToString(),
        "extrude/side/" + segment.ToString(), "側面");

    const auto parsed = ParseSubshapeKey(MakeExtrudeSide(segment).ToString());
    Require(parsed.HasValue(), "読めること");
    Require(parsed.Value().kind == SubshapeKind::ExtrudeSide, "種類");
    RequireEqual(parsed.Value().sourceSegmentId.ToString(), segment.ToString(), "元の線");
}

KACHA_V2_TEST(subshapeKey, ロフトとBooleanのキーを往復できる)
{
    DeterministicIdGenerator ids{5};
    const SegmentId first = ids.NextTyped<IdKind::Segment>();
    const SegmentId second = ids.NextTyped<IdKind::Segment>();
    const EntityId part = ids.NextTyped<IdKind::Entity>();

    const auto loft = ParseSubshapeKey(MakeLoftSpan(first, second).ToString());
    Require(loft.HasValue(), "ロフトが読めること");
    RequireEqual(loft.Value().firstSectionSegmentId.ToString(), first.ToString(), "断面A");
    RequireEqual(loft.Value().secondSectionSegmentId.ToString(), second.ToString(), "断面B");

    const std::string nested = MakeExtrudeSide(first).ToString();
    const auto boolean = ParseSubshapeKey(MakeBooleanProvenance(part, nested).ToString());
    Require(boolean.HasValue(), "Booleanが読めること");
    RequireEqual(boolean.Value().sourcePartId.ToString(), part.ToString(), "元の部品");
    RequireEqual(boolean.Value().nestedKey, nested, "中のキー");

    // Boolean を重ねても壊れないこと。
    const std::string twice =
        MakeBooleanProvenance(part, MakeBooleanProvenance(part, nested).ToString())
            .ToString();
    const auto deep = ParseSubshapeKey(twice);
    Require(deep.HasValue(), "2重でも読めること");
}

KACHA_V2_TEST(subshapeKey, 読めない記号を断る)
{
    const char* bad[]{
        "",
        "extrude",
        "extrude/cap",
        "extrude/cap/middle",
        "extrude/side/not-a-uuid",
        "loft/span/only-one",
        "cage/patch/",
        "boolean/provenance/not-a-uuid/extrude/cap/start",
        "magic/face/1",
        "face:12",          // OCCTの面番号のようなもの。絶対に受け付けない
        "0",
        "///",
    };
    for (const char* text : bad) {
        const auto parsed = ParseSubshapeKey(text);
        Require(!parsed.HasValue(), std::string("断ること: ") + text);
        RequireEqual(parsed.Diagnostics().front().code, std::string("GEO-K001"),
            std::string("診断コード: ") + text);
    }
}

KACHA_V2_TEST_MAIN("wire_cage_tests")
