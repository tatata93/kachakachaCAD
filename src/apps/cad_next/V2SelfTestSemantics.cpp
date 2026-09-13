//! 意味状態と部分要素の見え方のケース(ui-ux-integrated-spec §3)。
//!
//! ここで見るのは「状態ごとに描き分けているか」である。
//! 状態そのものの決め方は core の試験(tests_v2/semantic_state_tests.cpp)で見る。
//! こちらは、**画面がその決め方を本当に使っているか** と、
//! テーマを替えても色が潰れないかを確かめる。
//!
//! V2SelfTestScreen.cpp から分けたのは、ファイルの長さの門(1500行)を守るため。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/DisplaySettings.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SemanticState.h"
#include "kachakacha/geometry/ScreenMapping.h"

#include <QColor>
#include <QPen>
#include <QPointF>

#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::SemanticState;
using kachakacha::v2::app::SemanticStateNameJa;
using kachakacha::v2::domain::EntityKind;

//! 折れ線を1本引く。同じワイヤーの中に線分が2つできる。
//!
//! 「同じワイヤーの選んだ線分だけを強調する」を確かめるには、
//! 1つの物体の中に線分が2つ以上要る。線を2本引いても物体が2つになるだけである。
struct TwoSegmentWire {
    kachakacha::v2::base::EntityId entityId;
    kachakacha::v2::base::SegmentId first;
    kachakacha::v2::base::SegmentId second;
    //! それぞれの線分の途中(端点から離れた場所)の画面位置。
    QPointF onFirst;
    QPointF onSecond;
};

[[nodiscard]] std::optional<TwoSegmentWire> DrawTwoSegmentWire(V2MainWindow& window)
{
    using kachakacha::v2::geometry::Vector3;
    using kachakacha::v2::modeling::DrawingTool;
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    const Vector3 corners[3] = {
        {-40.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 40.0, 0.0}};
    window.SelectTool(DrawingTool::Polyline);
    for (const Vector3& corner : corners) {
        const auto screen = viewport.Mapping().Project(corner);
        if (!screen.has_value()) {
            window.SelectTool(DrawingTool::Select);
            return std::nullopt;
        }
        viewport.ClickAt(QPointF(screen->x, screen->y));
    }
    viewport.FinishTool();
    window.SelectTool(DrawingTool::Select);
    const auto& curves = window.Session().Scene().curves;
    if (curves.size() != 2 || curves[0].entityId != curves[1].entityId) {
        return std::nullopt;
    }
    // 角から離れた場所を押す。角の近くでは2本とも当たり判定に入り、
    // どちらの線分を選んだのかが決まらない。
    const auto onFirst = viewport.Mapping().Project(Vector3{-30.0, 0.0, 0.0});
    const auto onSecond = viewport.Mapping().Project(Vector3{0.0, 30.0, 0.0});
    if (!onFirst.has_value() || !onSecond.has_value()) {
        return std::nullopt;
    }
    TwoSegmentWire wire;
    wire.entityId = curves[0].entityId;
    wire.first = curves[0].segmentId;
    wire.second = curves[1].segmentId;
    wire.onFirst = QPointF(onFirst->x, onFirst->y);
    wire.onSecond = QPointF(onSecond->x, onSecond->y);
    return wire;
}

[[nodiscard]] bool CaseSelectedSegmentDoesNotLightTheWholeWire(V2MainWindow& window)
{
    // 折れ線の1本を選んだのに全部が選択色になると、
    // 次の操作の相手がどれなのか画面から読めない(ui-ux-integrated-spec §4.1)。
    const auto wire = DrawTwoSegmentWire(window);
    if (!Explain("線分が2つある折れ線を引ける", wire.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    viewport.SelectAt(wire->onFirst, Qt::NoModifier);
    if (!Explain((std::string("1本目の線分を選べる(")
                     + window.StatusText().toStdString() + ")").c_str(),
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 1)) {
        return false;
    }
    if (!Explain("選んだ線分は選択の状態",
            viewport.CurveStateOf(wire->entityId, wire->first)
                == SemanticState::Selected)) {
        return false;
    }
    const auto other = viewport.CurveStateOf(wire->entityId, wire->second);
    if (!Explain((std::string("同じワイヤーの別の線分は選択色にしない(")
                     + SemanticStateNameJa(other) + ")").c_str(),
            other != SemanticState::Selected)) {
        return false;
    }
    // 状態が別でも同じ色で描いていたら、画面からは読めない。色でも確かめる。
    if (!Explain("選択色と通常色が違う",
            SemanticInksAreDistinct(viewport.SemanticColor(SemanticState::Selected),
                viewport.SemanticColor(other)))) {
        return false;
    }
    // 制御点の四角も選択色で描く。線だけ1本に絞っても、
    // 別の線分に四角が出ていれば、そこも選んだように見えて掴めてしまう。
    const auto handles = kachakacha::v2::app::ControlPointsForSelection(
        window.Session().Scene(), viewport.Selection());
    std::size_t onFirst = 0;
    std::size_t onSecond = 0;
    for (const auto& handle : handles) {
        if (handle.segmentId == wire->first) {
            ++onFirst;
        }
        if (handle.segmentId == wire->second) {
            ++onSecond;
        }
    }
    if (!Explain((std::string("選んだ線分に制御点が出る(")
                     + std::to_string(onFirst) + " 個)").c_str(),
            onFirst > 0)) {
        return false;
    }
    if (!Explain((std::string("別の線分には制御点を出さない(")
                     + std::to_string(onSecond) + " 個)").c_str(),
            onSecond == 0)) {
        return false;
    }
    // 物体ごと選んだときは、逆に全部が選択でなければならない。
    // 一覧から選んだのに一部しか光らないと、消す・動かす相手が読めない。
    kachakacha::v2::app::SelectionSet whole;
    whole.entityIds.push_back(wire->entityId);
    viewport.SetSelection(whole);
    std::size_t wholeOnSecond = 0;
    for (const auto& handle : kachakacha::v2::app::ControlPointsForSelection(
             window.Session().Scene(), viewport.Selection())) {
        if (handle.segmentId == wire->second) {
            ++wholeOnSecond;
        }
    }
    return Explain("物体ごと選べば全線分が選択",
               viewport.CurveStateOf(wire->entityId, wire->first) == SemanticState::Selected
                   && viewport.CurveStateOf(wire->entityId, wire->second)
                       == SemanticState::Selected)
        && Explain((std::string("物体ごと選べば別の線分にも制御点が出る(")
                       + std::to_string(wholeOnSecond) + " 個)").c_str(),
            wholeOnSecond > 0);
}

[[nodiscard]] bool CaseHoverAndSelectionStayDifferent(V2MainWindow& window)
{
    const auto wire = DrawTwoSegmentWire(window);
    if (!Explain("線分が2つある折れ線を引ける", wire.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    viewport.HoverAt(wire->onFirst);
    if (!Explain((std::string("カーソルの下の線分はHover(")
                     + SemanticStateNameJa(viewport.CurveStateOf(wire->entityId,
                           wire->first))
                     + ")").c_str(),
            viewport.CurveStateOf(wire->entityId, wire->first) == SemanticState::Hover)) {
        return false;
    }
    if (!Explain("触れていない線分は通常のまま",
            viewport.CurveStateOf(wire->entityId, wire->second)
                == SemanticState::Default)) {
        return false;
    }
    if (!Explain("Hoverだけでは選択が増えない",
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 0)) {
        return false;
    }
    // 選んだ線へカーソルを載せても、色がHoverへ落ちてはならない。
    // 落ちると「載せたら選択が外れた」ように見える(§3 規則1)。
    viewport.SelectAt(wire->onFirst, Qt::NoModifier);
    viewport.HoverAt(wire->onFirst);
    if (!Explain("選んだ線へ載せても選択のまま",
            viewport.CurveStateOf(wire->entityId, wire->first)
                == SemanticState::Selected)) {
        return false;
    }
    // 選んだ線分を残したまま、別の線分をHoverにできる。
    viewport.HoverAt(wire->onSecond);
    return Explain("選択とHoverが同時に別の線分へ出る",
        viewport.CurveStateOf(wire->entityId, wire->first) == SemanticState::Selected
            && viewport.CurveStateOf(wire->entityId, wire->second)
                == SemanticState::Hover);
}

//! テーマ1つ分の色を突き合わせる。状態どうしと、補助線との両方を見る。
[[nodiscard]] bool InksStayApart(V2Viewport& viewport, const char* themeName,
    const SemanticState* states, std::size_t count)
{
    for (std::size_t first = 0; first < count; ++first) {
        for (std::size_t second = first + 1; second < count; ++second) {
            if (!Explain((std::string(themeName) + "テーマで「"
                             + SemanticStateNameJa(states[first]) + "」と「"
                             + SemanticStateNameJa(states[second])
                             + "」の色が違う").c_str(),
                    SemanticInksAreDistinct(viewport.SemanticColor(states[first]),
                        viewport.SemanticColor(states[second])))) {
                return false;
            }
        }
    }
    // 補助線は通常表示の仲間だが、状態の色と紛れてはいけない。
    for (std::size_t index = 0; index < count; ++index) {
        if (states[index] == SemanticState::Default) {
            continue;
        }
        if (!Explain((std::string(themeName) + "テーマで補助線と「"
                         + SemanticStateNameJa(states[index]) + "」の色が違う").c_str(),
                SemanticInksAreDistinct(viewport.Colors().construction,
                    viewport.SemanticColor(states[index])))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseSemanticStatesStayApartInEveryTheme(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    const ViewportPalette original = viewport.Colors();
    const SemanticState states[] = {SemanticState::Default, SemanticState::Hover,
        SemanticState::Selected, SemanticState::Snap, SemanticState::Preview};
    const std::pair<const char*, ViewportPalette> themes[] = {
        {"標準", ViewportPalette::Dark()},
        {"Windows 95", ViewportPalette::Win95()},
    };
    bool ok = true;
    for (const auto& theme : themes) {
        if (!ok) {
            break;
        }
        // テーマは色を替えるが、状態の区別は替えない(§3 末尾)。
        viewport.SetPalette(theme.second);
        ok = InksStayApart(viewport, theme.first, states, std::size(states));
    }
    viewport.SetPalette(original);
    if (!ok) {
        return false;
    }
    // 色だけに頼らない。太さでも状態を分ける(§3「別の色と線幅で表す」)。
    // 太さは利用者の線の太さの設定から決まるので、標準・細い線・太い線の全部で見る。
    // (0.25 と 12.0 は線の太さの設定で選べる端である)
    const kachakacha::v2::app::DisplaySettings originalDisplay =
        viewport.DisplaySettingsNow();
    const double widths[] = {originalDisplay.wireWidthPx, 0.25, 0.5, 6.0, 12.0};
    for (const double width : widths) {
        kachakacha::v2::app::DisplaySettings display = originalDisplay;
        display.wireWidthPx = width;
        viewport.SetDisplaySettings(display);
        const double preview = viewport.SemanticWidthPx(SemanticState::Preview);
        const double normal = viewport.SemanticWidthPx(SemanticState::Default);
        const double hover = viewport.SemanticWidthPx(SemanticState::Hover);
        const double selected = viewport.SemanticWidthPx(SemanticState::Selected);
        const std::string detail = "(線の太さ " + std::to_string(width) + "px: 途中経過 "
            + std::to_string(preview) + " / 通常 " + std::to_string(normal) + " / Hover "
            + std::to_string(hover) + " / 選択 " + std::to_string(selected) + ")";
        // 途中経過は確定した線より薄く見せる(§3 規則3)。
        // Hover は選択と違う軽い強調で、選択したように見せない(§3 規則1)。
        ok = Explain(("途中経過は通常より細い" + detail).c_str(),
                 preview > 0.0 && preview < normal)
            && Explain(("Hoverは通常より太い" + detail).c_str(), normal < hover)
            && Explain(("Hoverは選択より細い" + detail).c_str(), hover < selected);
        if (!ok) {
            break;
        }
    }
    viewport.SetDisplaySettings(originalDisplay);
    return ok;
}

[[nodiscard]] bool CasePreviewPenIsFainterThanConfirmedLines(V2MainWindow& window)
{
    // 作図の途中経過・掴んで動かす影・折り曲げの帯は、どれも PreviewPen で描く。
    // 表の太さだけ正しくても、描くペンが固定の太さだと細い線の設定で確定線より太くなる。
    auto& viewport = window.Viewport();
    const kachakacha::v2::app::DisplaySettings originalDisplay =
        viewport.DisplaySettingsNow();
    const double widths[] = {originalDisplay.wireWidthPx, 0.25};
    bool ok = true;
    for (const double width : widths) {
        kachakacha::v2::app::DisplaySettings display = originalDisplay;
        display.wireWidthPx = width;
        viewport.SetDisplaySettings(display);
        const QPen pen = viewport.PreviewPen();
        const double preview = viewport.SemanticWidthPx(SemanticState::Preview);
        const double normal = viewport.SemanticWidthPx(SemanticState::Default);
        const QColor ink = pen.color();
        const QColor expected = viewport.SemanticColor(SemanticState::Preview);
        const std::string detail = "(線の太さ " + std::to_string(width) + "px: ペン "
            + std::to_string(pen.widthF()) + " / 途中経過 " + std::to_string(preview)
            + " / 通常 " + std::to_string(normal) + " / 不透明度 "
            + std::to_string(ink.alphaF()) + ")";
        // §3 規則3: 確定済みより薄く、点線または半透明を併用する。
        ok = Explain(("途中経過のペンは途中経過の太さ" + detail).c_str(),
                 pen.widthF() == preview)
            && Explain(("途中経過のペンは通常より細い" + detail).c_str(),
                pen.widthF() > 0.0 && pen.widthF() < normal)
            && Explain(("途中経過のペンは破線か半透明" + detail).c_str(),
                pen.style() != Qt::SolidLine || ink.alphaF() < 1.0)
            && Explain(("途中経過のペンは途中経過の色" + detail).c_str(),
                ink.red() == expected.red() && ink.green() == expected.green()
                    && ink.blue() == expected.blue());
        if (!ok) {
            break;
        }
    }
    viewport.SetDisplaySettings(originalDisplay);
    return ok;
}

[[nodiscard]] bool CasePreviewIsNotSelectable(V2MainWindow& window)
{
    using kachakacha::v2::geometry::Vector3;
    using kachakacha::v2::modeling::DrawingTool;
    // 途中経過は文書にも場面にも入れない(§3 規則3)。
    // 入れてしまうと、引いている最中の線が選べて、消せて、次の操作の相手になる。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    const auto start = viewport.Mapping().Project(Vector3{-40.0, 0.0, 0.0});
    const auto end = viewport.Mapping().Project(Vector3{40.0, 0.0, 0.0});
    const auto middle = viewport.Mapping().Project(Vector3{0.0, 0.0, 0.0});
    if (!Explain("引く場所が画面に出る",
            start.has_value() && end.has_value() && middle.has_value())) {
        return false;
    }
    window.SelectTool(DrawingTool::Line);
    viewport.ClickAt(QPointF(start->x, start->y));
    viewport.HoverAt(QPointF(end->x, end->y));
    if (!Explain((std::string("途中経過が出ている(")
                     + std::to_string(viewport.PreviewSegmentCount()) + " 本)").c_str(),
            viewport.PreviewSegmentCount() > 0)) {
        return false;
    }
    // 途中経過の上へカーソルを置いても、拾える候補にはならない。
    viewport.HoverAt(QPointF(middle->x, middle->y));
    if (!Explain((std::string("途中経過は拾う候補に出ない(実際は ")
                     + std::to_string(viewport.CandidateCount()) + " 件)").c_str(),
            viewport.CandidateCount() == 0)) {
        return false;
    }
    // 拾えないのは偶然ではない。途中経過は場面にも文書にも入れていないからである。
    if (!Explain((std::string("途中経過は拾う場面に入っていない(線 ")
                     + std::to_string(window.Session().Scene().curves.size())
                     + " 本)").c_str(),
            window.Session().Scene().curves.empty())) {
        return false;
    }
    if (!Explain((std::string("途中経過はまだ文書に入っていない(ワイヤー ")
                     + std::to_string(CountOfKind(window, EntityKind::Wire))
                     + " 本)").c_str(),
            CountOfKind(window, EntityKind::Wire) == 0)) {
        return false;
    }
    // やめれば途中経過は消える。やめた後に同じ場所を押しても、線は何も残っていない。
    // (途中経過が選べないことの証拠は、上の「候補に出ない」「場面に入っていない」である)
    viewport.PressEscape();
    window.SelectTool(DrawingTool::Select);
    if (!Explain((std::string("やめると途中経過が消える(")
                     + std::to_string(viewport.PreviewSegmentCount()) + " 本)").c_str(),
            viewport.PreviewSegmentCount() == 0)) {
        return false;
    }
    viewport.SelectAt(QPointF(middle->x, middle->y), Qt::NoModifier);
    return Explain((std::string("やめた後に同じ場所を押しても線は残っていない(")
                       + std::to_string(kachakacha::v2::app::SelectionItemCount(
                             viewport.Selection()))
                       + " 件)").c_str(),
        kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 0);
}

[[nodiscard]] bool CaseFinishKeepsPreviewOnlyWhileStillDrawing(V2MainWindow& window)
{
    using kachakacha::v2::geometry::Vector3;
    using kachakacha::v2::modeling::DrawingTool;
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    const auto a = viewport.Mapping().Project(Vector3{-40.0, 0.0, 0.0});
    const auto b = viewport.Mapping().Project(Vector3{0.0, 0.0, 0.0});
    const auto c = viewport.Mapping().Project(Vector3{0.0, 40.0, 0.0});
    const auto d = viewport.Mapping().Project(Vector3{40.0, 40.0, 0.0});
    if (!Explain("引く場所が画面に出る",
            a.has_value() && b.has_value() && c.has_value() && d.has_value())) {
        return false;
    }
    // 線は点の数が決まっているので、1点目の後に締めても締められない。
    // 点は残り、まだ引いている途中なので、途中経過も消してはならない。
    window.SelectTool(DrawingTool::Line);
    viewport.ClickAt(QPointF(a->x, a->y));
    viewport.HoverAt(QPointF(b->x, b->y));
    viewport.FinishTool();
    const bool keptOnFailure = viewport.PreviewSegmentCount() > 0;
    viewport.PressEscape();
    if (!Explain((std::string("締められなかったときは途中経過が残る(")
                     + std::to_string(viewport.PreviewSegmentCount()) + " 本)").c_str(),
            keptOnFailure)) {
        window.SelectTool(DrawingTool::Select);
        return false;
    }
    // 折れ線は3点で締められる。締めた後に途中経過が残ると、確定した線に破線が重なる。
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    window.SelectTool(DrawingTool::Polyline);
    viewport.ClickAt(QPointF(a->x, a->y));
    viewport.ClickAt(QPointF(b->x, b->y));
    viewport.ClickAt(QPointF(c->x, c->y));
    viewport.HoverAt(QPointF(d->x, d->y));
    const bool shownBeforeFinish = viewport.PreviewSegmentCount() > 0;
    viewport.FinishTool();
    const int previewAfter = viewport.PreviewSegmentCount();
    const int wiresAfter = CountOfKind(window, EntityKind::Wire);
    window.SelectTool(DrawingTool::Select);
    return Explain("締める前は途中経過が出ている", shownBeforeFinish)
        && Explain((std::string("折れ線を締められた(ワイヤー ")
                       + std::to_string(wiresBefore) + " → "
                       + std::to_string(wiresAfter) + ")").c_str(),
            wiresAfter == wiresBefore + 1)
        && Explain((std::string("締めた後は途中経過が消える(")
                       + std::to_string(previewAfter) + " 本)").c_str(),
            previewAfter == 0);
}

} // namespace

std::vector<SelfTestCase> SemanticStateCases()
{
    return {
        {"同じワイヤーでも選んだ線分だけが選択色になる",
            &CaseSelectedSegmentDoesNotLightTheWholeWire},
        {"Hoverと選択は別の意味状態で描く", &CaseHoverAndSelectionStayDifferent},
        {"テーマを変えても意味状態の区別が残る",
            &CaseSemanticStatesStayApartInEveryTheme},
        {"途中経過のペンは確定した線より細い破線か半透明",
            &CasePreviewPenIsFainterThanConfirmedLines},
        {"途中経過は拾えないし選べない", &CasePreviewIsNotSelectable},
        {"締められなかったときだけ途中経過を残す",
            &CaseFinishKeepsPreviewOnlyWhileStillDrawing},
    };
}

} // namespace kachakacha::v2::selftest
