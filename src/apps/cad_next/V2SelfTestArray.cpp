//! 配列(直線/円形)の棚(HP-AR、指示書 D-23)。
//!
//! `wire.array_linear` / `wire.array_circular` は、自己試験で窓(SetArrayChooser)を
//! 差し替えていなければ右の棚(Shelf::Array、V2ArrayDock)を出す。ここではその
//! 「棚で聞く」道そのものを、窓を出さずに確かめる。

#include "V2SelfTest.h"

#include "V2ArrayDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/geometry/Vector3.h"

#include <QPointF>
#include <QPushButton>
#include <QWidget>
#include <QString>

#include <string>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::domain::EntityKind;

//! 画面に線を1本引く。0,0 から 40,0 まで(V2SelfTestScreen.cpp の DrawOneLine と同じ形)。
void DrawOneLine(V2MainWindow& window)
{
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    auto& viewport = window.Viewport();
    const auto first = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{0.0, 0.0, 0.0});
    const auto second = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{40.0, 0.0, 0.0});
    if (!first.has_value() || !second.has_value()) {
        return;
    }
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
}

//! 線を1本引いて拾うところまで。配列の道具はこのあとで押す。
[[nodiscard]] bool DrawAndPickOneLine(V2MainWindow& window)
{
    window.RunCommand("file.new");
    // 窓の差し替えが残っていれば棚の道を通らない。ここは棚の道を確かめる。
    window.SetArrayChooser(nullptr);
    DrawOneLine(window);
    return Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier));
}

//! HP-AR-01。線を選んで直線配列を押すと棚が出る。個数を打って確定すると並ぶ。
//! 1回の取り消しで元に戻る(まとめて1つの操作)。
[[nodiscard]] bool CaseLinearArrayShelfPlacesCopies(V2MainWindow& window)
{
    if (!DrawAndPickOneLine(window)) {
        return false;
    }
    const int before = CountOfKind(window, EntityKind::Wire);
    window.RunCommand("wire.array_linear");
    if (!Explain("配列の棚が出る", window.ShelfShown(Shelf::Array))) {
        return false;
    }
    // 打ち込む=個数の欄へ 3 を入れる(自己試験の「打ち込む」相当)。
    window.ArrayDock().SetCountTyped(3);
    if (!Explain((std::string("個数が3になっている(実際は ")
                     + std::to_string(window.ArrayDock().Count()) + ")").c_str(),
            window.ArrayDock().Count() == 3)) {
        return false;
    }
    if (!Explain("確定を押せる", window.ArrayDock().ClickConfirm())) {
        return false;
    }
    const int after = CountOfKind(window, EntityKind::Wire);
    if (!Explain((std::string("線が3本になる(元 ") + std::to_string(before) + " 本 → "
                     + std::to_string(after) + " 本)").c_str(),
            after == before + 2)) {
        return false;
    }
    if (!Explain("確定後は棚が片付く", !window.ShelfShown(Shelf::Array))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain((std::string("1回の取り消しで元の本数へ戻る(実際 ")
                       + std::to_string(CountOfKind(window, EntityKind::Wire)) + " 本)").c_str(),
        CountOfKind(window, EntityKind::Wire) == before);
}

//! HP-AR-02。Esc で棚を引っ込め、何も並べない。
[[nodiscard]] bool CaseArrayShelfEscCancels(V2MainWindow& window)
{
    if (!DrawAndPickOneLine(window)) {
        return false;
    }
    const int before = CountOfKind(window, EntityKind::Wire);
    window.RunCommand("wire.array_linear");
    if (!Explain("配列の棚が出る", window.ShelfShown(Shelf::Array))) {
        return false;
    }
    window.ArrayDock().SetCountTyped(5);
    if (!Explain("Esc を受け取る", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    if (!Explain("配列の棚が残る", window.ShelfShown(Shelf::Array))) {
        return false;
    }
    return Explain((std::string("本数は変わらない(実際 ")
                       + std::to_string(CountOfKind(window, EntityKind::Wire)) + " 本)").c_str(),
        CountOfKind(window, EntityKind::Wire) == before);
}

bool CaseArrayToolFirstRole(V2MainWindow& window)
{
    window.RunCommand("file.new");window.SetArrayChooser(nullptr);DrawOneLine(window);
    window.Viewport().SetSelection({});window.RunCommand("wire.array_linear");
    if(!Explain("対象なしで配列ツールを開ける",window.ShelfShown(Shelf::Array)))return false;
    auto* role=window.findChild<QWidget*>("arraySelectionRoles");
    auto* pick=role?role->findChild<QPushButton*>("selectionRolePick0"):nullptr;
    if(!Explain("配列の対象選択ボタンがある",pick&&pick->isVisible()))return false;
    pick->click();
    if(!Explain("対象だけをクリックで追加できる",ClickOnAnyCurve(window,Qt::NoModifier)
        &&window.Viewport().Selection().entityIds.size()==1))return false;
    window.ArrayDock().SetCountTyped(2);if(!window.ArrayDock().ClickConfirm())return false;
    return Explain("対象を後から選んで配列を確定できる",CountOfKind(window,EntityKind::Wire)==2);
}

bool CaseCornerRolesRetainOther(V2MainWindow& window)
{
    window.RunCommand("file.new");DrawOneLine(window);DrawOneLine(window);
    std::vector<base::EntityId> ids;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)
        if(entity.kind==EntityKind::Wire)ids.push_back(entity.id);
    if(ids.size()!=2)return false;
    window.Viewport().SetSelection({});window.RunCommand("wire.fillet");
    auto* role=window.findChild<QWidget*>("cornerSelectionRoles");if(!role)return false;
    auto* a=role->findChild<QPushButton*>("selectionRolePick0");
    auto* b=role->findChild<QPushButton*>("selectionRolePick1");
    auto* clear=role->findChild<QPushButton*>("selectionRoleClear0");
    if(!a||!b||!clear||!a->isVisible())return false;
    b->click();app::SelectionSet selected;selected.entityIds={ids[1]};window.Viewport().SetSelection(selected);
    a->click();selected.entityIds={ids[0]};window.Viewport().SetSelection(selected);
    if(!Explain("Bから選んでもA/Bの役割順を維持",window.Viewport().Selection().entityIds==ids))return false;
    clear->click();
    if(!Explain("Aの解除でBを保持",window.Viewport().Selection().entityIds==std::vector<base::EntityId>{ids[1]}))return false;
    a->click();selected.entityIds={ids[0]};window.Viewport().SetSelection(selected);
    return Explain("Aを選び直してもBを保持",window.Viewport().Selection().entityIds==ids);
}

} // namespace

std::vector<SelfTestCase> ArrayCases()
{
    return {
        {"HP-AR-03 tool first explicit source role",CaseArrayToolFirstRole},
        {"HP-CA-ROLE reverse order and clear retain other",CaseCornerRolesRetainOther},
        {"HP-AR-01 配列の棚で個数を決めて確定すると並び、1回の取り消しで戻る",
            CaseLinearArrayShelfPlacesCopies},
        {"HP-AR-02 配列の棚は Esc で何も作らずに引っ込む", CaseArrayShelfEscCancels},
    };
}

} // namespace kachakacha::v2::selftest
