//! V1同等の操作系のケース(docs/v2/v1-input-parity.md)。
//!
//! 中ボタンでの画面移動、Shift+中ボタンの軌道回転、Esc の段取り、
//! 作図中の Shift と S、主要キー配置、そして移動・複製・鏡映・回転。
//! どれも「V1では出来たのに V2で出来なかった」ものなので、
//! ここへまとめておくと、また落ちたときに何が戻ったのかがすぐ分かる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/EntityNaming.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/view/ViewOrientation.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include <QEvent>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPointF>
#include <QString>

namespace kachakacha::v2::selftest {
namespace {



[[nodiscard]] bool CaseMiddleDragPansTheView(V2MainWindow& window)
{
    // V1では中ボタンでも右ボタンでも画面を動かせた。V2には手立てが無かった。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    const auto before = viewport.ViewCenter();
    viewport.PanByPixels(40.0, 0.0);
    const auto moved = viewport.ViewCenter();
    if (!Explain("横に動く", (moved - before).Length() > 1.0e-9)) {
        return false;
    }
    // 同じだけ戻せば元へ戻る。掴んだ点が指から離れない、ということである。
    viewport.PanByPixels(-40.0, 0.0);
    if (!Explain("戻すと元へ戻る",
            (viewport.ViewCenter() - before).Length() < 1.0e-9)) {
        return false;
    }
    // 動かした量は倍率に比例する。拡大しているほど、同じpxで動く距離は小さい。
    viewport.SetVisibleWidthMm(200.0);
    const auto wide = viewport.ViewCenter();
    viewport.PanByPixels(40.0, 0.0);
    const double wideMove = (viewport.ViewCenter() - wide).Length();
    viewport.PanByPixels(-40.0, 0.0);
    viewport.SetVisibleWidthMm(100.0);
    const auto close = viewport.ViewCenter();
    viewport.PanByPixels(40.0, 0.0);
    const double closeMove = (viewport.ViewCenter() - close).Length();
    return Explain((std::string("倍率に比例する(") + std::to_string(wideMove) + " / "
                       + std::to_string(closeMove) + ")").c_str(),
        std::abs(wideMove - closeMove * 2.0) < 1.0e-6);
}

[[nodiscard]] bool CaseOrbitTurnsTheView(V2MainWindow& window)
{
    // Shift+中ボタンの軌道回転。形は変わらない。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Front);
    const auto before = viewport.Orientation();
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    viewport.OrbitByPixels(30.0, 0.0);
    const double turned = kachakacha::v2::view::AngleBetween(before,
        viewport.Orientation()) * 180.0 / 3.14159265358979323846;
    if (!Explain((std::string("15度まわる(実際は ") + std::to_string(turned)
                     + ")").c_str(), std::abs(turned - 15.0) < 1.0e-6)) {
        return false;
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == revision);
}

[[nodiscard]] bool CaseEscapeGoesBackToSelect(V2MainWindow& window)
{
    // V1と同じ。やりかけを1つ取り消してから、選択道具へ戻り、選択も解除する。
    auto& viewport = window.Viewport();
    // 線を1本引いて、選んでおく。
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    // 作図の途中を作る。1点だけ置く。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.4, viewport.height() * 0.4));
    if (!Explain("作図の途中になっている", window.Session().HasPlacedPoints())) {
        return false;
    }
    // 1回目のEsc: 作図を捨てて、選択も解除して、選択道具へ戻る。
    const auto steps = viewport.PressEscape();
    if (!Explain("作図が消える", !window.Session().HasPlacedPoints())) {
        return false;
    }
    if (!Explain("選択が解除される", viewport.Selection().entityIds.empty())) {
        return false;
    }
    if (!Explain("選択道具へ戻る",
            window.Session().CurrentTool()
                == kachakacha::v2::modeling::DrawingTool::Select)) {
        return false;
    }
    if (!Explain("何をしたかを言う", !steps.empty() && !window.StatusText().isEmpty())) {
        return false;
    }
    // 2回目のEsc: もうすることが無い。何も起きないし、何も言わない。
    return Explain("2回目は何も起きない", viewport.PressEscape().empty());
}

[[nodiscard]] bool CaseShiftConstrainsAndSSuppressesSnap(V2MainWindow& window)
{
    // Shift で水平・垂直へ寄る。S で吸着が止まる。どちらも押している間だけ。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.3, viewport.height() * 0.5));
    // Shift 無しなら、斜めに引ける。
    viewport.SetAxisConstraintByKey(false);
    viewport.HoverAt(QPointF(viewport.width() * 0.7, viewport.height() * 0.3));
    const auto slanted = viewport.HoverPosition();
    // Shift を押すと、同じ場所でも水平に寄る。
    viewport.SetAxisConstraintByKey(true);
    viewport.HoverAt(QPointF(viewport.width() * 0.7, viewport.height() * 0.3));
    const auto flat = viewport.HoverPosition();
    viewport.SetAxisConstraintByKey(false);
    if (!Explain("どちらも位置が取れる",
            slanted.has_value() && flat.has_value())) {
        return false;
    }
    if (!Explain("Shift で寄る", (*slanted - *flat).Length() > 1.0e-6)) {
        return false;
    }
    // 1点目と同じ高さになっている(作業平面の上での水平)。
    const double anchorHeight = window.Session().ConstraintAnchor().y;
    if (!Explain((std::string("水平になる(") + std::to_string(flat->y) + " と "
                     + std::to_string(anchorHeight) + ")").c_str(),
            std::abs(flat->y - anchorHeight) < 1.0e-6)) {
        return false;
    }
    viewport.CancelTool();
    // S は帯の言い方で確かめる。吸着していないときは、そう出る。
    viewport.SetSnapSuppressedByKey(true);
    viewport.HoverAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5));
    const bool saidNoSnap = window.StatusText().contains(QStringLiteral("スナップなし"));
    viewport.SetSnapSuppressedByKey(false);
    return Explain((std::string("S中は吸着しないと言う(")
                       + window.StatusText().toStdString() + ")").c_str(), saidNoSnap);
}

[[nodiscard]] bool CaseSnapRadiusHoldAndSKeyThroughViewport(V2MainWindow& window)
{
    // ui-ux-integrated-spec.md §6.1・§6.2 を、画面と同じ道(HoverAt とキーの知らせ)で確かめる。
    // 吸着半径 12px は拡大しても画面の上で同じ。境目で手が震えても吸着先を離さない。
    // S は押している間だけ止め、離すか焦点が外れれば戻す。
    using kachakacha::v2::geometry::Vector3;
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    const std::size_t curvesBefore = window.Session().Scene().curves.size();
    viewport.ClickAt(QPointF(viewport.width() * 0.3, viewport.height() * 0.5));
    viewport.ClickAt(QPointF(viewport.width() * 0.7, viewport.height() * 0.5));
    if (!Explain("線を1本引ける",
            window.Session().Scene().curves.size() == curvesBefore + 1)) {
        return false;
    }
    const Vector3 start = window.Session().Scene().curves.back().segment.StartPoint();
    const Vector3 end = window.Session().Scene().curves.back().segment.EndPoint();

    // 終点から、画面の上で線と直角に offsetPx 離れた点。線の上の最近点も終点になる。
    const auto besideEnd = [&viewport, start, end](double offsetPx) -> std::optional<QPointF> {
        const auto startOnScreen = viewport.Mapping().Project(start);
        const auto endOnScreen = viewport.Mapping().Project(end);
        if (!startOnScreen.has_value() || !endOnScreen.has_value()) {
            return std::nullopt;
        }
        const double dx = endOnScreen->x - startOnScreen->x;
        const double dy = endOnScreen->y - startOnScreen->y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0)) {
            return std::nullopt;
        }
        return QPointF(endOnScreen->x - dy / length * offsetPx,
            endOnScreen->y + dx / length * offsetPx);
    };
    const auto snapsToEnd = [&viewport, &besideEnd, end](double offsetPx) {
        const auto at = besideEnd(offsetPx);
        if (!at.has_value()) {
            return false;
        }
        viewport.HoverAt(*at);
        const auto position = viewport.HoverPosition();
        return position.has_value() && (*position - end).Length() < 1.0e-6;
    };

    for (const double widthMm : {200.0, 20.0}) {
        viewport.SetVisibleWidthMm(widthMm);
        viewport.SetViewCenter(end);
        const std::string zoom = "(画面の幅 " + std::to_string(widthMm) + "mm)";
        if (!Explain(("線が画面に写る" + zoom).c_str(), besideEnd(0.0).has_value())) {
            return false;
        }
        // 持ち越しを捨て、何も持っていない状態から見る。
        viewport.CancelTool();
        if (!Explain(("12px の外では吸わない" + zoom).c_str(), !snapsToEnd(14.0))) {
            return false;
        }
        if (!Explain(("12px の内で吸う" + zoom).c_str(), snapsToEnd(10.0))) {
            return false;
        }
        if (!Explain(("境目の外へ少し揺れても離さない" + zoom).c_str(), snapsToEnd(14.0))) {
            return false;
        }
        if (!Explain(("揺れの幅を越えて離れれば手放す" + zoom).c_str(), !snapsToEnd(17.0))) {
            return false;
        }
    }

    // S はキーの知らせで確かめる。SetSnapSuppressedByKey を直に呼ぶと、キーの道が壊れても気づかない。
    const auto sendS = [&viewport](QEvent::Type type, bool autoRepeat) {
        QKeyEvent event(type, Qt::Key_S, Qt::NoModifier, QStringLiteral("s"), autoRepeat);
        QCoreApplication::sendEvent(&viewport, &event);
    };
    // ポインタを動かさずに、いま出ている吸着(リング・プレビューの元)が終点かを見る。
    const auto hoverIsEnd = [&viewport, end]() {
        const auto position = viewport.HoverPosition();
        return position.has_value() && (*position - end).Length() < 1.0e-6;
    };
    viewport.CancelTool();
    if (!Explain("S を押す前は吸う", snapsToEnd(10.0))) {
        return false;
    }
    sendS(QEvent::KeyPress, false);
    if (!Explain("S を押しただけで、ポインタを動かさなくても吸着が消える", !hoverIsEnd())) {
        return false;
    }
    if (!Explain("S を押している間は吸わない", !snapsToEnd(10.0))) {
        return false;
    }
    sendS(QEvent::KeyRelease, true);
    if (!Explain("押しっぱなしの自動反復では戻らない", !hoverIsEnd() && !snapsToEnd(10.0))) {
        return false;
    }
    sendS(QEvent::KeyRelease, false);
    if (!Explain("S を離しただけで、ポインタを動かさなくても吸着が戻る", hoverIsEnd())) {
        return false;
    }
    sendS(QEvent::KeyPress, false);
    if (!Explain("もう一度 S を押すと止まる", !snapsToEnd(10.0))) {
        return false;
    }
    sendS(QEvent::KeyRelease, false);

    // 持ち越しは次の Hover を待たずに捨てる。境目の外(14px)で持ち越してから、
    // ポインタを動かさずに抑止を入れて切る。古い吸着先へ戻ってはならない。
    const auto holdOutsideRadius = [&](const char* how) {
        viewport.CancelTool();
        return Explain((std::string("境目の外で持ち越している(") + how + "の前提)").c_str(),
            snapsToEnd(10.0) && snapsToEnd(14.0));
    };
    if (!holdOutsideRadius("S を押して離す")) {
        return false;
    }
    sendS(QEvent::KeyPress, false);
    sendS(QEvent::KeyRelease, false);
    if (!Explain("Hover なしで S を押して離しても、境目の外の古い吸着先へ戻らない",
            !hoverIsEnd() && !snapsToEnd(14.0))) {
        return false;
    }
    if (!holdOutsideRadius("磁石")) {
        return false;
    }
    viewport.SetSnapSuppressed(true);
    if (!Explain("磁石を切ると、ポインタを動かさなくても吸着が消える", !hoverIsEnd())) {
        return false;
    }
    viewport.SetSnapSuppressed(false);
    if (!Explain("磁石を戻しても、境目の外の古い吸着先へ戻らない",
            !hoverIsEnd() && !snapsToEnd(14.0))) {
        return false;
    }
    if (!holdOutsideRadius("焦点が外れる")) {
        return false;
    }
    sendS(QEvent::KeyPress, false);
    QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QCoreApplication::sendEvent(&viewport, &focusOut);
    if (!Explain("S を押したまま焦点が外れても、境目の外の古い吸着先へ戻らない",
            !hoverIsEnd() && !snapsToEnd(14.0))) {
        return false;
    }
    return Explain("S を押したまま焦点が外れたら、吸着は戻る", snapsToEnd(10.0));
}

[[nodiscard]] bool CasePrimaryShortcutsDoNotConflict(V2MainWindow& window)
{
    // 主要キーが同じ道具を出し、操作中の S と競合しないこと。
    const struct {
        const char* key;
        const char* id;
    } expected[] = {
        {"V", "selection.activate"}, {"D", "draw.point"}, {"L", "draw.line"},
        {"P", "draw.polyline"}, {"R", "draw.rectangle"}, {"C", "draw.circle"},
        {"A", "draw.arc"}, {"B", "draw.bezier"},
        {"I", "wire.coincident"}, {"T", "wire.tangent"}, {"Shift+T", "wire.curvature"},
        {"X", "wire.trim"}, {"E", "wire.extend"}, {"M", "measure.open"},
    };
    for (const auto& entry : expected) {
        bool found = false;
        for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
            if (command.id != entry.id) {
                continue;
            }
            found = command.defaultShortcut == entry.key;
        }
        if (!Explain((std::string(entry.key) + " は " + entry.id).c_str(), found)) {
            return false;
        }
    }
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        if (command.id == "draw.spline") {
            return Explain("S は一時スナップ解除専用",
                command.defaultShortcut.empty());
        }
    }
    (void)window;
    return false;
}

[[nodiscard]] bool CaseTransformToolsActuallyMove(V2MainWindow& window)
{
    // 移動・コピー・ミラー・回転は、道具箱にありながら何もしていなかった。
    // 点を集めるところまでは動いていたので、気づきにくかった。
    // ここでは「文書が本当に変わったか」だけを見る。案内文は見ない。
    auto& viewport = window.Viewport();
    using kachakacha::v2::modeling::DrawingTool;

    // 線を1本引いて、それを選ぶ。
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    const auto selectAll = [&window, &viewport] {
        viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
            window.Session().GetDocument().Snapshot(),
            kachakacha::v2::domain::EntityKind::Wire));
    };
    const auto wireCount = [&window] {
        int count = 0;
        for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
            // 見えているものだけ数える。消せなかった線は隠して残るので、
            // 全部数えると「消えていない」と読み違える。
            if (entity.kind == kachakacha::v2::domain::EntityKind::Wire
                && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
                ++count;
            }
        }
        return count;
    };
    // 2点置いて変換を確定させる。画面を通すので、通す道はUIと同じ。
    const auto placeTwo = [&viewport] {
        viewport.ClickAt(QPointF(viewport.width() * 0.3, viewport.height() * 0.5));
        viewport.ClickAt(QPointF(viewport.width() * 0.6, viewport.height() * 0.5));
    };

    selectAll();
    const int before = wireCount();
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    const std::size_t featuresBefore = window.Session().GetDocument().Snapshot().features.size();
    window.SelectTool(DrawingTool::Move);
    placeTwo();
    if (!Explain("移動で文書が変わる",
            window.Session().GetDocument().Revision() != revision)) {
        return false;
    }
    if (!Explain((std::string("移動は本数を増やさない(") + std::to_string(before)
                     + " → " + std::to_string(wireCount()) + ")").c_str(),
            wireCount() == before)) {
        return false;
    }
    // 何本動かしても 1 回の元に戻すで動かす前へ戻る(1 本ずつ戻していた)。やり直しも 1 回。
    window.RunCommand("edit.undo");
    if (!Explain((std::string("1 回の元に戻すで動かす前へ戻る(作り方 ") + std::to_string(featuresBefore)
                     + " → " + std::to_string(window.Session().GetDocument().Snapshot().features.size())
                     + ")").c_str(),
            window.Session().GetDocument().Snapshot().features.size() == featuresBefore
                && wireCount() == before)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("1 回のやり直しで動かした後へ戻る",
            window.Session().GetDocument().Snapshot().features.size() > featuresBefore
                && wireCount() == before)) {
        return false;
    }

    selectAll();
    const int beforeCopy = wireCount();
    window.SelectTool(DrawingTool::Copy);
    placeTwo();
    // 1本につき1本ずつ増える。まとめて1本にしない。
    if (!Explain((std::string("コピーは本数が倍になる(") + std::to_string(beforeCopy)
                     + " → " + std::to_string(wireCount()) + ")").c_str(),
            wireCount() == beforeCopy * 2)) {
        return false;
    }

    selectAll();
    const int beforeMirror = wireCount();
    window.SelectTool(DrawingTool::Mirror);
    placeTwo();
    if (!Explain((std::string("ミラーも本数が倍になる(") + std::to_string(beforeMirror)
                     + " → " + std::to_string(wireCount()) + ")").c_str(),
            wireCount() == beforeMirror * 2)) {
        return false;
    }

    // 回転は3点。中心・始まりの向き・終わりの向き。
    selectAll();
    const int beforeRotate = wireCount();
    const std::uint64_t rotateFrom = window.Session().GetDocument().Revision();
    window.SelectTool(DrawingTool::Rotate);
    viewport.ClickAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5));
    viewport.ClickAt(QPointF(viewport.width() * 0.7, viewport.height() * 0.5));
    viewport.ClickAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.3));
    if (!Explain("回転で文書が変わる",
            window.Session().GetDocument().Revision() != rotateFrom)) {
        return false;
    }
    // 3本を回したら3本のまま。1本にまとめない。名前も分け方も残る。
    return Explain((std::string("回転は本数を変えない(") + std::to_string(beforeRotate)
                       + " → " + std::to_string(wireCount()) + ")").c_str(),
        wireCount() == beforeRotate);
}

[[nodiscard]] bool CaseTransformRefusesDegenerateInput(V2MainWindow& window)
{
    // 同じ場所へ2回押したら、動かす距離が無い。黙って作らず、断って言う。
    auto& viewport = window.Viewport();
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Move);
    // 吸着を止めて押す。止めないと、1点目を置いたあとに出る吸着(延長や垂線)へ
    // 2点目が寄って、同じ場所を押したのに少しだけ離れてしまう。
    viewport.SetSnapSuppressed(true);
    const QPointF same(viewport.width() * 0.5, viewport.height() * 0.5);
    viewport.ClickAt(same);
    viewport.ClickAt(same);
    viewport.SetSnapSuppressed(false);
    if (!Explain("文書は変わらない",
            window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    return Explain((std::string("理由を出す(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("距離")));
}

[[nodiscard]] bool CaseGrabSelectedAndDrag(V2MainWindow& window)
{
    // V1では、選んでいる線の上を押してそのまま引きずれば付いてきた。
    // 道具を選んで点を2つ置く、という手順を踏まなくてよい。
    auto& viewport = window.Viewport();
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));

    // 線の真ん中を画面へ写して、そこを掴む。
    const auto& curves = window.Session().Scene().curves;
    if (!Explain("線が場面にある", !curves.empty())) {
        return false;
    }
    const auto onScreen = viewport.Mapping().Project(curves.front().segment.Evaluate(0.5));
    if (!Explain("線が画面に出ている", onScreen.has_value())) {
        return false;
    }
    const QPointF grabAt(onScreen->x, onScreen->y);

    if (!Explain("選んでいる線の上は掴める", viewport.BeginBodyDrag(grabAt))) {
        return false;
    }
    if (!Explain("掴んでいる印がつく", viewport.BodyDragging())) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    viewport.DragBody(grabAt + QPointF(60.0, 0.0));
    if (!Explain("引きずっている間は文書を変えない",
            window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    if (!Explain("離すと文書が変わる",
            viewport.ReleaseBodyDrag(grabAt + QPointF(60.0, 0.0)))) {
        return false;
    }
    if (!Explain("掴みが終わっている", !viewport.BodyDragging())) {
        return false;
    }
    return Explain("実際に文書が変わった",
        window.Session().GetDocument().Revision() != revision);
}

[[nodiscard]] bool CaseGrabWithoutDraggingIsJustAClick(V2MainWindow& window)
{
    // 押して離しただけを 0mm の移動として文書へ入れない。
    // 入れてしまうと、選び直すたびに履歴が伸びて、元に戻すが効かなくなる。
    auto& viewport = window.Viewport();
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    const auto& curves = window.Session().Scene().curves;
    if (!Explain("線が場面にある", !curves.empty())) {
        return false;
    }
    const auto onScreen = viewport.Mapping().Project(curves.front().segment.Evaluate(0.5));
    if (!Explain("線が画面に出ている", onScreen.has_value())) {
        return false;
    }
    const QPointF at(onScreen->x, onScreen->y);
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    if (!Explain("掴める", viewport.BeginBodyDrag(at))) {
        return false;
    }
    // 手の震えぶん(2px)だけ動かして離す。
    if (!Explain("動かしていないので何も起きない",
            !viewport.ReleaseBodyDrag(at + QPointF(2.0, 1.0)))) {
        return false;
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == revision);
}

[[nodiscard]] bool CaseControlPointsAreGrabbable(V2MainWindow& window)
{
    // V1では、選んでいるワイヤーの制御点が四角で出て、掴んで引きずれた。
    // V2には無く、いちど引いた線は消して引き直すしかなかった。
    auto& viewport = window.Viewport();
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));

    const auto shown = kachakacha::v2::app::ControlPointsForSelection(
        window.Session().Scene(), viewport.Selection());
    if (!Explain((std::string("直線には制御点が2つ出る(実際は ")
                     + std::to_string(shown.size()) + ")").c_str(),
            shown.size() == 2)) {
        return false;
    }
    const auto onScreen = viewport.Mapping().Project(shown.front().position);
    if (!Explain("制御点が画面に出ている", onScreen.has_value())) {
        return false;
    }
    const QPointF at(onScreen->x, onScreen->y);
    if (!Explain("制御点を掴める", viewport.BeginControlPointDrag(at))) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    viewport.DragControlPoint(at + QPointF(0.0, 50.0));
    if (!Explain("引きずっている間は文書を変えない",
            window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    if (!Explain("離すと文書が変わる",
            viewport.ReleaseControlPointDrag(at + QPointF(0.0, 50.0)))) {
        return false;
    }
    if (!Explain("掴みが終わっている", !viewport.ControlPointDragging())) {
        return false;
    }
    // 種類は変わらない。折れ線へ落ちていたら、ここで気づく。
    const auto& curves = window.Session().Scene().curves;
    if (!Explain("線が残っている", !curves.empty())) {
        return false;
    }
    return Explain("直線のままである",
        curves.front().segment.Kind() == kachakacha::v2::geometry::CurveKind::Line);
}

[[nodiscard]] bool CaseControlPointsOnlyOnSelected(V2MainWindow& window)
{
    // 全部の線に出すと画面が埋まって、どれを掴んだのか分からなくなる。
    auto& viewport = window.Viewport();
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    const auto none = kachakacha::v2::app::ControlPointsForSelection(
        window.Session().Scene(), viewport.Selection());
    return Explain((std::string("選んでいなければ出ない(実際は ")
                       + std::to_string(none.size()) + ")").c_str(), none.empty());
}

[[nodiscard]] bool CaseDisplayStagesHaveTheirOwnKeys(V2MainWindow& window)
{
    // V1は Ctrl+1/2/3 で 設計 / 完成形 / 選択だけ を **直に** 選べた。回して探さなくてよい。
    const struct {
        const char* key;
        const char* id;
    } expected[] = {
        {"Ctrl+1", "view.stage_all"},
        {"Ctrl+2", "view.stage_no_construction"},
        {"Ctrl+3", "view.stage_selection_only"},
        {"F2", "entity.rename"},
    };
    for (const auto& entry : expected) {
        bool found = false;
        for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
            if (command.id == entry.id) {
                found = command.defaultShortcut == entry.key;
            }
        }
        if (!Explain((std::string(entry.key) + " は " + entry.id).c_str(), found)) {
            return false;
        }
    }
    // 3回とも押して、形が変わらないことを見る。見え方だけの話である。
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    for (const auto& entry : expected) {
        window.RunCommand(entry.id);
    }
    return Explain("形は変わらない",
        window.Session().GetDocument().Revision() == revision);
}

[[nodiscard]] bool CaseRenameFromTheList(V2MainWindow& window)
{
    // V1は F2 で一覧の名前を変えられた。V2には無かった。
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    kachakacha::v2::base::EntityId target;
    bool haveTarget = false;
    for (const auto& entity : snapshot.entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Wire) {
            target = entity.id;
            haveTarget = true;
            break;
        }
    }
    if (!Explain("名前を変える相手がいる", haveTarget)) {
        return false;
    }
    const auto rename = [&window, &target](const char* name) {
        return window.Session().GetDocument().Run(
            kachakacha::v2::document::RenameEntityCommand(target, name));
    };
    if (!Explain("名前を変えられる", rename("屋根の線").committed)) {
        return false;
    }
    bool renamed = false;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.id == target) {
            renamed = entity.displayName == "屋根の線";
        }
    }
    if (!Explain("一覧に出る名前が変わった", renamed)) {
        return false;
    }
    // 空の名前は core が断る。文書へは入れない。
    const auto empty = kachakacha::v2::app::NormalizeEntityName("   ");
    return Explain("空の名前は断る", !empty.HasValue());
}

[[nodiscard]] bool CaseCursorInputOpensWhileDrawingAndPlacesByNumber(V2MainWindow& window)
{
    // 最初の点を置くと入力列が出て、長さを打って Enter すると、その長さで線が決まる
    // (ui-workflows §7、V1 の「実寸で確定」)。これまでは撮影のときしか開かなかった。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    if (!Explain("道具を選んだ直後は入力列が無い", !viewport.CursorPanel().active)) {
        return false;
    }
    const QPointF first(viewport.width() * 0.5, viewport.height() * 0.5);
    viewport.ClickAt(first);
    if (!Explain("1点目で入力列が出る", viewport.CursorPanel().active)) {
        return false;
    }
    // ポインタを右へ。向きはポインタ、長さは数で決まる。
    viewport.HoverAt(QPointF(first.x() + 120.0, first.y()));
    if (!Explain("主要欄(長さ)に焦点がある",
            viewport.CursorPanel().fields[viewport.CursorPanel().focusedIndex].id
                == "length")) {
        return false;
    }
    const int before = CountOfKind(window, kachakacha::v2::domain::EntityKind::Wire);
    if (!Explain("長さを打てる", viewport.TypeIntoCursorField(QStringLiteral("50")))) {
        return false;
    }
    if (!Explain("Enter で形が決まる", viewport.CommitCursorField())) {
        return false;
    }
    if (!Explain("数で点が置ける", viewport.PlacePointFromCursorInput())) {
        return false;
    }
    if (!Explain("線が1本増える",
            CountOfKind(window, kachakacha::v2::domain::EntityKind::Wire) == before + 1)) {
        return false;
    }
    if (!Explain("確定したら入力列は閉じる", !viewport.CursorPanel().active)) {
        return false;
    }
    const auto& curves = window.Session().Scene().curves;
    if (curves.empty()) {
        return false;
    }
    const auto delta = curves.back().segment.EndPoint() - curves.back().segment.StartPoint();
    if (!Explain((std::string("長さが 50mm(実際は ") + std::to_string(delta.Length())
                     + ")").c_str(),
            std::abs(delta.Length() - 50.0) < 1.0e-6)) {
        return false;
    }
    if (!Explain("向きはポインタの側(+X)", delta.x > 49.9)) {
        return false;
    }
    // Esc で入力列ごとやめられる。文書は変わらない。
    viewport.ClickAt(first);
    if (!Explain("次の線でもまた出る", viewport.CursorPanel().active)) {
        return false;
    }
    const auto revision = window.Session().GetDocument().Revision();
    (void)viewport.PressEscape();
    return Explain("Esc で閉じて文書は変わらない",
        !viewport.CursorPanel().active
            && window.Session().GetDocument().Revision() == revision);
}

} // namespace

std::vector<SelfTestCase> InputCases()
{
    return {
        {"作図中に入力列が出て数で線が決まる", &CaseCursorInputOpensWhileDrawingAndPlacesByNumber},
        {"中ボタンで画面が動く", &CaseMiddleDragPansTheView},
        {"軌道回転で視点が回る", &CaseOrbitTurnsTheView},
        {"Escで選択へ戻り選択も解ける", &CaseEscapeGoesBackToSelect},
        {"Shiftで水平になりSで吸着が止まる", &CaseShiftConstrainsAndSSuppressesSnap},
        {"吸着半径は拡大しても同じで揺れでは離さずSの間だけ止まる",
            &CaseSnapRadiusHoldAndSKeyThroughViewport},
        {"主要キーが操作キーと競合しない", &CasePrimaryShortcutsDoNotConflict},
        {"移動と複製が本当に効く", &CaseTransformToolsActuallyMove},
        {"つぶれた変換は断る", &CaseTransformRefusesDegenerateInput},
        {"選んだ物を掴んで動かせる", &CaseGrabSelectedAndDrag},
        {"押しただけでは動かない", &CaseGrabWithoutDraggingIsJustAClick},
        {"制御点を掴んで動かせる", &CaseControlPointsAreGrabbable},
        {"制御点は選んだ線にだけ出る", &CaseControlPointsOnlyOnSelected},
        {"表示の段に数字のキーがある", &CaseDisplayStagesHaveTheirOwnKeys},
        {"一覧で名前を変えられる", &CaseRenameFromTheList},
    };
}

} // namespace kachakacha::v2::selftest
