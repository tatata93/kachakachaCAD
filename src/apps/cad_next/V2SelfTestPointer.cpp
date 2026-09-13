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
#include <optional>
#include <string>
#include <vector>

#include "kachakacha/modeling/ToolController.h"

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

        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        window.SelectTool(tool);
        PlainClick(viewport, spot);
        const std::uint64_t plainRevision =
            window.Session().GetDocument().Snapshot().revision;
        const std::size_t plainPoints = window.Session().PlacedPointCount();
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        UndoBackTo(window, base);

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
    };
}

} // namespace kachakacha::v2::selftest
