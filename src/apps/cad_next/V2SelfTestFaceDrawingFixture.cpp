#include "V2SelfTestFaceDrawingFixture.h"
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2SurfaceDock.h"
#include <QApplication>
#include <QString>
#include <optional>
#include <vector>

namespace kachakacha::v2::selftest::facefixture {
using geometry::Vector3;
using base::EntityId;
using domain::EntityKind;
EntityId MakeGuide(V2MainWindow& w, double height, bool tilted, bool holes)
{
    const auto initialPlane = w.ActiveWorkPlaneId();
    w.SetWorkPlaneChooser([height, tilted](app::WorkPlaneChoice choice, const app::WorkPlaneFacts&) {
        choice.method = modeling::WorkPlaneMethod::PointNormal;
        choice.origin = {0, 0, height};
        choice.normal = tilted ? Vector3{-.2, -.3, 1} : Vector3{0, 0, 1};
        choice.uAxis = tilted ? Vector3{1, 0, .2} : Vector3{1, 0, 0};
        return std::optional(choice);
    });
    w.RunCommand("workplane.create");
    const auto point = [height, tilted](double x, double y) {
        return Vector3{x, y, height + (tilted ? 0.2 * x + 0.3 * y : 0.0)};
    };
    const Vector3 corners[] = {point(-25, -20), point(25, -20), point(25, 20), point(-25, 20)};
    std::vector<geometry::CurveSegment> edges;
    for (int i = 0; i < 4; ++i) edges.push_back(geometry::CurveSegment::MakeLine(corners[i], corners[(i + 1) % 4]).Value());
    const auto wire = w.AddPlainWire(edges, "作図元の輪郭", false);
    if (wire.IsNil()) return {};
    app::SelectionSet boundary{{wire}};
    if (holes) for (const auto x : {-12.0, 12.0}) {
        const auto hole = w.AddPlainWire({geometry::CurveSegment::MakeCircle(point(x, 0),
            {0, 0, 1}, {1, 0, 0}, 4).Value()}, "内側の輪郭", false);
        if (hole.IsNil()) return {};
        boundary.entityIds.push_back(hole);
    }
    if (!w.SaveAndReopen("face-grid-source.kcd2")) return {};
    w.SelectTool(modeling::DrawingTool::Select);
    w.Viewport().SetSelection(boundary);
    w.RunCommand("surface.create");
    w.ChooseSurfaceMethod(modeling::GuideSurfaceMethod::PlanarBoundary);
    if (!Explain(("元の面の下見: " + w.StatusText().toStdString()).c_str(),
            w.SurfacePreviewShown() && w.SurfaceDock().CanConfirm())) return {};
    w.SurfaceDock().PressConfirm();
    EntityId newest;
    for (const auto& entity : w.Session().GetDocument().Snapshot().entities)
        if (entity.kind == EntityKind::GuideSurface) newest = entity.id;
    w.RunCommand("selection.activate");
    w.Viewport().SetSelection(app::SelectionSet{{initialPlane}});
    w.RunCommand("workplane.set_active");
    w.Viewport().SetSelection({});
    return newest;
}

void Setup(V2MainWindow& w)
{
    w.resize(1500, 950);
    QApplication::processEvents();
    w.RunCommand("file.new");
    w.Viewport().SetViewDirection(ViewDirection::Top);
    w.Viewport().SetViewCenter({0, 0, 12});
    w.Viewport().SetVisibleWidthMm(90);
}

}
