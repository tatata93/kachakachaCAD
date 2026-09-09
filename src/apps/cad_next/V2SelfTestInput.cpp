//! V1同等の操作系のケース(docs/v2/v1-input-parity.md)。
//!
//! 中ボタンでの画面移動、Shift+中ボタンの軌道回転、Esc の段取り、
//! 作図中の Shift と Ctrl、V1のキー配置、そして移動・複製・鏡映・回転。
//! どれも「V1では出来たのに V2で出来なかった」ものなので、
//! ここへまとめておくと、また落ちたときに何が戻ったのかがすぐ分かる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/view/ViewOrientation.h"

#include <cmath>
#include <cstdint>
#include <string>

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

[[nodiscard]] bool CaseShiftConstrainsAndCtrlSuppressesSnap(V2MainWindow& window)
{
    // Shift で水平・垂直へ寄る。Ctrl で吸着が止まる。どちらも押している間だけ。
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
    // Ctrl は帯の言い方で確かめる。吸着していないときは、そう出る。
    viewport.SetSnapSuppressedByKey(true);
    viewport.HoverAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5));
    const bool saidNoSnap = window.StatusText().contains(QStringLiteral("スナップなし"));
    viewport.SetSnapSuppressedByKey(false);
    return Explain((std::string("Ctrl中は吸着しないと言う(")
                       + window.StatusText().toStdString() + ")").c_str(), saidNoSnap);
}

[[nodiscard]] bool CaseV1ShortcutsAreBack(V2MainWindow& window)
{
    // V1で手が覚えたキーが、V1と同じ道具を出すこと。
    // ここがずれると、使うほど間違える。
    const struct {
        const char* key;
        const char* id;
    } expected[] = {
        {"V", "selection.activate"}, {"D", "draw.point"}, {"L", "draw.line"},
        {"P", "draw.polyline"}, {"R", "draw.rectangle"}, {"C", "draw.circle"},
        {"A", "draw.arc"}, {"B", "draw.bezier"}, {"S", "draw.spline"},
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
    (void)window;
    return true;
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
    const QPointF same(viewport.width() * 0.5, viewport.height() * 0.5);
    viewport.ClickAt(same);
    viewport.ClickAt(same);
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

} // namespace

std::vector<SelfTestCase> InputCases()
{
    return {
        {"中ボタンで画面が動く", &CaseMiddleDragPansTheView},
        {"軌道回転で視点が回る", &CaseOrbitTurnsTheView},
        {"Escで選択へ戻り選択も解ける", &CaseEscapeGoesBackToSelect},
        {"Shiftで水平になりCtrlで吸着が止まる", &CaseShiftConstrainsAndCtrlSuppressesSnap},
        {"V1のキーが戻っている", &CaseV1ShortcutsAreBack},
        {"移動と複製が本当に効く", &CaseTransformToolsActuallyMove},
        {"つぶれた変換は断る", &CaseTransformRefusesDegenerateInput},
        {"選んだ物を掴んで動かせる", &CaseGrabSelectedAndDrag},
        {"押しただけでは動かない", &CaseGrabWithoutDraggingIsJustAClick},
        {"制御点を掴んで動かせる", &CaseControlPointsAreGrabbable},
        {"制御点は選んだ線にだけ出る", &CaseControlPointsOnlyOnSelected},
    };
}

} // namespace kachakacha::v2::selftest
