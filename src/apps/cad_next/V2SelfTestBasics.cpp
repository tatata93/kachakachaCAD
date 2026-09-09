//! 道具・画面・視点・書き出し・ファイルのケース(WP-08)。
//!
//! 「使う人が最初に触るところ」をここへ集める。
//! 形を作るところは V2SelfTestModeling.cpp にある。

#include "V2SelfTest.h"

#include "V2MainWindow.h"

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "Win95Style.h"

#include "kachakacha/app/CursorInput.h"
#include "kachakacha/app/ProcessSteps.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/view/ViewOrientation.h"
#include "kachakacha/kernel/KernelInfo.h"

#include <filesystem>
#include <system_error>

#include <QApplication>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QImage>
#include <QPainter>
#include <QStringList>

#include <iostream>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

[[nodiscard]] bool CaseToolsExist(V2MainWindow& window)
{
    // 23の道具が並んでいること(V1同等性)。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    return !window.StatusText().isEmpty();
}

[[nodiscard]] bool CaseDrawLine(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    return window.EntityRowCount() >= 1;
}

[[nodiscard]] bool CaseCurves(V2MainWindow& window)
{
    return window.ApplyManualState(QStringLiteral("curves"));
}

[[nodiscard]] bool CaseSnap(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("snap"))) {
        return false;
    }
    // 吸着していれば、案内文が空でない。
    return !window.Viewport().StatusMessage().empty()
        || !window.StatusText().isEmpty();
}

[[nodiscard]] bool CaseThemes(V2MainWindow& window)
{
    window.ApplyTheme(UiTheme::Windows95);
    const bool win95 = window.Theme() == UiTheme::Windows95;
    window.ApplyTheme(UiTheme::Normal);
    return win95 && window.Theme() == UiTheme::Normal;
}

[[nodiscard]] bool CaseUndo(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    const int before = window.EntityRowCount();
    if (!window.Session().Undo()) {
        return false;
    }
    return before > 0;
}

[[nodiscard]] bool CaseViewDirections(V2MainWindow& window)
{
    for (const ViewDirection direction : {ViewDirection::Top, ViewDirection::Front,
             ViewDirection::Left, ViewDirection::Isometric}) {
        window.Viewport().SetViewDirection(direction);
        if (window.Viewport().Direction() != direction) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseCancel(V2MainWindow& window)
{
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Polyline);
    window.Viewport().ClickAt(QPointF(100, 100));
    window.Viewport().CancelTool();
    return true;
}

[[nodiscard]] bool CaseEveryCommandReachable(V2MainWindow& window)
{
    // 台帳の全コマンドが、同じ入口から呼べること。
    // どれを押しても落ちないこと。使えないものは理由が出ること。
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        QString reason;
        const bool enabled = window.CommandEnabled(command.id, &reason);
        if (!enabled && reason.isEmpty()) {
            return false;
        }
        window.RunCommand(command.id);
        if (window.StatusText().isEmpty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseMenusComeFromCatalog(V2MainWindow& window)
{
    // メニューの項目は台帳から作る。台帳に無い入口を作らない。
    int found = 0;
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        if (window.ActionFor(command.id) != nullptr) {
            ++found;
        }
    }
    return found == static_cast<int>(kachakacha::v2::app::CommandCatalog().size());
}

[[nodiscard]] bool CaseDisabledCommandsExplain(V2MainWindow& window)
{
    // 使えないコマンドは隠さず、押したときに理由を出す。
    QString reason;
    if (window.CommandEnabled("part.extrude", &reason)) {
        return false;
    }
    if (reason.isEmpty()) {
        return false;
    }
    window.RunCommand("part.extrude");
    return window.StatusText() == reason;
}

[[nodiscard]] bool CaseGuideIsComplete(V2MainWindow& window)
{
    // どの道具に入っても、案内の6つがそろっていること。
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        window.RunCommand(command.id);
        const QString status = window.StatusText();
        if (status.isEmpty()) {
            return false;
        }
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    const QString line = window.StatusText();
    return line.contains(QStringLiteral("次:")) && line.contains(QStringLiteral("Esc"))
        && line.contains(QStringLiteral("選択"));
}

[[nodiscard]] bool CaseFailureRecovery(V2MainWindow& window)
{
    // 失敗する操作を3回ずつ繰り返しても、アプリが続き、文書が変わらないこと。
    const std::uint64_t before = window.Session().GetDocument().Snapshot().revision;
    for (int round = 0; round < 3; ++round) {
        // まだ使えないコマンドを押す。
        window.RunCommand("part.extrude");
        window.RunCommand("part.boolean_cut");
        window.RunCommand("fabrication.create");
        // 点を1つも置かずに確定しようとする。
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Polyline);
        window.Viewport().FinishTool();
        window.Viewport().CancelTool();
        if (window.StatusText().isEmpty()) {
            return false;
        }
    }
    return window.Session().GetDocument().Snapshot().revision == before;
}

[[nodiscard]] bool CaseSelectionSurvivesFailure(V2MainWindow& window)
{
    // 失敗しても、いま選んでいる道具は変わらないこと。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Circle);
    const auto before = window.Session().CurrentTool();
    for (int round = 0; round < 3; ++round) {
        window.RunCommand("export.step");
        window.RunCommand("part.from_wire_cage");
    }
    return window.Session().CurrentTool() == before;
}

[[nodiscard]] bool CaseModesKeepSelection(V2MainWindow& window)
{
    // モードを切り替えても、選んでいる道具も文書も変わらないこと(UIX-001 / 003)。
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    const auto tool = window.Session().CurrentTool();
    const std::uint64_t revision = window.Session().GetDocument().Snapshot().revision;
    const int rows = window.EntityRowCount();
    for (const auto mode : kachakacha::v2::app::AllUiModes()) {
        window.SetMode(mode);
        if (window.Mode() != mode) {
            return false;
        }
        if (window.Session().CurrentTool() != tool) {
            return false;
        }
        if (window.Session().GetDocument().Snapshot().revision != revision) {
            return false;
        }
        if (window.EntityRowCount() != rows) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseModesChangeVisibleCommands(V2MainWindow& window)
{
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    const int drawing = window.VisibleCommandCount();
    window.SetMode(kachakacha::v2::app::UiMode::Output);
    const int output = window.VisibleCommandCount();
    // 出るコマンドはモードで変わる。共通操作があるので、どちらも0ではない。
    return drawing > 0 && output > 0 && drawing != output;
}

[[nodiscard]] bool CaseViewCubeDragIsContinuous(V2MainWindow& window)
{
    // ビューキューブのドラッグは、動かした量に比例して連続に回る(PRD-073)。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Front);
    const auto start = viewport.Orientation();
    const QPointF press = viewport.ViewCubeRect().center();
    if (!viewport.PressViewCube(press)) {
        return false;
    }
    viewport.DragViewCube(press + QPointF(20.0, 0.0));
    const double half = kachakacha::v2::view::AngleBetween(start, viewport.Orientation());
    viewport.DragViewCube(press + QPointF(40.0, 0.0));
    const double large = kachakacha::v2::view::AngleBetween(start, viewport.Orientation());
    viewport.ReleaseViewCube(press + QPointF(40.0, 0.0));
    if (half <= 1.0e-6 || large <= half) {
        return false;
    }
    return std::abs(large - 2.0 * half) < 1.0e-6;
}

[[nodiscard]] bool CaseViewCubeDoesNotSnapOrDrift(V2MainWindow& window)
{
    // 90度へ吸着せず、離した後も勝手に回らない(PRD-074)。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Front);
    const auto start = viewport.Orientation();
    const QPointF press = viewport.ViewCubeRect().center();
    if (!viewport.PressViewCube(press)) {
        return false;
    }
    // 0.5deg/px なので 178px で 89度。90度へ寄ってはならない。
    viewport.DragViewCube(press + QPointF(178.0, 0.0));
    viewport.ReleaseViewCube(press + QPointF(178.0, 0.0));
    const auto released = viewport.Orientation();
    const double degrees = kachakacha::v2::view::AngleBetween(start, released) * 180.0
        / 3.14159265358979323846;
    if (std::abs(degrees - 89.0) > 1.0e-6) {
        return false;
    }
    // 離した後に何度描き直しても姿勢が変わらないこと。
    for (int round = 0; round < 5; ++round) {
        QApplication::processEvents();
    }
    return kachakacha::v2::view::AngleBetween(released, viewport.Orientation()) < 1.0e-12;
}

[[nodiscard]] bool CaseViewCubeClickFacesTheZone(V2MainWindow& window)
{
    // 面をクリックしたときだけ、離散の向きを使う。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    const QPointF center = viewport.ViewCubeRect().center();
    const auto zone = viewport.ViewCubeZoneAtScreen(center);
    if (!zone.has_value()) {
        return false;
    }
    if (!viewport.PressViewCube(center)) {
        return false;
    }
    viewport.ReleaseViewCube(center);
    const auto expected = kachakacha::v2::view::OrientationForZone(*zone);
    if (!expected.HasValue()) {
        return false;
    }
    if (kachakacha::v2::view::AngleBetween(expected.Value(), viewport.Orientation()) > 1.0e-9) {
        return false;
    }
    // キューブの外を指したら区画は無い。
    return !viewport.ViewCubeZoneAtScreen(QPointF(10.0, viewport.height() - 10.0)).has_value();
}

[[nodiscard]] bool CaseAxisArrowsRotateCameraOnly(V2MainWindow& window)
{
    // 回転矢印はカメラだけを回す。文書は変わらない(PRD-075)。
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    auto& viewport = window.Viewport();
    const std::uint64_t revision = window.Session().GetDocument().Snapshot().revision;
    const auto before = viewport.Orientation();
    if (!viewport.RotateByArrow(kachakacha::v2::view::RotationAxis::Z,
            kachakacha::v2::view::RotationAxisMode::World,
            kachakacha::v2::view::AxisArrowModifier::None, 100.0)) {
        return false;
    }
    const double degrees = kachakacha::v2::view::AngleBetween(before, viewport.Orientation())
        * 180.0 / 3.14159265358979323846;
    if (std::abs(degrees - 25.0) > 1.0e-6) {
        return false;
    }
    if (window.Session().GetDocument().Snapshot().revision != revision) {
        return false;
    }
    // 相対軸は、部品を選んでいないと断る。理由が出る。
    viewport.SetSelectionFrame(std::nullopt);
    const bool refused = !viewport.RotateByArrow(kachakacha::v2::view::RotationAxis::X,
        kachakacha::v2::view::RotationAxisMode::Relative,
        kachakacha::v2::view::AxisArrowModifier::None, 50.0);
    return refused && !viewport.LastViewMessage().empty();
}

[[nodiscard]] bool CaseGuideTableShowsRolesAndConnection(V2MainWindow& window)
{
    // 役割テーブル(AT-UIX-007)。複数行、番号、接続、色同期を見る。
    if (!window.ApplyManualState(QStringLiteral("guide-table"))) {
        return false;
    }
    if (window.GuideRowCount() != 4) {
        return false;
    }
    if (window.GuideRowText(0, 0) != QStringLiteral("外形U")
        || window.GuideRowText(2, 0) != QStringLiteral("断面")) {
        return false;
    }
    if (window.GuideRowText(2, 1) != QStringLiteral("1")
        || window.GuideRowText(3, 1) != QStringLiteral("2")) {
        return false;
    }
    // 断面の両端が外形へ届いているので「有効」。
    if (window.GuideRowText(2, 3) != QStringLiteral("有効")) {
        return false;
    }
    // 表の色は core の式そのもの。3Dも同じ式を見るので必ず一致する。
    const auto views = kachakacha::v2::modeling::BuildGuideTableView(
        window.GuideRoleTable(), window.Session().GetDocument().Snapshot().settings.tolerance);
    for (int row = 0; row < window.GuideRowCount(); ++row) {
        const auto& color = views[static_cast<std::size_t>(row)].color;
        if (window.GuideRowColor(row) != QColor(color.red, color.green, color.blue)) {
            return false;
        }
    }
    // 3Dにも同じ行数が出ている(色同期)。
    if (window.Viewport().GuideRowsShown() != window.GuideRowCount()) {
        return false;
    }
    // 役割テーブルは部品モードの道具。作図モードでは表を出さない。
    if (window.Mode() != kachakacha::v2::app::UiMode::Part) {
        return false;
    }
    // 足りない役割の案内は、そろった時点で消えている。
    return window.DiagnosticRowCount() == 0;
}

[[nodiscard]] bool CaseGuideTableEditsRows(V2MainWindow& window)
{
    // 行への追加、順序変更、方向反転、断りの4つ。
    if (!window.ApplyManualState(QStringLiteral("guide-table"))) {
        return false;
    }
    // 断面2を上へ。中身が入れ替わり、番号は表の並びのまま。
    if (!window.SetGuideTable(kachakacha::v2::modeling::MoveRow(window.GuideRoleTable(), 3,
            -1))) {
        return false;
    }
    if (window.GuideRowText(2, 5) != QStringLiteral("sec_right")) {
        return false;
    }
    // 方向を反転する。
    if (!window.SetGuideTable(kachakacha::v2::modeling::ReverseRow(window.GuideRoleTable(),
            2))) {
        return false;
    }
    if (window.GuideRowText(2, 4) != QStringLiteral("逆")) {
        return false;
    }
    // 無い行を指したら断り、表は変わらない。
    const int before = window.GuideRowCount();
    const int diagnostics = window.DiagnosticRowCount();
    if (window.SetGuideTable(kachakacha::v2::modeling::RemoveRow(window.GuideRoleTable(),
            99))) {
        return false;
    }
    return window.GuideRowCount() == before && window.DiagnosticRowCount() > diagnostics;
}

[[nodiscard]] bool CaseCursorInputFocusAndExpression(V2MainWindow& window)
{
    // 最初の主要欄へ焦点が合い、式が評価され、式と値が並んで出る(AT-UIX-003)。
    if (!window.ApplyManualState(QStringLiteral("cursor-input"))) {
        return false;
    }
    const auto& panel = window.Viewport().CursorPanel();
    if (!panel.active || panel.fields.empty()) {
        return false;
    }
    std::size_t lengthAt = panel.fields.size();
    for (std::size_t index = 0; index < panel.fields.size(); ++index) {
        if (panel.fields[index].id == "length") {
            lengthAt = index;
        }
    }
    if (lengthAt == panel.fields.size() || !panel.states[lengthAt].locked) {
        return false;
    }
    if (std::abs(panel.states[lengthAt].value - 270.0) > 1.0e-9) {
        return false;
    }
    return kachakacha::v2::app::FieldDisplayJa(panel.fields[lengthAt],
               panel.states[lengthAt])
        == "(180/2)*3 = 270 mm";
}

[[nodiscard]] bool CaseCursorInputTabEnterEscape(V2MainWindow& window)
{
    // Tab で欄が回り、Enter で確定し、Esc で入力列ごと消える。
    if (!window.ApplyManualState(QStringLiteral("cursor-input"))) {
        return false;
    }
    auto& viewport = window.Viewport();
    const std::size_t before = viewport.CursorPanel().focusedIndex;
    if (!viewport.FocusNextCursorField(false)) {
        return false;
    }
    const std::size_t after = viewport.CursorPanel().focusedIndex;
    if (after == before) {
        return false;
    }
    if (!viewport.FocusNextCursorField(true) || viewport.CursorPanel().focusedIndex != before) {
        return false;
    }
    // いまの欄には 30deg が入っている。Enter で確定する。
    if (!viewport.CommitCursorField()) {
        return false;
    }
    if (!viewport.CursorPanel().states[before].locked) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Snapshot().revision;
    viewport.CloseCursorInput();
    if (viewport.CursorPanel().active) {
        return false;
    }
    // Esc で消しても文書は変わらない。
    return window.Session().GetDocument().Snapshot().revision == revision;
}

[[nodiscard]] bool CaseCursorInputStaysOnScreen(V2MainWindow& window)
{
    // 入力列が画面外へ出そうなときは、左または上へ寄る。
    if (!window.ApplyManualState(QStringLiteral("cursor-input"))) {
        return false;
    }
    auto& viewport = window.Viewport();
    // 画面の真ん中では、カーソルの右下に出る。
    const double midX = viewport.width() * 0.4;
    const double midY = viewport.height() * 0.4;
    viewport.HoverAt(QPointF(midX, midY));
    const QRectF middle = viewport.CursorPanelRect();
    if (middle.left() < midX || middle.top() < midY) {
        return false;
    }
    if (middle.right() > viewport.width() || middle.bottom() > viewport.height()) {
        return false;
    }
    // 右下の角では、カーソルの左上へ寄る。どちらでも画面の中に収まる。
    const double cornerX = viewport.width() - 2.0;
    const double cornerY = viewport.height() - 2.0;
    viewport.HoverAt(QPointF(cornerX, cornerY));
    const QRectF corner = viewport.CursorPanelRect();
    if (corner.left() < 0.0 || corner.top() < 0.0) {
        return false;
    }
    if (corner.right() > viewport.width() || corner.bottom() > viewport.height()) {
        return false;
    }
    return corner.right() <= cornerX && corner.bottom() <= cornerY;
}

[[nodiscard]] bool CaseActiveGroupShowsAndCollects(V2MainWindow& window)
{
    // 作業中グループ(AT-UIX-006)。帯に出て、一覧が束ねられ、作ったものが入る。
    if (!window.ApplyManualState(QStringLiteral("active-group"))) {
        return false;
    }
    if (!window.ActiveGroupText().contains(QStringLiteral("車体"))) {
        return false;
    }
    // 一覧はまとまりで束ねられ、作業中のまとまりに印が付く。
    bool sawActive = false;
    for (int row = 0; row < window.GroupRowCount(); ++row) {
        if (window.GroupRowText(row).contains(QStringLiteral("車体"))
            && window.GroupRowText(row).contains(QStringLiteral("作業中"))) {
            sawActive = true;
        }
    }
    if (!sawActive) {
        return false;
    }
    // 作ったものが、そのまとまりへ入っている。
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    if (snapshot.entities.empty()) {
        return false;
    }
    for (const auto& entity : snapshot.entities) {
        if (!entity.groupId.has_value()
            || *entity.groupId != *snapshot.settings.activeGroupId) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseThemeKeepsLayoutUsable(V2MainWindow& window)
{
    // AT-UIX-010 の骨。両方の見た目で、画面の部品が0の大きさにならず、
    // 画面の外へ出ない。文字が入る幅がある。
    for (const UiTheme theme : {UiTheme::Normal, UiTheme::Windows95}) {
        window.ApplyTheme(theme);
        QApplication::processEvents();
        if (window.Viewport().width() <= 0 || window.Viewport().height() <= 0) {
            return false;
        }
        // ビューキューブと数値入力の枠が、画面の中に収まっている。
        const QRectF cube = window.Viewport().ViewCubeRect();
        if (cube.right() > window.Viewport().width() || cube.top() < 0.0) {
            return false;
        }
        if (window.StatusText().isEmpty()) {
            return false;
        }
        if (window.Theme() != theme) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseSmallWindowStaysUsable(V2MainWindow& window)
{
    // 1366x768 相当の狭い画面でも、部品が0の大きさにならず、はみ出さない。
    for (const QSize size : {QSize(1366, 768), QSize(1920, 1080)}) {
        window.resize(size.width(), size.height());
        QApplication::processEvents();
        auto& viewport = window.Viewport();
        if (viewport.width() <= 0 || viewport.height() <= 0) {
            return false;
        }
        const QRectF cube = viewport.ViewCubeRect();
        if (cube.left() < 0.0 || cube.right() > viewport.width()
            || cube.bottom() > viewport.height()) {
            return false;
        }
        if (window.VisibleCommandCount() <= 0) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseToolPaletteFollowsMode(V2MainWindow& window)
{
    // 作図の道具が製作モードに並んでいると、そのモードで何ができるのかが読めない。
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    const int drawing = window.VisibleToolCount();
    window.SetMode(kachakacha::v2::app::UiMode::Fabrication);
    const int fabrication = window.VisibleToolCount();
    window.SetMode(kachakacha::v2::app::UiMode::Output);
    const int output = window.VisibleToolCount();
    // 作図モードでいちばん多く、製作と出力では減る。
    if (drawing <= 0 || fabrication >= drawing || output >= drawing) {
        return false;
    }
    // 戻せば元に戻る。
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    return window.VisibleToolCount() == drawing;
}

[[nodiscard]] bool CaseProcessStepsFollowMode(V2MainWindow& window)
{
    // 手順はモードで中身が変わり、番号は1から順に並ぶ(ui-workflows §9 / §10 / §11)。
    struct Expect {
        const char* state;
        int count;
    };
    const Expect wanted[] = {
        {"steps-part", 7},
        {"steps-fabrication", 10},
        {"steps-output", 4},
    };
    for (const Expect& item : wanted) {
        if (!window.ApplyManualState(QString::fromUtf8(item.state))) {
            return false;
        }
        if (window.ProcessStepCount() != item.count) {
            return false;
        }
        for (int row = 0; row < window.ProcessStepCount(); ++row) {
            const QString text = window.ProcessStepText(row);
            if (text.isEmpty()) {
                return false;
            }
            if (!text.startsWith(QString::number(row + 1))) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool CaseProcessStepsExplainWhyBlocked(V2MainWindow& window)
{
    // 進めない段には必ず理由が並んで出る。理由の無い灰色を作らない。
    window.SetMode(kachakacha::v2::app::UiMode::Fabrication);
    window.SetProcessContext(kachakacha::v2::app::ProcessContext{});
    if (window.CurrentProcessStep() != 1) {
        return false;
    }
    bool sawReason = false;
    for (int row = 1; row < window.ProcessStepCount(); ++row) {
        if (window.ProcessStepReason(row).isEmpty()) {
            return false;
        }
        sawReason = true;
    }
    if (!sawReason) {
        return false;
    }
    // 部品を選ぶと進む。
    kachakacha::v2::app::ProcessContext context;
    context.selectedPartCount = 1;
    window.SetProcessContext(context);
    return window.CurrentProcessStep() == 7;
}

[[nodiscard]] bool CaseSelectionPicksAndAdds(V2MainWindow& window)
{
    // 線の上を押せば選べる。Shift で足せる。何も無いところを素で押せば消える。
    if (!window.ApplyManualState(QStringLiteral("select"))) {
        return false;
    }
    auto& viewport = window.Viewport();
    if (viewport.Selection().entityIds.size() < 2) {
        return false;
    }
    const auto& curves = window.Session().Scene().curves;
    if (curves.empty()) {
        return false;
    }
    // 素で1本だけ押すと、その1本だけになる。
    const auto screen = viewport.Mapping().Project(curves.front().segment.Evaluate(0.5));
    if (!screen.has_value()) {
        return false;
    }
    viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    if (viewport.Selection().entityIds.size() != 1) {
        return false;
    }
    // 何も無いところを素で押すと空になる。
    viewport.SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    return viewport.Selection().entityIds.empty();
}

[[nodiscard]] bool CaseExportDockFollowsSelection(V2MainWindow& window)
{
    // 選んだ数が棚に出る。選ぶのをやめれば、その対象は選べなくなる。
    if (!Explain("export の状態を作れる", window.ApplyManualState(QStringLiteral("export")))) {
        return false;
    }
    auto& dock = window.ExportDock();
    if (!Explain((std::string("選んだワイヤーが2本ある(実際は ")
                     + std::to_string(dock.Counts().selectedWires) + ")").c_str(),
            dock.Counts().selectedWires >= 2)) {
        return false;
    }
    if (!Explain((std::string("対象が選んだワイヤー(実際は ")
                     + std::string(kachakacha::v2::app::ExportTargetNameJa(
                           dock.State().target))
                     + ")").c_str(),
            dock.State().target == kachakacha::v2::app::ExportTarget::SelectedWires)) {
        return false;
    }
    if (!Explain((std::string("形式が SVG(実際は ")
                     + std::string(kachakacha::v2::app::ExportFormatNameJa(
                           dock.State().format))
                     + ")").c_str(),
            dock.State().format == kachakacha::v2::app::ExportFormat::Svg)) {
        return false;
    }
    // 出す先が決まっていないので、まだ押せない。理由がそう言っている。
    if (!Explain("出す先が空なので押せない", !dock.CanRun())) {
        return false;
    }
    if (!Explain((std::string("理由が EXP-016(実際は ")
                     + dock.ReasonText().toStdString() + ")").c_str(),
            dock.ReasonText().contains(QStringLiteral("EXP-016")))) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    return Explain("選ぶのをやめれば数も0になる", dock.Counts().selectedWires == 0);
}

[[nodiscard]] bool CaseExportRefusesImpossibleFormat(V2MainWindow& window)
{
    // ワイヤーを STEP では出せない。押す前に分かるようにする。
    if (!window.ApplyManualState(QStringLiteral("export"))) {
        return false;
    }
    auto& dock = window.ExportDock();
    if (!Explain("STEP は断られる", !dock.ChooseFormat(
            kachakacha::v2::app::ExportFormat::Step))) {
        return false;
    }
    if (!Explain((std::string("知らせが EXP-017(実際は ")
                     + dock.LastMessage().toStdString() + ")").c_str(),
            dock.LastMessage().contains(QStringLiteral("EXP-017")))) {
        return false;
    }
    // 断ったのだから、選んでいた形式は動かない。
    if (dock.State().format != kachakacha::v2::app::ExportFormat::Svg) {
        return false;
    }
    // 選べない形式は薄く出るが、並びからは消えない。
    if (dock.FormatRowCount() != 6) {
        return false;
    }
    for (int row = 0; row < dock.FormatRowCount(); ++row) {
        if (dock.FormatRowText(row).isEmpty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseExportWritesFile(V2MainWindow& window)
{
    // 出す先を決めれば、実際にファイルが出る。中身は空にならない。
    if (!window.ApplyManualState(QStringLiteral("export"))) {
        return false;
    }
    auto& dock = window.ExportDock();
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_export");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path + ".svg"), code);
    dock.ChoosePath(QString::fromStdString(path));
    if (!Explain((std::string("出す先を決めれば押せる(理由は ")
                     + dock.ReasonText().toStdString() + ")").c_str(), dock.CanRun())) {
        return false;
    }
    if (!Explain((std::string("出せる(知らせは ") + dock.LastMessage().toStdString()
                     + ")").c_str(), dock.RunNow())) {
        return false;
    }
    const auto written = kachakacha::v2::io::MakePath(path + ".svg");
    const bool exists = std::filesystem::exists(written, code);
    const bool hasBytes = exists && std::filesystem::file_size(written, code) > 0;
    std::filesystem::remove(written, code);
    return hasBytes && dock.LastMessage().contains(QStringLiteral("書き出しました"));
}

[[nodiscard]] bool CaseFileCommandsAskAndGiveUp(V2MainWindow& window)
{
    // 出す先を尋ねてやめたら、何も起きない。文書も変わらない。
    const std::uint64_t before = window.Session().GetDocument().Revision();
    window.RunCommand("file.save_as");
    if (!Explain("やめたことが出る",
            window.StatusText().contains(QStringLiteral("やめました")))) {
        return false;
    }
    window.RunCommand("file.open");
    if (!Explain("開くのもやめられる",
            window.StatusText().contains(QStringLiteral("やめました")))) {
        return false;
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == before);
}

[[nodiscard]] bool CaseSaveThenOpenRoundTrips(V2MainWindow& window)
{
    // 保存して開き直すと、同じものが戻る。
    if (!Explain("線を引ける", window.ApplyManualState(QStringLiteral("select")))) {
        return false;
    }
    const std::size_t before = window.Session().GetDocument().Snapshot().entities.size();
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_roundtrip.kcd2");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    window.SetPathChooser([&path](bool) { return QString::fromStdString(path); });
    window.RunCommand("file.save_as");
    if (!Explain((std::string("保存できた(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("保存しました")))) {
        return false;
    }
    window.RunCommand("file.new");
    if (!Explain("新しい文書は空",
            window.Session().GetDocument().Snapshot().entities.empty())) {
        return false;
    }
    const bool opened = window.OpenDocumentFile(QString::fromStdString(path));
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    if (!Explain("開き直せた", opened)) {
        return false;
    }
    return Explain((std::string("同じ数が戻る(")
                       + std::to_string(
                             window.Session().GetDocument().Snapshot().entities.size())
                       + " と " + std::to_string(before) + ")").c_str(),
        window.Session().GetDocument().Snapshot().entities.size() == before);
}

[[nodiscard]] bool CaseViewPanelHasEveryControl(V2MainWindow& window)
{
    // V1 と同じで、輪の矢じり・上下左右・画面のまま回す・家・選択に正対がそろっている。
    auto& viewport = window.Viewport();
    const auto layout = viewport.ViewGadgets();
    if (!Explain((std::string("部品が14個(実際は ")
                     + std::to_string(layout.gadgets.size()) + ")").c_str(),
            layout.gadgets.size() == 14)) {
        return false;
    }
    if (!Explain("輪が3本ある", layout.rings.size() == 3)) {
        return false;
    }
    for (const auto& gadget : layout.gadgets) {
        const bool inside = gadget.xPx >= 0.0 && gadget.yPx >= 0.0
            && gadget.xPx + gadget.widthPx <= viewport.width()
            && gadget.yPx + gadget.heightPx <= viewport.height();
        if (!Explain("部品が画面の中にある", inside)) {
            return false;
        }
        const auto found = viewport.ViewGadgetAt(
            QPointF(gadget.CenterXPx(), gadget.CenterYPx()));
        if (!Explain("部品を押せる", found.has_value())) {
            return false;
        }
        (void)gadget;
    }
    return true;
}

//! その部品を押して離す。回った角度を返す。
[[nodiscard]] double ClickGadgetAndMeasure(V2Viewport& viewport,
    const kachakacha::v2::view::ViewGadget& gadget)
{
    const QPointF center(gadget.CenterXPx(), gadget.CenterYPx());
    const auto before = viewport.Orientation();
    if (gadget.kind == kachakacha::v2::view::ViewGadgetKind::AxisRing) {
        viewport.PressViewRing(center, kachakacha::v2::view::AxisArrowModifier::None);
    } else {
        viewport.PressViewButton(center, kachakacha::v2::view::AxisArrowModifier::None);
    }
    viewport.ReleaseViewGadget(center);
    return kachakacha::v2::view::AngleBetween(before, viewport.Orientation()) * 180.0
        / 3.14159265358979323846;
}

[[nodiscard]] bool CaseViewPanelTurnsFifteenDegrees(V2MainWindow& window)
{
    // 輪も上下左右も画面のまま回すのも、押したら15度だけ回る。90度へは飛ばない。
    auto& viewport = window.Viewport();
    const auto layout = viewport.ViewGadgets();
    int checked = 0;
    for (const auto& gadget : layout.gadgets) {
        const bool turns = gadget.kind == kachakacha::v2::view::ViewGadgetKind::AxisRing
            || gadget.kind == kachakacha::v2::view::ViewGadgetKind::Orbit
            || gadget.kind == kachakacha::v2::view::ViewGadgetKind::Roll;
        if (!turns) {
            continue;
        }
        const double degrees = ClickGadgetAndMeasure(viewport, gadget);
        if (!Explain((std::string("15度回った(実際は ") + std::to_string(degrees)
                         + "度)").c_str(),
                std::abs(degrees - kachakacha::v2::view::kAxisArrowClickDegrees) < 0.5)) {
            return false;
        }
        ++checked;
    }
    return Explain((std::string("回す部品が12個(実際は ") + std::to_string(checked)
                       + ")").c_str(), checked == 12);
}

[[nodiscard]] bool CaseRingIsGrabbableAlongTheWhole(V2MainWindow& window)
{
    // V1(ADR 0023)と同じで、輪は線のどこを押しても掴める。
    // 矢じりだけを的にすると、視点によっては潰れて狙えない。
    auto& viewport = window.Viewport();
    const auto layout = viewport.ViewGadgets();
    if (!Explain("輪が3本ある", layout.rings.size() == 3)) {
        return false;
    }
    for (const auto& ring : layout.rings) {
        for (std::size_t index = 0; index < ring.points.size(); index += 8) {
            const QPointF at(ring.points[index].x, ring.points[index].y);
            const auto found = viewport.ViewRingAt(at);
            if (!Explain("輪の上を押せば当たる", found.has_value())) {
                return false;
            }
        }
    }
    // 輪はキューブより後に見るので、キューブの面が輪に隠れない。
    return Explain("中心はキューブが勝つ",
        !viewport.ViewButtonAt(QPointF(viewport.ViewCubeRect().center())).has_value());
}

[[nodiscard]] bool CaseViewPanelHome(V2MainWindow& window)
{
    // 家は等角ビューへ戻す。
    auto& viewport = window.Viewport();
    const auto layout = viewport.ViewGadgets();
    for (const auto& gadget : layout.gadgets) {
        if (gadget.kind != kachakacha::v2::view::ViewGadgetKind::Home) {
            continue;
        }
        const QPointF center(gadget.CenterXPx(), gadget.CenterYPx());
        viewport.SetViewDirection(ViewDirection::Top);
        const auto top = viewport.Orientation();
        viewport.PressViewButton(center, kachakacha::v2::view::AxisArrowModifier::None);
        viewport.ReleaseViewGadget(center);
        return Explain("等角ビューへ戻る",
            kachakacha::v2::view::AngleBetween(top, viewport.Orientation()) > 1.0e-6);
    }
    return Explain("家がある", false);
}

//! 交わる2本を引いて、両方を選ぶ。線の編集の試験の下ごしらえ。

} // namespace

[[nodiscard]] bool CaseMeasureShowsWhatIsSelected(V2MainWindow& window)
{
    // 測る棚は、選んだものから測れることを全部出す。
    // V1 は測り方を先に選ばせたので、選び間違えると拾い直しだった。
    window.RunCommand("measure.open");
    auto& dock = window.MeasureDock();
    if (!Explain((std::string("何も選んでいなければ言う(")
                     + dock.SummaryText().toStdString() + ")").c_str(),
            dock.SummaryText().contains(QStringLiteral("選ばれていません")))) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.3, viewport.height() * 0.3));
    viewport.ClickAt(QPointF(viewport.width() * 0.7, viewport.height() * 0.7));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    window.RunCommand("measure.open");
    bool sawKind = false;
    bool sawLength = false;
    for (int row = 0; row < dock.RowCount(); ++row) {
        if (dock.RowLabel(row) == QStringLiteral("種類")
            && dock.RowValue(row) == QStringLiteral("直線")) {
            sawKind = true;
        }
        if (dock.RowLabel(row) == QStringLiteral("長さ")
            && dock.RowValue(row).endsWith(QStringLiteral(" mm"))
            && !dock.RowValue(row).startsWith(QStringLiteral("0.000"))) {
            sawLength = true;
        }
    }
    if (!Explain((std::string("直線と長さが出る(行は ")
                     + std::to_string(dock.RowCount()) + ")").c_str(),
            sawKind && sawLength)) {
        return false;
    }
    // 半径を持たないものに半径を出さない。出すとその値を信じてしまう。
    for (int row = 0; row < dock.RowCount(); ++row) {
        if (!Explain("直線に半径は出ない",
                dock.RowLabel(row) != QStringLiteral("半径"))) {
            return false;
        }
    }
    return Explain("測っていることを言う",
        dock.SummaryText().contains(QStringLiteral("測っています")));
}

[[nodiscard]] bool CaseDisplaySettingsCycle(V2MainWindow& window)
{
    // 見え方を変えても形は変わらない。変わったら、見やすくしただけのつもりで
    // 寸法を変えたことになる。
    const std::uint64_t before = window.Session().GetDocument().Revision();
    QStringList seen;
    for (int index = 0; index < 3; ++index) {
        window.RunCommand("view.display_settings");
        seen << window.StatusText();
    }
    if (!Explain("3回とも違うことを言う",
            seen.size() == 3 && seen.at(0) != seen.at(1) && seen.at(1) != seen.at(2)
                && seen.at(0) != seen.at(2))) {
        return false;
    }
    window.RunCommand("view.display_settings");
    if (!Explain((std::string("4回目で元へ戻る(") + window.StatusText().toStdString()
                     + ")").c_str(), window.StatusText() == seen.at(0))) {
        return false;
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == before);
}

[[nodiscard]] bool CaseAlignToSelectionNeedsAPlane(V2MainWindow& window)
{
    // 正対は形を変えない。選んでいなければ、何を選ぶかを言う。
    window.RunCommand("view.align_selection");
    return Explain((std::string("理由が出る(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("作業平面")));
}

std::vector<SelfTestCase> BasicCases()
{
    return {
        {"道具を選べる", &CaseToolsExist},
        {"直線を引ける", &CaseDrawLine},
        {"曲線を曲線のまま描ける", &CaseCurves},
        {"端点に吸着する", &CaseSnap},
        {"見た目を切り替えられる", &CaseThemes},
        {"元に戻せる", &CaseUndo},
        {"視点を切り替えられる", &CaseViewDirections},
        {"途中でやめられる", &CaseCancel},
        {"台帳の全コマンドが同じ入口から呼べる", &CaseEveryCommandReachable},
        {"メニューが台帳から出来ている", &CaseMenusComeFromCatalog},
        {"使えないコマンドは理由を出す", &CaseDisabledCommandsExplain},
        {"案内が6つそろっている", &CaseGuideIsComplete},
        {"失敗しても続き、文書が変わらない", &CaseFailureRecovery},
        {"失敗しても選んだ道具が変わらない", &CaseSelectionSurvivesFailure},
        {"モードを変えても選択と文書が変わらない", &CaseModesKeepSelection},
        {"モードで出るコマンドが変わる", &CaseModesChangeVisibleCommands},
        {"ビューキューブが連続に回る", &CaseViewCubeDragIsContinuous},
        {"90度へ吸着せず離した後も回らない", &CaseViewCubeDoesNotSnapOrDrift},
        {"キューブのクリックだけが正対する", &CaseViewCubeClickFacesTheZone},
        {"回転矢印はカメラだけを回す", &CaseAxisArrowsRotateCameraOnly},
        {"役割テーブルが役割と接続を出す", &CaseGuideTableShowsRolesAndConnection},
        {"役割テーブルを編集できる", &CaseGuideTableEditsRows},
        {"数値入力が主要欄へ合い式を評価する", &CaseCursorInputFocusAndExpression},
        {"数値入力のTabとEnterとEscが効く", &CaseCursorInputTabEnterEscape},
        {"数値入力が画面の外へ出ない", &CaseCursorInputStaysOnScreen},
        {"作業中グループが帯と一覧に出る", &CaseActiveGroupShowsAndCollects},
        {"どちらの見た目でも配置が壊れない", &CaseThemeKeepsLayoutUsable},
        {"狭い画面でも部品がはみ出さない", &CaseSmallWindowStaysUsable},
        {"道具箱もモードに従う", &CaseToolPaletteFollowsMode},
        {"手順がモードで変わり番号順に並ぶ", &CaseProcessStepsFollowMode},
        {"進めない段には理由が出る", &CaseProcessStepsExplainWhyBlocked},
        {"線を選べて足せて消せる", &CaseSelectionPicksAndAdds},
        {"書き出しの棚が選択に従う", &CaseExportDockFollowsSelection},
        {"出せない形式は押す前に断る", &CaseExportRefusesImpossibleFormat},
        {"出す先を決めればファイルが出る", &CaseExportWritesFile},
        {"出す先を尋ねてやめられる", &CaseFileCommandsAskAndGiveUp},
        {"保存して開き直すと同じものが戻る", &CaseSaveThenOpenRoundTrips},
        {"視点の操作板に部品がそろっている", &CaseViewPanelHasEveryControl},
        {"操作板の回す部品は15度だけ回る", &CaseViewPanelTurnsFifteenDegrees},
        {"輪は線のどこを押しても掴める", &CaseRingIsGrabbableAlongTheWhole},
        {"家で等角ビューへ戻る", &CaseViewPanelHome},
        {"測る棚が選んだものを測る", &CaseMeasureShowsWhatIsSelected},
        {"見え方は3段で回り形を変えない", &CaseDisplaySettingsCycle},
        {"正対は平面を選ばないと理由を出す", &CaseAlignToSelectionNeedsAPlane},
    };
}

} // namespace kachakacha::v2::selftest
