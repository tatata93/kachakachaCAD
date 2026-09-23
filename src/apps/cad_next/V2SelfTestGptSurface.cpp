#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/app/Selection.h"
#include <QComboBox>
#include <QPointF>
#include <QString>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>

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
    if (!Click(window, "gptSurfacePreview")) { return false; }
    const auto* status = window.findChild<QLabel*>(QStringLiteral("gptSurfaceStatus"));
    if (status != nullptr) { Note(status->text().toStdString().c_str()); }
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
        && window.Viewport().ToolPreviewFaceCount() == 0);
}
}
std::vector<SelfTestCase> GptSurfaceCases()
{
    return {{"HP-GPT-01 外周・下見・確定・Undo/Redo・保存再読込", Boundary},
        {"HP-GPT-02 複数断面から面を作る", Sections},
        {"HP-GPT-03 不完全な外周を拒否・取消で文書不変", CancelAndReject}};
}
}
