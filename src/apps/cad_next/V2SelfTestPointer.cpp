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
    };
}

} // namespace kachakacha::v2::selftest
