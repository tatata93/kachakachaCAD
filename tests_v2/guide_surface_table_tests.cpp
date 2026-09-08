// 形状ガイドの役割テーブル(AT-UIX-007)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <set>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;
using namespace kachakacha::v2::modeling;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    return CurveSegment::MakeLine(start, end).Value();
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    return GeometryTolerance{};
}

class Ids {
public:
    [[nodiscard]] EntityId Next() { return EntityId(generator_.Next()); }

private:
    DeterministicIdGenerator generator_{7};
};

[[nodiscard]] GuideTableSelection Selection(Ids& ids, std::string label,
    std::vector<CurveSegment> segments)
{
    GuideTableSelection selection;
    selection.sourceWireId = ids.Next();
    selection.label = std::move(label);
    selection.segments = std::move(segments);
    return selection;
}

//! 断面2枚と外形2本の、よくある入力。
struct Fixture {
    Ids ids;
    GuideTable table;
};

[[nodiscard]] Fixture MakeGuidedLoftFixture()
{
    Fixture fixture;
    fixture.table.method = GuideSurfaceMethod::GuidedLoft;
    // 外形Uを2本。x=0 と x=100 の断面の端をつなぐ。
    fixture.table = AddSelectionAsNewRow(fixture.table, ChainRole::GuideU,
        Selection(fixture.ids, "guide_a", {Line({0, 0, 0}, {100, 0, 0})}))
                        .Value();
    fixture.table = AddSelectionAsNewRow(fixture.table, ChainRole::GuideU,
        Selection(fixture.ids, "guide_b", {Line({0, 0, 40}, {100, 0, 40})}))
                        .Value();
    fixture.table = AddSelectionAsNewRow(fixture.table, ChainRole::Section,
        Selection(fixture.ids, "sec_1", {Line({0, 0, 0}, {0, 0, 40})}))
                        .Value();
    fixture.table = AddSelectionAsNewRow(fixture.table, ChainRole::Section,
        Selection(fixture.ids, "sec_2", {Line({100, 0, 0}, {100, 0, 40})}))
                        .Value();
    return fixture;
}

} // namespace

KACHA_V2_TEST(guide_table, 方法ごとに使う役割が決まっている)
{
    Require(RoleUsedByMethod(GuideSurfaceMethod::PlanarBoundary, ChainRole::OuterBoundary),
        "平面は外形を使う");
    Require(!RoleUsedByMethod(GuideSurfaceMethod::PlanarBoundary, ChainRole::Section),
        "平面は断面を使わない");
    Require(RoleUsedByMethod(GuideSurfaceMethod::GuidedLoft, ChainRole::GuideU),
        "案内付きは外形Uを使う");
    Require(RoleUsedByMethod(GuideSurfaceMethod::GordonNetwork, ChainRole::GuideV),
        "Gordon は外形Vを使う");
    Require(!RoleUsedByMethod(GuideSurfaceMethod::LoftSections, ChainRole::GuideU),
        "ロフトは外形Uを使わない");
}

KACHA_V2_TEST(guide_table, 役割の名前が日本語でそろっている)
{
    std::set<std::string> labels;
    for (ChainRole role : {ChainRole::OuterBoundary, ChainRole::HoleBoundary,
             ChainRole::Section, ChainRole::GuideU, ChainRole::GuideV,
             ChainRole::BoundarySide, ChainRole::SourceSurface}) {
        const std::string label = ChainRoleLabelJa(role);
        Require(!label.empty() && label != "不明", "名前がある");
        Require(labels.insert(label).second, "名前が重ならない: " + label);
    }
}

KACHA_V2_TEST(guide_table, 使わない役割の行は作れない)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::LoftSections;
    const auto result = AddSelectionAsNewRow(table, ChainRole::GuideU,
        Selection(ids, "w1", {Line({0, 0, 0}, {10, 0, 0})}));
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-R003", "役割違い");
}

KACHA_V2_TEST(guide_table, 線を選んでいなければ行を作らない)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::LoftSections;
    const auto result = AddSelectionAsNewRow(table, ChainRole::Section,
        Selection(ids, "w1", {}));
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-R004", "線がない");
}

KACHA_V2_TEST(guide_table, 複数行を作れて役割ごとに1から数える)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto views = BuildGuideTableView(fixture.table, Tolerance());
    RequireEqual(std::to_string(views.size()), "4", "4行");
    RequireEqual(views[0].roleLabelJa + std::to_string(views[0].number), "外形U1", "外形U1");
    RequireEqual(views[1].roleLabelJa + std::to_string(views[1].number), "外形U2", "外形U2");
    RequireEqual(views[2].roleLabelJa + std::to_string(views[2].number), "断面1", "断面1");
    RequireEqual(views[3].roleLabelJa + std::to_string(views[3].number), "断面2", "断面2");
}

KACHA_V2_TEST(guide_table, 1行に複数のワイヤーを足せる)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::LoftSections;
    table = AddSelectionAsNewRow(table, ChainRole::Section,
        Selection(ids, "arc_1", {Line({0, 0, 0}, {10, 0, 0})}))
                .Value();
    const auto added = AddSelectionToRow(table, 0,
        Selection(ids, "line_2", {Line({10, 0, 0}, {10, 10, 0})}), Tolerance());
    Require(added.HasValue(), "足せた");
    const auto more = AddSelectionToRow(added.Value(), 0,
        Selection(ids, "arc_3", {Line({10, 10, 0}, {0, 10, 0})}), Tolerance());
    Require(more.HasValue(), "もう1本足せた");
    const auto views = BuildGuideTableView(more.Value(), Tolerance());
    RequireEqual(std::to_string(views[0].segmentCount), "3", "3線分");
    RequireEqual(views[0].sourceLabelJa, "arc_1 + line_2 + arc_3", "元ワイヤー列");
}

KACHA_V2_TEST(guide_table, つながらない線は行へ足さない)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::LoftSections;
    table = AddSelectionAsNewRow(table, ChainRole::Section,
        Selection(ids, "arc_1", {Line({0, 0, 0}, {10, 0, 0})}))
                .Value();
    const auto added = AddSelectionToRow(table, 0,
        Selection(ids, "far_away", {Line({50, 0, 0}, {60, 0, 0})}), Tolerance());
    Require(!added.HasValue(), "断る");
    RequireEqual(FirstCode(added.Diagnostics()), "UI-R005", "つながらない");
    Require(added.Diagnostics().front().detailsJa.find("mm") != std::string::npos,
        "どれだけ離れているかを言う");
}

KACHA_V2_TEST(guide_table, 同じワイヤーを2つの行へ入れない)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::LoftSections;
    GuideTableSelection selection = Selection(ids, "w1", {Line({0, 0, 0}, {10, 0, 0})});
    table = AddSelectionAsNewRow(table, ChainRole::Section, selection).Value();
    const auto again = AddSelectionAsNewRow(table, ChainRole::Section, selection);
    Require(!again.HasValue(), "断る");
    RequireEqual(FirstCode(again.Diagnostics()), "UI-R006", "二重");
    const auto intoRow = AddSelectionToRow(table, 0, selection, Tolerance());
    Require(!intoRow.HasValue(), "同じ行へも入れない");
    RequireEqual(FirstCode(intoRow.Diagnostics()), "UI-R006", "二重");
}

KACHA_V2_TEST(guide_table, 行の順序を変えられる)
{
    Fixture fixture = MakeGuidedLoftFixture();
    // 断面2を上へ。断面1と入れ替わり、番号が付け直される。
    const auto moved = MoveRow(fixture.table, 3, -1);
    Require(moved.HasValue(), "動かせた");
    const auto views = BuildGuideTableView(moved.Value(), Tolerance());
    RequireEqual(views[2].sourceLabelJa, "sec_2", "断面1がsec_2になる");
    RequireEqual(std::to_string(views[2].number), "1", "番号は1");
    RequireEqual(views[3].sourceLabelJa, "sec_1", "断面2がsec_1になる");
    RequireEqual(std::to_string(views[3].number), "2", "番号は2");
}

KACHA_V2_TEST(guide_table, 順序変更は同じ役割の中だけで起きる)
{
    Fixture fixture = MakeGuidedLoftFixture();
    // 断面1(表では3行目)を上へ。外形Uとは入れ替わらず、端なので断る。
    const auto moved = MoveRow(fixture.table, 2, -1);
    Require(!moved.HasValue(), "断る");
    RequireEqual(FirstCode(moved.Diagnostics()), "UI-R008", "端の行");
    const auto down = MoveRow(fixture.table, 3, 1);
    Require(!down.HasValue(), "下端も断る");
    RequireEqual(FirstCode(down.Diagnostics()), "UI-R008", "端の行");
}

KACHA_V2_TEST(guide_table, 行は1つずつしか動かせない)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto jumped = MoveRow(fixture.table, 3, -2);
    Require(!jumped.HasValue(), "断る");
    RequireEqual(FirstCode(jumped.Diagnostics()), "UI-R007", "1つずつ");
}

KACHA_V2_TEST(guide_table, 無い行を指したら断る)
{
    Fixture fixture = MakeGuidedLoftFixture();
    for (const auto& result : {MoveRow(fixture.table, 9, 1), RemoveRow(fixture.table, 9),
             ReverseRow(fixture.table, 9)}) {
        Require(!result.HasValue(), "断る");
        RequireEqual(FirstCode(result.Diagnostics()), "UI-R001", "行がない");
    }
}

KACHA_V2_TEST(guide_table, 行を削除できる)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto removed = RemoveRow(fixture.table, 2);
    Require(removed.HasValue(), "消せた");
    const auto views = BuildGuideTableView(removed.Value(), Tolerance());
    RequireEqual(std::to_string(views.size()), "3", "3行");
    RequireEqual(views[2].sourceLabelJa, "sec_2", "残ったのはsec_2");
    RequireEqual(std::to_string(views[2].number), "1", "番号は詰め直される");
}

KACHA_V2_TEST(guide_table, 方向を反転できる)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::LoftSections;
    table = AddSelectionAsNewRow(table, ChainRole::Section,
        Selection(ids, "a", {Line({0, 0, 0}, {10, 0, 0})}))
                .Value();
    table = AddSelectionToRow(table, 0, Selection(ids, "b", {Line({10, 0, 0}, {10, 10, 0})}),
        Tolerance())
                .Value();
    const auto flipped = ReverseRow(table, 0);
    Require(flipped.HasValue(), "反転できた");
    const auto views = BuildGuideTableView(flipped.Value(), Tolerance());
    RequireEqual(views[0].directionLabelJa, "逆", "方向列が逆");
    RequireNear(views[0].startPoint.x, 10.0, 1e-9, "始点が入れ替わる");
    RequireNear(views[0].startPoint.y, 10.0, 1e-9, "始点が入れ替わる");
    RequireNear(views[0].endPoint.x, 0.0, 1e-9, "終点が入れ替わる");
    RequireEqual(views[0].sourceLabelJa, "b + a", "元ワイヤーの並びも逆になる");
    // 2度反転すれば元に戻る。
    const auto back = ReverseRow(flipped.Value(), 0);
    Require(back.HasValue(), "戻せた");
    const auto restored = BuildGuideTableView(back.Value(), Tolerance());
    RequireEqual(restored[0].directionLabelJa, "正", "方向が戻る");
    RequireNear(restored[0].startPoint.x, 0.0, 1e-9, "始点が戻る");
    RequireEqual(restored[0].sourceLabelJa, "a + b", "並びも戻る");
}

KACHA_V2_TEST(guide_table, 反転しても曲線の種類が変わらない)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::LoftSections;
    const CurveSegment arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0},
        20.0, 0.0, 1.2)
                                 .Value();
    const CurveSegment bezier = CurveSegment::MakeCubicBezier(
        {{20.0 * std::cos(1.2), 20.0 * std::sin(1.2), 0.0}, {5, 30, 0}, {-5, 35, 0},
            {-20, 30, 0}})
                                    .Value();
    table = AddSelectionAsNewRow(table, ChainRole::Section, Selection(ids, "arc", {arc}))
                .Value();
    table = AddSelectionToRow(table, 0, Selection(ids, "bez", {bezier}), Tolerance()).Value();
    const auto flipped = ReverseRow(table, 0);
    Require(flipped.HasValue(), "反転できた");
    const auto& segments = flipped.Value().rows[0].segments;
    RequireEqual(std::string(CurveKindName(segments[0].Kind())), "CubicBezier",
        "ベジェのまま");
    RequireEqual(std::string(CurveKindName(segments[1].Kind())), "CircularArc",
        "円弧のまま");
    // 端点も入れ替わっている。
    RequireNear((segments[0].StartPoint() - bezier.EndPoint()).Length(), 0.0, 1e-9,
        "始点はベジェの終点");
    RequireNear((segments[1].EndPoint() - arc.StartPoint()).Length(), 0.0, 1e-9,
        "終点は円弧の始点");
}

KACHA_V2_TEST(guide_table, 両端が外形へ届いていれば有効と出る)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto views = BuildGuideTableView(fixture.table, Tolerance());
    RequireEqual(views[2].connectionLabelJa, "有効", "断面1");
    RequireEqual(views[3].connectionLabelJa, "有効", "断面2");
    Require(views[2].startConnected && views[2].endConnected, "両端とも届く");
}

KACHA_V2_TEST(guide_table, 片側だけ届いていればそう出る)
{
    Fixture fixture = MakeGuidedLoftFixture();
    // 外形Uを1本消すと、断面の上端が届かなくなる。
    const auto trimmed = RemoveRow(fixture.table, 1);
    Require(trimmed.HasValue(), "消せた");
    const auto views = BuildGuideTableView(trimmed.Value(), Tolerance());
    RequireEqual(views[1].connectionLabelJa, "片側だけ", "断面1");
    Require(views[1].startConnected != views[1].endConnected, "片方だけ届く");
}

KACHA_V2_TEST(guide_table, どちらも届かなければ未接続と出る)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::GuidedLoft;
    table = AddSelectionAsNewRow(table, ChainRole::GuideU,
        Selection(ids, "guide", {Line({0, 0, 0}, {100, 0, 0})}))
                .Value();
    table = AddSelectionAsNewRow(table, ChainRole::Section,
        Selection(ids, "sec", {Line({0, 0, 50}, {0, 0, 90})}))
                .Value();
    const auto views = BuildGuideTableView(table, Tolerance());
    RequireEqual(views[1].connectionLabelJa, "未接続", "断面");
}

KACHA_V2_TEST(guide_table, 外形の行は閉じているかどうかを出す)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::PlanarBoundary;
    table = AddSelectionAsNewRow(table, ChainRole::OuterBoundary,
        Selection(ids, "open", {Line({0, 0, 0}, {10, 0, 0})}))
                .Value();
    const auto views = BuildGuideTableView(table, Tolerance());
    RequireEqual(views[0].connectionLabelJa, "開いている", "開いた外形");

    GuideTable closed;
    closed.method = GuideSurfaceMethod::PlanarBoundary;
    closed = AddSelectionAsNewRow(closed, ChainRole::OuterBoundary,
        Selection(ids, "square",
            {Line({0, 0, 0}, {10, 0, 0}), Line({10, 0, 0}, {10, 10, 0}),
                Line({10, 10, 0}, {0, 10, 0}), Line({0, 10, 0}, {0, 0, 0})}))
                 .Value();
    const auto closedViews = BuildGuideTableView(closed, Tolerance());
    RequireEqual(closedViews[0].connectionLabelJa, "閉じている", "閉じた外形");
}

KACHA_V2_TEST(guide_table, 色は役割と番号だけで決まる)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto views = BuildGuideTableView(fixture.table, Tolerance());
    std::set<std::string> colors;
    for (const auto& view : views) {
        const std::string key = std::to_string(view.color.red) + ","
            + std::to_string(view.color.green) + "," + std::to_string(view.color.blue);
        Require(colors.insert(key).second, "4行が別の色: " + key);
        // 表と3Dが同じ式を通るので、必ず一致する。
        const RowColor direct = ColorForRow(view.role, view.number);
        Require(direct == view.color, "表の色と3Dの色が一致する");
    }
}

KACHA_V2_TEST(guide_table, 色は行を並べ替えても番号について決まる)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto before = BuildGuideTableView(fixture.table, Tolerance());
    const auto moved = MoveRow(fixture.table, 3, -1);
    Require(moved.HasValue(), "動かせた");
    const auto after = BuildGuideTableView(moved.Value(), Tolerance());
    // 断面1の色は、中身が入れ替わっても「断面1の色」のまま。
    Require(before[2].color == after[2].color, "断面1の色は変わらない");
    Require(before[3].color == after[3].color, "断面2の色は変わらない");
    RequireEqual(after[2].sourceLabelJa, "sec_2", "中身は入れ替わっている");
}

KACHA_V2_TEST(guide_table, 足りない役割を案内する)
{
    GuideTable table;
    table.method = GuideSurfaceMethod::GuidedLoft;
    const auto guidance = MissingRoleGuidanceJa(table);
    RequireEqual(std::to_string(guidance.size()), "2", "断面と外形Uの2つ");
    for (const std::string& line : guidance) {
        Require(!line.empty(), "案内が空でない");
    }

    GuideTable planar;
    planar.method = GuideSurfaceMethod::PlanarBoundary;
    // 穴は無くてよいので、案内は外形だけ。
    RequireEqual(std::to_string(MissingRoleGuidanceJa(planar).size()), "1", "外形だけ");
}

KACHA_V2_TEST(guide_table, 役割がそろってから面の要求へ変えられる)
{
    GuideTable empty;
    empty.method = GuideSurfaceMethod::GuidedLoft;
    const auto refused = ToGuideSurfaceRequest(empty, Tolerance());
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "UI-R009", "そろっていない");

    Fixture fixture = MakeGuidedLoftFixture();
    const auto request = ToGuideSurfaceRequest(fixture.table, Tolerance());
    Require(request.HasValue(), "変えられた");
    RequireEqual(std::to_string(request.Value().chains.size()), "4", "4本");
    RequireEqual(std::to_string(request.Value().chains[2].index), "1", "断面1");
    RequireEqual(std::to_string(request.Value().chains[3].index), "2", "断面2");
    Require(request.Value().chains[0].role == ChainRole::GuideU, "役割が残る");
}

KACHA_V2_TEST(guide_table, 面の要求の番号は表の並びで振り直す)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto moved = MoveRow(fixture.table, 3, -1).Value();
    const auto request = ToGuideSurfaceRequest(moved, Tolerance());
    Require(request.HasValue(), "変えられた");
    // 表で上にある方が1番。中身は sec_2 になっている。
    RequireEqual(std::to_string(request.Value().chains[2].index), "1", "断面1");
    RequireNear(request.Value().chains[2].segments.front().StartPoint().x, 100.0, 1e-9,
        "中身は sec_2");
}

KACHA_V2_TEST(guide_table, 閉じた行は閉じていると伝える)
{
    Ids ids;
    GuideTable table;
    table.method = GuideSurfaceMethod::PlanarBoundary;
    table = AddSelectionAsNewRow(table, ChainRole::OuterBoundary,
        Selection(ids, "square",
            {Line({0, 0, 0}, {10, 0, 0}), Line({10, 0, 0}, {10, 10, 0}),
                Line({10, 10, 0}, {0, 10, 0}), Line({0, 10, 0}, {0, 0, 0})}))
                .Value();
    const auto request = ToGuideSurfaceRequest(table, Tolerance());
    Require(request.HasValue(), "変えられた");
    Require(request.Value().chains[0].closed, "閉じている");
}

KACHA_V2_TEST(guide_table, 方法を変えるとき残った行を黙って消さない)
{
    Fixture fixture = MakeGuidedLoftFixture();
    const auto changed = SetGuideTableMethod(fixture.table, GuideSurfaceMethod::LoftSections);
    Require(!changed.HasValue(), "断る");
    RequireEqual(FirstCode(changed.Diagnostics()), "UI-R003", "役割が残っている");
    Require(changed.Diagnostics().front().detailsJa.find("外形U") != std::string::npos,
        "どの役割かを言う");

    // 外形Uの行を消してからなら変えられる。
    GuideTable trimmed = RemoveRow(fixture.table, 0).Value();
    trimmed = RemoveRow(trimmed, 0).Value();
    const auto ok = SetGuideTableMethod(trimmed, GuideSurfaceMethod::LoftSections);
    Require(ok.HasValue(), "変えられた");
    Require(ok.Value().method == GuideSurfaceMethod::LoftSections, "方法が変わる");
    RequireEqual(std::to_string(ok.Value().rows.size()), "2", "断面2行が残る");
}

KACHA_V2_TEST_MAIN("guide_surface_table_tests")
