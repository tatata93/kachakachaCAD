//! 足す・引くの人の道(HP-BO)。引継ぎ 2026-09-17 の 4。
//!
//! 足す/引くを押す → 土台待ち → 3D で部品を押すと土台に入り、自動で相手待ちへ →
//! 相手を押す → 実際に足し引きした稜線が下見に出る → Enter で確定、1回の取り消しで消える。
//! 土台・相手は 3D の札と棚の欄に別々に出て、「ここへ選ぶ」で選び直せる。
//!
//! 選ぶのは実際に拾う道(`SelectAt`)だけ。ID の注入も、見えない widget を叩くこともしない。

#include "V2SelfTest.h"

#include "V2BooleanDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/BooleanInputState.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/domain/Feature.h"

#include <QPointF>
#include <QString>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::BooleanSlot;
using kachakacha::v2::app::Shelf;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

//! 画面の割合で指した矩形を引き、その線を拾って押し出し、Enter で部品にする。
[[nodiscard]] EntityId MakeBoxByHand(V2MainWindow& window, double x0, double y0, double x1,
    double y1)
{
    const int before = CountOfKind(window, EntityKind::Part);
    const EntityId wire = DrawRectangleAtByHand(window, x0, y0, x1, y1);
    if (wire.IsNil() || !Explain("線を画面から拾える", ClickOnCurveOf(window, wire))) {
        return EntityId{};
    }
    window.RunCommand("part.extrude");
    if (!Explain("押し出しの下見が出る", window.Viewport().ExtrudeHandleShown())
        || !Explain("Enterで部品にできる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が1つ増える", CountOfKind(window, EntityKind::Part) == before + 1)) {
        return EntityId{};
    }
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part) {
            newest = entity.id;
        }
    }
    return newest;
}

//! その部品の塗りの真ん中を、上から素のクリックで押す。
[[nodiscard]] bool PressPart(V2MainWindow& window, const EntityId& id)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.FitToDocument();
    for (const auto& shape : viewport.ShapeViews()) {
        if (shape.surface || shape.mesh.Empty() || !(shape.entityId == id)) {
            continue;
        }
        const auto center = kachakacha::v2::geometry::Vector3{
            (shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5, shape.mesh.maximum.z};
        const auto screen = viewport.Mapping().Project(center);
        if (!screen.has_value()
            || !Explain("部品の塗りに当たり判定がある",
                viewport.PickShapeAt(QPointF(screen->x, screen->y)).has_value())) {
            return false;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return Explain("その部品が画面に出ている", false);
}

//! 重なる箱を2つ作り、選択を空にして「足す」を構えるところまで。
[[nodiscard]] bool ArmUnionOnTwoBoxes(V2MainWindow& window, EntityId* first, EntityId* second)
{
    window.RunCommand("file.new");
    *first = MakeBoxByHand(window, 0.25, 0.35, 0.45, 0.65);
    *second = MakeBoxByHand(window, 0.40, 0.35, 0.60, 0.65);   // 少し重なる
    if (first->IsNil() || second->IsNil()) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("part.boolean_add");
    return Explain("足すを押すと棚が構える",
               window.BooleanShelfShown() && window.ShelfShown(Shelf::Boolean))
        && Explain("構えただけでは部品は増えない", CountOfKind(window, EntityKind::Part) == 2)
        && Explain("土台待ちが見えている",
            window.BooleanDock().ActiveSlotShown() == BooleanSlot::Target
                && window.ToolFooterTextJa().contains(QStringLiteral("NEXT=TARGET")));
}

//! HP-BO-01。土台 → 相手へ自動で移り、札と欄に別々に出て、押し直すと外れる。
[[nodiscard]] bool CaseHumanPathBooleanSlotsFollowClicks(V2MainWindow& window)
{
    EntityId first;
    EntityId second;
    if (!ArmUnionOnTwoBoxes(window, &first, &second)
        || !Explain("土台を画面で押せる", PressPart(window, first))) {
        return false;
    }
    auto& dock = window.BooleanDock();
    if (!Explain("押したものが土台の欄に入る", window.BooleanInput().target == first)
        || !Explain("自動で相手待ちへ移る", dock.ActiveSlotShown() == BooleanSlot::Tool)
        || !Explain("一番下の一行が NEXT=TOOL",
            window.ToolFooterTextJa().contains(QStringLiteral("NEXT=TOOL")))) {
        return false;
    }
    bool sawTarget = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        sawTarget = sawTarget || label.text == QStringLiteral("TARGET");
    }
    if (!Explain("3D に TARGET の札が出る", sawTarget)
        || !Explain("相手を画面で押せる", PressPart(window, second))
        || !Explain("相手の欄に入る", window.BooleanInput().tools.size() == 1
                && window.BooleanInput().tools.front() == second)) {
        return false;
    }
    bool sawTool = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        sawTool = sawTool || label.text == QStringLiteral("TOOL");
    }
    const std::string footer = window.ToolFooterTextJa().toStdString();
    if (!Explain("3D に TOOL の札も出る", sawTool)
        || !Explain((std::string("一番下の一行に両方が出る(") + footer + ")").c_str(),
            footer.find("TARGET=") != std::string::npos
                && footer.find("TOOL=") != std::string::npos)
        || !Explain("欄には名前が別々に出る",
            dock.TargetTextJa() != QStringLiteral("(選んでいません)")
                && dock.ToolTextJa() != QStringLiteral("(選んでいません)")
                && dock.TargetTextJa() != dock.ToolTextJa())) {
        return false;
    }
    // 土台をもう一度押すと外れ、土台の欄が次のクリックを待つ。
    if (!Explain("土台をもう一度押せる", PressPart(window, first))
        || !Explain("押し直すと土台だけ外れる",
            window.BooleanInput().target.IsNil() && window.BooleanInput().tools.size() == 1
                && window.BooleanInput().tools.front() == second)
        || !Explain("土台待ちへ戻る", dock.ActiveSlotShown() == BooleanSlot::Target)) {
        return false;
    }
    // 「ここへ選ぶ」で相手を選び直す。見えているボタンを押す。
    if (!Explain("相手の「ここへ選ぶ」を押せる", dock.ClickActivate(BooleanSlot::Tool))
        || !Explain("次のクリックは相手へ", dock.ActiveSlotShown() == BooleanSlot::Tool)
        || !Explain("別の部品を押せる", PressPart(window, first))
        || !Explain("相手に足される(相手は何個でも)", window.BooleanInput().tools.size() == 2
                && window.BooleanInput().tools.back() == first)) {
        return false;
    }
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    return Explain("やめると構えが解ける", !window.BooleanShelfShown())
        && Explain("部品は2つのまま", CountOfKind(window, EntityKind::Part) == 2)
        && Explain("札も一行も消える",
            window.Viewport().ToolRoleLabels().empty() && window.ToolFooterTextJa().isEmpty());
}

//! HP-BO-02。両方入ると実際に足した稜線が下見に出て、Enter で 1回で戻せる部品になる。
[[nodiscard]] bool CaseHumanPathBooleanPreviewThenConfirm(V2MainWindow& window)
{
    EntityId first;
    EntityId second;
    if (!ArmUnionOnTwoBoxes(window, &first, &second)
        || !Explain("土台を画面で押せる", PressPart(window, first))
        || !Explain("下見はまだ出ない", window.Viewport().ToolPreview().empty())
        || !Explain("相手を画面で押せる", PressPart(window, second))) {
        return false;
    }
    auto& dock = window.BooleanDock();
    const std::string status = dock.StatusTextJa().toStdString();
    if (!Explain((std::string("実際に足した結果が状態に出る(") + status + ")").c_str(),
            status.find("生成可能") != std::string::npos
                && status.find("mm3") != std::string::npos)
        || !Explain("下見が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("下見の間、部品は増えない", CountOfKind(window, EntityKind::Part) == 2)
        || !Explain("一番下の一行は Preview only",
            window.ToolFooterTextJa().contains(QStringLiteral("Preview only")))) {
        return false;
    }
    const std::uint64_t before = window.Session().GetDocument().Revision();
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("足した部品が1つできる(元の2つは隠れる)",
            CountOfKind(window, EntityKind::Part) == 3)
        || !Explain("確定すると構えが解ける", !window.BooleanShelfShown())
        || !Explain("文書が変わっている", window.Session().GetDocument().Revision() != before)) {
        return false;
    }
    int visibleParts = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++visibleParts;
        }
    }
    if (!Explain("見えている部品は足した1つだけ", visibleParts == 1)) {
        return false;
    }
    window.RunCommand("edit.undo");
    visibleParts = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++visibleParts;
        }
    }
    return Explain("1回の取り消しで元の2つに戻る(隠したのも戻る)",
        CountOfKind(window, EntityKind::Part) == 2 && visibleParts == 2);
}

//! 見えている部品の数。
[[nodiscard]] int VisiblePartCount(V2MainWindow& window)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++count;
        }
    }
    return count;
}

//! HP-BO-03。交差(P-17): 重なる 2 つの箱の共通部分だけを残す部品になり、1 回で戻り、
//! 保存して開き直しても作り直せる(文書には交差として残る)。
[[nodiscard]] bool CaseHumanPathIntersectKeepsOnlyTheOverlap(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = MakeBoxByHand(window, 0.25, 0.35, 0.45, 0.65);
    const EntityId second = MakeBoxByHand(window, 0.40, 0.35, 0.60, 0.65);   // 少し重なる
    if (first.IsNil() || second.IsNil()) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("part.boolean_intersect");
    auto& dock = window.BooleanDock();
    if (!Explain("交差を押すと棚が構える", window.BooleanShelfShown())
        || !Explain("棚の作り方は交差", dock.KindShown() == kachakacha::v2::app::BooleanKind::Intersect)
        || !Explain("土台を画面で押せる", PressPart(window, first))
        || !Explain("相手を画面で押せる", PressPart(window, second))) {
        return false;
    }
    const std::string status = dock.StatusTextJa().toStdString();
    if (!Explain((std::string("交差した結果が状態に出る(") + status + ")").c_str(),
            status.find("生成可能") != std::string::npos)
        || !Explain("一番下の一行は交差",
            window.ToolFooterTextJa().startsWith(QStringLiteral("交差")))
        || !Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("交差の部品が1つできる(元の2つは隠れる)",
            CountOfKind(window, EntityKind::Part) == 3 && VisiblePartCount(window) == 1)) {
        return false;
    }
    bool recorded = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        if (const auto* boolean =
                std::get_if<kachakacha::v2::domain::BooleanDefinition>(&feature.definition)) {
            recorded = recorded || boolean->mode == 2;
        }
    }
    if (!Explain("文書には交差(mode 2)として残る", recorded)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("1回の取り消しで元の2つに戻る",
            CountOfKind(window, EntityKind::Part) == 2 && VisiblePartCount(window) == 2)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("やり直すと交差の部品に戻る", VisiblePartCount(window) == 1)
        || !Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_intersect.kcd2")))) {
        return false;
    }
    // 開き直したあと、見えている部品(交差の結果)に形がある = 交差として作り直せた。
    kachakacha::v2::app::SelectionSet visible;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            visible.entityIds.push_back(entity.id);
        }
    }
    window.Viewport().SetSelection(visible);
    return Explain("開き直しても交差の部品は形があり、書き出せる",
        visible.entityIds.size() == 1 && window.CanExportSelectedParts());
}

} // namespace

std::vector<SelfTestCase> HumanPathBooleanCases()
{
    return {
        {"HP-BO-01 足す引くは土台→相手へ自動で移り、札と欄に別々に出て押し直すと外れる",
            CaseHumanPathBooleanSlotsFollowClicks},
        {"HP-BO-02 両方入ると実際の結果が下見に出て、Enter で 1回で戻せる部品になる",
            CaseHumanPathBooleanPreviewThenConfirm},
        {"HP-BO-03 交差は重なりだけを残す部品になり1回で戻り開き直しても作り直せる",
            CaseHumanPathIntersectKeepsOnlyTheOverlap},
    };
}

} // namespace kachakacha::v2::selftest
