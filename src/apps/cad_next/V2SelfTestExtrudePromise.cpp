//! 押し出しの約束(Codex P1-EXTRUDE-R1 の指摘の回帰試験)。
//!
//! ここで見るのは「作れるか」ではなく **約束を守っているか** である。
//!   - 下見を出しただけでは文書が変わらない。やめれば始める前とまったく同じ。
//!   - 1回の操作は1回の取り消しで完全に戻る。
//!   - 棚で選んだ演算のまま作られる。表示と実行が食い違わない。

#include "V2SelfTest.h"

#include "V2ExtrudeDialog.h"
#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {


//! 閉じた矩形を1つ引いて選ぶ。押し出しの相手になる。
[[nodiscard]] bool DrawClosedRectangle(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    // 見ている場所も戻す。戻さないと、同じ画面位置に引いた2つ目の矩形が
    // 別の場所に出て、足す・引くの相手と重ならなくなる。
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    // 吸着を止めて引く。止めないと、先に作った立体の角へ隅が吸い付き、
    // 矩形が作図面から少し浮く。そうなると
    // 「輪郭が同じ平面に載っていません」(EXT-001)で断られる。
    // ここで見たいのは演算の選び方であって、吸着の話ではない。
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * 0.35, viewport.height() * 0.35));
    viewport.HoverAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    return !viewport.Selection().entityIds.empty();
}

[[nodiscard]] int CountParts(V2MainWindow& window)
{
    return CountOfKind(window, kachakacha::v2::domain::EntityKind::Part);
}

//! R1 B2。面の押し引きを始めてやめても、文書がまったく変わらないこと。
[[nodiscard]] bool CaseFacePushPullCancelLeavesDocumentUntouched(V2MainWindow& window)
{
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    kachakacha::v2::base::EntityId part;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Part) {
            part = entity.id;
        }
    }
    if (!Explain("立体ができる", !part.IsNil())) {
        return false;
    }
    auto& viewport = window.Viewport();
    kachakacha::v2::app::SelectionSet faceSelection;
    kachakacha::v2::app::SelectionRef face;
    face.entityId = part;
    face.kind = kachakacha::v2::app::SelectionElementKind::Face;
    face.pickedFaceIndex = 0;
    faceSelection.ordered.push_back(face);
    faceSelection.entityIds.push_back(part);

    // やめ方は3通り。どれでも文書は始める前とまったく同じでなければならない。
    const char* const kWays[] = {"Esc", "棚のキャンセル", "道具替え"};
    for (int way = 0; way < 3; ++way) {
        viewport.SetSelection(faceSelection);
        const std::uint64_t revision = window.Session().GetDocument().Revision();
        const std::size_t entities =
            window.Session().GetDocument().Snapshot().entities.size();
        window.RunCommand("part.extrude");   // 下見を出す
        if (!Explain((std::string(kWays[way]) + ": 下見を出しただけでは文書が変わらない")
                         .c_str(),
                window.Session().GetDocument().Revision() == revision)) {
            return false;
        }
        if (way == 0) {
            viewport.CancelTool();
            window.RunCommand("view.fit_all");
            window.EndExtrudePreview();
        } else if (way == 1) {
            window.ExtrudeDock().PressCancel();
        } else {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        }
        if (!Explain((std::string(kWays[way]) + ": やめても文書が変わらない").c_str(),
                window.Session().GetDocument().Revision() == revision)) {
            return false;
        }
        if (!Explain((std::string(kWays[way]) + ": 物の数も変わらない").c_str(),
                window.Session().GetDocument().Snapshot().entities.size() == entities)) {
            return false;
        }
        if (!Explain((std::string(kWays[way]) + ": 下見も残らない").c_str(),
                !viewport.ExtrudeHandleShown())) {
            return false;
        }
    }
    return Explain("面の押し引きをやめても文書は不変", true);
}

//! R1 B3。1回の押し出しが1回の取り消しで完全に戻ること。
[[nodiscard]] bool CaseExtrudeIsOneUndoStep(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    const std::size_t before = window.Session().GetDocument().Snapshot().entities.size();
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    if (!Explain("立体ができる", CountParts(window) == 1)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain((std::string("1回の取り消しで完全に戻る(") + std::to_string(before)
                     + " → "
                     + std::to_string(window.Session().GetDocument().Snapshot()
                               .entities.size())
                     + ")").c_str(),
            window.Session().GetDocument().Snapshot().entities.size() == before)) {
        return false;
    }
    if (!Explain("立体も消える", CountParts(window) == 0)) {
        return false;
    }
    window.RunCommand("edit.redo");
    return Explain("1回のやり直しで完全に戻る", CountParts(window) == 1);
}

//! R1 B4。棚で選んだ演算が、確定のときに既定へ戻されないこと。
[[nodiscard]] bool CaseChosenBooleanSurvivesConfirm(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    kachakacha::v2::base::EntityId part;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Part) {
            part = entity.id;
        }
    }
    if (!Explain("立体ができる", !part.IsNil())) {
        return false;
    }
    // 立体 + 輪郭。読み取りの既定は「切削」。
    //
    // 輪郭は **立体を作ったときの矩形をそのまま使う。** 2枚目を引き直すと、
    // 引いた場所や吸着しだいで別の平面に載ることがあり、
    // 「輪郭が同じ平面に載っていません」(EXT-001)で断られてしまう。
    // ここで見たいのは棚で選んだ演算が残るかであって、平面の話ではない。
    auto& viewport = window.Viewport();
    kachakacha::v2::base::EntityId rectangle;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Wire) {
            rectangle = entity.id;
            break;
        }
    }
    if (!Explain("立体を作った矩形が残っている", !rectangle.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet both;
    both.entityIds.push_back(rectangle);
    both.entityIds.push_back(part);
    kachakacha::v2::app::SelectionRef solid;
    solid.entityId = part;
    both.ordered.push_back(solid);
    viewport.SetSelection(both);
    window.RunCommand("part.extrude");   // 下見と棚
    // 読み取り結果をここで控える。断られたときに、CAD が何を輪郭と見たかが要る。
    const std::string readAs = window.StatusText().toStdString();
    // 人が「足す」を選ぶ。
    window.ExtrudeDock().ChooseBoolean(
        kachakacha::v2::modeling::ExtrudeBooleanMode::AddToPart);
    if (!Explain("棚に「足す」が出る",
            window.ExtrudeDock().BooleanMode()
                == kachakacha::v2::modeling::ExtrudeBooleanMode::AddToPart)) {
        return false;
    }
    // 1回目より長く押す。同じ場所へ同じ大きさで短く押すと、
    // 足した形が元の立体の中へすっぽり入り、体積が変わらないので
    // 「重なっていません」(EXT-004)で断られる。それは正しい断り方である。
    window.ExtrudeDock().TypeDistanceMm(20.0);
    window.RunCommand("part.extrude");   // 確定
    // 保存された作り方が「足す」になっていること。表示だけ合っていて実行が違う、を防ぐ。
    bool found = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        if (definition->booleanMode
            == static_cast<int>(kachakacha::v2::modeling::ExtrudeBooleanMode::AddToPart)) {
            found = true;
        }
    }
    return Explain((std::string("選んだ演算のまま作られる(読み取り: ") + readAs
                       + " / 帯は " + window.StatusText().toStdString() + ")").c_str(),
        found);
}


//! Codex P1-EXTRUDE-R4 B1。棚の「方向」が、矢印・下見・確定・保存まで通ること。
//!
//! 欄に出ているだけで何も変わらないなら、それは嘘の欄である。
[[nodiscard]] bool CaseShelfDirectionReachesTheShape(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    // 上面に矩形を引く。輪郭の平面の法線も、作業平面の法線も Z になる。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");   // 下見と棚
    if (!Explain("矢印が出る", viewport.ExtrudeHandleShown())) {
        return false;
    }
    // 「面に垂直」と「作業平面に垂直」を行き来しても、矢印が向きを失わないこと。
    for (const auto mode :
        {kachakacha::v2::modeling::ExtrudeDirectionMode::WorkPlaneNormal,
            kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal}) {
        window.ExtrudeDock().ChooseDirection(mode);
        window.RefreshExtrudeFromDock();
        if (!Explain("棚の向きが窓へ伝わる",
                window.ExtrudeDock().DirectionMode() == mode)) {
            return false;
        }
        const auto direction = window.ExtrudeDirectionNow();
        if (!Explain((std::string("矢印の向きが決まる(")
                         + std::to_string(direction.z) + ")").c_str(),
                std::abs(direction.z) > 0.9)) {
            return false;
        }
    }

    window.ExtrudeDock().TypeDistanceMm(4.0);
    window.RunCommand("part.extrude");   // 確定
    if (!Explain((std::string("立体ができる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            CountParts(window) == 1)) {
        return false;
    }
    // 保存された作り方の向きが、矢印と同じであること。
    bool matched = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        const auto& saved = definition->direction;
        matched = std::abs(std::abs(saved.z) - 1.0) < 1.0e-6;
    }
    return Explain("保存された向きも矢印と同じ", matched);
}

//! 輪郭の平面と作業平面が別の向きのとき、棚の表示どおりに押せるか。
//!
//! これまでの試験は、上面に引いた矩形で両方の法線を Z に揃えていたので、
//! 2つの決め方がどちらも `abs(z) > 0.9` になり、**取り違えても通っていた**
//! (Codex P1-EXTRUDE-R5 の MISSING TESTS)。ここでは作業平面だけを傾ける。
[[nodiscard]] bool CaseDirectionFollowsWhatTheShelfShows(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    // 作業平面だけを傾ける。輪郭はさっき引いた面(法線 Z)のまま。
    kachakacha::v2::modeling::WorkPlaneFrame tilted;
    tilted.origin = {0.0, 0.0, 0.0};
    tilted.normal = {1.0, 0.0, 0.0};
    tilted.uAxis = {0.0, 1.0, 0.0};
    tilted.vAxis = {0.0, 0.0, 1.0};
    viewport.SetWorkPlane(tilted);

    window.RunCommand("part.extrude");   // 下見と棚
    if (!Explain("矢印が出る", viewport.ExtrudeHandleShown())) {
        return false;
    }
    // **棚を触る前**の向きが、棚が見せているものと同じであること。
    // ここが食い違っていた。棚は「面に垂直」と出しながら、矢印と下見は
    // 作業平面の法線(X)へ進んでいた。
    const bool shelfShowsProfile = window.ExtrudeDock().DirectionMode()
        == kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal;
    if (!Explain("棚は最初『面に垂直』を見せている", shelfShowsProfile)) {
        return false;
    }
    const auto firstDirection = window.ExtrudeDirectionNow();
    if (!Explain((std::string("触る前の矢印が輪郭の法線へ向く(x=")
                     + std::to_string(firstDirection.x) + " z="
                     + std::to_string(firstDirection.z) + ")").c_str(),
            std::abs(firstDirection.z) > 0.9 && std::abs(firstDirection.x) < 0.1)) {
        return false;
    }

    // 「作業平面に垂直」を選ぶと、今度は作業平面の法線(X)へ向くこと。
    window.ExtrudeDock().ChooseDirection(
        kachakacha::v2::modeling::ExtrudeDirectionMode::WorkPlaneNormal);
    window.RefreshExtrudeFromDock();
    const auto workPlaneDirection = window.ExtrudeDirectionNow();
    if (!Explain((std::string("作業平面に垂直では作業平面の法線へ向く(x=")
                     + std::to_string(workPlaneDirection.x) + ")").c_str(),
            std::abs(workPlaneDirection.x) > 0.9)) {
        return false;
    }

    // 戻したら、また輪郭の法線へ。2つの決め方が本当に別の向きを出している。
    window.ExtrudeDock().ChooseDirection(
        kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal);
    window.RefreshExtrudeFromDock();
    const auto backDirection = window.ExtrudeDirectionNow();
    if (!Explain("面に垂直へ戻すと輪郭の法線へ戻る",
            std::abs(backDirection.z) > 0.9 && std::abs(backDirection.x) < 0.1)) {
        return false;
    }

    // 確定した形と保存された向きも、見せていた向きと同じであること。
    window.ExtrudeDock().TypeDistanceMm(3.0);
    window.RunCommand("part.extrude");
    if (!Explain("立体ができる", CountParts(window) == 1)) {
        return false;
    }
    bool matched = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        const auto& saved = definition->direction;
        matched = std::abs(std::abs(saved.z) - 1.0) < 1.0e-6 && std::abs(saved.x) < 1.0e-6;
    }
    if (!Explain("保存された向きも輪郭の法線", matched)) {
        return false;
    }
    // 2回目を始めたとき、棚の表示が「面に垂直」のまま残っていること。
    // 確定のときに向きを CustomXYZ へ畳むので、そのまま覚えると決め方が失われる。
    window.RunCommand("select.all");
    window.RunCommand("part.extrude");
    return Explain("次に始めても棚の決め方が残っている",
        window.ExtrudeDock().DirectionMode()
            == kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal);
}

//! Codex P1-EXTRUDE-R6 B1。詳細の窓を開いて何も触らずに閉じても、
//! 決めごとが変わらないこと。
//!
//! 窓は渡された値を欄へ映していなかったので、開いて確定するだけで
//! 向きが並びの先頭(作業平面に垂直)へ、演算が「新しい部品」へ黙って戻っていた。
[[nodiscard]] bool CaseDialogKeepsWhatItWasGiven(V2MainWindow& window)
{
    using kachakacha::v2::modeling::ExtrudeBooleanMode;
    using kachakacha::v2::modeling::ExtrudeDirectionMode;
    using kachakacha::v2::modeling::ExtrudeExtentMode;

    kachakacha::v2::app::ExtrudeChoice initial;
    initial.direction = ExtrudeDirectionMode::WorldY;
    initial.customDirection = {0.0, 0.0, 7.0};
    initial.reversed = true;
    initial.extent = ExtrudeExtentMode::SymmetricDistance;
    initial.distanceMm = 3.25;
    initial.secondDistanceMm = 1.5;
    initial.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    initial.hasSelectedPart = true;
    initial.makePart = true;
    initial.makeEndProfileWire = true;

    kachakacha::v2::app::ExtrudeFacts facts;
    facts.closedProfiles = 1;
    facts.parts = 1;

    V2ExtrudeDialog dialog(initial, facts, {}, &window);
    const auto answered = dialog.Choice();
    if (!Explain("開いて何も触らなければ向きが変わらない",
            answered.direction == initial.direction)) {
        return false;
    }
    if (!Explain("欄に無い自由な向きも落とさない",
            std::abs(answered.customDirection.z - 7.0) < 1.0e-9)) {
        return false;
    }
    if (!Explain("逆向きも残る", answered.reversed == initial.reversed)) {
        return false;
    }
    if (!Explain("どこまで押すかも残る", answered.extent == initial.extent)) {
        return false;
    }
    if (!Explain("演算も残る", answered.booleanMode == initial.booleanMode)) {
        return false;
    }
    return Explain("作るものも残る",
        answered.makePart == initial.makePart
            && answered.makeEndProfileWire == initial.makeEndProfileWire);
}

//! Codex P1-EXTRUDE-R6 B2。詳細の窓で選んだ向きが、
//! 矢印・確定・保存・次回の初期値まで通ること。
//!
//! 棚が出せるのは2通りだけなので、残り5通りは確定のときに
//! 棚を触る前の決め方へ戻され、選んでも何も起きなかった。
//! 矢印と下見も、2通り以外はすべて作業平面の法線を向いていた。
//!
//! 矩形は上面に引くので、輪郭は z 一定の平面に載る。X や Y はその平面に
//! 寝てしまい「厚みが出ません」(EXT-007)になるので、ここでは斜めの
//! 自由な向きを選ぶ。輪郭の法線とも作業平面の法線とも違う向きになる。
[[nodiscard]] bool CaseDialogDirectionReachesTheShape(V2MainWindow& window)
{
    using kachakacha::v2::modeling::ExtrudeDirectionMode;
    window.RunCommand("file.new");
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");   // 下見と棚
    if (!Explain("矢印が出る", window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    // 窓が「斜めの向きを数値で決めた」と答えたのと同じことをする。
    // **窓は作らない。** 決めたことを棚と矢印と下見へ映して戻るだけである。
    // 長さは1でない値を渡す。向きに長さを持たせたまま矢印へ使うと、
    // 下見だけが伸びて、出来る形と食い違う(Codex P1-EXTRUDE-R7 B2)。
    auto chosen = window.ExtrudeChoice();
    chosen.direction = ExtrudeDirectionMode::CustomXYZ;
    chosen.customDirection = {4.2, 0.0, 5.6};   // 長さ7、向きは (0.6, 0, 0.8)
    chosen.distanceMm = 4.0;
    window.ApplyExtrudeChoice(chosen);

    // **確定する前に**、矢印と下見が決めたとおりになっていること。
    // ここを見ないと「次に始めたときには合っている」しか言えない
    // (Codex P1-EXTRUDE-R7 B1)。
    const auto shownDirection = window.ExtrudeDirectionNow();
    if (!Explain((std::string("確定前の矢印が決めた向きを向く(x=")
                     + std::to_string(shownDirection.x) + " z="
                     + std::to_string(shownDirection.z) + ")").c_str(),
            std::abs(shownDirection.x - 0.6) < 1.0e-6
                && std::abs(shownDirection.z - 0.8) < 1.0e-6)) {
        return false;
    }
    if (!Explain((std::string("確定前の矢印の長さが1(len=")
                     + std::to_string(shownDirection.Length()) + ")").c_str(),
            std::abs(shownDirection.Length() - 1.0) < 1.0e-9)) {
        return false;
    }
    // 下見も同じだけ進んでいること。長さの二重掛けはここに出る。
    const auto& loops = window.Viewport().ExtrudeHandlePreview();
    const auto outline = window.ExtrudeOutline();
    bool movedRight = false;
    if (!loops.empty() && !outline.empty() && loops.front().size() == outline.size()) {
        const auto moved = loops.front().front() - outline.front();
        movedRight = std::abs(moved.Length() - 4.0) < 1.0e-6;
    }
    if (!Explain("確定前の下見も、打った距離だけ進んでいる", movedRight)) {
        return false;
    }
    if (!Explain("棚も『詳細で決めた向き』を見せている",
            window.ExtrudeDock().DirectionMode() == ExtrudeDirectionMode::CustomXYZ)) {
        return false;
    }
    window.RunCommand("part.extrude");   // 確定
    if (!Explain((std::string("立体ができる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            CountParts(window) == 1)) {
        return false;
    }
    bool matched = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        const auto& saved = definition->direction;
        matched = std::abs(std::abs(saved.x) - 0.6) < 1.0e-6
            && std::abs(std::abs(saved.z) - 0.8) < 1.0e-6;
    }
    if (!Explain("保存された向きが、窓で選んだ斜めの向き", matched)) {
        return false;
    }
    if (!Explain("覚えている決め方も『数値で決める』のまま",
            window.ExtrudeChoice().direction == ExtrudeDirectionMode::CustomXYZ)) {
        return false;
    }
    if (!Explain("覚えている向きの数も落ちていない",
            std::abs(window.ExtrudeChoice().customDirection.x - 0.6) < 1.0e-9)) {
        return false;
    }
    // 次に始めたとき、棚がその決め方を名前で見せること。
    // 見せずにいると、棚は「作業平面に垂直」と出しながら別の向きへ押す。
    window.RunCommand("select.all");
    window.RunCommand("part.extrude");
    if (!Explain("棚も『詳細で決めた向き』を見せている",
            window.ExtrudeDock().DirectionMode() == ExtrudeDirectionMode::CustomXYZ)) {
        return false;
    }
    const auto direction = window.ExtrudeDirectionNow();
    if (!Explain((std::string("次の矢印も同じ斜めの向き(x=")
                     + std::to_string(direction.x) + " z="
                     + std::to_string(direction.z) + ")").c_str(),
            std::abs(direction.x - 0.6) < 1.0e-6
                && std::abs(direction.z - 0.8) < 1.0e-6)) {
        return false;
    }
    // 棚の別の欄を触っても、決めた向きが落ちないこと。
    // 棚が2つしか出せなかったころは、距離を打ち直しただけで
    // 向きが「面に垂直」へ戻っていた。
    window.ExtrudeDock().TypeDistanceMm(6.0);
    const auto afterTyping = window.ExtrudeDirectionNow();
    return Explain((std::string("距離を打ち直しても向きが変わらない(x=")
                       + std::to_string(afterTyping.x) + ")").c_str(),
        std::abs(afterTyping.x - 0.6) < 1.0e-6
            && window.ExtrudeChoice().direction == ExtrudeDirectionMode::CustomXYZ);
}

} // namespace

std::vector<SelfTestCase> ExtrudePromiseCases()
{
    return {
        {"面の押し引きをやめても文書が変わらない",
            CaseFacePushPullCancelLeavesDocumentUntouched},
        {"押し出しは1回の取り消しで完全に戻る", CaseExtrudeIsOneUndoStep},
        {"棚で選んだ演算のまま作られる", CaseChosenBooleanSurvivesConfirm},
        {"棚で選んだ向きが矢印と確定と保存まで通る", CaseShelfDirectionReachesTheShape},
        {"輪郭と作業平面が別の向きでも棚の表示どおりに押せる",
            CaseDirectionFollowsWhatTheShelfShows},
        {"詳細の窓は渡した決めごとをそのまま見せる", CaseDialogKeepsWhatItWasGiven},
        {"詳細の窓で選んだ向きが矢印と確定と次回まで通る",
            CaseDialogDirectionReachesTheShape},
    };
}

} // namespace kachakacha::v2::selftest
