// カーソル連動の数値入力(AT-UIX-003)。
#include "kachakacha/app/CursorInput.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::geometry::QuantityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;
using namespace kachakacha::v2::app;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] std::size_t IndexOf(const CursorInputPanel& panel, std::string_view id)
{
    for (std::size_t index = 0; index < panel.fields.size(); ++index) {
        if (panel.fields[index].id == id) {
            return index;
        }
    }
    return panel.fields.size();
}

//! その欄へ式を入れて Enter を押す。
[[nodiscard]] kachakacha::v2::base::Result<CursorCommitResult> TypeAndEnter(
    const CursorInputPanel& panel, std::string_view fieldId, std::string_view text,
    const Vector3& pointer)
{
    const auto focused = FocusField(panel, fieldId);
    Require(focused.HasValue(), std::string("欄を選べる: ") + std::string(fieldId));
    const auto typed = SetFieldText(focused.Value(), focused.Value().focusedIndex, text);
    Require(typed.HasValue(), "打てる");
    return CommitFocusedField(typed.Value(), pointer);
}

[[nodiscard]] CursorInputPanel BeginLine()
{
    const auto begun = BeginCursorInput(DrawingTool::Line, true);
    Require(begun.HasValue(), "直線の入力列が出る");
    return begun.Value();
}

} // namespace

KACHA_V2_TEST(cursor, 直線の最初の主要欄へ焦点が合う)
{
    const CursorInputPanel panel = BeginLine();
    Require(panel.active, "出ている");
    RequireEqual(panel.fields[panel.focusedIndex].id, "length", "長さ欄");
    Require(panel.fields[panel.focusedIndex].primary, "主要欄である");
}

KACHA_V2_TEST(cursor, 道具ごとに主要欄がちょうど1つある)
{
    for (DrawingTool tool : {DrawingTool::Line, DrawingTool::Circle, DrawingTool::Arc,
             DrawingTool::Move}) {
        for (bool onPlane : {true, false}) {
            const auto& fields = CursorFieldsFor(tool, onPlane);
            Require(!fields.empty(), "欄がある");
            int primaries = 0;
            for (const CursorField& field : fields) {
                primaries += field.primary ? 1 : 0;
                Require(!field.id.empty() && !field.labelJa.empty(), "名前がある");
            }
            RequireEqual(std::to_string(primaries), "1", "主要欄は1つ");
        }
    }
}

KACHA_V2_TEST(cursor, 作業平面と3Dで欄立てが変わる)
{
    const auto& planar = CursorFieldsFor(DrawingTool::Line, true);
    const auto& spatial = CursorFieldsFor(DrawingTool::Line, false);
    RequireEqual(std::to_string(planar.size()), "4", "長さ・角度・du・dv");
    RequireEqual(std::to_string(spatial.size()), "7", "長さ・dXYZ・各軸との角度");
}

KACHA_V2_TEST(cursor, 数値入力を使わない道具は断る)
{
    const auto result = BeginCursorInput(DrawingTool::Trim, true);
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C001", "使わない道具");
    Require(!ToolUsesCursorInput(DrawingTool::Trim), "使わない");
    Require(ToolUsesCursorInput(DrawingTool::Line), "直線は使う");
}

KACHA_V2_TEST(cursor, Tabで欄が回る)
{
    CursorInputPanel panel = BeginLine();
    const std::size_t start = panel.focusedIndex;
    for (std::size_t step = 1; step <= panel.fields.size(); ++step) {
        const auto moved = FocusNextField(panel, false);
        Require(moved.HasValue(), "動く");
        panel = moved.Value();
        RequireEqual(std::to_string(panel.focusedIndex),
            std::to_string((start + step) % panel.fields.size()), "1つずつ進む");
    }
    RequireEqual(std::to_string(panel.focusedIndex), std::to_string(start), "1周で戻る");
}

KACHA_V2_TEST(cursor, ShiftTabで逆に回る)
{
    const CursorInputPanel panel = BeginLine();
    const auto back = FocusNextField(panel, true);
    Require(back.HasValue(), "動く");
    RequireEqual(std::to_string(back.Value().focusedIndex),
        std::to_string((panel.focusedIndex + panel.fields.size() - 1) % panel.fields.size()),
        "1つ戻る");
}

KACHA_V2_TEST(cursor, 出ていない入力列は動かせない)
{
    CursorInputPanel panel;
    for (const auto& code : {FirstCode(FocusNextField(panel, false).Diagnostics()),
             FirstCode(UpdateFromPointer(panel, Vector3{1, 1, 0}).Diagnostics()),
             FirstCode(CommitFocusedField(panel, Vector3{1, 1, 0}).Diagnostics())}) {
        RequireEqual(code, "UI-C002", "出ていない");
    }
}

KACHA_V2_TEST(cursor, 無い欄を選んだら断る)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = FocusField(panel, "radius");
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C003", "欄がない");
}

KACHA_V2_TEST(cursor, 式を評価して確定できる)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = TypeAndEnter(panel, "length", "(180/2)*3", Vector3{10, 10, 0});
    Require(result.HasValue(), "確定できた");
    const CursorInputPanel& next = result.Value().panel;
    const std::size_t at = IndexOf(next, "length");
    RequireNear(next.states[at].value, 270.0, 1e-9, "270 mm");
    Require(next.states[at].locked, "ロックされる");
    Require(result.Value().readyToFinish, "主要欄が埋まったので確定してよい");
}

KACHA_V2_TEST(cursor, 式と評価値を同時に出す)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = TypeAndEnter(panel, "length", "(180/2)*3", Vector3{10, 10, 0});
    Require(result.HasValue(), "確定できた");
    const CursorInputPanel& next = result.Value().panel;
    const std::size_t at = IndexOf(next, "length");
    RequireEqual(FieldDisplayJa(next.fields[at], next.states[at]), "(180/2)*3 = 270 mm",
        "式 = 値 単位");
}

KACHA_V2_TEST(cursor, 角度欄は度で出す)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = TypeAndEnter(panel, "angle", "30deg", Vector3{10, 10, 0});
    Require(result.HasValue(), "確定できた");
    const CursorInputPanel& next = result.Value().panel;
    const std::size_t at = IndexOf(next, "angle");
    RequireNear(next.states[at].value, 30.0 * kPi / 180.0, 1e-9, "rad で持つ");
    RequireEqual(FieldDisplayJa(next.fields[at], next.states[at]), "30deg = 30 deg",
        "度で見せる");
}

KACHA_V2_TEST(cursor, 全角で打っても通る)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = TypeAndEnter(panel, "length", "１２０", Vector3{10, 10, 0});
    Require(result.HasValue(), "確定できた");
    const std::size_t at = IndexOf(result.Value().panel, "length");
    RequireNear(result.Value().panel.states[at].value, 120.0, 1e-9, "120 mm");
}

KACHA_V2_TEST(cursor, 壊れた式は確定しない)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = TypeAndEnter(panel, "length", "10/0", Vector3{10, 10, 0});
    Require(!result.HasValue(), "断る");
    Require(!result.Diagnostics().empty(), "理由が出る");
    // もとの入力列は変わらない。
    const std::size_t at = IndexOf(panel, "length");
    Require(!panel.states[at].locked, "ロックされない");
}

KACHA_V2_TEST(cursor, 空の欄でEnterを押しても確定しない)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = CommitFocusedField(panel, Vector3{});
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C008", "空の欄");
}

KACHA_V2_TEST(cursor, マウスを動かすと未ロックの欄が変わる)
{
    const CursorInputPanel panel = BeginLine();
    const auto moved = UpdateFromPointer(panel, Vector3{30.0, 40.0, 0.0});
    Require(moved.HasValue(), "動いた");
    const CursorInputPanel& next = moved.Value();
    RequireNear(next.states[IndexOf(next, "length")].value, 50.0, 1e-9, "3:4:5");
    RequireNear(next.states[IndexOf(next, "du")].value, 30.0, 1e-9, "du");
    RequireNear(next.states[IndexOf(next, "dv")].value, 40.0, 1e-9, "dv");
    RequireNear(next.states[IndexOf(next, "angle")].value, std::atan2(40.0, 30.0), 1e-9,
        "角度");
}

KACHA_V2_TEST(cursor, ロックした欄はマウスで動かない)
{
    const CursorInputPanel panel = BeginLine();
    const auto locked = TypeAndEnter(panel, "length", "100", Vector3{30.0, 40.0, 0.0});
    Require(locked.HasValue(), "確定できた");
    const auto moved = UpdateFromPointer(locked.Value().panel, Vector3{3.0, 4.0, 0.0});
    Require(moved.HasValue(), "動いた");
    const CursorInputPanel& next = moved.Value();
    RequireNear(next.states[IndexOf(next, "length")].value, 100.0, 1e-9, "長さは動かない");
    // 向きはマウスから取り、長さは100のまま。
    RequireNear(next.states[IndexOf(next, "du")].value, 60.0, 1e-9, "du は伸びる");
    RequireNear(next.states[IndexOf(next, "dv")].value, 80.0, 1e-9, "dv は伸びる");
}

KACHA_V2_TEST(cursor, 長さと角度をロックすると形が決まる)
{
    CursorInputPanel panel = BeginLine();
    panel = TypeAndEnter(panel, "length", "100", Vector3{10, 10, 0}).Value().panel;
    panel = TypeAndEnter(panel, "angle", "30deg", Vector3{10, 10, 0}).Value().panel;
    const auto solved = SolveDelta(panel, Vector3{1.0, 0.0, 0.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(solved.Value().x, 100.0 * std::cos(kPi / 6.0), 1e-9, "du");
    RequireNear(solved.Value().y, 100.0 * std::sin(kPi / 6.0), 1e-9, "dv");
}

KACHA_V2_TEST(cursor, duとdvをロックすると形が決まる)
{
    CursorInputPanel panel = BeginLine();
    panel = TypeAndEnter(panel, "du", "40", Vector3{1, 1, 0}).Value().panel;
    panel = TypeAndEnter(panel, "dv", "30", Vector3{1, 1, 0}).Value().panel;
    const auto solved = SolveDelta(panel, Vector3{999.0, -999.0, 0.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(solved.Value().x, 40.0, 1e-9, "du");
    RequireNear(solved.Value().y, 30.0, 1e-9, "dv");
}

KACHA_V2_TEST(cursor, 長さとduから残りを解く)
{
    CursorInputPanel panel = BeginLine();
    panel = TypeAndEnter(panel, "length", "50", Vector3{1, 1, 0}).Value().panel;
    panel = TypeAndEnter(panel, "du", "30", Vector3{1, 1, 0}).Value().panel;
    const auto solved = SolveDelta(panel, Vector3{1.0, 1.0, 0.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(solved.Value().y, 40.0, 1e-9, "dv は40");
    // カーソルが下側にあれば下向きに解く。
    const auto below = SolveDelta(panel, Vector3{1.0, -1.0, 0.0});
    Require(below.HasValue(), "決まる");
    RequireNear(below.Value().y, -40.0, 1e-9, "下向き");
}

KACHA_V2_TEST(cursor, 短すぎる長さは必要な値を言って断る)
{
    CursorInputPanel panel = BeginLine();
    panel = TypeAndEnter(panel, "du", "80", Vector3{1, 1, 0}).Value().panel;
    const auto result = TypeAndEnter(panel, "length", "50", Vector3{1, 1, 0});
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C005", "長さが足りない");
    Require(result.Diagnostics().front().detailsJa.find("80") != std::string::npos,
        "必要な値を言う");
}

KACHA_V2_TEST(cursor, 過剰拘束で矛盾すれば最後の変更だけ確定しない)
{
    CursorInputPanel panel = BeginLine();
    panel = TypeAndEnter(panel, "du", "30", Vector3{1, 1, 0}).Value().panel;
    panel = TypeAndEnter(panel, "dv", "40", Vector3{1, 1, 0}).Value().panel;
    // du と dv から長さは 50。ここで 90 を入れると矛盾する。
    const auto result = TypeAndEnter(panel, "length", "90", Vector3{1, 1, 0});
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C004", "矛盾");
    // 前の2欄は生きている。入力列ごと消さない。
    Require(panel.states[IndexOf(panel, "du")].locked, "du は残る");
    Require(panel.states[IndexOf(panel, "dv")].locked, "dv は残る");
    RequireNear(panel.states[IndexOf(panel, "du")].value, 30.0, 1e-9, "値も残る");
    // 合う値なら通る。
    const auto ok = TypeAndEnter(panel, "length", "50", Vector3{1, 1, 0});
    Require(ok.HasValue(), "合えば通る");
}

KACHA_V2_TEST(cursor, 円は半径から決まる)
{
    const auto begun = BeginCursorInput(DrawingTool::Circle, true);
    Require(begun.HasValue(), "出る");
    RequireEqual(begun.Value().fields[begun.Value().focusedIndex].id, "radius",
        "半径へ焦点");
    const auto locked = TypeAndEnter(begun.Value(), "radius", "12.5", Vector3{1, 0, 0});
    Require(locked.HasValue(), "確定できた");
    const auto solved = SolveDelta(locked.Value().panel, Vector3{100.0, 0.0, 0.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(std::hypot(solved.Value().x, solved.Value().y), 12.5, 1e-9, "半径12.5");
}

KACHA_V2_TEST(cursor, 直径から半径が決まる)
{
    const auto begun = BeginCursorInput(DrawingTool::Circle, true);
    const auto locked = TypeAndEnter(begun.Value(), "diameter", "50", Vector3{1, 0, 0});
    Require(locked.HasValue(), "確定できた");
    const auto solved = SolveDelta(locked.Value().panel, Vector3{7.0, 0.0, 0.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(std::hypot(solved.Value().x, solved.Value().y), 25.0, 1e-9, "半径25");
}

KACHA_V2_TEST(cursor, 半径が0以下なら断る)
{
    const auto begun = BeginCursorInput(DrawingTool::Circle, true);
    const auto result = TypeAndEnter(begun.Value(), "radius", "0", Vector3{1, 0, 0});
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C006", "半径");
}

KACHA_V2_TEST(cursor, 円弧は中心角と円弧長から半径が決まる)
{
    const auto begun = BeginCursorInput(DrawingTool::Arc, true);
    Require(begun.HasValue(), "出る");
    CursorInputPanel panel = begun.Value();
    panel = TypeAndEnter(panel, "sweep", "90deg", Vector3{1, 0, 0}).Value().panel;
    panel = TypeAndEnter(panel, "arc_length", "31.41592653589793", Vector3{1, 0, 0})
                .Value()
                .panel;
    const auto solved = SolveDelta(panel, Vector3{5.0, 0.0, 0.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(std::hypot(solved.Value().x, solved.Value().y), 20.0, 1e-6,
        "弧長 = 半径 x 中心角");
}

KACHA_V2_TEST(cursor, 3Dの直線は成分と長さから解ける)
{
    const auto begun = BeginCursorInput(DrawingTool::Line, false);
    Require(begun.HasValue(), "出る");
    CursorInputPanel panel = begun.Value();
    panel = TypeAndEnter(panel, "dx", "3", Vector3{1, 1, 1}).Value().panel;
    panel = TypeAndEnter(panel, "dy", "4", Vector3{1, 1, 1}).Value().panel;
    panel = TypeAndEnter(panel, "length", "13", Vector3{1, 1, 1}).Value().panel;
    const auto solved = SolveDelta(panel, Vector3{1.0, 1.0, 1.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(solved.Value().z, 12.0, 1e-9, "dz は12");
    RequireNear(solved.Value().Length(), 13.0, 1e-9, "長さ13");
}

KACHA_V2_TEST(cursor, 3Dの直線は軸との角度でも解ける)
{
    const auto begun = BeginCursorInput(DrawingTool::Line, false);
    CursorInputPanel panel = begun.Value();
    panel = TypeAndEnter(panel, "length", "100", Vector3{1, 0, 0}).Value().panel;
    panel = TypeAndEnter(panel, "angle_z", "60deg", Vector3{1, 0, 0}).Value().panel;
    const auto solved = SolveDelta(panel, Vector3{1.0, 0.0, 1.0});
    Require(solved.HasValue(), "決まる");
    RequireNear(solved.Value().z, 50.0, 1e-9, "100 x cos60 = 50");
}

KACHA_V2_TEST(cursor, 3Dでも矛盾すれば断る)
{
    const auto begun = BeginCursorInput(DrawingTool::Line, false);
    CursorInputPanel panel = begun.Value();
    panel = TypeAndEnter(panel, "dx", "3", Vector3{1, 1, 1}).Value().panel;
    panel = TypeAndEnter(panel, "dy", "4", Vector3{1, 1, 1}).Value().panel;
    panel = TypeAndEnter(panel, "dz", "12", Vector3{1, 1, 1}).Value().panel;
    const auto result = TypeAndEnter(panel, "length", "99", Vector3{1, 1, 1});
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C004", "矛盾");
}

KACHA_V2_TEST(cursor, Escで入力列ごと取り消す)
{
    CursorInputPanel panel = BeginLine();
    panel = TypeAndEnter(panel, "length", "100", Vector3{10, 10, 0}).Value().panel;
    const CursorInputPanel cancelled = CancelCursorInput(panel);
    Require(!cancelled.active, "消える");
    for (const CursorFieldState& state : cancelled.states) {
        Require(!state.locked && !state.hasValue && state.text.empty(), "値が残らない");
    }
    RequireEqual(cancelled.fields[cancelled.focusedIndex].id, "length",
        "次に出すときは主要欄から");
}

KACHA_V2_TEST(cursor, 入力列はカーソルの右下16pxに出る)
{
    const CursorPanelPlacement placement = PlaceCursorPanel(100.0, 100.0, 180.0, 90.0,
        1000.0, 800.0);
    RequireNear(placement.xPx, 116.0, 1e-9, "右へ16px");
    RequireNear(placement.yPx, 116.0, 1e-9, "下へ16px");
    Require(!placement.flippedHorizontally && !placement.flippedVertically, "そのまま");
}

KACHA_V2_TEST(cursor, 画面の右へはみ出すなら左へ寄せる)
{
    const CursorPanelPlacement placement = PlaceCursorPanel(950.0, 100.0, 180.0, 90.0,
        1000.0, 800.0);
    Require(placement.flippedHorizontally, "左へ寄せた");
    RequireNear(placement.xPx, 950.0 - 16.0 - 180.0, 1e-9, "カーソルの左");
    Require(placement.xPx + 180.0 <= 1000.0, "画面の中に入る");
    Require(!placement.flippedVertically, "縦はそのまま");
}

KACHA_V2_TEST(cursor, 画面の下へはみ出すなら上へ寄せる)
{
    const CursorPanelPlacement placement = PlaceCursorPanel(100.0, 760.0, 180.0, 90.0,
        1000.0, 800.0);
    Require(placement.flippedVertically, "上へ寄せた");
    RequireNear(placement.yPx, 760.0 - 16.0 - 90.0, 1e-9, "カーソルの上");
    Require(placement.yPx + 90.0 <= 800.0, "画面の中に入る");
}

KACHA_V2_TEST(cursor, 右下の角でも画面の中に収まる)
{
    const CursorPanelPlacement placement = PlaceCursorPanel(995.0, 795.0, 180.0, 90.0,
        1000.0, 800.0);
    Require(placement.flippedHorizontally && placement.flippedVertically, "両方寄せた");
    Require(placement.xPx >= 0.0 && placement.yPx >= 0.0, "画面の中");
    Require(placement.xPx + 180.0 <= 1000.0 && placement.yPx + 90.0 <= 800.0, "収まる");
}

KACHA_V2_TEST(cursor, 入力列より狭い画面でも画面外へ出さない)
{
    const CursorPanelPlacement placement = PlaceCursorPanel(50.0, 50.0, 400.0, 300.0,
        200.0, 150.0);
    RequireNear(placement.xPx, 0.0, 1e-9, "左端");
    RequireNear(placement.yPx, 0.0, 1e-9, "上端");
}

KACHA_V2_TEST(cursor, カーソルの位置が数値でなければ断る)
{
    const CursorInputPanel panel = BeginLine();
    const auto result = SolveDelta(panel, Vector3{std::nan(""), 0.0, 0.0});
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "UI-C007", "数値でない");
}

KACHA_V2_TEST_MAIN("cursor_input_tests")
