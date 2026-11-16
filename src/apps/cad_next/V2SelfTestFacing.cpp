//! 「選択に正対」の実用試験(オーナー指示 2026-09-14 §1〜6、VF-01〜08)。
//!
//! 正対は向きを変えるだけの道具ではない。1回の操作で
//! 向き・注視点・中央・大きさを合わせ、選択を残すところまでやる。
//! 「向きだけ変わって選んだ物が画面の外」は不合格である。

#include "V2SelfTest.h"

#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/view/ViewOrientation.h"

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointF>
#include <QPushButton>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using geometry::Vector3;

void Mouse(QWidget& widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, widget.mapToGlobal(at.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void Click(QWidget& widget, QPointF at)
{
    Mouse(widget, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    Mouse(widget, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
}

void Key(QWidget& widget, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

std::optional<QRectF> GadgetBox(V2Viewport& viewport, view::ViewGadgetKind kind,
    view::ViewGadgetDirection direction = view::ViewGadgetDirection::Positive)
{
    for (const auto& g : viewport.ViewGadgets().gadgets) {
        if (g.kind == kind && g.direction == direction) return QRectF(g.xPx, g.yPx, g.widthPx, g.heightPx);
    }
    return std::nullopt;
}

void UseObliquePlane(V2MainWindow& window)
{
    window.SetWorkPlaneChooser([](WorkPlaneChoice choice, const app::WorkPlaneFacts&) {
        choice.method = modeling::WorkPlaneMethod::PointNormal;
        choice.origin = Vector3{};
        choice.normal = Vector3{1, 2, 4};
        choice.uAxis = Vector3{1, 0, 0};
        return std::optional(choice);
    });
    window.RunCommand("workplane.create");
}

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

//! 立体を1つ作る。前の試験の残りに頼らない。
[[nodiscard]] EntityId MakeOneSolid(V2MainWindow& window, bool oblique = false)
{
    window.RunCommand("file.new");
    if (oblique) UseObliquePlane(window);
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
    // 厚みをここで決める。**押し始めの距離は輪郭の大きさで変わる**ので、
    // 決めずにおくと、この試験が「向きを変えないか」ではなく
    // 「押し始めの距離がいくつか」を見ることになる。
    window.ExtrudeDock().TypeDistanceMm(0.5);
    window.RunCommand("part.extrude");
    return FirstOfKind(window, EntityKind::Part);
}

//! VF-03/04。立体の面(平らな面)へ正対する。親の立体全体を Fit しないこと。
[[nodiscard]] bool CaseFacingASolidFace(V2MainWindow& window)
{
    const EntityId part = MakeOneSolid(window);
    auto& viewport = window.Viewport();
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
    const EntityId part = MakeOneSolid(window);
    if (!Explain("立体を作れる", !part.IsNil())) {
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


//! Q1 の契約(Codex Q1-Q5 の UX 指摘)。向きの違う相手を複数選んだとき、
//! **向きは最初の1つが決め、収まりは全部が決める。** そしてそれを帯で言う。
//! 黙ってどれか1つの向きになるのが、いちばん困る。
[[nodiscard]] bool CaseFacingManyTargetsFollowsTheFirst(V2MainWindow& window)
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
    // まず1枚目だけに正対して、その向きを覚える。
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(planes[0]);
    viewport.SetSelection(one);
    window.RunCommand("view.align_selection");
    const auto onlyFirst = kachakacha::v2::view::ForwardOf(viewport.Orientation());

    // 2枚目だけに正対すると、別の向きになるはずである。
    kachakacha::v2::app::SelectionSet other;
    other.entityIds.push_back(planes[1]);
    viewport.SetSelection(other);
    window.RunCommand("view.align_selection");
    const auto onlySecond = kachakacha::v2::view::ForwardOf(viewport.Orientation());
    if (!Explain("2枚は向きが違う",
            std::abs(kachakacha::v2::geometry::Dot(onlyFirst, onlySecond)) < 0.9)) {
        return false;
    }

    // 両方を選ぶ。順番は1枚目が先。向きは1枚目に合う。
    viewport.SetViewDirection(ViewDirection::Isometric);
    kachakacha::v2::app::SelectionSet both;
    both.entityIds.push_back(planes[0]);
    both.entityIds.push_back(planes[1]);
    viewport.SetSelection(both);
    window.RunCommand("view.align_selection");
    const auto together = kachakacha::v2::view::ForwardOf(viewport.Orientation());
    if (!Explain((std::string("向きは最初の1つに合う(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            kachakacha::v2::geometry::Dot(together, onlyFirst) > 0.99)) {
        return false;
    }
    // 混じっていたことを黙っていない。
    if (!Explain("向きが混じっていたと帯で言う",
            window.StatusText().contains(QStringLiteral("向きの違うものが混じっていた")))) {
        return false;
    }
    // 収まりは全部が決める。選択も残る。
    return Explain("選んだものは選ばれたまま",
        viewport.Selection().entityIds.size() == 2);
}

bool ObliqueFaceButtons(V2MainWindow& window)
{
    window.resize(1500, 900);
    QApplication::processEvents();
    const auto part = MakeOneSolid(window, true);
    auto& viewport = window.Viewport();
    const auto plane = viewport.WorkPlane();
    const auto planeId = window.ActiveWorkPlaneId();
    const auto& shapes = viewport.ShapeViews();
    const auto shape = std::find_if(shapes.begin(), shapes.end(), [&](const auto& s) { return s.entityId == part; });
    if (!Explain("斜めの立体を作成", shape != shapes.end() && std::abs(plane.normal.z) < 0.99)) return false;
    const auto& triangles = shape->mesh.triangles;
    const auto cap = std::find_if(triangles.begin(), triangles.end(), [&](const auto& face) {
        return Dot(face.normal, plane.normal) > 0.99;
    });
    if (!Explain("上側の平面フェイス", cap != triangles.end())) return false;
    const auto faceIndex = cap->faceIndex;
    viewport.SetOrientation(view::OrientationFacing(plane.normal + plane.uAxis, plane.vAxis).Value());
    const auto point = cap->Center();
    viewport.SetViewCenter(point);
    viewport.SetVisibleWidthMm(200.0);
    window.RunCommand("selection.activate");
    viewport.SetSelection({});
    QApplication::processEvents();
    const auto target = viewport.Mapping().Project(point);
    if (!target) return false;
    Mouse(viewport, QEvent::MouseMove, {target->x, target->y}, Qt::NoButton, Qt::NoButton);
    Click(viewport, {target->x, target->y});
    const auto selectedFace = [&] {
        const auto& refs = viewport.Selection().ordered;
        return refs.size() == 1 && refs.front().entityId == part && refs.front().pickedFaceIndex == faceIndex;
    };
    if (!Explain("3Dクリックで斜めの面を選択", selectedFace())) return false;
    const auto front = GadgetBox(viewport, view::ViewGadgetKind::AlignSelection);
    const auto back = GadgetBox(viewport, view::ViewGadgetKind::AlignSelection, view::ViewGadgetDirection::Negative);
    if (!front || !back) return false;
    const auto revision = window.Session().GetDocument().Revision();
    Click(viewport, front->center());
    const auto forward = view::ForwardOf(viewport.Orientation());
    const auto center = viewport.ViewCenter();
    if (!Explain("正対の方向と収まり", Dot(forward, plane.normal) < -0.999999 && viewport.VisibleWidthMm() < 200.0)) return false;
    viewport.SetOrientation(view::OrientationFacing(-forward, plane.uAxis + plane.vAxis).Value());
    const auto up = view::UpOf(viewport.Orientation());
    Click(viewport, back->center());
    if (!Explain("逆対で注視点と画面の上下を保持", Dot(view::ForwardOf(viewport.Orientation()), forward) < -0.999999
        && Dot(view::UpOf(viewport.Orientation()), up) > 0.999999 && (viewport.ViewCenter() - center).Length() < 1.0e-8)) return false;
    Click(viewport, back->center());
    return Explain("もう一度逆対しても選択と作業面を保持", Dot(view::ForwardOf(viewport.Orientation()), forward) > 0.999999
        && selectedFace() && window.ActiveWorkPlaneId() == planeId && window.Session().GetDocument().Revision() == revision);
}

bool ScreenHorizontal(V2MainWindow& window)
{
    window.resize(1440, 840);
    QApplication::processEvents();
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter({});
    viewport.SetVisibleWidthMm(100);
    window.SelectTool(modeling::DrawingTool::Line);
    if (!window.Session().PlacePoint({15, -15, 0}).placedPoint) return false;
    const auto button = GadgetBox(viewport, view::ViewGadgetKind::AlignHorizontal);
    if (!button) return false;
    const auto before = viewport.Orientation();
    const auto revision = window.Session().GetDocument().Revision();
    const QPointF a(viewport.width() * 0.25, viewport.height() * 0.67);
    const QPointF b(viewport.width() * 0.62, viewport.height() * 0.47);
    const auto map = viewport.Mapping();
    const auto p = map.UnprojectOntoPlane({a.x(), a.y()}, {}, {0, 0, 1});
    const auto q = map.UnprojectOntoPlane({b.x(), b.y()}, {}, {0, 0, 1});
    if (!p || !q) return false;
    Click(viewport, button->center());
    Click(viewport, a);
    Click(viewport, a);
    if (!Explain("同じ場所を続けて押しても確定しない", viewport.DirectionPickPending())) return false;
    Key(viewport, Qt::Key_Escape);
    if (!Explain("Escで途中の水平線指定だけを取り消す", !viewport.DirectionPickPending()
        && window.Session().PlacedPointCount() == 1)) return false;
    Click(viewport, button->center());
    Click(viewport, a);
    Mouse(viewport, QEvent::MouseMove, b, Qt::NoButton, Qt::NoButton);
    const auto pixmap = viewport.grab();
    const auto dpi = pixmap.devicePixelRatio();
    const QPoint onGuide = ((a + b) * (0.5 * dpi)).toPoint();
    const auto image = pixmap.toImage();
    bool guideVisible = false;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const auto pixel = image.pixelColor(onGuide + QPoint(x, y));
            const auto color = viewport.Colors().selected;
            if (std::abs(pixel.red() - color.red()) + std::abs(pixel.green() - color.green())
                + std::abs(pixel.blue() - color.blue()) < 30) guideVisible = true;
        }
    }
    if (!Explain("1点目からカーソルまで選択色のガイド線", guideVisible)) return false;
    const auto proof = qEnvironmentVariable("KACHACAD_VIEW_ALIGNMENT_PROOF_DIR");
    if (!proof.isEmpty()) {
        QDir dir(proof);
        dir.mkpath(".");
        window.grab().save(dir.filePath("view.align_horizontal.png"));
        pixmap.copy(QRectF(button->topLeft() * dpi, button->size() * dpi).toAlignedRect())
            .save(dir.filePath("view.align_horizontal-icon.png"));
    }
    Click(viewport, b);
    const auto first = viewport.Mapping().Project(*p);
    const auto last = viewport.Mapping().Project(*q);
    return Explain("空白2点で画面の回転だけを変え、作図途中とモデルを保持", !viewport.DirectionPickPending()
        && first && last && std::abs(first->y - last->y) < 1.0e-6
        && Dot(view::ForwardOf(before), view::ForwardOf(viewport.Orientation())) > 0.999999
        && viewport.ViewCenter() == Vector3{} && viewport.VisibleWidthMm() == 100.0
        && window.Session().PlacedPointCount() == 1 && window.Session().GetDocument().Revision() == revision);
}

bool GridAngle(V2MainWindow& window)
{
    window.resize(1440, 900);
    QApplication::processEvents();
    window.RunCommand("file.new");
    UseObliquePlane(window);
    auto& viewport = window.Viewport();
    const auto plane = viewport.WorkPlane();
    const auto planeId = window.ActiveWorkPlaneId();
    viewport.SetOrientation(view::OrientationFacing(plane.normal, plane.vAxis).Value());
    viewport.SetViewCenter(plane.origin);
    viewport.SetVisibleWidthMm(120);
    window.RunCommand("grid.edit");
    QApplication::processEvents();
    auto* angle = window.GridDock().findChild<QDoubleSpinBox*>("gridAngle");
    auto* fit = window.GridDock().findChild<QPushButton*>("gridPickAngle");
    if (!Explain("数値欄とフィットボタンが表示される", angle && fit && fit->isVisible())) return false;
    const auto initial = viewport.Orientation();
    const auto revision = window.Session().GetDocument().Revision();
    const auto beforePaint = viewport.grab().toImage();
    auto* editor = angle->findChild<QLineEdit*>();
    editor->setText(QStringLiteral("-30 °"));
    Key(*editor, Qt::Key_Return);
    const auto numeric = window.Session().Scene().grid;
    const auto expected = plane.uAxis * (std::sqrt(3.0) * 0.5) - plane.vAxis * 0.5;
    if (!Explain("数値入力でグリッドの描画と格子の軸が回る", numeric.angleDegrees == -30.0
        && (numeric.uDirection - expected).Length() < 1.0e-8 && viewport.grab().toImage() != beforePaint)) return false;
    viewport.SetSnapSuppressed(true);
    Click(*fit, fit->rect().center());
    const auto a = viewport.Mapping().Project(plane.PointAt(-20, -10));
    const auto b = viewport.Mapping().Project(plane.PointAt(20, 20));
    if (!a || !b || !viewport.DirectionPickPending()) return false;
    Click(viewport, {a->x, a->y});
    Key(viewport, Qt::Key_Escape);
    if (!Explain("1点入力後のEscで角度を保持", !viewport.DirectionPickPending() && angle->value() == -30.0)) return false;
    Click(*fit, fit->rect().center());
    Click(viewport, {a->x, a->y});
    Mouse(viewport, QEvent::MouseMove, {b->x, b->y}, Qt::NoButton, Qt::NoButton);
    if (const auto proof = qEnvironmentVariable("KACHACAD_VIEW_ALIGNMENT_PROOF_DIR"); !proof.isEmpty()) {
        QDir dir(proof);
        dir.mkpath(".");
        window.grab().save(dir.filePath("grid.angle.png"));
    }
    Click(viewport, {b->x, b->y});
    const auto grid = window.Session().Scene().grid;
    const auto direction = plane.uAxis * 0.8 + plane.vAxis * 0.6;
    if (!Explain("斜めの作業面上の2点にグリッドを合わせる", !viewport.DirectionPickPending()
        && (grid.uDirection - direction).Length() < 1.0e-8 && std::abs(angle->value() - 36.869898) < 1.0e-6)) return false;
    viewport.SetSnapSuppressed(false);
    window.SelectTool(modeling::DrawingTool::Line);
    const Vector3 lattice = grid.origin - grid.uDirection * grid.majorSpacingMm * 3.0 + grid.vDirection * grid.majorSpacingMm * 2.0;
    const auto near = viewport.Mapping().Project(lattice);
    if (!near) return false;
    const auto hover = window.Session().Hover({near->x + 1, near->y + 1});
    if (!Explain("回転後の主グリッド点に吸着する", hover.snap && hover.snap->kind == modeling::SnapKind::GridMajor
        && hover.position && (*hover.position - lattice).Length() < 1.0e-8)) return false;
    if (!Explain("文書・カメラ・作業面の維持", window.ActiveWorkPlaneId() == planeId
        && window.Session().GetDocument().Revision() == revision && view::AngleBetween(viewport.Orientation(), initial) < 1.0e-8
        && viewport.WorkPlane().uAxis == plane.uAxis)) return false;
    window.SelectTool(modeling::DrawingTool::Rectangle);
    if (!window.Session().PlacePoint(plane.PointAt(-11, -13)).placedPoint
        || !window.Session().PlacePoint(plane.PointAt(-8, -7)).committed) return false;
    const auto& edge = window.Session().Scene().curves.back().segment;
    const auto delta = edge.EndPoint() - edge.StartPoint();
    return Explain("矩形は作業面の軸に沿う", std::abs(Dot(delta, plane.uAxis)) < 1.0e-8
        || std::abs(Dot(delta, plane.vAxis)) < 1.0e-8);
}

} // namespace

std::vector<SelfTestCase> FacingCases()
{
    return {
        {"VIEW-ALIGN oblique face front and reverse buttons", &ObliqueFaceButtons},
        {"VIEW-ALIGN two-point screen horizontal with cursor guide and cancel", &ScreenHorizontal},
        {"VIEW-ALIGN grid angle typing and two-point fit with rotated snap", &GridAngle},
        {"作業平面へ続けて正対しても前の相手を引きずらない", &CaseFacingWorkPlanesInSequence},
        {"立体の面へ正対できる", &CaseFacingASolidFace},
        {"立体そのものを選んでも正対を断らない", &CaseFacingASolidDoesNotRefuse},
        {"向きの違う相手を複数選ぶと最初の1つの向きに合わせて全部を収める",
            &CaseFacingManyTargetsFollowsTheFirst},
    };
}

} // namespace kachakacha::v2::selftest
