//! 立体を作るの人の道(HP-SO、matrix P-08/P-09)。
//!
//! 帯の 回転体 / ロフト立体 / スイープ を押す → 何も選んでいなくても棚が出る →
//! 3D で線を押すと種類で欄に入る(閉じた線 → 輪郭・断面、直線 → 回転軸、開いた線 → 経路)→
//! **実際に核で作った** 稜線が下見に出て、体積が状態に出る → Enter で部品になり、
//! 1 回の取り消しで消え、保存して開き直しても同じ形に作り直せる。
//!
//! 選ぶのは実際に拾う道(`SelectAt`)だけ。ID の注入も、見えない widget を叩くこともしない。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2SolidDock.h"
#include "V2SolidTool.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"

#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/SolidInputState.h"
#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <QPointF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {

//! 上面 XY から決めた距離だけ離した作業平面を作って使う(断面を高さ違いに描くため)。
bool UseTopPlaneOffsetBy(V2MainWindow& window, double offsetMm)
{
    window.SetWorkPlaneChooser({});
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    V2WorkPlaneDock* dock = window.WorkPlaneDock();
    if (dock == nullptr) {
        return false;
    }
    const auto top = kachakacha::v2::app::OriginPlaneId(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::modeling::StandardPlaneKind::XY);
    if (!top.has_value()) {
        return false;
    }
    kachakacha::v2::app::WorkPlaneChoice choice;
    choice.method = kachakacha::v2::modeling::WorkPlaneMethod::OffsetFromPlane;
    choice.referencePlaneId = top;
    choice.offsetMm = offsetMm;
    dock->SetChoice(choice);
    if (!dock->CanCreate()) {
        return false;
    }
    dock->PressCreate();
    return std::abs(window.Viewport().WorkPlane().origin.z - offsetMm) < 1.0e-6;
}

namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::SolidSlot;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;

constexpr double kPi = 3.14159265358979323846;

struct Box3 {
    Vector3 minimum;
    Vector3 maximum;
};

//! いちばん新しいその種類のもの。
[[nodiscard]] EntityId Newest(V2MainWindow& window, EntityKind kind)
{
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            newest = entity.id;
        }
    }
    return newest;
}

//! 上から見て、画面の割合で指した2点を直線で結ぶ(吸着なし)。
[[nodiscard]] EntityId DrawLineByHand(V2MainWindow& window, double x0, double y0, double x1,
    double y1)
{
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, EntityKind::Wire);
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * x0, viewport.height() * y0));
    viewport.HoverAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.ClickAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return CountOfKind(window, EntityKind::Wire) == before + 1 ? Newest(window, EntityKind::Wire)
                                                              : EntityId{};
}

//! その線の外接箱(場面の線から)。
[[nodiscard]] std::optional<Box3> WireBounds(V2MainWindow& window, const EntityId& id)
{
    std::optional<Box3> box;
    for (const auto& curve : window.Session().Scene().curves) {
        if (!(curve.entityId == id)) {
            continue;
        }
        for (int step = 0; step <= 8; ++step) {
            const Vector3 p = curve.segment.Evaluate(step / 8.0);
            if (!box.has_value()) {
                box = Box3{p, p};
            }
            box->minimum = Vector3{std::min(box->minimum.x, p.x), std::min(box->minimum.y, p.y),
                std::min(box->minimum.z, p.z)};
            box->maximum = Vector3{std::max(box->maximum.x, p.x), std::max(box->maximum.y, p.y),
                std::max(box->maximum.z, p.z)};
        }
    }
    return box;
}

//! 画面に出ている、その部品の外接箱。
[[nodiscard]] std::optional<Box3> PartBounds(V2MainWindow& window, const EntityId& id)
{
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (!shape.surface && shape.entityId == id && !shape.mesh.Empty()) {
            return Box3{shape.mesh.minimum, shape.mesh.maximum};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<EntityId> VisibleParts(V2MainWindow& window)
{
    std::vector<EntityId> parts;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            parts.push_back(entity.id);
        }
    }
    return parts;
}

[[nodiscard]] bool Near(double actual, double expected, double relative)
{
    return std::abs(actual - expected) <= std::abs(expected) * relative;
}

[[nodiscard]] bool HasRoleLabel(V2MainWindow& window, const QString& text)
{
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        if (label.text == text) {
            return true;
        }
    }
    return false;
}

//! 何も選ばずに道具を押す。棚が出て、その作り方の名前が見える。
[[nodiscard]] bool ArmSolid(V2MainWindow& window, const char* command, const char* nameJa)
{
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand(command);
    auto& dock = *window.SolidTool().Dock();
    const std::string shown = dock.ToolNameJa().toStdString();
    return Explain((std::string(nameJa) + " を押すと棚が構える").c_str(),
               window.SolidTool().Active() && window.ShelfShown(Shelf::Solid))
        && Explain((std::string("棚に作り方の名前が出る(") + shown + ")").c_str(), shown == nameJa)
        && Explain("最初は輪郭待ち", dock.ActiveSlotShown() == SolidSlot::Profiles
                && window.ToolFooterTextJa().contains(QStringLiteral("NEXT=PROFILE")));
}

//! 状態の文と、下見の体積。
[[nodiscard]] bool PreviewReady(V2MainWindow& window, const char* what)
{
    const std::string status = window.SolidTool().Dock()->StatusTextJa().toStdString();
    return Explain((std::string(what) + ": 実際に作った結果が状態に出る(" + status + ")").c_str(),
               status.find("生成可能") != std::string::npos && status.find("mm3") != std::string::npos)
        && Explain((std::string(what) + ": 下見が 3D に出ている").c_str(),
            !window.Viewport().ToolPreview().empty())
        && Explain((std::string(what) + ": 確定が押せる").c_str(),
            window.SolidTool().Dock()->ConfirmEnabled());
}

//! 回す角度を変えた体積が、全回転の体積に角度の割合を掛けたものと合う。
[[nodiscard]] bool RevolveModesScaleVolume(V2MainWindow& window, double fullVolume)
{
    auto& tool = window.SolidTool();
    auto& dock = *tool.Dock();
    if (!Explain("全回転では角度を打てない(360° で固定)", !dock.AngleEditable())
        || !Explain("角度指定のカードを押せる", dock.ClickMethodCard(1))
        || !Explain("角度指定では角度を打てる", dock.TypeAngle(90.0))) {
        return false;
    }
    const double quarter = tool.Outcome().volumeMm3;
    if (!Explain((std::string("90° は全回転の 4 分の 1(") + std::to_string(quarter) + " mm3)").c_str(),
            tool.Outcome().available && Near(quarter, fullVolume / 4.0, 0.005))
        || !Explain("対称回転のカードを押せる", dock.ClickMethodCard(2))
        || !Explain("対称回転でも角度はそのまま(体積は同じ)",
            tool.Outcome().available && Near(tool.Outcome().volumeMm3, fullVolume / 4.0, 0.005))
        || !Explain("一番下の一行に SYM が出る",
            window.ToolFooterTextJa().contains(QStringLiteral("SYM")))) {
        return false;
    }
    return Explain("全回転のカードへ戻せる", dock.ClickMethodCard(0))
        && Explain("全回転の体積に戻る", Near(tool.Outcome().volumeMm3, fullVolume, 1.0e-6));
}

//! HP-SO-01。回転体: 輪郭 → 軸へ自動で移り、体積は Pappus の予測と合い、角度の作り方で
//! 体積が割合どおりに変わり、Enter で部品になり、1 回で戻り、開き直しても同じ形。
[[nodiscard]] bool CaseRevolveByClicks(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId profile = DrawRectangleAtByHand(window, 0.60, 0.45, 0.70, 0.55);
    const EntityId axis = DrawLineByHand(window, 0.50, 0.25, 0.50, 0.75);
    const auto box = profile.IsNil() ? std::nullopt : WireBounds(window, profile);
    const auto line = axis.IsNil() ? std::nullopt : WireBounds(window, axis);
    if (!Explain("輪郭の矩形と軸の線を手で引ける", box.has_value() && line.has_value())
        || !ArmSolid(window, "part.revolve", "回転体")) {
        return false;
    }
    auto& tool = window.SolidTool();
    auto& dock = *tool.Dock();
    if (!Explain("輪郭を画面で押せる", ClickOnCurveOf(window, profile))
        || !Explain("閉じた線は輪郭に入る", tool.Input().profiles.size() == 1
                && tool.Input().profiles.front() == profile)
        || !Explain("自動で軸待ちへ移る", dock.ActiveSlotShown() == SolidSlot::Axis
                && window.ToolFooterTextJa().contains(QStringLiteral("NEXT=AXIS")))
        || !Explain("軸を画面で押せる", ClickOnCurveOf(window, axis))
        || !Explain("直線は回転軸に入る", tool.Input().axis == axis)
        || !Explain("3D に PROFILE と AXIS の札が出る",
            HasRoleLabel(window, QStringLiteral("PROFILE")) && HasRoleLabel(window, QStringLiteral("AXIS")))
        || !PreviewReady(window, "回転体")) {
        return false;
    }
    // Pappus: 2π × 面積 × 重心から軸までの距離。
    const double axisX = line->minimum.x;
    const double width = box->maximum.x - box->minimum.x;
    const double height = box->maximum.y - box->minimum.y;
    const double expected = 2.0 * kPi * width * height * ((box->minimum.x + box->maximum.x) * 0.5 - axisX);
    const double volume = tool.Outcome().volumeMm3;
    if (!Explain((std::string("体積が Pappus の予測と合う(") + std::to_string(volume) + " / "
                     + std::to_string(expected) + ")").c_str(),
            Near(volume, expected, 0.005))
        || !RevolveModesScaleVolume(window, volume)) {
        return false;
    }
    if (!Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が 1 つできる", CountOfKind(window, EntityKind::Part) == 1)
        || !Explain("確定すると構えが解ける", !tool.Active() && !window.ShelfShown(Shelf::Solid))) {
        return false;
    }
    const EntityId part = Newest(window, EntityKind::Part);
    const auto made = PartBounds(window, part);
    const double radius = box->maximum.x - axisX;
    if (!Explain("回した立体は軸のまわりに z にも広がる(外径 = 輪郭の遠い縁)",
            made.has_value() && Near(made->maximum.z, radius, 0.05)
                && Near(-made->minimum.z, radius, 0.05) && Near(made->maximum.x, box->maximum.x, 0.02))) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("1 回の取り消しで部品が消える", CountOfKind(window, EntityKind::Part) == 0)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_solid_revolve.kcd2")))) {
        return false;
    }
    const auto reopened = PartBounds(window, Newest(window, EntityKind::Part));
    return Explain("開き直しても同じ形に作り直せる", reopened.has_value()
            && Near(reopened->maximum.z, made->maximum.z, 1.0e-6)
            && Near(reopened->maximum.x, made->maximum.x, 1.0e-6));
}

//! 前から見た画面で、ZX 平面に x = 0 の縦の線を z = 0 から heightMm まで引く(経路)。
[[nodiscard]] EntityId DrawUprightPath(V2MainWindow& window, double heightMm)
{
    window.SetWorkPlaneChooser([](const kachakacha::v2::app::WorkPlaneChoice&,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        kachakacha::v2::app::WorkPlaneChoice choice;
        choice.method = kachakacha::v2::modeling::WorkPlaneMethod::Standard;
        choice.standard = kachakacha::v2::modeling::StandardPlaneKind::ZX;
        return std::optional<kachakacha::v2::app::WorkPlaneChoice>(choice);
    });
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    window.SetWorkPlaneChooser({});
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Front);
    viewport.SetViewCenter(Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    const auto from = viewport.Mapping().Project(Vector3{0.0, 0.0, 0.0});
    const auto to = viewport.Mapping().Project(Vector3{0.0, 0.0, heightMm});
    if (!from.has_value() || !to.has_value()) {
        return EntityId{};
    }
    const int before = CountOfKind(window, EntityKind::Wire);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(from->x, from->y));
    viewport.HoverAt(QPointF(to->x, to->y));
    viewport.ClickAt(QPointF(to->x, to->y));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return CountOfKind(window, EntityKind::Wire) == before + 1 ? Newest(window, EntityKind::Wire)
                                                              : EntityId{};
}

//! HP-SO-02。スイープ: 輪郭と経路を押すと、まっすぐな経路なら体積は 面積 × 長さ。
[[nodiscard]] bool CaseSweepAlongPath(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId profile = DrawRectangleAtByHand(window, 0.45, 0.45, 0.55, 0.55);
    const auto box = profile.IsNil() ? std::nullopt : WireBounds(window, profile);
    const EntityId path = box.has_value() ? DrawUprightPath(window, 40.0) : EntityId{};
    const auto rail = path.IsNil() ? std::nullopt : WireBounds(window, path);
    if (!Explain("輪郭の矩形と縦の経路を手で引ける", box.has_value() && rail.has_value())
        || !Explain((std::string("経路は輪郭の平面(z = 0)から 40 mm 上へ(") + std::to_string(rail->minimum.z)
                        + " .. " + std::to_string(rail->maximum.z) + ")").c_str(),
            std::abs(rail->minimum.z) < 1.0e-3 && std::abs(rail->maximum.z - 40.0) < 1.0e-3)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    if (!ArmSolid(window, "part.sweep", "スイープ")) {
        return false;
    }
    auto& tool = window.SolidTool();
    auto& dock = *tool.Dock();
    if (!Explain("回転軸の欄は無く経路の欄がある",
            dock.SlotRowShown(SolidSlot::Path) && !dock.SlotRowShown(SolidSlot::Axis))
        || !Explain("ねじれ指定は押せない形で理由を言う", !dock.MethodCardEnabled(1)
                && dock.MethodCardWhyJa(1).startsWith(QStringLiteral("まだ作れません"))
                && !dock.ClickMethodCard(1))
        || !Explain("輪郭を画面で押せる", ClickOnCurveOf(window, profile))
        || !Explain("経路を画面で押せる", ClickOnCurveOf(window, path))
        || !Explain("輪郭と経路が別々の欄に入る", tool.Input().profiles.size() == 1
                && tool.Input().path.size() == 1 && tool.Input().path.front() == path)
        || !PreviewReady(window, "スイープ")) {
        return false;
    }
    const double expected = (box->maximum.x - box->minimum.x) * (box->maximum.y - box->minimum.y) * 40.0;
    if (!Explain((std::string("体積は 面積 × 長さ(") + std::to_string(tool.Outcome().volumeMm3) + " / "
                     + std::to_string(expected) + ")").c_str(),
            Near(tool.Outcome().volumeMm3, expected, 0.005))
        || !Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が 1 つできる", CountOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    const auto made = PartBounds(window, Newest(window, EntityKind::Part));
    return Explain("立体は経路に沿って z = 0 から 40 まで", made.has_value()
            && std::abs(made->minimum.z) < 0.5 && std::abs(made->maximum.z - 40.0) < 0.5);
}

//! HP-SO-03。ロフト立体: 高さ違いの断面 2 つを押した順に通す。体積は 2 つの柱の間。
[[nodiscard]] bool CaseLoftThroughSections(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId low = DrawRectangleAtByHand(window, 0.40, 0.40, 0.60, 0.60);
    if (!Explain("下の断面を引ける", !low.IsNil())
        || !Explain("30mm 上の作業平面を使える", UseTopPlaneOffsetBy(window, 30.0))) {
        return false;
    }
    const EntityId high = DrawRectangleAtByHand(window, 0.45, 0.45, 0.55, 0.55);
    const auto lowBox = WireBounds(window, low);
    const auto highBox = high.IsNil() ? std::nullopt : WireBounds(window, high);
    if (!Explain("上の断面を引ける", lowBox.has_value() && highBox.has_value()
                && std::abs(highBox->minimum.z - 30.0) < 1.0e-3)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    if (!ArmSolid(window, "part.loft_solid", "ロフト立体")) {
        return false;
    }
    auto& tool = window.SolidTool();
    auto& dock = *tool.Dock();
    if (!Explain("断面のみのカードが押された形", dock.MethodCardChecked(0) && dock.MethodCardEnabled(0))
        || !Explain("ガイド付き・中心線付きは押せない形で理由を言う",
            !dock.MethodCardEnabled(1) && !dock.MethodCardEnabled(2) && !dock.ClickMethodCard(2)
                && dock.MethodCardWhyJa(1).startsWith(QStringLiteral("まだ作れません")))
        || !Explain("回転軸・経路の欄は出ない",
            !dock.SlotRowShown(SolidSlot::Axis) && !dock.SlotRowShown(SolidSlot::Path))
        || !Explain("断面を順に押せる", ClickOnCurveOf(window, low) && ClickOnCurveOf(window, high))
        || !Explain("押した順に断面へ入る", tool.Input().profiles.size() == 2
                && tool.Input().profiles[0] == low && tool.Input().profiles[1] == high)
        || !PreviewReady(window, "ロフト立体")) {
        return false;
    }
    const double lowArea = (lowBox->maximum.x - lowBox->minimum.x) * (lowBox->maximum.y - lowBox->minimum.y);
    const double highArea = (highBox->maximum.x - highBox->minimum.x) * (highBox->maximum.y - highBox->minimum.y);
    const double volume = tool.Outcome().volumeMm3;
    if (!Explain((std::string("体積は上の柱と下の柱の間(") + std::to_string(volume) + ")").c_str(),
            volume > highArea * 30.0 && volume < lowArea * 30.0)
        || !Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が 1 つできる", CountOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    const auto made = PartBounds(window, Newest(window, EntityKind::Part));
    return Explain("立体は下の断面から上の断面まで(z = 0 .. 30)", made.has_value()
            && std::abs(made->minimum.z) < 0.5 && std::abs(made->maximum.z - 30.0) < 0.5);
}

//! 上から見て、その部品の塗りの真ん中を素のクリックで押す。
[[nodiscard]] bool PressPart(V2MainWindow& window, const EntityId& id)
{
    auto& viewport = window.Viewport();
    const auto box = PartBounds(window, id);
    if (!box.has_value()) {
        return Explain("その部品が画面に出ている", false);
    }
    const auto screen = viewport.Mapping().Project(Vector3{(box->minimum.x + box->maximum.x) * 0.5,
        (box->minimum.y + box->maximum.y) * 0.5, box->maximum.z});
    if (!screen.has_value()) {
        return false;
    }
    viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    return true;
}

//! 箱を 1 つ作る(矩形を引いて押し出す)。
[[nodiscard]] EntityId MakeBox(V2MainWindow& window, double x0, double y0, double x1, double y1)
{
    const EntityId wire = DrawRectangleAtByHand(window, x0, y0, x1, y1);
    if (wire.IsNil() || !ClickOnCurveOf(window, wire)) {
        return EntityId{};
    }
    window.RunCommand("part.extrude");
    if (!window.HandleToolKey(Qt::Key_Return, nullptr)) {
        return EntityId{};
    }
    return Newest(window, EntityKind::Part);
}

//! HP-SO-04。軸が輪郭を横切れば理由つきで断り(確定できない)、輪郭を入れ替えると作れ、
//! 「足す」にすると相手の欄が出て、部品を押すと相手に入り、確定で相手は隠れる。1 回で戻る。
[[nodiscard]] bool CaseRevolveRefusesThenAdds(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId box = MakeBox(window, 0.325, 0.42, 0.375, 0.58);   // x = -35 .. -25
    const EntityId crossing = DrawRectangleAtByHand(window, 0.48, 0.46, 0.56, 0.54);   // x = -4 .. 12
    const EntityId ring = DrawRectangleAtByHand(window, 0.60, 0.45, 0.70, 0.55);   // x = 20 .. 40
    const EntityId axis = DrawLineByHand(window, 0.50, 0.25, 0.50, 0.75);   // x = 0
    if (!Explain("箱・矩形 2 つ・軸を手で作れる", !box.IsNil() && !crossing.IsNil() && !ring.IsNil()
                && !axis.IsNil())
        || !ArmSolid(window, "part.revolve", "回転体")
        || !Explain("軸を横切る輪郭と軸を押せる",
            ClickOnCurveOf(window, crossing) && ClickOnCurveOf(window, axis))) {
        return false;
    }
    auto& tool = window.SolidTool();
    auto& dock = *tool.Dock();
    const std::string status = dock.StatusTextJa().toStdString();
    if (!Explain((std::string("軸が輪郭の内側を通ると理由つきで断る(") + status + ")").c_str(),
            tool.Outcome().evaluated && !tool.Outcome().available
                && status.find("回転軸が輪郭の内側を通っています") != std::string::npos)
        || !Explain("断った間は確定が押せない", !dock.ConfirmEnabled() && !dock.ClickConfirm())
        || !Explain("Enter でも作らない", window.HandleToolKey(Qt::Key_Return, nullptr)
                && CountOfKind(window, EntityKind::Part) == 1 && tool.Active())
        || !Explain("横切る輪郭をもう一度押すと外れる", ClickOnCurveOf(window, crossing)
                && tool.Input().profiles.empty() && tool.Input().axis == axis)
        || !Explain("別の輪郭を押せる", ClickOnCurveOf(window, ring))
        || !PreviewReady(window, "入れ替えた輪郭")) {
        return false;
    }
    const double ringVolume = tool.Outcome().volumeMm3;
    if (!Explain("「足す」を押せる", dock.ClickBoolean(1))
        || !Explain("足すでは相手の欄が出て相手待ちになる", dock.SlotRowShown(SolidSlot::Target)
                && dock.ActiveSlotShown() == SolidSlot::Target
                && window.ToolFooterTextJa().contains(QStringLiteral("NEXT=TARGET")))
        || !Explain("相手が無いうちは確定できない", !dock.ConfirmEnabled())
        || !Explain("箱を画面で押せる", PressPart(window, box))
        || !Explain("部品は相手に入る", tool.Input().target == box)
        || !PreviewReady(window, "足す")
        || !Explain((std::string("足した体積は回転体より大きい(") + std::to_string(tool.Outcome().volumeMm3)
                        + " > " + std::to_string(ringVolume) + ")").c_str(),
            tool.Outcome().volumeMm3 > ringVolume + 1.0)
        || !Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    const auto visible = VisibleParts(window);
    if (!Explain("足した部品だけが見える(相手の箱は隠れる)",
            visible.size() == 1 && !(visible.front() == box)
                && CountOfKind(window, EntityKind::Part) == 2)) {
        return false;
    }
    window.RunCommand("edit.undo");
    const auto undone = VisibleParts(window);
    return Explain("1 回の取り消しで箱だけに戻る", undone.size() == 1 && undone.front() == box
            && CountOfKind(window, EntityKind::Part) == 1);
}

} // namespace

std::vector<SelfTestCase> SolidCases()
{
    return {
        {"HP-SO-01 回転体は輪郭と軸を押すと体積が予測と合い角度で割合どおり変わり1回で戻り開き直しても同じ",
            CaseRevolveByClicks},
        {"HP-SO-02 スイープは輪郭と経路を押すとまっすぐな経路で面積×長さの立体になる", CaseSweepAlongPath},
        {"HP-SO-03 ロフト立体は高さ違いの断面を押した順に通しガイド付きは理由つきで押せない",
            CaseLoftThroughSections},
        {"HP-SO-04 回転体は軸が輪郭を横切れば断り足すでは相手を押して確定で相手が隠れ1回で戻る",
            CaseRevolveRefusesThenAdds},
    };
}

} // namespace kachakacha::v2::selftest
