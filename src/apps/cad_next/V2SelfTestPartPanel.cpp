//! 部品モードの押し出しの欄(HP-PA)。正本 part mock(2026-09-18、Inventor 風)、指示書 P-02〜P-07。
//!
//! 範囲(距離/対称/非対称/面まで/貫通)と方向(7通り)が **棚に全部** 出ていて、選ぶと
//! 要る欄だけが生え、その値が作る形(ExtrudeChoice)へ届くこと。
//! 核に無い From とテーパーは押せない形で理由が出ていること。

#include "V2SelfTest.h"

#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ExtrudeInput.h"

#include <QString>

#include <cmath>
#include <variant>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;

//! 矩形を引き、拾って、押し出しを構えるところまで。
[[nodiscard]] bool ArmExtrude(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.SetMode(UiMode::Part);
    window.RunCommand("part.extrude");
    return Explain("押し出しの棚が構える",
        window.Viewport().ExtrudeHandleShown() && window.ShelfShown(Shelf::Extrude));
}

//! HP-PA-01。範囲 5 通り・方向 7 通りが棚に並び、選ぶと要る欄だけが生えて、作る形へ届く。
[[nodiscard]] bool CaseExtrudeShelfListsAllExtentsAndDirections(V2MainWindow& window)
{
    if (!ArmExtrude(window)) {
        return false;
    }
    auto& dock = window.ExtrudeDock();
    if (!Explain((std::string("範囲は 5 通り(実際 ") + std::to_string(dock.ExtentLabels().size()) + ")").c_str(),
            dock.ExtentLabels().size() == 5)
        || !Explain((std::string("方向は 7 通り(実際 ") + std::to_string(dock.DirectionLabels().size()) + ")").c_str(),
            dock.DirectionLabels().size() == 7)
        || !Explain("最初は距離で、逆側の距離も相手も出ていない",
            dock.ExtentMode() == ExtrudeExtentMode::Distance && !dock.SecondDistanceShown()
                && !dock.TargetRowShown())) {
        return false;
    }
    // 非対称(両方向に別々の距離)を選ぶと逆側の距離が生え、作る形へ届く。
    dock.ChooseExtent(ExtrudeExtentMode::TwoDistances);
    window.RefreshExtrudeFromDock();
    dock.SetSecondDistanceMm(3.0);
    window.RefreshExtrudeFromDock();
    if (!Explain("非対称では逆側の距離の欄が生える", dock.SecondDistanceShown())
        || !Explain("作る形の範囲が非対称", window.ExtrudeChoice().extent == ExtrudeExtentMode::TwoDistances)
        || !Explain("逆側の距離が届く", std::abs(window.ExtrudeChoice().secondDistanceMm - 3.0) < 1.0e-9)) {
        return false;
    }
    // 面までを選ぶと相手の欄が生える(作業平面が無ければその旨)。
    dock.ChooseExtent(ExtrudeExtentMode::ToTarget);
    window.RefreshExtrudeFromDock();
    if (!Explain("面までは相手の欄が生える", dock.TargetRowShown() && !dock.SecondDistanceShown())
        || !Explain("作る形の範囲が面まで", window.ExtrudeChoice().extent == ExtrudeExtentMode::ToTarget)) {
        return false;
    }
    // 数値で決める向きを選ぶと x y z の欄が生え、その値が向きになる。
    dock.ChooseExtent(ExtrudeExtentMode::Distance);
    dock.ChooseDirection(ExtrudeDirectionMode::CustomXYZ);
    dock.SetCustomDirection(kachakacha::v2::geometry::Vector3{0.0, 3.0, 4.0});
    window.RefreshExtrudeFromDock();
    const auto direction = window.ExtrudeDirectionNow();
    if (!Explain("作る形の向きが数値で決める", window.ExtrudeChoice().direction == ExtrudeDirectionMode::CustomXYZ)
        || !Explain((std::string("矢印が (0, 0.6, 0.8) を向く(y=") + std::to_string(direction.y) + " z="
                        + std::to_string(direction.z) + ")").c_str(),
            std::abs(direction.y - 0.6) < 1.0e-6 && std::abs(direction.z - 0.8) < 1.0e-6)) {
        return false;
    }
    // 核に無い欄は押せない形で理由が読める。
    if (!Explain("From は押せない形で理由がある", dock.FromBlockedReasonJa().contains(QStringLiteral("核")))
        || !Explain("テーパーは押せない形で理由がある", dock.TaperBlockedReasonJa().contains(QStringLiteral("核")))) {
        return false;
    }
    dock.ChooseDirection(ExtrudeDirectionMode::ProfileNormal);
    window.RefreshExtrudeFromDock();
    return Explain("Esc でやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))
        && Explain("棚が引っ込む", !window.ShelfShown(Shelf::Extrude));
}

//! HP-PA-02。左右対称を棚で選んで Enter すると、対称で作られる(下見と確定が同じ値)。
[[nodiscard]] bool CaseSymmetricExtentFromShelfReachesTheSolid(V2MainWindow& window)
{
    if (!ArmExtrude(window)) {
        return false;
    }
    auto& dock = window.ExtrudeDock();
    dock.ChooseExtent(ExtrudeExtentMode::SymmetricDistance);
    window.RefreshExtrudeFromDock();
    if (!Explain("作る形の範囲が左右対称",
            window.ExtrudeChoice().extent == ExtrudeExtentMode::SymmetricDistance)) {
        return false;
    }
    window.RunCommand("part.extrude");   // 確定
    if (!Explain((std::string("立体ができる(帯は ") + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, kachakacha::v2::domain::EntityKind::Part) == 1)) {
        return false;
    }
    bool symmetric = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition != nullptr) {
            symmetric = definition->extentMode
                == static_cast<int>(ExtrudeExtentMode::SymmetricDistance);
        }
    }
    return Explain("保存された作り方も左右対称", symmetric);
}

} // namespace

std::vector<SelfTestCase> PartPanelCases()
{
    return {
        {"HP-PA-01 押し出しの棚に範囲 5 通り・方向 7 通りが並び、要る欄だけ生えて作る形へ届く",
            CaseExtrudeShelfListsAllExtentsAndDirections},
        {"HP-PA-02 棚で選んだ左右対称が確定した立体に残る", CaseSymmetricExtentFromShelfReachesTheSolid},
    };
}

} // namespace kachakacha::v2::selftest
