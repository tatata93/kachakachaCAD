//! 道具の棚の共通の枠(C-10、HP-PF)。
//!
//! 正本(作図・部品・製作の 3 つの HTML)は、どの道具の棚も
//!   見出し + 案内 → 作り方 → 入力(対象) → 設定・オプション → 共通 → 状態 → キャンセル・確定
//! の並びで描いている。ここでは **実際に道具を押して出た棚** の、見えている節の見出しを
//! 上から順に読み、core の約束(app/PanelFrame)に通す。下にキャンセルと確定が見えていて
//! 押せることも見る。新しい道具の棚を足したときに並びが崩れたら、ここが鳴る(門)。

#include "V2SelfTest.h"

#include "V2DrawingDock.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Viewport.h"

#include "kachakacha/app/PanelFrame.h"

#include <QLabel>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

//! いまの棚の、見えている節の見出し(上から順)。
[[nodiscard]] std::vector<std::string> VisibleSectionTitles(QWidget* page)
{
    std::vector<std::pair<int, std::string>> found;
    if (page == nullptr) {
        return {};
    }
    for (QLabel* label : page->findChildren<QLabel*>()) {
        if (label->objectName() == QStringLiteral("panelSectionTitle") && label->isVisible()) {
            found.emplace_back(label->mapTo(page, QPoint(0, 0)).y(), label->text().toStdString());
        }
    }
    std::stable_sort(found.begin(), found.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> titles;
    for (auto& entry : found) {
        titles.push_back(std::move(entry.second));
    }
    return titles;
}

//! いまの棚に、見えていて押せる名前つきのボタンがあるか。
[[nodiscard]] bool HasUsableButton(QWidget* page, const char* objectName)
{
    if (page == nullptr) {
        return false;
    }
    for (QPushButton* button : page->findChildren<QPushButton*>()) {
        if (button->objectName() == QString::fromUtf8(objectName) && button->isVisible()) {
            return true;
        }
    }
    return false;
}

//! 棚 1 つぶんを確かめる。見出しは 2 つ以上、並びは約束どおり、キャンセルと確定が見える。
[[nodiscard]] bool CheckPanel(V2MainWindow& window, const char* toolJa)
{
    QWidget* page = window.OperationHost().CurrentPage();
    const auto titles = VisibleSectionTitles(page);
    std::string joined;
    for (const auto& title : titles) {
        joined += (joined.empty() ? "" : " / ") + title;
    }
    const std::string problem = kachakacha::v2::app::PanelSectionOrderProblemJa(titles);
    const std::string head = std::string(toolJa) + ": ";
    return Explain((head + "棚が出ている").c_str(), page != nullptr)
        && Explain((head + "見出しに棚の名前が出ている(" 
                       + window.OperationHost().CurrentShelfTitle().toStdString() + ")").c_str(),
            window.OperationHost().CurrentShelfTitle() != QStringLiteral("現在の操作"))
        && Explain((head + "節の見出しが 2 つ以上見える(" + joined + ")").c_str(), titles.size() >= 2)
        && Explain((head + "節の並びが約束どおり(" + joined + ") " + problem).c_str(),
            problem.empty())
        && Explain((head + "キャンセルが見える").c_str(), HasUsableButton(page, "panelCancel"))
        && Explain((head + "確定が見える").c_str(), HasUsableButton(page, "panelConfirm"));
}

//! HP-PF-01。選ばなくても開く道具の棚は、どれも共通の枠。
[[nodiscard]] bool CaseToolPanelsShareTheFrame(V2MainWindow& window)
{
    struct Tool {
        const char* command;
        const char* nameJa;
    };
    const Tool tools[] = {
        {"draw.polyline", "ポリライン"},
        {"draw.arc", "円弧"},
        {"part.extrude", "押し出し"},
        {"surface.create", "面を作る"},
        {"surface.refit", "面の作り直し"},
        {"part.thicken", "厚み"},
        {"part.boolean_add", "足す"},
        {"part.revolve", "回転体"},
        {"part.loft_solid", "ロフト立体"},
        {"part.fillet", "フィレット"},
        {"wire.chamfer", "C面取り"},
        {"fabrication.create", "近似"},
    };
    for (const Tool& tool : tools) {
        window.RunCommand("file.new");
        window.RunCommand(tool.command);
        const bool ok = CheckPanel(window, tool.nameJa);
        (void)window.HandleToolKey(Qt::Key_Escape, nullptr);
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        if (!ok) {
            return false;
        }
    }
    return true;
}

//! HP-PF-02。選んでから開く道具(配列)も同じ枠。
[[nodiscard]] bool CaseSelectionToolPanelsShareTheFrame(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.SetArrayChooser(nullptr);   // 窓の差し替えが残っていれば棚の道を通らない
    window.RunCommand("wire.array_linear");
    const bool ok = CheckPanel(window, "配列");
    (void)window.HandleToolKey(Qt::Key_Escape, nullptr);
    return ok;
}

//! HP-PF-03。作図の棚の「共通」のスナップは状態行の Snap と同じ値で、キャンセル・確定は
//! 3D の Esc・Enter と同じ道(マウスだけで折れ線を締められる)。
[[nodiscard]] bool CaseDrawingPanelCommonAndActionsAreReal(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.RunCommand("draw.polyline");
    auto& dock = window.DrawingDock();
    const bool snapBefore = window.SnapEnabled();
    if (!Explain("スナップの欄は状態行の値と同じ", dock.SnapChecked() == snapBefore)
        || !Explain("スナップの欄を押せる", dock.ClickSnap())
        || !Explain("押すと状態行の Snap も変わる", window.SnapEnabled() != snapBefore)) {
        return false;
    }
    window.RunCommand("snap.toggle");   // 窓の側から戻すと、棚の欄も戻る
    if (!Explain("状態行から戻すと棚の欄も戻る",
            window.SnapEnabled() == snapBefore && dock.SnapChecked() == snapBefore)) {
        return false;
    }
    // 3 点を打って「確定」を押すと、折れ線が 1 本できる(Enter と同じ)。
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, kachakacha::v2::domain::EntityKind::Wire);
    const QPointF points[3] = {{220.0, 220.0}, {320.0, 230.0}, {330.0, 320.0}};
    for (const QPointF& point : points) {
        viewport.ClickAt(point);
    }
    if (!Explain("確定を押せる", dock.ClickConfirm())) {
        return false;
    }
    const int after = CountOfKind(window, kachakacha::v2::domain::EntityKind::Wire);
    if (!Explain((std::string("確定で折れ線が 1 本できる(") + std::to_string(before) + " → "
                     + std::to_string(after) + ")").c_str(),
            after == before + 1)) {
        return false;
    }
    // 1 点だけ打って「キャンセル」を押すと、何も作らずにやめる(Esc と同じ)。
    window.RunCommand("draw.polyline");
    viewport.ClickAt(QPointF(240.0, 260.0));
    if (!Explain("キャンセルを押せる", dock.ClickCancel())) {
        return false;
    }
    return Explain("キャンセルでは何も作らない",
        CountOfKind(window, kachakacha::v2::domain::EntityKind::Wire) == after);
}

} // namespace

std::vector<SelfTestCase> PanelFrameCases()
{
    return {
        {"HP-PF-01 道具の棚は共通の枠(節の並びとキャンセル・確定)",
            CaseToolPanelsShareTheFrame},
        {"HP-PF-02 選んでから開く道具の棚も共通の枠", CaseSelectionToolPanelsShareTheFrame},
        {"HP-PF-03 作図の棚の共通とキャンセル・確定は本物", CaseDrawingPanelCommonAndActionsAreReal},
    };
}

} // namespace kachakacha::v2::selftest
