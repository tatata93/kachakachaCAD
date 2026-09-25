#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2DrawingDock.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/Selection.h"
#include <QComboBox>
#include <QCheckBox>
#include <QTreeWidgetItem>
#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <Qt>

namespace kachakacha::v2::selftest {
namespace {
bool Click(V2MainWindow& window, const char* name)
{
    auto* button = window.findChild<QPushButton*>(QString::fromLatin1(name));
    if (button == nullptr || !button->isEnabled()) { return Explain(name, false); }
    button->click();
    return true;
}

void DrawLine(V2MainWindow& window, double x0, double y0, double x1, double y1)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter({});
    viewport.SetVisibleWidthMm(200);
    window.SelectTool(modeling::DrawingTool::Line);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * x0, viewport.height() * y0));
    viewport.HoverAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.ClickAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(modeling::DrawingTool::Select);
}

void SelectWires(V2MainWindow& window)
{
    window.Viewport().SetSelection(app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), domain::EntityKind::Wire));
}

bool Boundary(V2MainWindow& window)
{
    DrawLine(window, .3, .3, .7, .3);
    DrawLine(window, .7, .3, .7, .7);
    DrawLine(window, .7, .7, .3, .7);
    DrawLine(window, .3, .7, .3, .3);
    SelectWires(window);
    const auto before = window.Session().GetDocument().Revision();
    window.RunCommand("surface.gpt_create");
    if (!Explain("GPT operation panel is shown", window.ShelfShown(app::Shelf::GptSurface))) { return false; }
    if (!Click(window, "gptSurfacePreview")) { return false; }
    const auto* status = window.findChild<QLabel*>(QStringLiteral("gptSurfaceStatus"));
    if (status != nullptr) { Note(status->text().toStdString().c_str()); }
    QApplication::processEvents();
    const auto imagePath = QDir::tempPath() + QStringLiteral("/kachakacha-gpt-preview.png");
    if (window.grab().save(imagePath)) { Note(imagePath.toStdString().c_str()); }
    if (!Explain("preview has filled faces", window.Viewport().ToolPreviewFaceCount() > 0)
        || !Explain("preview does not mutate document", window.Session().GetDocument().Revision() == before)
        || !Click(window, "gptSurfaceConfirm")) { return false; }
    if (!Explain("one GPT surface", CountOfKind(window, domain::EntityKind::GuideSurface) == 1)
        || !Explain("all source wires retained", CountOfKind(window, domain::EntityKind::Wire) == 4)) { return false; }
    window.RunCommand("edit.undo");
    if (!Explain("one undo removes only the surface", CountOfKind(window, domain::EntityKind::GuideSurface) == 0
        && CountOfKind(window, domain::EntityKind::Wire) == 4)) { return false; }
    window.RunCommand("edit.redo");
    if (!Explain("redo rebuilds GPT shape", window.KernelShapeCount() == 1)) { return false; }
    return Explain("save/reopen rebuilds GPT shape", window.SaveAndReopen(QStringLiteral("gpt_surface.kcd2"))
        && window.KernelShapeCount() == 1 && window.Viewport().ShapeTriangleCount() > 0);
}

bool Sections(V2MainWindow& window)
{
    DrawLine(window, .3, .35, .7, .35);
    DrawLine(window, .3, .65, .7, .65);
    SelectWires(window);
    window.RunCommand("surface.gpt_create");
    auto* mode = window.findChild<QComboBox*>(QStringLiteral("gptSurfaceMethod"));
    if (mode == nullptr) { return false; }
    mode->setCurrentIndex(1);
    if (!Click(window, "gptSurfacePreview") || !Click(window, "gptSurfaceConfirm")) { return false; }
    return Explain("two sections produce a surface", CountOfKind(window, domain::EntityKind::GuideSurface) == 1
        && window.KernelShapeCount() == 1);
}

bool CancelAndReject(V2MainWindow& window)
{
    DrawLine(window, .3, .3, .7, .3);
    SelectWires(window);
    const auto before = window.Session().GetDocument().Revision();
    window.RunCommand("surface.gpt_create");
    if (!Click(window, "gptSurfacePreview")) { return false; }
    auto* confirm = window.findChild<QPushButton*>(QStringLiteral("gptSurfaceConfirm"));
    if (!Explain("open boundary cannot commit", confirm != nullptr && !confirm->isEnabled())) { return false; }
    if (!Click(window, "gptSurfaceCancel")) { return false; }
    return Explain("cancel keeps document", window.Session().GetDocument().Revision() == before
        && window.Viewport().ToolPreviewFaceCount() == 0 && window.Viewport().ToolRoleLabels().empty());
}

bool PickCompositeSections(V2MainWindow& window)
{
    DrawLine(window, .3, .35, .5, .35);
    DrawLine(window, .5, .35, .7, .35);
    DrawLine(window, .3, .65, .5, .65);
    DrawLine(window, .5, .65, .7, .65);
    std::vector<geometry::Vector3> midpoints;
    for (const auto& curve : window.Session().Scene().curves) { midpoints.push_back(curve.segment.Evaluate(.5)); }
    if (!Explain(("four source segments: " + std::to_string(midpoints.size())).c_str(), midpoints.size() == 4)) { return false; }
    window.Viewport().SetSelection({});
    window.RunCommand("surface.gpt_create");
    auto* method = window.findChild<QComboBox*>(QStringLiteral("gptSurfaceMethod"));
    auto* role = window.findChild<QComboBox*>(QStringLiteral("gptSurfaceRole"));
    auto* list = window.findChild<QTreeWidget*>(QStringLiteral("gptSurfaceInputs"));
    if (method == nullptr || role == nullptr || list == nullptr) { return false; }
    method->setCurrentIndex(1);
    const auto pick = [&](std::size_t index) {
        const auto screen = window.Viewport().Mapping().Project(midpoints[index]);
        if (!screen.has_value()) { return false; }
        window.Viewport().SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    };
    for (int row = 0; row < 2; ++row) {
        role->setCurrentIndex(2);
        if (!pick(static_cast<std::size_t>(row * 2)) || !Explain(("new section row " + std::to_string(row)
            + " has count " + std::to_string(list->topLevelItemCount())).c_str(), list->topLevelItemCount() == row + 1)) {
            const auto* status = window.findChild<QLabel*>(QStringLiteral("gptSurfaceStatus"));
            if (status != nullptr) { Note(status->text().toStdString().c_str()); }
            return false;
        }
        list->setCurrentItem(list->topLevelItem(row));
        role->setCurrentIndex(3);
        if (!pick(static_cast<std::size_t>(row * 2 + 1))) { return false; }
    }
    if (!Explain("clicks append to two composite section rows", list->topLevelItemCount() == 2)) { return false; }
    if (!Click(window, "gptSurfaceUp") || !Explain("selection follows reordered section",
        list->indexOfTopLevelItem(list->currentItem()) == 0) || !Click(window, "gptSurfaceDown")) { return false; }
    if (!Click(window, "gptSurfacePreview") || !Click(window, "gptSurfaceConfirm")) { return false; }
    return Explain("picked composite sections produce a surface", CountOfKind(window, domain::EntityKind::GuideSurface) == 1);
}
bool AutomaticBranchAndMarks(V2MainWindow& window)
{
    DrawLine(window, .3, .3, .7, .3);
    DrawLine(window, .7, .3, .7, .7);
    DrawLine(window, .7, .7, .3, .7);
    DrawLine(window, .3, .7, .3, .3);
    DrawLine(window, .3, .3, .7, .7);
    const auto interior = window.Session().Scene().curves.back();
    SelectWires(window);
    window.RunCommand("surface.gpt_create");
    auto* automatic = window.findChild<QCheckBox*>(QStringLiteral("gptSurfaceAutomatic"));
    auto* list = window.findChild<QTreeWidget*>(QStringLiteral("gptSurfaceInputs"));
    auto* next = window.findChild<QPushButton*>(QStringLiteral("gptSurfaceNextBoundary"));
    if (automatic == nullptr || list == nullptr || next == nullptr) { return false; }
    if (!Explain("automatic classification is the default", automatic->isChecked())
        || !Explain("all five inputs shown", list->topLevelItemCount() == 5)
        || !Explain("other loops available", next->isEnabled())
        || !Explain("branch becomes interior", list->topLevelItem(4)->text(0).contains(QStringLiteral("通る線")))) { return false; }
    if (!Click(window, "gptSurfaceNextBoundary")) { return false; }
    auto* confirm = window.findChild<QPushButton*>(QStringLiteral("gptSurfaceConfirm"));
    if (!Explain("alternative keeps all inputs and refuses outside constraints", list->topLevelItemCount() == 5
        && confirm != nullptr && !confirm->isEnabled())) { return false; }
    automatic->setChecked(false);
    automatic->setChecked(true);
    const auto& marks = window.Viewport().ToolRoleLabels();
    if (!Explain("five numbered direction markers", marks.size() == 5 && marks.front().arrowToward.has_value())) { return false; }
    if (!Click(window, "gptSurfacePreview")
        || !Explain("branched selection makes a face", window.Viewport().ToolPreviewFaceCount() > 0)) { return false; }
    const auto screen = window.Viewport().Mapping().Project(interior.segment.Evaluate(.5));
    if (!screen.has_value()) { return false; }
    window.Viewport().SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    if (!Explain("picking registered interior selects matching row", list->indexOfTopLevelItem(list->currentItem()) == 4)) { return false; }
    const auto before = window.Viewport().ToolRoleLabels().back();
    if (!Explain("selected input is emphasized", before.emphasized) || !Click(window, "gptSurfaceReverse")) { return false; }
    const auto after = window.Viewport().ToolRoleLabels().back();
    if (!Explain("manual edits disable inference", !automatic->isChecked())
        || !Explain("arrow reverses on screen", before.arrowToward.has_value() && after.arrowToward.has_value()
            && (*before.arrowToward - *after.arrowToward).Length() > 0.1)) { return false; }
    if (!Click(window, "gptSurfacePreview")) { return false; }
    QApplication::processEvents();
    window.grab().save(QDir::tempPath() + QStringLiteral("/kachakacha-gpt-auto-marks.png"));
    if (!Click(window, "gptSurfaceConfirm")) { return false; }
    return Explain("confirmation keeps wires and removes temporary marks", CountOfKind(window, domain::EntityKind::Wire) == 5
        && CountOfKind(window, domain::EntityKind::GuideSurface) == 1 && window.Viewport().ToolRoleLabels().empty()
        && !window.Viewport().RoleColorOf(interior.entityId).has_value());
}

bool SmoothHeadShell(V2MainWindow& window)
{
    using app::DirectWireKind;
    using geometry::Vector3;
    window.resize(1440,900);
    QApplication::processEvents();
    const auto wire = [&](DirectWireKind kind, std::vector<Vector3> points) {
        app::DirectWireRequest request;
        request.kind=kind; request.points=std::move(points);
        window.DrawingDock().SetDirectWire(request, QStringLiteral("形状線"));
        window.DrawingDock().PressCreateWire();
    };
    modeling::WorkPlaneFrame top;
    window.Viewport().SetWorkPlane(top);
    wire(DirectWireKind::PlanarArc, {{-3.5,1.936491673,0},{0,2.5,0},{3.5,1.936491673,0}});
    wire(DirectWireKind::PlanarArc, {{-3.5,1.936491673,0},{-4.581139,1.224745,0},{-5,0,0}});
    wire(DirectWireKind::PlanarArc, {{5,0,0},{4.581139,1.224745,0},{3.5,1.936491673,0}});
    wire(DirectWireKind::SpatialLine, {{5,0,0},{3,0,3}});
    wire(DirectWireKind::SpatialLine, {{-5,0,0},{-3,0,3}});
    wire(DirectWireKind::SpatialLine, {{3,0,3},{0,0,3.5}});
    wire(DirectWireKind::SpatialLine, {{0,0,3.5},{-3,0,3}});
    modeling::WorkPlaneFrame side;
    side.uAxis={0,1,0};side.vAxis={0,0,1};side.normal={1,0,0};
    window.Viewport().SetWorkPlane(side);
    wire(DirectWireKind::PlanarArc, {{0,3.5,0},{2.105897,2.361355,0},{2.5,0,0}});
    window.Viewport().SetWorkPlane(top);
    if (!Explain("eight owner shape wires", CountOfKind(window,domain::EntityKind::Wire)==8)) { return false; }
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().SetViewCenter({0,1,1.5});
    window.Viewport().SetVisibleWidthMm(14);
    SelectWires(window);
    window.RunCommand("surface.gpt_create");
    if (!Click(window,"gptSurfacePreview")) { return false; }
    if (!Explain("smooth shell preview",window.Viewport().ToolPreviewFaceCount()>0)) {
        Note(window.findChild<QLabel*>(QStringLiteral("gptSurfaceStatus"))->text().toStdString().c_str());
        return false;
    }
    QApplication::processEvents();
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-gpt-smooth-preview.png"));
    if (!Click(window,"gptSurfaceConfirm")) { return false; }
    window.Viewport().SetSelection({});
    QApplication::processEvents();
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-gpt-smooth-shell.png"));
    const auto& shapes=window.Viewport().ShapeViews();
    return Explain("one open smooth shell retaining all source wires", shapes.size()==1 && !shapes.front().mesh.closed
        && shapes.front().mesh.faceCount==1 && CountOfKind(window,domain::EntityKind::Wire)==8);
}

}
std::vector<SelfTestCase> GptSurfaceCases()
{
    return {{"HP-GPT-01 外周・下見・確定・Undo/Redo・保存再読込", Boundary},
        {"HP-GPT-02 複数断面から面を作る", Sections},
        {"HP-GPT-03 不完全な外周を拒否・取消で文書不変", CancelAndReject},
        {"HP-GPT-04 クリックで複合断面を追加・並べ替え・確定", PickCompositeSections},
        {"HP-GPT-05 分岐から外周自動判定・番号矢印・クリック修正", AutomaticBranchAndMarks},
        {"HP-GPT-06 前頭部の滑らかな開いた殻・面表示", SmoothHeadShell}};
}
}
