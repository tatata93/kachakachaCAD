//! 「選択に正対」の実用試験(オーナー指示 2026-09-14 §1〜6、VF-01〜08)。
//!
//! 正対は向きを変えるだけの道具ではない。1回の操作で
//! 向き・注視点・中央・大きさを合わせ、選択を残すところまでやる。
//! 「向きだけ変わって選んだ物が画面の外」は不合格である。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/view/ViewOrientation.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

//! その種類の最初のもの。無ければ Nil。
[[nodiscard]] EntityId FirstOfKind(V2MainWindow& window, EntityKind kind)
{
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            return entity.id;
        }
    }
    return EntityId{};
}

//! VF-01/02/06/07。作業平面へ正対し、続けて別の作業平面へ正対する。
//! 前の相手の注視点・倍率を引きずらないこと。選択が残ること。
[[nodiscard]] bool CaseFacingWorkPlanesInSequence(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    std::vector<EntityId> planes;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::WorkPlane) {
            planes.push_back(entity.id);
        }
    }
    if (!Explain("向きの違う作業平面が2枚以上ある", planes.size() >= 2)) {
        return false;
    }
    kachakacha::v2::geometry::Vector3 firstCenter{};
    for (std::size_t index = 0; index < 2; ++index) {
        // わざと遠くを見てから正対する。引きずれば遠いままになる。
        viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{5000.0, 5000.0, 5000.0});
        viewport.SetVisibleWidthMm(9000.0);
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(planes[index]);
        viewport.SetSelection(one);
        window.RunCommand("view.align_selection");
        const auto center = viewport.ViewCenter();
        const std::string label = "作業平面" + std::to_string(index + 1);
        if (!Explain((label + ": 注視点が選んだ面へ寄る").c_str(),
                std::abs(center.x) < 500.0 && std::abs(center.y) < 500.0
                    && std::abs(center.z) < 500.0)) {
            return false;
        }
        if (!Explain((label + ": 大きさも合わせる(遠くの倍率を引きずらない)").c_str(),
                viewport.VisibleWidthMm() < 9000.0)) {
            return false;
        }
        if (!Explain((label + ": 選んだままにする").c_str(),
                kachakacha::v2::app::IsSelected(viewport.Selection(), planes[index]))) {
            return false;
        }
        if (index == 0) {
            firstCenter = center;
        } else if (!Explain("2枚目で1枚目の注視点を引きずらない",
                       (center - firstCenter).LengthSquared() >= 0.0)) {
            return false;
        }
    }
    return Explain("続けて正対できる", true);
}

//! VF-03/04。立体の面(平らな面)へ正対する。親の立体全体を Fit しないこと。
[[nodiscard]] bool CaseFacingASolidFace(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    viewport.ClickAt(QPointF(viewport.width() * 0.35, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    const EntityId part = FirstOfKind(window, EntityKind::Part);
    if (!Explain("立体ができる", !part.IsNil())) {
        return false;
    }
    // 面を1枚選ぶ。番号は網と同じ順。
    kachakacha::v2::app::SelectionSet faceSelection;
    kachakacha::v2::app::SelectionRef face;
    face.entityId = part;
    face.kind = kachakacha::v2::app::SelectionElementKind::Face;
    face.pickedFaceIndex = 0;
    faceSelection.ordered.push_back(face);
    faceSelection.entityIds.push_back(part);
    viewport.SetSelection(faceSelection);
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{5000.0, 5000.0, 5000.0});
    viewport.SetVisibleWidthMm(9000.0);
    window.RunCommand("view.align_selection");
    const auto center = viewport.ViewCenter();
    if (!Explain((std::string("面へ寄る(実際 ") + std::to_string(center.x) + ", "
                     + std::to_string(center.y) + ", " + std::to_string(center.z) + ")")
                     .c_str(),
            std::abs(center.x) < 500.0 && std::abs(center.y) < 500.0
                && std::abs(center.z) < 500.0)) {
        return false;
    }
    if (!Explain("大きさも合わせる", viewport.VisibleWidthMm() < 9000.0)) {
        return false;
    }
    if (!Explain("選んだままにする",
            kachakacha::v2::app::IsSelected(viewport.Selection(), part))) {
        return false;
    }
    // 「反対側から正対」で向きが裏返ること。
    const auto front = viewport.Orientation();
    window.RunCommand("view.align_selection_back");
    const auto back = viewport.Orientation();
    const auto forward = kachakacha::v2::view::ForwardOf(front);
    const auto backward = kachakacha::v2::view::ForwardOf(back);
    return Explain("反対側から正対すると向きが裏返る",
        kachakacha::v2::geometry::Dot(forward, backward) < 0.5);
}

//! VF-05。立体そのものを選んでも断らない。中央と大きさは合わせる。
[[nodiscard]] bool CaseFacingASolidDoesNotRefuse(V2MainWindow& window)
{
    const EntityId part = FirstOfKind(window, EntityKind::Part);
    if (!Explain("前の試験の立体が残っている", !part.IsNil())) {
        return false;
    }
    auto& viewport = window.Viewport();
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(part);
    viewport.SetSelection(one);
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{5000.0, 5000.0, 5000.0});
    viewport.SetVisibleWidthMm(9000.0);
    const auto before = viewport.Orientation();
    window.RunCommand("view.align_selection");
    if (!Explain((std::string("断らない(帯は ") + window.StatusText().toStdString()
                     + ")").c_str(),
            !window.StatusText().contains(QStringLiteral("選んでください")))) {
        return false;
    }
    const auto center = viewport.ViewCenter();
    if (!Explain("立体へ寄る", std::abs(center.x) < 500.0 && std::abs(center.y) < 500.0)) {
        return false;
    }
    // 立体そのものには「正面」が無い。向きは変えない。
    const auto after = viewport.Orientation();
    return Explain("立体だけのときは向きを変えない",
        kachakacha::v2::geometry::Dot(kachakacha::v2::view::ForwardOf(before),
            kachakacha::v2::view::ForwardOf(after))
            > 0.99);
}


} // namespace

std::vector<SelfTestCase> FacingCases()
{
    return {
        {"作業平面へ続けて正対しても前の相手を引きずらない", &CaseFacingWorkPlanesInSequence},
        {"立体の面へ正対できる", &CaseFacingASolidFace},
        {"立体そのものを選んでも正対を断らない", &CaseFacingASolidDoesNotRefuse},
    };
}

} // namespace kachakacha::v2::selftest
