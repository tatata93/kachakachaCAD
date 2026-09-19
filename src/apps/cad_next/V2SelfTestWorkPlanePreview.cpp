//! 作業平面の棚の下見(D-24)。HP-WP-01。
//!
//! 「平面を作る」を押すまで、右の棚で選んだ作り方がどんな平面になるのか
//! 3D では見えなかった。ここでは棚を出し、作り方と数を決めた時点で
//! 40mm四方の四角が3Dに出て、実際に作ったら消えることを見る。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"

#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <string>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneMethod;

//! 棚の道を使う。差し替えた答え(窓の代わり)を外す(V2SelfTestPlanes.cpp と同じ)。
void UseDock(V2MainWindow& window)
{
    window.SetWorkPlaneChooser({});
}

[[nodiscard]] bool CaseShelfPreviewShowsAndHidesOnCreate(V2MainWindow& window)
{
    UseDock(window);
    window.RunCommand("file.new");
    window.RunCommand("workplane.create");
    if (!Explain("作業平面の棚が見える(HP-WP-01)",
            window.ShelfShown(kachakacha::v2::app::Shelf::WorkPlane))) {
        return false;
    }
    V2WorkPlaneDock* dock = window.WorkPlaneDock();
    if (!Explain("棚がある", dock != nullptr)) {
        return false;
    }
    // 棚の初期値は「標準面」で、何も選ばなくても原点を通る面が作れるので、
    // 棚を出した時点ですでに下見が出ている。
    if (!Explain("標準面のままでも下見が出ている", window.WorkPlanePreviewShown())) {
        return false;
    }

    // 「基準平面から離す」で作り方と数を決める(V2SelfTestPlanes.cpp
    // CaseDockOffsetsFromComboPlane と同じ道)。SetChoice が棚の欄を一度に書き換え、
    // 書き終えたところで1回だけ変更を知らせる。
    const auto front = kachakacha::v2::app::OriginPlaneId(
        window.Session().GetDocument().Snapshot(), StandardPlaneKind::ZX);
    if (!Explain("front_XZ がある", front.has_value())) {
        return false;
    }
    WorkPlaneChoice choice;
    choice.method = WorkPlaneMethod::OffsetFromPlane;
    choice.referencePlaneId = front;
    choice.offsetMm = 15.0;
    dock->SetChoice(choice);
    if (!Explain((std::string("押せる(") + dock->NeedsText().toStdString() + ")").c_str(),
            dock->CanCreate())) {
        return false;
    }
    if (!Explain("平面の下見が出る", window.WorkPlanePreviewShown())) {
        return false;
    }
    if (!Explain("3Dに下見の線がある", !window.Viewport().ToolPreview().empty())) {
        return false;
    }

    const int before = CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane);
    dock->PressCreate();
    if (!Explain((std::string("平面が増える(") + window.StatusText().toStdString()
                     + ")").c_str(),
            CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane) == before + 1)) {
        return false;
    }
    return Explain("作ったら下見は消える", !window.WorkPlanePreviewShown());
}

} // namespace

std::vector<SelfTestCase> WorkPlanePreviewCases()
{
    return {
        {"HP-WP-01 作業面の棚で作り方を選ぶと平面の下見が出て、作ると消える",
            &CaseShelfPreviewShowsAndHidesOnCreate},
    };
}

} // namespace kachakacha::v2::selftest
