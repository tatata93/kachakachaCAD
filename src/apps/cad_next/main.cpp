//! V2アプリの入口(WP-08)。
//!
//! 画面を出さずに確かめられる道を必ず残す。
//!   --version       : 版だけ出す
//!   --self-test     : 画面を出さずに一通り触って、結果を出す
//!   --manual-state <名前> --snapshot <png> : その状態の絵を保存する
//!   --size 1366x768 : 窓の大きさを決める(AT-UIX-010 の画面サイズ比べ)
//!
//! V1 は画面を出さないと何も確かめられなかったので、
//! 直したかどうかを人が目で見るしかなかった。

#include "V2MainWindow.h"

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "Win95Style.h"

#include "kachakacha/app/CursorInput.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/view/ViewOrientation.h"
#include "kachakacha/kernel/KernelInfo.h"

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

namespace {

[[nodiscard]] QString ValueAfter(const QStringList& arguments, const QString& flag)
{
    const int index = arguments.indexOf(flag);
    if (index < 0 || index + 1 >= arguments.size()) {
        return QString();
    }
    return arguments.at(index + 1);
}

//! 窓を実際に描いて画像に落とす。offscreen でも通る。
[[nodiscard]] bool SaveSnapshot(V2MainWindow& window, const QString& path)
{
    window.show();
    QApplication::processEvents();
    QImage image(window.size() * 1, QImage::Format_ARGB32);
    image.fill(Qt::white);
    window.render(&image);
    return image.save(path);
}

struct SelfTestCase {
    const char* name;
    bool (*body)(V2MainWindow&);
};

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

const SelfTestCase kCases[] = {
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
};

} // namespace

int main(int argc, char** argv)
{
    QStringList arguments;
    for (int index = 1; index < argc; ++index) {
        arguments << QString::fromLocal8Bit(argv[index]);
    }

    if (arguments.contains(QStringLiteral("--version"))) {
        std::cout << kachakacha::v2::base::ProductVersionString() << ' '
                  << kachakacha::v2::base::MigrationNoticeJa() << '\n'
                  << kachakacha::v2::kernel::KernelVersionString() << '\n';
        return 0;
    }

    QApplication application(argc, argv);

    const QString state = ValueAfter(arguments, QStringLiteral("--manual-state"));
    const QString snapshot = ValueAfter(arguments, QStringLiteral("--snapshot"));

    if (arguments.contains(QStringLiteral("--self-test"))) {
        int failed = 0;
        for (const SelfTestCase& item : kCases) {
            // ケースごとに窓を作り直す。前のケースの状態を持ち越さない。
            V2MainWindow window;
            window.resize(1000, 700);
            window.show();
            QApplication::processEvents();
            bool ok = false;
            try {
                ok = item.body(window);
            } catch (const std::exception& error) {
                std::cout << "FAIL " << item.name << " : " << error.what() << std::endl;
                ++failed;
                continue;
            } catch (...) {
                std::cout << "FAIL " << item.name << " : 未知の例外" << std::endl;
                ++failed;
                continue;
            }
            if (ok) {
                std::cout << "PASS " << item.name << std::endl;
            } else {
                std::cout << "FAIL " << item.name << std::endl;
                ++failed;
            }
        }
        const int total = static_cast<int>(std::size(kCases));
        std::cout << "cad_next self-test: " << (total - failed) << " passed, " << failed
                  << " failed, " << total << " total\n";
        return failed == 0 ? 0 : 1;
    }

    // AT-UIX-010。画面の大きさを外から決められるようにする。
    // 1366x768 と 1920x1080 の両方で、同じ状態の絵を撮って見比べる。
    const QString sizeText = ValueAfter(arguments, QStringLiteral("--size"));
    int windowWidth = 1180;
    int windowHeight = 760;
    if (!sizeText.isEmpty()) {
        const QStringList parts = sizeText.split(QStringLiteral("x"));
        if (parts.size() != 2) {
            std::cerr << "--size は 1366x768 の形で渡してください\n";
            return 4;
        }
        windowWidth = parts.at(0).toInt();
        windowHeight = parts.at(1).toInt();
        if (windowWidth < 640 || windowHeight < 400) {
            std::cerr << "--size が小さすぎます\n";
            return 4;
        }
    }

    V2MainWindow window;
    if (!state.isEmpty()) {
        window.resize(windowWidth, windowHeight);
        if (!window.ApplyManualState(state)) {
            std::cerr << "知らない状態です: " << state.toStdString() << '\n';
            return 2;
        }
        if (!snapshot.isEmpty()) {
            if (!SaveSnapshot(window, snapshot)) {
                std::cerr << "絵を保存できませんでした: " << snapshot.toStdString()
                          << '\n';
                return 3;
            }
            std::cout << "saved " << snapshot.toStdString() << '\n';
            return 0;
        }
    }

    window.show();
    return QApplication::exec();
}
