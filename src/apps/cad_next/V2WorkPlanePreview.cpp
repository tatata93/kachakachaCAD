//! 作業平面の棚を開いている間、作る前の平面を下見に出す(D-24)。
//!
//! いままでは棚で欄を決めても、「平面を作る」を押すまでどんな平面ができるのか
//! 3D では見えなかった。ここでは棚が見えていて、欄から平面が組み立てられる間だけ、
//! 原点を中心に 40mm 四方の四角を作業平面の u/v 軸で描き、押す前に向きと位置を
//! 見せる。文書には一切書かない(下見はいつでも消せる)。
//!
//! 組み立てられない・棚が見えない・平面ができたときは、黙って何も出さない。
//! それらしい平面をでっち上げるくらいなら、何も見せない方がよい。

#include "V2MainWindow.h"

#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <QString>

#include <utility>
#include <vector>

//! 下見の四角の半幅(mm)。40mm 四方。
namespace {
constexpr double kWorkPlanePreviewHalfSizeMm = 20.0;
}

void V2MainWindow::RefreshWorkPlanePreview()
{
    using kachakacha::v2::app::Shelf;
    using kachakacha::v2::geometry::Vector3;
    using kachakacha::v2::modeling::BuildWorkPlane;

    if (viewport_ == nullptr) {
        return;
    }
    // 棚が見えていなければ、自分が出したものだけ片づける。
    // (HideToolPreview は面取り/丸めの下見とも共有しているので、
    //  自分が出していないときには呼ばない。)
    const bool shelfVisible = workPlaneDock_ != nullptr && ShelfShown(Shelf::WorkPlane);
    if (!shelfVisible) {
        if (workPlanePreviewShown_) {
            workPlanePreviewShown_ = false;
            viewport_->HideToolPreview();
        }
        return;
    }

    // 棚の欄から、確定と同じ道で平面を組み立てる。診断は出さない(まだ下見なので)。
    const WorkPlaneChoice choice = workPlaneDock_->Choice();
    const auto request = kachakacha::v2::app::BuildWorkPlaneRequest(choice,
        CollectWorkPlaneMaterials(choice));
    if (!request.HasValue()) {
        if (workPlanePreviewShown_) {
            workPlanePreviewShown_ = false;
            viewport_->HideToolPreview();
        }
        return;
    }
    const auto built = BuildWorkPlane(request.Value(),
        session_->GetDocument().Snapshot().settings.tolerance);
    if (!built.HasValue()) {
        if (workPlanePreviewShown_) {
            workPlanePreviewShown_ = false;
            viewport_->HideToolPreview();
        }
        return;
    }

    // 平面の u/v で 40mm 四方の四角を作る。閉じた輪にするため始点をもう一度足す。
    const auto& frame = built.Value();
    const double half = kWorkPlanePreviewHalfSizeMm;
    std::vector<Vector3> loop{
        frame.PointAt(-half, -half),
        frame.PointAt(half, -half),
        frame.PointAt(half, half),
        frame.PointAt(-half, half),
        frame.PointAt(-half, -half),
    };
    const bool wasShown = workPlanePreviewShown_;
    workPlanePreviewShown_ = true;
    viewport_->ShowToolPreview(std::vector<std::vector<Vector3>>{std::move(loop)});
    if (!wasShown) {
        // 出たり消えたりのたびに言うと帯がうるさくなるので、新しく見えたときだけ。
        SetStatus(QStringLiteral(
            "作業面(下見): 「平面を作る」で文書へ入れます。"));
    }
}
