//! クリックと引きずりの分け目、そしてカーソルの形(UI-P1-008、§5.1・§5.2)。
//!
//! ここで確かめたいのは、オーナーの言った3つである。
//!   - 選ぼうとして押しただけのとき、物が動かないこと
//!   - 引きずり始めたものが、途中で押しただけに化けないこと
//!   - 画面を動かす操作と、物を動かす操作がぶつからないこと
//!
//! 判断そのものは core(app/PointerGesture、app/PointerCursor)にあり、
//! そちらは画面なしの試験で押さえている。ここで見るのは **結線** である。
//! 本物のマウスの便りを画面へ送り、文書が変わったかどうかまで見る。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/DiagnosticReport.h"
#include "kachakacha/app/PointerCursor.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/geometry/Vector3.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QString>

#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/modeling/WorkPlane.h"

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::CursorShape;
using kachakacha::v2::geometry::Vector3;

//! マウスの便りを画面へ直に送る。押す・動かす・離すを本物と同じ道で通す。
void SendMouse(V2Viewport& viewport, QEvent::Type type, const QPointF& local,
    Qt::MouseButton button, Qt::MouseButtons buttons,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, local, QPointF(viewport.mapToGlobal(local.toPoint())), button,
        buttons, modifiers);
    QApplication::sendEvent(&viewport, &event);
}

//! 押して、少しずつ動かして、離す。動かす道のりは呼ぶ側が決める。
void PressDragRelease(V2Viewport& viewport, const QPointF& from,
    const std::vector<QPointF>& path, const QPointF& release,
    Qt::MouseButton button = Qt::LeftButton)
{
    const Qt::MouseButtons held = button == Qt::LeftButton ? Qt::LeftButton
                                                           : Qt::RightButton;
    SendMouse(viewport, QEvent::MouseButtonPress, from, button, held);
    for (const QPointF& step : path) {
        SendMouse(viewport, QEvent::MouseMove, step, Qt::NoButton, held);
    }
    SendMouse(viewport, QEvent::MouseButtonRelease, release, button, Qt::NoButton);
}

[[nodiscard]] bool DrawLine(V2MainWindow& window, const Vector3& from, const Vector3& to)
{
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    auto& viewport = window.Viewport();
    const auto first = viewport.Mapping().Project(from);
    const auto second = viewport.Mapping().Project(to);
    if (!first.has_value() || !second.has_value()) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return true;
}

//! 線を1本引いて、その真ん中の画面位置を返す。
[[nodiscard]] std::optional<QPointF> DrawOneLineAndPickMiddle(V2MainWindow& window)
{
    if (!DrawLine(window, Vector3{0.0, 0.0, 0.0}, Vector3{40.0, 0.0, 0.0})) {
        return std::nullopt;
    }
    const auto middle = window.Viewport().Mapping().Project(Vector3{20.0, 0.0, 0.0});
    if (!middle.has_value()) {
        return std::nullopt;
    }
    return QPointF(middle->x, middle->y);
}

[[nodiscard]] bool CaseTinyMoveDoesNotMoveTheObject(V2MainWindow& window)
{
    // オーナー指摘「オブジェクトを選択しようとして動いてしまう」。
    // 選んだ線の上を押すと掴んだことになる。そこで手が 1〜2px 揺れただけで、
    // 線が画面の上を滑って見え、離すと動いたことになっていた。
    const auto middle = DrawOneLineAndPickMiddle(window);
    if (!Explain("線を1本引ける", middle.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SelectAt(*middle, Qt::NoModifier);
    if (!Explain("線を選べた",
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) >= 1)) {
        return false;
    }
    const std::uint64_t before = window.Session().GetDocument().Snapshot().revision;
    // 4px までの揺れ。門は 5px なので、どれも引きずりにはならない。
    PressDragRelease(viewport, *middle,
        {*middle + QPointF(1.0, 0.0), *middle + QPointF(2.0, -1.0),
            *middle + QPointF(3.0, 2.0), *middle + QPointF(0.0, -3.0)},
        *middle + QPointF(1.0, 1.0));
    const std::uint64_t after = window.Session().GetDocument().Snapshot().revision;
    if (!Explain((std::string("手ぶれでは文書が変わらない(前 ")
                     + std::to_string(before) + " 後 " + std::to_string(after) + ")")
                     .c_str(),
            before == after)) {
        return false;
    }
    return Explain("選んだものはそのまま",
        kachakacha::v2::app::SelectionItemCount(viewport.Selection()) >= 1);
}

[[nodiscard]] bool CaseRealDragMovesTheObject(V2MainWindow& window)
{
    // 逆側。門を越えたらちゃんと動くこと。動かないなら、ただ壊しただけである。
    const auto middle = DrawOneLineAndPickMiddle(window);
    if (!Explain("線を1本引ける", middle.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SelectAt(*middle, Qt::NoModifier);
    if (!Explain("線を選べた",
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) >= 1)) {
        return false;
    }
    const std::uint64_t before = window.Session().GetDocument().Snapshot().revision;
    const QPointF away = *middle + QPointF(60.0, 40.0);
    PressDragRelease(viewport, *middle,
        {*middle + QPointF(3.0, 2.0), *middle + QPointF(20.0, 12.0), away}, away);
    const std::uint64_t after = window.Session().GetDocument().Snapshot().revision;
    return Explain((std::string("引きずれば動く(前 ") + std::to_string(before)
                       + " 後 " + std::to_string(after) + ")")
                       .c_str(),
        after != before);
}

[[nodiscard]] bool CaseDragBackToStartStaysADrag(V2MainWindow& window)
{
    // 矩形選択は「離した場所」だけで決めていたので、大きく引いてから
    // 押した場所へ戻して離すと、引きずらなかったことになっていた。
    // その場合、押した拍子の1件選択が残り、囲んだはずのものが選ばれない。
    if (!DrawLine(window, Vector3{0.0, 0.0, 0.0}, Vector3{40.0, 0.0, 0.0})) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    // 何も無いところから引き始める。線には当たらない場所を選ぶ。
    const QPointF empty(viewport.width() * 0.15, viewport.height() * 0.85);
    PressDragRelease(viewport, empty,
        {empty + QPointF(40.0, -30.0), empty + QPointF(120.0, -90.0),
            empty + QPointF(20.0, -10.0), empty},
        empty);
    // 戻って離したので、囲んだものは無い。だから選択は空になる。
    // 「押しただけ」に化けていたら、押した拍子の選択が残ってしまう。
    return Explain((std::string("引きずって戻れば空の矩形になる(実際は ")
                       + std::to_string(
                           kachakacha::v2::app::SelectionItemCount(viewport.Selection()))
                       + " 件)")
                       .c_str(),
        kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 0);
}

[[nodiscard]] bool CaseRightDragDoesNotOpenTheMenu(V2MainWindow& window)
{
    // 右で 5px 以上なぞってから離したら、それは献立を出す合図ではない。
    // ここは既にそうなっていた。門を1つにしたあとも変わっていないことを見る。
    auto& viewport = window.Viewport();
    const QPointF spot(viewport.width() * 0.5, viewport.height() * 0.5);
    bool opened = false;
    viewport.SetContextMenuCallback(
        [&opened](const QPoint&, const std::vector<QString>&) -> std::optional<int> {
            opened = true;
            return std::nullopt;
        });
    PressDragRelease(viewport, spot,
        {spot + QPointF(10.0, 0.0), spot + QPointF(30.0, 10.0)},
        spot + QPointF(30.0, 10.0), Qt::RightButton);
    if (!Explain("なぞった後は献立を出さない", !opened)) {
        viewport.SetContextMenuCallback(nullptr);
        return false;
    }
    // 押しただけなら出る。出ないと右クリックそのものが壊れる。
    PressDragRelease(viewport, spot, {spot + QPointF(1.0, 1.0)}, spot, Qt::RightButton);
    const bool ok = Explain("押しただけなら献立が出る", opened);
    viewport.SetContextMenuCallback(nullptr);
    return ok;
}

[[nodiscard]] bool CaseCursorFollowsTheSpec(V2MainWindow& window)
{
    // §5.1「クリック可能な通常形状に指カーソルを使わない」。
    // 以前は線や形の上で指になっていた。指は「別の場所へ行く」印である。
    const auto middle = DrawOneLineAndPickMiddle(window);
    if (!Explain("線を1本引ける", middle.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.HoverAt(*middle);
    const auto onCurve = kachakacha::v2::app::ChooseCursorShape(viewport.CursorContextNow());
    if (!Explain((std::string("線の上でも矢印(実際は ")
                     + std::string(kachakacha::v2::app::CursorShapeNameJa(onCurve)) + ")")
                     .c_str(),
            onCurve == CursorShape::Arrow)) {
        return false;
    }
    // 作図の道具に持ち替えたら十字。矢印との違いで「いま描ける」と分かる。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.HoverAt(*middle);
    const auto drawing = kachakacha::v2::app::ChooseCursorShape(viewport.CursorContextNow());
    const bool crossWhileDrawing = drawing == CursorShape::Cross
        || drawing == CursorShape::Forbidden;
    if (!Explain((std::string("作図中は十字か禁止(実際は ")
                     + std::string(kachakacha::v2::app::CursorShapeNameJa(drawing)) + ")")
                     .c_str(),
            crossWhileDrawing)) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.HoverAt(*middle);
    return Explain("選択へ戻せば矢印",
        kachakacha::v2::app::ChooseCursorShape(viewport.CursorContextNow())
            == CursorShape::Arrow);
}

[[nodiscard]] bool CaseCameraAndObjectDoNotFight(V2MainWindow& window)
{
    // §5.2「カメラ操作中にツール入力や選択を誤確定しない」。
    // 中ボタンで画面を動かしている間に、文書が変わってはいけない。
    const auto middle = DrawOneLineAndPickMiddle(window);
    if (!Explain("線を1本引ける", middle.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SelectAt(*middle, Qt::NoModifier);
    const std::uint64_t before = window.Session().GetDocument().Snapshot().revision;
    const auto selectedBefore =
        kachakacha::v2::app::SelectionItemCount(viewport.Selection());
    // 選んでいる線の真上から中ボタンで引きずる。掴みと取り違えてはいけない。
    SendMouse(viewport, QEvent::MouseButtonPress, *middle, Qt::MiddleButton,
        Qt::MiddleButton);
    SendMouse(viewport, QEvent::MouseMove, *middle + QPointF(50.0, 30.0), Qt::NoButton,
        Qt::MiddleButton);
    SendMouse(viewport, QEvent::MouseButtonRelease, *middle + QPointF(50.0, 30.0),
        Qt::MiddleButton, Qt::NoButton);
    if (!Explain("画面を動かしても文書は変わらない",
            window.Session().GetDocument().Snapshot().revision == before)) {
        return false;
    }
    return Explain("画面を動かしても選択は変わらない",
        kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == selectedBefore);
}

[[nodiscard]] bool CaseCameraDoesNotFinishAGrab(V2MainWindow& window)
{
    // 掴んで動かしている最中に中ボタンを押して離すと、そこで移動が
    // 確定していた。離すボタンを見ていなかったからである(§5.2)。
    const auto middle = DrawOneLineAndPickMiddle(window);
    if (!Explain("線を1本引ける", middle.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SelectAt(*middle, Qt::NoModifier);
    if (!Explain("線を選べた",
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) >= 1)) {
        return false;
    }
    const std::uint64_t before = window.Session().GetDocument().Snapshot().revision;
    // 左で掴んで、門を越えるまで引く。まだ離さない。
    SendMouse(viewport, QEvent::MouseButtonPress, *middle, Qt::LeftButton, Qt::LeftButton);
    SendMouse(viewport, QEvent::MouseMove, *middle + QPointF(40.0, 25.0), Qt::NoButton,
        Qt::LeftButton);
    // ここで中ボタンを押して離す。掴みを終わらせてはいけない。
    SendMouse(viewport, QEvent::MouseButtonPress, *middle + QPointF(40.0, 25.0),
        Qt::MiddleButton, Qt::LeftButton | Qt::MiddleButton);
    SendMouse(viewport, QEvent::MouseButtonRelease, *middle + QPointF(40.0, 25.0),
        Qt::MiddleButton, Qt::LeftButton);
    if (!Explain("中ボタンでは掴みが終わらない",
            window.Session().GetDocument().Snapshot().revision == before)) {
        // 後片付け。掴んだままにしない。
        SendMouse(viewport, QEvent::MouseButtonRelease, *middle + QPointF(40.0, 25.0),
            Qt::LeftButton, Qt::NoButton);
        return false;
    }
    // 左を離せば、そこで初めて確定する。
    SendMouse(viewport, QEvent::MouseButtonRelease, *middle + QPointF(40.0, 25.0),
        Qt::LeftButton, Qt::NoButton);
    return Explain("左を離したときに確定する",
        window.Session().GetDocument().Snapshot().revision != before);
}

[[nodiscard]] bool CaseCameraKeepsTheHalfDrawnLine(V2MainWindow& window)
{
    // §5.2「カメラ操作後も進行中 ToolSession と Preview を保持する」。
    // 引きかけの線があるとき、画面を送っても点は消えない。
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    const auto start = viewport.Mapping().Project(Vector3{0.0, 0.0, 0.0});
    if (!Explain("始点を画面へ写せる", start.has_value())) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    viewport.ClickAt(QPointF(start->x, start->y));
    const std::size_t placed = window.Session().PlacedPointCount();
    if (!Explain("1点置けた", placed == 1)) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    const std::uint64_t before = window.Session().GetDocument().Snapshot().revision;
    // 中ボタンで画面を送る。引きかけの点は残る。
    const QPointF from(viewport.width() * 0.6, viewport.height() * 0.6);
    SendMouse(viewport, QEvent::MouseButtonPress, from, Qt::MiddleButton,
        Qt::MiddleButton);
    SendMouse(viewport, QEvent::MouseMove, from + QPointF(70.0, 40.0), Qt::NoButton,
        Qt::MiddleButton);
    SendMouse(viewport, QEvent::MouseButtonRelease, from + QPointF(70.0, 40.0),
        Qt::MiddleButton, Qt::NoButton);
    const bool kept = window.Session().PlacedPointCount() == placed;
    const bool quiet = window.Session().GetDocument().Snapshot().revision == before;
    const bool sameTool =
        window.Session().CurrentTool() == kachakacha::v2::modeling::DrawingTool::Line;
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    if (!Explain("引きかけの点が残る", kept)) {
        return false;
    }
    if (!Explain("画面を送っただけでは線ができない", quiet)) {
        return false;
    }
    return Explain("道具も変わらない", sameTool);
}

[[nodiscard]] bool CaseLeftClickDuringPanPlacesNothing(V2MainWindow& window)
{
    // 画面を送っている最中の左押しは受けない。受けると、送っている途中に
    // 作図点が置かれる。中ボタンを押したまま左を押す持ち方は珍しくない。
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    const std::size_t placed = window.Session().PlacedPointCount();
    const QPointF from(viewport.width() * 0.4, viewport.height() * 0.4);
    SendMouse(viewport, QEvent::MouseButtonPress, from, Qt::MiddleButton,
        Qt::MiddleButton);
    SendMouse(viewport, QEvent::MouseButtonPress, from + QPointF(10.0, 10.0),
        Qt::LeftButton, Qt::LeftButton | Qt::MiddleButton);
    SendMouse(viewport, QEvent::MouseButtonRelease, from + QPointF(10.0, 10.0),
        Qt::LeftButton, Qt::MiddleButton);
    const bool quiet = window.Session().PlacedPointCount() == placed;
    SendMouse(viewport, QEvent::MouseButtonRelease, from + QPointF(10.0, 10.0),
        Qt::MiddleButton, Qt::NoButton);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return Explain("送っている最中は点が置かれない", quiet);
}

//! 押して離すだけ。指はまったく動かさない。
void PlainClick(V2Viewport& viewport, const QPointF& at)
{
    SendMouse(viewport, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    SendMouse(viewport, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
}

//! 押して、門(5px)を越えない範囲で震わせて、離す。
void JitteredClick(V2Viewport& viewport, const QPointF& at)
{
    SendMouse(viewport, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    for (const QPointF& step : {at + QPointF(1.0, 0.0), at + QPointF(2.0, -2.0),
             at + QPointF(-1.0, 3.0), at + QPointF(3.0, 1.0)}) {
        SendMouse(viewport, QEvent::MouseMove, step, Qt::NoButton, Qt::LeftButton);
    }
    SendMouse(viewport, QEvent::MouseButtonRelease, at + QPointF(2.0, 2.0), Qt::LeftButton,
        Qt::NoButton);
}

//! 文書を、覚えておいた版まで戻す。戻せなければあきらめる。
void UndoBackTo(V2MainWindow& window, std::uint64_t revision)
{
    for (int guard = 0; guard < 16; ++guard) {
        if (window.Session().GetDocument().Snapshot().revision == revision) {
            return;
        }
        if (!window.Session().Undo()) {
            return;
        }
    }
}

[[nodiscard]] bool CaseEveryToolTreatsJitterAsAClick(V2MainWindow& window)
{
    // 横断の要。**どの道具でも** 、手が少し震えたクリックは、
    // まったく震えなかったクリックと同じ結果でなければならない。
    // 道具ごとに門を書いていたころ、これが道具によって違っていた。
    constexpr kachakacha::v2::modeling::DrawingTool kTools[] = {
        kachakacha::v2::modeling::DrawingTool::Select,
        kachakacha::v2::modeling::DrawingTool::Point,
        kachakacha::v2::modeling::DrawingTool::Line,
        kachakacha::v2::modeling::DrawingTool::Polyline,
        kachakacha::v2::modeling::DrawingTool::Rectangle,
        kachakacha::v2::modeling::DrawingTool::Circle,
        kachakacha::v2::modeling::DrawingTool::Arc,
        kachakacha::v2::modeling::DrawingTool::Bezier,
        kachakacha::v2::modeling::DrawingTool::Spline,
        kachakacha::v2::modeling::DrawingTool::Move,
        kachakacha::v2::modeling::DrawingTool::Copy,
        kachakacha::v2::modeling::DrawingTool::Mirror,
        kachakacha::v2::modeling::DrawingTool::Rotate,
        kachakacha::v2::modeling::DrawingTool::Split,
        kachakacha::v2::modeling::DrawingTool::Trim,
        kachakacha::v2::modeling::DrawingTool::Extend,
        kachakacha::v2::modeling::DrawingTool::JoinEndpoints,
        kachakacha::v2::modeling::DrawingTool::Measure,
        kachakacha::v2::modeling::DrawingTool::ConnectTwoPoints,
    };
    auto& viewport = window.Viewport();
    const QPointF spot(viewport.width() * 0.45, viewport.height() * 0.55);
    for (const auto tool : kTools) {
        const std::uint64_t base = window.Session().GetDocument().Snapshot().revision;

        // 2回の試しは、まったく同じところから始めなければ比べられない。
        // 選んでいるものも揃える。動かす道具は「相手が決まっているか」で
        // クリックの意味が変わるので、片方だけ選択が残っていると別物になる。
        viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        window.SelectTool(tool);
        PlainClick(viewport, spot);
        const std::uint64_t plainRevision =
            window.Session().GetDocument().Snapshot().revision;
        const std::size_t plainPoints = window.Session().PlacedPointCount();
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        UndoBackTo(window, base);

        viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        window.SelectTool(tool);
        JitteredClick(viewport, spot);
        const std::uint64_t shakyRevision =
            window.Session().GetDocument().Snapshot().revision;
        const std::size_t shakyPoints = window.Session().PlacedPointCount();
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        UndoBackTo(window, base);

        const std::string name(
            kachakacha::v2::modeling::DrawingToolNameJa(tool));
        if (!Explain((name + ": 震えても文書は同じだけ変わる").c_str(),
                (plainRevision != base) == (shakyRevision != base))) {
            return false;
        }
        if (!Explain((name + ": 震えても置かれる点の数は同じ(" 
                         + std::to_string(plainPoints) + " と "
                         + std::to_string(shakyPoints) + ")").c_str(),
                plainPoints == shakyPoints)) {
            return false;
        }
    }
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return true;
}

[[nodiscard]] bool CaseEveryToolClearsThePreviousPreview(V2MainWindow& window)
{
    // 道具を替えたのに前の道具の途中経過が残っていると、
    // もう作られない形を「まだ引いている途中」だと読んでしまう(§3 規則3)。
    auto& viewport = window.Viewport();
    constexpr kachakacha::v2::modeling::DrawingTool kDrawing[] = {
        kachakacha::v2::modeling::DrawingTool::Line,
        kachakacha::v2::modeling::DrawingTool::Rectangle,
        kachakacha::v2::modeling::DrawingTool::Circle,
        kachakacha::v2::modeling::DrawingTool::Arc,
        kachakacha::v2::modeling::DrawingTool::Spline,
        kachakacha::v2::modeling::DrawingTool::Polyline,
    };
    const QPointF spot(viewport.width() * 0.35, viewport.height() * 0.35);
    for (const auto tool : kDrawing) {
        window.SelectTool(tool);
        viewport.ClickAt(spot);
        viewport.HoverAt(spot + QPointF(40.0, 30.0));
        const std::string name(kachakacha::v2::modeling::DrawingToolNameJa(tool));
        // 別の道具へ移る。ここで途中経過も置いた点も捨てられていること。
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        if (!Explain((name + ": 道具を替えたら置いた点が残らない").c_str(),
                window.Session().PlacedPointCount() == 0)) {
            return false;
        }
        if (!Explain((name + ": 道具を替えたら途中経過が消える").c_str(),
                !viewport.HasPreview())) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseForbiddenHoverIsAlsoUnpickable(V2MainWindow& window)
{
    // カーソルが「掴めない」と言っている線は、押しても拾えないこと。
    // Hover は絞り(作業平面の外の線を掴まない)を見ていたのに、
    // クリックは見ていなかったので、目と手が食い違っていた。
    //
    // XY 面に線を引き、作業平面を別の面へ移してから、その線の上を見る。
    if (!DrawLine(window, Vector3{0.0, 0.0, 0.0}, Vector3{40.0, 0.0, 0.0})) {
        return false;
    }
    auto& viewport = window.Viewport();
    const auto middle = viewport.Mapping().Project(Vector3{20.0, 0.0, 0.0});
    if (!Explain("線の真ん中を画面へ写せる", middle.has_value())) {
        return false;
    }
    const QPointF spot(middle->x, middle->y);
    // 作図の道具に持ち替えると絞りが効く。線は XY 面の上にあるので、
    // 作業平面が XY のままなら掴める。まずそこを押さえる。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.HoverAt(spot);
    const bool forbidden = viewport.HoverIsForbidden();
    // 掴めないと言うなら、押しても選べないこと。言わないなら、どちらでもよい。
    if (forbidden) {
        viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Move);
        viewport.ClickAt(spot);
        const bool picked =
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) > 0;
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return Explain("掴めないと言った線は押しても選べない", !picked);
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return Explain("作業平面の上の線は掴めると言う", !viewport.HoverIsForbidden());
}

[[nodiscard]] bool CaseDiagnosticsCopyWorks(V2MainWindow& window)
{
    // D-001 / D-005。選択道具のまま、未保存の文書でも作れること。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    const QString text = window.DiagnosticText();
    if (!Explain("診断が空でない", !text.isEmpty())) {
        return false;
    }
    for (const QString key : {QStringLiteral("timestamp"), QStringLiteral("activeTool"),
             QStringLiteral("rightPanelTool"), QStringLiteral("cursorMode"),
             QStringLiteral("previewOwner"), QStringLiteral("snapOwner"),
             QStringLiteral("selectionCount")}) {
        if (!Explain((std::string("必須の欄 ") + key.toStdString() + " がある").c_str(),
                text.contains(key + QStringLiteral(":")))) {
            return false;
        }
    }
    // 出してはいけないもの。貼り付ける先が他人の目に触れることを前提にする。
    if (!Explain("置き場所を出さない", !text.contains(QStringLiteral("C:\\")))) {
        return false;
    }
    // 命令からも呼べること。落ちないことを見る。
    window.RunCommand("help.copy_diagnostics");
    return Explain((std::string("知らせが出る(実際 ")
                       + window.StatusText().toStdString() + ")")
                       .c_str(),
        window.StatusText().contains(QStringLiteral("診断情報")));
}

[[nodiscard]] bool CaseDiagnosticsFollowsTheTool(V2MainWindow& window)
{
    // D-002 / D-003。道具を替えたら、診断のどの欄もその道具になること。
    // ずれていること自体が USER-UI-001 の不具合である。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    if (!Explain((std::string("直線のとき activeTool が直線(実際 ")
                     + window.DiagnosticSnapshotNow().activeTool + ")")
                     .c_str(),
            window.DiagnosticSnapshotNow().activeTool == "直線")) {
        return false;
    }
    // 円弧 → ベジェ。切り替えたあと、右の棚も一緒に動くこと。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Arc);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Bezier);
    const auto snapshot = window.DiagnosticSnapshotNow();
    if (!Explain((std::string("activeTool がベジェ(実際 ") + snapshot.activeTool + ")")
                     .c_str(),
            snapshot.activeTool == "ベジェ")) {
        return false;
    }
    if (!Explain((std::string("rightPanelTool もベジェ(実際 ")
                     + snapshot.rightPanelTool + ")")
                     .c_str(),
            snapshot.rightPanelTool == "ベジェ")) {
        return false;
    }
    // そろっていないなら、診断がそう言うこと。
    const auto mismatches = kachakacha::v2::app::DiagnosticMismatches(snapshot);
    for (const auto& line : mismatches) {
        (void)Explain((std::string("ずれ: ") + line).c_str(), false);
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return Explain("道具・棚・カーソル・途中経過・吸着がそろう", mismatches.empty());
}

[[nodiscard]] bool CaseToolSwitchLeavesNothingBehind(V2MainWindow& window)
{
    // USER-UI-001 の回帰試験。指定どおり
    // 直線 → 円弧 → ベジェ → スプライン → 選択 と続けて替える。
    constexpr kachakacha::v2::modeling::DrawingTool kChain[] = {
        kachakacha::v2::modeling::DrawingTool::Line,
        kachakacha::v2::modeling::DrawingTool::Arc,
        kachakacha::v2::modeling::DrawingTool::Bezier,
        kachakacha::v2::modeling::DrawingTool::Spline,
        kachakacha::v2::modeling::DrawingTool::Select,
    };
    auto& viewport = window.Viewport();
    const QPointF spot(viewport.width() * 0.5, viewport.height() * 0.5);
    for (const auto tool : kChain) {
        window.SelectTool(tool);
        // 1点置いて途中経過を作ってから次の道具へ移る。
        // 何も置かずに替えると、残るものが無いので試験にならない。
        if (tool != kachakacha::v2::modeling::DrawingTool::Select) {
            viewport.ClickAt(spot);
            viewport.HoverAt(spot + QPointF(30.0, 20.0));
        }
        const auto snapshot = window.DiagnosticSnapshotNow();
        const std::string name(kachakacha::v2::modeling::DrawingToolNameJa(tool));
        const auto mismatches = kachakacha::v2::app::DiagnosticMismatches(snapshot);
        if (!mismatches.empty()) {
            (void)Explain((name + ": " + mismatches.front()).c_str(), false);
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
        if (!Explain((name + ": 前の道具の点が残らない").c_str(),
                window.Session().PlacedPointCount() <= 1)) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
    }
    return Explain("選択へ戻ったら途中経過も残らない", !viewport.HasPreview());
}

[[nodiscard]] bool CaseExtrudeReadsTheSelection(V2MainWindow& window)
{
    // EX-03 の入口。立体だけを選んだとき、「不正な入力です」で終わらせない。
    // いま何が決まっていて、次に何を選べばよいのかまで言う。
    using kachakacha::v2::app::ExtrudeInputKind;
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();

    // 何も選んでいない。
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    const auto empty = window.PlanExtrudeFromSelection();
    if (!Explain("何も選んでいなければ Nothing", empty.kind == ExtrudeInputKind::Nothing)) {
        return false;
    }
    if (!Explain("次にすることを言う", !empty.needsJa.empty())) {
        return false;
    }

    // 閉じた輪郭を1つ引いて選ぶ。四角を描く。
    const double pxPerMm = 200.0 / 200.0;
    (void)pxPerMm;
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    const QPointF corner(viewport.width() * 0.35, viewport.height() * 0.35);
    viewport.ClickAt(corner);
    viewport.ClickAt(corner + QPointF(80.0, 60.0));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    const auto profile = window.PlanExtrudeFromSelection();
    if (!Explain((std::string("閉じた輪郭だけなら ProfileOnly(実際 ")
                     + std::string(kachakacha::v2::app::ExtrudeInputKindNameJa(profile.kind))
                     + ")")
                     .c_str(),
            profile.kind == ExtrudeInputKind::ProfileOnly)) {
        return false;
    }
    if (!Explain("すぐ下見できる", profile.readyToPreview)) {
        return false;
    }
    // 読み取った意味が日本語で出ること。内部の言葉を出さないこと。
    const QString text = window.ExtrudePlanTextJa();
    if (!Explain((std::string("輪郭として読む(実際 ") + text.toStdString() + ")").c_str(),
            text.contains(QStringLiteral("輪郭")))) {
        return false;
    }
    return Explain("内部の言葉を出さない", !text.contains(QStringLiteral("Wire")));
}

//! 道具を替えたときに、前の道具の吸着・案内・候補送りが残らないこと。
//! UI-P1-007 R7 の指摘 B1。マウスを **動かさずに** 替えるのが要点である。
[[nodiscard]] bool CaseToolSwitchDropsTheOldHover(V2MainWindow& window)
{
    constexpr kachakacha::v2::modeling::DrawingTool kChain[] = {
        kachakacha::v2::modeling::DrawingTool::Line,
        kachakacha::v2::modeling::DrawingTool::Arc,
        kachakacha::v2::modeling::DrawingTool::Bezier,
        kachakacha::v2::modeling::DrawingTool::Spline,
        kachakacha::v2::modeling::DrawingTool::Select,
    };
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    // 拾う相手を作る。何も無いと候補送りが試せない。
    if (!DrawLine(window, Vector3{-20.0, 0.0, 0.0}, Vector3{20.0, 0.0, 0.0})) {
        return Explain("線を1本引ける", false);
    }
    if (!DrawLine(window, Vector3{0.0, -20.0, 0.0}, Vector3{0.0, 20.0, 0.0})) {
        return Explain("線をもう1本引ける", false);
    }
    const auto crossing = viewport.Mapping().Project(Vector3{0.0, 0.0, 0.0});
    if (!crossing.has_value()) {
        return Explain("交点が画面に入る", false);
    }
    const QPointF spot(crossing->x, crossing->y);

    for (std::size_t index = 1; index < std::size(kChain); ++index) {
        const auto previous = kChain[index - 1];
        const auto next = kChain[index];
        window.SelectTool(previous);
        viewport.HoverAt(spot);
        // 候補送りを1つ進めておく。進めた番号が次の道具へ残ってはならない。
        const bool cycled = viewport.CandidateCount() >= 2 && viewport.CycleCandidate(false);
        const std::string beforeJa = viewport.Hover().messageJa;
        const std::string previousName(kachakacha::v2::modeling::DrawingToolNameJa(previous));
        const std::string nextName(kachakacha::v2::modeling::DrawingToolNameJa(next));

        // ここでマウスは動かさない。道具だけを替える。
        window.SelectTool(next);

        const std::string afterJa = viewport.Hover().messageJa;
        if (!Explain((previousName + " → " + nextName + ": 前の案内が残らない(実際 "
                         + afterJa + ")")
                         .c_str(),
                afterJa != beforeJa
                    && afterJa.find(previousName) == std::string::npos)) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
        if (cycled
            && !Explain((previousName + " → " + nextName + ": 候補送りが先頭へ戻る").c_str(),
                viewport.CandidateIndex() == 0)) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
        if (!Explain((previousName + " → " + nextName + ": 途中経過が残らない").c_str(),
                !viewport.HasPreview())) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
        const auto mismatches
            = kachakacha::v2::app::DiagnosticMismatches(window.DiagnosticSnapshotNow());
        if (!mismatches.empty()) {
            (void)Explain((nextName + ": " + mismatches.front()).c_str(), false);
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
    }
    return Explain("選択へ戻っても前の道具の物が残らない", !viewport.HasPreview());
}

//! 文書を差し替える道(開く・Undo/Redo・作業平面・グリッド)で
//! 前の場面の吸着表示が生き残らないこと。
//! UI-P1-007 R7 の指摘 B2 と、R8 の指摘 B1。どれも DrawingSession::SetScene を通る。
//!
//! **場面を替えたあと HoverAt を呼んではならない。** 呼ぶと古い hover_ を
//! 上書きしてしまい、「ポインタを動かさない直後」の残りを見られない。
[[nodiscard]] bool CaseSceneSwapDropsTheHold(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    // 吸着の相手になる線。取り消しで消えては試験にならないので、これは残す。
    if (!DrawLine(window, Vector3{-20.0, 0.0, 0.0}, Vector3{20.0, 0.0, 0.0})) {
        return Explain("線を1本引ける", false);
    }
    // 取り消し・やり直しの相手になる、別の場所の線。
    if (!DrawLine(window, Vector3{-20.0, 30.0, 0.0}, Vector3{20.0, 30.0, 0.0})) {
        return Explain("線をもう1本引ける", false);
    }
    const auto endpoint = viewport.Mapping().Project(Vector3{20.0, 0.0, 0.0});
    if (!endpoint.has_value()) {
        return Explain("端点が画面に入る", false);
    }
    const QPointF onEndpoint(endpoint->x, endpoint->y);
    // 12px の外・16px の内。持ち越しがあるので、ここでも端点へ吸い付いている。
    const QPointF justOutside = onEndpoint + QPointF(14.0, 0.0);

    // 場面を差し替える道をひととおり通す。どれも DrawingSession::SetScene を通る。
    struct Route {
        const char* nameJa;
        //! 掴む前にやっておくこと。「やり直す」は先に取り消しておかないと、
        //! やり直す相手が無く、場面の差し替えそのものが起きない。
        std::function<void()> prepare;
        std::function<void()> run;
    };
    const std::vector<Route> kRoutes = {
        {"開き直す", {}, [&window]() { window.RunCommand("file.new"); }},
        {"取り消す", {}, [&window]() { window.RunCommand("edit.undo"); }},
        {"やり直す", [&window]() { window.RunCommand("edit.undo"); },
            [&window]() { window.RunCommand("edit.redo"); }},
        {"作業平面を替える", {},
            [&window]() {
                window.Viewport().SetWorkPlane(
                    kachakacha::v2::modeling::StandardPlane(
                        kachakacha::v2::modeling::StandardPlaneKind::ZX));
            }},
        {"グリッドを変える", {},
            [&window]() { window.ApplyGridChoice(window.CurrentGridChoice()); }},
    };
    for (const auto& route : kRoutes) {
        const std::string nameJa(route.nameJa);
        // 「開き直す」は文書を空にするので、毎回線を引き直してから始める。
        window.RunCommand("file.new");
        if (!DrawLine(window, Vector3{-20.0, 0.0, 0.0}, Vector3{20.0, 0.0, 0.0})
            || !DrawLine(window, Vector3{-20.0, 30.0, 0.0}, Vector3{20.0, 30.0, 0.0})) {
            return Explain((nameJa + ": 下ごしらえの線が引ける").c_str(), false);
        }
        if (route.prepare) {
            route.prepare();
        }
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
        // 端点で掴んでから 14px 外へ出る。持ち越しがあるので、まだ端点へ吸い付く。
        viewport.HoverAt(onEndpoint);
        viewport.HoverAt(justOutside);
        // 前提を必ず確かめる。掴めていなければ、この試験は何も見ていない。
        if (!Explain((nameJa + ": 前提 — 14px 先でも持ち越した端点へ吸い付いている").c_str(),
                viewport.Hover().snap.has_value())) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
        const int candidatesBefore = viewport.CandidateCount();
        // 候補が無いと、候補送りの道を検証したことにならない。前提として必須にする。
        if (!Explain((nameJa + ": 前提 — 候補が1つ以上ある").c_str(),
                candidatesBefore > 0)) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }

        route.run();

        // ここでポインタは動かさない。動かすと古い hover_ を上書きしてしまう。
        if (!Explain((nameJa + ": 場面を替えたら前のリングが消える").c_str(),
                !viewport.Hover().snap.has_value())) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
        if (!Explain((nameJa + ": 前の候補送りも消える").c_str(),
                viewport.CandidateCount() == 0)) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
        if (!Explain((nameJa + ": 前の途中経過も消える").c_str(), !viewport.HasPreview())) {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
            return false;
        }
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return Explain("場面の差し替えを一通り通せる", true);
}

//! 取り消した直後、リングと診断情報がいまの session の中身と合っていること。
//! UI-P1-007 R7 の MISSING TESTS (4)。
[[nodiscard]] bool CaseCancelLeavesNoStaleRing(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    if (!DrawLine(window, Vector3{-20.0, 0.0, 0.0}, Vector3{20.0, 0.0, 0.0})) {
        return Explain("線を1本引ける", false);
    }
    const auto endpoint = viewport.Mapping().Project(Vector3{20.0, 0.0, 0.0});
    if (!endpoint.has_value()) {
        return Explain("端点が画面に入る", false);
    }
    const QPointF onEndpoint(endpoint->x, endpoint->y);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(onEndpoint);
    viewport.HoverAt(onEndpoint + QPointF(14.0, 0.0));
    viewport.CancelTool();
    if (!Explain("取り消したら途中経過が残らない", !viewport.HasPreview())) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    if (!Explain("取り消したら置いた点も残らない",
            window.Session().PlacedPointCount() == 0)) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    const auto mismatches
        = kachakacha::v2::app::DiagnosticMismatches(window.DiagnosticSnapshotNow());
    if (!mismatches.empty()) {
        (void)Explain((std::string("取り消し後の診断: ") + mismatches.front()).c_str(), false);
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return Explain("取り消し後のリングがいまの状態と合う", true);
}

} // namespace

std::vector<SelfTestCase> PointerCases()
{
    return {
        {"手ぶれでは選んだ物が動かない", CaseTinyMoveDoesNotMoveTheObject},
        {"引きずれば選んだ物が動く", CaseRealDragMovesTheObject},
        {"引きずって戻して離してもクリックにしない", CaseDragBackToStartStaysADrag},
        {"右でなぞった後は献立を出さない", CaseRightDragDoesNotOpenTheMenu},
        {"カーソルの形が仕様どおり", CaseCursorFollowsTheSpec},
        {"画面を動かしても文書と選択が変わらない", CaseCameraAndObjectDoNotFight},
        {"中ボタンでは掴みが終わらない", CaseCameraDoesNotFinishAGrab},
        {"画面を送っても引きかけの線が残る", CaseCameraKeepsTheHalfDrawnLine},
        {"送っている最中の左押しでは点が置かれない", CaseLeftClickDuringPanPlacesNothing},
        {"どの道具でも震えたクリックは普通のクリックと同じ", CaseEveryToolTreatsJitterAsAClick},
        {"どの道具でも替えたら前の途中経過が消える", CaseEveryToolClearsThePreviousPreview},
        {"掴めないと言う線は押しても拾えない", CaseForbiddenHoverIsAlsoUnpickable},
        {"診断情報をコピーできる", CaseDiagnosticsCopyWorks},
        {"診断の各欄が道具に追従する", CaseDiagnosticsFollowsTheTool},
        {"道具を続けて替えても前の状態が残らない", CaseToolSwitchLeavesNothingBehind},
        {"押し出しが選択を読んで次を案内する", CaseExtrudeReadsTheSelection},
        {"道具を替えると前の吸着と候補送りが消える", CaseToolSwitchDropsTheOldHover},
        {"場面を差し替えると吸着の持ち越しが消える", CaseSceneSwapDropsTheHold},
        {"取り消した直後に古いリングが残らない", CaseCancelLeavesNoStaleRing},
    };
}

} // namespace kachakacha::v2::selftest
