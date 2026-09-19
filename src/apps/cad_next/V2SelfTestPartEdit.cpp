//! 部材の編集を 3D のクリックから当てる(F-05/06/07、正本 fabrication mock「部材編集」)。
//!
//! これまでは「曲げる部材」の番号を必ず**手で打つ**しかなかった。ここは、
//! 3D で ApproxPart(近似モデルの元になった物体)を押すと「対象部材」欄に
//! その部材番号が入り、3D に PART の札が出ることを確かめる。
//!
//! 選ぶのは実際に拾う道(`SelectAt`)だけ。欄を直に書き換えて確かめない。

#include "V2SelfTest.h"

#include "V2FabricationDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Feature.h"

#include <QPointF>
#include <QString>

#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::domain::EntityKind;

//! 矩形から形状ガイドを1枚作り、近似(候補 A = 面ごとに展開)を確定するところまで。
//! HP-AP-02(V2SelfTestHumanPathApprox.cpp)と同じ道。平らな面なので必ず部材1枚になる。
[[nodiscard]] bool BuildOnePanelFabricationModel(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("境界を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!Explain("面の下見が見える", window.SurfacePreviewShown())
        || !Explain("Enterで面を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("形状ガイドができる", CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    viewport.SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("fabrication.create");
    if (!Explain("近似を押すと製作の棚が構える", window.ApproxShelfShown())
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    auto& dock = window.FabricationDock();
    // 候補 A(面ごとに展開)。平らな1枚は必ず1部材になる(HP-AP-02 と同じ見立て)。
    if (!Explain("候補 A のボタンを押せる", dock.ClickCandidate(0))) {
        return false;
    }
    return Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        && Explain("近似モデルが1つできる", window.FabricationModelCount() == 1)
        && Explain(("部材は1枚(実際 " + std::to_string(window.FabricationPanelCount()) + ")")
                       .c_str(),
            window.FabricationPanelCount() == 1);
}

//! HP-PE-01。3D で近似モデルの元になった面を押すと、「対象部材」欄がその番号になり、
//! 3D に PART の札が出る。
[[nodiscard]] bool CasePickPartInThreeDSetsPartNumber(V2MainWindow& window)
{
    if (!BuildOnePanelFabricationModel(window)) {
        return false;
    }
    // 製作モードのまま(fabrication.create が既に UiMode::Fabrication にしている)。
    if (!Explain("製作モードのまま", window.Mode() == UiMode::Fabrication)) {
        return false;
    }
    auto& dock = window.FabricationDock();
    dock.SetPartNumbersText(QString());   // 何も打っていない状態から始める。
    auto& viewport = window.Viewport();
    // 近似モデルの元になった面(形状ガイド)の真ん中を、素のクリックで押す。
    kachakacha::v2::geometry::Vector3 center{};
    bool found = false;
    for (const auto& shape : viewport.ShapeViews()) {
        if (!shape.surface || shape.mesh.Empty()) {
            continue;
        }
        center = kachakacha::v2::geometry::Vector3{
            (shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5,
            (shape.mesh.minimum.z + shape.mesh.maximum.z) * 0.5};
        found = true;
        break;
    }
    if (!Explain("面がまだ画面にある", found)) {
        return false;
    }
    const auto screen = viewport.Mapping().Project(center);
    if (!Explain("面を画面へ写せる", screen.has_value())) {
        return false;
    }
    viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    const std::string numberText = dock.PartNumbersText().toStdString();
    if (!Explain(("3D で押すと「対象部材」欄が1になる(実際 " + numberText + ")").c_str(),
            numberText == "1")) {
        return false;
    }
    const auto& labels = viewport.ToolRoleLabels();
    if (!Explain("3D に PART の札が出る",
            !labels.empty() && labels.front().text.contains(QStringLiteral("PART")))) {
        return false;
    }
    // 方式 / 最大誤差は、部材ごとの値ではないことを正直に言う。
    const std::string info = dock.PartInfoText().toStdString();
    return Explain(("「方式 / 最大誤差」がモデル全体だと言う(" + info + ")").c_str(),
        dock.PartInfoText().contains(QStringLiteral("モデル全体")));
}

} // namespace

std::vector<SelfTestCase> PartEditCases()
{
    return {
        {"HP-PE-01 3D で部材を押すと「対象部材」欄がその番号になり PART の札が出る",
            CasePickPartInThreeDSetsPartNumber},
    };
}

} // namespace kachakacha::v2::selftest
